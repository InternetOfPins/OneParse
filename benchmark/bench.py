#!/usr/bin/env python3
"""
bench.py — OneParse runtime parsing benchmark
Run:     python3 bench.py
Results: results/history.csv  (append-only, one row per run)
Chart:   bench_runtime.png
"""

import os, sys, csv, json, datetime, subprocess
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec

# ── Paths ──────────────────────────────────────────────────────────────────

BENCH_DIR    = os.path.dirname(os.path.abspath(__file__))
SRC          = os.path.join(BENCH_DIR, "bench_runtime.cpp")
BUILD_DIR    = os.path.join(BENCH_DIR, "build")
RESULTS_DIR  = os.path.join(BENCH_DIR, "results")
HISTORY_CSV  = os.path.join(RESULTS_DIR, "history.csv")
DATA_DIR     = os.path.join(BENCH_DIR, "data")
CHART_OUT    = os.path.join(BENCH_DIR, "bench_runtime.png")
LIBRARY_JSON = os.path.join(BENCH_DIR, "..", "library.json")
HAPI_INC     = os.path.join(BENCH_DIR, "..", "..", "HAPI", "include")
OP_INC       = os.path.join(BENCH_DIR, "..", "include")
OUT_INC      = os.path.join(BENCH_DIR, "..", "..", "OneOutput", "include")
LEXY_INC     = os.path.join(BENCH_DIR, "lexy_src", "include")

os.makedirs(BUILD_DIR,   exist_ok=True)
os.makedirs(RESULTS_DIR, exist_ok=True)

# ── Config ─────────────────────────────────────────────────────────────────

ITERS      = 20000
RUNS       = 5
DATA_FILES = ["small.json", "medium.json", "large.json"]
CSV_FIELDS = ["date", "version", "parser", "input",
              "bytes", "iters", "median_ms", "throughput_mbs"]

PARSERS = [
    ("strlen",    ["-std=c++17", "-DPARSER_STRLEN"]),
    ("oneParse",  ["-std=c++17", "-DPARSER_ONEPARSE_STREAM",
                   f"-I{OP_INC}", f"-I{HAPI_INC}", f"-I{OUT_INC}"]),
    # old pointer-based API — needs full rewrite to re-enable
    # ("oneParse",  ["-std=c++17", "-DPARSER_ONEPARSE",  ...]),
    # ("op-nokey",  ["-std=c++17", "-DPARSER_ONEPARSE_NOKEY", ...]),
    ("spirit.x3", ["-std=c++17", "-DPARSER_SPIRIT"]),
    ("lexy",      ["-std=c++20", "-DPARSER_LEXY",
                   f"-I{LEXY_INC}"]),
]

BASE_FLAGS = ["g++", "-O2"]

# ── Version ─────────────────────────────────────────────────────────────────

with open(LIBRARY_JSON) as f:
    version = json.load(f)["version"]
today = datetime.date.today().isoformat()

# ── Compile ─────────────────────────────────────────────────────────────────

print("=== Compiling ===")
binaries = {}
for name, extra_flags in PARSERS:
    exe = os.path.join(BUILD_DIR, "bench_" + name.replace(".", "_"))
    cmd = BASE_FLAGS + extra_flags + ["-o", exe, SRC]  # std flag is in extra_flags
    print(f"  {name:12} ... ", end="", flush=True)
    result = subprocess.run(cmd, capture_output=True)
    if result.returncode != 0:
        print(f"FAILED\n{result.stderr.decode()}")
        sys.exit(1)
    print("ok")
    binaries[name] = exe

# ── Run ─────────────────────────────────────────────────────────────────────

print(f"\n=== Running  iters={ITERS}  runs={RUNS} ===")
rows = []
for name, exe in binaries.items():
    for fname in DATA_FILES:
        fpath = os.path.join(DATA_DIR, fname)
        if not os.path.exists(fpath):
            print(f"  SKIP {fname} (not found)")
            continue
        cmd = [exe, fpath, str(ITERS), str(RUNS)]
        print(f"  {name:12}  {fname:14} ... ", end="", flush=True)
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"FAILED: {res.stderr[:80]}")
            continue
        # stdout: parser,file,bytes,iters,median_ms
        parts = res.stdout.strip().split(",")
        if len(parts) != 5:
            print(f"unexpected output: {res.stdout!r}")
            continue
        _, _, nbytes, _, median_ms = parts
        nbytes    = int(nbytes)
        median_ms = float(median_ms)
        throughput = (nbytes * ITERS) / (median_ms / 1000.0) / 1e6  # MB/s
        rows.append(dict(
            date=today, version=version, parser=name, input=fname,
            bytes=nbytes, iters=ITERS,
            median_ms=round(median_ms, 3),
            throughput_mbs=round(throughput, 2)
        ))
        print(f"{median_ms:8.1f} ms   {throughput:7.2f} MB/s")

