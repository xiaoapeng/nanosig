<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-16 | Updated: 2026-05-24 -->

# platform

## Purpose
Guidance for source directory `platform/`, which contains nanosig's OS abstraction layer. It is the only OS-coupling point for loop-only allocation, TLS, wakeups, mutexes, and clocks during P1b.

## Key Files
| File | Description |
|------|-------------|
| `port.h` | Internal loop-only platform contract used by core implementation. |
| `README.md` | Platform contract, backend mapping, lifecycle, and deferred broker/waitset notes. |
| `.gitkeep` | Historical placeholder. |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `linux/` | Linux loop-only backend source. |
| `windows/` | Windows loop-only backend source. |

## For AI Agents

### Working In `platform/`
- Add platform abstractions here, not under `src/`.
- Keep P1b backend scope loop-only: TLS, single wakeup, mutex, monotonic clock, and allocation.
- Do not add thread creation, condvar, wait-many, fd/socket readiness, or broker/waitset APIs until the later P5b broker phase.
- Do not create empty RTOS/MCU backend directories for v1; document future ports instead.

### Testing Requirements
- Platform edits require `ctest --preset windows-release --output-on-failure` locally and Linux preset smoke tests where available.

### Common Patterns
- Public core code should consume `platform/port.h` without OS-specific preprocessor branches.

## Dependencies

### Internal
- Future dependency of `src/` implementation files.

### External
- Linux backend uses pthread TLS/mutex, eventfd or an equivalent single wakeup, clock APIs, and platform allocation.
- Windows backend uses TLS, auto-reset events, single-handle wait, SRWLOCK, QPC, and platform allocation.
- `WaitForMultipleObjects`, fd/socket readiness, and broker waitsets are deferred to P5b.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
