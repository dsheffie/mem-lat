/* 4-byte pointer chase: ldr w8, [x8] and friends.
 *
 * A 32-bit load cannot produce a full 64-bit address unless the array lives
 * below 4 GiB, and Apple silicon refuses to run native arm64 binaries with a
 * small __PAGEZERO (the kernel kills them: "Security policy would not allow
 * process"). So the chase ORs the constant high half of the array address
 * back in after each load:
 *
 *     ldr w8, [x8]        ; or [x8, #imm]
 *     orr x8, x9, x8      ; x9 = base & ~0xffffffff
 *
 * That puts one ALU op in the dependency chain, so every number here is one
 * cycle higher than the equivalent in mem_micro; -A 2 (a 64-bit pointer
 * chase with the same orr) calibrates it. The register-indexed form
 * ldr w8, [x0, x8] needs no fix-up since the base carries the high bits.
 * The array is allocated so it does not straddle a 4 GiB boundary.
 *
 * Node k is at base + M + k*S as in mem_micro: -S 8 -M 4 puts every 4-byte
 * pointer at 4 mod 8 (naturally aligned, upper half of an 8-byte word), -M 2
 * inside a word but unaligned, -M 6 straddling two words.
 *
 *   ./chase32 [-c P] [-S stride] [-M misalign] [-A 0|1|2] [-D imm] [-n log2min] [-m log2max]
 *
 *   -A 0  ldr w8, [x8]; orr        32-bit pointer chase (default)
 *   -A 1  ldr w8, [x0, x8]         32-bit offsets from a base register
 *   -A 2  ldr x8, [x8]; orr        64-bit pointer chase with the same orr (calibration)
 *   -A 3  ldr x8, [x8]             64-bit pointer chase, no fix-up (control)
 *   -A 4  ldr x8, [x0, x8]         64-bit offsets (control)
 *   -D d  ldr w8, [x8, #d]; orr    biased 32-bit pointers, d in {4 8 64 1 -4}
 */
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <chrono>
#include <iostream>
#include <algorithm>

#include "perf.hh"
#include "placement.hh"

static volatile uint64_t g_sink;

#define S4(x)  x x x x
#define S32(x) S4(x) S4(x) S4(x) S4(x) S4(x) S4(x) S4(x) S4(x)

__attribute__((noinline)) uint64_t chase_ptr32(uint64_t p, uint64_t hi, uint64_t iters) {
  while(iters) {
    S32(p = hi | *reinterpret_cast<const uint32_t*>(p);)
    iters -= 32;
  }
  return p;
}

__attribute__((noinline)) uint64_t chase_ptr64(uint64_t p, uint64_t hi, uint64_t iters) {
  while(iters) {
    S32(p = hi | *reinterpret_cast<const uint64_t*>(p);)
    iters -= 32;
  }
  return p;
}

__attribute__((noinline)) uint64_t chase_plain64(uint64_t p, uint64_t iters) {
  /* control: the mem_micro pointer chase, no fix-up op */
  while(iters) {
    S32(p = *reinterpret_cast<const uint64_t*>(p);)
    iters -= 32;
  }
  return p;
}

__attribute__((noinline)) uint64_t chase_idx64(const char *base, uint64_t off, uint64_t iters) {
  /* control: 64-bit offsets, ldr x8, [x0, x8] */
  while(iters) {
    S32(off = *reinterpret_cast<const uint64_t*>(base + off);)
    iters -= 32;
  }
  return off;
}

__attribute__((noinline)) uint64_t chase_idx(const char *base, uint64_t off, uint64_t iters) {
  while(iters) {
    S32(off = *reinterpret_cast<const uint32_t*>(base + off);)
    iters -= 32;
  }
  return off;
}

template <int D>
__attribute__((noinline)) uint64_t chase_disp(uint64_t b, uint64_t hi, uint64_t iters) {
  /* stored values are (next - D) truncated to 32 bits; b = hi | value */
  while(iters) {
    S32(b = hi | *reinterpret_cast<const uint32_t*>(b + D);)
    iters -= 32;
  }
  return b;
}
#define DISP_LIST X(4) X(8) X(64) X(1) X(-4)
static uint64_t chase_disp_dyn(int d, uint64_t b, uint64_t hi, uint64_t iters) {
  switch(d) {
#define X(dd) case dd: return chase_disp<dd>(b, hi, iters);
    DISP_LIST
#undef X
  default: return 0;
  }
}

