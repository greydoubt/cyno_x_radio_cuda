// PC speaker driver, Snooze_Until Timing. Accepts and outputs 8 bit unsigned 11.025KHz mono audio data.
#include <SupportDefs.h>
#include <KernelExport.h>
#include <Drivers.h>
#include <ISA.h>

// Communication to/from driver.
static status_t speaker_open(const char *name, uint32 flags, void **cookie);
static status_t speaker_close(void *cookie);
static status_t speaker_free(void *cookie);
static status_t speaker_control(void *cookie, uint32 op, void *buf, size_t len);
static status_t speaker_read(void *cookie, off_t pos, void *buf, size_t *len);
static status_t speaker_write(void *cookie, off_t pos, const void *buf, size_t *len);

// Used to load and unload driver.
status_t init_hardware (void);
status_t init_driver(void);
void uninit_driver(void);

// Internal functions of the driver.
static status_t play_chunk(uchar *buffer, long len);

//* Internal Data.
const char **publish_devices(void);
device_hooks* find_device(const char *name);

static bool resources_flag = false; // Track if system resources are allocated.
static bool debug = false;          // Don't send debugging information.
static bool interrupt_flag = false; // Allows system interrupts during play-back of sound as default.
static int  over_sample = 0;        // Over_Sampling mode: 0 = Don't use Over_sampling.
                                    //                     1 = Use 2x Over_sampling.
                                    //                     2 = Use 4x Over_sampling.
static int  sleep_flag = 1;         // Set timing mode: 0 = Busy Wait Loop.
                                    //                  1 = Use snooze_until() to control output timing,
                                    //                  2 = Skip duplicate sample values.
static int32 open_count = 0;        // Number of open instance of the driver. 
static int32 frame_rate = 11025;    // Default sampling rate. Should be set in init_driver().

#define LATENCY 10                  // Max. Latency of the Snooze_Until() in microseconds.
static bigtime_t Latency = 0;       // Latency to use in drver (User changable in realtime).

static sem_id write_lock = -1;      // Semaphore block writes to the driver.
static isa_module_info *isa = NULL; // Module structure

#define MAX_PLAY_BUFFER_LENGTH 0x100 // Warning!!! Buffer must be small enough to be processed completely between
                                     // two sound samples, ie frame_length in microseconds.
static uchar play_buffer[MAX_PLAY_BUFFER_LENGTH]; // Area to copy the data pointed to by the write_device(*buffer)
                                    // because original data pointed to by *buffer is not considered stable.
static uchar output_buffer[MAX_PLAY_BUFFER_LENGTH * 4 + 8]; // Processed data ready to be outputted.
static uchar old_sample;            // Contains last sample from previous buffer when write_driver() first called.
static uchar new_sample;            // Used to convert samples.
static long play_buffer_length, output_buffer_length; // Number of elements in buffers.
static uchar convert_1[65540], convert_2[65540], convert_3[65540]; // Convertion arrays for over_sampling

/* api_version - This variable defines the API version to which the driver was written, and should be set to
 B_CUR_DRIVER_API_VERSION at compile time. This variable's value will be changed with every revision to the driver
 API; the value with which your driver was compiled will tell devfs how it can communicate with the driver. */

int32 api_version = B_CUR_DRIVER_API_VERSION;

const char *speaker_names[] = { "audio/PC_Speaker",NULL };

/*****************************************************************************************************************
 Device Hooks - The hook functions specified in the device_hooks function returned by the driver's find_device()
 function handle requests made by devfs (and through devfs, from user applications). These are described in this
 section.  The structure itself looks like this: 
 typedef struct { 
         device_open_hook open;       -> open entry point
         device_close_hook close;     -> close entry point
         device_free_hook free;       -> free cookie
         device_control_hook control; -> control entry point
         device_read_hook read;       -> read entry point
         device_write_hook write;     -> write entry point
         device_select_hook select;   -> select entry point
         device_deselect_hook deselect; -> deselect entry point
         device_readv_hook readv;     -> posix read entry point
         device_writev_hook writev;   -> posix write entry point
 } device_hooks;
 In all cases, return B_OK if the operation is successfully completed, or an appropriate error code if not.

 Function pointers for the device hooks entry points. */

