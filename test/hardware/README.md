# MCXN947 FreeRTOS MPSC 实机回归

`mcxn947_mpsc_preempt.c` 用于 fly 的 `project/mcxn947/freertos-demo`。在私有工程副本中
替换 `app/task_main.c`，保留原 `main.c` 和板级初始化，并把 nanosig 源码根目录加入 app
的 include 路径。它包含并重命名生产实现，只在测试副本的成功 CAS 后挂起真实 SysTick；
不向库增加调度 hook。仅用于测试固件，不能替代正常 demo 的业务任务。

高优先级任务先执行 `vTaskDelay(1)`；低优先级生产者取得 busy 后挂起 SysTick。
修复版应延迟中断到 busy 清除后，再由 PendSV 调度高优先级生产者。旧实现会使后者
在 busy 上自旋。测试期间暂时关闭周期 SysTick 请求，使交错可重复；每轮成功后恢复。
另检查真实 BASEPRI 嵌套恢复和满队列失败路径。

修复版预期 RTT：

```text
MPSC PASS rounds=128 checks=2571 high=128 irq=128
```

`g_hw_mpsc` 保留在 ELF 中，可用 J-Link 读取：成功时 `stage=5`、`failures=0`。
对照旧实现时，用固定提交的 MPSC `.c` 替换测试文件中包含的实现；预期输出
`MPSC HIGH entered while busy`，停在 `stage=3`、`high_saw_busy=1`。

烧录前备份现有固件，对照测试结束后恢复带修复的正常 demo。测试源和日志不代表
已经测量最坏中断延迟或长期稳定性。实测记录见
`docs/review/evidence/mpsc-preempt-hardware-2026-11-19.json`。
