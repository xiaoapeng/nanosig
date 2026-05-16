# nanosig

nanosig is a C11 thread-based signal/slot library planned for Linux and
Windows. It borrows style from `tmp/eventhub_os` but does not keep
source-compatible `eh_*` APIs.

Current status: P0 + PD scaffold complete. Public API headers, API review
demos, and compile-only API checks are available; implementation `.c` files are
intentionally pending until the next implementation phase.

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

The PD API syntax checks are wired into CTest and the `api-compile-checks`
target. They cover the API review demos and the macro matrix in
`test/unit/test_macro_expansion.c` without linking against unfinished runtime
implementation.
