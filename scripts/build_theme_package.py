#!/usr/bin/env python3
"""Build an independently flashable ES3C28P theme package."""

import argparse
import json
import pathlib
import struct
import zlib


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    manifest = json.loads(args.input.read_text(encoding="utf-8"))
    if not isinstance(manifest.get("id"), str) or not isinstance(
        manifest.get("palette"), dict
    ):
        raise SystemExit("theme manifest requires string 'id' and object 'palette'")
    payload = json.dumps(
        manifest, ensure_ascii=False, separators=(",", ":"), sort_keys=True
    ).encode("utf-8")
    header = struct.pack("<4sHHII", b"XTHM", 1, 0, len(payload), zlib.crc32(payload))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + payload)
    print(f"Built {args.output} ({len(header) + len(payload)} bytes)")


if __name__ == "__main__":
    main()
