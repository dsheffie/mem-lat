#include "mem_micro.hh"

template <bool xor_ptrs>
node *traverse(node *n, uint64_t iters) {
  while(iters) {
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    iters -= 32;
  }
  return n;
}


template node* traverse<true>(node*, uint64_t);
template node* traverse<false>(node*, uint64_t);

static node *get_next(node *n, uint64_t amt) {
  uint64_t *p = reinterpret_cast<uint64_t*>(n);
  node *o = reinterpret_cast<node*>(__atomic_fetch_add(p, amt, __ATOMIC_RELAXED));
  return o;
}

node *atomic_traverse(node *n, uint64_t iters, uint64_t amt) {
  while(iters) {
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    n = get_next(n, amt);
    iters -= 32;
  }
  return n;
}

/* Same chase, but the ring holds byte offsets (or element indices) relative
 * to a fixed base instead of pointers, so the load must use register-offset
 * addressing: ldr x1, [x0, x1] or ldr x1, [x0, x1, lsl #3] instead of
 * ldr x1, [x1]. Lets you measure whether base+index adds a cycle. */
#define OFF_STEP  off = *reinterpret_cast<const uint64_t*>(base + off);
#define OFF_STEP4  OFF_STEP OFF_STEP OFF_STEP OFF_STEP
#define OFF_STEP16 OFF_STEP4 OFF_STEP4 OFF_STEP4 OFF_STEP4

uint64_t traverse_offset(const node *nodes, uint64_t off, uint64_t iters) {
  const char *base = reinterpret_cast<const char*>(nodes);
  while(iters) {
    OFF_STEP16 OFF_STEP16
    iters -= 32;
  }
  return off;
}

#define IDX_STEP  idx = base[idx];
#define IDX_STEP4  IDX_STEP IDX_STEP IDX_STEP IDX_STEP
#define IDX_STEP16 IDX_STEP4 IDX_STEP4 IDX_STEP4 IDX_STEP4

uint64_t traverse_index(const node *nodes, uint64_t idx, uint64_t iters) {
  const uint64_t *base = reinterpret_cast<const uint64_t*>(nodes);
  while(iters) {
    IDX_STEP16 IDX_STEP16
    iters -= 32;
  }
  return idx;
}
