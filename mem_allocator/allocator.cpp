// allocator.cpp

#include "allocator.h"
#include <sys/mman.h> // For mmap, munmap
#include <cassert>    // For assert
#include <cstdlib>    // For std::malloc and std::free
#include <iostream>   // For debug output
#include <algorithm>  // For std::min

// --- Static and Global Variables ---
static thread_local MyAllocator::ThreadCache my_cache;
static const size_t PAGE_SHIFT = 12; // 2^12 = 4096 (4KB)
const size_t SIZE_CLASSES[] = {8, 16, 32, 64, 128, 256, 512, 1024};
constexpr size_t MAX_SMALL_ALLOC_SIZE = 1024;
constexpr int SCAVENGE_THRESHOLD = 128;

// Global span allocator to avoid recursion
static char span_memory[4096 * 10]; // Pre-allocated memory for spans
static size_t span_offset = 0;
static std::mutex span_alloc_mutex;

static void* allocate_span_memory(size_t size) {
    std::lock_guard<std::mutex> lock(span_alloc_mutex);
    if (span_offset + size > sizeof(span_memory)) {
        return nullptr; // Out of pre-allocated span memory
    }
    void* ptr = span_memory + span_offset;
    span_offset += size;
    return ptr;
}

// --- Helper Function Implementations ---
size_t MyAllocator::getSizeClassIndex(size_t size) {
    for (size_t i = 0; i < 8; ++i) {
        if (size <= SIZE_CLASSES[i]) return i;
    }
    return 8; // Return invalid index, not -1
}

size_t MyAllocator::getClassSizeFromIndex(size_t index) {
    if (index >= 8) return 0; // Safety check
    return SIZE_CLASSES[index];
}

// --- PageHeap Implementation ---
MyAllocator::Span* MyAllocator::PageHeap::lookupSpan(void* ptr) {
    size_t page_id = (uintptr_t)ptr >> PAGE_SHIFT;
    std::lock_guard<std::mutex> lock(mtx);
    auto it = page_map.find(page_id);
    return (it == page_map.end()) ? nullptr : it->second;
}

MyAllocator::Span* MyAllocator::PageHeap::allocateSpan(size_t num_pages) {
    std::lock_guard<std::mutex> lock(mtx);
    size_t total_size = num_pages << PAGE_SHIFT;
    void* new_mem = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (new_mem == MAP_FAILED) {
        return nullptr;
    }

    // Use pre-allocated memory instead of malloc to avoid recursion
    Span* span = (Span*)allocate_span_memory(sizeof(Span));
    if (!span) {
        munmap(new_mem, total_size);
        return nullptr;
    }
    
    span->start_page_id = (uintptr_t)new_mem >> PAGE_SHIFT;
    span->num_pages = num_pages;
    span->is_free = false;
    span->next = nullptr;
    span->prev = nullptr;

    for (size_t i = 0; i < num_pages; ++i) {
        page_map[span->start_page_id + i] = span;
    }
    return span;
}

void MyAllocator::PageHeap::deallocateSpan(Span* span) {
    std::lock_guard<std::mutex> lock(mtx);
    munmap((void*)(span->start_page_id << PAGE_SHIFT), span->num_pages << PAGE_SHIFT);
    for (size_t i = 0; i < span->num_pages; ++i) {
        page_map.erase(span->start_page_id + i);
    }
    // Don't free span memory since it's from our pre-allocated pool
}

