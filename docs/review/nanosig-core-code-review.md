---
review_schema: "nanosig-review-v1"
entries:
  - id: "CORE-012"
    module: "nanosig-core"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/nanosig-core-code-review.md#CORE-012"
  - id: "CORE-015"
    module: "nanosig-core"
    status: "closed-fixed"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/nanosig-core-code-review.md#CORE-015"
  - id: "CORE-016"
    module: "nanosig-core"
    status: "open"
    severity: "medium"
    type: "bug"
    section: "open"
    location: "docs/review/nanosig-core-code-review.md#CORE-016"
    locations:
      - "src/nanosig.c:65"
      - "src/nanosig.c:423"
      - "src/nanosig.c:442"
      - "src/nanosig.c:444"
      - "include/nanosig/nanosig_loop.h:42"
      - "include/nanosig/nanosig_signal.h:363"
  - id: "CORE-017"
    module: "nanosig-core"
    status: "open"
    severity: "medium"
    type: "bug"
    section: "open"
    location: "docs/review/nanosig-core-code-review.md#CORE-017"
    locations:
      - "src/nanosig.c:130"
      - "src/nanosig.c:140"
      - "src/nanosig.c:156"
      - "src/nanosig.c:159"
      - "include/nanosig/nanosig_loop.h:74"
  - id: "CORE-001"
    module: "nanosig-core"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/nanosig-core-code-review.md#CORE-001"
  - id: "CORE-002"
    module: "nanosig-core"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/nanosig-core-code-review.md#CORE-002"
  - id: "CORE-003"
    module: "nanosig-core"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/nanosig-core-code-review.md#CORE-003"
  - id: "CORE-004"
    module: "nanosig-core"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/nanosig-core-code-review.md#CORE-004"
  - id: "CORE-005"
    module: "nanosig-core"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/nanosig-core-code-review.md#CORE-005"
  - id: "CORE-006"
    module: "nanosig-core"
    status: "closed-rejected"
    severity: "medium"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/nanosig-core-code-review.md#CORE-006"
  - id: "CORE-007"
    module: "nanosig-core"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/nanosig-core-code-review.md#CORE-007"
  - id: "CORE-008"
    module: "nanosig-core"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/nanosig-core-code-review.md#CORE-008"
  - id: "CORE-009"
    module: "nanosig-core"
    status: "closed-rejected"
    severity: "low"
    type: "bug"
    section: "closed"
    closed_date: "2026-07-04"
    location: "docs/review/nanosig-core-code-review.md#CORE-009"
  - id: "CORE-013"
    module: "nanosig-core"
    status: "closed-fixed"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-19"
    location: "docs/review/nanosig-core-code-review.md#CORE-013"
    locations:
      - "src/nanosig.c:296"
      - "src/nanosig.c:307"
      - "src/nanosig.c:281"
      - "include/nanosig/nanosig_loop.h:140"
      - "test/unit/test_loop.c:397"
  - id: "CORE-014"
    module: "nanosig-core"
    status: "closed-rejected"
    severity: "high"
    type: "bug"
    section: "closed"
    closed_date: "2026-10-18"
    location: "docs/review/nanosig-core-code-review.md#CORE-014"
    locations:
      - "src/nanosig.c:184"
      - "src/nanosig.c:190"
      - "src/nanosig.c:218"
      - "src/nanosig.c:220"
      - "include/nanosig/nanosig_loop.h:97"
      - "include/nanosig/nanosig_loop.h:133"
  - id: "CORE-018"
    module: "nanosig-core"
    status: "open"
    severity: "high"
    type: "bug"
    section: "open"
    location: "docs/review/nanosig-core-code-review.md#CORE-018"
    locations:
      - "src/nanosig.c:185"
      - "src/nanosig.c:190"
      - "src/nanosig.c:272"
      - "platform/linux/port.c:128"
      - "platform/linux/port.c:141"
      - "test/unit/test_loop.c:177"
