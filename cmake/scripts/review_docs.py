#!/usr/bin/env python3
"""review_docs.py — docs/review 下 review 文档的查询、校验与编辑接口。

只依赖 Python 3 标准库。frontmatter 与正文三大分区都会被解析，因此能发现
"元数据说关闭、正文还在打开分区"这类漂移。

用法:
  review_docs.py list [--status S] [--module M] [--severity S] [--type T]
                      [--section SEC] [--id GLOB] [--text REGEX]
                      [--location PATH] [--opened-after D] [--opened-before D]
                      [--closed-after D] [--closed-before D]
                      [--sort FIELD] [--group-by FIELD] [--count] [--limit N]
                      [--json]
  review_docs.py show <ID> [<ID> ...] [--body-only] [--json]
  review_docs.py search <REGEX> [--section SEC] [--status S] [--json]
  review_docs.py next [--limit N] [--json]
  review_docs.py stats [--json]
  review_docs.py validate [--json] [--strict-archive]
  review_docs.py locate <ID>
  review_docs.py check-locations [--json]

  review_docs.py sync [--apply] [--prune] [--json]
  review_docs.py normalize [--apply]
  review_docs.py close <ID> --status {closed-fixed,closed-rejected,closed-not-applicable,closed-deferred}
                      --reason TEXT [--date YYYY-MM-DD] [--force] [--apply]
  review_docs.py reopen <ID> [--apply]
  review_docs.py move <ID> --to {open,closed,fixed-history} [--apply]
  review_docs.py add --json FILE|- [--apply]
  review_docs.py edit <ID> --field FIELD [--text T | --from FILE | --value V] [--apply]
  review_docs.py record-fix <ID> --commit SHA [--summary S] [--date D] [--force] [--apply]
  review_docs.py archive [--before YYYY-MM-DD] [--apply]
  review_docs.py batch --file ops.jsonl

写命令默认只打印将要发生的变更；加 --apply 才写回文件。
退出码: 0 = 成功; 1 = validate 有 error; 2 = 参数/状态错误。
"""

import argparse
import fnmatch
import json
import os
import re
import sys
from datetime import date

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REVIEW_DIR = os.path.join(REPO, "docs", "review")

SCHEMA = "nanosig-review-v1"
STATUSES = {"open", "closed-fixed", "closed-rejected", "closed-not-applicable",
            "closed-deferred", "unknown"}
SEVERITIES = {"critical", "high", "medium", "low", "unknown"}
TYPES = {"bug", "doc", "cleanup", "design", "test", "unknown"}
SECTIONS = {"open", "closed", "fixed-history"}
REQUIRED = ("id", "module", "status", "severity", "type", "section", "location")
ARCHIVE_DAYS = 30
KEY_ORDER = ("id", "module", "status", "severity", "type", "section",
             "opened_date", "closed_date", "location", "locations")

SECTION_HEADINGS = (
    ("## 修复历史", "fixed-history"),
    ("## 现在打开的问题", "open"),
    ("## 现在关闭的问题", "closed"),
)

ID_RE = re.compile(r"\b([A-Z][A-Z0-9]*-\d+)\b")
HEADING_RE = re.compile(r"^###\s+(.+?)\s*$")
FENCE_RE = re.compile(r"^(```|~~~)")

BODY_STATUS = {
    "打开": "open",
    "关闭-已修复": "closed-fixed",
    "关闭-已拒绝": "closed-rejected",
    "关闭-延期": "closed-deferred",
    "关闭-不适用": "closed-not-applicable",
}
CLOSED_TEXT = {value: key for key, value in BODY_STATUS.items()}
SEVERITY_RANK = {"critical": 4, "high": 3, "medium": 2, "low": 1, "unknown": 0}


def _unquote(value):
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1]
    return value


def module_of(filename):
    return re.sub(r"(-code-review)?\.md$", "", filename)


def parse_frontmatter(text):
    """返回 (meta, body)。没有 frontmatter 时 meta 为 None。"""
    lines = text.splitlines()
    if not lines or lines[0].strip() != "---":
        return None, text
    end = None
    for i in range(1, len(lines)):
        if lines[i].strip() == "---":
            end = i
            break
    if end is None:
        return None, text

    meta = {"entries": []}
    current = None
    current_key = None
    for line in lines[1:end]:
        m = re.match(r"^\s*-\s+([A-Za-z_][\w-]*):\s*(.*)$", line)
        if m:
            current = {}
            meta["entries"].append(current)
            current[m.group(1)] = _unquote(m.group(2))
            current_key = m.group(1)
            continue
        m = re.match(r"^\s+([A-Za-z_][\w-]*):\s*(.*)$", line)
        if m and current is not None:
            current_key = m.group(1)
            value = _unquote(m.group(2))
            if value:
                current[current_key] = value
            else:
                current[current_key] = []
            continue
        m = re.match(r"^\s+-\s+(.*)$", line)
        if m and current is not None and isinstance(current.get(current_key), list):
            current[current_key].append(_unquote(m.group(1)))
            continue
        m = re.match(r"^([A-Za-z_][\w-]*):\s*(.*)$", line)
        if m and m.group(1) != "entries":
            meta[m.group(1)] = _unquote(m.group(2))
    return meta, "\n".join(lines[end + 1:])


def parse_body(body):
    """按三大分区切出条目块，返回 {id: {section, title, text}}。"""
    entries = {}
    section = None
    in_fence = False
    current = None

    def flush():
        nonlocal current
        if current is not None:
            lines = current["lines"]
            while lines and not lines[-1].strip():
                lines.pop()
            while lines and (lines[-1].strip() == "---"
                             or lines[-1].strip().startswith('<a id="')):
                lines.pop()
                while lines and not lines[-1].strip():
                    lines.pop()
            entries[current["ids"][0]]["text"] = "\n".join(lines).strip()
            current = None

    for line in body.splitlines():
        if FENCE_RE.match(line.strip()):
            in_fence = not in_fence
        if not in_fence:
            m = re.match(r"^##\s+(.+?)\s*$", line)
            if m:
                flush()
                title = m.group(1)
                section = None
                for prefix, name in SECTION_HEADINGS:
                    if title.startswith(prefix[len("## "):]):
                        section = name
                        break
                continue
            m = HEADING_RE.match(line)
            if m and section is not None:
                flush()
                ids = ID_RE.findall(m.group(1).split(":", 1)[0])
                if ids:
                    title = m.group(1)
                    if ":" in title:
                        title = title.split(":", 1)[1].strip()
                    priority = {"fixed-history": 0, "open": 1, "closed": 2}
                    for entry_id in ids:
                        known = entries.get(entry_id)
                        if known is None or priority[section] > priority[known["section"]]:
                            entries[entry_id] = {
                                "id": entry_id, "section": section,
                                "title": title, "text": "",
                            }
                    current = {"ids": ids, "lines": []}
                    continue
                current = None
        if current is not None:
            current["lines"].append(line)
    flush()
    return entries