static device_hooks speaker_device = { // NULLs=speaker_select, speaker_deselect, speaker_readv, speaker_writev
 speaker_open, speaker_close, speaker_free, speaker_control, speaker_read, speaker_write, NULL,NULL,NULL,NULL };

/*****************************************************************************************************************
 init_hardware - status_t init_hardware (void) - This function is called when the system is booted, which lets
 the driver detect and reset the hardware it controls. The function should return B_OK if the initialization is
 successful; otherwise, an appropriate error code should be returned. If this function returns an error, the
 driver won't be used.
 
 PC_Speaker notes: Since this driver uses the parallel port that is already going to be initialized by the OS, to
 avoid any conflicts in the port's hardware setup we do nothing to the system hardware here.  Turns out this is a
 good location to load parameters of a driver setting file.  Note: Found out that driver_parameter string can not
 contain spaces.  Use underlines '_' if multiple words are needed.  Frame_Rate was chosen to match the sampling
 rate of the included .WAV file (TheManSong.wav), it can be changed to match the sampling frequency of any test
 files you may like to try with the code by just editting the 'PC_Speaker_Settings' file in the folder: 
 /boot/home/config/settings/kernel/drivers/.  When the driver has finished reading in the settings and anything
 else that needs to be done in init_hardware() it must return B_OK to tell the kernel that the hardware was
 detected, otherwise the (simulated) hardware will not be useable. */

