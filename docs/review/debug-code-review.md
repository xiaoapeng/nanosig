---
review_schema: "nanosig-review-v1"
entries:
  - id: "DEBUG-001"
    module: "debug"
    status: "open"
    severity: "medium"
    type: "bug"
    section: "open"
    location: "docs/review/debug-code-review.md#DEBUG-001"
    locations:
      - "src/ns_debug.c:72"
      - "src/ns_debug.c:105"
      - "src/ns_debug.c:126"
---

# debug Code Review

## 修复历史

## 现在打开的问题

<a id="DEBUG-001"></a>
### DEBUG-001: 运行时修改日志等级与输出线程之间存在数据竞争

- **状态**: 打开
- **严重度**: 🟡 中（前提：初始化完成后一个线程 set_level、另一个线程输出日志，全部线程 join 后才 shutdown；macOS ThreadSanitizer 实测）
- **首次发现日期**: 2026-11-04
- **类型**: bug

#### 问题描述

运行期调整日志等级会与正常日志输出产生 C 数据竞争。dbg_level 是普通 int，setter 无同步写入；ns_dbg_raw/ns_vdbg_raw 在输出 mutex 之外读取它。因此输出锁不能保护该共享等级。

#### review 建议

使用项目已有原子抽象保存 dbg_level，对等级执行 relaxed load/store。无需新增输出热路径锁。

#### 作者建议

（待作者补充）

#### 可重现的失败场景


初始化 ns_debug；启动两个 pthread 并用原子 go 同时放行。线程 A 循环 100000 次 ns_dbg_set_level，在 NS_DBG_DEBUG 和 NS_DBG_WARNING 间切换；线程 B 循环 100000 次 ns_dbg_raw(NS_DBG_DEBUG,0,"")。join 两线程后 shutdown。使用 -fsanitize=thread 构建，并以 TSAN_OPTIONS=halt_on_error=1 运行，实测 exit 134：ThreadSanitizer data race，写为 ns_debug.c:72、读为 :105，Location is global dbg_level。

可独立运行的 harness（保存为 /tmp/debug-race.c）：
```c
#include <nanosig/ns_debug.h>
#include <pthread.h>
#include <stdatomic.h>
static atomic_int go;
static void *writer(void *p){(void)p;while(!atomic_load(&go)){}for(int i=0;i<100000;i++)ns_dbg_set_level((i&1)?NS_DBG_DEBUG:NS_DBG_WARNING);return 0;}
static void *reader(void *p){(void)p;while(!atomic_load(&go)){}for(int i=0;i<100000;i++)ns_dbg_raw(NS_DBG_DEBUG,0,"");return 0;}
int main(void){pthread_t a,b;ns_debug_init();pthread_create(&a,0,writer,0);pthread_create(&b,0,reader,0);atomic_store(&go,1);pthread_join(a,0);pthread_join(b,0);ns_debug_shutdown();return 0;}
```
```sh
clang -std=gnu11 -g -O1 -fsanitize=thread -DNS_MPSC_CACHELINE_ALIGNED -Iinclude -Ibuild/include -Iplatform/macos /tmp/debug-race.c src/ns_debug.c build/libnanosig.a -o /tmp/debug-race
TSAN_OPTIONS=halt_on_error=1 /tmp/debug-race
```


#### 影响面

直接读取者 ns_dbg_raw、ns_vdbg_raw；向上包括所有 debug 宏、ns_dbg_hex 以及 core/timer/broker/MPSC 日志。setter 为公开运行期配置接口。初始化和 shutdown 已串行，竞态只涉及等级；现有 debug 测试未并发调整等级与输出。

#### 文档仲裁与历史去重

公共 debug 头未约定 set_level 只能在线程启动前调用，README 明示运行期静音。formatio 正文/history 无同根问题；FORMATIO-025 禁止 sink 内重入输出，与本项无关。

#### 定位
src/ns_debug.c:72
src/ns_debug.c:105
src/ns_debug.c:126

---

## 现在关闭的问题
