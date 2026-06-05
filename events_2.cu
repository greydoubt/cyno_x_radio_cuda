#include "CudaEventTimer.h"
#include <stdexcept>

CudaEventTimer::CudaEventTimer() {
    // Create start and stop events
    cudaError_t err = cudaEventCreate(&startEvent);
    if (err != cudaSuccess) {
        throw std::runtime_error("Failed to create start event");
    }
    err = cudaEventCreate(&stopEvent);
    if (err != cudaSuccess) {
        cudaEventDestroy(startEvent);
        throw std::runtime_error("Failed to create stop event");
    }
}

CudaEventTimer::~CudaEventTimer() {
    // Destroy events
    cudaEventDestroy(startEvent);
    cudaEventDestroy(stopEvent);
}

void CudaEventTimer::start(cudaStream_t stream) {
    // Record start event
    cudaEventRecord(startEvent, stream);
}

void CudaEventTimer::stop(cudaStream_t stream) {
    // Record stop event
    cudaEventRecord(stopEvent, stream);
}

void CudaEventTimer::synchronize() {
    // Synchronize stop event
    cudaEventSynchronize(stopEvent);
}

bool CudaEventTimer::query() {
    // Query stop event
    cudaError_t err = cudaEventQuery(stopEvent);
    return err == cudaSuccess;
}

float CudaEventTimer::getElapsedTime() {
    // Get elapsed time between start and stop events
    float ms = 0.0f;
    cudaEventElapsedTime(&ms, startEvent, stopEvent);
    return ms;
}
