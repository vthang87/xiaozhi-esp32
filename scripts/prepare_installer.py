#!/usr/bin/env python3
"""Copy ES3C28P build artifacts into web/installer for ESP Web Tools.

Run after a board build:

    python scripts/release.py es3c28p
    python scripts/prepare_installer.py

Firmware .bin files stay gitignored. Hosted install on Cloudflare only works
after this script has populated web/installer/firmware on the machine that
deploys. The installer page can also flash a local merged-binary.bin.
"""

from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
DEST = ROOT / "web/installer/firmware"
MANIFEST = ROOT / "web/installer/manifest.json"
VERSION_JSON = ROOT / "web/installer/version.json"
READY_JSON = DEST / "ready.json"
CMAKE = ROOT / "CMakeLists.txt"
FLASHER_ARGS = BUILD / "flasher_args.json"
MERGED = BUILD / "merged-binary.bin"
THEME_BIN = BUILD / "storybook-garden.theme.bin"
THEME_OFFSET = 0xF80000


def project_version() -> str:
    with CMAKE.open() as f:
        for line in f:
            if line.startswith("set(PROJECT_VER"):
                return line.split("\"")[1]
    return "0.0.0"


def load_flash_parts() -> list[dict]:
    """Prefer individual flash_files from ESP-IDF (no padding)."""
    if not FLASHER_ARGS.exists():
        return []
    data = json.loads(FLASHER_ARGS.read_text())
    flash_files = data.get("flash_files") or {}
    parts: list[dict] = []
    for offset_str, rel in sorted(flash_files.items(), key=lambda item: int(item[0], 0)):
        src = BUILD / rel
        if not src.exists():
            print(f"skip missing flash file {src}", file=sys.stderr)
            continue
        name = src.name
        shutil.copy2(src, DEST / name)
        parts.append({"path": f"firmware/{name}", "offset": int(offset_str, 0)})
    return parts


def copy_merged() -> Optional[dict]:
    if not MERGED.exists():
        return None
    shutil.copy2(MERGED, DEST / "merged-binary.bin")
    return {"path": "firmware/merged-binary.bin", "offset": 0}


def copy_theme() -> Optional[dict]:
    if not THEME_BIN.exists():
        return None
    shutil.copy2(THEME_BIN, DEST / "storybook-garden.theme.bin")
    return {"path": "firmware/storybook-garden.theme.bin", "offset": THEME_OFFSET}


def write_json(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data, indent=2) + "\n")


def main() -> int:
    DEST.mkdir(parents=True, exist_ok=True)
    version = project_version()
    parts = load_flash_parts()
    merged = copy_merged()
    if not parts and merged:
        parts = [merged]
    if not parts:
        print(
            "No firmware found. Build first:\n"
            "  python scripts/release.py es3c28p\n"
            "or:\n"
            "  idf.py merge-bin",
            file=sys.stderr,
        )
        return 1

    write_json(
        MANIFEST,
        {
            "name": "ES3C28P XiaoZhi",
            "version": version,
            "new_install_prompt_erase": True,
            "builds": [{"chipFamily": "ESP32-S3", "parts": parts}],
        },
    )
    firmware_rel = parts[-1]["path"] if len(parts) == 1 else "firmware/merged-binary.bin"
    if merged:
        firmware_rel = "firmware/merged-binary.bin"
    write_json(
        VERSION_JSON,
        {
            "version": version,
            "board": "es3c28p",
            "chipFamily": "ESP32-S3",
            "firmware": firmware_rel,
        },
    )
    theme = copy_theme()
    write_json(
        READY_JSON,
        {
            "ready": True,
            "version": version,
            "board": "es3c28p",
            "parts": parts,
            "theme": theme,
        },
    )

    print(f"Installer firmware ready in {DEST}")
    for entry in sorted(DEST.iterdir()):
        if entry.is_file() and entry.name != ".gitkeep":
            print(f"  {entry.name:32} {entry.stat().st_size:10} bytes")
    print()
    print("Local preview:")
    print("  python3 -m http.server 8080 --directory web/installer")
    print("Cloudflare: add Worker es3c28p-install in the dashboard (see board README).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