---

# Nanosig Core 代码审查报告

**日期**: 2026-07-04
**审查者**: claude-code-review

---

## 修复历史

(无)

---

## 现在打开的问题

<a id="CORE-018"></a>
### CORE-018: `ns_loop_quit`（他线程）与 `ns_loop_destroy` 之间无 happens-before 边：wakeup 关闭与信号并发，TSAN 报 data race

- **状态**: 打开
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述

跨线程 `ns_loop_quit` 与 loop 线程内的 `ns_loop_destroy` 之间没有任何同步边：

- `ns_loop_quit` (src/nanosig.c:272) → `ns_platform_wakeup_signal` → `write(eventfd)`
- `ns_loop_destroy` (src/nanosig.c:185/190) → `ns_platform_wakeup_deinit` → `close(eventfd)` + `ns_platform_free(loop)`

`ns_loop_quit` 文档标注为 MPM-safe，而 `ns_loop_destroy` 的 `@pre` 只禁止与 `ns_loop_create`/自身并发，**没有**禁止与 `ns_loop_quit` 并发；实际顺序上 quit 的 write 先于 close（eventfd 唤醒由内核保证），但 C 内存模型下没有 happens-before 边，`free(loop)` 对堆块的写与主线程读 `loop->wakeup` 的读构成真实数据竞争（TSAN 报 `ns_platform_free` 与 `ns_platform_wakeup_signal` 竞争，close/write 在 TSAN 内部 fd 状态表上亦报竞争）。

后果：`nightly` 的 `linux-tsan` job 退出码 66（TSAN 报 race 的默认退出码），ctest 判定测试失败——这是 master nightly 自 2026-10-18 起持续失败的**另一个**独立原因（run 39106875930 只有这一条 data race，无自 join 崩溃）。

#### review 建议

二选一，需要作者裁决：

A. **实现层补同步边（推荐）**：在 `ns_loop_t` 中加入保护 wakeup 生命周期的锁。`ns_loop_quit` 加锁 → 读 fd → write → 解锁；`ns_loop_destroy` 加锁 → close → 解锁 → free。锁的获取/释放天然提供 happens-before 边，同时消除 free 与并发读之间的 C 级数据竞争；`ns_loop_quit` 仍是 MPM-safe（只是多一次无竞争的加解锁）。

B. **收紧契约**：在 `nanosig_loop.h` 明确 “`ns_loop_destroy` 不得与 `ns_loop_quit` 并发”，并修改 `test_cross_thread_quit_and_ownership`（test/unit/test_loop.c:177）——由主线程在 join worker 之后再 destroy，避免测试自身制造无同步的并发访问。

注意本条与已关闭的 CORE-001 不是同一问题：CORE-001 是 “run 与 deinit 并发”，本条是 “quit 与 destroy 并发”，且 quit 被文档标注为 MPM-safe。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

1. `cmake --preset linux-debug-tsan && cmake --build --preset linux-debug-tsan`
2. `ctest --preset linux-debug-tsan -R nanosig_test_loop`
3. 观察：TSAN 报 2 条 data race：
   - `SUMMARY: data race platform/linux/port.c:128 in ns_platform_wakeup_deinit`（close 与 write 在 eventfd 上竞争）
   - `SUMMARY: data race platform/linux/port.c:90 in ns_platform_free`（free(loop) 与主线程读 loop->wakeup 竞争）
   栈顶分别是 `ns_loop_destroy` (src/nanosig.c:185/190) ← `loop_thread_entry` (test/unit/test_loop.c:145) 与 `ns_loop_quit` (src/nanosig.c:272) ← `test_cross_thread_quit_and_ownership` (test/unit/test_loop.c:177)。
4. 测试进程退出码 66，ctest 判定失败。

#### 定位
src/nanosig.c:185
src/nanosig.c:190
src/nanosig.c:272
platform/linux/port.c:128
platform/linux/port.c:141
test/unit/test_loop.c:177

