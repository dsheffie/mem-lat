#!/usr/bin/env python3
"""Plot mem_micro default-sweep csvs (size,cycles,ns[,cpu]) as a pdf.

usage: plot.py CSV [CSV ...] [--label L ...] [--out NAME.pdf] [--title T]
               [--mark BYTES:LABEL ...] [--png] [--linear-y] [--no-table]
               [--no-knees] [--knee-rise R]

One csv gives the single-series chart with plateau knees marked. Several
csvs are overlaid as one series each (give a --label per csv) with a legend;
knees are not marked in that mode. Page 1 is the chart, both axes log scaled
by default (capacity base 2, latency base 10) plus a panel with the implied
core clock (cycles / ns); the following pages tabulate the raw rows.

--mark draws a labelled vertical guide at a capacity, e.g.
    --mark 131072:L1D --mark 20971520:L2
"""
import argparse
import os
import re
import sys
import matplotlib
matplotlib.use('PDF')
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter, LogLocator, NullFormatter
from matplotlib.backends.backend_pdf import PdfPages

perf_re = re.compile(r'(\d+),(\d+(?:\.\d+)?(?:e[+-]?\d+)?),(\d+(?:\.\d+)?(?:e[+-]?\d+)?)')

ap = argparse.ArgumentParser()
ap.add_argument('src', nargs='+')
ap.add_argument('--label', action='append', default=[], help='series label, one per csv')
ap.add_argument('--out', default=None, help='output pdf (default <first csv stem>.pdf)')
ap.add_argument('--title', default=None)
ap.add_argument('--mark', action='append', default=[],
                help='BYTES:LABEL vertical guide (repeatable)')
ap.add_argument('--png', action='store_true', help='also write png(s)')
ap.add_argument('--linear-y', action='store_true', help='linear latency axis (default log)')
ap.add_argument('--no-table', action='store_true', help='omit the raw data table pages')
ap.add_argument('--no-knees', action='store_true', help='do not mark plateau knees')
ap.add_argument('--knee-rise', type=float, default=1.15,
                help='latency ratio between neighbours that counts as leaving a plateau')
args = ap.parse_args()


def load(path):
    sizes, cycles, ns, cpus = [], [], [], []
    with open(path) as fp:
        for line in fp:
            m = perf_re.match(line)
            if m is None:
                continue
            sizes.append(int(m.group(1)))
            cycles.append(float(m.group(2)))
            ns.append(float(m.group(3)))
            rest = line.strip().split(',')
            cpus.append(rest[3] if len(rest) > 3 else '')
    ghz = [c / t if t > 0 else 0.0 for c, t in zip(cycles, ns)]
    return {'sizes': sizes, 'cycles': cycles, 'ns': ns, 'ghz': ghz, 'cpus': cpus}


series = [load(p) for p in args.src]
labels = list(args.label)
while len(labels) < len(series):
    labels.append(os.path.splitext(os.path.basename(args.src[len(labels)]))[0])
multi = len(series) > 1
if multi and len(labels) > 4:
    sys.exit('at most 4 series per chart')

stem = os.path.splitext(os.path.basename(args.src[0]))[0]
title = args.title or (stem + ' memory latency')

# palette: categorical hues in fixed order (blue, orange, aqua, yellow);
# ink/axes in muted greys
HUES = ['#2a78d6', '#eb6834', '#1baf7a', '#eda100']
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


def find_knees(ys, rise, min_gap=6):
    """Indices where a plateau ends: the two steps into point i are both
    below `rise` and the step out of it is at or above `rise`. Tolerates the
    gentle slope a shared cache shows before its edge; in a bumpy region only
    the first hit within `min_gap` samples is kept."""
    knees = []
    for i in range(2, len(ys) - 1):
        left_gentle = ys[i] / ys[i - 1] < rise and ys[i - 1] / ys[i - 2] < rise
        if left_gentle and ys[i + 1] / ys[i] >= rise:
            if knees and i - knees[-1] <= min_gap:
                continue
            knees.append(i)
    return knees


knees = [] if (args.no_knees or multi) else find_knees(series[0]['cycles'], args.knee_rise)

plt.rcParams.update({
    'font.size': 9, 'axes.edgecolor': AXIS, 'axes.labelcolor': INK,
    'xtick.color': MUTED, 'ytick.color': MUTED, 'axes.titlecolor': INK,
    'axes.spines.top': False, 'axes.spines.right': False,
})

fig, (ax_c, ax_n, ax_f) = plt.subplots(
    3, 1, sharex=True, figsize=(7.5, 7.4),
    gridspec_kw={'hspace': 0.12, 'height_ratios': [3, 3, 1.1]})
