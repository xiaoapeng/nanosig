# review 文档工具使用说明

`docs/review/AGENTS.md` 是规范，本文是 `cmake/scripts/review_docs.py` 的使用手册。
工具只依赖 Python 3 标准库，所有命令都从仓库根目录执行。

```sh
python3 cmake/scripts/review_docs.py --help
python3 cmake/scripts/review_docs.py <子命令> --help
```

## 1. 先理解数据模型

每个 `docs/review/*-code-review.md` 由两部分组成：

1. 文件头的 YAML frontmatter：结构化索引，`entries` 数组一条对应正文一个条目。
2. 正文三大分区：`## 修复历史`、`## 现在打开的问题`、`## 现在关闭的问题`。

**正文是唯一事实来源，frontmatter 是派生索引。** 不要手改 frontmatter 来"制造"状态；
正确做法是先改正文（或通过本工具改），再执行 `sync` 回填 frontmatter。

条目正文结构：

```markdown
<a id="RBTREE-016"></a>
### RBTREE-016: 标题

- **状态**: 打开
- **严重度**: 🟠 高
- **类型**: bug

#### 问题描述
...
#### review 建议
...
#### 作者建议
（待作者补充）
#### 可重现的失败场景
...
#### 定位
src/ds/ns_rbtree.c:607
include/nanosig/nanosig_rbtree.h:103
```

定位只允许三种写法，每行一条，不加反引号、不带 `./`：

```
src/ns_broker.c
src/ns_broker.c:459
src/ns_broker.c:459-470
```

frontmatter 中会派生为：

```yaml
    location: "docs/review/rbtree-code-review.md#RBTREE-016"
    locations:
      - "src/ds/ns_rbtree.c:607"
      - "include/nanosig/nanosig_rbtree.h:103"
```

## 2. 查询

### 列出条目

```sh
# 所有打开的高危 bug
python3 cmake/scripts/review_docs.py list --status open --severity high --type bug

# 某模块的所有条目
python3 cmake/scripts/review_docs.py list --module broker

# ID 支持 glob
python3 cmake/scripts/review_docs.py list --id 'BROKER-0*'

# 按源码位置过滤（子串匹配 locations）
python3 cmake/scripts/review_docs.py list --location platform/macos/port.c

# 时间区间
python3 cmake/scripts/review_docs.py list --closed-after 2026-07-01 --closed-before 2026-07-31

# 只看数量 / 分组 / JSON
python3 cmake/scripts/review_docs.py list --status open --count
python3 cmake/scripts/review_docs.py list --group-by severity
python3 cmake/scripts/review_docs.py list --status open --json
```

默认输出列：`ID 状态 严重度 类型 实际分区 文件`。

### 查看单条或多条

```sh
python3 cmake/scripts/review_docs.py show RBTREE-016
python3 cmake/scripts/review_docs.py show RBTREE-016 FORMATIO-021 --body-only
python3 cmake/scripts/review_docs.py show RBTREE-016 --json
```

`--body-only` 只输出条目正文，适合直接喂给修复 agent；`--json` 包含 frontmatter
字段、标题、文件、正文全文。

### 搜索、待办、统计

```sh
# 正文/标题正则搜索
python3 cmake/scripts/review_docs.py search 'SIGPIPE' --status open
python3 cmake/scripts/review_docs.py search 'ns_hashtbl' --section closed

# 按严重度列出待处理打开条目
python3 cmake/scripts/review_docs.py next --limit 20

# 全库统计（JSON）
python3 cmake/scripts/review_docs.py stats
```

### 解析定位

```sh
python3 cmake/scripts/review_docs.py locate RBTREE-016
```

输出每条 `locations`，并尽量给出所在函数/声明行和该行文本。
只做格式、文件存在性、行号范围检查：

```sh
python3 cmake/scripts/review_docs.py check-locations
python3 cmake/scripts/review_docs.py check-locations --json
```

## 3. 校验

```sh
python3 cmake/scripts/review_docs.py validate
python3 cmake/scripts/review_docs.py validate --json
```

检查内容：

- frontmatter 必填字段、枚举值、ID 格式与全局唯一性
- 三大分区存在、顺序正确、标题精确
- 正文状态与所在分区一致（打开分区不能放已关闭条目）
- 每个条目有 `<a id="ID"></a>` 锚点
- 打开条目必须有 `#### 定位`，且 `locations` 与正文一致
- 定位文件存在、行号不越界、不得使用反引号
- 关闭条目必须有 `关闭原因` 和 `关闭日期`
- 归档：`关闭-已拒绝` 永不归档；其他关闭条目满 30 天提示归档

