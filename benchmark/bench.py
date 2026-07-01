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
LEXY_INC     = os.path.join(BENCH_DIR, "lexy_src",    "include")
PEGTL_INC    = os.path.join(BENCH_DIR, "pegtl_src",   "include")
SIMDJSON_SRC = os.path.join(BENCH_DIR, "simdjson_src")

os.makedirs(BUILD_DIR,   exist_ok=True)
os.makedirs(RESULTS_DIR, exist_ok=True)

# ── Config ─────────────────────────────────────────────────────────────────

ITERS      = 20000
RUNS       = 10
DATA_FILES = ["small.json", "medium.json", "large.json", "longstr.json"]
CSV_FIELDS = ["date", "version", "parser", "input",
              "bytes", "iters", "median_ms", "throughput_mbs"]

PARSERS = [
    ("strlen",    ["-std=c++17", "-DPARSER_STRLEN"]),
    ("oneParse",  ["-std=c++17", "-DPARSER_ONEPARSE_INDEX",
                   f"-I{OP_INC}", f"-I{HAPI_INC}", f"-I{OUT_INC}"]),
    # previous streaming (JsonObj/Meta/Alt) implementation — kept in
    # bench_runtime.cpp for reference, no longer the reported "oneParse" row
    # ("oneParse-stream", ["-std=c++17", "-DPARSER_ONEPARSE_STREAM",
    #                f"-I{OP_INC}", f"-I{HAPI_INC}", f"-I{OUT_INC}"]),
    # old pointer-based API — needs full rewrite to re-enable
    # ("oneParse",  ["-std=c++17", "-DPARSER_ONEPARSE",  ...]),
    # ("op-nokey",  ["-std=c++17", "-DPARSER_ONEPARSE_NOKEY", ...]),
    ("spirit.x3", ["-std=c++17", "-DPARSER_SPIRIT"]),
    ("lexy",      ["-std=c++20", "-DPARSER_LEXY",
                   f"-I{LEXY_INC}"]),
    ("pegtl",     ["-std=c++17", "-DPARSER_PEGTL",
                   f"-I{PEGTL_INC}"]),
    ("simdjson",  ["-std=c++17", "-DPARSER_SIMDJSON",
                   f"-I{SIMDJSON_SRC}",
                   f"{SIMDJSON_SRC}/simdjson.cpp"]),
]

BASE_FLAGS = ["g++", "-O2"]

# ── Version ─────────────────────────────────────────────────────────────────

with open(LIBRARY_JSON) as f:
    version = json.load(f)["version"]
today = datetime.date.today().isoformat()

# ── Validate (op-index only) ─────────────────────────────────────────────────
#
# Correctness gate for the "oneParse" (PARSER_ONEPARSE_INDEX) fast scan/walk
# extraction: built as a SEPARATE binary (-DBENCH_VALIDATE), never the timed
# one, so validation code can't skew codegen or the measured numbers. Compares
# against an independent reference extractor (no memchr, no structural table)
# baked into bench_runtime.cpp under the same guard. Aborts the whole run if
# any fixture fails, before any timing happens.

print("=== Validating oneParse extraction ===")
validate_exe = os.path.join(BUILD_DIR, "bench_validate")
result = subprocess.run(
    BASE_FLAGS + ["-std=c++17", "-DPARSER_ONEPARSE_INDEX", "-DBENCH_VALIDATE",
                  f"-I{OP_INC}", f"-I{HAPI_INC}", f"-I{OUT_INC}",
                  "-o", validate_exe, SRC],
    capture_output=True)
if result.returncode != 0:
    print(f"  build FAILED\n{result.stderr.decode()}")
    sys.exit(1)

validate_failed = False
for fname in DATA_FILES:
    fpath = os.path.join(DATA_DIR, fname)
    res = subprocess.run([validate_exe, fpath], capture_output=True, text=True)
    print(f"  {res.stdout.strip()}")
    if res.returncode != 0:
        print(res.stderr)
        validate_failed = True
if validate_failed:
    print("=== Validation FAILED — aborting before timed run ===")
    sys.exit(1)
print()

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
           "spirit.x3": "red", "strlen": "gray", "lexy": "steelblue",
           "pegtl": "darkorange", "simdjson": "purple"}
MARKERS = {"small.json": "o", "medium.json": "s", "large.json": "^", "longstr.json": "D"}

fig = plt.figure(figsize=(18, 7), layout="constrained")
gs  = gridspec.GridSpec(1, 2, figure=fig, wspace=0.35)
fig.suptitle(
    f"OneParse  v{version}  —  Runtime Parsing Benchmark\n"
    "Flat JSON object  ·  OneParse (HAPI chain) vs PEGTL vs simdjson vs lexy vs Spirit.X3",
    fontsize=12
)

# Panel left: current throughput (MB/s) — bar chart by parser × input file
ax1 = fig.add_subplot(gs[0, 0])

parser_names = ["lexy", "pegtl", "oneParse", "simdjson", "spirit.x3"]
x     = list(range(len(DATA_FILES)))
width = 0.15
# use last row per (parser, input) — handles multiple runs on same day
latest_rows = {}
for r in history:
    latest_rows[(r["parser"], r["input"])] = r
for i, pname in enumerate(parser_names):
    vals = []
    for fname in DATA_FILES:
        match = latest_rows.get((pname, fname))
        vals.append(float(match["throughput_mbs"]) if match else 0)
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

# Panel right: OneParse throughput — every individual bench.py run as a point
ax2 = fig.add_subplot(gs[0, 1])

# Detect run boundaries: each (strlen, small.json) row starts a new invocation
run_id = -1
annotated = []
for r in history:
    if r["parser"] == "strlen" and r["input"] == "small.json":
        run_id += 1
    annotated.append(dict(r, run_id=run_id))

op_rows    = [r for r in annotated if r["parser"] == "oneParse"]
runs_with_op = sorted({r["run_id"] for r in op_rows})
run_to_xi  = {rid: xi for xi, rid in enumerate(runs_with_op)}

# x-axis label: v{ver} #N  (N = index within that version)
ver_counts, x_labels = {}, []
for rid in runs_with_op:
    ver = next(r["version"] for r in annotated if r["run_id"] == rid)
    ver_counts[ver] = ver_counts.get(ver, 0) + 1
    x_labels.append(f"v{ver}\n#{ver_counts[ver]}")

# shade background per version
ver_spans = {}
for xi, rid in enumerate(runs_with_op):
    ver = next(r["version"] for r in annotated if r["run_id"] == rid)
    lo, hi = ver_spans.get(ver, (xi, xi))
    ver_spans[ver] = (min(lo, xi), max(hi, xi))
shade_colors = ["#e8f4e8", "#d0e8ff", "#fff0d0", "#fde8e8"]
for i, (ver, (lo, hi)) in enumerate(sorted(ver_spans.items())):
    ax2.axvspan(lo - 0.4, hi + 0.4, alpha=0.35,
                color=shade_colors[i % len(shade_colors)], zorder=0)

# one line per input file
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

# version labels after data is plotted (ylim is now correct)
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

plt.savefig(CHART_OUT, dpi=150)
print(f"Chart  →  {CHART_OUT}")
