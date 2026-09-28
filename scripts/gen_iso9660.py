#!/usr/bin/env python3
"""Generate iso9660_disk.h: a read-only ISO9660 (CD-ROM) disk image containing
AUTORUN.INF (open=INSTALL.HTM) and INSTALL.HTM, for the second (CD-ROM class)
USB MSC LUN. Requires `pip install pycdlib`.

Regenerate with: python scripts/gen_iso9660.py
"""
import io
import re
import sys
from pathlib import Path

try:
    import pycdlib
except ImportError:
    sys.exit("pycdlib is required: pip install pycdlib")

ROOT = Path(__file__).resolve().parent.parent
APP_DIR = ROOT / "USB_DEVICE" / "App"
INSTALL_HTML_H = APP_DIR / "install_html.h"
OUT_H = APP_DIR / "iso9660_disk.h"

SECTOR_SIZE = 2048

AUTORUN_INF = (
    "[autorun]\r\n"
    "label=USB-PD Tester\r\n"
    "action=Open the USB-PD Tester setup guide\r\n"
    "shellexecute=INSTALL.HTM\r\n"
).encode("ascii")


def extract_install_html_bytes():
    """Pull the exact byte contents back out of the existing generated
    install_html.h so LUN 0 (FAT12) and LUN 1 (ISO9660) serve byte-identical
    files without needing the original install.html source on disk."""
    text = INSTALL_HTML_H.read_text()

    m = re.search(r"INSTALL_HTML_LEN\s+(\d+)", text)
    if not m:
        sys.exit("could not find INSTALL_HTML_LEN in install_html.h")
    expected_len = int(m.group(1))

    m = re.search(r"install_html\[\d+\]\s*=\s*\{(.*?)\};", text, re.DOTALL)
    if not m:
        sys.exit("could not find install_html[] array in install_html.h")

    hex_bytes = re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1))
    data = bytes(int(h, 16) for h in hex_bytes)

    if len(data) != expected_len:
        sys.exit(f"length mismatch: array has {len(data)} bytes, expected {expected_len}")

    return data


def build_iso(install_html_bytes):
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=1, vol_ident="USB_PD_TESTER", sys_ident="")

    iso.add_fp(
        io.BytesIO(AUTORUN_INF), len(AUTORUN_INF), "/AUTORUN.INF;1"
    )
    iso.add_fp(
        io.BytesIO(install_html_bytes), len(install_html_bytes), "/INSTALL.HTM;1"
    )

    out = io.BytesIO()
    iso.write_fp(out)
    iso.close()

    data = out.getvalue()
    if len(data) % SECTOR_SIZE != 0:
        pad = SECTOR_SIZE - (len(data) % SECTOR_SIZE)
        data += b"\x00" * pad

    return data


def emit_header(image_bytes):
    total_sectors = len(image_bytes) // SECTOR_SIZE

    lines = []
    lines.append("/* Auto-generated read-only ISO9660 (CD-ROM) disk image data. */")
    lines.append("/* Regenerate with: python scripts/gen_iso9660.py */")
    lines.append("#ifndef __ISO9660_DISK_H__")
    lines.append("#define __ISO9660_DISK_H__")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"#define ISO9660_BYTES_PER_SECTOR  {SECTOR_SIZE}")
    lines.append(f"#define ISO9660_TOTAL_SECTORS     {total_sectors}")
    lines.append(f"#define ISO9660_IMAGE_LEN         {len(image_bytes)}U")
    lines.append("")
    lines.append(f"static const uint8_t iso9660_image[{len(image_bytes)}] = {{")

    for i in range(0, len(image_bytes), 16):
        chunk = image_bytes[i:i + 16]
        lines.append("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")

    lines.append("};")
    lines.append("")
    lines.append("#endif /* __ISO9660_DISK_H__ */")
    lines.append("")

    OUT_H.write_text("\n".join(lines))


def main():
    install_html_bytes = extract_install_html_bytes()
    image = build_iso(install_html_bytes)
    emit_header(image)
    print(f"Wrote {OUT_H} : {len(image)} bytes, {len(image)//SECTOR_SIZE} sectors")


if __name__ == "__main__":
    main()