退出码：`0` 无 error；`1` 有 error；`2` 参数或状态错误。warning 默认不影响退出码，
`--strict-archive` 会把"该归档但没归档"升级为 error。

CMake 目标与 CI：

```sh
cmake --build <build-dir> --target review-docs-check
```

CI 的 `linux-release` job 会执行同一个 `validate`；error 会让 PR 失败。

## 4. 写操作通用规则

- **默认 dry-run**：只打印将要发生的变更，加 `--apply` 才写文件。
- **先备份/在副本上试**：用 `--review-dir` 指向一份拷贝，确认后再对 `docs/review` 执行。

```sh
cp -R docs/review /tmp/review-copy
python3 cmake/scripts/review_docs.py --review-dir /tmp/review-copy close RBTREE-016 \
    --status closed-rejected --reason "合同前提保护" --force --apply
```

- 写命令会重新解析正文并同步 frontmatter，不需要手工补索引。
- `close` 默认要求 `#### 作者建议` 非空；作者建议为空时拒绝关闭，确认后加 `--force`。
- ID 重复（正文同一 ID 出现多次）时，`move` 会拒绝执行，先人工去重。

## 5. 新建条目

准备 JSON（`-` 表示从 stdin 读）：

```json
{
  "module": "rbtree",
  "title": "ns_rbtree_add 重复插入已链接节点",
  "severity": "high",
  "type": "bug",
  "description": "问题描述……",
  "review_suggestion": "修复建议……",
  "repro": "可复现步骤或代码……",
  "locations": ["src/ds/ns_rbtree.c:607", "include/nanosig/nanosig_rbtree.h:103"]
}
```

```sh
python3 cmake/scripts/review_docs.py add --json /tmp/new.json --apply
python3 cmake/scripts/review_docs.py validate
```

- `module` 取 `platform-ports`、`nanosig-core`、`global`、`rbtree` 等文件名前缀。
- 不传 `id` 时按模块前缀自动分配下一个三位编号（如 `RBTREE-019`）。
- 条目按严重度插入打开分区；自动生成锚点、状态、类型、作者建议占位和定位块。
- `severity`：`critical` / `high` / `medium` / `low`。
- `type`：`bug` / `doc` / `cleanup` / `design` / `test`。

## 6. 修改条目

```sh
# 改子分区内容
python3 cmake/scripts/review_docs.py edit RBTREE-016 --field description --text "新的描述" --apply
python3 cmake/scripts/review_docs.py edit RBTREE-016 --field review-suggestion --from /tmp/suggestion.md --apply
python3 cmake/scripts/review_docs.py edit RBTREE-016 --field repro --text "复现步骤" --apply

# 改元数据
python3 cmake/scripts/review_docs.py edit RBTREE-016 --field severity --value medium --apply
python3 cmake/scripts/review_docs.py edit RBTREE-016 --field type --value cleanup --apply

# 改源码定位（逗号分隔，会同步正文与 locations）
python3 cmake/scripts/review_docs.py edit RBTREE-016 --field locations \
    --value "src/ds/ns_rbtree.c:607-620,include/nanosig/nanosig_rbtree.h:103" --apply
```

可选字段：`description`、`review-suggestion`、`author-suggestion`、`repro`、`locations`、
`severity`、`type`。

## 7. 关闭、重开、移动

```sh
# 已修复：关闭并写修复历史（推荐）
python3 cmake/scripts/review_docs.py record-fix RBTREE-016 \
    --commit abc1234 --summary "补充已链接节点守卫" --apply

# 拒绝 / 延期 / 不适用
python3 cmake/scripts/review_docs.py close RBTREE-016 \
    --status closed-rejected --reason "合同前提保护，调用方保证不重复插入" \
    --date 2026-10-20 --force --apply

# 重新打开（清掉关闭字段，回到打开分区）
python3 cmake/scripts/review_docs.py reopen RBTREE-016 --apply

# 只移动分区，不改字段
python3 cmake/scripts/review_docs.py move RBTREE-016 --to closed --apply
```

`close` 的状态：`closed-fixed` / `closed-rejected` / `closed-not-applicable` / `closed-deferred`。
`record-fix` 等价于关闭为 `closed-fixed`，并在修复历史追加一条记录。
`close` 的 `--date` 省略时使用当天日期。

