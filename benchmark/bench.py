#!/usr/bin/env python3
"""
bench.py — OneParse runtime parsing benchmark
Run:     python3 bench.py
Results: results/history.csv  (append-only, one row per run)
Charts:  bench_comparison.png (shareable cross-parser comparison)
         bench_history.png    (internal OneParse version-over-version tracking)
"""

import os, sys, csv, json, datetime, subprocess
import matplotlib.pyplot as plt

# ── Paths ──────────────────────────────────────────────────────────────────

BENCH_DIR    = os.path.dirname(os.path.abspath(__file__))
SRC          = os.path.join(BENCH_DIR, "bench_runtime.cpp")
BUILD_DIR    = os.path.join(BENCH_DIR, "build")
RESULTS_DIR  = os.path.join(BENCH_DIR, "results")
HISTORY_CSV  = os.path.join(RESULTS_DIR, "history.csv")
DATA_DIR     = os.path.join(BENCH_DIR, "data")
CHART_COMPARISON = os.path.join(BENCH_DIR, "bench_comparison.png")
CHART_HISTORY    = os.path.join(BENCH_DIR, "bench_history.png")
LIBRARY_JSON = os.path.join(BENCH_DIR, "..", "library.json")
HAPI_INC     = os.path.join(BENCH_DIR, "..", "..", "HAPI", "include")
OP_INC       = os.path.join(BENCH_DIR, "..", "include")
OUT_INC      = os.path.join(BENCH_DIR, "..", "..", "OneOutput", "include")
LEXY_INC     = os.path.join(BENCH_DIR, "lexy_src",    "include")
PEGTL_INC    = os.path.join(BENCH_DIR, "pegtl_src",   "include")
SIMDJSON_SRC = os.path.join(BENCH_DIR, "simdjson_src")
RAPIDJSON_INC = os.path.join(BENCH_DIR, "rapidjson_src", "include")

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
    ("oneParse-stream", ["-std=c++17", "-DPARSER_ONEPARSE_STREAM",
                   f"-I{OP_INC}", f"-I{HAPI_INC}", f"-I{OUT_INC}"]),
    # same PARSER_ONEPARSE_INDEX build with exception unwind tables stripped —
    # tracks whether -fno-exceptions moves the number now that OneParse's
    # combinators return Res<T>/ok-flags rather than throwing; excluded from
    # parser_names below (reference row, not a reported comparison bar)
    ("oneParse-noexc", ["-std=c++17", "-DPARSER_ONEPARSE_INDEX", "-fno-exceptions",
                   f"-I{OP_INC}", f"-I{HAPI_INC}", f"-I{OUT_INC}"]),
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
    ("rapidjson", ["-std=c++17", "-DPARSER_RAPIDJSON",
                   f"-I{RAPIDJSON_INC}"]),
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

def newest_mtime(path):
    """mtime of path, or the newest mtime of any file under it if a directory."""
    if os.path.isfile(path):
        return os.path.getmtime(path)
    newest = 0.0
    for root, _, files in os.walk(path):
        for f in files:
            try:
                newest = max(newest, os.path.getmtime(os.path.join(root, f)))
            except OSError:
                pass
    return newest

def parser_deps(extra_flags):
    """SRC + bench.py itself (covers BASE_FLAGS/PARSERS edits) + every -I dir
    and any literal source file (e.g. simdjson.cpp) passed in extra_flags."""
    deps = [SRC, os.path.abspath(__file__)]
    for tok in extra_flags:
        if tok.startswith("-I"):
            deps.append(tok[2:])
        elif os.path.isfile(tok):
            deps.append(tok)
    return deps

print("=== Compiling ===")
binaries = {}
for name, extra_flags in PARSERS:
    exe = os.path.join(BUILD_DIR, "bench_" + name.replace(".", "_"))
    print(f"  {name:12} ... ", end="", flush=True)

    dep_mtime = max(newest_mtime(d) for d in parser_deps(extra_flags))
    if os.path.exists(exe) and os.path.getmtime(exe) >= dep_mtime:
        print("cached (up to date)")
        binaries[name] = exe
        continue

    cmd = BASE_FLAGS + extra_flags + ["-o", exe, SRC]  # std flag is in extra_flags
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
           "pegtl": "darkorange", "simdjson": "purple", "rapidjson": "brown"}
MARKERS = {"small.json": "o", "medium.json": "s", "large.json": "^", "longstr.json": "D"}

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
# use last row per (parser, input) — handles multiple runs on same day
latest_rows = {}
for r in history:
    latest_rows[(r["parser"], r["input"])] = r

# grammar-combinator frameworks (grammar compiled specifically for THIS
# benchmark's flat-object shape) vs simdjson (dedicated, general-purpose,
# hand-tuned JSON parser) — hatch the combinator bars so the two categories
# are visually distinct, not just disclosed in a caption nobody reads.
COMBINATOR_PARSERS = {"lexy", "pegtl", "oneParse", "spirit.x3"}

for i, pname in enumerate(parser_names):
    vals = []
    for fname in DATA_FILES:
        match = latest_rows.get((pname, fname))
        vals.append(float(match["throughput_mbs"]) if match else 0)
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

plt.savefig(CHART_HISTORY, dpi=150)
print(f"Chart  →  {CHART_HISTORY}")
