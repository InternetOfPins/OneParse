#!/usr/bin/env python3
"""Regenerate bench_comparison.png / bench_history.png from existing history.csv
without re-running benchmarks."""

import os, csv, json
import matplotlib.pyplot as plt

BENCH_DIR        = os.path.dirname(os.path.abspath(__file__))
HISTORY_CSV      = os.path.join(BENCH_DIR, "results", "history.csv")
CHART_COMPARISON = os.path.join(BENCH_DIR, "bench_comparison.png")
CHART_HISTORY    = os.path.join(BENCH_DIR, "bench_history.png")
LIBRARY_JSON     = os.path.join(BENCH_DIR, "..", "library.json")

DATA_FILES = ["small.json", "medium.json", "large.json", "longstr.json"]
COLORS = {"oneParse": "green", "op-nokey": "limegreen",
          "spirit.x3": "red", "strlen": "gray", "lexy": "steelblue",
          "pegtl": "darkorange", "simdjson": "purple", "rapidjson": "brown"}
MARKERS = {"small.json": "o", "medium.json": "s", "large.json": "^", "longstr.json": "D"}

# grammar-combinator frameworks (grammar compiled specifically for THIS
# benchmark's flat-object shape) vs simdjson (dedicated, general-purpose,
# hand-tuned, SIMD-accelerated JSON parser).
COMBINATOR_PARSERS = {"lexy", "pegtl", "oneParse", "spirit.x3"}

with open(LIBRARY_JSON) as f:
    version = json.load(f)["version"]

with open(HISTORY_CSV, newline="") as f:
    history = list(csv.DictReader(f))

# Use the most recent run's rows for the left panel
run_id_latest = -1
annotated_all = []
for r in history:
    if r["parser"] == "strlen" and r["input"] == "small.json":
        run_id_latest += 1
    annotated_all.append(dict(r, run_id=run_id_latest))
latest_rows = [r for r in annotated_all if r["run_id"] == run_id_latest]

# ── Chart 1: cross-parser comparison (shareable) ────────────────────────────

fig1 = plt.figure(figsize=(10, 7), layout="constrained")
ax1  = fig1.add_subplot(1, 1, 1)
fig1.suptitle(
    f"OneParse  v{version}  —  Runtime Parsing Benchmark\n"
    "Flat JSON object  ·  OneParse (HAPI chain) vs PEGTL vs simdjson vs lexy vs Spirit.X3 vs RapidJSON",
    fontsize=12
)

parser_names = ["lexy", "pegtl", "oneParse", "simdjson", "spirit.x3", "rapidjson"]
x     = list(range(len(DATA_FILES)))
width = 0.15
for i, pname in enumerate(parser_names):
    vals = []
    for fname in DATA_FILES:
        match = [r for r in latest_rows if r["parser"] == pname and r["input"] == fname]
        vals.append(float(match[0]["throughput_mbs"]) if match else 0)
    offset = (i - (len(parser_names) - 1) / 2) * width
    hatch = "oo" if pname in ("simdjson", "rapidjson") else None
    label = {"simdjson": f"{pname}  (SIMD)", "rapidjson": f"{pname}  (general-purpose)"}.get(pname, pname)
    ax1.bar([xi + offset for xi in x], vals, width,
            label=label,
            color=COLORS.get(pname, "blue"), alpha=0.82, hatch=hatch)

ax1.set_title("Throughput by input size  (higher is faster)")
ax1.set_xlabel("Input")
ax1.set_ylabel("MB / s")
ax1.set_xticks(x)
ax1.set_xticklabels([f.replace(".json", "") for f in DATA_FILES])
ax1.legend()
ax1.grid(axis="y", alpha=0.5)
ax1.text(0.5, -0.14,
          "lexy, PEGTL, oneParse, and Spirit.X3 are grammar-combinator frameworks — each parser is compiled from a\n"
          "grammar definition specific to this benchmark, not a general-purpose library. Circled bars (simdjson,\n"
          "RapidJSON) are dedicated, general-purpose JSON libraries — not a like-for-like comparison.",
          transform=ax1.transAxes, ha="center", va="top", fontsize=8, color="#555555", style="italic")

plt.savefig(CHART_COMPARISON, dpi=150)
print(f"Chart  →  {CHART_COMPARISON}")

# ── Chart 2: OneParse version-over-version history (internal) ──────────────

fig2 = plt.figure(figsize=(10, 7), layout="constrained")
ax2  = fig2.add_subplot(1, 1, 1)

run_id = -1
annotated = []
for r in history:
    if r["parser"] == "strlen" and r["input"] == "small.json":
        run_id += 1
    annotated.append(dict(r, run_id=run_id))

op_rows      = [r for r in annotated if r["parser"] == "oneParse"]
runs_with_op = sorted({r["run_id"] for r in op_rows})
run_to_xi    = {rid: xi for xi, rid in enumerate(runs_with_op)}

ver_counts, x_labels = {}, []
for rid in runs_with_op:
    ver = next(r["version"] for r in annotated if r["run_id"] == rid)
    ver_counts[ver] = ver_counts.get(ver, 0) + 1
    x_labels.append(f"v{ver}\n#{ver_counts[ver]}")

ver_spans = {}
for xi, rid in enumerate(runs_with_op):
    ver = next(r["version"] for r in annotated if r["run_id"] == rid)
    lo, hi = ver_spans.get(ver, (xi, xi))
    ver_spans[ver] = (min(lo, xi), max(hi, xi))
shade_colors = ["#e8f4e8", "#d0e8ff", "#fff0d0", "#fde8e8"]
for i, (ver, (lo, hi)) in enumerate(sorted(ver_spans.items())):
    ax2.axvspan(lo - 0.4, hi + 0.4, alpha=0.35,
                color=shade_colors[i % len(shade_colors)], zorder=0)

for fi, fname in enumerate(DATA_FILES):
    xs_f, ys_f = [], []
    for rid in runs_with_op:
        match = [r for r in op_rows if r["run_id"] == rid and r["input"] == fname]
        if match:
            xs_f.append(run_to_xi[rid])
            ys_f.append(float(match[0]["throughput_mbs"]))
    if xs_f:
        ax2.plot(xs_f, ys_f,
                 marker=MARKERS.get(fname, "o"), markersize=5,
                 color="green",
                 linestyle=["-", "--", ":", (0,(3,1))][fi],
                 label=fname.replace(".json", ""), zorder=2)

for i, (ver, (lo, hi)) in enumerate(sorted(ver_spans.items())):
    ax2.text((lo + hi) / 2, 1.0, f"v{ver}",
             ha="center", va="bottom", fontsize=7, color="#555555",
             transform=ax2.get_xaxis_transform())

xs_all = list(range(len(runs_with_op)))
ax2.set_xticks(xs_all)
ax2.set_xticklabels(x_labels, fontsize=6)
ax2.set_title("OneParse throughput — every run  (all inputs)")
ax2.set_xlabel("Run")
ax2.set_ylabel("MB / s")
ax2.legend()
ax2.grid(True, alpha=0.5)

if not op_rows:
    ax2.text(0.5, 0.5, "No history yet", transform=ax2.transAxes,
             ha="center", va="center", fontsize=11, color="gray")

plt.savefig(CHART_HISTORY, dpi=150)
print(f"Chart  →  {CHART_HISTORY}")
