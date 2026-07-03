#ifndef __mem_micro__
#define __mem_micro__

#include <cstdint>
#include <vector>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>
#define PROT (PROT_READ | PROT_WRITE)
#define MAP (MAP_ANONYMOUS|MAP_PRIVATE|MAP_POPULATE)



enum class loader_t {read, triad};

static const uint64_t ptr_key = 0x1234567076543210UL;

#ifdef UNALIGNED_NODE
typedef struct __attribute__ ((__packed__)) node {
  uint8_t pad0[57];
  struct node *next;
  uint8_t pad1[128 - (sizeof(struct node*) + 57)];
} node ;
#else
typedef struct node {
  struct node *next;
} node;
#endif

static const void* failed_mmap = reinterpret_cast<void *>(-1);

template <bool enable>
static inline node* xor_ptr(node *ptr) {
  if(enable) {
    uint64_t p = reinterpret_cast<uint64_t>(ptr);
    p ^= ptr_key;
    return reinterpret_cast<node*>(p);
  }
  else {
    return ptr;
  }
}


static void *alloc_mem(size_t bytes) {
  void *p = mmap(nullptr, bytes, PROT, MAP|MAP_HUGETLB, -1, 0);
  if(p == failed_mmap) {
    std::cout << "warn : large page allocation failed, falling back to "
	      << getpagesize() << " byte allocations\n";
    p = mmap(nullptr, bytes, PROT, MAP, -1, 0);
  }
  return (p == failed_mmap) ? nullptr : p;
}


template <typename T>
void swap(T &x, T &y) {
  T t = x;
  x = y; y = t;
}

template <typename T>
static void shuffle(std::vector<T> &vec, size_t len) {
  for(size_t i = 0; i < len; i++) {
    size_t j = i + (rand() % (len - i));
    swap(vec[i], vec[j]);
  }
}


template <bool xor_ptrs> node *traverse(node *n, uint64_t iters);
node *atomic_traverse(node *n, uint64_t iters, uint64_t amt);

int run_loaded(uint64_t chain_nodes,
	       bool bind,
	       int max_load_threads,
	       loader_t load = loader_t::read,
	       int step = 8,
	       uint64_t iter_max = (1UL<<27)
	       );
#endif
