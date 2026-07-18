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
| `docs/review/AGENTS.md` | Review document规范：三大分区、问题 ID、关闭状态、review agent 自动化删除规则。所有 review 文档（`docs/review/*.md`）必须遵守。 |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `bench/` | Future benchmark harness placeholder (see `docs/agents/bench/AGENTS.md`). |
| `cmake/` | Shared CMake helper modules (see `docs/agents/cmake/AGENTS.md`). |
| `demos/` | PD-stage API review demos (see `docs/agents/demos/AGENTS.md`). |
| `docs/` | API and data-structure design drafts plus centralized AGENTS copies (see `docs/AGENTS.md` and `docs/agents/AGENTS.md`). |
| `docs/review/` | Code review documents。**Review 工作前必读 `docs/review/AGENTS.md`**：每个 review agent 只能 review 自己负责的模块；跨模块问题写到 `global-review.md`。所有 review 文档遵守"修复历史 / 现在打开 / 现在关闭"三大分区结构。关闭-已拒绝条目永久保留在主文件中（不归档）；其他关闭条目满一个月由 review agent 自动归档到 `docs/review/history/<模块>/`。 |
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
- Preserve the explicit loop-passing model: `ns_loop_init` does not bind a thread, and `ns_signal_connect` requires a non-null target loop.
- Follow `docs/EVENTHUB_OS_STYLE.md` when borrowing eventhub_os coding/comment style.
- Do not generate AGENTS files inside hidden workflow/runtime directories such as `.omx/`, `.omc/`, or `.github/` unless the user explicitly asks for workflow documentation.
- Keep reference-code documentation shallow; `tmp/eventhub_os/AGENTS.md` is enough unless active work moves into that tree.
- **任何 review 工作必须先读 `docs/review/AGENTS.md`**。该规范强制：① 每 agent 只能 review 自己负责的模块；② Review 文档分三大分区（修复历史 / 现在打开 / 现在关闭）；③ 关闭-已拒绝条目永久保留在主文件中，其他关闭条目满一个月由 review agent 自动归档到 `docs/review/history/<模块>/<模块>-archive-<年>-<月>.md`。

### Testing Requirements
- For scaffold/API-only edits, run `cmake --build --preset windows-release`, `cmake --build --preset windows-release --target api-compile-checks`, `ctest --preset windows-release`, and `cmake --build --preset windows-release --target sanitize-all`.
- For later implementation phases, add targeted tests first, then run the relevant preset plus `sanitize-all`.

### Common Patterns
- **源码中禁止出现阶段计划编号。** 源文件（`.c`、`.h`、`CMakeLists.txt`、`demos/`、`test/`）的注释、字符串和标识符中不得包含 `P0`、`P1a`、`P1b`、`P2`、`P3`、`P4`、`P5b`、`P6`、`P7`、`PD`、`phase-1`、`phase-2` 等计划阶段引用。这些信息只属于 `docs/` 目录下的设计文档。新代码和修改后的代码必须遵守此规则。
- **头文件包含顺序。** 除非逻辑限制，头文件包含顺序应为：系统头文件和稳定的头文件在前，其次是其他库的头文件，然后是用户的通用头文件，最后才是业务相关的头文件。各组之间用空行分隔。
- Public symbols use `ns_*`, public constants use `NS_*`, function-wrapper signal connect/emit macros use lowercase `ns_signal_*`, and declaration/definition/initializer/type-only macros use uppercase `NS_*`.
- Signal connect macros must provide the payload/no-payload and current-loop/explicit-loop matrix without adding a separate `0` function family; no-payload signals use `ns_no_payload_t` and `NS_NO_PAYLOAD`, and typed variants use compile-time slot signature checks.
- Zero tolerance for duplicate internal/public-low-level functions that only differ by default-vs-explicit parameters. Use a parameter such as nullable `target_loop` instead; wrapper macros may keep ergonomic names but must call the single underlying function.
- Configuration examples use C designated initializers.
- Examples that acquire more than one nanosig resource must show kernel-style `goto` cleanup labels and release every successfully initialized resource on all failure paths.
- `ns_no_payload_t` is only a compile-time marker for typed no-payload slots; no-payload emit must pass `NS_NO_PAYLOAD` and copy 0 bytes.
- `ns_signal_t` may be embedded in user structs; initialize members with `ns_signal_init(signal, payload_type)` and release internal resources with `ns_signal_deinit(signal)` after disconnecting connections. Do not reintroduce `NS_SIGNAL_CONFIG_DEFAULT`, `ns_signal_config_t`, or aggregate/static signal initializer macros. `ns_signal_disconnect` removes the connection from the signal's slot list, with `ns_signal_disconnect_all` reserved for teardown escape hatches. Neither function frees memory; callers own `ns_connection_t` storage.
- `ns_timer_t` uses caller-owned storage, has `ns_signal_t signal` as its first field, only emits no-payload events, uses `uint64_t` microsecond intervals, and uses `NS_TIMER_ATTR_*` bitmaps for repeat/reload behavior.
- Platform-specific code belongs under `platform/`; code outside `platform/` should not contain OS preprocessor branches.
- **公共工具宏优先从 `nanosig_types.h` 复用。** 编写宏之前先检查 `nanosig_types.h` 是否有现成的（如 `ns_same_type`、`NS_STATIC_ASSERT`、`NS_CONTAINER_OF` 等）。避免重复造轮子或在各模块头文件中各自实现等价宏。新增通用宏也追加到 `nanosig_types.h`，每个宏附加单行用途注释。

## Dependencies

