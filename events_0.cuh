// An event is declared as follows:

cudaEvent_t event;

// Once declared, the event can be created using:

cudaError_t cudaEventCreate(cudaEvent_t* event);

// An event can be destroyed using:

cudaError_t cudaEventDestroy(cudaEvent_t event);

// If the event has not yet been satisfied when cudaEventDestroy is called, the call returns immediately and the resources associated with that event are released automatically when the event is marked complete.

// Recording Events and Measuring Elapsed Time

// Events mark a point in stream execution. They can be used to check if the executing stream operations have reached a given point. You can think of them as operations added to a CUDA stream whose only action when popped from the head of the work queue is to raise a host-side flag to indicate completion. An event is queued to a CUDA stream using the following function:

cudaError_t cudaEventRecord(cudaEvent_t event, cudaStream_t stream = 0);

// The passed event can be used to either wait for, or test for, the completion of all preceding operations in the specified stream. Waiting for an event blocks the calling host thread, and is performed using the following function:

cudaError_t cudaEventSynchronize(cudaEvent_t event);

cudaEventSynchronize is analogous to cudaStreamSynchronize for streams, but allows the host to wait for an intermediate point in stream execution.

// You can also test if an event has completed without blocking the host application using:

cudaError_t cudaEventQuery(cudaEvent_t event);

// cudaEventQuery is similar to cudaStreamQuery, but for events.

// You can measure the elapsed time of CUDA operations marked by two events using the following function:

cudaError_t cudaEventElapsedTime(float* ms, cudaEvent_t start, cudaEvent_t stop);
