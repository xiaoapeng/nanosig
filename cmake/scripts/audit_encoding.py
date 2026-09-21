#!/usr/bin/env python3
"""audit_encoding.py: enforce strict UTF-8 + mojibake scan over repo text.

Re-runs the encoding sanity policy described in the consensus plan:
- every text file under include/, src/, test/, docs/, platform/, cmake/,
  demos/, bench/ and the top-level project files must decode strictly as
  UTF-8;
- ASCII-range mojibake fingerprints (UTF-8 bytes re-decoded as Latin-1
  then re-encoded as UTF-8) must not appear in any scanned file.
"""

import argparse
import sys
from pathlib import Path

SCAN_DIRS = [
    "include",
    "src",
    "test",
    "docs",
    "platform",
    "cmake",
    "demos",
    "bench",
]

SCAN_TOP = [
    "CMakeLists.txt",
    "CMakePresets.json",
    "README.md",
    "LICENSE",
    "AGENTS.md",
]

MOJIBAKE_FINGERPRINTS = [
    "\u00e2\u20ac\u2122",  # right single quote double-encoded
    "\u00e2\u20ac\u0153",  # left double quote double-encoded
    "\u00e2\u20ac\u009d",  # right double quote double-encoded
    "\u00c2\u00a0",        # non-breaking space double-encoded
    "\u00c3\u00a9",        # e-acute double-encoded
    "\u00c3\u00a8",        # e-grave double-encoded
]

SELF_SKIP_SUBSTR = "cmake/scripts/audit_"


def _iter_files(src: Path):
    for rel in SCAN_DIRS:
        base = src / rel
        if not base.exists():
            continue
        for p in base.rglob("*"):
            if not p.is_file():
                continue
            rel_text = str(p.relative_to(src))
            if SELF_SKIP_SUBSTR in rel_text:
                continue
            yield p
    for rel in SCAN_TOP:
        p = src / rel
        if p.exists() and p.is_file():
            yield p


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", required=True)
    args = parser.parse_args()

    src = Path(args.source_dir)
    bad_decode: list[tuple[str, str]] = []
    mojibake: list[tuple[str, str]] = []
    scanned = 0

    for path in _iter_files(src):
        try:
            data = path.read_bytes()
        except OSError:
            continue
        if b"\x00" in data[:8192]:
            continue
        scanned += 1
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError as exc:
            bad_decode.append((str(path.relative_to(src)), str(exc)))
            continue
        for pat in MOJIBAKE_FINGERPRINTS:
            if pat in text:
                mojibake.append((str(path.relative_to(src)), pat))
                break

    failed = False
    if bad_decode:
        print("[audit_encoding] FAIL: UTF-8 decode errors:", file=sys.stderr)
        for rel, err in bad_decode:
            print(f"  - {rel}: {err}", file=sys.stderr)
        failed = True
    if mojibake:
        print("[audit_encoding] FAIL: mojibake markers found:", file=sys.stderr)
        for rel, pat in mojibake:
            print(f"  - {rel}: {pat!r}", file=sys.stderr)
        failed = True

    if failed:
        return 1
    print(f"[audit_encoding] OK ({scanned} files scanned)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
