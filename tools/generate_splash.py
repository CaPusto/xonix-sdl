#!/usr/bin/env python3
"""Embed the startup picture as PNG bytes in src/splash_image.h.

stb_image decodes the bytes at runtime, so the game does not depend on the
asset being installed next to the executable. Run this script only after
replacing assets/1980sRetroGamingRoomNostalgia960x600.png.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "1980sRetroGamingRoomNostalgia960x600.png"
OUTPUT = ROOT / "src" / "splash_image.h"


def main() -> None:
    data = SOURCE.read_bytes()
    lines = [
        "/* Generated from assets/1980sRetroGamingRoomNostalgia960x600.png.",
        " * Regenerate with tools/generate_splash.py. Do not edit by hand. */",
        "#ifndef XONIX_SPLASH_IMAGE_H",
        "#define XONIX_SPLASH_IMAGE_H",
        "",
        f"#define SPLASH_PNG_LEN {len(data)}",
        "",
        "static const unsigned char splash_png[SPLASH_PNG_LEN] = {",
    ]
    for start in range(0, len(data), 16):
        lines.append("    " + ", ".join(str(b) for b in data[start:start + 16]) + ",")
    lines.extend([
        "};",
        "",
        "#endif /* XONIX_SPLASH_IMAGE_H */",
        "",
    ])
    OUTPUT.write_text("\n".join(lines), encoding="ascii")
    print(f"wrote {OUTPUT.relative_to(ROOT)} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