def parse_structure(text):
    """行级结构：分区范围与条目块范围（用于写操作）。"""
    lines = text.splitlines()
    fm = None
    if lines and lines[0].strip() == "---":
        for i in range(1, len(lines)):
            if lines[i].strip() == "---":
                fm = (0, i)
                break
    body_start = (fm[1] + 1) if fm else 0

    sections = {}
    order = []
    for i in range(body_start, len(lines)):
        m = re.match(r"^##\s+(.+?)\s*$", lines[i])
        if not m:
            continue
        name = None
        for prefix, section in SECTION_HEADINGS:
            if m.group(1).startswith(prefix[len("## "):]):
                name = section
        if name:
            if order:
                sections[order[-1]]["end"] = i
            sections[name] = {"head": i, "end": len(lines)}
            order.append(name)

    starts = []
    section = None
    for i in range(body_start, len(lines)):
        heading = re.match(r"^##\s+(.+?)\s*$", lines[i])
        if heading:
            section = None
            for prefix, name in SECTION_HEADINGS:
                if heading.group(1).startswith(prefix[len("## "):]):
                    section = name
            continue
        m = HEADING_RE.match(lines[i])
        if not m or section is None:
            continue
        ids = ID_RE.findall(m.group(1).split(":", 1)[0])
        if not ids:
            continue
        start = i
        if (i - 1 >= body_start and lines[i - 1].strip().startswith('<a id="')
                and lines[i - 1].strip().endswith("</a>")):
            start = i - 1
        starts.append({"start": start, "heading": i, "ids": ids, "section": section})

    for index, entry in enumerate(starts):
        end = sections.get(entry["section"], {}).get("end", len(lines))
        if index + 1 < len(starts):
            end = min(end, starts[index + 1]["start"])
        entry["end"] = end
    return {"lines": lines, "fm": fm, "sections": sections,
            "order": order, "entries": starts}


def load_docs():
    docs = []
    if not os.path.isdir(REVIEW_DIR):
        return docs
    for name in sorted(os.listdir(REVIEW_DIR)):
        if not name.endswith(".md") or name in ("AGENTS.md", "README.md"):
            continue
        path = os.path.join(REVIEW_DIR, name)
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        meta, body = parse_frontmatter(text)
        docs.append({
            "file": name,
            "path": path,
            "text": text,
            "body": body,
            "meta": meta or {"entries": []},
            "entries": parse_body(body),
        })
    return docs


def flatten(docs):
    rows = []
    for doc in docs:
        fm = {}
        for entry in doc["meta"].get("entries", []):
            fm.setdefault(entry.get("id"), entry)
        for entry_id, body_entry in doc["entries"].items():
            row = dict(fm.get(entry_id) or {})
            row.setdefault("id", entry_id)
            row.setdefault("module", module_of(doc["file"]))
            row.setdefault("status", "unknown")
            row.setdefault("severity", "unknown")
            row.setdefault("type", "unknown")
            row["section_actual"] = body_entry["section"]
            row["title"] = body_entry["title"]
            row["file"] = doc["file"]
            row["text"] = body_entry["text"]
            rows.append(row)
    return rows


def filter_rows(rows, args):
    for key in ("status", "module", "severity", "type"):
        want = getattr(args, key, None)
        if want:
            rows = [row for row in rows if row.get(key) == want]
    section = getattr(args, "section", None)
    if section:
        rows = [row for row in rows if row.get("section_actual") == section]
    pattern = getattr(args, "id", None)
    if pattern:
        rows = [row for row in rows if fnmatch.fnmatch(row["id"], pattern)]
    text = getattr(args, "text", None)
    if text:
        regex = re.compile(text)
        rows = [row for row in rows
                if regex.search(row["id"] + "\n" + row["title"] + "\n" + row["text"])]
    location = getattr(args, "location", None)
    if location:
        rows = [row for row in rows
                if any(location in loc for loc in (row.get("locations") or []))]
    for key in ("opened", "closed"):
        after = getattr(args, f"{key}_after", None)
        before = getattr(args, f"{key}_before", None)
        if not after and not before:
            continue
        field = f"{key}_date"
        kept = []
        for row in rows:
            value = parse_date(row.get(field))
            if value is None:
                continue
            if after and value < parse_date(after):
                continue
            if before and value > parse_date(before):
                continue
            kept.append(row)
        rows = kept
    return rows


def sort_rows(rows, field):
    if field == "severity":
        return sorted(rows, key=lambda r: (-SEVERITY_RANK.get(r.get("severity"), 0), r["id"]))
    if field == "status":
        return sorted(rows, key=lambda r: (r.get("status", ""), r["id"]))
    if field in ("opened_date", "closed_date"):
        return sorted(rows, key=lambda r: (r.get(field) or "", r["id"]), reverse=True)
    return sorted(rows, key=lambda r: (r.get("module", ""), r["id"]))


def parse_date(value):
    try:
        return date.fromisoformat(value)
    except (TypeError, ValueError):
        return None