## 8. 同步、规范化、归档

```sh
# 以正文为准回填 frontmatter（dry-run 会列出每个字段变化）
python3 cmake/scripts/review_docs.py sync
python3 cmake/scripts/review_docs.py sync --apply

# 同时删除正文已不存在的孤儿 frontmatter 记录
python3 cmake/scripts/review_docs.py sync --apply --prune

# 补锚点、规范三大分区标题、加粗元数据、规范化定位写法
python3 cmake/scripts/review_docs.py normalize
python3 cmake/scripts/review_docs.py normalize --apply

# 归档关闭满 30 天的条目（默认移动到 docs/review/history/<模块>/）
python3 cmake/scripts/review_docs.py archive
python3 cmake/scripts/review_docs.py archive --before 2026-09-20 --apply
```

归档规则：

- 只归档 `## 现在关闭的问题` 分区里的条目。
- `关闭-已拒绝` 永不归档，永久保留在主文件。
- 修复历史是 append-only，不会被归档。
- 同一模块同月写入 `<模块>-archive-<年>-<月>.md`。

## 9. 批量操作

`ops.jsonl` 每行一个 JSON 操作，按顺序执行；任一操作失败会报错退出。

```jsonl
{"op":"record-fix","id":"RBTREE-016","commit":"abc1234","summary":"补守卫","force":true}
{"op":"close","id":"RBTREE-017","status":"closed-rejected","reason":"误报","force":true}
{"op":"edit","id":"RBTREE-018","field":"severity","value":"low"}
{"op":"reopen","id":"RBTREE-019"}
```

```sh
python3 cmake/scripts/review_docs.py batch --file /tmp/ops.jsonl
python3 cmake/scripts/review_docs.py validate
```

支持的 `op`：`close`、`reopen`、`edit`、`record-fix`。批量执行始终落盘，执行前请先
在 `--review-dir` 副本上跑一遍。

## 10. 典型工作流

### 新发现一个问题

```sh
# 1. 写 JSON
# 2. 新建
python3 cmake/scripts/review_docs.py add --json /tmp/new.json --apply
# 3. 校验
python3 cmake/scripts/review_docs.py validate
```

### 修复一个问题

```sh
# 1. 确认作者建议已填写
python3 cmake/scripts/review_docs.py show RBTREE-016 --body-only
# 2. 改代码、跑测试
# 3. 关闭并记录 commit
python3 cmake/scripts/review_docs.py record-fix RBTREE-016 --commit abc1234 --summary "..." --apply
# 4. 校验
python3 cmake/scripts/review_docs.py validate
```

### 作者拒绝一个问题

```sh
python3 cmake/scripts/review_docs.py edit RBTREE-016 --field author-suggestion \
    --text "该行为已在头文件文档化，属设计权衡。" --apply
python3 cmake/scripts/review_docs.py close RBTREE-016 --status closed-rejected \
    --reason "文档仲裁：行为已文档化" --force --apply
```

### 月度归档

```sh
python3 cmake/scripts/review_docs.py archive --before "$(date -v-30d +%F)" --apply
python3 cmake/scripts/review_docs.py validate
```

（Linux 用 `date -d '30 days ago' +%F`。）

## 11. 常见问题

**Q: 改了正文，frontmatter 没变？**
执行 `sync --apply`。

**Q: `close` 报"作者建议为空"？**
按规范，作者建议为空时修复 agent 不得关闭。让作者补 `author-suggestion`；确认后加 `--force`。

**Q: `move` 报"正文出现 N 次"？**
同一 ID 在多个分区重复。先保留实质条目、删除空桩，再移动。

**Q: 定位检查失败？**
按 `path` / `path:line` / `path:start-end` 重写，去掉反引号与 `./`，再 `normalize --apply`。
文件路径必须与仓库实际大小写一致（Linux CI 区分大小写）。

**Q: 想在不影响仓库的情况下试写命令？**
先 `cp -R docs/review /tmp/review-copy`，所有命令加 `--review-dir /tmp/review-copy`。

**Q: 归档后原文件里还有 `关闭-已拒绝`？**
正常。拒绝项不归档，永久保留。

**Q: CI 的 `review-docs-check` 失败？**
本地跑 `python3 cmake/scripts/review_docs.py validate --json`，按 `rule` 字段定位问题。
