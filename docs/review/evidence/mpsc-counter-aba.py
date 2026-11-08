"""Historical pre-fix macOS reproducer: simulate 32-bit positions, retain native headers.
Run from repo root after building build/libnanosig.a. No production files are edited.
The pre-CAS hook deterministically models a paused producer and other actors.
Source and public header are pinned to c152789; the current implementation is covered by CTest's ABA test.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
source = subprocess.check_output([
    "git", "show", "c1527890bb1f3bb6705809b4d7ac674bcd9180cf:src/ds/ns_mpsc_record_ring.c"
], cwd=root, text=True)
header = subprocess.check_output([
    "git", "show", "c1527890bb1f3bb6705809b4d7ac674bcd9180cf:include/nanosig/nanosig_mpsc_record_ring.h"
], cwd=root, text=True)
replacements = {
    "return write_pos - read_pos;": "return (uint32_t)(write_pos - read_pos);",
    "desired = write_pos + plan.publish_size;": "desired = (uint32_t)(write_pos + plan.publish_size);\n        pause_before_cas();",
    "reserve_pos + plan.publish_size, ns_memory_order_release": "(uint32_t)(reserve_pos + plan.publish_size), ns_memory_order_release",
    "read_pos + stride, ns_memory_order_release": "(uint32_t)(read_pos + stride), ns_memory_order_release",
}
for old, new in replacements.items():
    assert old in source, "Source changed; recheck instrumentation: " + old
    source = source.replace(old, new)
HARNESS = r'''#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
void pause_before_cas(void);
#include "ring32.c"
static ns_mpsc_record_ring_t ring;
static size_t storage[65536 / sizeof(size_t)];
static unsigned char payload[32768 - sizeof(size_t)];
static int armed = 1;
void pause_before_cas(void) {
    if (!armed) return;
    armed = 0;
    void *record; size_t size;
    memset(payload, 0x42, sizeof payload);
    for (unsigned i=0; i<131070; ++i) {
        assert(ns_mpsc_record_ring_try_push(&ring,payload,sizeof payload)==NS_OK);
        assert(ns_mpsc_record_ring_try_acquire(&ring,&record,&size)==NS_OK);
        assert(ns_mpsc_record_ring_release(&ring,record)==NS_OK);
    }
    assert(ns_mpsc_record_ring_try_push(&ring,payload,sizeof payload)==NS_OK);
    assert(ns_mpsc_record_ring_try_push(&ring,payload,sizeof payload)==NS_OK);
    printf("Before stale CAS: reserve=%zu write=%zu read=%zu free=%zu\n",(size_t)ring.reserve_pos,(size_t)ring.write_pos,(size_t)ring.read_pos,ns_mpsc_record_ring_free_capacity(&ring));
    assert(((unsigned char *)storage)[sizeof(size_t)]==0x42);
}
int main(void) {
    unsigned char original[sizeof payload]; memset(original,0x41,sizeof original);
    assert(ns_mpsc_record_ring_init(&ring,storage,NS_CAPACITY_65536)==NS_OK);
    int rc=ns_mpsc_record_ring_try_push(&ring,original,sizeof original);
    printf("Stale producer returned %d; unread first byte = 0x%02x (expected 0x42)\n",rc,((unsigned char *)storage)[sizeof(size_t)]);
    void *record; size_t size;
    printf("Consumer acquire = %d\n",ns_mpsc_record_ring_try_acquire(&ring,&record,&size));
    assert(rc==NS_OK);
    assert(((unsigned char *)storage)[sizeof(size_t)]==0x41);
}
'''
with tempfile.TemporaryDirectory(prefix="nanosig-mpsc-aba-") as directory:
    tmp = Path(directory)
    (tmp / "nanosig").mkdir()
    (tmp / "nanosig/nanosig_mpsc_record_ring.h").write_text(header)
    (tmp / "ring32.c").write_text(source)
    (tmp / "aba.c").write_text(HARNESS)
    subprocess.run(["clang", "-std=c11", "-O2", "-DNS_MPSC_CACHELINE_ALIGNED",
        "-I" + str(tmp), "-I" + str(root / "include"), "-I" + str(root / "build/include"),
        "-I" + str(root / "platform/macos"), str(tmp / "aba.c"),
        str(root / "build/libnanosig.a"), "-o", str(tmp / "aba")], check=True)
    subprocess.run([str(tmp / "aba")], check=True)