status_t init_hardware (void) {
void *settings; // Handle to the driver settings file.
const char * str_value; // Used for string value settings.
ulong num_value; // Used for string value settings.

 if (debug) dprintf("PPA: Initialize Hardware.\n");
 settings = (void *)load_driver_settings("PC_Speaker_Settings"); // Open the settings file for this driver.
            // It is located in ~/config/settings/kernel/drivers.

 if (settings) { // Test file exists.  If file does not exist variable will equal zero(0).
    if (debug) dprintf("PPA: Driver's Settings File Found.\n");

    debug = get_driver_boolean_parameter(settings, "Debug_Info:", false, false); // Debugging status.
    if (debug) { dprintf("PPA: Debug = true.\n"); } else { dprintf("PPA: Debug = false.\n"); }
            // If True =  Enables dprint() of debugging info for details of driver operation.
            // If the parameter was not found, the third parameter 'false' is returned.
            // If the parameter was found, but no value defined, the fourth parameter 'false' is returned.

    interrupt_flag = get_driver_boolean_parameter(settings, "System_Interrupts:", false, false); // Interrupt status.
    if (debug) {
       if (interrupt_flag) { 
          dprintf("PPA: Interrupts not allowed during playing of sounds.\n");
       } else {
          dprintf("PPA: Interrupts are allowed during playing of sounds.\n"); } }
            // False = System Interrupts allowed.  True = System Interrupts disabled.
            // If the parameter was not found, the third parameter 'false' is returned.
            // If the parameter was found, but no value defined, the fourth parameter 'false' is returned.

    str_value = (const char*)get_driver_parameter(settings, "Over_Sampling:", "Unknown", "NoArg");
            // If the parameter was not found, the third parameter 'Unknown' is returned.
            // If the parameter was found, but no value defined, the fourth parameter 'NoArg' is returned.
    if (strcmp(str_value,"Unknown") != 0) { // Test if parameter was found.
         if (strcmp(str_value, "NoArg") != 0) { // Test if unknown value for parameter.
            num_value = atoi(str_value); // Convert to a integer number.
            if ((num_value >=0) && (num_value <3)) { // Test in correct range of values
               over_sample = num_value;
               if (debug) dprintf("PPA: Over-Sampling %d .\n", (long)over_sample); // Valid value.
            } else {
               if (debug) dprintf("PPA: Over-Sampling parameter value out of range.\n"); }
         } else {
            if (debug) dprintf("PPA: Over-Sampling parameter value missing.\n"); }
    } else {
       if (debug) dprintf("PPA: Over-Sampling parameter not found.\n"); }

    str_value = (const char*)get_driver_parameter(settings, "Sample_Rate:", "Unknown", "NoArg");
            // If the parameter was not found, the third parameter 'Unknown' is returned.
            // If the parameter was found, but no value defined, the fourth parameter 'NoArg' is returned.
    if (strcmp(str_value,"Unknown") != 0) { // Test if parameter was found.
         if (strcmp(str_value, "NoArg") != 0) { // Test if unknown value for parameter.
            num_value = atoi(str_value); // Convert to a integer number.
            if ((num_value > 3999) && (num_value < 100001)) { // Test in correct range of values
               frame_rate = num_value;
               if (debug) dprintf("PPA: Sampling Rate %d .\n", (long)frame_rate); // Valid value.
            } else {
               if (debug) dprintf("PPA: Sampling Rate parameter value out of range.\n"); }
         } else {
            if (debug) dprintf("PPA: Sampling Rate parameter value missing.\n"); }
    } else {
       if (debug) dprintf("PPA: Sampling Rate parameter not found.\n"); }

    str_value = (const char*)get_driver_parameter(settings, "Sleep_Timing:", "Unknown", "NoArg");
            // If the parameter was not found, the third parameter 'Unknown' is returned.
            // If the parameter was found, but no value defined, the fourth parameter 'NoArg' is returned.
    if (strcmp(str_value,"Unknown") != 0) { // Test if parameter was found.
         if (strcmp(str_value, "NoArg") != 0) { // Test if unknown value for parameter.
            num_value = atoi(str_value); // Convert to a integer number.
            if ((num_value >= 0) && (num_value < 3)) { // Test in correct range of values
               sleep_flag = num_value;
               if (debug) dprintf("PPA: Sleep Timing %d .\n", (long)sleep_flag); // Valid value.
            } else {
               if (debug) dprintf("PPA: Sleep Timing parameter value out of range.\n"); }
         } else {
            if (debug) dprintf("PPA: Sleep Timing parameter value missing.\n"); }
    } else {
       if (debug) dprintf("PPA: Sleep Timing parameter not found.\n"); }

    str_value = (const char*)get_driver_parameter(settings, "Timing_Latency:", "Unknown", "NoArg");
            // If the parameter was not found, the third parameter 'Unknown' is returned.
            // If the parameter was found, but no value defined, the fourth parameter 'NoArg' is returned.
    if (strcmp(str_value,"Unknown") != 0) { // Test if parameter was found.
         if (strcmp(str_value, "NoArg") != 0) { // Test if unknown value for parameter.
            num_value = atoi(str_value); // Convert to a integer number.
            if ((num_value >= 0) && (num_value < 21)) { // Test in correct range of values
               Latency = num_value;
               if (debug) dprintf("PPA: Latency Timing %d .\n", (long)Latency); // Valid value.
            } else {
               if (debug) dprintf("PPA: Latency Timing parameter value out of range.\n"); }
         } else {
            if (debug) dprintf("PPA: Latency Timing parameter value missing.\n"); }
    } else {
       if (debug) dprintf("PPA: Latency Timing parameter not found.\n"); }

 } else {
    dprintf("PPA: Setting File *NOT* Found..\n"); }

 unload_driver_settings(settings); // Always close the settings file before exiting.
 return B_OK; } 

/*****************************************************************************************************************
 init_driver - status_t init_driver(void) - optional function - called every time the driver is loaded.  Drivers
 are loaded and unloaded on an as-needed basis. When a driver is loaded by devfs, this function is called to let
 the driver allocate memory and other needed system resources. Return B_OK if initialization succeeds, otherwise
 return an appropriate error code. <<<what happens if this returns an error? Appears to unload driver.  Note, if
 the code detects an error allocating a resource, it frees up all previous allocated resources before returning
 the error of the resource that failed. >>>
 
 PC_Speaker notes: Write_Lock's purpose is to lock out any additional writes to the driver before it has finished
 servicing the previous write to the driver.  Again since the parallel port is already going to be initialized by
 the OS, this driver function does not try to do any parallel port hardware setup here either. */