<a id="CORE-016"></a>
### CORE-016: 超出 ring 半容量的 payload 被 emit 报成 `NS_E_INVAL`，该上限未在任何公开文档中说明

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述
`ns_signal_emit_raw`（src/nanosig.c:415-453）只校验 `payload_size == signal->payload_size`（src/nanosig.c:423）与 payload 非空，随后把记录交给 `ns_mpsc_record_ring_try_pushv`。record ring 的单条记录上限是 `capacity / 2`，超限时 `try_pushv` 返回 `NS_E_INVAL`，emit 原样透传（src/nanosig.c:442-446）。对调用方而言这是"合法入参被判为非法"：

- 默认配置 `NS_LOOP_CONFIG_DEFAULT()` 的 ring 为 1024 字节，单条记录上限 512，减去 `sizeof(ns_slot_call_t)`（24）后 **payload 实际上限只有 480 字节**。payload 从 484 字节起，每次 emit 恒返回 `NS_E_INVAL`——与队列是否为空无关，也不是文档化的 `NS_E_QUEUE_FULL`。
- 同一根因还有一个更极端的形态：`ns_loop_config_validate`（src/nanosig.c:65-72）只要求容量为 2 的幂，`queue_byte_capacity = 32` 时 `ns_loop_init` 返回 `NS_OK`，但该 loop 上任何 emit 都无法成功（连"无 payload"记录的 8 + 24 字节都超过上限），loop 永远执行不了一次 slot 调用。

公开头文件都未说明这个约束：include/nanosig/nanosig_loop.h:42 只说 `queue_byte_capacity` 决定"跨线程 emit 可排队的总字节数"，include/nanosig/nanosig_signal.h:350-363 只说 payload_size 必须与 signal 一致、emit 路径不允许分配内存。调用方只能读 MPSC 实现或跑起来试错才能得到该限制。

#### review 建议
1. 把"单条记录超过 `capacity/2`"（配置/契约问题，重试无用）与"队列暂时放不下"（瞬时容量问题，可重试）区分表达，不要复用 `NS_E_INVAL`；或
2. 在 `nanosig_loop.h` / `nanosig_signal.h` 明确写出"单条 emit 的有效载荷上限 = `queue_byte_capacity / 2 - 记录头 - sizeof(ns_slot_call_t)`，超出返回 `NS_E_INVAL`"；并
3. 给 `ns_loop_config_validate` 加容量下限（至少能容纳一条最小 slot 调用记录），使 `queue_byte_capacity = 32` 这类永远无法工作的配置在 `ns_loop_init` 阶段就返回 `NS_E_INVAL`。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
typedef struct p500 { unsigned char b[500]; } p500_t;   /* payload_size = 500 */
ns_loop_init(&loop, NULL);                              /* 默认 1024 字节 ring */
ns_signal_init(&sig, p500_t);
ns_signal_connect_typed(sig, slot, p500_t, loop, NULL, &conn);
rc = ns_signal_emit_raw(&sig, &pl, sizeof(pl));         /* 队列为空 */
```
实测：`rc = -3 (NS_E_INVAL)`。同环境逐点扫描（队列始终为空）：
```
payload= 480 -> rc= 0 NS_OK
payload= 484 -> rc=-3 NS_E_INVAL
```
容量下限的实测：
```
capacity=32     loop_init OK, emit(no payload) rc=-3   <-- 该 loop 永远无法执行任何 slot
capacity=64     loop_init OK, emit(no payload) rc=0
capacity=1/16   loop_init rc=-3
```

#### 定位
src/nanosig.c:65
src/nanosig.c:423
src/nanosig.c:442
src/nanosig.c:444
include/nanosig/nanosig_loop.h:42
include/nanosig/nanosig_signal.h:363

---

<a id="CORE-017"></a>
### CORE-017: `ns_loop_init(out_loop == NULL)` 被文档允许，但会静默泄漏 loop 与 wakeup 句柄

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述
`ns_loop_init`（src/nanosig.c:122-165）在 `out_loop == NULL` 时于第 130 行只把入参置空便继续执行，完成 `ns_platform_alloc`（src/nanosig.c:140）与 `ns_platform_wakeup_init`（src/nanosig.c:156）后，在第 159 行跳过回传，返回 `NS_OK`。调用方拿不到任何句柄，**没有任何路径可以释放**这块内存与 wakeup 句柄（`ns_loop_deinit` 需要 loop 指针）。每次这样的调用泄漏一份 loop 结构体 + 内嵌 MPSC ring 存储 + 一个 OS 句柄（Linux eventfd / macOS kqueue / Windows Event）。

include/nanosig/nanosig_loop.h:74 明确文档化"输出创建得到的 loop 句柄；可为 `NULL`，此时不向外传值"，因此这是被公开文档允许的调用，不是调用方违约（不属于合同前提保护）。

#### review 建议
在入口把 `out_loop == NULL` 当作参数错误拒绝（返回 `NS_E_INVAL`，与 `ns_loop_run` / `ns_loop_deinit` 等直接返回 `NS_E_INVAL` 的校验风格一致），或删除该参数"可为 NULL"的契约描述。不应保留一条会分配资源却无法回收的路径。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
ns_init();
for(i = 0; i < 20; i++) rc = ns_loop_init(NULL, NULL);   /* 每次 rc == NS_OK */
```
实测（macOS，统计 /dev/fd 条目数）：
```
fds before=7 after=27 delta=20 (20 calls with out_loop=NULL)
```
即 20 次调用泄漏 20 个 OS 句柄，另有 20 块已分配内存无从释放。

