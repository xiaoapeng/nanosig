# CI 流程

nanosig 的持续集成分为两条流水线：快速 CI（`ci.yml`，push / PR 触发）和夜间 CI（`nightly.yml`，定时 + 手动触发）。两者共用 `CMakePresets.json` 中的预置和 `cmake/Sanitizers.cmake`、`cmake/SanitizeAll.cmake` 审计管道。

## 总览

| 流水线 | 文件 | 触发 | 作用 |
|--------|------|------|------|
| 快速 CI | `.github/workflows/ci.yml` | `pull_request`、push 到 `master` | 平台矩阵 + ASAN/UBSAN/Release + `sanitize-all` 审计，跑 `ctest -LE nightly`（排除长时用例） |
| 夜间 CI | `.github/workflows/nightly.yml` | `schedule`（`17 2 * * *` UTC）+ `workflow_dispatch` | Linux ThreadSanitizer，跑**完整**测试集（含 `nightly` 标签的长时用例） |

设计原则：push / PR 必须快，只跑快速 + stress 套件；耗时的长稳定性用例和 TSAN 放到夜间，避免拖慢日常反馈。

## 快速 CI（`ci.yml`）

四个并行 job：

### `linux`

- runner：`ubuntu-latest`
- 矩阵：`linux-debug-asan`、`linux-debug-ubsan`、`linux-release`
- 步骤：Configure → Build → Test（`ctest --preset <preset> -LE nightly`）→ Audits（`cmake --build --preset <preset> --target sanitize-all`）

### `windows`

- runner：`windows-latest`
- 工具链：MinGW-w64 GCC（`C:\mingw64`）。源码使用 GNU 扩展且 broker 测试依赖 POSIX 线程，**MSVC / clang-cl 无法编译本树**，因此必须用 GNU 工具链。
- 预置：`windows-release`，配置时显式指定 `-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++`
- 步骤：Configure → Build → Test（`-LE nightly`）→ Audits。Windows 无 sanitizer 预置，`sanitize-all` 退化为空操作审计。

### `macos`

- runner：`macos-latest`
- 矩阵：`macos-debug-asan`、`macos-debug-ubsan`、`macos-release`
- 步骤：Configure → Build → Test（`-LE nightly`）→ Audits
- 备注：某些 runner 镜像上 `macos-debug-asan` 会在 dyld 启动阶段挂起（ASAN shadow 初始化死锁）。若 CI 卡在 ASAN 入口，需先从矩阵移除该项，待镜像 / Apple Clang ASAN 运行时更新后再加回；UBSAN 与 release 不受影响。

### `freertos`

- runner：`ubuntu-latest` + `macos-latest`（`fail-fast: false`，`timeout-minutes: 20`）
- 预置：`freertos-release`，通过 FetchContent 拉取固定版本 FreeRTOS-Kernel（V11.1.0）
- 步骤：Configure → Build → Test（`-LE nightly`）。只注册 FreeRTOS 契约测试，不跑 `sanitize-all`。
- macOS 会在 `cmake/FreeRTOSPosix.cmake` 中应用带保护的 pthread 栈补丁；Linux 使用未改动的上游 POSIX port。

## 夜间 CI（`nightly.yml`）

- 触发：`schedule`（每天 `17 2 * * *` UTC）+ `workflow_dispatch`（可指定分支，便于验证特性分支）
- job `linux-tsan`：`ubuntu-latest`，预置 `linux-debug-tsan`
- 步骤：Configure TSAN → Build → Test。**不加 `-LE nightly`**，运行全部用例，包括 `ci.yml` 刻意跳过的长时用例。

## 测试标签与 `-LE nightly`

测试在 `CMakeLists.txt` 中通过 `LABELS` 打标签，`ctest -LE nightly` 表示排除带 `nightly` 标签的用例。

| 用例 | 标签 | 是否在快速 CI 运行 |
|------|------|--------------------|
| `nanosig_test_integration_layer1` / `layer2` | `integration;quick` | 是 |
| `nanosig_test_scenario_*` | `integration;quick` | 是 |
| `nanosig_test_mpsc_record_ring_stress` | `stress;long-stability` | 是（通过 `ENVIRONMENT` 限制为 20 秒） |
| `nanosig_test_integration_layer3` | `integration;nightly` | 否（仅夜间） |
| `nanosig_test_integration_hive` | `integration;nightly;long-stability` | 否（仅夜间） |

常规编译 / 测试时，MPSC 压力测试通过 CMake `ENVIRONMENT` 属性固定为 20 秒（`NS_MPSC_RECORD_RING_STRESS_DURATION_SEC=20`）。完整稳定性验证可手动运行测试二进制、去掉该环境变量覆盖，使用默认时长。

## Sanitizer 与 `sanitize-all`

- 开关：`NANOSIG_ENABLE_ASAN`、`NANOSIG_ENABLE_TSAN`、`NANOSIG_ENABLE_UBSAN`（见 `cmake/Sanitizers.cmake`）。ASAN 与 TSAN 互斥。
- `sanitize-all` 目标由 `cmake/SanitizeAll.cmake` 生成：按已启用的 sanitizer 派生 `nanosig_audit_<kind>` 子目标，执行 A1 api-contract、A2 config、A3 headers、A4 encoding 审计管道（`cmake/scripts/audit_runner.sh`）。该目标**不运行 ctest**，测试由 CI 的 Test 步骤负责。
- TSAN 仅在 Linux 夜间流水线作为权威门槛；Windows 忽略 TSAN 请求。

## 本地复现

```sh
# 快速路径（与 ci.yml 一致）
cmake --preset <preset>                 # linux- | macos- | windows- + {release,debug-asan,debug-ubsan}
cmake --build --preset <preset>
ctest --preset <preset> -LE nightly
cmake --build --preset <preset> --target sanitize-all

# 完整路径（与 nightly.yml 一致，需 Linux + clang/gcc）
cmake --preset linux-debug-tsan
cmake --build --preset linux-debug-tsan
ctest --preset linux-debug-tsan

# 本地手动触发夜间流水线（验证特性分支）
gh workflow run nightly.yml --ref <branch>
gh run watch
```

预置列表（`CMakePresets.json`）：`linux-release`、`linux-debug-asan`、`linux-debug-tsan`、`linux-debug-ubsan`、`linux-coverage`、`macos-release`、`macos-debug-asan`、`macos-debug-ubsan`、`windows-release`、`windows-debug-asan`、`freertos-release`。

## 已知限制

- 本地 macOS TSAN 构建（`-DNANOSIG_ENABLE_TSAN=ON`）会产生库内部误报：Apple TSAN 不建模 macOS wakeup 的握手同步，导致 broker 与 loop 之间的访问被误判为竞争。权威门槛始终是 Linux TSAN 夜间流水线。
- `macos-debug-asan` 存在 runner 镜像相关的启动挂起风险（见上文 `macos` job 备注）。
- Windows 只支持 MinGW-w64 GNU 工具链，不支持 MSVC / clang-cl。
