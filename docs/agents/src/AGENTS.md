<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-16 | Updated: 2026-05-24 -->

# src

## Purpose
Guidance for source directory `src/`, which contains nanosig implementation sources and internal headers. Loop management, signal/slot runtime, data-structure implementations, and the phase-1 timer manager now live here.

## Key Files
| File | Description |
|------|-------------|
| `nanosig.c` | Core lifecycle, loop runtime, and signal/slot runtime. |
| `ns_timer.c` | Phase-1 timer manager and public timer API implementation. |
| `ns_timer_mgr.h` | Internal timer manager interface used by core lifecycle and timer tests. |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `ds/` | Implementation-backed public data structures. |

## For AI Agents

### Working In `src/`
- Keep OS-specific code out of this directory; use `platform/` instead.
- Keep timer manager broker-independent; phase 1 is tested through `next_timeout` / `fire_expired`, with broker integration deferred.

### Testing Requirements
- Source changes require targeted unit/integration tests plus relevant sanitizer presets.

### Common Patterns
- Internal symbols should use `_ns_*` where cross-file visibility is needed.
- Emit-path internals must remain allocation-free.

## Dependencies

### Internal
- Depends on `include/nanosig/` and `platform/port.h`.

### External
- Standard C and platform abstraction only.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