#### 定位
src/nanosig.c:130
src/nanosig.c:140
src/nanosig.c:156
src/nanosig.c:159
include/nanosig/nanosig_loop.h:74

---

## 现在关闭的问题

<a id="CORE-015"></a>
### CORE-015: slot 收到的 payload 指针只保证 8 字节对齐，`_Alignof > 8` 的 payload 类型触发未对齐访问

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **类型**: bug
- **关闭原因**: commit `7326c27` 把对齐超限从运行期 UB 提前到编译期报错：新增 `NS_SIGNAL_PAYLOAD_ALIGN_MAX`（= `sizeof(size_t)`）与 `NS_SIGNAL_PAYLOAD_ALIGN_CHECK`，`ns_signal_init`、`ns_signal_connect_typed` 均带该断言；`src/nanosig.c:50-53` 增加静态断言，保证记录起点与 payload 偏移都满足上限；底层 `ns_signal_init_raw` 的调用方责任已在头文件 `@note` 文档化。需要更高对齐时改为传指针。
- **关闭日期**: 2026-10-19

---

<a id="CORE-012"></a>
### CORE-012: `ns_loop_quit` 在 loop 被 `ns_loop_deinit` 释放后写入 → heap-use-after-free `bug`

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **类型**: bug
- **关闭原因**: 合同前提保护：`nanosig_loop.h` 中 `ns_loop_deinit` 已明确文档化"deinit 前必须确保 loop 未运行"。在 `ns_loop_deinit` 后调用 `ns_loop_quit` 属调用方违反文档化前置条件，行为未定义，不属库缺陷。正常使用中不会发生此情况。
- **关闭日期**: 2026-10-19

<a id="CORE-001"></a>
### CORE-001: `ns_loop_deinit` 与同步 `ns_loop_run` 并发可造成 UAF

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 关闭原因
文档化合同违规。`nanosig_loop.h` 明确要求 deinit 前必须确保 loop 未运行；`ns_loop_deinit` 已通过 `running` 原子标志检查拦截。提案的引用计数方案属于过度设计——文档化的"run/deinit 不能并发调用"约束已足够。

#### 关闭日期
2026-07-04

---

