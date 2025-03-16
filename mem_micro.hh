#ifndef __mem_micro__
#define __mem_micro__

#include <cstdint>

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

template <bool xor_ptrs> node *traverse(node *n, uint64_t iters);
node *atomic_traverse(node *n, uint64_t iters, uint64_t amt);
#endif