def validate(docs, strict_archive=False):
    problems = []

    def add(level, file, entry_id, rule, message):
        problems.append({"level": level, "file": file, "id": entry_id,
                         "rule": rule, "message": message})

    seen_ids = {}
    for doc in docs:
        name = doc["file"]
        meta = doc["meta"]
        if not meta.get("entries") and "review_schema" not in meta:
            add("error", name, "-", "frontmatter", "文件缺少 YAML frontmatter")
            continue
        if meta.get("review_schema") != SCHEMA:
            add("error", name, "-", "frontmatter",
                f"review_schema 应为 {SCHEMA!r}，实际 {meta.get('review_schema')!r}")

        headings = [line for line in doc["text"].splitlines() if line.startswith("## ")]
        order = []
        for line in headings:
            for prefix, section in SECTION_HEADINGS:
                if line[len("## "):].startswith(prefix[len("## "):]):
                    if line.strip() != prefix:
                        add("warn", name, "-", "section-heading",
                            f"分区标题应精确为 {prefix!r}，实际 {line.strip()!r}")
                    order.append(section)
                    break
        if order.count("fixed-history") != 1 or order.count("open") != 1 or order.count("closed") != 1:
            add("error", name, "-", "section-heading",
                f"三个分区应各出现一次，实际 {order}")
        if order != ["fixed-history", "open", "closed"]:
            add("error", name, "-", "section-order",
                f"分区顺序应为 修复历史→打开→关闭，实际 {order}")

        fm_ids = []
        anchored = {}
        for item in parse_structure(doc["text"])["entries"]:
            for item_id in item["ids"]:
                anchored[item_id] = item["start"] < item["heading"]
        for entry in meta.get("entries", []):
            entry_id = entry.get("id", "-")
            if not re.match(r"^[A-Z][A-Z0-9]*-\d{3}$", entry_id):
                add("error", name, entry_id, "id-format",
                    "ID 格式应为 <模块缩写>-<三位数字>")
            if entry_id in seen_ids:
                add("error", name, entry_id, "duplicate-id",
                    f"ID 与 {seen_ids[entry_id]} 重复")
            else:
                seen_ids[entry_id] = name
            fm_ids.append(entry_id)
            for key in REQUIRED:
                if not entry.get(key):
                    add("error", name, entry_id, "missing-field", f"缺少字段 {key}")
            if entry.get("status") not in STATUSES:
                add("error", name, entry_id, "enum-status",
                    f"status 非法: {entry.get('status')!r}")
            if entry.get("severity") not in SEVERITIES:
                add("error", name, entry_id, "enum-severity",
                    f"severity 非法: {entry.get('severity')!r}")
            if entry.get("type") not in TYPES:
                add("error", name, entry_id, "enum-type",
                    f"type 非法: {entry.get('type')!r}")
            if entry.get("section") not in SECTIONS:
                add("error", name, entry_id, "enum-section",
                    f"section 非法: {entry.get('section')!r}")

            body_entry = doc["entries"].get(entry_id)
            if body_entry is None:
                add("error", name, entry_id, "missing-body",
                    "frontmatter 有条目但正文无标题")
                continue
            body_text = body_entry["text"]
            if entry.get("section") and entry["section"] != body_entry["section"]:
                add("error", name, entry_id, "section-mismatch",
                    f"section 写 {entry['section']!r}，实际在 {body_entry['section']!r} 分区")

            expected_location = f"docs/review/{name}#{entry_id}"
            if entry.get("location") != expected_location:
                add("warn", name, entry_id, "location-format",
                    f"location 应为 {expected_location!r}，实际 {entry.get('location')!r}")

            locations = entry.get("locations")
            if locations is not None and not isinstance(locations, list):
                add("error", name, entry_id, "location-type", "locations 应为列表")
                locations = None
            if isinstance(locations, list):
                for loc in locations:
                    if not LOCATION_RE.match(loc):
                        add("error", name, entry_id, "location-format",
                            f"locations 写法不规范: {loc!r}")
                        continue
                    loc_path, loc_start, loc_end = location_line_count(loc)
                    full = os.path.join(REPO, loc_path)
                    if not os.path.isfile(full):
                        add("error", name, entry_id, "location-missing",
                            f"定位文件不存在: {loc_path}")
                        continue
                    if loc_start and loc_end:
                        total = sum(1 for _ in open(full, encoding="utf-8", errors="ignore"))
                        if loc_start < 1 or loc_end < loc_start or loc_end > total:
                            add("error", name, entry_id, "location-range",
                                f"行号越界: {loc}（文件 {total} 行）")
            loc_block = re.search(r"^#### 定位\s*$(.*?)(?=^#### |^---\s*$|^### |^## |\Z)",
                                  body_entry["text"], re.S | re.M)
            if loc_block and "`" in loc_block.group(1):
                add("error", name, entry_id, "location-backtick",
                    "定位 行不得使用反引号")
            body_locations = extract_locations(body_text)
            if body_locations and isinstance(locations, list) and body_locations != locations:
                add("error", name, entry_id, "location-drift",
                    "frontmatter locations 与正文 定位 不一致")
            if body_entry["section"] == "open" and not body_locations:
                add("error", name, entry_id, "location-empty",
                    "条目缺少可解析的 定位 内容")
            if not anchored.get(entry_id, False):
                add("error", name, entry_id, "missing-anchor",
                    f"条目缺少 <a id=\"{entry_id}\"></a> 锚点")

            status = entry.get("status", "")
            closed = status.startswith("closed-")
            if closed and not entry.get("closed_date") and entry.get("section") == "closed":
                add("error", name, entry_id, "missing-closed-date",
                    "关闭条目缺少 closed_date")
            if not closed and entry.get("closed_date"):
                add("error", name, entry_id, "unexpected-closed-date",
                    "未关闭条目不应有 closed_date")

            text = body_entry["text"]
            if body_entry["section"] == "open":
                if not re.search(r"^- \*\*状态\*\*:\s*打开", text, re.M):
                    add("error", name, entry_id, "body-status",
                        "打开分区条目的正文状态不是“打开”")
                for section_title in ("问题描述", "review 建议", "可重现的失败场景", "定位"):
                    if f"#### {section_title}" not in text:
                        add("error", name, entry_id, "missing-subsection",
                            f"打开条目缺少 #### {section_title}")
                # 过渡期：影响面（调用者清单）先以 warning 提示，补齐后升级为 error。
                if "#### 影响面" not in text:
                    add("warning", name, entry_id, "missing-impact",
                        "打开条目缺少 #### 影响面（见 AGENTS.md §2.2）")
            elif body_entry["section"] == "closed":
                if not re.search(r"^- \*\*状态\*\*:\s*关闭-", text, re.M):
                    add("error", name, entry_id, "body-status",
                        "关闭分区条目的正文状态不是“关闭-*”")
                for field in ("关闭原因", "关闭日期"):
                    if (f"**{field}**" not in text
                            and f"#### {field}" not in text):
                        add("error", name, entry_id, "missing-subsection",
                            f"关闭条目缺少 {field}")

            if (closed and status != "closed-rejected"
                    and entry.get("section") == "closed" and entry.get("closed_date")):
                closed_on = parse_date(entry["closed_date"])
                if closed_on is None:
                    add("error", name, entry_id, "closed-date-format",
                        f"closed_date 非法: {entry['closed_date']!r}")
                else:
                    age = (date.today() - closed_on).days
                    if age > ARCHIVE_DAYS:
                        add("warn" if not strict_archive else "error", name, entry_id,
                            "archive-overdue",
                            f"已关闭 {age} 天（> {ARCHIVE_DAYS}），应归档到 docs/review/history/")

        for entry_id in doc["entries"]:
            if entry_id not in fm_ids:
                add("error", name, entry_id, "missing-frontmatter",
                    "正文有条目但 frontmatter 无对应记录")

    return problems


LOCATION_RE = re.compile(
    r"^(?:src|include|platform|test|demos|cmake|docs)/[A-Za-z0-9_.\-/]+"
    r"(?::(\d+)(?:-(\d+))?)?$")


def canonical_location(line):
    value = line.replace("`", "").strip().lstrip("./")
    match = re.match(
        r"((?:src|include|platform|test|demos|cmake|docs)/[A-Za-z0-9_.\-/]+"
        r"(?::\d+(?:-\d+)?)?)", value)
    return match.group(1) if match else value.rstrip("，。；,;")


def extract_locations(text):
    """从 #### 定位 子分区提取规范化位置列表。"""
    match = re.search(r"^#### 定位\s*$(.*?)(?=^#### |^---\s*$|^### |^## |\Z)",
                      text, re.S | re.M)
    if not match:
        return []
    result = []
    for raw in match.group(1).splitlines():
        line = canonical_location(raw)
        if line and LOCATION_RE.match(line) and line not in result:
            result.append(line)
    return result


def location_line_count(location):
    path, _, span = location.partition(":")
    start = int(span.split("-", 1)[0]) if span else None
    end = int(span.split("-", 1)[1]) if "-" in span else start
    return path, start, end


def body_meta(text, section, prev):
    """从正文提取状态/严重度/类型/关闭日期。"""
    result = {}
    m = re.search(r"^- \*\*状态\*\*:\s*(\S+)\s*$", text, re.M)
    if m:
        result["status"] = BODY_STATUS.get(m.group(1), "unknown")
    elif section == "fixed-history":
        result["status"] = "closed-fixed"

    m = re.search(r"^- \*\*严重度\*\*:\s*(.+)$", text, re.M)
    if m:
        label = m.group(1)
        for token, severity in (("关键", "critical"), ("较低", "low"), ("高", "high"),
                                ("中", "medium"), ("低", "low")):
            if token in label:
                result["severity"] = severity
                break

    m = re.search(r"^- \*\*类型\*\*:\s*(\w+)", text, re.M)
    if m and m.group(1) in TYPES:
        result["type"] = m.group(1)

    m = re.search(r"关闭日期\*{0,2}\s*[:：]?\s*(\d{4}-\d{2}-\d{2})", text)
    if m:
        result["closed_date"] = m.group(1)
    return result


