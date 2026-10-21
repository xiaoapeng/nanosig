<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-16 | Updated: 2026-10-11 -->

# platform

## Purpose
Guidance for source directory `platform/`, which contains nanosig's OS abstraction layer. It is the only OS-coupling point for allocation, wakeups, mutexes, clocks, threads, and waitsets.

## Key Files
| File | Description |
|------|-------------|
| `README.md` | Platform contract, backend mapping, lifecycle/ownership rules, and platform-type injection. |
| `.gitkeep` | Historical placeholder. |

Each `<platform>/` directory holds both the backend implementation `port.c` and the
build-in type header `nanosig_platform_port.h` (transparent aliases of that platform's
primitives + `ns_waitable_handle_is_valid` + `NS_PLATFORM_ABI_PROBE`).

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `linux/` | Linux loop-only backend source + platform type header. |
| `macos/` | macOS loop-only backend source + platform type header. |
| `windows/` | Windows loop-only backend source + platform type header. |
| `freertos/` | FreeRTOS queue-set backend source (real RTOS backend, not a placeholder) + platform type header. |
| `zephyr/` | Zephyr k_poll backend source (real RTOS backend; built as a Zephyr module, not via the top-level `NANOSIG_PLATFORM` build) + platform type header. |

## For AI Agents

### Working In `platform/`
- Add platform abstractions here, not under `src/`.
- Platform objects (wakeup / mutex / thread / waitset) are **caller-owned storage**: the
  types are transparent aliases of kernel primitives defined per platform in
  `nanosig_platform_port.h`, and the API is `*_init` / `*_deinit` (never create/destroy
  with allocation). `ns_platform_thread_join` keeps the `join` name because it blocks.
- The platform include dir is the **only** platform-selection injection point: all five backend
  headers share the fixed name `nanosig_platform_port.h`, and `nanosig_port.h` includes it by
  that name (`#include <nanosig_platform_port.h>`) with no platform-detection macros. Top-level
  `CMakeLists.txt` sets `NANOSIG_PLATFORM_DIR` and propagates that dir as a PUBLIC include path;
  Zephyr uses `zephyr/CMakeLists.txt`. A missing include path fails at compile time
  ("cannot find nanosig_platform_port.h") rather than silently selecting the wrong types.
- Platform headers are build-in (never installed) and may pull in platform SDK headers
  (`pthread.h` / `windows.h` / `FreeRTOS.h` / `zephyr/kernel.h`).
- Loop-only scope: single wakeup, mutex, monotonic clock, and allocation.
- Waitset scope: waitset init/deinit/add/remove/wait, and waitable registration state. Waitset decoupled from wakeup.
- Thread scope: thread init/join, plus the stack-size parameter (`ns_platform_thread_init`'s
  5th arg, in **bytes**, `0` = backend default) and the backend's acceptable range
  (`ns_platform_thread_stack_range`, whose `min` is the smallest **accepted request**).
  Per-backend mapping (bytes → native unit) and the lower-bound check live in each
  backend; the public API stays byte-based and platform-agnostic.
- No NULL sentinel exists for caller-owned storage, so "already initialized" state is
  tracked with dedicated flags in library code (`ns_signal_t.mutex_initialized`,
  `struct ns_loop.async_started`, `ns_debug.c`'s ready flag, `ns_timer_mgr.initialized`).
- FreeRTOS is a first-class backend selected with `NANOSIG_PLATFORM=freertos`; it is not an empty placeholder. Its host coverage comes from the FreeRTOS POSIX harness (`NANOSIG_BUILD_FREERTOS_POSIX=ON` / `freertos-release` preset), which only validates queue-set semantics.
- Zephyr is a first-class backend built as a Zephyr module (`zephyr/module.yml` + `zephyr/CMakeLists.txt` + `zephyr/Kconfig`, opt-in `CONFIG_NANOSIG_ZEPHYR=y`); the top-level `NANOSIG_PLATFORM=zephyr` value only prints guidance and does not build. Its coverage comes from `test/zephyr/` ztest on `native_sim/native/64` in CI; it requires `CONFIG_POLL=y`, `CONFIG_MULTITHREADING=y`, and `CONFIG_HEAP_MEM_POOL_SIZE > 0`.
- Do not create empty RTOS/MCU backend directories; add real backends with source, contract tests, and docs.

### Testing Requirements
- Platform edits require the relevant OS preset smoke tests where available, including `ctest --preset macos-release --output-on-failure` on macOS.
- Waitset tests use platform primitives directly, not wakeup: Windows event, Linux eventfd, and macOS kqueue user event. Tests: lifecycle, add/remove semantics, timeout, single signal, multi waitable.

### Common Patterns
- Public core code should consume `nanosig/nanosig_port.h` without OS-specific preprocessor branches; platform identity comes only from the platform include path.

## Dependencies

### Internal
- Future dependency of `src/` implementation files.

### External
- Linux backend uses pthread mutex/thread, eventfd or an equivalent single wakeup, clock APIs, platform allocation, and epoll for waitset.
- macOS backend uses pthread mutex/thread, kqueue EVFILT_USER wakeups, clock APIs, platform allocation, and kqueue/kevent for waitset.
- Windows backend uses `_beginthreadex` (not `CreateThread`), auto-reset events, single-handle wait, SRWLOCK, QPC, platform allocation, and WaitForMultipleObjects for waitset. Its minimum stack size is a **nanosig policy value** (Win32 threads have no guard page, so an undersized stack is a silent overflow), not a platform guarantee.
- FreeRTOS backend uses xTaskCreate + join semaphore, per-object binary-semaphore wakeups (never task notifications), xSemaphoreCreateMutex, xTaskGetTickCount, pvPortMalloc/vPortFree, and queue sets (`xQueueCreateSet`/`xQueueSelectFromSet`, IN-only) for waitset. Thread stack sizes arrive in bytes and are rounded **up** to whole `StackType_t` words.
- Zephyr backend uses `k_thread_create` + `k_thread_join` with a **runtime-allocated** kernel stack (`k_thread_stack_alloc` / `k_thread_stack_free`, requiring `CONFIG_DYNAMIC_THREAD` + `CONFIG_DYNAMIC_THREAD_ALLOC`, which `zephyr/Kconfig` selects); the thread object is the one platform type owned by the port rather than by the caller (allocated in `ns_platform_thread_init`, freed in `ns_platform_thread_join`), so the stack's alignment requirement never reaches the generic allocator (`ns_platform_alloc` is plain `k_malloc` and the heap-allocated `ns_loop_t`/broker only hold a handle), per-object by-value `k_sem` wakeups (limit 1), by-value `k_mutex`, `k_uptime_ticks`, `k_malloc`/`k_free` for internal state, `k_poll_signal` events (level-triggered; `ns_platform_event_drain` is the only reset site and runs on the poller thread), and `k_poll` (`K_POLL_TYPE_SIGNAL` + `K_POLL_MODE_NOTIFY_ONLY`, IN-only) for waitset.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
