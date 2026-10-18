# 深度访谈规格：broker op 等待改为永不超时 + shutdown 契约收紧

> 本文档由深度访谈 + 共识评审（Architect 1 轮 + Critic 3 轮）收口，权威计划见
> `.omc/plans/consensus-broker-no-timeout.md`，实施记录见 `docs/review/broker-code-review.md`
> 的 2026-10-18 条目。

## 背景与动机

`ns_broker_wait_op_completion` 原本用 500ms 超时。超时后 `ns_broker_add` /
`ns_broker_remove` 会尝试 `ns_broker_try_dequeue_op` 摘除请求；若摘除失败（broker
已出队但尚未处理完），用户线程再等一次，仍超时则返回 `NS_E_TIMEOUT` 并销毁栈上的
`req` 与 `wakeup`。而 broker 线程随后仍会执行 `req->rc = op_rc` 并向 `req->wakeup`
signal——写已失效的栈帧、向已关闭的 fd 写信号，构成 **use-after-free**。

入队侧已能在 signal 失败时持锁回滚（`ns_broker_queue_op`），超时恢复路径只剩这一条
不安全分支。

## 契约立场

正常使用中不应出现阻塞；出现阻塞必然是用户违反契约——在 watcher handler 中使用阻塞
函数，或未在 `ns_shutdown()` 前释放资源。**库不为违反契约的场景提供运行时救援。**

## 决策

### 1. 等待语义

- `ns_broker_wait_op_completion` 改用 `NS_PLATFORM_WAIT_INFINITE_US`。
- **等待失败不得返回**：此时 `req` 仍挂在 op 队列中，提前返回同样构成上面的 UAF。
  非 `NS_PLATFORM_WAIT_SIGNALED` 时记一次日志后继续等待，只在收到 signal 时返回。
- 删除 `ns_broker_try_dequeue_op`。
- `ns_broker_add` / `ns_broker_remove` 的公共骨架合并为 `ns_broker_submit_op`，
  两个公开函数成为薄封装；两者不再返回 `NS_E_TIMEOUT`（`NS_E_TIMEOUT` 符号保留，
  demos 自身轮询仍在使用）。

### 2. shutdown 契约收紧

- 删除 `ns_broker_drain_op_queue_shutdown`：不再为残留 op 的等待线程补发
  `NS_E_SHUTDOWN`。
- 删除 `ns_broker_remove_all_watchers`：库不再兜底清理残留 watcher。
- `ns_broker_global_shutdown` 在 thread join 后，若 op 队列非空则记一条警告
  （best-effort，可能漏掉检查后才入队的 op；刻意不持 `op_lock`，避免与卡在
  `ns_broker_queue_op` 的违约线程互锁）。
- `ns_shutdown` 的公开契约改为：调用方必须已销毁所有 loop / timer，并
  `ns_broker_remove` 全部 watcher。违反时 `ns_platform_waitset_deinit` 因非空返回
  `NS_E_EXISTS`，底层句柄泄漏（Linux/macOS 为 epoll/kqueue 与 timer fd）。

### 3. 随契约收紧一并删除的死代码

- broker 内部 `watcher_mutex`（唯一的跨线程持有者是正在被删除的 shutdown 遍历；
  add/remove 全部经 op 队列在 broker 线程串行执行）。
- broker 内部 `watcher_head` 链表与 `ns_watcher_t.broker_node` 字段。
- 「已注册」判定改用平台层的 `waitable.registered_waitset`，且**必须收窄**为
  `== &g_broker->waitset`（该字段的语义是"注册在某个 waitset"，而
  `ns_platform_waitset_init` 是公开 API，调用方可用自建 waitset）；`g_broker == NULL`
  时返回"未注册"。
- `ns_broker_run` 中 `ns_timer_mgr_next_timeout` 的错误分支（out 参数在错误路径不被
  写，两个分支赋值与初值相同）。
- `shutdown_started` 改 `atomic_int`（消除形式上的 C11 竞争；**不构成 UAF 屏障**，
  `g_broker` 仍是 plain pointer 且会被 free，对象生命周期仍依赖调用方契约）。

## 行为变更（有意）

handler 内误调 `ns_broker_add` / `ns_broker_remove` / `ns_shutdown`，表现从
"500ms 后返回 `NS_E_TIMEOUT`"变为**永久阻塞**。已在 `nanosig_broker.h` 的 `@warning`
与 `@note` 中写明。

## 已知缺陷（不在本次范围）

Linux `ns_linux_wakeup_drain` 在非 `EAGAIN`/`EINTR` 的 errno 上返回 `NS_E_INVAL`
且未消费 eventfd 计数；`poll` 已报 `POLLIN`，因此无限等待下调用方会 **100% CPU
热自旋**（仅 host 后端，恰是 CI 平台）。根因在 `platform/`，本规格所属改动不动平台层；
已登记到 `docs/review/platform-ports-code-review.md` 的 PORT-023。

## 非目标

- 不修改任何平台后端（`platform/*/port.c`）。
- 不引入超时日志、看门狗、自检计数等替代诊断（shutdown 的那一条警告为唯一例外）。
- 不新增阻塞场景测试用例。
- 不移除 `NS_E_TIMEOUT` 符号，不改 demos。
- 不改 `NS_E_*` 返回码集合：`add` 重复注册仍为 `NS_E_EXISTS`，`remove` 未注册仍为
  `NS_E_INVAL`。

## 验收要点

- `src/ns_broker.c` 内不再出现 `NS_E_TIMEOUT`；`try_dequeue_op` /
  `drain_op_queue_shutdown` / `remove_all_watchers` / `watcher_head` /
  `broker_node` / `watcher_mutex` 全仓库无残留。
- `ns_watcher_t` 只剩 `waitable` 与 `handler`。
- `test_watcher_deinit_after_shutdown`（原 `test_shutdown_removes_residual_watcher`）
  保留：验证违约路径不崩溃、`ns_watcher_deinit` 返回 `NS_OK` 且 `ns_broker() == NULL`。
- 六个 broker 单测带 `TIMEOUT 60`（无等待上界使死锁更易发生，避免烧掉整个 CI job）。
- 构建 / 测试 / `api-compile-checks` / sanitizer 全绿。
