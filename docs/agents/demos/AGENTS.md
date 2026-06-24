<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-16 | Updated: 2026-05-24 -->

# demos

## Purpose
Guidance for source directory `demos/`, which contains API review demos. They are examples for reviewing call shape and are intentionally kept as syntax-only checks.

## Key Files
| File | Description |
|------|-------------|
| `README.md` | Explains why demos are review-only. |
| `demo_same_thread.c` | Shows same-thread loop creation, typed connection, emit, disconnect, and teardown. |
| `demo_cross_thread.c` | Sketches producer/consumer threads where the consumer thread owns the target loop. |
| `demo_timer_cross_thread.c` | Sketches repeating timer creation, embedded timer signal connection, and cross-loop delivery. |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| _None_ | Demo sources are currently flat. |

## For AI Agents

### Working In `demos/`
- Keep examples readable and explicit; use `.field = value` designated initializers.
- Every example that acquires multiple resources must use kernel-style `goto` cleanup labels and release each successfully initialized resource on every failure path.
- Do not wire these files into CMake as linked executables until the corresponding implementation can link them.
- Keep demo API usage synchronized with `docs/API_DESIGN.md`.
- Keep at least one demo covering no-payload signal usage and one demo covering explicit target-loop connection usage.

### Testing Requirements
- Run `ctest --preset windows-release` or `cmake --build --preset windows-release --target api-compile-checks`.

### Common Patterns
- Define payload structs explicitly.
- Define signals as `ns_signal_t name;`, initialize them with `ns_signal_init(&name, payload_type)`, and deinitialize each successful init with `ns_signal_deinit(name)`.
- Define no-payload signals the same way, using `ns_signal_init(&name, ns_no_payload_t)`.
- Use `ns_signal_connect_typed(...)` / `ns_signal_emit(...)` for both payload and no-payload signals; no-payload emit passes `NS_NO_PAYLOAD`.
- Use `_to` connect macros only when the example is intentionally showing an explicit `ns_loop_t *` target.
- Do not instantiate or copy `ns_no_payload_t`; it is a type marker only and maps to a 0-byte payload.
- Timer demos use caller-owned `ns_timer_t`, connect slots to `timer.signal`, use `uint64_t` microsecond intervals, and pass `NS_TIMER_ATTR_*` bitmaps to `ns_timer_init`. Do not add `timer_started` / `timer_created` flags; route cleanup labels so failed create skips timer teardown, and call `ns_timer_cancel` directly after successful create when a cleanup path needs it.

## Dependencies

### Internal
- Includes `include/nanosig/nanosig.h`.

### External
- C11 compiler for syntax checks.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