status_t init_driver(void) {
 int32 old_data = 0, new_data = 0;
 status_t ret = B_OK; if (debug) dprintf("PPA: Initializing Driver.\n");

 while ( old_data < 256 ) {
        while ( new_data < 256) { // Create Convertion data arrays for over-sampling format.
              convert_1[old_data*256+new_data] = (uchar)((old_data * 3 + new_data * 1) >> 2); // (Old*3+New*1)/4
              convert_2[old_data*256+new_data] = (uchar)((old_data * 2 + new_data * 2) >> 2); // (Old*2+New*2)/4
              convert_3[old_data*256+new_data] = (uchar)((old_data * 1 + new_data * 3) >> 2); // (Old*1+New*3)/4
              new_data++; }; new_data = 0; old_data++; }
 
 ret = get_module(B_ISA_MODULE_NAME, (module_info**) &isa);
 if (ret == B_OK) {
     if (debug) dprintf("PPA: B_ISA_MODULE_NAME ok.\n");

     write_lock = create_sem(1, "write lock"); ret = write_lock;
     if (write_lock > B_OK) {
        if (debug) dprintf("PPA: Created write_lock sem ok.\n");
        resources_flag = true; return B_OK; } // No errors creating resources.

     if (debug) dprintf("PPA: Failed to create write_lock."); // 
     put_module(B_ISA_MODULE_NAME); if (debug) dprintf("PPA: Deleted B_ISA_MODULE_NAME.\n"); 
 } else {
     if (debug) dprintf("PPA: Failed to get B_ISA_MODULE_NAME.\n"); }

 if (debug) dprintf("\n"); // Spacer in debugger output.
 return ret; }

/*****************************************************************************************************************
 uninit_driver - void uninit_driver(void) - optional function - called every time the driver is unloaded.  This
 function is called by devfs just before the driver is unloaded from memory. This lets the driver clean up after
 itself, freeing any resources it allocated.
 
 PC_Speaker notes: Since this driver uses the parallel port that is already going to be initialized by the OS, to
 avoid any conflicts in the port's hardware setup we do nothing here.  Also uninit_driver() does not return a
 status report on the freeing up of resources, you should not free up something here unless you are 100% sure that
 it will happen. */

void uninit_driver(void) {
 if (debug) dprintf("PPA: Uninitializing Driver.\n\n"); 
 return; }

/*****************************************************************************************************************
 open_hook() - status_t open_hook(const char *name, uint32 flags, void **cookie) - This hook function is called
 when a program opens one of the devices supported by the driver. The name of the device (as returned by
 publish_devices()) is passed in name, along with the flags passed to the Posix open() function. cookie points to
 space large enough for you to store a single pointer. You can use this to store state information specific to the
 open() instance. If you need to track information on a per-open() basis, allocate the memory you need and store a
 pointer to it in *cookie.

 PC_Speaker notes: I have had real problems using the cookie structure, and since it is not really needed in this
 code as this is a test driver only, I just set it to NULL and never reference it again.  Originally Open_Count
 was not used in this driver, but I do not limit Open_Count like the original code did.  Many drivers do limit
 the OS to one open instance only.  This helps avoid conflicts, but in this case I needed the ability to send
 IOCTL codes to the driver independently of the sound playing.  So since I added the ability to control
 different timing options using a re-write of Alexander G. M. Smith's AGMSDeviceTest program (called
 PC_Speaker_Controller) it became necessary to have the ability for the driver to be opened by more than one
 accessing thread at the same time. */

static status_t speaker_open(const char *name, uint32 flags, void **cookie) {
 if (debug) dprintf("PPA: Open Driver.\n\n"); 
 atomic_add(&open_count, 1); *cookie = NULL; return B_OK; }

/* PC_Speaker notes: This is what the original open_hook code looked like.
   if (atomic_add(&open_count, 1) < 1) {
      *cookie = NULL; return B_OK;
   } else {
      atomic_add(&open_count, -1); return B_ERROR; } } */

