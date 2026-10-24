# global-review review 归档 2026-09

<a id="GLOBAL-003"></a>
### GLOBAL-003: `共识计划.md` 仍引用已删除的 `ns_platform_wakeup_get_waitable`

- **状态**: 关闭-已修复
- **严重度**: 🟢 较低
- **类型**: doc
- **关闭原因**: 已更新 docs/plans/共识计划.md:122 的当前实现快照引用为 `ns_platform_event_*`；历史段落（:613/:641/:988）保留不动。
- **关闭日期**: 2026-09-21

#### 问题描述

本次 event 重构移除了平台接口 `ns_platform_wakeup_get_waitable`（由
`ns_platform_event_*` 取代），但 `docs/plans/共识计划.md:122` 在"当前实现快照"
段仍将其列为"平台层新增"接口。该文档被 AGENTS.md 指定为需与当前实现决策保持
同步。其余引用（:613、:641、:988）位于 P5b 设计段与 Iter 记录，属历史快照，
可不改。

#### review 建议

更新 `docs/plans/共识计划.md:122`：把 `ns_platform_wakeup_get_waitable` 替换为
`ns_platform_event_*`（或注明该接口已被 event 原语取代）。历史段落
（:613、:641、:988）保留不动。

#### 作者建议

（2026-09-21 作者决定：直接修文档。）仅更新 `共识计划.md:122` 的当前实现快照引用为 `ns_platform_event_*`，历史段落（:613/:641/:988）保留不动。

#### 可重现的失败场景

`grep -rn "ns_platform_wakeup_get_waitable" docs/plans/共识计划.md`
→ 4 命中，其中 :122 与当前平台接口面不一致。

#### 定位

---
