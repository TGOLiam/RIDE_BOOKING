#include <iostream>
#include <vector>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cuda_runtime.h>

#define TILE_SIZE 16
using namespace std;

//CPU Matrix Multiplication (Baseline)
void matrixMulCPU(const float* A, const float* B, float* C, int N) {
    for (int row = 0; row < N; ++row) {
        for (int col = 0; col < N; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < N; ++k) {
                sum += A[row * N + k] * B[k * N + col];
            }
            C[row * N + col] = sum;
        }
    }
}

//Naive GPU Matrix Multiplication
__global__ void matrixMulNaive(const float* A, const float* B, float* C, int N) {
    int row = blockIdx.y * blockDim.y + threadIdx.y;
    int col = blockIdx.x * blockDim.x + threadIdx.x;

    if (row < N && col < N) {
        float sum = 0.0f;
        for (int k = 0; k < N; ++k) {
            sum += A[row * N + k] * B[k * N + col];
        }
        C[row * N + col] = sum;
    }
}

//Tiled GPU Matrix Multiplication (Shared Memory)
__global__ void matrixMulTiled(const float* A, const float* B, float* C, int N) {
    //Allocate shared memory for tiles
    __shared__ float tileA[TILE_SIZE][TILE_SIZE];
    __shared__ float tileB[TILE_SIZE][TILE_SIZE];

    int bx = blockIdx.x;
    int by = blockIdx.y;
    int tx = threadIdx.x;
    int ty = threadIdx.y;

    //Identify row and column this thread computes
    int row = by * TILE_SIZE + ty;
    int col = bx * TILE_SIZE + tx;

    float sum = 0.0f;

    //Number of tiles needed
    int numPhases = (N + TILE_SIZE - 1) / TILE_SIZE;

    for (int ph = 0; ph < numPhases; ph++) {

        //Load tile from matrix A into shared memory
        if (row < N && (ph * TILE_SIZE + tx) < N)
            tileA[ty][tx] = A[row * N + ph * TILE_SIZE + tx];
        else
            tileA[ty][tx] = 0.0f;

        //Load tile from matrix B into shared memory
        if (col < N && (ph * TILE_SIZE + ty) < N)
            tileB[ty][tx] = B[(ph * TILE_SIZE + ty) * N + col];
        else
            tileB[ty][tx] = 0.0f;

        //Wait until all threads finish loading
        __syncthreads();

        //Multiply the two tiles
        for (int k = 0; k < TILE_SIZE; k++) {
            sum += tileA[ty][k] * tileB[k][tx];
        }

        //Wait before loading the next tile
        __syncthreads();
    }

    //Store result
    if (row < N && col < N) {
        C[row * N + col] = sum;
    }
}

// Helper to Check Correctness
bool checkCorrectness(const float* C_cpu, const float* C_gpu, int N) {
    float epsilon = 1e-3; //Tolerance for floating point drift (can be adjusted based on precision requirements, helps with floating point errors)
    for (int i = 0; i < N * N; ++i) {
        if (fabs(C_cpu[i] - C_gpu[i]) > epsilon) {
            return false;
        }
    }
    return true;
}

//Helper to check on CUDA errors
void checkCudaError(cudaError_t err, const char* msg){
    if (err != cudaSuccess) {
        cerr << "CUDA ERROR DURING " << msg << ": " << cudaGetErrorString(err) << endl;
        exit(EXIT_FAILURE); 
    }
}