/*****************************************************************************************************************
 close_hook() - status_t close_hook(void *cookie) - This hook is called when an open instance of the driver is
 closed using the close() Posix function. Note that because of the multi-threaded nature of the BeOS, it's
 possible there may still be transactions pending, and you may receive more calls on the device. For that reason,
 you shouldn't free instance-wide system resources here. Instead, you should do this in free_hook(). However, if
 there are any blocked transactions pending, you should unblock them here.
 
 PC_Speaker notes: Originally Open_Count was not used in this driver, but because I needed to open it also with
 PC_Speaker_Controller the code was changed.  Since it is possible to have more than one open instance, I do not
 free resources unless this is the final close_hook() call. Free_Hook is the place to normally do this, I just
 added it to the code here to show how to do it and to be safe. */

static status_t speaker_close(void *cookie) {
 if (debug) dprintf("PPA: Closed the Driver.\n\n");
 atomic_add(&open_count, -1);
 if (open_count == 0) { // Added to make sure resources are freed at the right time only.
    if (resources_flag == true) { // Only free resources if they still exist.
       delete_sem(write_lock); put_module(B_ISA_MODULE_NAME); resources_flag = false; // Free resources and tag status.
       if (debug) dprintf("PPA: Deleted write semaphores.\nPPA: Deleted B_ISA_MODULE_NAME.\n"); } }
 if (debug) dprintf("\n"); // Spacer in debugger output.
 return B_OK; }

/*****************************************************************************************************************
 free_hook() - status_t free_hook(void *cookie) - This hook is called once all pending transactions on an open
 (but closing) instance of your driver are completed. This is where your driver should release instance wide
 system resources. free_hook() doesn't correspond to any Posix function.

 PC_Speaker notes: Most sample code I found free up the driver's resources here.  When I test the calls to the
 driver I found calls to free_hook() worked all the time, calls to uninit_driver() did not happen when I expected
 them to.  It may be best to have the same code in both in the port's hardware setup we do nothing here. */

static status_t speaker_free(void *cookie) {
 if (debug) dprintf("PPA: Free the Driver.\n");
 if (open_count < 2) { // Do not free resources if driver still open to more than one caller.
    if (resources_flag == true) { // Only free resources if they still exist.
       delete_sem(write_lock); put_module(B_ISA_MODULE_NAME); resources_flag = false; // Free resources and tag status.
       if (debug) dprintf("PPA: Deleted write semaphores.\nPPA: Deleted B_ISA_MODULE_NAME.\n"); } }
 if (debug) dprintf("\n"); // Spacer in debugger output.
 return B_OK; }

/*****************************************************************************************************************
 control_hook() - status_t control_hook(void *cookie, uint32 op, void *data, size_t len) - This hook handles the
 ioctl() function for an open instance of your driver. The control hook provides a means to perform operations
 that don't map directly to either read() or write(). It receives the cookie for the open instance, plus the
 command code op and the data and len arguments specified by ioctl()'s caller.  These arguments have no inherent
 relationship; they're simply arguments to ioctl() that are forwarded to your hook function. Their definitions are
 defined by the driver. Common command codes can be found in be/drivers/Drivers.h.  The len argument is only valid
 when ioctl() is called from user space; the kernel always sets it to 0.

 PC_Speaker notes: Using a re-write of Alexander G. M. Smith's AGMSDeviceTest program ( now called
 'PC_Speaker_Controller' ) different IOCTL control codes can change the different ways the sound driver works
 so the different timing methods can be compared. */

