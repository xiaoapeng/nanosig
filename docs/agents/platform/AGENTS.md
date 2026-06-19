<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-16 | Updated: 2026-05-24 -->

# platform

## Purpose
Guidance for source directory `platform/`, which contains nanosig's OS abstraction layer. It is the only OS-coupling point for allocation, wakeups, mutexes, clocks, threads, and waitsets.

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
| `macos/` | macOS loop-only backend source. |
| `windows/` | Windows loop-only backend source. |

## For AI Agents

### Working In `platform/`
- Add platform abstractions here, not under `src/`.
- Loop-only scope: single wakeup, mutex, monotonic clock, and allocation.
- Waitset scope: waitset create/destroy/add/remove/wait, and waitable registration state. Waitset decoupled from wakeup.
- Thread scope: thread create/join.
- Do not create empty RTOS/MCU backend directories for v1; document future ports instead.

### Testing Requirements
- Platform edits require the relevant OS preset smoke tests where available, including `ctest --preset macos-release --output-on-failure` on macOS.
- Waitset tests use platform primitives directly, not wakeup: Windows event, Linux eventfd, and macOS kqueue user event. Tests: lifecycle, add/remove semantics, timeout, single signal, multi waitable.

### Common Patterns
- Public core code should consume `platform/port.h` without OS-specific preprocessor branches.

## Dependencies

### Internal
- Future dependency of `src/` implementation files.

### External
- Linux backend uses pthread mutex/thread, eventfd or an equivalent single wakeup, clock APIs, platform allocation, and epoll for waitset.
- macOS backend uses pthread mutex/thread, kqueue EVFILT_USER wakeups, clock APIs, platform allocation, and kqueue/kevent for waitset.
- Windows backend uses CreateThread, auto-reset events, single-handle wait, SRWLOCK, QPC, platform allocation, and WaitForMultipleObjects for waitset.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
