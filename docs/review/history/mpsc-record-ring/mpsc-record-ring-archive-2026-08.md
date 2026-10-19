# mpsc-record-ring review 归档 2026-08

<a id="MPSC-011"></a>
### MPSC-011: `MPSC_MEMORY_ORDER.md` 描述已删除的固定容量 MPSC 队列，未文档化 record ring 的内存序协议

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: doc

#### 问题描述
`docs/MPSC_MEMORY_ORDER.md` 描述的是已删除的固定容量 MPSC 队列（`nanosig_mpsc.h`），包括 `ns_mpsc_try_push` / `ns_mpsc_try_pop` 等已不存在的函数。当前仓库只有 MPSC record ring，其内存序协议（reserve_pos CAS + valid-bit release + consumer acquire）仅在内联代码注释中描述，无独立文档。

#### review 建议
创建 `docs/MPSC_RECORD_RING_MEMORY_ORDER.md` 或更新 `MPSC_MEMORY_ORDER.md` 添加 record ring 章节，文档化三阶段发布链。

#### 作者建议
已重写 `docs/MPSC_MEMORY_ORDER.md`，删除旧版固定容量 MPSC 队列描述，替换为 MPSC record ring 的三阶段发布链（reserve → write → commit）和完整内存序协议文档。
→ 关闭-已修复

#### 关闭原因
`docs/MPSC_MEMORY_ORDER.md` 已重写为 record ring 内存序协议文档。

- **关闭日期**: 2026-08-02
- **状态**: 关闭-已修复

#### 定位
docs/MPSC_MEMORY_ORDER.md

<a id="MPSC-012"></a>

---
