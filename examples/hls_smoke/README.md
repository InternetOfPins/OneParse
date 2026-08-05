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

Without `BAMBU_APPIMAGE` set, both fail immediately with a clear message
naming the missing prerequisite — nothing is silently downloaded or
substituted. RTL and Bambu's own logs land in `.hls_out_tiny/` and
`.hls_out_buffer/` respectively (gitignored).

## Footprint comparison (verified, not estimated)

| | `jsonCharTop` (tiny) | `jsonBufTop` (steroids) |
|---|---|---|
| Flip-flops | **1141** | **3282** (+129 in a separate `memchr` helper Bambu factored out) |
| Registers (SE + STD) | 94 | 182 |
| Multiplexers | 88 | 163 |
| LUT-based FUs | 61 | 91 |
| Folded constants | 83 | 101 |

Same grammar, same `Alt<5 alternatives>` dispatch, same `JsonObj` state
machine — roughly **3x** the hardware for the buffer-driven bulk-scan
entry point over the single-char one. That's not overhead or bloat: it's
the real, additional work the `run_n()` fast path does (structural byte
scanning, buffer addressing, the extra memory port) that `run()` alone
never touches. Composing the identical grammar into two different entry
shapes and getting a footprint that tracks the *actual* additional
work — not the composition depth, not the grammar's own complexity, which
is identical between both — is the point of this example.
