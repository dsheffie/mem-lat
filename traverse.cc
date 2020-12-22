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