<a id="CORE-002"></a>
### CORE-002: `ns_signal_disconnect` 在 signal 销毁后可解引用悬空 mutex

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 关闭原因
文档化合同违规。`nanosig_signal.h` 明确要求 connection 生命周期 ≤ signal 生命周期（先 disconnect 再 deinit）。`ns_signal_lock` 已检查 `signal->mutex == NULL` 并返回 `NS_E_INVAL` 作为软失败模式，提供了安全防护。

#### 关闭日期
2026-07-04

---

<a id="CORE-003"></a>
### CORE-003: `ns_signal_emit_raw` 在持锁时执行阻塞性 `wakeup_signal`

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高

#### 关闭原因
设计约束。eventfd/Windows event 的 level-triggered wakeup_signal 是 O(1) 非阻塞 syscall（一次 write 系统调用），不构成 critical section 延长。提案的批处理 + 延迟 wakeup 需要临时内存分配，违反 emit 路径无分配约束（`nanosig_signal.h` 明确声明）。批处理 wakeup 是调用方的优化关注点。

#### 关闭日期
2026-07-04

---

<a id="CORE-004"></a>
### CORE-004: Partial-broadcast 没有原子语义保证

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
文档化约束。`nanosig.c` 函数注释明确记录此约束：第 N 个 push 失败时前 N-1 个已成功入队无法回滚。提案的全或无语义需要两阶段提交（commit token）或 dry-run reserve，与非阻塞 API 设计冲突。调用方应在 emit 前检查队列容量。

#### 关闭日期
2026-07-04

---

<a id="CORE-005"></a>
### CORE-005: `quit_requested` 重置时的 wakeup_signal 可能在边沿触发下丢失

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
文档化设计约束。`nanosig.c:213-221` 注释明确标记此为"已知设计约束"，并指出"单线程驱动 loop 的使用模式不受影响"。提案（reset 移到 run 入口）无法根本解决非原子检查-修改序列的固有竞态窗口。文档要求 wakeup 必须保持 level-triggered。

#### 关闭日期
2026-07-04

---

<a id="CORE-006"></a>
### CORE-006: `ns_loop_stop` 后未 drain in-flight slot 调用

- **状态**: 关闭-已拒绝
- **严重度**: 🟡 中

#### 关闭原因
文档化行为。`nanosig_loop.h` 明确 `ns_loop_stop` 不保证 slot_fn 完成（只 join 循环主线程）。loop 线程是唯一执行 slot_fn 的线程，thread_join 后无 in-flight 调用。若调用方需确保 slot_fn 完成，需自行同步（如 dispatch 后再 join）。提案的 `ns_loop_drain_and_stop` 增加了 API 表面而无新能力。

#### 关闭日期
2026-07-04

---

<a id="CORE-007"></a>
### CORE-007: `ns_signal_disconnect_all` 唤醒释放的 connections

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
文档化行为。`nanosig_signal.h:330-344` 明确描述 `ns_signal_disconnect_all` 为 teardown / 兜底接口，调用方负责后续 connection 的处理。header 已记录 disconnect_all 后 connection 的 signal 引用应视为无效。

#### 关闭日期
2026-07-04

---

<a id="CORE-008"></a>
### CORE-008: `ns_signal_emit_raw` 跨多个目标循环的部分广播无法 rollback

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
重复问题（与 CORE-004 同源）。同样的无回滚设计约束。提案的"取消标记"机制违反 emit 路径无分配约束。文档已记录此行为，调用方需自行处理部分广播后果。

#### 关闭日期
2026-07-04

---

<a id="CORE-009"></a>
### CORE-009: `ns_loop_quit` 在非运行状态下仍可调用

- **状态**: 关闭-已拒绝
- **严重度**: 🟢 较低

#### 关闭原因
设计意图。`ns_loop_quit` 的预设置行为对于"优雅退出"场景有用（loop 启动前预先 set quit_requested）。审查本身也承认"可以不修复"。函数幂等且无副作用，重复调用安全。属于文档问题，非代码缺陷。

