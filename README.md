# nanosig

C11 signal/slot 库 — Linux / macOS / Windows。

- **调用方拥有存储**：`ns_connection_t`、`ns_loop_t`、`ns_timer_t`、`ns_watcher_t` 自持类型，库不分配。
- **热路径零分配**：emit / dispatch / slot 在预分配 MPSC ring 上操作，不调用 `malloc`。
- **显式 loop 绑定**：loop 不绑定线程，跨线程 emit 安全。
- **事件 broker**：全局 `ns_event_broker_t` 转换平台事件（fd/handle readiness）和定时器为 signal emit。

**状态**：P0–P9 已完成（2026-06-20）。详见 [共识计划](docs/plans/共识计划.md)。

## 构建

```sh
# macOS
cmake --preset macos-release
cmake --build --preset macos-release
ctest --preset macos-release

# Linux 将 macos- 替换为 linux-，Windows 替换为 windows-
```

所有平台共享：
- `cmake --build <preset> --target api-compile-checks` — 编译期 API 契约检查
- `cmake -DNANOSIG_BUILD_BENCH=ON ...` — 启用 bench 目标

依赖：C11 编译器 + pthreads（Linux/macOS）；Windows 无外部依赖。

## 使用

```c
#include <nanosig/nanosig.h>

static void on_signal(void *user_data, const ns_no_payload_t *payload)
{
    (void)payload;
    ns_loop_t *loop = (ns_loop_t *)user_data;
    ns_loop_quit(loop);
}

int main(void)
{
    ns_signal_t sig;
    ns_connection_t conn;
    ns_loop_t *loop = NULL;

    ns_init();
    ns_loop_create(&loop, NULL);
    ns_signal_init(&sig, ns_no_payload_t);
    ns_signal_connect(&sig, (ns_slot_fn)on_signal, loop, loop, &conn);

    ns_signal_emit(sig, NS_NO_PAYLOAD);
    ns_loop_run(loop);          // dispatch → on_signal → quit

    ns_signal_disconnect(&conn);
    ns_signal_deinit(sig);
    ns_loop_destroy(loop);
    ns_shutdown();
    return 0;
}
```

验收 demo（`demos/`）：`demo_same_thread` / `demo_cross_thread` / `demo_timer_cross_thread`。

## API 索引

| 头文件 | 用途 |
|--------|------|
| `nanosig.h` | 聚合入口 |
| `nanosig_loop.h` | loop 生命周期与运行控制 |
| `nanosig_signal.h` | signal init / connect / disconnect / emit |
| `nanosig_timer.h` | timer create / start / cancel / restart / destroy |
| `nanosig_broker.h` | watcher / broker 注册与注销 |
| `nanosig_waitable.h` | waitable 类型与事件位 |
| `nanosig_status.h` | 状态码（`NS_OK`, `NS_E_*`） |
| `nanosig_types.h` | 通用宏（`NS_CONTAINER_OF`, `NS_STATIC_ASSERT`） |
| `nanosig_atomic.h` | 原子操作 |
| `nanosig_ds.h` | 数据结构聚合（list / slist / ringbuf / hashtable / rbtree / MPSC ring） |

## 设计文档

- [共识计划](docs/plans/共识计划.md) — 阶段计划与 API 决策
- [架构](docs/ARCHITECTURE.md) — 架构、数据流、所有权模型、线程安全
- [API 设计](docs/API_DESIGN.md)
- [数据结构](docs/DATA_STRUCTURES.md)
- [定时器设计](docs/TIMER_DESIGN.md)
- [Broker 设计](docs/BROKER_DESIGN.md)
- [审计](docs/audit/) — 7 份审计报告
- [Bench 基线](bench/results/) — 跨平台性能基线

## 许可

nanosig contributors — 见 `LICENSE`。
