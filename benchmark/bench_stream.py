#!/usr/bin/env python3
"""
bench_stream.py — rParser (streaming) vs oneParse (pointer-based), char-by-char dispatch
Run:  python3 bench_stream.py
"""

import os, sys, subprocess
import matplotlib.pyplot as plt

BENCH_DIR = os.path.dirname(os.path.abspath(__file__))
SRC       = os.path.join(BENCH_DIR, "bench_stream.cpp")
BUILD_DIR = os.path.join(BENCH_DIR, "build")
DATA_DIR  = os.path.join(BENCH_DIR, "data")
CHART_OUT = os.path.join(BENCH_DIR, "bench_stream.png")
HAPI_INC   = os.path.join(BENCH_DIR, "..", "..", "HAPI", "include")
OP_INC     = os.path.join(BENCH_DIR, "..", "include")
OUT_INC    = os.path.join(BENCH_DIR, "..", "..", "OneOutput", "include")

os.makedirs(BUILD_DIR, exist_ok=True)

ITERS      = 20000
RUNS       = 5
DATA_FILES = ["small.json", "medium.json", "large.json"]

PARSERS = [
    ("rParser",   ["-std=c++17", "-DPARSER_RPARSER",   f"-I{HAPI_INC}", f"-I{OUT_INC}"]),
    ("op-stream", ["-std=c++17", "-DPARSER_OP_STREAM", f"-I{OP_INC}", f"-I{HAPI_INC}", f"-I{OUT_INC}"]),
]

# ── Compile ───────────────────────────────────────────────────────────────────

print("=== Compiling ===")
binaries = {}
for name, flags in PARSERS:
    exe = os.path.join(BUILD_DIR, "bench_" + name.replace("-", "_"))
    cmd = ["g++", "-O2"] + flags + ["-o", exe, SRC]
    print(f"  {name:12} ... ", end="", flush=True)
    r = subprocess.run(cmd, capture_output=True)
    if r.returncode != 0:
        print(f"FAILED\n{r.stderr.decode()}")
        sys.exit(1)
    print("ok")
    binaries[name] = exe

# ── Run ───────────────────────────────────────────────────────────────────────

print(f"\n=== Running  iters={ITERS}  runs={RUNS} ===")
results = {}  # name → {fname → throughput_mbs}
for name, exe in binaries.items():
    results[name] = {}
    for fname in DATA_FILES:
        fpath = os.path.join(DATA_DIR, fname)
        if not os.path.exists(fpath):
            print(f"  SKIP {fname}")
            continue
        cmd = [exe, fpath, str(ITERS), str(RUNS)]
        print(f"  {name:12}  {fname:14} ... ", end="", flush=True)
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"FAILED: {res.stderr[:80]}")
            continue
        parts = res.stdout.strip().split(",")
        _, _, nbytes, _, median_ms = parts
        nbytes, median_ms = int(nbytes), float(median_ms)
        throughput = (nbytes * ITERS) / (median_ms / 1000.0) / 1e6
        results[name][fname] = throughput
        print(f"{median_ms:8.1f} ms   {throughput:7.2f} MB/s")

# ── Plot ──────────────────────────────────────────────────────────────────────

files = [f for f in DATA_FILES if any(f in results[n] for n in results)]
x     = list(range(len(files)))
width = 0.35
colors = {"rParser": "steelblue", "op-stream": "green"}

fig, ax = plt.subplots(figsize=(8, 5))
for i, (name, _) in enumerate(PARSERS):
    vals   = [results[name].get(f, 0) for f in files]
    offset = (i - 0.5) * width
    ax.bar([xi + offset for xi in x], vals, width,
           label=name, color=colors.get(name, "gray"), alpha=0.85)

ax.set_title("Streaming (rParser) vs pointer-based (oneParse) — char-by-char Digit dispatch\n"
             "Same HAPI chain, different Res type · higher is faster")
ax.set_xlabel("Input")
ax.set_ylabel("MB / s")
ax.set_xticks(x)
ax.set_xticklabels([f.replace(".json", "") for f in files])
ax.legend()
ax.grid(axis="y", alpha=0.5)
plt.tight_layout()
plt.savefig(CHART_OUT, dpi=150)
print(f"\nChart → {CHART_OUT}")
