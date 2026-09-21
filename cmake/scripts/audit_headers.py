#!/usr/bin/env python3
"""audit_headers.py: verify public headers surface thread-safety contract.

Each required header must declare either a Doxygen @thread-safety tag, or a
@warning comment that names thread-safety / MPM-safe semantics. Optional
headers only warn when missing so that incremental cleanup does not block
the audit pipeline.
"""

import argparse
import sys
from pathlib import Path

REQUIRED_HEADERS = [
    "include/nanosig/nanosig_port.h",
    "include/nanosig/nanosig_loop.h",
    "include/nanosig/nanosig_broker.h",
    "include/nanosig/nanosig_signal.h",
    "include/nanosig/nanosig_slist.h",
    "include/nanosig/nanosig_list.h",
    "include/nanosig/nanosig_ringbuf.h",
    "include/nanosig/nanosig_hashtbl.h",
    "include/nanosig/nanosig_mpsc_record_ring.h",
]

OPTIONAL_HEADERS = [
    "include/nanosig/nanosig_rbtree.h",
]

THREAD_SAFETY_MARKERS = (
    "@thread-safety",
    "\u7ebf\u7a0b\u5b89\u5168",
    "\u975e MPM-safe",
    "MPM-safe",
    "\u975e\u7ebf\u7a0b\u5b89\u5168",
    "\u5355\u8bfb\u5355\u5199",
    "\u539f\u5b50\u64cd\u4f5c",
    "\u5e76\u53d1",
    "\u4e32\u884c",
)

KEY_SYMBOLS = {
    "include/nanosig/nanosig_loop.h":   ("ns_loop_init", "ns_loop_deinit", "ns_loop_run", "ns_loop_quit", "ns_loop_stop"),
    "include/nanosig/nanosig_broker.h": ("ns_broker", "ns_broker_add", "ns_broker_remove"),
    "include/nanosig/nanosig_signal.h": ("ns_signal_init", "ns_signal_deinit", "ns_signal_emit"),
}


def _has_thread_safety_doc(text: str) -> bool:
    return any(marker in text for marker in THREAD_SAFETY_MARKERS)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--kind", required=True)
    args = parser.parse_args()

    src = Path(args.source_dir)
    failures: list[str] = []
    warnings: list[str] = []

    for rel in REQUIRED_HEADERS:
        path = src / rel
        if not path.exists():
            failures.append(f"missing required file: {rel}")
            continue
        text = path.read_text(encoding="utf-8", errors="strict")
        if not _has_thread_safety_doc(text):
            failures.append(f"{rel}: no thread-safety / MPM-safe / @warning marker")

    for rel in OPTIONAL_HEADERS:
        path = src / rel
        if not path.exists():
            warnings.append(f"optional header not found: {rel}")
            continue
        text = path.read_text(encoding="utf-8", errors="strict")
        if not _has_thread_safety_doc(text):
            warnings.append(f"{rel}: optional header has no thread-safety marker")

    for rel, symbols in KEY_SYMBOLS.items():
        path = src / rel
        if not path.exists():
            failures.append(f"missing required file: {rel}")
            continue
        text = path.read_text(encoding="utf-8", errors="strict")
        for sym in symbols:
            if sym not in text:
                failures.append(f"{rel}: expected symbol {sym!r} not found")

    for w in warnings:
        print(f"[audit_headers] ({args.kind}) WARN: {w}", file=sys.stderr)

    if failures:
        print(f"[audit_headers] ({args.kind}) FAIL:", file=sys.stderr)
        for item in failures:
            print(f"  - {item}", file=sys.stderr)
        return 1

    print(
        f"[audit_headers] ({args.kind}) OK "
        f"({len(REQUIRED_HEADERS)} required headers, "
        f"{len(warnings)} optional warnings)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
