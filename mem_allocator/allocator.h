// allocator.h

#pragma once

#include <cstddef> // For size_t
#include <mutex>   // For std::mutex
#include <unordered_map> // For the PageHeap's page map

class MyAllocator {
public:
    void* allocate(size_t size);
    void deallocate(void* ptr);

    // Hidden header for ALL allocations. Stores the size.
    struct BlockHeader {
        size_t size;
    };

    // Header for a free block within a list.
    struct FreeBlockHeader {
        FreeBlockHeader* next;
    };

    // A contiguous run of one or more pages managed by the PageHeap.
    struct Span {
        size_t start_page_id;
        size_t num_pages;
        Span* next = nullptr;
        Span* prev = nullptr;
        bool is_free = true;
    };

    // Per-thread private cache for small allocations.
    struct ThreadCache {
        FreeBlockHeader* free_lists[8] = {nullptr};
        int list_lengths[8] = {0};
    };

private:

    // Shared buffer between ThreadCache and PageHeap.
    struct TransferCache {
        std::mutex mtx;
        FreeBlockHeader* list = nullptr;
        int count = 0;
    };

    // --- The Three Tiers ---

    class PageHeap {
    public:
        Span* allocateSpan(size_t num_pages);
        void deallocateSpan(Span* span);
        Span* lookupSpan(void* ptr);
    private:
        std::mutex mtx;
        std::unordered_map<size_t, Span*> page_map;
    };

    // --- Private Members and Helpers ---

    PageHeap page_heap;
    TransferCache transfer_caches[8];

    static size_t getSizeClassIndex(size_t size);
    static size_t getClassSizeFromIndex(size_t index);

    void fetchFromTransferCache(size_t class_index);
    void releaseToTransferCache(size_t class_index);
};
