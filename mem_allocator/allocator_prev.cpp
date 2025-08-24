#include "allocate.h"
#include <cstddef>
// the below is for the mmap and munmap
#include <sys/mman.h>
#include <unistd.h>
#include <cassert>
#include <mutex>

using namespace std;

constexpr size_t pool_size = 128 * 1024 * 1024 ; // here taking the size to be 128MiB;
constexpr size_t refill_block = 16; // let it be a constant for now
// so now we now have to define the lock on the free_list_head only once, that is the global head, which is not related to the specific thread
mutex global_mutex;
static thread_local MyAllocator::thread_cache mycache; // this is the thread cache that is only avaliable to this specific thread
size_t MyAllocator::getclassindex(size_t size) {
    for (size_t i = 0; i < 8; ++i) {
        if (size <= SIZE_CLASSES[i]) return i;
    }
    assert(false && "Invalid size for small allocation");
    return -1;
}

size_t MyAllocator::getsizeindex(size_t index) {
    return SIZE_CLASSES[index];
}
MyAllocator::MyAllocator() {
  memory_pool_start = requestMem(pool_size);
  if(memory_pool_start != nullptr) {
      free_list_head = static_cast<Header*>(memory_pool_start);
      free_list_head->next = nullptr;
      free_list_head->size = pool_size;
  }
};
// here the below step is nothing but us trying to implement the RAII, Resource Aquistion is initialisation, that is deleting the memory when allocated when it goes out of scope
MyAllocator::~MyAllocator() {
 if(memory_pool_start!=nullptr) {
     munmap(memory_pool_start, pool_size);
 }
}

void* MyAllocator::requestMem(size_t size) {
    //okay look the arguemets of the mmap online, they are easy to understand
    void* ptr = mmap(nullptr, size,  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(ptr == MAP_FAILED) return nullptr;
    return ptr;
}

void* MyAllocator::allocate(size_t size) {
    if(mycache.thread_free_list_head) {
        Header* block =
    }








    // *** The below code is the unoptimised first fit based

   //  lock_guard<mutex> lock(mtx);

   //  if(size==0) return nullptr;

   //  //total size we need to allocate is the data size and also the header's size
   //  size_t total_size = size + sizeof(Header);

   //  Header* curr = free_list_head;
   //  Header* prev = nullptr;

   //  // we will use the first fit algorithm here
   //  while(curr) {
   //      if(curr->size>=total_size) {
   //          // then we check if we there is enough extra space to create a new block;
   //          if(curr->size > total_size+sizeof(Header)) {
   //              // so we are splitting now,
   //              Header* new_free_header = Header*((char*)curr+total_size);
   //              new_free_header->size = curr->size - total_size;
   //              new_free_header->next = curr->next;

   //              if(prev) {
   //                  prev->next = new_free_header;
   //              }
   //              else {
   //                  free_list_head = new_free_header; // make this the current head;
   //              }
   //          }
   //          else {
   //              if(prev) prev->next = curr->next;
   //              else free_list_head = curr->next;
   //          }
   //         return (void*)((char*)curr + sizeof(Header));
   //      }
   //      prev = curr;
   //      curr = curr->next;
   //  }
   // return nullptr;
}

void MyAllocator::free(void* ptr) {

    lock_guard<mutex> lock(mtx);
    if(ptr==nullptr) return;

    Header* block = (Header*)((char*)ptr-sizeof(Header));
    block->next = free_list_head;
    free_list_head = block;
    // adding the freed block to the beginning of the list
}

void* MyAllocator::refillthreadcache(thread_cache* cache) {

}
