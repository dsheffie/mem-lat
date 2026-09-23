#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <iostream>
#include <chrono>
#include <cstring>
#include <fstream>
#include <cmath>
#include <algorithm>

#include "mem_micro.hh"
#include "perf.hh"
#include "placement.hh"

  
int main(int argc, char *argv[]) {
  int c;
  uint64_t max_keys = 1UL<<23;
  void *ptr = nullptr;
  node *nodes = nullptr;
  bool xor_pointers = false, atomic = false, bind = true;
  int loaded = -2; /* -2 = off; >= -1 selects loaded mode (-1 = all cpus) */
  loader_t load = loader_t::read;
  int step = 1;
  uint64_t max_iters = 1UL<<27;
  int points_per_octave = 1;
  std::vector<int> cpus; /* empty = anywhere */
  while ((c = getopt (argc, argv, "a:b:c:i:m:p:s:tx:L:")) != -1) {
    switch(c)
      {
      case 'a':
	atomic = (atoi(optarg) != 0);
	break;
      case 'b':
	bind = (atoi(optarg) != 0);
	break;
      case 'c':
	cpus = parse_cpu_spec(optarg);
	if(cpus.empty() && strcmp(optarg, "any") != 0) {
	  std::cout << "no cpus match -c " << optarg << "\n";
	  return -1;
	}
	break;
      case 'i':
	max_iters = atoll(optarg);
	break;
      case 'm':
	max_keys = 1UL << atoi(optarg);
	break;
      case 'p':
	points_per_octave = std::max(1, atoi(optarg));
	break;
      case 's':
	step = atoi(optarg);
	break;
      case 't':
	load = loader_t::triad;
	break;
      case 'x':
	xor_pointers = (atoi(optarg) != 0);
	break;
      case 'L':
	loaded = atoi(optarg);
	break;
      default:
	break;
      }
  }

  if(loaded != -2) {
    step = (step < 1) ? 8 : step;
    return run_loaded(max_keys, bind, loaded, load, step, max_iters);
  }

  std::cout << "node size = " << sizeof(node) << ", running with xor'd pointers = "
	    << xor_pointers << "\n";

  /* Linux: hard pin. macOS: QoS hint now, verify each sample below. */
  bool pinned = pin_to_cpus(cpus);
  if(!cpus.empty()) {
    std::cout << (pinned ? "pinned to cpus " : "requiring samples on cpus ")
	      << cpu_list_str(cpus) << "\n";
  }

  ptr = alloc_mem(sizeof(node)*max_keys);
  if(ptr == nullptr) {
    std::cout << "unable to mmap memory\n";
    return -1;
  }
  nodes = reinterpret_cast<node*>(ptr);
  
  std::ofstream out("cpu.csv");
  std::vector<uint64_t> keys(max_keys);

  /* working set sizes: points_per_octave geometrically spaced sizes per
   * doubling, starting at 2 nodes; -p 1 is the classic power-of-two sweep */
  std::vector<uint64_t> sizes;
  for(int octave = 1; ; octave++) {
    bool any = false;
    for(int j = 0; j < points_per_octave; j++) {
      double e = octave + static_cast<double>(j) / points_per_octave;
      uint64_t n = static_cast<uint64_t>(std::pow(2.0, e) + 0.5);
      if(n > max_keys) break;
      if(sizes.empty() || n != sizes.back()) {
	sizes.push_back(n);
      }
      any = true;
    }
    if(!any) break;
  }

  for(uint64_t n_keys : sizes) {
    
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
    if(iters > max_iters) {
      iters = max_iters;
    }
    iters &= ~static_cast<size_t>(31); /* traverse unrolls by 32 */

    /* Without hard pinning, a sample counts only if it started and ended on
     * one of the requested cpus; otherwise redo it (bounded). */
    const int max_tries = pinned || cpus.empty() ? 1 : 8;
    double t = 0.0, c_t = 0.0;
    int cpu0 = -1, cpu1 = -1;
    bool ok = false;
    for(int attempt = 0; attempt < max_tries && !ok; attempt++) {
      cycle_counter cc;
      cc.reset_counter();    
      cpu0 = current_cpu();
      auto start = std::chrono::high_resolution_clock::now();
      cc.enable_counter();
      auto c_start = cc.read_counter();
      if(xor_pointers) {
	traverse<true>(h, iters);
      }
      else {
	if(atomic) {
	  atomic_traverse(h, iters, 0);
	}
	else {
	  traverse<false>(h, iters);
	}
      }
      auto c_stop = cc.read_counter();    
      auto stop = std::chrono::high_resolution_clock::now();
      cpu1 = current_cpu();
      std::chrono::duration<double> elapsed = stop-start;
      t = elapsed.count() / (1e-9) / iters;
      c_t = static_cast<double>(c_stop-c_start) / iters;
      ok = cpus.empty() || pinned ||
	(cpu_in(cpus, cpu0) && cpu_in(cpus, cpu1));
    }
    if(!ok) {
      std::cout << "warn : sample ran on cpu " << cpu0 << "/" << cpu1
		<< " despite -c\n";
    }
    std::cout << (n_keys*sizeof(node)) << "," << c_t <<" cycles," <<t << " ns"
	      << ",cpu " << cpu1 << "\n";
    out << (n_keys*sizeof(node)) << "," << c_t <<"," << t << "," << cpu1 << "\n";
    out.flush();
  }
  out.close();
  munmap(ptr, sizeof(node)*max_keys);  
  return 0;
}