fig.suptitle(title, x=0.02, ha='left', fontsize=12, color=INK)

all_sizes = [s for d in series for s in d['sizes']]

for ax, key, ylabel, unit in ((ax_c, 'cycles', 'latency (core cycles)', 'cyc'),
                              (ax_n, 'ns', 'latency (ns)', 'ns')):
    lo = min(min(d[key]) for d in series)
    hi = max(max(d[key]) for d in series)
    for k, d in enumerate(series):
        ax.plot(d['sizes'], d[key], color=HUES[k], linewidth=1.6,
                marker='o', markersize=3, markerfacecolor='white',
                markeredgewidth=1.0, zorder=3, label=labels[k])
    ax.set_ylabel(ylabel)
    if args.linear_y:
        ax.set_ylim(bottom=0)
    else:
        ax.set_yscale('log')
        ax.set_ylim(lo * 0.55, hi * 1.6)
        ax.yaxis.set_major_locator(LogLocator(base=10, numticks=8))
        ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: '%g' % v))
        # label the 2/3/5 minor ticks so a plateau at 3 is readable
        ax.yaxis.set_minor_locator(LogLocator(base=10, subs=(2, 3, 5), numticks=12))
        ax.yaxis.set_minor_formatter(FuncFormatter(lambda v, _p: '%g' % v))
        ax.tick_params(axis='y', which='minor', labelsize=7, labelcolor=MUTED)
    ax.grid(True, axis='y', which='major', color=GRID, linewidth=0.8, zorder=0)
    if not args.linear_y:
        ax.grid(True, axis='y', which='minor', color=GRID, linewidth=0.4, zorder=0)
    ax.tick_params(length=0, which='both')
    ax.spines['left'].set_visible(False)

    if multi:
        # legend carries each series' first-plateau value (no start labels,
        # which would collide where the curves coincide)
        handles, _ = ax.get_legend_handles_labels()
        ax.legend(handles, ['%s   %.1f %s at %s' % (labels[k], d[key][0], unit,
                                                     fmt_bytes(d['sizes'][0]))
                            for k, d in enumerate(series)],
                  loc='upper left', frameon=False, fontsize=8,
                  labelcolor=INK, handlelength=1.6)
        # values at the last point, stacked by rank so they never overlap
        ends = sorted(range(len(series)), key=lambda k: series[k][key][-1])
        n = len(ends)
        for rank, k in enumerate(ends):
            d = series[k]
            ax.annotate('%.1f' % d[key][-1], (d['sizes'][-1], d[key][-1]),
                        xytext=(4, (rank - (n - 1) / 2.0) * 9),
                        textcoords='offset points',
                        va='center', fontsize=7.5, color=HUES[k])
    else:
        d = series[0]
        ys = d[key]
        # direct labels: first plateau level, last point, and each knee
        ax.annotate('%.1f %s' % (ys[0], unit), (d['sizes'][0], ys[0]), xytext=(0, 7),
                    textcoords='offset points', ha='left', va='bottom',
                    fontsize=8, color=INK)
        ax.annotate('%.1f %s' % (ys[-1], unit), (d['sizes'][-1], ys[-1]), xytext=(4, 0),
                    textcoords='offset points', va='center', fontsize=8, color=INK)
        for i in knees:
            ax.plot([d['sizes'][i]], [ys[i]], marker='o', markersize=6, color=HUES[0],
                    markeredgecolor='white', markeredgewidth=1.2, zorder=4)
            ax.annotate('%s\n%.1f %s' % (fmt_bytes(d['sizes'][i]), ys[i], unit),
                        (d['sizes'][i], ys[i]), xytext=(-8, 14), textcoords='offset points',
                        ha='right', va='bottom', fontsize=7.5, color=INK,
                        arrowprops=dict(arrowstyle='-', color=MUTED, linewidth=0.7,
                                        shrinkA=0, shrinkB=3))

# implied core clock per sample: shows DVFS shifts that would otherwise hide
# in the cycles column (e.g. a cluster changing frequency mid-sweep)
for k, d in enumerate(series):
    ax_f.plot(d['sizes'], d['ghz'], color=HUES[k], linewidth=1.2, marker='o',
              markersize=2.5, markerfacecolor='white', markeredgewidth=0.9, zorder=3)
ax_f.set_ylabel('clock (GHz)')
ax_f.set_ylim(0, max(max(d['ghz']) for d in series) * 1.35)
ax_f.grid(True, axis='y', color=GRID, linewidth=0.8, zorder=0)
ax_f.tick_params(length=0)
ax_f.spines['left'].set_visible(False)
if not multi:
    ax_f.annotate('%.2f' % series[0]['ghz'][-1], (series[0]['sizes'][-1], series[0]['ghz'][-1]),
                  xytext=(4, 0), textcoords='offset points', va='center', fontsize=8, color=INK)
