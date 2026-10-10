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

/* Base + immediate displacement: the ring holds biased pointers (next - D)
 * and the chase loads from biased + D, so the effective address is the
 * same node as the plain pointer chase but the instruction is
 * ldr x8, [x8, #D] (or ldur for negative / unscaled D). */
#define DISP_STEP  b = *reinterpret_cast<const intptr_t*>(b + D);
#define DISP_STEP4  DISP_STEP DISP_STEP DISP_STEP DISP_STEP
#define DISP_STEP16 DISP_STEP4 DISP_STEP4 DISP_STEP4 DISP_STEP4

template <int D>
intptr_t traverse_disp(intptr_t b, uint64_t iters) {
  while(iters) {
    DISP_STEP16 DISP_STEP16
    iters -= 32;
  }
  return b;
}

#define DISP_LIST X(8) X(64) X(256) X(4096) X(32760) X(1) X(-8) X(-256)
#define X(d) template intptr_t traverse_disp<d>(intptr_t, uint64_t);
DISP_LIST
#undef X

intptr_t traverse_disp_dyn(int d, intptr_t b, uint64_t iters) {
  switch(d) {
#define X(dd) case dd: return traverse_disp<dd>(b, iters);
    DISP_LIST
#undef X
  default: return 0;
  }
}
