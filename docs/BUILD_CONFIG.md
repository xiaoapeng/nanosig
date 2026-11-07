# 构建与模块配置

## 构建文件职责

| 入口 | 职责 |
|---|---|
| CMakeLists.txt | 项目、构建开关、库源码和目标、子目录入口 |
| cmake/Platform.cmake | 选择同一后端的源文件与平台头，应用平台参数及依赖 |
| cmake/ModuleConfig.cmake | 工具模块的可选参数入口及 PUBLIC/PRIVATE 传播 |
| test/CMakeLists.txt | 单元、集成、后端测试，以及统一的 API 检查聚合目标 |
| demos/CMakeLists.txt、bench/CMakeLists.txt | 各自的可执行目标及 CTest 注册 |
| cmake/ApiCompileChecks.cmake | C/C++ syntax-only 检查，共用库的公共宏和包含路径 |
| cmake/Coverage.cmake | 覆盖率编译选项与报告目标 |
| cmake/SanitizeAll.cmake | 审计编排；通过 `api-compile-checks` 调用检查，不解析生成器文件 |

公开 preset、target 名称及可执行文件输出位置保持不变；共享 `build/` 仍用于编辑器集成，
baremetal 保留独立目录。Presets 的隐藏基类只收拢公共配置，三个 sanitizer 开关明确复位；
切回 host 会同时关闭 FreeRTOS harness 与 coverage，用户设置的模块参数仍保留在 cache 中。

`NANOSIG_BUILD_TESTS` 与 `NANOSIG_BUILD_API_CHECKS` 独立控制运行时测试和语法检查。
`sanitize-all` 保留仅在启用 sanitizer 时执行审计、不代替 CTest 的职责。

顶层 CMake 负责构建目标和平台选择，模块拥有自己的参数与源码默认值。
现有宏名保留；无需生成配置头，也可以直接集成源码。

## 工具模块入口

cmake/ModuleConfig.cmake 提供 `nanosig_apply_module_config(target)`。
下列 CMake cache 变量与 C 宏同名，类型均为 `STRING`。**空值表示不注入宏，使用模块源码默认值**；
填写后才通过目标的编译定义传入，不在 CMake 中复制一份默认值。

| 模块 | CMake 变量 / C 宏 | 源码默认值 | 有效输入 | 传播 |
|---|---|---|---|---|
| debug | `NS_CONFIG_DEFAULT_DEBUG_LEVEL` | `NS_DBG_DEBUG` | 0–5 或对应 `NS_DBG_*` 等级常量 | PRIVATE |
| debug | `NS_DBG_MODULE_LEVEL_DEFAULT` | `NS_DBG_SYS` | 同上 | PUBLIC |
| debug | `NS_DBG_MODULE_LEVEL_CORE` | 公共默认模块等级 | 同上 | PRIVATE |
| debug | `NS_DBG_MODULE_LEVEL_TIMER` | 公共默认模块等级 | 同上 | PRIVATE |
| debug | `NS_DBG_MODULE_LEVEL_BROKER` | 公共默认模块等级 | 同上 | PRIVATE |
| debug | `NS_DBG_MODULE_LEVEL_MPSC` | 公共默认模块等级 | 同上 | PRIVATE |
| debug | `NS_DBG_MODULE_LEVEL_PLATFORM` | 公共默认模块等级 | 同上 | PRIVATE |
| debug | `NS_CONFIG_DEBUG_FLAGS` | `NS_DBG_FLAGS_DEBUG_TAG \| NS_DBG_FLAGS_MONOTONIC_CLOCK` | 日志 flags 的 C 表达式，例如 `0` 或 `NS_DBG_FLAGS_DEBUG_TAG` | PUBLIC |
| debug | `NS_CONFIG_DEBUG_ENTER_SIGN` | `"\n"` | 带双引号的 C 字符串字面量，例如 `"\r\n"` | PUBLIC |
| formatio | `NS_CONFIG_STDOUT_MEM_CACHE_SIZE` | 512 字节 | 正十进制整数 | PRIVATE |
| hashtable | `NS_CONFIG_HASHTBL_MIN_SIZE` | 16 个桶 | 不小于 8 的十进制 2 的幂 | PUBLIC |
| platform | `NS_PLATFORM_WAIT_EMPTY_MAX_US` | 1000000 微秒 | 正十进制整数 | PUBLIC |

`PUBLIC` 同时传给库及链接 `nanosig::nanosig` 的调用方，确保头文件宏行为一致；
`PRIVATE` 只传给库。flags 表达式由 C 编译器校验；输入不得包含分号、实际换行字符或原始 `#`。
CMake 会丢弃含原始 `#` 的编译定义，因此入口直接报错；C 字符串需要此字符时使用八进制转义，
例如命令行传 `'-DNS_CONFIG_DEBUG_ENTER_SIGN="\043"'`，库、消费者及 API 检查均按 `"#"` 使用。
换行符使用转义字符，CMake 空值与 C 字符串 `""` 不同：前者使用默认值，后者关闭自动换行。

```sh
cmake --preset macos-release \
  -DNS_CONFIG_DEFAULT_DEBUG_LEVEL=NS_DBG_WARNING \
  -DNS_DBG_MODULE_LEVEL_DEFAULT=NS_DBG_SYS \
  -DNS_DBG_MODULE_LEVEL_BROKER=NS_DBG_DEBUG \
  -DNS_CONFIG_STDOUT_MEM_CACHE_SIZE=1024 \
  -DNS_CONFIG_HASHTBL_MIN_SIZE=32 \
  -DNS_CONFIG_DEBUG_FLAGS=NS_DBG_FLAGS_DEBUG_TAG \
  '-DNS_CONFIG_DEBUG_ENTER_SIGN="\r\n"'
cmake --build --preset macos-release
```

