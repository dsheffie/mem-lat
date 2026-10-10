#!/bin/bash
# Runs the chase32 alignment matrix inside an arm64 Linux guest, where the
# array can be mapped below 4 GiB and 32-bit values are complete pointers.
# From macOS with Apple's container tool (brew install container):
#
#   container system start
#   container run --rm --volume $PWD:/work docker.io/library/gcc:14 bash /work/chase32-linux.sh
#
# The guest has no PMU; the clock is derived from the aligned 64-bit chase
# (3 cycles by definition on this core) and used to convert ns to cycles.
# vCPU 3 is pinned inside the guest; the derived clock tells you which host
# cluster the vCPU thread landed on (about 4.75 GHz = M6 Super core).
mkdir -p /tmp/b && cd /tmp/b
g++ -std=c++11 -O2 -pthread -I/work /work/chase32.cc /work/m1cycles.cc /work/placement.cc -o chase32 || exit 1
G=$(./chase32 -c 3 -n 9 -m 12 -A 3 2>/dev/null | grep -E '^[0-9]+,' | awk -F, '{split($3,a," "); s+=a[1]; n++} END{printf "%.3f", 3.0/(s/n)}')
echo "derived clock from aligned 64-bit chase: $G GHz"
run(){ ./chase32 -c 3 -G $G "$@" 2>/dev/null | grep -E '^[0-9]+,' | awk -F, '{split($2,a," "); s+=a[1]; n++} END{if(n) printf "%5.2f", s/n; else printf "  n/a"}'; }
printf "%-30s %8s %8s %8s %8s %10s %10s %9s\n" "Linux guest, L1, cycles" "x,[x]" "w,[x]" "w,[x,#4]" "w,[x,#1]" "x,[x];orr" "w,[x];orr" "w,[x0,x1]"
while read -r S M lo hi d; do
  printf "S=%-3s M=%-3s %-18s %8s %8s %8s %8s %10s %10s %9s\n" $S $M "$d" \
    "$(run -S $S -M $M -n $lo -m $hi -A 3)" "$(run -S $S -M $M -n $lo -m $hi -L -A 5)" \
    "$(run -S $S -M $M -n $lo -m $hi -L -D 4)" "$(run -S $S -M $M -n $lo -m $hi -L -D 1)" \
    "$(run -S $S -M $M -n $lo -m $hi -A 2)" "$(run -S $S -M $M -n $lo -m $hi -A 0)" "$(run -S $S -M $M -n $lo -m $hi -A 1)"
done <<LIST
8 0 9 13 aligned
8 4 9 13 4-mod-8
8 1 9 13 1-mod-8-in-word
8 2 9 13 2-mod-8-in-word
8 5 9 13 5-mod-8-crosses-8B
8 6 9 13 6-mod-8-crosses-8B
16 12 8 12 12-mod-16-(4B-ok,8B-x)
64 60 6 10 crosses-64B-(8B)
64 62 6 10 crosses-64B-(4B)
LIST
