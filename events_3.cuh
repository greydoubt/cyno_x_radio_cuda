// create two events

cudaEvent_t start, stop;

cudaEventCreate(&start);

cudaEventCreate(&stop);

// record start event on the default stream

cudaEventRecord(start);

// execute kernel

kernel<<<grid, block>>>(arguments);



// record stop event on the default stream

cudaEventRecord(stop);

// wait until the stop event completes

cudaEventSynchronize(stop);

// calculate the elapsed time between two events

float time;

cudaEventElapsedTime(&time, start, stop);

// clean up the two events

cudaEventDestroy(start);

cudaEventDestroy(stop);
