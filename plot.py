#!/usr/bin/env python3
"""Plot a mem_micro default-sweep csv (size,cycles,ns[,cpu]) as <name>.pdf.

usage: plot.py [cpu.csv] [--out NAME.pdf] [--title T] [--mark BYTES:LABEL ...]
               [--png] [--linear-y] [--no-table]

Page 1 is the chart, both axes log scaled by default (capacity base 2,
latency base 10); the following pages tabulate the raw csv rows.

--mark draws a labelled vertical guide at a capacity, e.g.
    --mark 131072:L1D --mark 20971520:L2
"""
import argparse
import os
import re
import matplotlib
matplotlib.use('PDF')
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter, LogLocator, NullFormatter
from matplotlib.backends.backend_pdf import PdfPages

perf_re = re.compile(r'(\d+),(\d+(?:\.\d+)?(?:e[+-]?\d+)?),(\d+(?:\.\d+)?(?:e[+-]?\d+)?)')

ap = argparse.ArgumentParser()
ap.add_argument('src', nargs='?', default='cpu.csv')
ap.add_argument('--out', default=None, help='output pdf (default <csv stem>.pdf)')
ap.add_argument('--title', default=None)
ap.add_argument('--mark', action='append', default=[],
                help='BYTES:LABEL vertical guide (repeatable)')
ap.add_argument('--png', action='store_true', help='also write a png')
ap.add_argument('--linear-y', action='store_true', help='linear latency axis (default log)')
ap.add_argument('--no-table', action='store_true', help='omit the raw data table pages')
args = ap.parse_args()

sizes, cycles, ns, cpus = [], [], [], []
with open(args.src) as fp:
    for line in fp:
        m = perf_re.match(line)
        if m is None:
            continue
        sizes.append(int(m.group(1)))
        cycles.append(float(m.group(2)))
        ns.append(float(m.group(3)))
        rest = line.strip().split(',')
        cpus.append(rest[3] if len(rest) > 3 else '')

stem = os.path.splitext(os.path.basename(args.src))[0]
title = args.title or (stem + ' memory latency')

# palette: one series -> one hue; ink/axes in muted greys
SERIES = '#2a78d6'
INK = '#1f1e1c'
MUTED = '#898781'
AXIS = '#c3c2b7'
GRID = '#e8e7e2'

def fmt_bytes(x, _pos=None):
    if x >= 1 << 30:
        v, u = x / (1 << 30), 'GiB'
    elif x >= 1 << 20:
        v, u = x / (1 << 20), 'MiB'
    elif x >= 1 << 10:
        v, u = x / (1 << 10), 'KiB'
    else:
        v, u = x, 'B'
    return ('%d %s' % (v, u)) if float(v).is_integer() else ('%.3g %s' % (v, u))

plt.rcParams.update({
    'font.size': 9, 'axes.edgecolor': AXIS, 'axes.labelcolor': INK,
    'xtick.color': MUTED, 'ytick.color': MUTED, 'axes.titlecolor': INK,
    'axes.spines.top': False, 'axes.spines.right': False,
})

fig, (ax_c, ax_n) = plt.subplots(2, 1, sharex=True, figsize=(7.5, 6.2),
                                 gridspec_kw={'hspace': 0.12})
fig.suptitle(title, x=0.02, ha='left', fontsize=12, color=INK)