def sync_meta(doc):
    """以正文为准推导 frontmatter，返回 (entries, changes, orphans)。"""
    existing = {}
    order = []
    for entry in doc["meta"].get("entries", []):
        entry_id = entry.get("id")
        if not entry_id or entry_id in existing:
            continue
        existing[entry_id] = dict(entry)
        order.append(entry_id)

    desired = {}
    structure = parse_structure(doc["text"])
    body_entries = structure["entries"]
    for body_entry in body_entries:
        section = body_entry["section"]
        body_text = "\n".join(structure["lines"][body_entry["start"]:body_entry["end"]])
        for entry_id in body_entry["ids"]:
            prev = existing.get(entry_id, {})
            row = dict(prev)
            row["id"] = entry_id
            row["module"] = module_of(doc["file"])
            info = body_meta(body_text, section, prev)

            if section == "open":
                info.setdefault("status", "open")
                info["section"] = "open"
                info.pop("closed_date", None)
            elif section == "closed":
                info["section"] = "closed"
            else:
                info["section"] = "fixed-history"
                info.setdefault("status", "closed-fixed")

            row.update(info)
            if section == "open":
                row.pop("closed_date", None)
            locations = extract_locations(body_text)
            if locations:
                row["locations"] = locations
            else:
                row.pop("locations", None)
            row.setdefault("severity", "unknown")
            row.setdefault("type", "unknown")
            row["location"] = f"docs/review/{doc['file']}#{entry_id}"
            desired[entry_id] = row

    body_ids = set(ID_RE.findall(doc["body"]))
    orphans = [entry_id for entry_id in order
               if entry_id not in doc["entries"] and entry_id not in body_ids]
    changes = []
    for entry_id, row in desired.items():
        before = existing.get(entry_id, {})
        for key in KEY_ORDER:
            if before.get(key) != row.get(key):
                changes.append({"id": entry_id, "field": key,
                                "before": before.get(key), "after": row.get(key)})

    merged = [desired[entry_id] for entry_id in order if entry_id in desired]
    merged_ids = {row["id"] for row in merged}
    merged.extend(row for entry_id, row in desired.items() if entry_id not in merged_ids)
    return merged, changes, orphans


def render_frontmatter(entries):
    out = ["---", f'review_schema: "{SCHEMA}"', "entries:"]
    for entry in entries:
        keys = [key for key in KEY_ORDER if entry.get(key)]
        keys += sorted(key for key in entry
                       if key not in KEY_ORDER and entry.get(key))
        for index, key in enumerate(keys):
            prefix = "  - " if index == 0 else "    "
            value = entry[key]
            if isinstance(value, list):
                out.append(f"{prefix}{key}:")
                for item in value:
                    out.append(f'      - "{item}"')
            else:
                out.append(f'{prefix}{key}: "{value}"')
    out.append("---")
    return out


def assemble(frontmatter_lines, body):
    body = body.lstrip("\n")
    return "\n".join(frontmatter_lines) + "\n\n" + body.rstrip("\n") + "\n"


def write_text(path, text):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as handle:
        handle.write(text)
    os.replace(tmp, path)


def reload_doc(doc, new_text):
    meta, body = parse_frontmatter(new_text)
    return {"file": doc["file"], "path": doc["path"], "text": new_text,
            "body": body, "meta": meta or {"entries": []},
            "entries": parse_body(body)}


def resync_text(doc, new_text):
    """正文变化后重算 frontmatter 并拼回完整文本。"""
    fresh = reload_doc(doc, new_text)
    meta, _, _ = sync_meta(fresh)
    return assemble(render_frontmatter(meta), fresh["body"])


def find_structure_entry(text, entry_id):
    for entry in parse_structure(text)["entries"]:
        if entry_id in entry["ids"]:
            return entry
    return None


def get_block(text, entry_id):
    entry = find_structure_entry(text, entry_id)
    if entry is None:
        return None
    block = parse_structure(text)["lines"][entry["start"]:entry["end"]]
    while block and not block[-1].strip():
        block.pop()
    while block and block[-1].strip() == "---":
        block.pop()
    while block and not block[-1].strip():
        block.pop()
    return block


def remove_entry_text(text, entry_id):
    structure = parse_structure(text)
    entry = next((e for e in structure["entries"] if entry_id in e["ids"]), None)
    if entry is None:
        return text
    lines = structure["lines"][:]
    start, end = entry["start"], entry["end"]
    while end < len(lines) and lines[end].strip() in ("", "---"):
        end += 1
    del lines[start:end]
    out = []
    blanks = 0
    for line in lines:
        blanks = blanks + 1 if not line.strip() else 0
        if blanks <= 2:
            out.append(line)
    return "\n".join(out) + "\n"


def insert_block_text(text, section, block):
    structure = parse_structure(text)
    section_info = structure["sections"].get(section)
    if section_info is None:
        raise ValueError(f"找不到分区 {section}")
    lines = structure["lines"][:]
    index = section_info["end"]
    floor = section_info["head"] + 1
    while index > floor and not lines[index - 1].strip():
        index -= 1
    if index > floor and lines[index - 1].strip() == "---":
        index -= 1
        while index > floor and not lines[index - 1].strip():
            index -= 1
    lines[index:index] = [""] + list(block) + ["", "---"]
    return "\n".join(lines) + "\n"


def mark_status(block, status_text):
    out = []
    for line in block:
        if re.match(r"^- \*\*状态\*\*:", line):
            out.append(f"- **状态**: {status_text}")
        else:
            out.append(line)
    return out


def add_closed_fields(block, reason, day):
    out = list(block)
    indices = [i for i, line in enumerate(out) if line.startswith("- **")]
    insert_at = (max(indices) + 1) if indices else 1
    out[insert_at:insert_at] = [f"- **关闭原因**: {reason}",
                                f"- **关闭日期**: {day}"]
    return out


def strip_closed_fields(block):
    out = []
    skip = False
    for line in block:
        if re.match(r"^- \*\*状态\*\*:", line):
            out.append("- **状态**: 打开")
            continue
        if re.match(r"^- \*\*(关闭原因|关闭日期)\*\*:", line):
            continue
        if re.match(r"^#### 关闭(原因|日期)\s*$", line):
            skip = True
            continue
        if skip:
            if re.match(r"^(#### |### |## )", line):
                skip = False
            else:
                continue
        out.append(line)
    return out


def cmd_list(args):
    rows = filter_rows(flatten(load_docs()), args)
    rows = sort_rows(rows, args.sort)
    if args.limit:
        rows = rows[:args.limit]
    if args.count:
        print(len(rows))
        return 0
    if args.json:
        payload = [{k: v for k, v in row.items() if k != "text"} for row in rows]
        print(json.dumps(payload, ensure_ascii=False, indent=2))
        return 0
    if args.group_by:
        groups = {}
        for row in rows:
            if args.group_by == "section":
                key = row.get("section_actual") or "-"
            else:
                key = row.get(args.group_by) or "-"
            groups.setdefault(key, []).append(row)
        for key in sorted(groups):
            print(f"\n## {key} ({len(groups[key])})")
            for row in groups[key]:
                print(f"  {row['id']:14s} {row['status']:22s} {row['severity']:8s} {row['file']}")
        return 0
    for row in rows:
        print(f"{row['id']:14s} {row['status']:22s} {row['severity']:8s} "
              f"{row['type']:8s} {row['section_actual']:12s} {row['file']}")
    return 0


