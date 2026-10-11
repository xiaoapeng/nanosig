# Zephyr Backend 代码审查报告

**日期**: 2026-10-11
**审查者**: opencode-review
**范围**: platform/zephyr/port.c、test/zephyr/*、zephyr/{module.yml,CMakeLists.txt,Kconfig}、include/nanosig/nanosig_port.h 中的 Zephyr 分支，以及相关文档（platform/README.md、docs/ARCHITECTURE.md 第 7 节、docs/plans/子计划-Zephyr平台后端接入.md）。
**规范**: docs/review/AGENTS.md
**验证**: 本机未运行 Zephyr native_sim（需 west 工作区），结论基于源码 ↔ 公开契约（nanosig_port.h / platform/README.md）↔ 已批准计划（子计划-Zephyr平台后端接入.md / 深度访谈规格）三方比对；运行证据引用 CI `zephyr` job（`ci.yml:72-106`，最近一次 pull_request run 38120360058 为 success）。
**基线**: master 111b5a3

---

## 修复历史

（暂无。）

---

## 现在打开的问题

### ZEPHYR-001: 空 waitset + 无限超时的语义未定义（五个后端 3 永久阻塞 / 2 报错），需统一为有界行为

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: design（含 doc 契约缺口）

#### 问题描述

`ns_platform_waitset_wait` 在"无可等待 slot"（空 waitset，或全部 slot 句柄为空）时，对 `timeout_us == NS_PLATFORM_WAIT_INFINITE_US` 直接返回 `NS_E_INVAL`（port.c:531），只对有限超时走 `k_sleep` 睡眠路径（port.c:532）。

公开契约把无限超时定义为"无限等待"（nanosig_port.h:604、platform/README.md:161），但**没有**规定空 waitset 的语义。五个后端在该输入下并不统一，且除 Windows 外都是底层原语的副作用而非显式设计：

| 后端 | timeout = 0 | 有限 timeout | INFINITE |
|------|-------------|--------------|----------|
| Linux | 立即返回 NS_OK/0（`epoll_wait(timeout=0)`） | 睡眠到点 → NS_OK/0 | `timeout_ms = -1` → 永久阻塞（linux/port.c:562；timerfd 常驻 epoll 但未 arm，永不就绪） |
| macOS | kevent timeout 0 → NS_OK/0 | kevent 睡眠 → NS_OK/0 | `timeout_ptr == NULL` → 永久阻塞（macos/port.c:622-632） |
| Windows | 显式提前返回 NS_OK/0（windows/port.c:449） | armed timer → NS_OK/0 | `WaitForMultipleObjects(0, …)` → `WAIT_FAILED`(ERROR_INVALID_PARAMETER) → NS_E_INVAL（windows/port.c:476、504） |
| FreeRTOS | select(0) → NS_OK/0 | select(ticks) → NS_OK/0 | select(`portMAX_DELAY`) → 永久阻塞（freertos/port.c:646） |
| Zephyr | NS_OK/0 | `k_sleep` → NS_OK/0 | NS_E_INVAL |

即 3 个后端永久阻塞（Linux/macOS/FreeRTOS）、2 个后端报错（Windows/Zephyr）。因此这是**契约缺口**而非单纯的 Zephyr 偏差："空 waitset + 无限超时"目前没有可预期行为。`port.c:527-528` 的注释只描述了有限超时路径；该例外未出现在 nanosig_port.h、platform/README.md 或已批准计划中（计划 Z8 只写 `NS_PLATFORM_WAIT_INFINITE_US → K_FOREVER`）。按 §2.4 文档仲裁，该行为既非公共头文档化的设计，也不是计划记录的取舍。

#### review 建议

建议把语义定死为"**空 waitset + `NS_PLATFORM_WAIT_INFINITE_US` 是错误，返回 `NS_E_EMPTY`**"，并在契约层统一五个后端——当前 Zephyr 的**方向**正确，但错误码不精确、契约缺失。有限超时/0 的行为（睡眠或立即返回、`out_count == 0`）保持不变并写进 docstring。

理由：

1. 空 waitset 上不存在任何潜在唤醒源；且 `k_poll` 在阻塞前用活跃 slot **快照**构造 `k_poll_event[]`（port.c:511-525），阻塞期间由另一线程 `waitset_add` 加入的成员对该次 wait **不可见**——"永久阻塞等后续注册"在本后端根本不成立，其唯一含义是"永久挂起且不可唤醒"。
2. 静默挂死是 RTOS 上最难诊断的失效模式（无错误码、无日志，与优先级反转/丢中断无法区分）；立即返回错误码可诊断。
3. 平台原生语义也支持"拒绝空等待集"：Windows 的 `WaitForMultipleObjects` 要求 nCount ∈ [1,64]，空集直接 ERROR_INVALID_PARAMETER——"报错"并非 Zephyr 独创，Windows 后端实际上已如此。
4. 若确需"阻塞等外部唤醒"，nanosig 已有专用原语 `ns_platform_event_t` / `ns_platform_wakeup_t`（nanosig_port.h:395 明确"唯一阻塞单等待原语是 `ns_platform_wakeup_t`"），broker 自身就把 `broker->event` 常驻注册进 waitset（src/ns_broker.c:700），因此 in-tree 永远不会走到空集合路径——该路径只能由直接调用平台 API 的调用方触发。

具体落地（跨模块，建议同步记入 `docs/review/global-review.md`）：

- (`ZEPHYR-001-a`) 用 `NS_E_EMPTY`（nanosig_status.h:30）替代 `NS_E_INVAL`：`NS_E_INVAL` 的契约语义是"NULL 指针、类型错配"（nanosig_status.h:17），而 `NS_E_EMPTY` 在本仓库既有语义就是"集合/队列为空"（nanosig_hashtbl.h:215、nanosig_mpsc_record_ring.h:193、src/ns_broker.c:293），既精确又让调用方能区分"参数写错"与"集合为空"。
- (`ZEPHYR-001-b`) 在 `ns_platform_waitset_wait` docstring（nanosig_port.h:604-612）补"空 waitset"小节，写明 0 / 有限 / INFINITE 三种 timeout 的行为与返回码，并同步 platform/README.md:161。
- (`ZEPHYR-001-c`) 其余四个后端各加显式判断（Windows 把现已"偶然"落到错误码的路径改为显式处理；Linux 补上显式守卫；macOS/FreeRTOS 把"永久阻塞"改为可预期返回），使行为可预期，而非取决于各底层原语的偶然语义。
- (`ZEPHYR-001-d`) 五个后端各补契约测试：空 waitset + 0 → `NS_OK` 且 `out_count == 0`；空 waitset + 有限 timeout → `NS_OK`、`out_count == 0` 且实测等待 ≥ timeout；空 waitset + INFINITE → `NS_E_EMPTY`。
- (`ZEPHYR-001-e`) **第三选项（对调用方最无感）**：空 waitset + INFINITE 不返回错误，而是**封顶为最大等待值**（如 10 秒）后按普通超时返回 `NS_OK`。代价是把"静默永久挂死"换成"每 10 秒静默空转一次"，可诊断性弱于错误码，但调用方无需改动、五后端天然一致、且不需要动 Windows 的 `WaitForMultipleObjects(0,…)` 失败路径。**作者采纳本选项**，见"作者建议"。

（作者采纳 (e) 后，(a)/(c) 的 `NS_E_EMPTY` 改为统一的封顶语义，(d) 的 INFINITE 断言改为"在封顶值附近返回 `NS_OK` 且 `out_count == 0`"，(b) 的契约文档小节照做。）

备选方案（若作者倾向"与 POSIX poll/epoll/kevent 一致、空集合永久阻塞"）：Zephyr 侧改用 `k_sleep(K_FOREVER)`（Zephyr 文档：K_FOREVER 使线程挂起，仅 `k_wakeup()` 可唤醒，而 nanosig 未暴露该路径），并在 docstring 写明"空 waitset + INFINITE 永久阻塞、不返回"；但该方案反而需要改 Windows（把 `WaitForMultipleObjects(0,…)` 的失败路径改成真正的永久等待），且要把"永久不可唤醒"的后果写进契约。

#### 作者建议

（2026-10-11 作者决定）不返回错误码、也不永久阻塞：空 waitset + 无限超时统一**封顶为最大等待值**为 **1 秒**；到期后按普通超时返回 `NS_OK`（`out_count == 0`）。封顶值作为公开常量 `NS_PLATFORM_WAIT_EMPTY_MAX_US`（默认 1000000 µs，可编译期覆盖），五个后端行为一致；**waitable 非空时仍是真无限等待**（broker 稳态依赖这一点），有限超时不受影响。

封顶值由 10 秒调整为 **1 秒**（2026-10-11，作者要求让 CI 跑更快）：只影响"误用空 waitset 无限等待"这一路径，1 秒仍是有效防御，同时避免 host / FreeRTOS / Zephyr 契约测试每次都实等近 10 秒。

按此决定已实现（工作区，未提交）：
include/nanosig/nanosig_port.h
platform/linux/port.c
platform/macos/port.c
platform/windows/port.c
platform/freertos/port.c
platform/zephyr/port.c
platform/README.md
test/unit/test_platform_backend.c
test/unit/test_platform_contract_compile.c
test/zephyr/src/main.c
test/freertos/test_freertos_waitset.c

跨模块的同一改动记录在 docs/review/global-review.md（GLOBAL-004）。提交后本条移入"修复历史"。

#### 可重现的失败场景

```c
ns_platform_waitset_t *ws = NULL;
ns_platform_waitset_completion_t c[4];
size_t n = 0u;

ns_platform_waitset_create(&ws);            /* 不 add 任何 waitable */
ns_platform_waitset_wait(ws, NS_PLATFORM_WAIT_INFINITE_US, c, 4u, &n);
```

观察到：Zephyr / Windows 立即返回 `NS_E_INVAL`、`n == 0`；Linux / macOS / FreeRTOS 进入阻塞（不返回）。

#### 定位

platform/zephyr/port.c:529-534
include/nanosig/nanosig_port.h:604
platform/README.md:161
platform/linux/port.c:562
platform/windows/port.c:449
platform/windows/port.c:476
platform/macos/port.c:622-632
platform/freertos/port.c:646

（以上行号对应审查基线 master 111b5a3；修复已落地于工作区，见"作者建议"。）

---

### ZEPHYR-002: `waitset_add` 只判 NULL，未排除 `(void *)~0` 无效哨兵，会把哨兵交给 `k_poll`

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: bug

#### 问题描述

`ns_platform_waitset_add` 用 `waitable->primitive.handle == NULL` 判定句柄有效性（port.c:456）。但 `ns_waitable_init` 把无效句柄初始化为 **`(void *)~0` 哨兵**，而不是 NULL（nanosig_port.h:144-158），头文件为此专门提供 `ns_waitable_handle_is_valid` 并显式排除该哨兵（nanosig_port.h:175-182）。

因此"仅 `ns_waitable_init` 过、尚未绑定真实 `k_poll_signal`"的 waitable 会通过 add 校验；随后 `ns_platform_waitset_wait` 取出该句柄时 `sig != NULL`（port.c:518-519），把 `(void *)~0` 交给 `k_poll_event_init`（port.c:521-522），由 `k_poll` 解引用，在 poller 线程内表现为内核异常/内存错误，而不是干净的 `NS_E_INVAL`。

对照：Linux 后端用等价谓词 `waitable->primitive.fd < 0` 拦截同样输入并返回 `NS_E_INVAL`（linux/port.c:486），macOS 亦有同类无效判据（macos/port.c:476）；Zephyr 自己的契约测试也在用 `ns_waitable_handle_is_valid`（test/zephyr/src/main.c:52），唯独后端实现没有用它。

§4.4.5 反例条款适用：即使把"调用方须提供有效句柄"视为前提，违反前提的后果仍是内核异常（非优雅失败），属值得防御性加固的情形。

#### review 建议

把 port.c:456 的判据改为与平台宏一致的形式：

```c
if(!ns_waitable_handle_is_valid(NS_WAITABLE_GET(waitable))) return NS_E_INVAL;
```

使其同时排除 NULL 与 `(void *)~0`。建议一并核对 `ns_platform_event_signal`（port.c:375）与 `ns_platform_event_drain`（port.c:389）的无效句柄判据，保证无效句柄统一返回 `NS_E_INVAL`（不作为事件，而是"无效输入"）。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

```c
ns_platform_waitable_t w;
ns_platform_waitset_t *ws = NULL;

