# mem-lat
Measure memory latency on Linux and macOS (Apple silicon) systems. Attempt to use hugeTLB when possible to avoid TLB miss latency.
Option to xor pointers to defeat linked-list prefetchers.

The default mode sweeps working-set sizes from `-n` (default 4 KiB) up to
`-m` and reports latency per size (`cpu.csv`, plot with `plot.py`). By
default the sizes are powers of two; `-p <n>` places `n` geometrically spaced
sizes per doubling (`-p 4` gives 1, 1.19, 1.41, 1.68 x 2^k) so cache edges
land between the powers of two. `-c` chooses which cpus the measurement may
run on (see Flags). The loaded mode (`-L`) measures latency under bandwidth
load and is described below.

The sweep starts at 4 KiB because rings whose node count divides the 32-way
unroll (2, 4, 8, 16, 32 nodes, i.e. up to 256 bytes) read well under one
cycle per hop on Apple silicon: each unrolled load PC then always sees the
same address and a last-value predictor wins. Rings that do not divide the
unroll (3, 5, 31, 64, ...) are not predicted, so this is a per-PC last-value
predictor; a larger power-of-two unroll would only widen the affected set,
while a prime unroll would shrink it to the trivial ring and the ring equal
to the unroll count. Use `-n 1` to sweep from 16 bytes anyway.

## Loaded latency (`-L`)

```
./mem_micro -m <log2 chain nodes> -L <max load threads>
```

This mode reproduces the classic Intel MLC "loaded latency" experiment:
measure pointer-chase latency on one core while a growing number of other
cores generate memory bandwidth, showing how latency degrades as the memory
subsystem approaches saturation.

### How it works

**The latency thread.** A single chain of `2^m` nodes (8 bytes each, one
`next` pointer per node) is allocated and linked into a randomized ring: node
order is shuffled, so each hop lands at an effectively random address within
the working set. The measurement is a dependent-load pointer chase
(`n = n->next`, unrolled 32×) — every load's address comes from the previous
load, so no memory-level parallelism is possible and the elapsed time per hop
is the true load-to-use latency. With the default `-m 23` the chain is 64 MiB,
comfortably larger than typical last-level caches, so hops miss to DRAM. The
latency thread is pinned to cpu 0. The number of chase iterations is
`min(8 × chain_nodes, -i cap)`, rounded down to a multiple of 32 to match the
unroll factor.

**The load threads.** Each load thread loops over its own private buffer until
told to stop, counting the bytes it touched. Two traffic generators are
available:

- **read** (default): sums a 256 MiB buffer of `uint64_t`s. Pure read
  bandwidth. The running sum is kept live so the compiler cannot delete the
  loop.
- **triad** (`-t`): a STREAM-triad kernel, `C[i] = B[i] + 3*A[i]`, over three
  256 MiB arrays (768 MiB per thread). Mixed read/write bandwidth; the byte
  count credits 3 words (two reads + one write) per element, so write-allocate
  fill traffic is not included in the reported GB/s.

The buffers are much larger than the last-level cache, so every pass streams
from DRAM. By default load thread `j` is pinned to cpu `j+1`; with `-b 0` the
load threads instead float over the process affinity mask and the scheduler
places them (the latency thread stays pinned to cpu 0 either way).

**The sweep.** For each load-thread count `k` from 0 up to the maximum, in
increments of `-s` (the final step is clamped so the maximum itself is always
measured): spawn `k` load threads, release them all at once via an atomic go
flag, run the timed pointer chase while they stream, then stop and join them.
Each step reports the chase latency in both cycles and ns/access and the
aggregate bandwidth the load threads achieved during the chase. The `k = 0`
row is the unloaded baseline. Passing `-L -1` (or any value larger than
`ncpus - 1`) uses every online cpu: one for latency, the rest for load.