def cmd_show(args):
    rows = {row["id"]: row for row in flatten(load_docs())}
    missing = [entry_id for entry_id in args.ids if entry_id not in rows]
    if missing:
        print(f"未找到条目: {', '.join(missing)}", file=sys.stderr)
        return 2
    if args.json:
        payload = [rows[entry_id] for entry_id in args.ids]
        print(json.dumps(payload if len(payload) > 1 else payload[0],
                         ensure_ascii=False, indent=2))
        return 0
    for index, entry_id in enumerate(args.ids):
        row = rows[entry_id]
        if index:
            print("\n" + "=" * 72 + "\n")
        if args.body_only:
            print(row["text"])
            continue
        print(f"# {entry_id}: {row['title']}\n")
        print(row["text"])
    return 0


def cmd_search(args):
    regex = re.compile(args.pattern)
    rows = flatten(load_docs())
    if args.section:
        rows = [row for row in rows if row["section_actual"] == args.section]
    if args.status:
        rows = [row for row in rows if row["status"] == args.status]
    hits = []
    for row in rows:
        haystack = row["id"] + "\n" + row["title"] + "\n" + row["text"]
        matches = regex.findall(haystack)
        if matches:
            lines = [line.strip() for line in haystack.splitlines()
                     if regex.search(line)][:3]
            hits.append({"id": row["id"], "file": row["file"],
                         "status": row["status"], "section": row["section_actual"],
                         "title": row["title"], "lines": lines})
    if args.json:
        print(json.dumps(hits, ensure_ascii=False, indent=2))
        return 0
    for hit in hits:
        print(f"{hit['id']:14s} {hit['status']:22s} {hit['section']:12s} {hit['file']}")
        for line in hit["lines"]:
            print(f"    {line}")
    print(f"\n{len(hits)} hit(s)")
    return 0


def cmd_next(args):
    rows = [row for row in flatten(load_docs()) if row["status"] == "open"]
    rows = sort_rows(rows, "severity")[:args.limit]
    if args.json:
        print(json.dumps([{k: v for k, v in row.items() if k != "text"} for row in rows],
                         ensure_ascii=False, indent=2))
        return 0
    for index, row in enumerate(rows, 1):
        print(f"{index:2d}. {row['id']:14s} {row['severity']:8s} {row['title']}")
        print(f"    {row['file']}")
    return 0


def cmd_stats(args):
    rows = flatten(load_docs())
    stats = {"total": len(rows), "by_status": {}, "by_module": {},
             "by_severity": {}, "by_section": {}, "open_by_module": {}}
    for row in rows:
        stats["by_status"][row["status"]] = stats["by_status"].get(row["status"], 0) + 1
        stats["by_module"][row["module"]] = stats["by_module"].get(row["module"], 0) + 1
        stats["by_severity"][row["severity"]] = stats["by_severity"].get(row["severity"], 0) + 1
        stats["by_section"][row["section_actual"]] = stats["by_section"].get(row["section_actual"], 0) + 1
        if row["status"] == "open":
            stats["open_by_module"][row["module"]] = \
                stats["open_by_module"].get(row["module"], 0) + 1
    print(json.dumps(stats, ensure_ascii=False, indent=2))
    return 0


def cmd_validate(args):
    problems = validate(load_docs(), args.strict_archive)
    if args.json:
        print(json.dumps(problems, ensure_ascii=False, indent=2))
    else:
        for problem in problems:
            print(f"[{problem['level']}] {problem['file']}:{problem['id']} "
                  f"({problem['rule']}) {problem['message']}")
        errors = sum(1 for problem in problems if problem["level"] == "error")
        warns = len(problems) - errors
        print(f"\n{errors} error(s), {warns} warning(s)")
    return 1 if any(problem["level"] == "error" for problem in problems) else 0


def cmd_sync(args):
    total_changes = 0
    for doc in load_docs():
        meta, changes, orphans = sync_meta(doc)
        if args.json:
            if changes or orphans:
                print(json.dumps({"file": doc["file"], "changes": changes,
                                  "orphans": orphans}, ensure_ascii=False))
            total_changes += len(changes)
            continue
        if not changes and not orphans:
            continue
        print(f"{doc['file']}: {len(changes)} change(s)"
              + (f", {len(orphans)} orphan(s)" if orphans else ""))
        for change in changes:
            print(f"  {change['id']:14s} {change['field']:12s} "
                  f"{change['before']!r} -> {change['after']!r}")
        for entry_id in orphans:
            action = "删除 frontmatter 记录" if args.prune else "保留（加 --prune 删除）"
            print(f"  {entry_id:14s} 正文无对应标题，{action}")
        total_changes += len(changes)
        if args.apply:
            if args.prune:
                meta = [entry for entry in meta if entry["id"] not in orphans]
            new_text = assemble(render_frontmatter(meta), doc["body"])
            write_text(doc["path"], new_text)
    if not args.json:
        verb = "applied" if args.apply else "would change"
        print(f"\n{total_changes} field change(s) {verb}")
    return 0


def apply_body_change(doc, new_text):
    final_text = resync_text(doc, new_text)
    write_text(doc["path"], final_text)


def cmd_close(args):
    status_text = CLOSED_TEXT[args.status]
    day = args.close_date or date.today().isoformat()
    for doc in load_docs():
        entry = doc["entries"].get(args.id)
        if entry is None:
            continue
        if entry["section"] != "open":
            print(f"{args.id} 当前在 {entry['section']} 分区，只有打开条目可以关闭",
                  file=sys.stderr)
            return 2
        if not args.force and not author_suggestion(entry["text"]):
            print(f"{args.id} 的作者建议为空，按规范禁止直接关闭；确认后加 --force",
                  file=sys.stderr)
            return 2
        block = get_block(doc["text"], args.id)
        if block is None:
            print(f"{args.id} 定位失败", file=sys.stderr)
            return 2
        block = mark_status(block, status_text)
        block = add_closed_fields(block, args.reason, day)
        new_text = remove_entry_text(doc["text"], args.id)
        new_text = insert_block_text(new_text, "closed", block)
        print(f"{doc['file']}: {args.id} -> {status_text} @ {day}")
        if args.apply:
            apply_body_change(doc, new_text)
            print("  applied")
        else:
            print("  (dry-run; 加 --apply 写回)")
        return 0
    print(f"未找到条目 {args.id}", file=sys.stderr)
    return 2


def cmd_reopen(args):
    for doc in load_docs():
        entry = doc["entries"].get(args.id)
        if entry is None:
            continue
        if entry["section"] != "closed":
            print(f"{args.id} 当前在 {entry['section']} 分区，只有关闭条目可以重新打开",
                  file=sys.stderr)
            return 2
        block = get_block(doc["text"], args.id)
        if block is None:
            print(f"{args.id} 定位失败", file=sys.stderr)
            return 2
        block = strip_closed_fields(block)
        new_text = remove_entry_text(doc["text"], args.id)
        new_text = insert_block_text(new_text, "open", block)
        print(f"{doc['file']}: {args.id} -> 打开")
        if args.apply:
            apply_body_change(doc, new_text)
            print("  applied")
        else:
            print("  (dry-run; 加 --apply 写回)")
        return 0
    print(f"未找到条目 {args.id}", file=sys.stderr)
    return 2


EXACT_HEADING = {section: prefix for prefix, section in SECTION_HEADINGS}


