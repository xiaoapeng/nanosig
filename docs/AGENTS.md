<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-16 | Updated: 2026-05-24 -->

# docs

## Purpose
This directory holds the public design documentation for the PD closeout and implementation phases. It explains the consensus plan, requirements interview, public API shape, data-structure ownership, and runtime implementation constraints.

## Key Files
| File | Description |
|------|-------------|
| `plans/共识计划.md` | 中文权威共识计划，记录当前状态、API 决策、阶段计划和验证证据。 |
| `specs/需求访谈.md` | 中文需求访谈收口文档，记录目标、约束、验收标准和最终修订。 |
| `plans/子计划-信号槽定时器代理测试方案.md` | 共识评审后的信号槽/Timer/Broker 全量测试实施计划。 |
| `specs/深度访谈-信号槽定时器代理测试规格.md` | 深度访谈输出的信号槽/Timer/Broker 测试规格文档。 |
| `API_DESIGN.md` | Public API draft, usage examples, lifecycle rules, and PD review notes. |
| `DATA_STRUCTURES.md` | Public opaque type list and internal structure design draft for later phases. |
| `EVENTHUB_OS_STYLE.md` | Recorded eventhub_os code/comment style and nanosig adaptation rules. |
| `THREAD_LOOP_BINDING.md` | Loop lifecycle model (explicit passing, no thread binding) and connect behavior. |
| `agents/AGENTS.md` | Central index for moved directory-specific AGENTS instructions. |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `plans/` | 中文测试方案与实施计划文档（原 `.omc/plans/`）。 |
| `specs/` | 中文需求规格与需求访谈文档（原 `.omc/specs/` + 需求访谈）。 |
| `agents/` | Centralized AGENTS copies for source-tree instructions. |

## For AI Agents

### Working In This Directory
- Keep docs synchronized with `include/nanosig/*.h` and `demos/*.c` in the same change.
- Keep `plans/共识计划.md` and `specs/需求访谈.md` synchronized with current implementation decisions; do not let hidden workflow artifacts become the only updated copy.
- Mark unresolved public API choices as PD review points instead of silently treating them as final.
- Keep the centralized `docs/agents/` copies synchronized with the source-tree directories they describe.
- Keep `EVENTHUB_OS_STYLE.md` updated when nanosig deliberately accepts or rejects an eventhub_os style convention.
- Keep `THREAD_LOOP_BINDING.md` synchronized with loop/connect API changes.
- Document zero-tolerance consolidation rules when API review rejects duplicate low-level functions; wrappers may differ in ergonomics, but the low-level function surface should use parameters.

### Testing Requirements
- Re-run demo syntax checks after changing examples in documentation.

### Common Patterns
- Prefer concrete code snippets over prose-only API descriptions.
- Code snippets that acquire more than one nanosig resource must show deterministic teardown with kernel-style `goto` cleanup labels.
- Use the current macro style in examples: function-wrapper connect/emit/init macros stay lowercase, while declaration, payload-metadata, and type-only macros are uppercase.
- When documenting signal/slot APIs, keep the payload/no-payload connect matrix visible, and show no-payload usage via `ns_no_payload_t` rather than `0` suffix APIs. Loop is always explicitly passed (no implicit current-thread loop).
- Document static and struct-owned signals as caller-owned `ns_signal_t` storage that must be initialized with `ns_signal_init(signal, payload_type)` before use and deinitialized with `ns_signal_deinit(signal)` after connections are disconnected. Do not reintroduce legacy signal config objects or aggregate/static signal initializer macros.
- Document timers as caller-owned `ns_timer_t` objects with first member `ns_signal_t signal`; timer callbacks connect to that embedded no-payload signal, intervals are `uint64_t` microseconds, attrs are `NS_TIMER_ATTR_*` bitmaps, and `ns_timer_cancel` is valid even when the timer is already stopped.

## Dependencies

### Internal
- Mirrors `include/nanosig/` and `demos/`.
- `plans/共识计划.md` and `specs/需求访谈.md` are the public source of truth for planning context.

### External
- None.

<!-- MANUAL: Any manually added notes below this line are preserved on regeneration -->