static status_t speaker_control(void *cookie, uint32 opcode, void *buf, size_t len) {
 if (debug) dprintf("PPA: OpCode.\n");
 switch(opcode) {
    
    case B_GET_DEVICE_SIZE: // Plain Audio.  Outputs audio sample data directly to the DAC.
         if (debug) dprintf("PPA: Disabled Over_Sampling.  Outputs plain audio data.\n\n");
         over_sample = 0; return B_OK;

    case B_GET_BIOS_GEOMETRY: // OverSampled Audio.  Outputs audio sample data x2 times.
         if (debug) dprintf("PPA: Enabled Over_Sampling.  Outputs 2x over_sampled audio data.\n\n");
         over_sample = 1; return B_OK;

    case B_SET_DEVICE_SIZE: // OverSampled Audio.  Outputs audio sample data x4 times.
         if (debug) dprintf("PPA: Enabled Over_Sampling.  Outputs 4x over_sampled audio data.\n\n");
         over_sample = 2; return B_OK;

    case B_SET_INTERRUPTABLE_IO: // Allows system interrupts during play-back of sound.
         if (debug) dprintf("PPA: Enabled system interrupts.\n\n");
         interrupt_flag = false; return B_OK;

    case B_SET_UNINTERRUPTABLE_IO: { // Disable system interrupts during play-back of sound.
         if (debug) dprintf("PPA: Disabled system interrupts.\n\n");
         interrupt_flag = true; return B_OK; }

    case B_SET_BLOCKING_IO: // Use busy wait loops to control timing.
         if (debug) dprintf("PPA: Use Busy Wait Loop.\n\n");
         sleep_flag = 0; return B_OK;

    case B_SET_NONBLOCKING_IO: // Use snooze_until() function to control timing.
         if (debug) dprintf("PPA: Use snooze_until().\n\n");
         sleep_flag = 1; return B_OK;

    case B_FLUSH_DRIVE_CACHE: // Use snooze_until() function to skip duplicates sample outputs.
         if (debug) dprintf("PPA: Use snooze_until() to skip duplicates in sample outputs.\n\n");
         sleep_flag = 2; return B_OK;

    case B_EJECT_DEVICE: // Use snooze_until() functions with a Latency = 0 Microseconds.
         if (debug) dprintf("PPA: Latency = 0.\n\n");
         Latency = 0; return B_OK;

    case B_GET_GEOMETRY: // Use snooze_until() functions with a Latency of 5 Microseconds.
         if (debug) dprintf("PPA: Latency = 5.\n\n");
         Latency = LATENCY/2; return B_OK;

    case B_LOAD_MEDIA: // Use snooze_until() functions with a Latency of 10 Microseconds.
         if (debug) dprintf("PPA: Latency = 10.\n\n");
         Latency = LATENCY; return B_OK;

    case B_GET_MEDIA_STATUS: // Disables dprint() of debugging info.
         dprintf("PPA: Debug Info Off.\n\n");
         debug = false; return B_OK;

    case B_FORMAT_DEVICE: // Enables dprint() of debugging info for details of driver operation.
         dprintf("PPA: Debug Info On.\n\n");
         debug = true; return B_OK;

 } // End of switch.
 if (debug) dprintf("Unknown opcode.\n\n");
 return B_ERROR; }

/*****************************************************************************************************************
 read_hook() - status_t read_hook(void *cookie, off_t position, void *data, size_t *len) - This hook handles the
 Posix read() function for an open instance of your driver.  Implement it to read len bytes of data starting at
 the specified byte position on the device, storing the read bytes at data. Exactly what this does is
 device-specific (disk devices would read from the specified offset on the disk, but a graphics driver might have
 some other interpretation of this request). Before returning, you should set len to the actual number of bytes
 read into the buffer. Return B_OK if data was read (even if the number of returned bytes is less than requested),
 otherwise return an appropriate error.

 PC_Speaker notes: This driver is only to play sound.  As there is no recording hardware to be supportted it
 returns an error to the OS if a read_hook() is attempted. */

static status_t speaker_read(void *cookie, off_t pos, void *buf, size_t *len) {
 if (debug) dprintf("PPA: Read from Driver.\n\n"); 
 return B_ERROR; }

/*****************************************************************************************************************
 write_hook() - status_t write_hook(void *cookie, off_t position, void *data, size_t len) -  This hook handles
 the Posix write() function for an open instance of your driver. Implement it to write len bytes of data starting
 at the specified byte position on the device, from the buffer pointed to by data. Exactly what this does is
 device-specific (disk devices would write to the specified offset on the disk, but a graphics driver might have
 some other interpretation of this request). Return B_OK if data was read (even if the number of returned bytes
 is less than requested), otherwise return an appropriate error.

 PC_Speaker notes: This driver assumes the write()buffer is filled with un-signed eight(8) bit sound samples, it
 will outputs the samples thru the parallel (printer) port which then used to feed an eight(8) bit DAC. */

