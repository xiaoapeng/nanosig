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
      - "src/ns_debug.c:116"
  - id: "DEBUG-002"
    module: "debug"
    status: "closed-fixed"
    severity: "low"
    type: "cleanup"
    section: "closed"
    closed_date: "2026-11-06"
    location: "docs/review/debug-code-review.md#DEBUG-002"
    locations:
      - "src/ns_debug.c:99"
      - "src/ns_debug.c:111"
      - "include/nanosig/ns_debug.h:173"
---

# debug Code Review

## 修复历史

- **DEBUG-002（2026-11-06）**: ns_dbg_raw 仅适配变参后调用 ns_vdbg_raw，过滤与加解锁保留单一路径。新增真实 mutex 下的双入口精确输出、返回值和 suppress 回归，修改前后通过。macOS release 53/53、ASAN/UBSAN 定向各2/2、两套 sanitize-all 通过；Clang 静态分析无 debug 告警。DEBUG-001 仍打开，仅更新复用后的源码定位和描述。未运行 Linux/Windows。 — commit `refactor/ponytail-hashtable-debug（工作区，未提交）`

## 现在打开的问题

<a id="DEBUG-001"></a>
### DEBUG-001: 运行时修改日志等级与输出线程之间存在数据竞争

- **状态**: 打开
- **严重度**: 🟡 中（前提：初始化完成后一个线程 set_level、另一个线程输出日志，全部线程 join 后才 shutdown；macOS ThreadSanitizer 实测）
- **首次发现日期**: 2026-11-04
- **类型**: bug

#### 问题描述

运行期调整日志等级会与正常日志输出产生 C 数据竞争。dbg_level 是普通 int，setter 无同步写入；ns_vdbg_raw 在输出 mutex 之外读取它，ns_dbg_raw 也通过该入口输出。因此输出锁不能保护该共享等级。DEBUG-002 合并入口后只剩一处读取，竞态性质不变。

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
src/ns_debug.c:116


---

---

## 现在关闭的问题

<a id="DEBUG-002"></a>
### DEBUG-002: 变参日志入口未复用已有 va_list 入口

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低（前提：按当前仓库实现与公开契约静态走查；仅维护性重复，未发现由重复本身导致的功能故障）
- **首次发现日期**: 2026-11-06
- **类型**: cleanup
- **关闭原因**: 修复 commit refactor/ponytail-hashtable-debug（工作区，未提交）
- **关闭日期**: 2026-11-06

#### 问题描述

ns_dbg_raw 和 ns_vdbg_raw 各自实现同一套等级过滤、条件加锁、格式化和解锁。两处策略完全相同，后续修改必须同时维护。

#### review 建议

ns_dbg_raw 仅保留 va_start、调用 ns_vdbg_raw、va_end 和返回值。等级过滤与锁统一留在 ns_vdbg_raw，不能保留外层锁而形成嵌套加锁；不新增辅助层。

#### 作者建议

作者于 2026-11-06 同意先处理哈希表与日志两项。清理计划：先验证等级过滤、flags、返回值及 va_list 入口的回归；缺少覆盖时先补测试，再将 ns_dbg_raw 简化为 va_start → ns_vdbg_raw → va_end。过滤及加解锁只留在 ns_vdbg_raw，避免嵌套锁；不处理 DEBUG-001。完成针对性与 sanitizer 验证；改动留工作区，不提交。

#### 可重现的失败场景

给两入口相同 level/flags/fmt 和参数，都会执行相同过滤和临界区。静态对比两函数可确认重复；这是维护性问题，不声称已发生输出错误。

#### 影响面

ns_dbg_raw 的所有日志宏、ns_dbg_hex 及外部调用者；ns_vdbg_raw 的外部调用者不变。下级仍为 ns_dbg_vprintf 和平台 mutex。test/unit/test_debug.c:244 覆盖 va_list 输出，另有等级、flags 和返回值测试；本轮未运行。

#### 文档仲裁与历史去重

DEBUG-001 是 dbg_level 数据竞争，保持独立。本项仅合并入口实现，不声称能修复该竞态。公共头没有要求两个入口各自实现相同策略。

#### 定位

src/ns_debug.c:99
src/ns_debug.c:111
include/nanosig/ns_debug.h:173


---

---