def normalize_text(text):
    """补锚点、规范分区标题、规范 定位 写法。"""
    # 1. anchors（自下而上，避免索引漂移）
    structure = parse_structure(text)
    lines = structure["lines"][:]
    for entry in sorted(structure["entries"], key=lambda e: e["heading"], reverse=True):
        heading = entry["heading"]
        index = heading - 1
        present = set()
        while index >= 0 and re.match(r'^\s*<a id="[^"]+"></a>\s*$', lines[index]):
            present.add(re.search(r'id="([^"]+)"', lines[index]).group(1))
            index -= 1
        missing = [item for item in entry["ids"] if item not in present]
        if missing:
            lines[heading:heading] = [f'<a id="{item}"></a>' for item in missing]
    text = "\n".join(lines) + "\n"

    # 2. 定位 规范化（自下而上）
    structure = parse_structure(text)
    lines = structure["lines"][:]
    bold_fields = ("状态", "严重度", "类型", "关闭原因", "关闭日期",
                   "置信度", "修复 commit", "修复方式", "来源")
    for entry in sorted(structure["entries"], key=lambda e: e["start"], reverse=True):
        block = lines[entry["start"]:entry["end"]]
        for offset, line in enumerate(block):
            match = re.match(r"^-\s*(" + "|".join(bold_fields) + r")\s*[:：]\s*(.*)$", line)
            if match:
                block[offset] = f"- **{match.group(1)}**: {match.group(2)}"
        for offset, line in enumerate(block):
            if line.strip() != "#### 定位":
                continue
            cursor = offset + 1
            while cursor < len(block) and not re.match(r"^(#### |---\s*$|### |## )", block[cursor]):
                if block[cursor].strip():
                    block[cursor] = canonical_location(block[cursor])
                cursor += 1
            break
        lines[entry["start"]:entry["end"]] = block
    text = "\n".join(lines) + "\n"

    # 3. 分区标题精确化
    structure = parse_structure(text)
    lines = structure["lines"][:]
    for section, info in structure["sections"].items():
        lines[info["head"]] = EXACT_HEADING[section]
    return "\n".join(lines) + "\n"


def cmd_move(args):
    for doc in load_docs():
        entry = doc["entries"].get(args.id)
        if entry is None:
            continue
        occurrences = [item for item in parse_structure(doc["text"])["entries"]
                       if args.id in item["ids"]]
        if len(occurrences) > 1:
            print(f"{args.id} 在正文出现 {len(occurrences)} 次，先人工去重再移动",
                  file=sys.stderr)
            return 2
        if entry["section"] == args.to:
            print(f"{args.id} 已在 {args.to} 分区")
            return 0
        block = get_block(doc["text"], args.id)
        if block is None:
            print(f"{args.id} 定位失败", file=sys.stderr)
            return 2
        new_text = remove_entry_text(doc["text"], args.id)
        new_text = insert_block_text(new_text, args.to, block)
        print(f"{doc['file']}: {args.id} {entry['section']} -> {args.to}")
        if args.apply:
            apply_body_change(doc, new_text)
            print("  applied")
        else:
            print("  (dry-run; 加 --apply 写回)")
        return 0
    print(f"未找到条目 {args.id}", file=sys.stderr)
    return 2


def cmd_normalize(args):
    changed = 0
    for doc in load_docs():
        new_text = normalize_text(doc["text"])
        if new_text == doc["text"]:
            continue
        changed += 1
        print(f"{doc['file']}: normalized")
        if args.apply:
            write_text(doc["path"], new_text)
    if not changed:
        print("already normalized")
    elif not args.apply:
        print(f"\n{changed} file(s) would change; 加 --apply 写回")
    return 0


def cmd_locate(args):
    row = {item["id"]: item for item in flatten(load_docs())}.get(args.id)
    if row is None:
        print(f"未找到条目 {args.id}", file=sys.stderr)
        return 2
    locations = row.get("locations") or []
    if not locations:
        print(f"{args.id} 没有结构化定位", file=sys.stderr)
        return 2
    for location in locations:
        path, start, _ = location_line_count(location)
        full = os.path.join(REPO, path)
        symbol = ""
        line_text = ""
        if os.path.isfile(full) and start:
            source = open(full, encoding="utf-8", errors="ignore").read().splitlines()
            if start <= len(source):
                line_text = source[start - 1].strip()
                for index in range(min(start - 1, len(source) - 1), -1, -1):
                    if re.match(r"^[A-Za-z_][A-Za-z0-9_ \*]*\**\s*[A-Za-z_]\w*\s*\(", source[index]):
                        symbol = source[index].strip()
                        break
        print(f"{location}")
        if symbol:
            print(f"    {symbol}")
        if line_text:
            print(f"    {line_text[:100]}")
    return 0


def cmd_check_locations(args):
    problems = [problem for problem in validate(load_docs())
                if problem["rule"].startswith("location")]
    if args.json:
        print(json.dumps(problems, ensure_ascii=False, indent=2))
    else:
        for problem in problems:
            print(f"[{problem['level']}] {problem['file']}:{problem['id']} "
                  f"({problem['rule']}) {problem['message']}")
        print(f"\n{len(problems)} problem(s)")
    return 1 if any(problem["level"] == "error" for problem in problems) else 0


def find_doc(module):
    for doc in load_docs():
        if module in (module_of(doc["file"]), doc["file"]):
            return doc
    return None


def id_prefix(module):
    mapping = {"platform-ports": "PORT", "nanosig-core": "CORE",
               "mpsc-record-ring": "MPSC", "global": "GLOBAL",
               "hashtable": "HASHTBL"}
    return mapping.get(module, module.upper().replace("-", ""))


def next_id(module):
    prefix = id_prefix(module)
    numbers = [int(m.group(1)) for doc in load_docs()
               for m in [re.match(rf"^{prefix}-(\d+)$", entry["id"])
                         for entry in doc["meta"].get("entries", []) if entry.get("id")]
               if m]
    return f"{prefix}-{(max(numbers) + 1) if numbers else 1:03d}"


def author_suggestion(text):
    match = re.search(r"^#### 作者建议\s*$(.*?)(?=^#### |^---\s*$|\Z)", text, re.S | re.M)
    if not match:
        return ""
    body = re.sub(r"\s+", "", match.group(1))
    return "" if body in ("", "（待作者补充）") else body


def replace_subsection(block, title, content_lines):
    out = list(block)
    for index, line in enumerate(out):
        if line.strip() != f"#### {title}":
            continue
        end = index + 1
        while end < len(out) and not re.match(r"^(#### |---\s*$|### |## )", out[end]):
            end += 1
        out[index + 1:end] = [""] + content_lines + [""]
        return out
    # 不存在则追加到条目末尾
    while out and not out[-1].strip():
        out.pop()
    return out + ["", f"#### {title}", ""] + content_lines


def entry_block_after(text, entry_id, transform):
    block = get_block(text, entry_id)
    if block is None:
        return None
    block = transform(block)
    new_text = remove_entry_text(text, entry_id)
    section = find_structure_entry(text, entry_id)["section"]
    return insert_block_text(new_text, section, block)


def build_entry_block(entry_id, data):
    severity_label = {"critical": "🔴 关键", "high": "🟠 高",
                      "medium": "🟡 中", "low": "🟢 较低"}[data["severity"]]
    lines = [f'<a id="{entry_id}"></a>', f"### {entry_id}: {data['title']}", "",
             "- **状态**: 打开",
             f"- **严重度**: {severity_label}",
             f"- **类型**: {data['type']}", "",
             "#### 问题描述", ""]
    lines += data.get("description", "").splitlines() or [""]
    lines += ["", "#### review 建议", ""]
    lines += data.get("review_suggestion", "").splitlines() or [""]
    lines += ["", "#### 作者建议", "", data.get("author_suggestion") or "（待作者补充）",
              "", "#### 可重现的失败场景", ""]
    lines += data.get("repro", "").splitlines() or ["（待补充）"]
    lines += ["", "#### 定位"]
    lines += data.get("locations", [])
    return lines