for ax, ys, ylabel in ((ax_c, cycles, 'latency (core cycles)'),
                       (ax_n, ns, 'latency (ns)')):
    ax.plot(sizes, ys, color=SERIES, linewidth=1.6,
            marker='o', markersize=3, markerfacecolor='white',
            markeredgewidth=1.0, zorder=3)
    ax.set_ylabel(ylabel)
    if args.linear_y:
        ax.set_ylim(bottom=0)
    else:
        ax.set_yscale('log')
        ax.set_ylim(min(ys) * 0.7, max(ys) * 1.5)
        ax.yaxis.set_major_locator(LogLocator(base=10, numticks=8))
        ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: '%g' % v))
        ax.yaxis.set_minor_formatter(NullFormatter())
    ax.grid(True, axis='y', which='major', color=GRID, linewidth=0.8, zorder=0)
    if not args.linear_y:
        ax.grid(True, axis='y', which='minor', color=GRID, linewidth=0.4, zorder=0)
    ax.tick_params(length=0)
    for spine in ('left',):
        ax.spines[spine].set_visible(False)
    # direct label on the last point only
    ax.annotate('%.1f' % ys[-1], (sizes[-1], ys[-1]), xytext=(4, 0),
                textcoords='offset points', va='center', fontsize=8, color=INK)

ax_n.set_xscale('log', base=2)
ax_n.set_xlabel('working set (bytes, log2)')
ax_n.xaxis.set_major_locator(LogLocator(base=2, numticks=32))
ax_n.xaxis.set_major_formatter(FuncFormatter(fmt_bytes))
ax_n.xaxis.set_minor_formatter(NullFormatter())
ax_n.set_xlim(min(sizes) * 0.8, max(sizes) * 1.6)
# thin the tick labels so they don't collide
for i, lab in enumerate(ax_n.get_xticklabels()):
    if i % 2:
        lab.set_visible(False)
plt.setp(ax_n.get_xticklabels(), rotation=45, ha='right')

for spec in args.mark:
    b, _, label = spec.partition(':')
    b = int(b)
    for ax in (ax_c, ax_n):
        ax.axvline(b, color=AXIS, linewidth=0.9, linestyle=(0, (4, 3)), zorder=1)
    ax_c.annotate(label or fmt_bytes(b), (b, 1), xycoords=('data', 'axes fraction'),
                  xytext=(3, -2), textcoords='offset points', va='top',
                  fontsize=8, color=MUTED)

def table_pages(pdf, rows_per_page=52):
    """Raw data as monospaced text pages after the chart."""
    header = '%-12s %14s %10s %10s %5s' % ('size', 'bytes', 'cycles', 'ns', 'cpu')
    rows = ['%-12s %14d %10.2f %10.3f %5s' % (fmt_bytes(b), b, c, t, u)
            for b, c, t, u in zip(sizes, cycles, ns, cpus)]
    npages = max(1, (len(rows) + rows_per_page - 1) // rows_per_page)
    for pg in range(npages):
        chunk = rows[pg * rows_per_page:(pg + 1) * rows_per_page]
        tfig = plt.figure(figsize=(7.5, 9.5))
        tfig.text(0.06, 0.965, title + ' - raw data (%d/%d)' % (pg + 1, npages),
                  fontsize=11, color=INK, va='top')
        tfig.text(0.06, 0.935, os.path.basename(args.src), fontsize=8,
                  color=MUTED, va='top')
        y = 0.905
        tfig.text(0.06, y, header, family='monospace', fontsize=8, color=MUTED, va='top')
        tfig.add_artist(plt.Line2D([0.06, 0.94], [y - 0.016, y - 0.016],
                                   color=AXIS, linewidth=0.8))
        y -= 0.024
        for r in chunk:
            tfig.text(0.06, y, r, family='monospace', fontsize=8, color=INK, va='top')
            y -= 0.0158
        pdf.savefig(tfig)
        if args.png:
            tfig.savefig('%s-table%d.png' % (os.path.splitext(out)[0], pg + 1),
                         format='png', dpi=150)
        plt.close(tfig)

out = args.out or (stem + '.pdf')
with PdfPages(out) as pdf:
    pdf.savefig(fig, bbox_inches='tight')
    if not args.no_table:
        table_pages(pdf)
if args.png:
    fig.savefig(os.path.splitext(out)[0] + '.png', format='png', dpi=150, bbox_inches='tight')
print('wrote', out)