### Internal
- `include/nanosig/` defines the public API surface consumed by demos, tests, and future implementation.
- `tmp/eventhub_os/` is reference material only, not a source-compatible dependency.

### External
- C11 compiler and CMake 3.20 or newer.
- C++11 or newer for C++ compatibility headers.
- Ninja or Make depending on selected CMake preset.
- Future Linux/macOS sanitizer runs need ASAN/TSAN/UBSAN-capable Clang/GCC or Apple Clang; Windows validation targets MSVC/Clang-cl style environments.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->

## 构建与测试命令

```sh
# 配置 + 构建 + 测试（标准三步）
cmake --preset <preset>-release      # <preset> = linux- | macos- | windows-
cmake --build --preset <preset>-release
ctest --preset <preset>-release

# 编译期 API 契约检查（语法测试）
cmake --build --preset <preset>-release --target api-compile-checks

# 启用 bench 目标
cmake -DNANOSIG_BUILD_BENCH=ON ...

# 启用 sanitizer（all = ASAN/TSAN/UBSAN）
cmake --build <build-dir> --target sanitize-all
```

依赖：C11 编译器 + pthreads（Linux/macOS）；Windows 无外部依赖。

## 项目核心结构（详细）

- `include/nanosig/` — 公共 API 头文件（用户可见）
- `src/` — 实现代码
  - `src/nanosig.c` — 生命周期、loop runtime、signal/slot runtime
  - `src/ns_timer.c` — 定时器管理器
  - `src/ns_broker.c` — 事件 broker
  - `src/ds/` — 数据结构（ringbuf, slist, list, hashtable, rbtree, MPSC record ring）
- `platform/<os>/port.c` — 平台后端（thread / mutex / wakeup / waitset）
- `test/` — 单元测试 + 集成测试（**review 工作不进入**）
- `demos/` — API 验收 demo（**review 工作不进入**）

## 关键架构特性

1. **调用方拥有存储**：`ns_connection_t`、`ns_loop_t`、`ns_timer_t`、`ns_watcher_t` 都是自持类型，库不分配内存
2. **热路径零分配**：emit / dispatch / slot 在预分配 MPSC ring 上操作
3. **显式 loop 绑定**：loop 不绑定线程，跨线程 emit 安全（连接时指定 target_loop）
4. **事件 broker**：全局 `ns_event_broker_t` 把平台事件（fd/handle readiness）和定时器转换为 signal emit

## Review 工作硬性约束（强化版）

任何 review 工作必须先读 `docs/review/AGENTS.md`。三个核心规则：

1. **每个 review agent 只能 review 自己负责的模块**——禁止跨模块 review
2. **review 文档三大分区**：`## 修复历史` / `## 现在打开的问题` / `## 现在关闭的问题`
3. **`关闭-已拒绝` 条目永久保留**；其他关闭条目满一个月由 review agent 自动归档到 `docs/review/history/<模块>/<模块>-archive-<年>-<月>.md`
4. **字段语义**：`review 建议` 由 review agent 写，`作者建议` 由代码作者写（**初始为空**）。**作者建议为空时任何修复 agent 不得擅自实施修复**

跨模块问题写入 `docs/review/global-review.md`。已有的 8 个 review 文件遵循新规范但保留历史内容。

## 工作流速查

- 修改源码后运行：`cmake --build --preset <preset>-release` + `ctest --preset <preset>-release`
- 修改公共 API 后：`cmake --build <preset>-release --target api-compile-checks`
- 修改 thread/mutex/wait：所有三个平台后端都要改（`platform/{linux,macos,windows}/port.c`）
- 启动新模块 review：写 `docs/review/<模块>-code-review.md`，结构按 AGENTS.md §2
- 阶段编号（P0/P1/...）**禁止出现在源码注释、字符串、标识符中**

## 禁止事项（强约束）

- 禁止在源码中引用 `P0`、`P1`、`P2` 等计划阶段编号——这些信息只属于 `docs/`
- 禁止在 `src/`、`include/`、`test/`、`demos/`、`CMakeLists.txt` 中写 OS 预处理器分支——平台特定代码应在 `platform/`
- 禁止在代码中拼出 stage 阶段 ID 的字面量（会被 review 自动检查）

## AI Agent 行为规则（动态累积）

以下规则来自用户交互中的"以后..."指令，必须遵守：

1. **输出问题需附带 review 文档定位**：讨论或输出代码审查问题时，除了代码文件的行号，还必须输出该问题在 review 文档（`docs/review/*.md`）中的位置（文件名 + 行号）。
2. **"以后"指令自动持久化**：当用户提示或对话中出现"以后 X"格式的指令（如"以后输出问题要附带 review 文档行号"），必须立即写入本文件的 `AI Agent 行为规则` 节，后续交互中无条件遵守。
3. **源码定位使用无反引号**：review 文档的 `#### 定位` 中及所有输出中，源码路径不包裹反引号，使用 `file:line` 格式，每行一条（多个引用分散到多行），以利终端识别为可点击链接。文件引用统一使用项目根相对路径，不带 `./` 前缀。例如：
   ```
   #### 定位
   src/ns_broker.c:459
   src/ns_broker.c:551
   ```
   而非 `` `src/ns_broker.c:459` ``。

4. **MPSC 压力测试限时 20 秒**：在普通编译/测试（`cmake --build` + `ctest`）时，`nanosig_test_mpsc_record_ring_stress` 必须限制为 20 秒。如默认值过高，通过 CMake `ENVIRONMENT` 属性或直接传递 `NS_MPSC_RECORD_RING_STRESS_DURATION_SEC=20` 环境变量控制。完整稳定性测试可手动运行测试二进制（不带 env 覆盖）使用默认时长。
