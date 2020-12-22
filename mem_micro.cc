#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <iostream>
#include <chrono>
#include <cstring>
#include <fstream>

#include "mem_micro.hh"
#include "perf.hh"

#define PROT (PROT_READ | PROT_WRITE)
#define MAP (MAP_ANONYMOUS|MAP_PRIVATE|MAP_POPULATE)

template <typename T>
void swap(T &x, T &y) {
  T t = x;
  x = y; y = t;
}

template <typename T>
void shuffle(std::vector<T> &vec, size_t len) {
  for(size_t i = 0; i < len; i++) {
    size_t j = i + (rand() % (len - i));
    swap(vec[i], vec[j]);
  }
}
  
int main(int argc, char *argv[]) {
  int c;
  uint64_t max_keys = 1UL<<27;
  void *ptr = nullptr;
  node *nodes = nullptr;
  bool xor_pointers = false;

  while ((c = getopt (argc, argv, "m:x:")) != -1) {
    switch(c)
      {
      case 'm':
	max_keys = 1UL << atoi(optarg);
	break;
      case 'x':
	xor_pointers = (atoi(optarg) != 0);
	break;
      default:
	break;
      }
  }

    
  
  std::cout << "running with xor'd pointers = "
	    << xor_pointers << "\n";
  
  ptr = mmap(nullptr, sizeof(node)*max_keys, PROT, MAP|MAP_HUGETLB, -1, 0);

  if(ptr == failed_mmap) {
    ptr = mmap(nullptr, sizeof(node)*max_keys, PROT, MAP, -1, 0);
    if(ptr == failed_mmap) {
      std::cout << "unable to mmap memory\n";
      return -1;
    }
    else {
      std::cout << "unable to allocate memory with hugetlb\n";
    }
  }
  else {
    std::cout << "allocated nodes with hugetlb\n";
  }
  nodes = reinterpret_cast<node*>(ptr);
  
  std::ofstream out("cpu.csv");
  std::vector<uint64_t> keys(max_keys);
  
  for(uint64_t n_keys = 1UL<<8; n_keys <= max_keys; n_keys *= 2) {
    
    for(uint64_t i = 0; i < n_keys; i++) {
      keys[i] = i;
    }
    
    shuffle(keys, n_keys);
    node *h = &nodes[keys[0]];
    node *c = h;  
    h->next = h;
    for(uint64_t i = 1; i < n_keys; i++) {
      node *n = &nodes[keys[i]];
      node *t = c->next;
      c->next = n;
      n->next = t;
      c = n;
    }
    
    if(xor_pointers) {
      for(uint64_t i = 0; i < n_keys; i++) {
	nodes[i].next = xor_ptr<true>(nodes[i].next);
      }
    }
    
    size_t iters = n_keys*16;
    if(iters < (1UL<<20)) {
      iters = (1UL<<20);
    }
    cycle_counter cc;
    cc.reset_counter();    
    auto start = std::chrono::high_resolution_clock::now();
    cc.enable_counter();
    auto c_start = cc.read_counter();
    if(xor_pointers) {
      traverse<true>(h, iters);
    }
    else {
      traverse<false>(h, iters);
    }
    auto c_stop = cc.read_counter();    
    auto stop = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = stop-start;
    double t = elapsed.count() / (1e-9);
    double c_t = static_cast<double>(c_stop-c_start);        
    t /= iters;
    c_t /= iters;
    std::cout << (n_keys*sizeof(node)) << "," << c_t <<" cycles," <<t << " ns \n";
    out << (n_keys*sizeof(node)) << "," << c_t <<"," << t << "\n";
    out.flush();
  }
  out.close();
  munmap(ptr, sizeof(node)*max_keys);  
  return 0;
}
