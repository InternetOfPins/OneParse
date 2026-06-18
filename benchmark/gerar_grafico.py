import os, time, subprocess
import matplotlib.pyplot as plt

BENCH_DIR     = os.path.dirname(os.path.abspath(__file__))
parse_source  = os.path.join(BENCH_DIR, "bench_parse.cpp")
op_include    = os.path.join(BENCH_DIR, "..", "include")
hapi_include  = os.path.join(BENCH_DIR, "..", "..", "HAPI", "include")

sizes_parse = [5, 10, 20, 40, 80]

def parse_cmd(n, flag):
    return (
        f"g++ -std=c++17 -fsyntax-only -ftemplate-depth=2000 "
        f"-I{op_include} -I{hapi_include} "
        f"-DTEST_SIZE={n} -D{flag} {parse_source}"
    )

def measure_parse(sizes, flag):
    times = []
    for n in sizes:
        t0 = time.time()
        result = subprocess.run(
            parse_cmd(n, flag), shell=True,
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        elapsed = time.time() - t0
        if result.returncode != 0:
            print(f"  ERROR {flag} N={n}: {result.stderr.decode()[:120]}")
            times.append(None)
        else:
            times.append(elapsed)
            print(f"  {flag} N={n}: {elapsed:.3f}s")
    return times

parser_tests = [
    ("TEST_PARSE_BASELINE",      "Baseline",   "black", "--", "x"),
    ("TEST_PARSE_ONEPARSE_JSON", "oneParse",   "green", "-",  "s"),
    ("TEST_PARSE_SPIRIT_JSON",   "Spirit.X3",  "red",   "-",  "o"),
    ("TEST_PARSE_HANA_JSON",     "Hana (type)","blue",  "--", "^"),
]

parse_results = {}
for flag, label, color, ls, marker in parser_tests:
    print(f"\nMeasuring: {label}")
    parse_results[flag] = measure_parse(sizes_parse, flag)

fig, ax = plt.subplots(figsize=(9, 6))
fig.suptitle(
    "Parser grammar instantiation — N distinct field types\n"
    "OneParse (HAPI chain) vs Spirit.X3 vs Hana tuple\n"
    "(g++ -fsyntax-only, no runtime values)",
    fontsize=11
)
for flag, label, color, ls, marker in parser_tests:
    vals = parse_results[flag]
    xs = [s for s, v in zip(sizes_parse, vals) if v is not None]
    ys = [v for v in vals if v is not None]
    ax.plot(xs, ys, label=label, color=color, linestyle=ls, marker=marker)
ax.set_xlabel("N distinct field types")
ax.set_ylabel("Seconds")
ax.legend(fontsize=9)
ax.grid(True)

plt.tight_layout()
plt.savefig("bench_results.png", dpi=150)
print("\nSaved: bench_results.png")