ax_f.annotate('cycles / ns', (0.01, 0.92), xycoords='axes fraction',
              va='top', fontsize=7.5, color=MUTED)

ax_f.set_xscale('log', base=2)
ax_f.set_xlabel('working set (bytes, log2)')
ax_f.xaxis.set_major_locator(LogLocator(base=2, numticks=32))
ax_f.xaxis.set_major_formatter(FuncFormatter(fmt_bytes))
ax_f.xaxis.set_minor_formatter(NullFormatter())
ax_f.set_xlim(min(all_sizes) * 0.8, max(all_sizes) * 1.6)
# thin the tick labels so they don't collide
for i, lab in enumerate(ax_f.get_xticklabels()):
    if i % 2:
        lab.set_visible(False)
plt.setp(ax_f.get_xticklabels(), rotation=45, ha='right')

for spec in args.mark:
    b, _, label = spec.partition(':')
    b = int(b)
    for ax in (ax_c, ax_n, ax_f):
        ax.axvline(b, color=AXIS, linewidth=0.9, linestyle=(0, (4, 3)), zorder=1)
    ax_c.annotate(label or fmt_bytes(b), (b, 1), xycoords=('data', 'axes fraction'),
                  xytext=(3, -2), textcoords='offset points', va='top',
                  fontsize=8, color=MUTED)

out = args.out or (stem + '.pdf')


def table_rows():
    """(header, rows) for the raw data pages."""
    if not multi:
        d = series[0]
        header = '%-12s %14s %10s %10s %7s %5s' % ('size', 'bytes', 'cycles', 'ns', 'GHz', 'cpu')
        rows = ['%-12s %14d %10.2f %10.3f %7.2f %5s' % (fmt_bytes(b), b, c, t, g, u)
                for b, c, t, g, u in zip(d['sizes'], d['cycles'], d['ns'], d['ghz'], d['cpus'])]
        return header, rows
    # several series: align by row index and warn if the sizes differ
    n = min(len(d['sizes']) for d in series)
    if any(d['sizes'][:n] != series[0]['sizes'][:n] for d in series):
        print('warning: series have different sizes; table aligned by row', file=sys.stderr)
    # short ids keep the page narrow; the key line spells them out
    ids = 'ABCD'[:len(series)]
    header = '%-10s %12s' % ('size', 'bytes')
    header += ''.join('  %6s %7s' % (i + ' cyc', i + ' ns') for i in ids)
    rows = []
    for i in range(n):
        b = series[0]['sizes'][i]
        r = '%-10s %12d' % (fmt_bytes(b), b)
        r += ''.join('  %6.2f %7.2f' % (d['cycles'][i], d['ns'][i]) for d in series)
        rows.append(r)
    return header, rows


def table_key():
    if not multi:
        return ', '.join(os.path.basename(p) for p in args.src)
    return '    '.join('%s = %s (%s)' % (i, l, os.path.basename(p))
                       for i, l, p in zip('ABCD', labels, args.src))


def table_pages(pdf, rows_per_page=52):
    """Raw data as monospaced text pages after the chart."""
    header, rows = table_rows()
    npages = max(1, (len(rows) + rows_per_page - 1) // rows_per_page)
    for pg in range(npages):
        chunk = rows[pg * rows_per_page:(pg + 1) * rows_per_page]
        tfig = plt.figure(figsize=(7.5, 9.5))
        tfig.text(0.06, 0.965, title + ' - raw data (%d/%d)' % (pg + 1, npages),
                  fontsize=11, color=INK, va='top')
        tfig.text(0.06, 0.935, table_key(), fontsize=8, color=MUTED, va='top')
        y = 0.905
        tfig.text(0.06, y, header, family='monospace', fontsize=7.5, color=MUTED, va='top')
        tfig.add_artist(plt.Line2D([0.06, 0.94], [y - 0.016, y - 0.016],
                                   color=AXIS, linewidth=0.8))
        y -= 0.024
        for r in chunk:
            tfig.text(0.06, y, r, family='monospace', fontsize=7.5, color=INK, va='top')
            y -= 0.0158
        pdf.savefig(tfig)
        if args.png:
            tfig.savefig('%s-table%d.png' % (os.path.splitext(out)[0], pg + 1),
                         format='png', dpi=150)
        plt.close(tfig)


with PdfPages(out) as pdf:
    pdf.savefig(fig, bbox_inches='tight')
    if not args.no_table:
        table_pages(pdf)
if args.png:
    fig.savefig(os.path.splitext(out)[0] + '.png', format='png', dpi=150, bbox_inches='tight')
print('wrote', out)