ns_platform_waitset_create(&ws);
ns_waitable_init(&w);                       /* primitive.handle == (void *)~0 */
w.events = NS_WAITABLE_EVENT_IN;
ns_platform_waitset_add(ws, &w);            /* 期望 NS_E_INVAL，实际 NS_OK */
ns_platform_waitset_wait(ws, 0u, c, 4u, &n);/* k_poll 解引用 (void *)~0 */
```

#### 定位

platform/zephyr/port.c:456
platform/zephyr/port.c:518-522
include/nanosig/nanosig_port.h:144-158
include/nanosig/nanosig_port.h:175-182
platform/linux/port.c:486
platform/macos/port.c:476
test/zephyr/src/main.c:52

---

### ZEPHYR-003: 跨线程 signal→wait / drain-只在-poller 的确定性用例缺失（计划 Step 4 与 AC-9 明确要求）

- **状态**: 打开
- **严重度**: 🟡 中
- **类型**: test

#### 问题描述

计划把"`k_poll_signal` 跨线程 reset 丢事件"列为 R2（首位风险），并在 Step 4 明确要求：event 用例"含两线程：poller 线程 reset，用 `k_sem` 显式排序两线程（**禁止依赖 POSIX arch 调度时序**），验证跨线程 signal 后 drain 只在 poller 发生"（子计划:118）；AC-9 也要求 wakeup"每对象 `k_sem`，**跨线程 signal→wait**"有独立断言（子计划:86）。

但 test/zephyr/src/main.c 现状：

- `test_wakeup`（:260-272）在同线程内先 `ns_platform_wakeup_signal` 再 `ns_platform_wakeup_wait`，不是跨线程。
- `test_broker_roundtrip`（:330-356）虽然跨线程（测试线程 signal、broker 线程 drain），但等待用的是 `for(i=0; i<2000; ++i) k_msleep(1)` 轮询，而不是计划要求的 `k_sem` 显式排序——即"验证跨线程 signal 后 drain 只在 poller 发生"这一断言仍依赖调度时序，恰好是 native_sim（确定性单核模拟）最容易掩盖的面。

#### review 建议

新增一个两线程确定性用例：测试线程只负责 `ns_platform_event_signal`，另一线程在 `waitset_wait` 循环内消费并 `event_drain`；用两个 `k_sem`（"已就绪"、"已 drain"）在两个线程间做显式握手代替睡眠轮询，断言（1）跨线程 signal 被 poller 观测到，（2）`k_poll_signal_reset` 只发生在 poller 线程。wakeup 另补一个跨线程 signal→wait 用例。若沿用 `test_broker_roundtrip`，至少把其中的 `k_msleep` 轮询改为 `k_sem`/`k_event` 等待，去掉对调度时序的依赖。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

运行 ztest 套件：跨线程路径仅由 `test_broker_roundtrip` 的 2000×`k_msleep(1)` 轮询覆盖，无 `k_sem` 排序、无 wakeup 跨线程用例。把 `event_drain` 改成从非 poller 线程调用（违反计划的 Z6 约束）后，现有用例仍全绿。

#### 定位

test/zephyr/src/main.c:260-272
test/zephyr/src/main.c:330-356
docs/plans/子计划-Zephyr平台后端接入.md:118
docs/plans/子计划-Zephyr平台后端接入.md:134
docs/plans/子计划-Zephyr平台后端接入.md:86

---

### ZEPHYR-004: `waitset_wait` 的 `max_completions` 截断行为无用例（计划 Step 4 明确要求）

- **状态**: 打开
- **严重度**: 🟢 较低
- **类型**: test

#### 问题描述

计划 Step 4 的用例清单明确列出"`waitset_wait` 在 `max_completions > CAP` 时的截断行为"（子计划:118）。但 test/zephyr/src/main.c 现有 12 个用例中，没有任何一个在"就绪成员数 > `max_completions`"的情况下调用 `ns_platform_waitset_wait`，因此 port.c:549-555 的截断提前退出（`count < max_completions`）没有断言保护。

截断在电平触发语义下是有契约含义的行为：未上报的成员 `signaled` 保持置位，由下一轮 wait 继续上报；这条行为一旦被改坏（例如把循环条件误写成按事件索引上限而非完成数上限）不会被现有用例发现。

#### review 建议

新增用例：注册 3 个 event 并全部 signal，用 `max_completions == 2` 调用 `waitset_wait`，断言 `out_count == 2`；再 wait 一次断言"剩余那个仍被上报"（电平保持）；逐个 `event_drain` 后断言 `out_count == 0`。同一模式可覆盖 `max_completions == 0` 的快速返回路径（port.c:509）。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

运行 ztest 套件：截断分支无断言。把 port.c:549 的循环条件改为按事件索引截断（例如把 `count < max_completions` 写成 `i < max_completions`）后套件仍全绿。

#### 定位

test/zephyr/src/main.c:117-158
platform/zephyr/port.c:549-555
docs/plans/子计划-Zephyr平台后端接入.md:118

---

### ZEPHYR-005: 超时用例未断言"等待 ≥ timeout"（AC-8 / Z10 明确要求）

- **状态**: 打开
- **严重度**: 🟢 较低
- **类型**: test

#### 问题描述

AC-8 要求"契约测试断言'等待 **≥ timeout**'"（子计划:85），Z10 也写明"断言措辞为'等待**至少** timeout，允许晚 fire'"（子计划:61）——因为"微秒→tick **向上取整**、禁止向下取整导致忙等"（AC-10、port.c:146-156）是 Zephyr 后端的核心约束之一。

但 test/zephyr/src/main.c:129-132 的超时用例只断言 `count == 0`，没有任何时长断言。也就是说：把 `ns_zephyr_us_to_timeout` 改成向下取整（floor）语义引入忙等，现有用例不会失败。

#### review 建议

在超时用例前后读取 `ns_platform_clock_monotonic_us`（或 `k_uptime_ticks`），断言 `elapsed >= timeout_us`（允许实际更长）。配合 `prj.conf` 的 `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000`，向上取整只会让 `elapsed` 更大，断言稳定。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

运行 ztest 套件：无用例检查等待时长。把 port.c:146-156 的换算改成向下取整后套件仍全绿（忙等不被发现）。

#### 定位

test/zephyr/src/main.c:129-132
platform/zephyr/port.c:146-156
docs/plans/子计划-Zephyr平台后端接入.md:61
docs/plans/子计划-Zephyr平台后端接入.md:85

---

### ZEPHYR-006: 未断言"pending 时 remove 后不再出现在 completions"（AC-5 / AC-7 明确要求）

- **状态**: 打开
- **严重度**: 🟢 较低
- **类型**: test

#### 问题描述

AC-5 要求"移除后同一（未重注册）waitable 不再出现在 completions"（子计划:82），AC-7 进一步给出断言措辞"返回 `NS_OK`，且被移除 waitable 在**同轮及后续** wait 中不再出现在 completions"（子计划:84）。

`test_remove_does_not_reset`（test/zephyr/src/main.c:164-193）覆盖的是另一半语义：signal → remove → **re-add** → 断言立即再次触发（电平保持、remove 不 reset）。但"pending（已 signal）时 remove 且**不** re-add，之后 wait 不再返回该 waitable"这一可观察保证没有断言。

#### review 建议

在 `test_remove_does_not_reset` 中补一段：`event_signal` → `waitset_remove` → 直接 `waitset_wait`（不 re-add）断言 `out_count == 0`、且 completions 中不出现该 waitable；随后再执行现有的 re-add 断言。这样 AC-5/AC-7 的两个方向都被覆盖。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

运行 ztest 套件：无"移除后不再出现"断言。若 remove 漏清 `slots[i]`（该 waitable 仍参与 `k_poll` 构造）而只清 `registered_waitset`，现有用例不会失败。

#### 定位

test/zephyr/src/main.c:164-193
platform/zephyr/port.c:469-491
docs/plans/子计划-Zephyr平台后端接入.md:82
docs/plans/子计划-Zephyr平台后端接入.md:84

---

### ZEPHYR-007: `waitset_add` 中 `i == CAP` 分支不可达（cleanup）

- **状态**: 打开
- **严重度**: 🟢 较低
- **类型**: cleanup

#### 问题描述

`ns_platform_waitset_add` 入口已用 `waitset->count >= NS_ZEPHYR_WAITSET_CAP` 拒绝超限（port.c:454）。而 `count` 与 `slots[]` 在 add/remove 中成对维护：add 在首个空槽写入并 `count++`（port.c:463-464），remove 置 NULL 并 `count--`（port.c:487-488），重复注册被 `registered_waitset` 拦住（port.c:453）。因此 port.c:458-461 的查找循环必然在某 `i < CAP` 处 break，`if(i == NS_ZEPHYR_WAITSET_CAP) return NS_E_TOO_MANY_HANDLES;` 不可达——与 FreeRTOS 后端 FREERTOS-004 同类。

#### review 建议

删除 port.c:461 的不可达返回，改以注释声明不变量（`count` == 非空 slots 数），与 FreeRTOS 后端在 FREERTOS-004 的处理方式保持一致。remove 中的同类检查（port.c:478-481）同样不可达，可一并删除或保留并注明"防御性检查"。

#### 作者建议

（待作者补充）

#### 可重现的失败场景

静态不可达：进入循环时 `count < CAP`，而 `count` 恰为非空 slot 数，故必存在空槽，`i` 不可能到达 `CAP`。

#### 定位

platform/zephyr/port.c:454
platform/zephyr/port.c:458-461
platform/zephyr/port.c:478-481

---

## 现在关闭的问题

（暂无。）
