import os, time, subprocess
import matplotlib.pyplot as plt

# --- Paths (relative to benchmark/) -----------------------------------------

source      = "../main.cpp"                          # existing HAPI/Hana tests
include_dir = "../../include"                        # existing test include path

parse_source      = "bench_parse.cpp"
parse_include_dir = "../include"
hapi_include_dir  = "../../HAPI/include"

# --- Sizes -------------------------------------------------------------------

sizes_map   = [10, 50, 100, 200, 500, 1000]
sizes_find  = [10, 50, 100, 200, 400, 750]
sizes_tree  = [2, 4, 6, 8, 10, 14, 18, 20]
sizes_val   = [10, 50, 100, 200, 500, 1000]
sizes_parse = [2, 4, 6, 8, 10, 12, 14]

# --- Compile helpers ---------------------------------------------------------

def base_cmd(n, flag, tree=False):
    size_flag = f"-DTREE_B={n}" if tree else f"-DTEST_SIZE={n}"
    return (
        f"g++ -std=c++17 -fsyntax-only -ftemplate-depth=2000 "
        f"-I{include_dir} {size_flag} -D{flag} {source}"
    )

def parse_cmd(n, flag):
    return (
        f"g++ -std=c++17 -fsyntax-only -ftemplate-depth=2000 "
        f"-I{parse_include_dir} -I{hapi_include_dir} "
        f"-DTEST_SIZE={n} -D{flag} {parse_source}"
    )