int main(int argc, char *argv[]) {
  int c, mode = 0, disp = 0, lo = 9, hi = 14;
  size_t stride = 8, misalign = 0;
  std::vector<int> cpus;
  while((c = getopt(argc, argv, "c:S:M:A:D:n:m:")) != -1) {
    switch(c) {
    case 'c': cpus = parse_cpu_spec(optarg); break;
    case 'S': stride = std::max(4, atoi(optarg)); break;
    case 'M': misalign = atoi(optarg); break;
    case 'A': mode = atoi(optarg); break;
    case 'D': disp = atoi(optarg); break;
    case 'n': lo = atoi(optarg); break;
    case 'm': hi = atoi(optarg); break;
    default: break;
    }
  }
  if(disp && chase_disp_dyn(disp, 0, 0, 0) != 0) {
    std::cout << "-D needs one of 4 8 64 1 -4\n";
    return -1;
  }
  const uint64_t max_keys = 1ULL << hi;
  const size_t bytes = stride * max_keys + misalign + 256;
  /* any address is fine as long as the array does not straddle a 4 GiB
   * boundary; retry with a hint if it does */
  void *ptr = MAP_FAILED;
  uint64_t hint = 0;
  for(int attempt = 0; attempt < 8; attempt++) {
    ptr = mmap(reinterpret_cast<void*>(hint), bytes, PROT_READ | PROT_WRITE,
               MAP_ANON | MAP_PRIVATE, -1, 0);
    if(ptr == MAP_FAILED) break;
    uint64_t lo_addr = reinterpret_cast<uint64_t>(ptr);
    if((lo_addr >> 32) == ((lo_addr + bytes - 1) >> 32)) break;
    munmap(ptr, bytes);
    hint = ((lo_addr >> 32) + 1) << 32; /* start of the next 4 GiB region */
    ptr = MAP_FAILED;
  }
  if(ptr == MAP_FAILED) {
    std::cout << "could not map the array within one 4 GiB region\n";
    return -1;
  }
  memset(ptr, 0, bytes);
  char *base = reinterpret_cast<char*>(ptr) + misalign;
  const uint64_t hibits = reinterpret_cast<uint64_t>(ptr) & ~0xffffffffULL;
  auto addr = [&](uint64_t k) { return reinterpret_cast<uint64_t>(base + k * stride); };

  bool pinned = pin_to_cpus(cpus, 0);
  {
    /* bring the per-thread counters up before the first timed sample */
    cycle_counter warm;
    g_sink += chase_plain64(reinterpret_cast<uint64_t>(base), 0);
    for(volatile int i = 0; i < 1000000; i++) { }
    g_sink += warm.read_counter();
  }
  const char *mname = disp ? "ldr w, [x, #imm]; orr" : mode == 1 ? "ldr w, [x0, x1]"
                      : mode == 2 ? "ldr x, [x]; orr (64-bit calibration)"
                      : mode == 3 ? "ldr x, [x] (64-bit control)"
                      : mode == 4 ? "ldr x, [x0, x1] (64-bit control)" : "ldr w, [x]; orr";
  std::cout << "mapped at " << ptr << ", stride " << stride << ", misalign " << misalign
            << ", mode " << mname << (disp ? " imm " + std::to_string(disp) : "") << "\n";

  std::vector<uint64_t> keys(max_keys);
  for(int l = lo; l <= hi; l++) {
    uint64_t n = 1ULL << l;
    for(uint64_t i = 0; i < n; i++) keys[i] = i;
    for(uint64_t i = 0; i < n; i++) {
      uint64_t j = i + (rand() % (n - i));
      std::swap(keys[i], keys[j]);
    }
    /* ring: node keys[i] -> node keys[i+1] */
    for(uint64_t i = 0; i < n; i++) {
      uint64_t from = addr(keys[i]), to = addr(keys[(i + 1) % n]);
      if(mode == 2 || mode == 3) {
        memcpy(reinterpret_cast<void*>(from), &to, sizeof(to));
        continue;
      }
      if(mode == 4) {
        uint64_t o = to - reinterpret_cast<uint64_t>(base);
        memcpy(reinterpret_cast<void*>(from), &o, sizeof(o));
        continue;
      }
      uint32_t v;
      if(mode == 1)      v = static_cast<uint32_t>(to - reinterpret_cast<uint64_t>(base));
      else if(disp)      v = static_cast<uint32_t>(to - disp);
      else               v = static_cast<uint32_t>(to);
      memcpy(reinterpret_cast<void*>(from), &v, sizeof(v));
    }
    uint64_t iters = std::max<uint64_t>(n * 16, 1ULL << 22) & ~31ULL;
    uint64_t start = addr(keys[0]);
    double cyc = 0, ns = 0;
    int cpu0 = -1, cpu1 = -1;
    for(int attempt = 0; attempt < 8; attempt++) {
      cycle_counter cc;
      cpu0 = current_cpu();
      auto t0 = std::chrono::high_resolution_clock::now();
      uint64_t c0 = cc.read_counter();
      uint64_t r;
      if(mode == 1)      r = chase_idx(base, start - reinterpret_cast<uint64_t>(base), iters);
      else if(mode == 2) r = chase_ptr64(start, hibits, iters);
      else if(mode == 3) r = chase_plain64(start, iters);
      else if(mode == 4) r = chase_idx64(base, start - reinterpret_cast<uint64_t>(base), iters);
      else if(disp)      r = chase_disp_dyn(disp, start - disp, hibits, iters);
      else               r = chase_ptr32(start, hibits, iters);
      g_sink += r; /* keep the chase alive */
      uint64_t c1 = cc.read_counter();
      auto t1 = std::chrono::high_resolution_clock::now();
      cpu1 = current_cpu();
      cyc = static_cast<double>(c1 - c0) / iters;
      ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
      if(pinned || cpus.empty() || (cpu_in(cpus, cpu0) && cpu_in(cpus, cpu1))) break;
    }
    std::cout << (n * stride) << "," << cyc << " cycles," << ns << " ns,cpu " << cpu1 << "\n";
  }
  return 0;
}