# ── Append history ───────────────────────────────────────────────────────────

need_header = not os.path.exists(HISTORY_CSV)
with open(HISTORY_CSV, "a", newline="") as f:
    w = csv.DictWriter(f, fieldnames=CSV_FIELDS)
    if need_header:
        w.writeheader()
    w.writerows(rows)
print(f"\n{len(rows)} rows → {HISTORY_CSV}")

# ── Read full history ────────────────────────────────────────────────────────

with open(HISTORY_CSV, newline="") as f:
    history = list(csv.DictReader(f))

# ── Plot ─────────────────────────────────────────────────────────────────────

COLORS  = {"oneParse": "green", "op-nokey": "limegreen",
           "spirit.x3": "red", "strlen": "gray", "lexy": "steelblue"}
MARKERS = {"small.json": "o", "medium.json": "s", "large.json": "^"}

fig = plt.figure(figsize=(18, 7), layout="constrained")
gs  = gridspec.GridSpec(1, 2, figure=fig, wspace=0.35)
fig.suptitle(
    f"OneParse  v{version}  —  Runtime Parsing Benchmark\n"
    "Flat JSON object  ·  OneParse (HAPI chain) vs Spirit.X3 vs lexy vs strlen",
    fontsize=12
)

# Panel left: current throughput (MB/s) — bar chart by parser × input file
ax1 = fig.add_subplot(gs[0, 0])

parser_names = [n for n, _ in PARSERS if n != "strlen"]
x     = list(range(len(DATA_FILES)))
width = 0.18
for i, pname in enumerate(parser_names):
    vals = []
    for fname in DATA_FILES:
        match = [r for r in rows if r["parser"] == pname and r["input"] == fname]
        vals.append(match[0]["throughput_mbs"] if match else 0)
    offset = (i - (len(parser_names) - 1) / 2) * width
    ax1.bar([xi + offset for xi in x], vals, width,
            label=pname, color=COLORS.get(pname, "blue"), alpha=0.82)

ax1.set_title("Throughput by input size  (higher is faster)")
ax1.set_xlabel("Input")
ax1.set_ylabel("MB / s")
ax1.set_xticks(x)
ax1.set_xticklabels([f.replace(".json", "") for f in DATA_FILES])
ax1.legend()
ax1.grid(axis="y", alpha=0.5)

# Panel right: OneParse throughput over time (one line per input file)
ax2 = fig.add_subplot(gs[0, 1])
op_history = [r for r in history if r["parser"] == "oneParse"]

# Build x-axis labels: unique (date, version) pairs in order of appearance
x_labels = []
seen = set()
for r in op_history:
    key = (r["date"], r["version"])
    if key not in seen:
        seen.add(key)
        x_labels.append(f"{r['date']}\nv{r['version']}")

for fname in DATA_FILES:
    data_for_file = [r for r in op_history if r["input"] == fname]
    # one point per (date, version) pair
    pts = {}
    for r in data_for_file:
        key = (r["date"], r["version"])
        pts[key] = float(r["throughput_mbs"])
    # build aligned series
    all_keys = [(r["date"], r["version"]) for r in op_history
                if r["input"] == fname]
    # deduplicate preserving order
    seen2, ordered = set(), []
    for k in all_keys:
        if k not in seen2:
            seen2.add(k)
            ordered.append(k)
    xs = list(range(len(ordered)))
    ys = [pts[k] for k in ordered]
    lbls = [f"{k[0]}\nv{k[1]}" for k in ordered]
    ax2.plot(xs, ys, marker=MARKERS.get(fname, "o"),
             color="green",
             linestyle=["-", "--", ":"][DATA_FILES.index(fname)],
             label=fname.replace(".json", ""))
    if xs:
        ax2.set_xticks(xs)
        ax2.set_xticklabels(lbls, fontsize=7)

ax2.set_title("OneParse throughput over time  (all inputs)")
ax2.set_xlabel("Run  (date / version)")
ax2.set_ylabel("MB / s")
ax2.legend()
ax2.grid(True, alpha=0.5)

if not op_history:
    ax2.text(0.5, 0.5, "No history yet", transform=ax2.transAxes,
             ha="center", va="center", fontsize=11, color="gray")

plt.savefig(CHART_OUT, dpi=150)
print(f"Chart  →  {CHART_OUT}")