// --- Main Allocator Logic ---
void MyAllocator::fetchFromTransferCache(size_t class_index) {
    if (class_index >= 8) return;
    
    TransferCache& tc = transfer_caches[class_index];
    std::lock_guard<std::mutex> lock(tc.mtx);

    if (tc.count == 0) {
        size_t block_size = getClassSizeFromIndex(class_index);
        if (block_size == 0) return;
        
        // Use actual block size including header
        size_t actual_block_size = block_size + sizeof(BlockHeader);
        size_t num_blocks_to_fetch = 4096 / actual_block_size;
        if (num_blocks_to_fetch == 0) num_blocks_to_fetch = 1;

        Span* span = page_heap.allocateSpan(1);
        if (span == nullptr) return;

        char* start = (char*)(span->start_page_id << PAGE_SHIFT);
        
        for(size_t i = 0; i < num_blocks_to_fetch; ++i) {
            char* block_ptr = start + i * actual_block_size;
            FreeBlockHeader* block = reinterpret_cast<FreeBlockHeader*>(block_ptr);
            block->next = tc.list;
            tc.list = block;
        }
        tc.count += num_blocks_to_fetch;
    }

    // Transfer some blocks to thread cache
    int blocks_to_transfer = std::min((int)tc.count, 32);
    
    FreeBlockHeader* head = tc.list;
    FreeBlockHeader* tail = head;
    
    for (int i = 1; i < blocks_to_transfer && tail && tail->next; ++i) {
        tail = tail->next;
    }
    
    if (tail) {
        tc.list = tail->next;
        tail->next = nullptr;
        tc.count -= blocks_to_transfer;
        
        my_cache.free_lists[class_index] = head;
        my_cache.list_lengths[class_index] = blocks_to_transfer;
    }
}

void MyAllocator::releaseToTransferCache(size_t class_index) {
    if (class_index >= 8 || my_cache.list_lengths[class_index] == 0) return;
    
    TransferCache& tc = transfer_caches[class_index];
    std::lock_guard<std::mutex> lock(tc.mtx);

    // Find the tail of the thread cache list
    FreeBlockHeader* head = my_cache.free_lists[class_index];
    FreeBlockHeader* tail = head;
    int count = 1;
    
    // Safely traverse to find tail, with bounds checking
    while (tail && tail->next && count < my_cache.list_lengths[class_index]) {
        tail = tail->next;
        count++;
    }
    
    if (tail) {
        tail->next = tc.list;  // Connect to existing transfer cache list
    }
    
    tc.list = head;
    tc.count += my_cache.list_lengths[class_index];
    my_cache.free_lists[class_index] = nullptr;
    my_cache.list_lengths[class_index] = 0;
}

void* MyAllocator::allocate(size_t size) {
    if (size == 0) return nullptr;

    // --- Large Allocation Path ---
    if (size > MAX_SMALL_ALLOC_SIZE) {
        size_t total_size = size + sizeof(MyAllocator::BlockHeader);
        size_t num_pages = (total_size + 4095) >> PAGE_SHIFT;
        Span* span = page_heap.allocateSpan(num_pages);
        if (span == nullptr) return nullptr;

        MyAllocator::BlockHeader* header = (MyAllocator::BlockHeader*)(span->start_page_id << PAGE_SHIFT);
        header->size = size;
        return (void*)(header + 1);
    }

    // --- Small Allocation Path ---
    size_t index = getSizeClassIndex(size);
    if (index >= 8) return nullptr;
    
    if (my_cache.free_lists[index] == nullptr) {
        fetchFromTransferCache(index);
        if (my_cache.free_lists[index] == nullptr) return nullptr;
    }

    FreeBlockHeader* block = my_cache.free_lists[index];
    my_cache.free_lists[index] = block->next;
    my_cache.list_lengths[index]--;

    MyAllocator::BlockHeader* header = (MyAllocator::BlockHeader*)block;
    header->size = getClassSizeFromIndex(index);
    return (void*)(header + 1);
}

void MyAllocator::deallocate(void* ptr) {
    if (ptr == nullptr) return;

    MyAllocator::BlockHeader* header = (MyAllocator::BlockHeader*)((char*)ptr - sizeof(MyAllocator::BlockHeader));
    size_t size = header->size;

    // --- Large Deallocation Path ---
    if (size > MAX_SMALL_ALLOC_SIZE) {
        Span* span = page_heap.lookupSpan(header);
        if (span == nullptr) return;
        page_heap.deallocateSpan(span);
        return;
    }

    // --- Small Deallocation Path ---
    size_t index = getSizeClassIndex(size);
    if (index >= 8) return;
    
    FreeBlockHeader* block = (FreeBlockHeader*)header;
    block->next = my_cache.free_lists[index];
    my_cache.free_lists[index] = block;
    my_cache.list_lengths[index]++;

    if (my_cache.list_lengths[index] > SCAVENGE_THRESHOLD) {
        releaseToTransferCache(index);
    }
}
