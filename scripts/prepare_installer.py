#!/usr/bin/env python3
"""Build the XiaoZhi web-installer catalog and copy firmware for ESP Web Tools.

    python scripts/prepare_installer.py              # write boards.json
    python scripts/prepare_installer.py es3c28p      # also copy the current IDF build
    python scripts/prepare_installer.py --from-build # detect variant from compile_commands

Firmware .bin files stay gitignored. Hosted install on Cloudflare only works
after this script has populated web/installer/firmware/<variant> on the machine
that deploys. The installer page can also flash a local merged-binary.bin.
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parent.parent
BOARDS_DIR = ROOT / "main/boards"
BUILD = ROOT / "build"
INSTALLER = ROOT / "web/installer"
FIRMWARE = INSTALLER / "firmware"
BOARDS_JSON = INSTALLER / "boards.json"
VERSION_JSON = INSTALLER / "version.json"
MANIFEST = INSTALLER / "manifest.json"
CMAKE = ROOT / "CMakeLists.txt"
FLASHER_ARGS = BUILD / "flasher_args.json"
MERGED = BUILD / "merged-binary.bin"
COMPILE_COMMANDS = BUILD / "compile_commands.json"
THEME_BIN = BUILD / "storybook-garden.theme.bin"
THEME_OFFSET = 0xF80000
# Temporarily ship only ES3C28P in the web installer.
INSTALLER_BOARDS = frozenset({"es3c28p"})

CHIP_FAMILY = {
    "esp32": "ESP32",
    "esp32s2": "ESP32-S2",
    "esp32s3": "ESP32-S3",
    "esp32c2": "ESP32-C2",
    "esp32c3": "ESP32-C3",
    "esp32c5": "ESP32-C5",
    "esp32c6": "ESP32-C6",
    "esp32c61": "ESP32-C61",
    "esp32h2": "ESP32-H2",
    "esp32p4": "ESP32-P4",
}


def project_version() -> str:
    with CMAKE.open() as f:
        for line in f:
            if line.startswith("set(PROJECT_VER"):
                return line.split("\"")[1]
    return "0.0.0"


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n")


def board_title(board_dir: Path, fallback: str) -> str:
    readme = board_dir / "README.md"
    if not readme.exists():
        return fallback
    try:
        for line in readme.read_text(encoding="utf-8", errors="ignore").splitlines():
            if line.startswith("# "):
                return line[2:].strip()
    except OSError:
        return fallback
    return fallback


def theme_offset_for(build: dict) -> Optional[int]:
    for line in build.get("sdkconfig_append", []):
        if "16m_es3c28p.csv" in line:
            return THEME_OFFSET
    return None


def collect_boards() -> list[dict]:
    boards: list[dict] = []
    for board_path in sorted(BOARDS_DIR.iterdir()):
        if not board_path.is_dir() or board_path.name == "common":
            continue
        cfg_path = board_path / "config.json"
        if not cfg_path.exists():
            continue
        try:
            cfg = json.loads(cfg_path.read_text())
        except json.JSONDecodeError as exc:
            print(f"[WARN] skip {cfg_path}: {exc}", file=sys.stderr)
            continue
        target = cfg.get("target", "")
        chip = CHIP_FAMILY.get(target)
        if not chip:
            print(f"[WARN] unknown target {target!r} in {cfg_path}", file=sys.stderr)
            continue
        title = board_title(board_path, board_path.name)
        for build in cfg.get("builds", []):
            variant = build.get("name")
            if not variant:
                continue
            label = title if variant == board_path.name else f"{title} ({variant})"
            entry = {
                "id": variant,
                "board": board_path.name,
                "name": variant,
                "title": label,
                "target": target,
                "chipFamily": chip,
                "firmware": f"firmware/{variant}/merged-binary.bin",
            }
            offset = theme_offset_for(build)
            if offset is not None:
                entry["themeOffset"] = offset
            boards.append(entry)
    return [b for b in boards if b["board"] in INSTALLER_BOARDS]


def write_catalog(boards: list[dict], version: str) -> None:
    write_json(
        BOARDS_JSON,
        {
            "name": "XiaoZhi",
            "version": version,
            "worker": "xiaozhi-install",
            "boards": boards,
        },
    )
    write_json(
        VERSION_JSON,
        {
            "version": version,
            "firmware": "firmware/{board}/merged-binary.bin",
        },
    )
    write_json(
        MANIFEST,
        {
            "name": "XiaoZhi",
            "version": version,
            "new_install_prompt_erase": True,
            "builds": [],
        },
    )


def detect_variant() -> Optional[str]:
    if not COMPILE_COMMANDS.exists():
        return None
    try:
        data = json.loads(COMPILE_COMMANDS.read_text())
    except json.JSONDecodeError:
        return None
    for item in data:
        if not str(item.get("file", "")).endswith("main.cc"):
            continue
        cmd = item.get("command", "")
        if "-DBOARD_NAME=\\\"" in cmd:
            return cmd.split("-DBOARD_NAME=\\\"")[1].split("\\\"")[0].strip()
        if "-DBOARD_TYPE=\\\"" in cmd:
            return cmd.split("-DBOARD_TYPE=\\\"")[1].split("\\\"")[0].strip()
    return None


def load_flash_parts(dest: Path, variant: str) -> list[dict]:
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
        shutil.copy2(src, dest / src.name)
        parts.append({"path": f"firmware/{variant}/{src.name}", "offset": int(offset_str, 0)})
    return parts


def copy_merged(dest: Path, variant: str) -> Optional[dict]:
    if not MERGED.exists():
        return None
    shutil.copy2(MERGED, dest / "merged-binary.bin")
    return {"path": f"firmware/{variant}/merged-binary.bin", "offset": 0}


def copy_theme(dest: Path, variant: str, offset: Optional[int]) -> Optional[dict]:
    if offset is None or not THEME_BIN.exists():
        return None
    shutil.copy2(THEME_BIN, dest / "storybook-garden.theme.bin")
    return {"path": f"firmware/{variant}/storybook-garden.theme.bin", "offset": offset}


def prepare_variant(variant: str, boards: list[dict], version: str) -> int:
    meta = next((b for b in boards if b["id"] == variant), None)
    if meta is None:
        print(f"Unknown variant {variant!r}. It must exist in a board config.json.", file=sys.stderr)
        return 1
    dest = FIRMWARE / variant
    dest.mkdir(parents=True, exist_ok=True)
    parts = load_flash_parts(dest, variant)
    merged = copy_merged(dest, variant)
    if not parts and merged:
        parts = [merged]
    if not parts:
        print(
            "No firmware found. Build first:\n"
            f"  python scripts/release.py {meta['board']}\n"
            "or:\n"
            "  idf.py merge-bin",
            file=sys.stderr,
        )
        return 1
    theme = copy_theme(dest, variant, meta.get("themeOffset"))
    write_json(
        dest / "ready.json",
        {
            "ready": True,
            "version": version,
            "board": meta["board"],
            "name": variant,
            "chipFamily": meta["chipFamily"],
            "parts": parts,
            "theme": theme,
        },
    )
    print(f"Installer firmware ready in {dest}")
    for entry in sorted(dest.iterdir()):
        if entry.is_file() and entry.suffix == ".bin":
            print(f"  {entry.name:32} {entry.stat().st_size:10} bytes")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare the XiaoZhi web installer")
    parser.add_argument("variant", nargs="?", help="Variant name from config.json builds[].name")
    parser.add_argument("--from-build", action="store_true", help="Detect variant from the current IDF build")
    args = parser.parse_args()

    FIRMWARE.mkdir(parents=True, exist_ok=True)
    version = project_version()
    boards = collect_boards()
    if not boards:
        print("No boards found under main/boards", file=sys.stderr)
        return 1
    write_catalog(boards, version)
    print(f"Wrote {BOARDS_JSON} ({len(boards)} variants, v{version})")

    variant = args.variant
    if args.from_build:
        variant = detect_variant()
        if not variant:
            print("Could not detect BOARD_NAME / BOARD_TYPE from compile_commands.json", file=sys.stderr)
            return 1
        print(f"Detected variant {variant}")

    if variant:
        code = prepare_variant(variant, boards, version)
        if code != 0:
            return code

    print()
    print("Local preview:")
    print("  python3 -m http.server 8080 --directory web/installer")
    print("GitHub Actions packs web/installer after each board build.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
