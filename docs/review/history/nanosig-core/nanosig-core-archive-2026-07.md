# nanosig-core review 归档 2026-07

<a id="CORE-010"></a>
### CORE-010: `ns_loop_deinit` 在 `ns_platform_wakeup_destroy` 失败时泄漏 loop 内存

- **状态**: 关闭-已修复
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述
`ns_loop_deinit` 在 `ns_platform_wakeup_destroy` 返回错误时提前返回（第 162 行），未调用 `ns_platform_free(loop)`。这导致 loop 结构体及其内嵌的 MPSC ring 存储区域泄漏。调用方无法重试——wakeup 句柄可能处于未定义状态，而 loop 指针指向的内存已不可恢复。

#### review 建议
无论 `ns_platform_wakeup_destroy` 是否成功，都应释放 loop 内存：
```c
rc = ns_platform_wakeup_destroy(loop->wakeup);
ns_platform_free(loop);
return rc;
```

#### 作者建议
当前代码（`src/nanosig.c:171-177`）已实现 review 建议——无论 wakeup_destroy 是否失败，都打印日志后 fall through 到 `ns_platform_free(loop)`。问题已修复，review 文档未同步更新。→ 关闭-已修复

#### 关闭原因
代码已修复：`src/nanosig.c:172-177` 在 wakeup_destroy 失败时先记日志，然后始终执行 `ns_platform_free(loop)`。

- **关闭日期**: 2026-07-18
- **状态**: 关闭-已修复

#### 可重现的失败场景
1. 调用 `ns_loop_init(&loop, NULL)` 成功创建 loop。
2. 平台层 `ns_platform_wakeup_destroy` 因某种原因返回错误。
3. `ns_loop_deinit(loop)` 返回错误码，但 `loop` 指向的内存未被释放。
4. 调用方丢弃 loop 指针，内存永久泄漏。

#### 定位
src/nanosig.c:161-165

---
