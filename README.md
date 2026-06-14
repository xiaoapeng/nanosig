# nanosig

nanosig is a C11 thread-based signal/slot library planned for Linux and
Windows. It borrows style from `tmp/eventhub_os` but does not keep
source-compatible `eh_*` APIs.

Current status: P6 phase-1 timer manager is implemented. Loop management,
cross-thread emit via MPSC record ring, connect/disconnect, slot dispatch, and
direct timer manager semantics are functional. Public API headers and review
demos remain stable; list, slist, ring buffer, string-key hashtable, rbtree,
and variable-size MPSC record-ring utilities are exposed under
`include/nanosig/` and have CTest coverage on Windows and Linux. Broker-driven
timer integration remains a later phase.

## Configure

```sh
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
cmake --build --preset linux-release --target api-compile-checks
cmake --build --preset linux-release --target sanitize-all
```

On Windows, use the `windows-release` or `windows-debug-asan` preset from an
MSVC developer environment with Ninja available.

## API Review

Review the PD API docs in:

- `docs/共识计划.md`
- `docs/需求访谈.md`
- `docs/API_DESIGN.md`
- `docs/DATA_STRUCTURES.md`
- `docs/THREAD_LOOP_BINDING.md`

Review usage demos in:

- `demos/demo_same_thread.c`
- `demos/demo_cross_thread.c`
- `demos/demo_timer_cross_thread.c`

The PD API syntax checks, platform smoke test, public data-structure tests,
public MPSC runtime test, loop/signal/timer runtime tests, and public
type/data-structure contract checks are wired into CTest and the
`api-compile-checks` target.