static status_t speaker_write(void *cookie, off_t pos, const void *buffer, size_t *length) {
 int index; acquire_sem(write_lock); // Block writes to buffer before it is played out.
 if (debug) dprintf("PPA: Write to Driver.\n\n"); 
 index = 0; play_buffer_length = *length;
 while (play_buffer_length > 0) { // Check if buffer contains any sound data
    if (play_buffer_length > MAX_PLAY_BUFFER_LENGTH) {
       play_buffer_length = MAX_PLAY_BUFFER_LENGTH; } /* If more data than size of play_buffer just fill
       play_buffer.  We must copy the chunk from the user buffer to our own buffer in kernel space, reading from
       the user buffer could page fault, which is bad to do. */
    memcpy(play_buffer, (uchar*) buffer + index, play_buffer_length); // Copy buffer into play_buffer
    if (play_chunk(play_buffer, play_buffer_length) < B_OK) {
       release_sem(write_lock); return B_OK; } // Allow new writes to buffer.
    index = index + play_buffer_length; play_buffer_length = *length - index; }
 release_sem(write_lock); return B_OK; }

/* PC_Speaker notes: Timing of the bytes sent to the parallel (printer) port for the DAC can be by one of three
 methods. (1) Busy Wait Loop, (2) Snooze_Until() or (3) Snooze_Until() that skips over same value outputs. */

static status_t play_chunk(uchar *buffer, long len) {
 cpu_status st; int current_sample, index; bigtime_t frame_length, next_frame_time;

 current_sample = 0, index = 0; // Set over_sampling pointers to start of audio data.

 if (over_sample == 0) {
    frame_length = 1000000/frame_rate; // How many microseconds between samples.
    while (current_sample < len) {
          output_buffer[index++] = buffer[current_sample++]; } } // Move pointer to next sample.

 if (over_sample == 1) {
    frame_length = 500000/frame_rate; // How many microseconds between x2 samples.
    while (current_sample < len) { // Convert data to over sampling format.
          new_sample = ((uchar*) buffer)[current_sample];
          output_buffer[index++] = convert_2[((int32)old_sample<<8)+(int32)new_sample]; // (Old+New)/2
          output_buffer[index++] = new_sample; old_sample = new_sample;                 // (Old*0+New*4)/4
          current_sample++; } } // Move pointer to next sample.
 
 if (over_sample == 2) {
    frame_length = 250000/frame_rate; // How many microseconds between x4 samples.
    while (current_sample < len) { // Convert data to over sampling format.
          new_sample = ((uchar*) buffer)[current_sample];
          output_buffer[index++] = convert_1[((int32)old_sample<<8)+(int32)new_sample]; // (Old*3+New*1)/4
          output_buffer[index++] = convert_2[((int32)old_sample<<8)+(int32)new_sample]; // (Old*2+New*2)/4
          output_buffer[index++] = convert_3[((int32)old_sample<<8)+(int32)new_sample]; // (Old*1+New*3)/4
          output_buffer[index++] = new_sample; old_sample = new_sample;                 // (Old*0+New*4)/4
          current_sample++; } } // Move pointer to next sample.

 next_frame_time = (1 + (system_time()/frame_length)) * frame_length; /* Quantumize the timing so that each
 audio sample is outputted at regular timing even between calls to play_chunk(). */
 len = index; // Adjust length to match over_sampled version.

 if (interrupt_flag == true) {
    st = disable_interrupts(); } // Disable interrupts to improve timing.

 current_sample = 0; old_sample = output_buffer[current_sample]; // Get first sample in buffer to play.

 if (sleep_flag == 0) {
    while (system_time() < next_frame_time) { } // Busy-Wait, loop until time to play first sample
 } else {
    snooze_until(next_frame_time - Latency, B_SYSTEM_TIMEBASE); // Sleep until time to play first sound sample.
    if (Latency > 0) {
       while (system_time() < next_frame_time) { } } } // Busy-Wait, loop until time to play next sample

 isa->write_io_8(0x378, old_sample); // Write data to parallel port.
 current_sample++;                   // Move pointer to next sample.
 next_frame_time += frame_length;    // Setup time to output next sample.

 while (current_sample < len) {
    new_sample = output_buffer[current_sample]; // Get next sample to play.
    if (sleep_flag == 2) { // Skip duplicate sound sample outputs.
       while ((old_sample == new_sample)&&(current_sample < (len-1))) {
             current_sample++;                           // Move pointer to next sample.
             new_sample = output_buffer[current_sample]; // Get Sample to play.
             next_frame_time += frame_length; } }        // Time for next sound sample out.
    if (sleep_flag > 0) { // Not using Busy-Wait then need to use Snooze_Until() function.
       snooze_until(next_frame_time - Latency, B_SYSTEM_TIMEBASE); } // Sleep until time to play next sample.
    if ((sleep_flag == 0)|(Latency > 0)) {
       while (system_time() < next_frame_time) { } }       // Busy-Wait, loop until time to play next sample.

    isa->write_io_8(0x378, new_sample); // Write data to parallel port.
    old_sample = new_sample;            // Update old_sample to match present output.
    current_sample++;                   // Move pointer to next sample.
    next_frame_time += frame_length;    // Time for next sound sample out.
 } // End While loop 

 if (interrupt_flag == true) {
    restore_interrupts(st); } // Enable interrupts to maintain OS requirements.

 return B_OK; }

