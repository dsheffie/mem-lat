#!/usr/bin/env python3
import sys
import matplotlib
matplotlib.use('PDF')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages

src = sys.argv[1] if len(sys.argv) > 1 else 'loaded.csv'
h = src.split('.')[0]

threads = []
ns = []
gbps = []

with open(src, 'r') as fp:
    next(fp) # skip header
    for line in fp:
        f = line.strip().split(',')
        threads.append(int(f[0]))
        ns.append(float(f[2]))
        gbps.append(float(f[3]))

pp = PdfPages(h + '.pdf')
plt.figure()
plt.xlabel('load bandwidth (GB/s)')
plt.ylabel('latency (ns)')
plt.title(h + ' memory latency')
plt.plot(gbps, ns, '--o', color='b')
for t, x, y in zip(threads, gbps, ns):
    plt.annotate(str(t), (x, y), textcoords='offset points', xytext=(5, 5))
plt.savefig(pp, format='pdf')

pp.close()
