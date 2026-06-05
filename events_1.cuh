#ifndef CUDA_EVENT_TIMER_H
#define CUDA_EVENT_TIMER_H

#include <cuda_runtime.h>

class CudaEventTimer {
public:
    // Constructor to create and initialize events
    CudaEventTimer();

    // Destructor to destroy events
    ~CudaEventTimer();

    // Record the start event
    void start(cudaStream_t stream = 0);

    // Record the stop event
    void stop(cudaStream_t stream = 0);

    // Synchronize the stop event
    void synchronize();

    // Query the stop event
    bool query();

    // Get elapsed time in milliseconds
    float getElapsedTime();

private:
    cudaEvent_t startEvent;
    cudaEvent_t stopEvent;
};

#endif // CUDA_EVENT_TIMER_H