/*****************************************************************************************************************
 publish_devices - const char** publish_devices(void) - returns a null-terminated array of devices supported by
 this driver.  Devfs calls publish_devices() to learn the names, relative to /dev, of the devices the driver
 supports. The driver should return a NULL-terminated array of strings indicating all the installed devices the
 driver supports. For example, an ethernet device driver might return: 
 static char *devices[] = { "net/ether", NULL };

 In this case, devfs will then create the pseudo-file /dev/net/ether, through which all user applications can
 access the driver.  Since only one instance of the driver will be loaded, if support for multiple devices of the
 same type is desired, the driver must be capable of supporting them. If the driver senses (and supports) two
 ethernet cards, it might return: 
 static char *devices[] = { "net/ether1", "net/ether2", NULL };
 
 PC_Speaker notes: Gets published at /dev/audio/PC_Speaker. */

const char** publish_devices(void) {
 return speaker_names; }

/*****************************************************************************************************************
 find_device - device_hooks* find_device(const char *name) - returns a pointer to device hooks structure for a
 given device name.  When a device published by the driver is accessed, devfs communicates with it through a
 series of hook functions that handle the requests.  The find_device() function is called to obtain a list of
 these hook functions, so that devfs can call them.  The device_hooks structure returned lists out the hook
 functions.  The device_hooks structure, and what each hook does, is described in the next section. */

device_hooks* find_device(const char *name) {
 return &speaker_device; }

/* readv_hook() - status_t readv_hook(void *cookie, off_t position, const struct iovec *vec, size_t count, size_t *len) 
This hook handles the Posix readv() function for an open instance of your driver.  This is a scatter/gather read
function; given an array of iovec structures describing address/length pairs for a group of destination buffers,
your implementation should fill each successive buffer with bytes, up to a total of len bytes. The vec array has
count items in it. As with read_hook(), set len to the actual number of bytes read, and return an appropriate
result code.

static status_t my_device_readv(void *cookie, off_t position, const iovec *vec, size_t count, size_t *len) {
 return B_OK; }

/* writev_hook() - status_t writev_hook(void *cookie, off_t position, const struct iovec *vec, size_t count, size_t *len) 
 This hook handles the Posix writev() function for an open instance of your driver. This is a scatter/gather write
 function; given an array of iovec structures describing address/length pairs for a group of source buffers, your
 implementation should write each successive buffer to disk, up to a total of len bytes. The vec array has count
 items in it. Before returning, set len to the actual number of bytes written, and return an appropriate result
 code.

static status_t my_device_writev(void *cookie, off_t position, const iovec *vec, size_t count, size_t *len) {
  return B_OK; }

select_hook() , deselect_hook() 
 These hooks are reserved for future use. Set the corresponding entries in your device_hooks structure to NULL.

status_t my_device_select(void *cookie, uint8 event, uint32 ref, selectsync *sync) {
 return B_OK; }

status_t my_device_deselect(void *cookie, uint8 event, selectsync *sync) {
 return B_OK; } */