Cycles come from `rdtsc` on x86-64 — i.e. constant-rate TSC ticks, not core
clocks, so under DVFS the ns column is the trustworthy one — from a perf
`PERF_COUNT_HW_CPU_CYCLES` counter on other Linux architectures, and from the
kpc fixed cycle counter (true core cycles) on Apple silicon.

**Output.** One line per step on stdout, and `loaded.csv` with columns
`load_threads,latency_cycles,latency_ns,load_GBps`. Plot with
`plot_loaded.py`, which draws the latency-vs-bandwidth curve.

### Options that shape the experiment

- `-m` sets the chain working set. Keep it well above LLC size if you want
  DRAM latency; shrink it to watch cache-resident latency under load instead.
- `-t` switches the load from read-only to triad. Triad drives the memory
  controller with a read/write mix, which typically saturates earlier and
  inflates loaded latency more than pure reads.
- `-s` coarsens the sweep on big machines (e.g. `-s 8` on a 64-core part
  measures k = 0, 8, 16, …, 63 instead of every count).
- `-i` caps chase iterations to bound runtime; each sweep step runs one full
  timed chase, so total runtime scales with (iterations × loaded latency ×
  number of steps).
- `-b 0` unpins the load threads, letting the scheduler migrate them — useful
  for comparing against explicit placement on multi-CCX/multi-socket parts.

## Flags

| Flag | Default | Applies to | Meaning |
|------|---------|------------|---------|
| `-m <n>` | 23 | both modes | log2 of the number of chain nodes. At 8 bytes/node, `-m 23` = 64 MiB, `-m 26` = 512 MiB. Default mode sweeps sizes up to this; loaded mode uses exactly this size. |
| `-n <n>` | 9 | default mode | log2 of the smallest chain to sweep. `-n 9` = 512 nodes = 4 KiB; `-n 1` starts at 16 bytes. |
| `-p <n>` | 1 | default mode | Sizes per octave in the sweep. 1 = powers of two only; `-p 4` adds three geometrically spaced sizes between each pair. |
| `-c <spec>` | any | default mode | Cpus the latency chase may run on: a list/range (`6,7`, `8-11`) or, on macOS, a cluster type letter (`P`, `M`, `E`) resolved through the IO registry. Linux hard-pins with `sched_setaffinity`; macOS cannot pin, so each sample is checked with the cpu it started and ended on and redone (up to 8 times) if it ran elsewhere. The cpu the sample ended on is the fourth column of `cpu.csv`. |
| `-L <n>` | off | selects loaded mode | Run the loaded-latency sweep with up to `n` load threads. `-1` (or anything above `ncpus - 1`) means "all remaining cpus". |
| `-t` | read | loaded mode | Use the STREAM-triad load kernel instead of the read-only summation. |
| `-s <n>` | 1 | loaded mode | Load-thread count increment for the sweep; the maximum count is always included as the final step. Values < 1 are coerced to 8. |
| `-i <n>` | 2^27 | both modes | Upper bound on pointer-chase iterations per size/step (the natural count is 16× the node count in the default sweep, 8× in loaded mode). Rounded down to a multiple of 32. |
| `-b <0\|1>` | 1 | loaded mode | 1 = pin load thread `j` to cpu `j+1`; 0 = let load threads float over the process affinity mask. The latency thread is always pinned to cpu 0. |
| `-x <0\|1>` | 0 | default mode | XOR the stored `next` pointers with a key (undone during the chase) so the in-memory values are not valid addresses, defeating linked-list/pointer prefetchers. |
| `-a <0\|1>` | 0 | default mode | Chase with `atomic_fetch_add(ptr, 0)` instead of plain loads, measuring atomic-RMW latency over the same chain. |

## Huge page behavior

All measurement memory — the chain in both modes, and each load thread's
streaming buffer — goes through one allocator (`alloc_mem` in
`mem_micro.hh`): it first tries
`mmap(MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE | MAP_HUGETLB)`, and if that
fails it prints

