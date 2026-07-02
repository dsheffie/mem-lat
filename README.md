# mem-lat
Measure memory latency on Linux systems. Attempt to use hugeTLB when possible to avoid TLB miss latency.
Option to xor pointers to defeat linked-list prefetchers.

## Loaded latency (`-L`)
`./mem_micro -m <log2 chain nodes> -L <max load threads>` measures pointer-chase
latency while background threads generate streaming-read bandwidth (MLC-style
loaded latency). Pass `-L -1` to use all available cores. The latency thread is
pinned to cpu 0; load thread `j` is pinned to cpu `j+1`. For each load-thread
count `k` in `0..max` it reports latency (cycles and ns/access) and the achieved
aggregate load bandwidth, writing `loaded.csv` (`load_threads,latency_cycles,latency_ns,load_GBps`).