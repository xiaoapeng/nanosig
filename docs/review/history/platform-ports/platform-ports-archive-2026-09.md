# platform-ports review 归档 2026-09

<a id="PORT-017"></a>
### PORT-017: `ns_platform_event_*` 生命周期契约不完整（重复 init 泄漏 / 未初始化调用）

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: doc
- **关闭原因**: 已按"文档保护"处置：include/nanosig/nanosig_port.h:316-346 的 event 文档块补 `@warning`（重复 init 泄漏底层 fd/HANDLE）与 `@pre`（signal/drain/deinit 前须成功 init，deinit 另需已 waitset_remove）。无代码改动，未加运行时守卫。
- **关闭日期**: 2026-09-21

#### 问题描述

`ns_platform_event_t` 是本次 event 重构新增的 caller-owned 平台原语。其公共文档块
include/nanosig/nanosig_port.h:316-346 只描述字段语义与四条"硬语义"，未声明两条
生命周期前提：

1. **init 非幂等**：对同一 event 重复调用 `ns_platform_event_init` 而不先 `deinit`
   会覆盖 `waitable` / `signal_handle`，泄漏第一次分配的资源（Linux `eventfd`
   一个 fd、macOS `pipe` 两个 fd、Windows `CreateEvent` 一个 HANDLE）。三个后端
   init 都是"无条件新建资源、不检查旧状态"，见 platform/linux/port.c:199-219、
   platform/macos/port.c:240-266、platform/windows/port.c:151-170。
2. **未初始化调用**：在未成功 `init` 的对象上调用 `signal` / `drain` / `deinit`
   会读取未初始化的 `waitable.registered_waitset` 与 `signal_handle`，属未定义行为，
   但头文件未声明 `@pre`。

仓库对同类问题（signal / watcher 重复 init）在审计处置中选择"文档免责 +
头文件 `@warning`"（见 P9 F1/F2），本问题应与之一致处置。

#### review 建议

在 `nanosig_port.h` 的 event 文档块补充生命周期约束：

- `@pre`：`signal` / `drain` / `deinit` 前必须已成功 `init`；`deinit` 另需已
  `ns_platform_waitset_remove`（后者已在硬语义条目中）。
- `@warning`：对同一 event 重复 `init` 而未 `deinit` 会泄漏底层 fd / HANDLE；
  需 `init` 一次、`deinit` 一次。

#### 作者建议

（2026-09-21 作者决定：文档保护，不加运行时守卫。）理由：① 与 P9 F1/F2（signal / watcher 重复 init）的处置保持一致；② `ns_platform_event_t` 是 caller-owned 且不要求清零，运行时守卫无法可靠区分"未初始化垃圾句柄"与"已初始化合法句柄"，可靠检测需强制 zero-init 或新增 magic/状态字段，代价与收益不成比例；③ 重复 init 仅在调用方违规时发生，对正常路径无正确性影响。处置方式：仅补 `@warning` + `@pre` 文档。

#### 可重现的失败场景

```c
ns_platform_event_t e;
ns_platform_event_init(&e, "x");   /* 分配 eventfd / pipe / CreateEvent #1 */
ns_platform_event_init(&e, "x");   /* 覆盖句柄，资源 #1 永久泄漏 */
```

`grep -nE "@pre|@warning" include/nanosig/nanosig_port.h`（event 段落 :316-346）
→ 仅硬语义条目，无 init 配对与前置条件声明。

#### 定位
include/nanosig/nanosig_port.h:316-346
platform/linux/port.c:199-219
platform/macos/port.c:240-266
platform/windows/port.c:151-170

---
