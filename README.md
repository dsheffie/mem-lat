# mem-lat
Measure memory latency on Linux systems. Attempt to use hugeTLB when possible to avoid TLB miss latency.
Option to xor pointers to defeat linked-list prefetchers.

The default mode sweeps working-set sizes from 16 bytes up to `-m` and reports
latency per size (`cpu.csv`, plot with `plot.py`). The loaded mode (`-L`)
measures latency under bandwidth load and is described below.

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
clocks, so under DVFS the ns column is the trustworthy one — and from a perf
`PERF_COUNT_HW_CPU_CYCLES` counter on other architectures.

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
| `-m <n>` | 23 | both modes | log2 of the number of chain nodes. At 8 bytes/node, `-m 23` = 64 MiB. Default mode sweeps sizes up to this; loaded mode uses exactly this size. |
| `-L <n>` | off | selects loaded mode | Run the loaded-latency sweep with up to `n` load threads. `-1` (or anything above `ncpus - 1`) means "all remaining cpus". |
| `-t` | read | loaded mode | Use the STREAM-triad load kernel instead of the read-only summation. |
| `-s <n>` | 1 | loaded mode | Load-thread count increment for the sweep; the maximum count is always included as the final step. Values < 1 are coerced to 8. |
| `-i <n>` | 2^27 | loaded mode | Upper bound on pointer-chase iterations per sweep step (the natural count is 8× the node count). |
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
