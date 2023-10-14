#!/usr/bin/env python3
import glob
import os
import re
import subprocess
import time
import sys
import matplotlib
import numpy as np
matplotlib.use('PDF')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages

perf_re = re.compile('(\d+),(\d+(\.\d+)?),(\d+(\.\d+)?)')

ilist = []
clist = []

src = sys.argv[1]
h = src.split('.')[0]

with open(src, 'r') as fp:
    for line in fp:
        mtx = re.match(perf_re, line)
        g = mtx.groups()
        size = int(g[0])
        cycles = float(g[1])
        ilist.append(size)
        clist.append(cycles)

pp = PdfPages(h + '.pdf')
plt.figure()
plt.xlabel('size')
plt.ylabel('latency (cycles)')
plt.title(h + ' memory hierarchy')
plt.xscale('log',basex=2)
plt.plot(ilist,clist,'--b')
plt.savefig(pp,format='pdf')

pp.close()