重置单项配置可传空值，例如 `-DNS_CONFIG_STDOUT_MEM_CACHE_SIZE=`。
配置会保存在构建目录的 cache 中，切换 preset 不会清空用户设置的模块参数。

日志有两个独立门限：`NS_DBG_MODULE_LEVEL_*` 决定模块宏的编译期过滤，
`NS_CONFIG_DEFAULT_DEBUG_LEVEL` 决定运行时初始门限，后者仍可用 `ns_dbg_set_level()` 修改。
内置模块遵循“显式模块等级 → 公共默认模块等级”；应用自行定义的模块也可使用
`#define NS_DBG_MODULE_LEVEL_MY_APP NS_DBG_MODULE_LEVEL_DEFAULT`。

## 平台与运行时入口

| CMake 变量 / C 宏 | 默认值 | 适用范围 | 传播 |
|---|---|---|---|
| `NANOSIG_BROKER_STACK_SIZE` | 0 字节，使用后端默认栈 | 非裸机 | PRIVATE |
| `NANOSIG_FREERTOS_WAITSET_CAP` | 32 | FreeRTOS | PUBLIC |
| `NANOSIG_FREERTOS_TASK_PRIORITY` | 1 | FreeRTOS | PUBLIC |
| `NANOSIG_BAREMETAL_LOOP_QUEUE_BYTES` | 1024 字节 | 裸机 | PUBLIC |
| `NANOSIG_BAREMETAL_POLL_MAX_ROUNDS` | 4 | 裸机 | PUBLIC |

这些原有入口仍保留原默认值和契约。broker 栈必须由构建系统注入，源码缺少该宏即报错。
`NS_MPSC_CACHELINE_ALIGNED` 影响结构体布局，由构建层向库和调用方一致传递，
顶层在 `WIN32 OR UNIX` 时启用，Zephyr 模块入口也启用；本轮不改变该选择策略。
`NANOSIG_BAREMETAL` 由所选平台头定义，不另设 CMake 开关。

## 直接集成源码与 Zephyr

`cmake -DNAME=value` 设置 CMake 变量，编译器的 `-DNAME=value` 才定义 C 宏。
本页列出的入口有明确转发逻辑；任意其他 cache 变量不会自动变成源码宏。
直接集成源码仍可用编译器定义覆盖模块的 `#ifndef` 默认值；涉及头文件的配置必须同时
传给库及应用，不能仅在应用源文件中定义后期待已经编译的库发生改变。
同一参数选择 cache 入口或直接编译定义其中一种，避免重复、冲突的 `-D`。

Zephyr 保留独立 module/Kconfig 入口，本轮不把顶层 cache 参数自动映射为 Kconfig。
`CONFIG_NANOSIG_BROKER_STACK_SIZE` 由 Zephyr 构建转发；waitset 容量与线程优先级遵循
`CONFIG_NANOSIG_*` > 同名去掉 `CONFIG_` 的编译定义 > 平台头默认值。
Zephyr 中调整工具模块参数仍使用其构建系统的编译定义，并保持公共配置对应用可见。

## 配置回归验证

```sh
cmake -DTEST_BINARY_ROOT=/tmp/nanosig-module-config \
  -P test/cmake/check_module_config.cmake
```

独立消费者分别验证默认配置和自定义配置：公共宏传播、内部宏隔离、运行时初始日志门限、
CRLF 换行、stdout 缓冲容量和哈希表实际初始桶数；另验证非法参数在配置期失败。
原始 `#` 必须在配置期被拒绝；`"\043"` 则同时经过 C/C++ API 检查、消费者字符串断言及库内 hex 日志的输出捕获。
该检查单独运行，避免普通 CTest 递归配置整个项目。

脚本还覆盖字面量零值，以及关闭 API 检查时 `OFF`、`FALSE`、`0`、`NO` 四种写法，
确保 CMake 的目标注册与 shell 审计采用同一个布尔值。

## 本轮重构验证（2026-11-07）

验证基线为 db44636，本轮重构位于 `refactor/cmake-module-config` 分支。

- 默认 host 构建的 53 项 CTest 名称、属性和可执行文件路径与基线一致；普通测试仍将 MPSC 压测参数设为 20 秒。
- 默认构建的源文件与编译参数一致，只有迁移到子目录的对象文件路径变化。
- 48 个公开 preset 展开后的原有字段一致；新增 platform、FreeRTOS harness、coverage 的默认复位值。
- macOS Release、ASAN、UBSAN 常规测试各 51 项通过；两种 sanitizer 的 `sanitize-all` 通过。
- baremetal 默认测试及自定义容量/轮数的 UBSAN 测试各 7 项通过，保留缺失 watcher 符号的负向链接门。
- FreeRTOS POSIX Release 测试 3 项通过；关闭运行时测试时，demo/bench 独立构建并通过 8 项测试。
- 默认、自定义、零值模块参数的实际库/消费者/C/C++ API 回归通过，包含空格路径、CRLF、位运算表达式和非法参数拒绝。
- `review-docs-check` 为 0 error、0 warning；只同步源码定位，未改变已有文档保护裁决。

bench 开启验证时，基线代码在 AppleClang 的 `-Werror` 下已有两处 size_t 到 double 的隐式转换警告；
本轮仅增加显式转换，保持原有百分位计算。未修改 CI 工作流，也未执行云端 CI、Linux/Windows/Zephyr 构建或 nightly 长测。
