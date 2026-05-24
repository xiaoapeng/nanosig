<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-16 | Updated: 2026-05-24 -->

# nanosig

## Purpose
Guidance for source directory `include/nanosig/`, which defines the public nanosig API surface for lifecycle, loop, signal, timer, and future safety extension points.

## Key Files
| File | Description |
|------|-------------|
| `nanosig.h` | Umbrella header, version constants, status codes, and global lifecycle APIs. |
| `nanosig_loop.h` | Opaque loop type, loop configuration, and loop lifecycle/run APIs. |
| `nanosig_signal.h` | Signal/connection types, function-like macros, slot connection APIs, and raw emit API. |
| `nanosig_timer.h` | Timer type with embedded no-payload signal and timer lifecycle APIs. |
| `nanosig_safety.h` | Reserved future v2 safety/ISR extension point; currently does not expose `__safety`. |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| _None_ | Public API is currently flat. |

## For AI Agents

### Working In `include/nanosig/`
- Every public API declaration and public macro must have a Chinese doc comment.
- Keep function-wrapper public macros lowercase with the `ns_signal_*` prefix, for example `ns_signal_connect_typed(...)`, `ns_signal_connect_typed_to(...)`, and `ns_signal_emit(...)`; use uppercase for declaration, definition, initializer, payload metadata, and type-only macros.
- Do not reintroduce public `__safety` annotations unless the user explicitly changes the PD decision.
- Use designated initializers in public examples and initializer macros.
- Do not add low-level function pairs that only differ by default-vs-explicit target parameters. `ns_signal_connect` uses a nullable `target_loop` parameter for both cases; wrapper macros may keep ergonomic `_to` names.
- Keep `ns_no_payload_t` as a type marker only: public macros must map it to `payload_size == 0`, no-payload emit uses `NS_NO_PAYLOAD`, and implementations must copy 0 payload bytes for it.
- Support struct-owned signals through `NS_SIGNAL_INITIALIZER(payload_type)` for aggregate/member initialization and `ns_signal_init(signal, payload_type)` for dynamic object metadata. Do not reintroduce `NS_SIGNAL_CONFIG_DEFAULT`, `ns_signal_config_t`, or `ns_signal_deinit`; connection resources are released by `ns_signal_disconnect`, with `ns_signal_disconnect_all` as a teardown escape hatch.

### Testing Requirements
- Run C11 syntax checks against all demo files after header edits.
- Reconfigure/build the scaffold when changing include dependencies.

### Common Patterns
- Opaque handles use forward declarations such as `typedef struct ns_loop ns_loop_t;`.
- Public functions return `int` status codes from `ns_status_t`.
- `ns_signal_t` is visible enough for static definition but implementation state stays in `impl`.
- `ns_loop_t` remains the public event-loop type, with a one-loop-per-thread binding invariant.
- Signal connect macros must cover payload/no-payload and current-loop/explicit-loop combinations without adding a separate `0` function family; no-payload signals use `ns_no_payload_t` and `NS_NO_PAYLOAD`.
- `ns_timer_t` is caller-owned storage; its first field is `ns_signal_t signal`, timer callbacks connect to that no-payload signal, intervals are `uint64_t` microseconds, and attr bits follow `NS_TIMER_ATTR_*`. `ns_timer_cancel` is valid for any successfully created timer and is a no-op success path when it is not running.

## Dependencies

### Internal
- `nanosig.h` includes loop, signal, and timer headers after declaring lifecycle/status APIs.

### External
- Standard C headers: `stddef.h`, `stdint.h`.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