def insert_sorted(text, block, severity):
    """按严重度把条目插入打开分区。"""
    structure = parse_structure(text)
    section = structure["sections"].get("open")
    if section is None:
        raise ValueError("找不到打开分区")
    rank = SEVERITY_RANK.get(severity, 0)
    for entry in structure["entries"]:
        if entry["section"] != "open":
            continue
        body = "\n".join(structure["lines"][entry["start"]:entry["end"]])
        match = re.search(r"^\s*-\s*\*\*严重度\*\*:\s*(.+)$", body, re.M)
        label = match.group(1) if match else ""
        other = 0
        for token, weight in (("关键", 4), ("较高", 3), ("高", 3), ("中", 2), ("较低", 1), ("低", 1)):
            if token in label:
                other = weight
                break
        if other < rank:
            lines = structure["lines"][:]
            lines[entry["start"]:entry["start"]] = block + [""]
            return "\n".join(lines) + "\n"
    return insert_block_text(text, "open", block)


def cmd_add(args):
    data = json.load(sys.stdin) if args.json in (None, "-") else json.load(open(args.json))
    if "id" not in data:
        data["id"] = next_id(data["module"])
    doc = find_doc(data["module"])
    if doc is None:
        print(f"找不到模块 {data['module']}", file=sys.stderr)
        return 2
    if data["id"] in {entry["id"] for entry in doc["meta"].get("entries", [])}:
        print(f"ID 已存在: {data['id']}", file=sys.stderr)
        return 2
    block = build_entry_block(data["id"], data)
    new_text = insert_sorted(doc["text"], block, data["severity"])
    print(f"{doc['file']}: + {data['id']} {data['severity']} {data['title']}")
    if args.apply:
        apply_body_change(doc, new_text)
        print("  applied")
    else:
        print("  (dry-run; 加 --apply 写回)")
    return 0


def cmd_edit(args):
    for doc in load_docs():
        if args.id not in doc["entries"]:
            continue
        if args.field in ("severity", "type"):
            block = get_block(doc["text"], args.id)
            prefix = "- **严重度**:" if args.field == "severity" else "- **类型**:"
            if args.field == "severity":
                label = {"critical": "🔴 关键", "high": "🟠 高",
                         "medium": "🟡 中", "low": "🟢 较低"}[args.value]
            else:
                label = args.value
            block = [f"{prefix} {label}" if line.startswith(prefix) else line
                     for line in block]
            new_text = entry_block_after(doc["text"], args.id, lambda _: block)
        elif args.field == "locations":
            locations = [canonical_location(item) for item in args.value.split(",") if item.strip()]
            new_text = entry_block_after(
                doc["text"], args.id,
                lambda block: replace_subsection(block, "定位", locations))
        else:
            titles = {"description": "问题描述", "review-suggestion": "review 建议",
                      "author-suggestion": "作者建议", "repro": "可重现的失败场景"}
            content = args.text if args.text is not None else open(args.from_file, encoding="utf-8").read().rstrip("\n")
            new_text = entry_block_after(
                doc["text"], args.id,
                lambda block: replace_subsection(block, titles[args.field], content.splitlines()))
        print(f"{doc['file']}: {args.id} {args.field} updated")
        if args.apply:
            apply_body_change(doc, new_text)
            print("  applied")
        else:
            print("  (dry-run; 加 --apply 写回)")
        return 0
    print(f"未找到条目 {args.id}", file=sys.stderr)
    return 2


def append_fix_record(text, entry_id, summary, commit, day):
    structure = parse_structure(text)
    section = structure["sections"].get("fixed-history")
    if section is None:
        return text
    lines = structure["lines"][:]
    index = section["end"]
    while index > section["head"] + 1 and not lines[index - 1].strip():
        index -= 1
    record = f"- **{entry_id}（{day}）**: {summary} — commit `{commit}`"
    lines[index:index] = ["", record]
    return "\n".join(lines) + "\n"


def cmd_record_fix(args):
    for doc in load_docs():
        entry = doc["entries"].get(args.id)
        if entry is None or entry["section"] != "open":
            continue
        if not author_suggestion(entry["text"]) and not args.force:
            print(f"{args.id} 的作者建议为空，按规范禁止直接关闭；确认后加 --force",
                  file=sys.stderr)
            return 2
        day = args.close_date or date.today().isoformat()
        block = get_block(doc["text"], args.id)
        block = mark_status(block, "关闭-已修复")
        block = add_closed_fields(block, f"修复 commit {args.commit}", day)
        new_text = remove_entry_text(doc["text"], args.id)
        new_text = insert_block_text(new_text, "closed", block)
        new_text = append_fix_record(new_text, args.id, args.summary or entry["title"],
                                     args.commit, day)
        print(f"{doc['file']}: {args.id} -> 关闭-已修复 + 修复历史")
        if args.apply:
            apply_body_change(doc, new_text)
            print("  applied")
        else:
            print("  (dry-run; 加 --apply 写回)")
        return 0
    print(f"未找到可关闭的打开条目 {args.id}", file=sys.stderr)
    return 2


def cmd_archive(args):
    cutoff = args.before or (date.today().replace(day=1)).isoformat()
    moved = []
    for doc in load_docs():
        for entry in doc["meta"].get("entries", []):
            if entry.get("status") in (None, "open", "closed-rejected", "unknown"):
                continue
            if entry.get("section") != "closed":
                continue
            closed_on = parse_date(entry.get("closed_date"))
            if closed_on is None or closed_on >= parse_date(cutoff):
                continue
            moved.append((doc["file"], entry["id"], closed_on))
    if not moved:
        print("没有需要归档的条目")
        return 0
    for file_name, entry_id, closed_on in moved:
        print(f"{file_name}: {entry_id} (closed {closed_on})")
    if not args.apply:
        print(f"\n{len(moved)} 条待归档; 加 --apply 执行")
        return 0

    by_file = {}
    for file_name, entry_id, _ in moved:
        by_file.setdefault(file_name, []).append(entry_id)
    for doc in load_docs():
        if doc["file"] not in by_file:
            continue
        text = doc["text"]
        for entry_id in by_file[doc["file"]]:
            closed_on = next(item[2] for item in moved
                             if item[0] == doc["file"] and item[1] == entry_id)
            block = get_block(text, entry_id)
            if block is None:
                continue
            module = module_of(doc["file"])
            folder = os.path.join(REVIEW_DIR, "history", module)
            os.makedirs(folder, exist_ok=True)
            target = os.path.join(folder, f"{module}-archive-{closed_on.year}-{closed_on.month:02d}.md")
            if not os.path.exists(target):
                with open(target, "w", encoding="utf-8") as handle:
                    handle.write(f"# {module} review 归档 {closed_on.year}-{closed_on.month:02d}\n")
            with open(target, "a", encoding="utf-8") as handle:
                handle.write("\n" + "\n".join(block) + "\n\n---\n")
            text = remove_entry_text(text, entry_id)
        meta, body = parse_frontmatter(text)
        remaining = parse_body(body)
        kept = [entry for entry in (meta or {}).get("entries", [])
                if entry.get("id") in remaining]
        write_text(doc["path"], assemble(render_frontmatter(kept), body))
    print(f"{len(moved)} 条已归档")
    return 0