int main() {
    //Add 2048 or 4096 if your GPU has enough VRAM, remove the 2 numbers if you want to test smaller sizes only
    vector<int> matrix_sizes = {256, 512, 1024, 2048, 4096}; 


    cout << "========================================================\n";
    cout << " CUDA Matrix Multiplication Performance Comparison\n";
    cout << "========================================================\n\n";

    for (int N : matrix_sizes) {
        cout << "Testing Matrix Size: " << N << " x " << N << "\n";
        cout << "--------------------------------------------------------\n";

        size_t size_bytes = N * N * sizeof(float);

        //Host Memory Allocationn
        float *h_A = new float[N * N];
        float *h_B = new float[N * N];
        float *h_C_cpu = new float[N * N];
        float *h_C_naive = new float[N * N];
        float *h_C_tiled = new float[N * N];

        //Initialize Matrices with Random Values
        for (int i = 0; i < N * N; ++i) {
            h_A[i] = static_cast<float>(rand()) / RAND_MAX;
            h_B[i] = static_cast<float>(rand()) / RAND_MAX;
        }

        //Allocates GPU Memory
        float *d_A, *d_B, *d_C;
        checkCudaError(cudaMalloc(&d_A, size_bytes), "Allocating d_A");
        checkCudaError(cudaMalloc(&d_B, size_bytes), "Allocating d_B");
        checkCudaError(cudaMalloc(&d_C, size_bytes), "Allocating d_C");

        //Copies data from Host to Device
        checkCudaError(cudaMemcpy(d_A, h_A, size_bytes, cudaMemcpyHostToDevice), "Copying d_A to Device");
        checkCudaError(cudaMemcpy(d_B, h_B, size_bytes, cudaMemcpyHostToDevice), "Copying d_B to Device");
        checkCudaError(cudaMemcpy(d_C, h_C_cpu, size_bytes, cudaMemcpyHostToDevice), "Copying d_C to Device");

        //Setup CUDA Grid and Block dimensions
        dim3 threadsPerBlock(TILE_SIZE, TILE_SIZE);
        dim3 blocksPerGrid((N + TILE_SIZE - 1) / TILE_SIZE, (N + TILE_SIZE - 1) / TILE_SIZE);

        //CUDA Timing Events
        cudaEvent_t start, stop;
        checkCudaError(cudaEventCreate(&start), "Creating start event");
        checkCudaError(cudaEventCreate(&stop), "Creating stop event");
        float ms_naive = 0.0f, ms_tiled = 0.0f;

        //CPU EXECUTION (skip for very large sizes to avoid extremely long runs)
        float ms_cpu = 0.0f;
        if (N <= 4096) { // Adjust this threshold based on your system's capabilities
            auto cpu_start = chrono::high_resolution_clock::now();
            matrixMulCPU(h_A, h_B, h_C_cpu, N);
            auto cpu_end = chrono::high_resolution_clock::now();
            chrono::duration<float, milli> cpu_duration = cpu_end - cpu_start;
            ms_cpu = cpu_duration.count();
        } else {
            cout << "Skipping CPU baseline for N = " << N << " (too large)\n";
        }

        //NAIVE GPU EXECUTION
        checkCudaError(cudaEventRecord(start), "Recording start event");
        matrixMulNaive<<<blocksPerGrid, threadsPerBlock>>>(d_A, d_B, d_C, N);
        checkCudaError(cudaEventRecord(stop), "Recording stop event");
        checkCudaError(cudaEventSynchronize(stop), "Synchronizing stop event");
        checkCudaError(cudaEventElapsedTime(&ms_naive, start, stop), "Getting elapsed time");
        checkCudaError(cudaMemcpy(h_C_naive, d_C, size_bytes, cudaMemcpyDeviceToHost), "Copying result from device to host");

        //TILED GPU EXECUTION
        checkCudaError(cudaEventRecord(start), "Recording start event");
        matrixMulTiled<<<blocksPerGrid, threadsPerBlock>>>(d_A, d_B, d_C, N);
        checkCudaError(cudaEventRecord(stop), "Recording stop event");
        checkCudaError(cudaEventSynchronize(stop), "Synchronizing stop event");
        checkCudaError(cudaEventElapsedTime(&ms_tiled, start, stop), "Getting elapsed time");
        checkCudaError(cudaMemcpy(h_C_tiled, d_C, size_bytes, cudaMemcpyDeviceToHost), "Copying result from device to host");

        //CHECKS CORRECTNESS 
        bool naive_correct = checkCorrectness(h_C_cpu, h_C_naive, N);
        bool tiled_correct = checkCorrectness(h_C_cpu, h_C_tiled, N);

        //PRINTS RESULTS
        cout << "CPU Time:         ";
        if (ms_cpu > 0.0f) cout << ms_cpu << " ms\n";
        else cout << "(skipped)\n";

        cout << "Naive GPU Time:     " << ms_naive << " ms (Correct: " << (naive_correct ? "Yes" : "NO!") << ")\n";
        cout << "Tiled GPU Time:     " << ms_tiled << " ms (Correct: " << (tiled_correct ? "Yes" : "NO!") << ")\n";
        
        cout << "\nSpeedups:\n";
        if (ms_cpu > 0.0f) {
            cout << "CPU vs Naive GPU:   " << ms_cpu / ms_naive << "x faster\n";
            cout << "CPU vs Tiled GPU:   " << ms_cpu / ms_tiled << "x faster\n";
        } else {
            cout << "CPU baseline skipped for this size; GPU vs GPU:\n";
        }
        cout << "Naive vs Tiled GPU: " << ms_naive / ms_tiled << "x faster\n";
        cout << "========================================================\n\n";

        // Free Memory
        delete[] h_A; delete[] h_B; delete[] h_C_cpu; delete[] h_C_naive; delete[] h_C_tiled;
        checkCudaError(cudaFree(d_A), "Freeing d_A");
        checkCudaError(cudaFree(d_B), "Freeing d_B");
        checkCudaError(cudaFree(d_C), "Freeing d_C");
        checkCudaError(cudaEventDestroy(start), "Destroying start event");
        checkCudaError(cudaEventDestroy(stop), "Destroying stop event");
    }

    return 0;
}