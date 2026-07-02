#include <sys/mman.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <thread>
#include <atomic>
#include <iostream>
#include <fstream>
#include <chrono>

#include "mem_micro.hh"
#include "perf.hh"

#define PROT (PROT_READ | PROT_WRITE)
#define MAP (MAP_ANONYMOUS|MAP_PRIVATE|MAP_POPULATE)

/* bytes each background load thread streams over, per thread.
 * must be larger than the last-level cache so every pass hits DRAM. */
static const size_t load_bytes = 1UL<<26; /* 64 MB */

static void pin_to_cpu(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

static void *alloc_mem(size_t bytes) {
  void *p = mmap(nullptr, bytes, PROT, MAP|MAP_HUGETLB, -1, 0);
  if(p == failed_mmap) {
    p = mmap(nullptr, bytes, PROT, MAP, -1, 0);
  }
  return (p == failed_mmap) ? nullptr : p;
}

/* streaming-read bandwidth generator: sums a private buffer over and over
 * until told to stop, counting the bytes touched during the measurement. */
static void load_thread(int cpu,
			uint64_t *buf,
			size_t nwords,
			std::atomic<bool> *go,
			std::atomic<bool> *stop,
			std::atomic<uint64_t> *bytes) {
  pin_to_cpu(cpu);
  uint64_t sink = 0, local = 0;
  while(!go->load(std::memory_order_acquire)) { }
  while(!stop->load(std::memory_order_acquire)) {
    uint64_t s = 0;
    for(size_t i = 0; i < nwords; i++) {
      s += buf[i];
    }
    sink += s;
    local += nwords * sizeof(uint64_t);
  }
  bytes->fetch_add(local, std::memory_order_relaxed);
  /* keep the summation live so -O2 cannot delete the reads */
  if(sink == 0xdeadbeefUL) {
    std::cerr << "";
  }
}

template <typename T>
static void swap_v(T &x, T &y) { T t = x; x = y; y = t; }

template <typename T>
static void shuffle(std::vector<T> &vec, size_t len) {
  for(size_t i = 0; i < len; i++) {
    size_t j = i + (rand() % (len - i));
    swap_v(vec[i], vec[j]);
  }
}

/* build a randomized pointer-chase ring over n_keys nodes, return the head */
static node *build_ring(node *nodes, uint64_t n_keys) {
  std::vector<uint64_t> keys(n_keys);
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
  return h;
}

int run_loaded(uint64_t chain_nodes, int max_load_threads) {
  int ncpus = sysconf(_SC_NPROCESSORS_ONLN);
  /* latency thread owns cpu 0, load thread j owns cpu j+1 */
  int cap = ncpus - 1;
  if(max_load_threads < 0 || max_load_threads > cap) {
    max_load_threads = cap;
  }

  void *chain_mem = alloc_mem(sizeof(node) * chain_nodes);
  if(chain_mem == nullptr) {
    std::cout << "unable to mmap chain memory\n";
    return -1;
  }
  node *nodes = reinterpret_cast<node*>(chain_mem);
  node *head = build_ring(nodes, chain_nodes);

  /* private streaming buffer for every possible load thread */
  size_t load_words = load_bytes / sizeof(uint64_t);
  std::vector<uint64_t*> bufs(max_load_threads, nullptr);
  for(int i = 0; i < max_load_threads; i++) {
    bufs[i] = reinterpret_cast<uint64_t*>(alloc_mem(load_bytes));
    if(bufs[i] == nullptr) {
      std::cout << "unable to mmap load buffer\n";
      return -1;
    }
    for(size_t w = 0; w < load_words; w++) {
      bufs[i][w] = w + 1;
    }
  }

  std::cout << "loaded latency: chain = " << (sizeof(node)*chain_nodes)
	    << " bytes, " << load_bytes << " bytes/load-thread, up to "
	    << max_load_threads << " load threads\n";

  std::ofstream out("loaded.csv");
  out << "load_threads,latency_cycles,latency_ns,load_GBps\n";

  pin_to_cpu(0);
  size_t iters = chain_nodes * 8;
  if(iters < (1UL<<22)) {
    iters = (1UL<<22);
  }
  iters &= ~static_cast<size_t>(31); /* traverse unrolls by 32 */

  for(int k = 0; k <= max_load_threads; k++) {
    std::atomic<bool> go(false), stop(false);
    std::atomic<uint64_t> bytes(0);
    std::vector<std::thread> loaders;
    for(int j = 0; j < k; j++) {
      loaders.emplace_back(load_thread, j + 1, bufs[j], load_words,
			   &go, &stop, &bytes);
    }

    cycle_counter cc;
    cc.reset_counter();
    cc.enable_counter();

    /* release the load threads, then measure the latency chain while they run */
    go.store(true, std::memory_order_release);
    auto wall_start = std::chrono::high_resolution_clock::now();
    auto c_start = cc.read_counter();
    traverse<false>(head, iters);
    auto c_stop = cc.read_counter();
    auto wall_stop = std::chrono::high_resolution_clock::now();
    stop.store(true, std::memory_order_release);

    for(auto &t : loaders) {
      t.join();
    }

    std::chrono::duration<double> elapsed = wall_stop - wall_start;
    double ns = (elapsed.count() / 1e-9) / iters;
    double cyc = static_cast<double>(c_stop - c_start) / iters;
    double gbps = static_cast<double>(bytes.load()) / elapsed.count() / 1e9;

    std::cout << k << " load threads: " << cyc << " cycles, " << ns
	      << " ns/access, " << gbps << " GB/s load\n";
    out << k << "," << cyc << "," << ns << "," << gbps << "\n";
    out.flush();
  }

  out.close();
  return 0;
}
