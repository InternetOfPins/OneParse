"""
PlatformIO custom targets for HLS synthesis via PandA-Bambu.

Bambu isn't a PlatformIO `platform` (it synthesizes RTL, not a linked
executable) -- this hooks it in as custom targets instead, so the two
demonstrated entry points are runnable as:

    pio run -e hls -t synthesize-tiny
    pio run -e hls -t synthesize-buffer
    pio run -e hls -t synthesize-tiny-gcc8
    pio run -e hls -t synthesize-buffer-gcc8
    pio run -e hls -t synthesize-tiny-altdevice

Requires the BAMBU_APPIMAGE environment variable pointing at a bambu
AppImage (see README.md -- no bundled/auto-installed toolchain here,
flagged explicitly rather than silently downloading one).

Synthesizes against an explicit device (--device-name) and clock period
(--clock-period), same device/period as HAPI's hls_fir/hls_can_disabler/
hls_smoke for direct comparability -- Bambu is target-aware, not
target-independent: functional-unit selection is characterized against a
specific device technology library, so an unconfirmed/default device
produces numbers that aren't citable against any real, ownable board.

The -gcc8/-altdevice targets are second, independent Bambu configs (see
README.md's Cross-tool/cross-config validation section) -- not a
different HLS tool, but a cross-check that the footprint comparison
isn't an artifact of one frontend/device pairing.
"""
import os
Import("env")

BAMBU = os.environ.get("BAMBU_APPIMAGE")
DEVICE = "xc7a100t-1csg324-VVD"
ALT_DEVICE = "LFE5U85F8BG756C"  # Lattice ECP5-85, different vendor/architecture
CLOCK_PERIOD = "10"

HERE = env.subst("$PROJECT_DIR")
HAPI_INC = os.path.join(HERE, "..", "..", "..", "HAPI", "include")
OP_INC   = os.path.join(HERE, "..", "..", "include")
OUT_INC  = os.path.join(HERE, "..", "..", "..", "OneOutput", "include")

# Each target points at its own isolated hls/*.cpp, NOT src/main.cpp.
# Two same-typed global parser objects in one translation unit (as
# src/main.cpp has, for the combined native demo) pull each other's
# static-initialization machinery into Bambu's output and inflate the
# footprint -- confirmed empirically (1141 FF isolated vs. 3282 FF
# combined for the same jsonCharTop). Isolated files give the real,
# comparable numbers reported in README.md.


def _bambu_cmd(top_fname, src_file, outdir, frontend="I386_CLANG16",
                device=DEVICE):
    if not BAMBU:
        return (
            'echo "BAMBU_APPIMAGE is not set -- point it at a bambu AppImage '
            '(e.g. https://release.bambuhls.eu/bambu-2024.10.AppImage) and '
            're-run. Not auto-installing anything." && exit 1'
        )
    os.makedirs(outdir, exist_ok=True)
    return (
        f'cd "{outdir}" && "{BAMBU}" '
        f'-I"{HAPI_INC}" -I"{OP_INC}" -I"{OUT_INC}" '
        f'--std=gnu++17 --compiler={frontend} '
        f'--device-name={device} --clock-period={CLOCK_PERIOD} '
        f'--top-fname={top_fname} -v2 "{src_file}"'
    )


env.AddCustomTarget(
    name="synthesize-tiny",
    dependencies=None,
    actions=[_bambu_cmd(
        "jsonCharTop",
        os.path.join(HERE, "hls", "char_top.cpp"),
        os.path.join(HERE, ".hls_out_tiny"),
    )],
    title="HLS: synthesize jsonCharTop (tiny)",
    description="Single-char run() entry point, no memory interface -- "
                 "the small end of the footprint comparison in README.md",
    always_build=True,
)

env.AddCustomTarget(
    name="synthesize-buffer",
    dependencies=None,
    actions=[_bambu_cmd(
        "jsonBufTop",
        os.path.join(HERE, "hls", "buf_top.cpp"),
        os.path.join(HERE, ".hls_out_buffer"),
    )],
    title="HLS: synthesize jsonBufTop (on steroids)",
    description="Buffer + run_n() bulk-scan entry point -- the large end "
                 "of the footprint comparison in README.md",
    always_build=True,
)

env.AddCustomTarget(
    name="synthesize-tiny-gcc8",
    dependencies=None,
    actions=[_bambu_cmd(
        "jsonCharTop",
        os.path.join(HERE, "hls", "char_top.cpp"),
        os.path.join(HERE, ".hls_out_tiny_gcc8"),
        frontend="I386_GCC8",
    )],
    title="HLS: synthesize jsonCharTop (GCC8 frontend)",
    description="Same design as synthesize-tiny, through Bambu's "
                 "I386_GCC8 frontend -- cross-check for a "
                 "frontend-specific artifact. See README.md.",
    always_build=True,
)

env.AddCustomTarget(
    name="synthesize-buffer-gcc8",
    dependencies=None,
    actions=[_bambu_cmd(
        "jsonBufTop",
        os.path.join(HERE, "hls", "buf_top.cpp"),
        os.path.join(HERE, ".hls_out_buffer_gcc8"),
        frontend="I386_GCC8",
    )],
    title="HLS: synthesize jsonBufTop (GCC8 frontend)",
    description="Same design as synthesize-buffer, through Bambu's "
                 "I386_GCC8 frontend -- cross-check for a "
                 "frontend-specific artifact. See README.md.",
    always_build=True,
)

env.AddCustomTarget(
    name="synthesize-tiny-altdevice",
    dependencies=None,
    actions=[_bambu_cmd(
        "jsonCharTop",
        os.path.join(HERE, "hls", "char_top.cpp"),
        os.path.join(HERE, ".hls_out_tiny_altdevice"),
        device=ALT_DEVICE,
    )],
    title="HLS: synthesize jsonCharTop (Lattice ECP5)",
    description="Same design as synthesize-tiny, against a Lattice "
                 "ECP5-85 (LFE5U85F8BG756C) instead of the Xilinx "
                 "Artix-7 -- cross-check for a device-specific artifact. "
                 "See README.md.",
    always_build=True,
)