def _run(cmd, flag, n):
    t0 = time.time()
    result = subprocess.run(cmd, shell=True,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    elapsed = time.time() - t0
    if result.returncode != 0:
        print(f"  ERROR {flag} N={n}: {result.stderr.decode()[:120]}")
        return None
    print(f"  {flag} N={n}: {elapsed:.3f}s")
    return elapsed

def measure(sizes, flag, tree=False):
    return [_run(base_cmd(n, flag, tree), flag, n) for n in sizes]

def measure_parse(sizes, flag):
    return [_run(parse_cmd(n, flag), flag, n) for n in sizes]

# --- Existing test lists (HAPI / Hana map / find / tree / val) ---------------

map_tests = [
    ("TEST_BASELINE",   "Baseline",              "black", "--", "x", sizes_map,  False),
    ("TEST_TUPLE_TYPE", "std::tuple (type)",     "red",   "-",  "o", sizes_map,  False),
    ("TEST_HANA_TYPE",  "Hana transform (type)", "blue",  "-",  "^", sizes_map,  False),
    ("TEST_HAPI_TYPE",  "hapi::Map (type)",      "green", "-",  "s", sizes_map,  False),
]

find_tests = [
    ("TEST_BASELINE",    "Baseline",              "black", "--",  "x", sizes_find, False),
    ("TEST_HAPI_FIRST",  "HAPI find — first",     "green", "-",   "s", sizes_find, False),
    ("TEST_HAPI_MIDDLE", "HAPI find — middle",    "green", "-.",  "D", sizes_find, False),
    ("TEST_HAPI_LAST",   "HAPI find — last",      "green", ":",   "^", sizes_find, False),
    ("TEST_HANA_FIRST",  "Hana find_if — first",  "blue",  "-",   "s", sizes_find, False),
    ("TEST_HANA_MIDDLE", "Hana find_if — middle", "blue",  "-.",  "D", sizes_find, False),
    ("TEST_HANA_LAST",   "Hana find_if — last",   "blue",  ":",   "^", sizes_find, False),
]

tree_tests = [
    ("TEST_BASELINE",        "Baseline",                 "black", "--", "x", sizes_tree, False),
    ("TEST_HAPI_TREE_MAP",   "HAPI Map — tree (native)", "green", "-",  "s", sizes_tree, True),
    ("TEST_HAPI_TREE_FIRST", "HAPI find — tree first",   "green", "-.", "D", sizes_tree, True),
    ("TEST_HAPI_TREE_LAST",  "HAPI find — tree last",    "green", ":",  "^", sizes_tree, True),
    ("TEST_HANA_TREE_MAP",   "Hana flatten+transform",   "blue",  "-",  "s", sizes_tree, True),
    ("TEST_HANA_TREE_FIND",  "Hana flatten+find_if",     "blue",  "-.", "D", sizes_tree, True),
]

val_tests = [
    ("TEST_BASELINE",      "Baseline",                  "black", "--", "x", sizes_val, False),
    ("TEST_HANA_VAL_MAP",  "Hana transform (value)",    "blue",  "-",  "^", sizes_val, False),
    ("TEST_HANA_VAL_FIND", "Hana find_if (value)",      "blue",  "-.", "D", sizes_val, False),
    ("TEST_HAPI_VAL_MAP",  "hapi::Map (value, forced)", "green", ":",  "s", sizes_val, False),
]

# --- Parser tests (bench_parse.cpp) ------------------------------------------

parser_tests = [
    ("TEST_PARSE_BASELINE",     "Baseline",    "black", "--", "x", sizes_parse),
    ("TEST_PARSE_ONEPARSE_JSON", "oneParse",   "green", "-",  "s", sizes_parse),
    ("TEST_PARSE_SPIRIT_JSON",   "Spirit.X3",  "red",   "-",  "o", sizes_parse),
    ("TEST_PARSE_HANA_JSON",     "Hana (type)","blue",  "--", "^", sizes_parse),
]

# --- Measure -----------------------------------------------------------------

# Existing tests
results = {}
for flag, label, color, ls, marker, sizes, tree in (map_tests + find_tests + tree_tests + val_tests):
    key = (flag, sizes[0], tree)
    if key not in results:
        print(f"\nMeasuring: {label}")
        results[key] = measure(sizes, flag, tree)

# Parser tests (separate source, separate results dict)
parse_results = {}
for flag, label, color, ls, marker, sizes in parser_tests:
    if flag not in parse_results:
        print(f"\nMeasuring: {label}")
        parse_results[flag] = measure_parse(sizes, flag)

# --- Plot helpers ------------------------------------------------------------

def get(flag, sizes, tree):
    return results.get((flag, sizes[0], tree), [None] * len(sizes))

def get_parse(flag, sizes):
    return parse_results.get(flag, [None] * len(sizes))

def plot_panel(ax, tests, title, xlabel='N', x_fn=None):
    for flag, label, color, ls, marker, sizes, tree in tests:
        vals = get(flag, sizes, tree)
        xs = [x_fn(s) if x_fn else s for s, v in zip(sizes, vals) if v is not None]
        ys = [v for v in vals if v is not None]
        ax.plot(xs, ys, label=label, color=color, linestyle=ls, marker=marker)
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_ylabel("Seconds")
    ax.grid(True)
    ax.legend(fontsize=8)

def plot_parse_panel(ax, tests, title):
    for flag, label, color, ls, marker, sizes in tests:
        vals = get_parse(flag, sizes)
        xs = [s for s, v in zip(sizes, vals) if v is not None]
        ys = [v for v in vals if v is not None]
        ax.plot(xs, ys, label=label, color=color, linestyle=ls, marker=marker)
    ax.set_title(title)
    ax.set_xlabel("N fields")
    ax.set_ylabel("Seconds")
    ax.grid(True)
    ax.legend(fontsize=8)

# --- Plot -------------------------------------------------------------------

fig, axes = plt.subplots(3, 2, figsize=(16, 18))
fig.suptitle("HAPI vs Hana vs Spirit.X3 — Compile-time Performance", fontsize=14)

plot_panel(axes[0][0], map_tests,  "Map: int → int* (type-level)")
plot_panel(axes[0][1], find_tests, "Find: flat chain — first / middle / last")
plot_panel(axes[1][0], tree_tests, "Tree topology (B×B) — HAPI native vs flatten",
           xlabel="Total Elements (N = B²)", x_fn=lambda b: b * b)
plot_panel(axes[1][1], val_tests,
           "Hana's terrain: value-level heterogeneous\n(HAPI forced for reference)")
plot_parse_panel(axes[2][0], parser_tests,
                 "Parser grammar instantiation — N distinct field types\n"
                 "OneParse (HAPI chain) vs Spirit.X3 vs Hana tuple")
axes[2][1].set_visible(False)   # spare panel

plt.tight_layout()
plt.savefig("bench_results.png", dpi=150)
print("\nSaved: bench_results.png")
