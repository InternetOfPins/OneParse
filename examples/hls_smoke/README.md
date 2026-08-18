# hls_smoke

Proof that OneParse's real, published JSON grammar — the full
`JsonVal = Alt<JsonStr, JsonNum, JsonNull, JsonTrue, JsonFalse>` from
[`examples/json`](../json) — synthesizes to actual hardware through
[PandA-Bambu](https://github.com/ferrandi/PandA-bambu), at **two** entry
points into the exact same composed grammar: a single genuine `char` with
no memory interface at all, and a real `const char*` buffer through the
`run_n()` bulk-scan path. Same composition machinery, same grammar, same
`JsonObj` state machine — genuinely different hardware footprint depending
on which shape you synthesize.

## The two targets

- **`jsonCharTop(char c)`** (`hls/char_top.cpp`) — "tiny": one char in,
  `run()` alone. No buffer, no memory port, no length parameter.
- **`jsonBufTop(const char* buf, size_t n)`** (`hls/buf_top.cpp`) — "on
  steroids": a real buffer through `run_n()`, the same memchr-optimized
  bulk-scan fast path `oneParse.h` uses internally (fast-scans string
  bodies, falls back to char-by-char only at phase transitions) — not a
  synthetic wrapper loop around `run()`.

Both use the identical `JsonObj`/`JsonStr`/`JsonNum`/`Alt<5 alternatives>`
grammar. `ConsoleOut` is swapped for a synthesizable accumulating `Out`
(same pattern as every other `hls_smoke` example — I/O has no synthesis
target). `JsonObj::run()`'s phase-transition self-recursion is rewritten
as a loop — a real HLS-portability fix, not a style choice; see
[`OneParse/.RnD/hls/FINDINGS.md`](../../.RnD/hls/FINDINGS.md) for the full
root-cause writeup (a genuine bug in Bambu's own `topfname` call-graph
pass, worked around from OneParse's side with zero behavior change).

`hls/char_top.cpp` and `hls/buf_top.cpp` are isolated, single-purpose
translation units (not part of `src/`, so PlatformIO's native build
doesn't compile them) — each is exactly one grammar, one entry point, one
global parser object, so the synthesized footprint reflects only that
entry point's real cost.

## Run it natively (regression check, both entry points)

```sh
pio run -e native
.pio/build/native/program   # prints 1, then 7
```

`1` = `jsonCharTop('{')` matched. `7` = `jsonBufTop` consumed all 7 bytes
of `{"x":1}`.

## Target device

Bambu is target-aware, not target-independent: functional-unit selection
and every area/frequency/slack number are characterized against a
specific device technology library -- a run with no `--device-name`
produces numbers against Bambu's undocumented internal default, which
aren't citable against any real, ownable board. **This example originally
ran against that undocumented default**; it now pins the same device/
period as HAPI's `hls_fir`/`hls_can_disabler`/`hls_smoke`:

```
--device-name=xc7a100t-1csg324-VVD --clock-period=10
```

`xc7a100t-1csg324-VVD` is the Xilinx Artix-7 on the Digilent Arty A7/
Nexys A7 (10ns targets 100MHz). The flip-flop counts in the footprint
comparison below (1141 / 3282) are **unchanged** from the original
default-device run — only the frequency/slack/area figures are new, now
citable against a confirmed device instead of an undocumented one.

## Run it through Bambu HLS

Get the prebuilt AppImage (no Docker, no LLVM/GCC build needed):

```sh
curl -L -o bambu.AppImage https://release.bambuhls.eu/bambu-2024.10.AppImage
chmod +x bambu.AppImage
export BAMBU_APPIMAGE="$(pwd)/bambu.AppImage"
```

Bambu's frontend compiles internally as a 32-bit (`i386`) target, so it
needs 32-bit glibc headers most desktop installs don't have by default:

```sh
sudo apt install gcc-multilib g++-multilib   # or just libc6-dev-i386
```

> Before running that on a machine with a large pending-upgrade backlog,
> dry-run it first with `apt-get install -s gcc-multilib g++-multilib`
> (no root needed) — on a system that's behind on updates it can pull in
> far more than the two named packages, including a kernel update. Not
> expected on a clean/up-to-date system.

Then synthesize either target as a real PlatformIO custom target
(wired via `extra_hls.py`, `env.AddCustomTarget`):

```sh
pio run -e hls -t synthesize-tiny     # jsonCharTop
pio run -e hls -t synthesize-buffer   # jsonBufTop
```

Plus two independent-config cross-checks (see
[Cross-tool/cross-config validation](#cross-toolcross-config-validation)):

```sh
pio run -e hls -t synthesize-tiny-gcc8        # jsonCharTop, GCC8 frontend (fails)
pio run -e hls -t synthesize-buffer-gcc8      # jsonBufTop, GCC8 frontend (fails)
pio run -e hls -t synthesize-tiny-altdevice   # jsonCharTop, Lattice ECP5
```

Without `BAMBU_APPIMAGE` set, all targets fail immediately with a clear
message naming the missing prerequisite — nothing is silently downloaded or
substituted. RTL and Bambu's own logs land in `.hls_out_tiny/` and
`.hls_out_buffer/` (and each's `_gcc8`/`_altdevice`-suffixed counterpart)
respectively (gitignored).

## Footprint comparison (verified, not estimated)

| | `jsonCharTop` (tiny), Bambu | `jsonBufTop` (steroids), Bambu | `jsonCharTop`, AMD Vitis HLS 2026.1 |
|---|---|---|---|
| Flip-flops | **1141** | **3282** (+129 in a separate `memchr` helper Bambu factored out) | 2182 |
| Registers (SE + STD) | 94 | 182 | — |
| Multiplexers | 88 | 163 | — |
| LUTs | — | — | 2835 |
| LUT-based FUs | 61 | 91 | — |
| Folded constants | 83 | 101 | — |
| Modules instantiated | 343 | 1638 (+26 in `memchr`) | — |
| Estimated max frequency | 103.82 MHz | 100.20 MHz (`memchr`: 156.38 MHz) | not reported directly (see slack) |
| Minimum slack | 0.368 ns | 0.020 ns (`memchr`: 3.605 ns) | 0.41 ns |
| **Total estimated area** | **9585** | **15468** (+238 in `memchr`) | N/A — Vitis reports LUT/FF/DSP, not a unified area score |
| Estimated number of DSPs | **0** | **0** | **0** |
| BRAM | 0 (distributed RAM only) | 0 | **1** |

Re-derived directly from `.hls_out_tiny_vitis/proj/solution1/syn/report/csynth.rpt`
(2026-08-18) after this table's earlier Vitis numbers (5 FF / 88 LUT /
155.6 MHz) turned out to be wrong — a transcription error, not a second
real run: those figures were implausibly small for a design that
actually compiles the full `Alt<5 alternatives>`/`JsonStr`/`JsonNum`
dispatch tree (2,150 total instructions per Vitis's own Design Size
Report), and don't match Bambu's own 1141-FF result for the identical
source closely enough to be believable at face value. Re-run fresh,
independently, to confirm: same 2182 FF / 2835 LUT / 1 BRAM / 0.41ns
slack, reproducible. Roughly ~2x Bambu's flip-flop count for the same
design — a real cross-toolchain difference in resource-mapping strategy,
same spirit as the DSP-vs-shift/add divergences already documented
elsewhere in this project family, not a discrepancy to be resolved. The
1 BRAM instance is new information this correction surfaced — not
investigated further here (which structure it maps to is an open
question), but real, not zero the way Bambu's distributed-RAM-only
result is.

`jsonBufTop` does not synthesize under Vitis HLS: its frontend rejects
`memchr()` as an undefined function, where Bambu recognizes and lowers
it. `jsonCharTop`, which doesn't call `memchr()`, is unaffected and
synthesizes cleanly on both tools.

Same grammar, same `Alt<5 alternatives>` dispatch, same `JsonObj` state
machine — roughly **3x** the hardware for the buffer-driven bulk-scan
entry point over the single-char one. That's not overhead or bloat: it's
the real, additional work the `run_n()` fast path does (structural byte
scanning, buffer addressing, the extra memory port) that `run()` alone
never touches. Composing the identical grammar into two different entry
shapes and getting a footprint that tracks the *actual* additional
work — not the composition depth, not the grammar's own complexity, which
is identical between both — is the point of this example. Zero DSPs on
both: the JSON grammar's dispatch logic is comparisons/branching, not
arithmetic, so there was never a multiply for Bambu to map to a DSP block.

### Cross-tool/cross-config validation

Bambu is currently the only HLS backend actually run against this design
— a Bambu-specific quirk could in principle masquerade as an OneParse/
HAPI property (or vice versa). Two independent Bambu configs were run as
a first cross-check:

- **GCC8 frontend (`--compiler=I386_GCC8`): rejected on both targets, a
  known, already-diagnosed GCC8 limitation.** Fails with
  `error: overflow in constant expression [-fpermissive]` inside
  `oneParse::detail::make_alt_table`'s `constexpr` dispatch-table
  construction — a distinct failure mode from HAPI's own GCC8 rejection
  (`bound_template_template_parm`, a parse-time template-template-
  parameter issue; see `HAPI/.RnD/bambuHLS/HANDOFF.md` finding #4). This
  one is a `constexpr`-evaluation issue specific to GCC8's signed-`char`
  overflow handling under `-fwrapv`, already root-caused in
  [`OneParse/.RnD/hls/FINDINGS.md`](../../.RnD/hls/FINDINGS.md)'s
  "Blocker 1" — confirmed here to still reproduce against the isolated,
  device-pinned `synthesize-tiny-gcc8`/`synthesize-buffer-gcc8` targets.
  `I386_CLANG16` remains the only viable frontend for this codebase, same
  conclusion as HAPI's own examples, reached via a genuinely different
  bug.
- **Lattice ECP5 device (`LFE5U85F8BG756C`), `jsonCharTop` only: DSP
  inference is device-independent, flip-flop/area/frequency are not.**
  DSP count (0) matched exactly against a second, non-Xilinx vendor's
  device. Flip-flop count (1141 → 1308), modules instantiated (343 →
  369), and total area (9585 → 13270) did **not** match — expected, same
  device-technology-library-binding story as HAPI's own alt-device
  cross-checks; these numbers were never claimed portable across devices.

**AMD Vitis HLS 2026.1** (`pio run -e hls-vitis -t synthesize-tiny-vitis`):
`jsonCharTop` synthesizes cleanly — see the table above. `jsonBufTop`
does not: Vitis HLS's frontend has no synthesizable `memchr()`, which
`run_n()`'s bulk-scan path relies on. Intel HLS Compiler and LegUp were
both ruled out for this whole codebase: the
classic `i++` command-line compiler now appears to require Quartus Prime
Pro edition (paid), and LegUp's free academic version is a frozen
pre-C++17 snapshot with a closed commercial successor; see
`HAPI/.RnD/legupHLS/HANDOFF.md`.