```
warn : large page allocation failed, falling back to 4096 byte allocations
```

and retries without `MAP_HUGETLB`. `MAP_POPULATE` prefaults every page in
both cases, so page-fault cost never lands inside the timed region.

Why it matters: with 4 KiB pages, a 64 MiB random chase touches ~16K distinct
pages — far more than any dTLB holds — so most hops pay a TLB miss and a page
walk on top of the memory access, and the walk itself can miss in the caches.
With 2 MiB pages the same chain needs only 32 TLB entries and the measured
number is pure memory latency. If you see the warning, the results are
(memory + TLB-miss) latency, not memory latency.

`MAP_HUGETLB` draws from the *explicit* hugetlb pool, which is empty on most
systems by default. Reserve pages before running, e.g.:

```
echo 4096 | sudo tee /proc/sys/vm/nr_hugepages
```

(uses the default huge page size, 2 MiB on x86-64; check
`grep Huge /proc/meminfo`). Size the pool for everything the run allocates:

- chain: `8 × 2^m` bytes (64 MiB at the default `-m 23`)
- each read load thread: 256 MiB
- each triad load thread: 768 MiB

So `-L 15` with the read loader needs 64 MiB + 15 × 256 MiB ≈ 4 GiB of huge
pages, and the triad loader roughly triples the per-thread part. Each
allocation falls back independently — with an undersized pool the chain may
land on huge pages while later load buffers silently drop to 4 KiB pages
(watch for the warning per allocation).

Note that the fallback path does not `madvise` for transparent huge pages;
whether THP backs the small-page mapping depends on the system-wide setting
(`/sys/kernel/mm/transparent_hugepage/enabled` — `always` will, `madvise`
will not).

## macOS / Apple silicon notes

The cycle counter uses the private `kperf` framework (`m1cycles.cc`, after
Dougall Johnson / Daniel Lemire), which needs root:

```
sudo ./mem_micro ...
```

Without root the cycles column reads 0 and a warning is printed; the ns column
is still valid.

- **Fixed counters only.** macOS 26 refuses every configurable PMU event for
  unentitled processes (`kpc: kpc_set_config: not allowed to count event` in
  the kernel log), and the M1-era event numbers are not valid on the M6 PMU
  anyway. The code tries the configurable setup first and falls back to the
  fixed counters (0 = cycles, 1 = instructions), which is all that is needed
  here. Cycles are real core cycles, not a constant-rate reference clock.
- **No huge pages.** There is no explicit huge page pool for anonymous
  memory and no `MAP_POPULATE`; `alloc_mem` maps normally and prefaults by
  touching every page. Apple silicon uses 16 KiB base pages, so a 64 MiB
  chain is 4K pages rather than 16K, but large working sets still pay TLB
  misses on top of memory latency.
- **No core pinning.** Apple silicon has no thread-to-core affinity API. The
  latency thread and load threads request the `USER_INTERACTIVE` QoS class so
  the scheduler prefers performance cores, but which core each lands on is up
  to the scheduler; `-b` has no effect. Do not expect load thread `k` to map
  to a specific core, and on parts with several core tiers the loaders may
  spill onto efficiency cores as `k` grows.
- **Choosing a core tier with `-c`.** In the default sweep `-c P` (or an
  explicit list such as `-c 6,7`) makes the run verify, via kperf, that each
  sample started and ended on one of those cpus and repeat it otherwise. In
  practice a lone interactive thread is placed on the fastest cluster anyway,
  so the retries rarely fire. The cpu-to-cluster map comes from the IO
  registry; to see it by hand:

  ```
  ioreg -l -w0 | grep -E '"cluster-type"|"logical-cpu-id"'
  ```

  On the M6 Mac mini cpus 0-5 are `E` (Efficiency), 6-7 are `P` (the two
  Super cores, 128 KiB L1D, shared 20 MiB L2) and 8-11 are `M`
  (Performance).

