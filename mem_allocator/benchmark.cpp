// benchmark.cpp

#include "allocator.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <numeric>

MyAllocator g_allocator;

// Recursion guard
static thread_local bool in_allocator = false;

// --- Global Overrides for new and delete ---
void* operator new(size_t size) {
    if (in_allocator) {
        return std::malloc(size);
    }
    in_allocator = true;
    void* result = g_allocator.allocate(size);
    in_allocator = false;
    return result;
}

void operator delete(void* ptr) noexcept {
    if (in_allocator) {
        std::free(ptr);
        return;
    }
    in_allocator = true;
    g_allocator.deallocate(ptr);
    in_allocator = false;
}

void* operator new[](size_t size) {
    if (in_allocator) {
        return std::malloc(size);
    }
    in_allocator = true;
    void* result = g_allocator.allocate(size);
    in_allocator = false;
    return result;
}

void operator delete[](void* ptr) noexcept {
    if (in_allocator) {
        std::free(ptr);
        return;
    }
    in_allocator = true;
    g_allocator.deallocate(ptr);
    in_allocator = false;
}

// --- The Benchmark Workload ---
const int NUM_ALLOCATIONS_PER_THREAD = 10000;
const int MAX_ALLOC_SIZE = 128;

void worker_thread_workload(int thread_id) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1, MAX_ALLOC_SIZE);

    std::vector<char*> allocations;
    allocations.reserve(NUM_ALLOCATIONS_PER_THREAD);

    // Allocation phase
    for (int i = 0; i < NUM_ALLOCATIONS_PER_THREAD; ++i) {
        char* ptr = new char[distrib(gen)];
        if (ptr == nullptr) {
            std::cout << "Thread " << thread_id << " allocation failed at " << i << std::endl;
            break;
        }
        allocations.push_back(ptr);
    }

    // Deallocation phase
    for (char* ptr : allocations) {
        delete[] ptr;
    }
}

int main() {
    std::cout << "--- Allocator Benchmark (Using Custom Allocator) ---" << std::endl;
    
    // Basic test first
    std::cout << "Running basic test..." << std::endl;
    char* test_ptr = new char[32];
    if (test_ptr) {
        delete[] test_ptr;
        std::cout << "Basic test passed!" << std::endl;
    } else {
        std::cout << "Basic test failed!" << std::endl;
        return 1;
    }
    
    // Benchmark
    const std::vector<int> thread_counts = {1, 2, 4, 8};

    for (int n_threads : thread_counts) {
        std::cout << "\nTesting with " << n_threads << " threads..." << std::endl;
        std::vector<std::thread> threads;

        auto start_time = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < n_threads; ++i) {
            threads.emplace_back(worker_thread_workload, i);
        }

        for (auto& t : threads) {
            t.join();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

        std::cout << "Threads: " << n_threads
                  << "\tTotal Time: " << elapsed.count() << " ms"
                  << "\tOperations: " << (n_threads * NUM_ALLOCATIONS_PER_THREAD * 2)
                  << "\tOps/sec: " << (n_threads * NUM_ALLOCATIONS_PER_THREAD * 2 * 1000.0 / elapsed.count())
                  << std::endl;
    }

    std::cout << "\nBenchmark completed successfully!" << std::endl;
    return 0;
}
