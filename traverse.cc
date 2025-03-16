#include "mem_micro.hh"

template <bool xor_ptrs>
node *traverse(node *n, uint64_t iters) {
  while(iters) {
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    n = xor_ptr<xor_ptrs>(n->next);
    iters -= 4;
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
    iters -= 4;
  }
  return n;
}
