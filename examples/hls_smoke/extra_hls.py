"""
PlatformIO custom targets for HLS synthesis via PandA-Bambu.

Bambu isn't a PlatformIO `platform` (it synthesizes RTL, not a linked
executable) -- this hooks it in as custom targets instead, so the two
demonstrated entry points are runnable as:

    pio run -e hls -t synthesize-tiny
    pio run -e hls -t synthesize-buffer

Requires the BAMBU_APPIMAGE environment variable pointing at a bambu
AppImage (see README.md -- no bundled/auto-installed toolchain here,
flagged explicitly rather than silently downloading one).
"""
import os
Import("env")

BAMBU = os.environ.get("BAMBU_APPIMAGE")

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


def _bambu_cmd(top_fname, src_file, outdir):
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
        f'--std=gnu++17 --compiler=I386_CLANG16 '
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
