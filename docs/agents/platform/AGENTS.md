<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-16 | Updated: 2026-10-11 -->

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
| `freertos/` | FreeRTOS queue-set backend source (real RTOS backend, not a placeholder). |
| `zephyr/` | Zephyr k_poll backend source (real RTOS backend; built as a Zephyr module, not via the top-level `NANOSIG_PLATFORM` build). |

## For AI Agents

### Working In `platform/`
- Add platform abstractions here, not under `src/`.
- Loop-only scope: single wakeup, mutex, monotonic clock, and allocation.
- Waitset scope: waitset create/destroy/add/remove/wait, and waitable registration state. Waitset decoupled from wakeup.
- Thread scope: thread create/join.
- FreeRTOS is a first-class backend selected with `NANOSIG_PLATFORM=freertos`; it is not an empty placeholder. Its host coverage comes from the FreeRTOS POSIX harness (`NANOSIG_BUILD_FREERTOS_POSIX=ON` / `freertos-release` preset), which only validates queue-set semantics.
- Zephyr is a first-class backend built as a Zephyr module (`zephyr/module.yml` + `zephyr/CMakeLists.txt` + `zephyr/Kconfig`, opt-in `CONFIG_NANOSIG_ZEPHYR=y`); the top-level `NANOSIG_PLATFORM=zephyr` value only prints guidance and does not build. Its coverage comes from `test/zephyr/` ztest on `native_sim/native/64` in CI; it requires `CONFIG_POLL=y`, `CONFIG_MULTITHREADING=y`, and `CONFIG_HEAP_MEM_POOL_SIZE > 0`.
- Do not create empty RTOS/MCU backend directories; add real backends with source, contract tests, and docs.

### Testing Requirements
- Platform edits require the relevant OS preset smoke tests where available, including `ctest --preset macos-release --output-on-failure` on macOS.
- Waitset tests use platform primitives directly, not wakeup: Windows event, Linux eventfd, and macOS kqueue user event. Tests: lifecycle, add/remove semantics, timeout, single signal, multi waitable.

### Common Patterns
- Public core code should consume `nanosig/nanosig_port.h` without OS-specific preprocessor branches.

## Dependencies

### Internal
- Future dependency of `src/` implementation files.

### External
- Linux backend uses pthread mutex/thread, eventfd or an equivalent single wakeup, clock APIs, platform allocation, and epoll for waitset.
- macOS backend uses pthread mutex/thread, kqueue EVFILT_USER wakeups, clock APIs, platform allocation, and kqueue/kevent for waitset.
- Windows backend uses CreateThread, auto-reset events, single-handle wait, SRWLOCK, QPC, platform allocation, and WaitForMultipleObjects for waitset.
- FreeRTOS backend uses xTaskCreate + join semaphore, per-object binary-semaphore wakeups (never task notifications), xSemaphoreCreateMutex, xTaskGetTickCount, pvPortMalloc/vPortFree, and queue sets (`xQueueCreateSet`/`xQueueSelectFromSet`, IN-only) for waitset.
- Zephyr backend uses `k_thread_create` + `k_thread_join` with an embedded `K_KERNEL_STACK_MEMBER` stack, per-object `k_sem` wakeups (limit 1), `k_mutex`, `k_uptime_ticks`, `k_malloc`/`k_aligned_alloc`/`k_free`, `k_poll_signal` events (level-triggered; `ns_platform_event_drain` is the only reset site and runs on the poller thread), and `k_poll` (`K_POLL_TYPE_SIGNAL` + `K_POLL_MODE_NOTIFY_ONLY`, IN-only) for waitset.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