#### 关闭日期
2026-07-04

<a id="CORE-013"></a>
### CORE-013: `ns_loop_stop` 在 join 失败时仍清除 `async_started`，旧 worker 与 loop 生命周期脱钩

- **状态**: 关闭-已修复
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述
`ns_loop_stop`（src/nanosig.c:296-308）先 `ns_loop_quit`，再 `ns_platform_thread_join`，然后**无条件**把 `loop->async_started` 清零（src/nanosig.c:305）。平台层 `ns_platform_thread_join` 在 `pthread_join` 返回非 0 时一律返回 `NS_E_INVAL`（platform/macos/port.c:370、platform/linux/port.c:325），最典型的触发场景就是**在 loop 自身线程（slot 回调内）调用 stop**——`pthread_join` 自 join 返回 `EDEADLK`。

此时 `ns_loop_stop` 返回错误，但 `async_started` 已经被清掉，两个守卫同时失效：
- `ns_loop_start`（src/nanosig.c:281）的 `NS_E_BUSY` 守卫失效 → 旧 worker 线程仍存活时可以再次 start，同一 loop 上出现两个线程并发执行 `ns_loop_run_impl`（src/nanosig.c:206）的消费侧逻辑，共享同一个 MPSC ring 的单一消费者位置。
- `ns_loop_deinit`（src/nanosig.c:172）的 `NS_E_BUSY` 守卫同样失效 → 只要 `running` 在旧 worker 处于 slot 内部时读到 0，deinit 就会 `ns_platform_free(loop)` 并关闭 wakeup 句柄（src/nanosig.c:175-180），而旧 worker 线程仍在运行并会继续访问 `loop->queue` / `loop->wakeup`。

`nanosig_loop.h` 只声明了 start/stop 必须串行化（include/nanosig/nanosig_loop.h:8、include/nanosig/nanosig_loop.h:138），未声明"禁止在 loop 线程或 slot 回调内调用 `ns_loop_stop`"；slot 内自停是自然写法，属未文档化约束下的可达路径。

#### 作者建议
按 review 建议修复：只在 join 成功时清 `async_started`；并在 `ns_loop_stop` 的 docstring 补 `@pre`（不得在 loop 自身线程调用；失败时须由其他线程再次 stop），把该约束从"未文档化"变为文档化契约。
→ 关闭-已修复

#### 关闭原因
`src/nanosig.c:307` 改为 `if(rc == NS_OK) loop->async_started = 0;`；`include/nanosig/nanosig_loop.h:140` 补 `@pre`。
新增回归用例 `test/unit/test_loop.c:397`（`test_async_stop_in_slot`，`#if !defined(_WIN32)` 守卫：Windows 的 join 是 `WaitForSingleObject` 自身句柄，自 join 会永久阻塞而非返回错误）：slot 内自停返回错误后，断言 `ns_loop_start` / `ns_loop_deinit` 均返回 `NS_E_BUSY`，再由主线程 stop 得到 `NS_OK`。去掉守卫后该用例在 `ns_loop_start(loop) == NS_E_BUSY` 处失败。

复现口径修正：实测（macOS release）确认的是 start 守卫失效（自停 rc=-3，随后 `ns_loop_start` 返回 0）；场景 2 的 deinit 在"worker 仍处于 slot 内"时被 `running` 守卫拦下（rc=-3），该路径未被本次复现观测到。两条路径同根因，修复后均不再成立。

- **关闭日期**: 2026-10-19
- **状态**: 关闭-已修复

#### 可重现的失败场景
slot 内调用 `ns_loop_stop`（返回错误），随后主线程 `ns_loop_start` 返回 `NS_OK` 而旧 worker 仍存活。实测输出（macOS release）：
```
1) ns_loop_stop() called from the loop's own thread -> rc=-3 (NS_E_INVAL=-3)
2) worker thread is still alive & inside the slot: 1
3) ns_loop_start() while the old worker is still alive -> rc=0 (NS_OK=0)
```