def acquire_lock():
    try:
        import fcntl
    except ImportError:
        return None
    handle = open(os.path.join(REVIEW_DIR, ".review.lock"), "w")
    fcntl.flock(handle, fcntl.LOCK_EX)
    return handle


def cmd_batch(args):
    operations = [json.loads(line) for line in open(args.file, encoding="utf-8")
                  if line.strip()]
    handle = acquire_lock()
    try:
        for operation in operations:
            operation = dict(operation)
            kind = operation.pop("op")
            namespace = argparse.Namespace(**operation)
            namespace.apply = True
            namespace.force = operation.get("force", False)
            namespace.close_date = operation.get("date")
            namespace.text = operation.get("text")
            namespace.from_file = operation.get("from")
            namespace.value = operation.get("value")
            if kind == "close":
                cmd_close(namespace)
            elif kind == "reopen":
                cmd_reopen(namespace)
            elif kind == "edit":
                cmd_edit(namespace)
            elif kind == "record-fix":
                cmd_record_fix(namespace)
            else:
                print(f"未知 op: {kind}", file=sys.stderr)
                return 2
    finally:
        if handle:
            handle.close()
    return 0


def main():
    global REVIEW_DIR
    parser = argparse.ArgumentParser(description="review 文档查询、校验与编辑接口")
    parser.add_argument("--review-dir", default=REVIEW_DIR,
                        help="review 文档目录（默认 docs/review）")
    sub = parser.add_subparsers(dest="command", required=True)

    p_list = sub.add_parser("list", help="列出条目")
    for name in ("status", "module", "severity", "type", "section", "id", "text",
                 "location", "opened-after", "opened-before", "closed-after",
                 "closed-before"):
        p_list.add_argument(f"--{name}")
    p_list.add_argument("--sort", default="id",
                        choices=["id", "severity", "status", "opened_date", "closed_date"])
    p_list.add_argument("--group-by", choices=["module", "status", "severity", "section"])
    p_list.add_argument("--count", action="store_true")
    p_list.add_argument("--limit", type=int)
    p_list.add_argument("--json", action="store_true")
    p_list.set_defaults(func=cmd_list)

    p_show = sub.add_parser("show", help="显示条目全文")
    p_show.add_argument("ids", nargs="+", metavar="ID")
    p_show.add_argument("--body-only", action="store_true")
    p_show.add_argument("--json", action="store_true")
    p_show.set_defaults(func=cmd_show)

    p_search = sub.add_parser("search", help="正则搜索标题与正文")
    p_search.add_argument("pattern")
    p_search.add_argument("--section")
    p_search.add_argument("--status")
    p_search.add_argument("--json", action="store_true")
    p_search.set_defaults(func=cmd_search)

    p_next = sub.add_parser("next", help="按严重度列出待处理打开条目")
    p_next.add_argument("--limit", type=int, default=10)
    p_next.add_argument("--json", action="store_true")
    p_next.set_defaults(func=cmd_next)

    p_stats = sub.add_parser("stats", help="统计条目")
    p_stats.add_argument("--json", action="store_true")
    p_stats.set_defaults(func=cmd_stats)

    p_validate = sub.add_parser("validate", help="校验是否符合 AGENTS.md 规范")
    p_validate.add_argument("--json", action="store_true")
    p_validate.add_argument("--strict-archive", action="store_true")
    p_validate.set_defaults(func=cmd_validate)

    p_sync = sub.add_parser("sync", help="以正文为准回填 frontmatter")
    p_sync.add_argument("--apply", action="store_true")
    p_sync.add_argument("--prune", action="store_true",
                        help="同时删除正文已不存在的 frontmatter 记录")
    p_sync.add_argument("--json", action="store_true")
    p_sync.set_defaults(func=cmd_sync)

    p_close = sub.add_parser("close", help="把打开条目移到关闭分区")
    p_close.add_argument("id")
    p_close.add_argument("--status", required=True,
                         choices=["closed-fixed", "closed-rejected",
                                  "closed-not-applicable", "closed-deferred"])
    p_close.add_argument("--reason", required=True)
    p_close.add_argument("--date", dest="close_date")
    p_close.add_argument("--apply", action="store_true")
    p_close.add_argument("--force", action="store_true",
                         help="作者建议为空时仍强制关闭")
    p_close.set_defaults(func=cmd_close)

    p_normalize = sub.add_parser("normalize", help="补锚点、规范分区标题与定位写法")
    p_normalize.add_argument("--apply", action="store_true")
    p_normalize.set_defaults(func=cmd_normalize)

    p_move = sub.add_parser("move", help="在分区之间移动条目")
    p_move.add_argument("id")
    p_move.add_argument("--to", required=True,
                        choices=["open", "closed", "fixed-history"])
    p_move.add_argument("--apply", action="store_true")
    p_move.set_defaults(func=cmd_move)

    p_locate = sub.add_parser("locate", help="解析条目的结构化定位")
    p_locate.add_argument("id")
    p_locate.set_defaults(func=cmd_locate)

    p_check = sub.add_parser("check-locations", help="校验定位格式、文件与行号")
    p_check.add_argument("--json", action="store_true")
    p_check.set_defaults(func=cmd_check_locations)

    p_add = sub.add_parser("add", help="从 JSON 新建打开条目")
    p_add.add_argument("--json", default="-", help="JSON 文件，默认 stdin")
    p_add.add_argument("--apply", action="store_true")
    p_add.set_defaults(func=cmd_add)

    p_edit = sub.add_parser("edit", help="修改条目字段或子分区")
    p_edit.add_argument("id")
    p_edit.add_argument("--field", required=True,
                        choices=["description", "review-suggestion", "author-suggestion",
                                 "repro", "locations", "severity", "type"])
    p_edit.add_argument("--text")
    p_edit.add_argument("--from", dest="from_file")
    p_edit.add_argument("--value")
    p_edit.add_argument("--apply", action="store_true")
    p_edit.set_defaults(func=cmd_edit)

    p_fix = sub.add_parser("record-fix", help="关闭为已修复并写修复历史")
    p_fix.add_argument("id")
    p_fix.add_argument("--commit", required=True)
    p_fix.add_argument("--summary")
    p_fix.add_argument("--date", dest="close_date")
    p_fix.add_argument("--apply", action="store_true")
    p_fix.add_argument("--force", action="store_true")
    p_fix.set_defaults(func=cmd_record_fix)

    p_archive = sub.add_parser("archive", help="归档满 30 天的关闭条目")
    p_archive.add_argument("--before", help="截止日期 yyyy-mm-dd")
    p_archive.add_argument("--apply", action="store_true")
    p_archive.set_defaults(func=cmd_archive)

    p_batch = sub.add_parser("batch", help="按 JSONL 批量执行写操作")
    p_batch.add_argument("--file", required=True)
    p_batch.set_defaults(func=cmd_batch)

    p_reopen = sub.add_parser("reopen", help="把关闭条目移回打开分区")
    p_reopen.add_argument("id")
    p_reopen.add_argument("--apply", action="store_true")
    p_reopen.set_defaults(func=cmd_reopen)

    args = parser.parse_args()
    REVIEW_DIR = args.review_dir
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
