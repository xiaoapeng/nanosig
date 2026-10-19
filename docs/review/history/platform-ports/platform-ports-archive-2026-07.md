# platform-ports review 归档 2026-07

<a id="PLATFORM-012"></a>
### PLATFORM-012: Windows port `ns_platform_waitset_wait` 编译错误

- **状态**: 关闭-已修复
- **关闭原因**: 补上 `->primitive.handle` 前缀，当前工作树（未提交），对应 `platform/windows/port.c:385`
- **关闭日期**: 2026-07-04

---

<a id="PLATFORM-013"></a>

---

### PLATFORM-013: Windows 平台 `CreateThread` 改用 `_beginthreadex`

- **状态**: 关闭-已修复
- **关闭原因**: 替换为 `_beginthreadex` + 线程函数签名匹配 `unsigned __stdcall(void *)` + 添加 `#include <process.h>`，当前工作树（未提交）
- **关闭日期**: 2026-07-04

---