#### 定位
src/nanosig.c:296
src/nanosig.c:307
src/nanosig.c:281
include/nanosig/nanosig_loop.h:140
test/unit/test_loop.c:397

<a id="CORE-014"></a>
### CORE-014: slot 内重入 emit 使 loop 永远观察不到 `quit_requested`，`ns_loop_run` / `ns_loop_stop` 永久阻塞

- **状态**: 关闭-已拒绝
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述
`ns_loop_run_impl`（src/nanosig.c:217-226）的循环体是"先把队列**排空**，再检查 `quit_requested`"，而 `ns_loop_dispatch_pending`（src/nanosig.c:184-204）是 `for(;;)`，只有 `ns_mpsc_record_ring_try_acquire` 返回非 `NS_OK` 才 break。只要一次 dispatch 过程中队列始终非空，退出检查（src/nanosig.c:220）就永远轮不到。

slot 回调内 `ns_signal_emit` 到同一 loop 会持续往队列补记录，因此在该边界上队列从不空：`ns_loop_quit`（含 `ns_loop_stop` 内部调用，src/nanosig.c:303）设置标志并 signal wakeup 被完全忽略。

这与公开文档承诺冲突：include/nanosig/nanosig_loop.h:97 写明"持续等待并分发队列中的 slot 调用，**直到收到 `ns_loop_quit()` 请求**"，include/nanosig/nanosig_loop.h:133-136 承诺 `ns_loop_stop` 会 join 返回。实测两者都不成立——`ns_loop_run` 永不返回，`ns_loop_stop` 卡死在 join（无超时、无恢复路径）。计划文档记录的"dispatch_pending 后只检查一次 quit_requested"是该实现的来源，但对外头文件的承诺并未随之收窄。

#### review 建议
在批量分发之间周期性检查 `quit_requested`（例如每处理 N 条记录或每轮 acquire 成功后检查一次原子标志），使"持续事件源存在时 quit 仍然生效"。
若有意接受当前语义，则必须收窄文档：明确写"quit 只在队列出现空档时生效；slot 内无限重入 emit 会导致 loop 无法退出"，由作者确认取舍。

#### 作者建议
（待作者补充）

#### 可重现的失败场景
```c
static ns_signal_t g_sig;
static void slot_chain(void *ud, const ns_no_payload_t *p){
    (void)ud; (void)p;
    (void)ns_signal_emit(g_sig, NS_NO_PAYLOAD);   /* 自喂，队列永不空 */
}
ns_signal_emit(g_sig, NS_NO_PAYLOAD);             /* 播种一条记录 */
pthread_create(&t, NULL, quitter, NULL);          /* 300ms 后调用 ns_loop_quit(g_loop) */
rc = ns_loop_run(g_loop);                         /* 永不返回 */
```
实测输出（5 秒后进程仍存活，手工 kill）：
```
main: entering synchronous ns_loop_run
quitter: ns_loop_quit -> 0
>>> ns_loop_run NEVER returned despite ns_loop_quit from another thread
```
异步模式同样复现：`ns_loop_start` + slot 自喂后，主线程调用 `ns_loop_stop(loop)`，4 秒后仍未返回。

#### 定位
src/nanosig.c:184
src/nanosig.c:190
src/nanosig.c:218
src/nanosig.c:220
include/nanosig/nanosig_loop.h:97
include/nanosig/nanosig_loop.h:133

#### 关闭原因

作者仲裁：判定不构成需要修复的库缺陷——只有当调用方让事件源永不中断时才会触发，
而"队列空档处生效"的 quit 语义是已知且可接受的。
保留口径记录：include/nanosig/nanosig_loop.h:97 的表述（"直到收到 ns_loop_quit 请求"）
与现状存在语义差距，如需可另开 doc 条目收窄。

- **关闭日期**: 2026-10-18
- **状态**: 关闭-已拒绝
