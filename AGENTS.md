<!-- Generated: 2026-05-16 | Updated: 2026-05-16 -->

# nanosig

## Purpose
nanosig is a new C11 signal/slot library being built with explicit-passing event loops, variable-size MPSC record-ring cross-thread emit, a global timer service, an event broker, and Linux/macOS/Windows platform backends. The repository has completed scaffolding, API design, platform backends, data structures, MPSC record ring, loop management, signal/slot runtime, timer manager, and event broker — all implemented and tested.

## Key Files
| File | Description |
|------|-------------|
| `CMakeLists.txt` | Top-level CMake project, static compile-anchor target, compile-only API checks, testing enablement, and `sanitize-all` placeholder. |
| `CMakePresets.json` | Linux, macOS, and Windows configure/build/test presets, all writing to `build/` for clangd and CMake plugin integration. |
| `README.md` | Current project status, quick configure commands, and API review entry points. |
| `LICENSE` | MIT license for nanosig. |
| `docs/plans/共识计划.md` | 中文权威共识计划，记录当前阶段状态、API 决策、阶段计划和验证证据。 |
| `docs/specs/需求访谈.md` | 中文需求访谈收口文档，记录目标、约束、验收标准和被 PD 更新覆盖的早期结论。 |
| `docs/plans/子计划-信号槽定时器代理测试方案.md` | 共识评审后的信号槽/Timer/Broker 全量测试实施计划。 |
| `docs/specs/深度访谈-信号槽定时器代理测试规格.md` | 深度访谈输出的信号槽/Timer/Broker 测试规格文档。 |
| `docs/EVENTHUB_OS_STYLE.md` | Recorded eventhub_os code/comment style and nanosig-specific adaptations. |
| `docs/THREAD_LOOP_BINDING.md` | Current explicit-loop-passing design constraint. |
| `docs/agents/AGENTS.md` | Central index for moved directory-specific AGENTS instructions. |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `bench/` | Future benchmark harness placeholder (see `docs/agents/bench/AGENTS.md`). |
| `cmake/` | Shared CMake helper modules (see `docs/agents/cmake/AGENTS.md`). |
| `demos/` | PD-stage API review demos (see `docs/agents/demos/AGENTS.md`). |
| `docs/` | API and data-structure design drafts plus centralized AGENTS copies (see `docs/AGENTS.md` and `docs/agents/AGENTS.md`). |
| `include/` | Public nanosig headers (see `docs/agents/include/AGENTS.md`). |
| `platform/` | Future nanosig platform abstraction backends (see `docs/agents/platform/AGENTS.md`). |
| `src/` | Future nanosig implementation sources (see `docs/agents/src/AGENTS.md`). |
| `test/` | Future nanosig tests (see `docs/agents/test/AGENTS.md`). |
| `tmp/` | Reference source snapshots, especially eventhub_os (see `tmp/AGENTS.md`). |

## For AI Agents

### Working In This Directory
- Treat `docs/plans/共识计划.md` and `docs/specs/需求访谈.md` as the binding design context. Hidden `.omx/` and `.omc/` artifacts are workflow caches, not the public source of truth.
- The empty `src/nanosig.c` file is a compile-anchor exception for CMake and clangd, and `api-compile-checks` is syntax-only.
- Do not port `eh_*` symbols into public nanosig APIs except the explicitly planned `eh_atomic.h` to `ns_atomic.h` reuse.
- Preserve the latest PD API style: no public `__safety` annotations for now; function-wrapper connect/emit macros are lowercase, while declaration/definition/initializer/type-only macros are uppercase; use C designated initializers in examples.
- The moved per-directory AGENTS instructions are centralized under `docs/agents/`; update those copies when source-tree guidance changes.
- Preserve the explicit loop-passing model: `ns_loop_create` does not bind a thread, and `ns_signal_connect` requires a non-null target loop.
- Follow `docs/EVENTHUB_OS_STYLE.md` when borrowing eventhub_os coding/comment style.
- Do not generate AGENTS files inside hidden workflow/runtime directories such as `.omx/`, `.omc/`, or `.github/` unless the user explicitly asks for workflow documentation.
- Keep reference-code documentation shallow; `tmp/eventhub_os/AGENTS.md` is enough unless active work moves into that tree.

### Testing Requirements
- For scaffold/API-only edits, run `cmake --build --preset windows-release`, `cmake --build --preset windows-release --target api-compile-checks`, `ctest --preset windows-release`, and `cmake --build --preset windows-release --target sanitize-all`.
- For later implementation phases, add targeted tests first, then run the relevant preset plus `sanitize-all`.

### Common Patterns
- **源码中禁止出现阶段计划编号。** 源文件（`.c`、`.h`、`CMakeLists.txt`、`demos/`、`test/`）的注释、字符串和标识符中不得包含 `P0`、`P1a`、`P1b`、`P2`、`P3`、`P4`、`P5b`、`P6`、`P7`、`PD`、`phase-1`、`phase-2` 等计划阶段引用。这些信息只属于 `docs/` 目录下的设计文档。新代码和修改后的代码必须遵守此规则。
- Public symbols use `ns_*`, public constants use `NS_*`, function-wrapper signal connect/emit macros use lowercase `ns_signal_*`, and declaration/definition/initializer/type-only macros use uppercase `NS_*`.
- Signal connect macros must provide the payload/no-payload and current-loop/explicit-loop matrix without adding a separate `0` function family; no-payload signals use `ns_no_payload_t` and `NS_NO_PAYLOAD`, and typed variants use compile-time slot signature checks.
- Zero tolerance for duplicate internal/public-low-level functions that only differ by default-vs-explicit parameters. Use a parameter such as nullable `target_loop` instead; wrapper macros may keep ergonomic names but must call the single underlying function.
- Configuration examples use C designated initializers.
- Examples that acquire more than one nanosig resource must show kernel-style `goto` cleanup labels and release every successfully initialized resource on all failure paths.
- `ns_no_payload_t` is only a compile-time marker for typed no-payload slots; no-payload emit must pass `NS_NO_PAYLOAD` and copy 0 bytes.
- `ns_signal_t` may be embedded in user structs; initialize members with `ns_signal_init(signal, payload_type)` and release internal resources with `ns_signal_deinit(signal)` after disconnecting connections. Do not reintroduce `NS_SIGNAL_CONFIG_DEFAULT`, `ns_signal_config_t`, or aggregate/static signal initializer macros. `ns_signal_disconnect` removes the connection from the signal's slot list, with `ns_signal_disconnect_all` reserved for teardown escape hatches. Neither function frees memory; callers own `ns_connection_t` storage.
- `ns_timer_t` uses caller-owned storage, has `ns_signal_t signal` as its first field, only emits no-payload events, uses `uint64_t` microsecond intervals, and uses `NS_TIMER_ATTR_*` bitmaps for repeat/reload behavior.
- Platform-specific code belongs under `platform/`; code outside `platform/` should not contain OS preprocessor branches.

## Dependencies

### Internal
- `include/nanosig/` defines the public API surface consumed by demos, tests, and future implementation.
- `tmp/eventhub_os/` is reference material only, not a source-compatible dependency.

### External
- C11 compiler and CMake 3.20 or newer.
- Ninja or Make depending on selected CMake preset.
- Future Linux/macOS sanitizer runs need ASAN/TSAN/UBSAN-capable Clang/GCC or Apple Clang; Windows validation targets MSVC/Clang-cl style environments.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
