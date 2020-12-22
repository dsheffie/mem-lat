#ifndef __mem_micro__
#define __mem_micro__

#include <cstdint>

static const uint64_t ptr_key = 0x1234567076543210UL;

struct node {
  node *next;
};

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

template <bool xor_ptrs> node *traverse(node *n, uint64_t iters);

#endif
