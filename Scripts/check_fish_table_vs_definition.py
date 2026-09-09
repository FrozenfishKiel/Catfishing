# -*- coding: utf-8 -*-
"""鱼表格 ↔ UCatFishDefinition 发布映射校验（文本级，不开引擎）。

用法：python Scripts/check_fish_table_vs_definition.py [--out Docs/gap-analysis/<日期>/鱼表格发布映射.md]

读四样东西：
  1. Knowledge/Schema/鱼表格.第一版.yaml   —— 人维护的列级发布映射（sidecar）
  2. 镜像 CSV（sidecar.table.mirror）        —— 飞书导出，表头 + 16 行
  3. Source/Catfishing/Data/CatFishDefinition.h —— UPROPERTY 字段名（正则抓声明行）
  4. .harness/formal-fish-asset-input-package.json 与 Knowledge/Feishu/_manifest.json —— 资产按哪个表 revision 生成、镜像是哪个 revision

输出三类差异 + 行级检查：
  ① 表有列、sidecar 声明了 publish 目标，但目标在头文件里不存在（或已 Deprecated）
  ② 代码字段表里没有列（对照 sidecar 的 code_only_fields 是否都有说法）
  ③ 输入包 revision 与镜像 revision 的差（资产是否落后于表）
  ④ 行级：identity 列唯一且形如 Fish_X；declarative 列按 type 解析（range/number/enum）
资产里的实际值读不到，值级比对不在本脚本范围。
"""
from __future__ import annotations

import argparse
import csv
import io
import json
import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:  # pragma: no cover
    print("需要 PyYAML：pip install pyyaml", file=sys.stderr)
    raise

ROOT = Path(__file__).resolve().parents[1]
SIDECAR = ROOT / "Knowledge/Schema/鱼表格.第一版.yaml"
HEADER = ROOT / "Source/Catfishing/Data/CatFishDefinition.h"
PACKAGE = ROOT / ".harness/formal-fish-asset-input-package.json"
MANIFEST = ROOT / "Knowledge/Feishu/_manifest.json"

DECL_RE = re.compile(
    r"^\s*(?:[A-Za-z0-9_:<>]+\s+)+?(?P<name>b?[A-Z][A-Za-z0-9_]*)\s*(?:=|;)", re.M)


def read_text(p: Path) -> str:
    return p.read_text(encoding="utf-8-sig")


def header_fields(text: str) -> tuple[list[str], set[str]]:
    """抓 UCatFishDefinition 类体里 UPROPERTY 之后的成员名；标 DeprecatedProperty 的另计。"""
    m = re.search(r"class\s+CATFISHING_API\s+UCatFishDefinition\b.*?\n\{(.*?)\n\};", text, re.S)
    body = m.group(1) if m else text
    names: list[str] = []
    deprecated: set[str] = set()
    chunks = re.split(r"UPROPERTY\s*\(", body)[1:]
    for ch in chunks:
        depth, i = 1, 0
        while i < len(ch) and depth:
            depth += {"(": 1, ")": -1}.get(ch[i], 0)
            i += 1
        meta, rest = ch[:i], ch[i:]
        dm = DECL_RE.search(rest)
        if not dm:
            continue
        name = dm.group("name")
        names.append(name)
        if "DeprecatedProperty" in meta:
            deprecated.add(name)
    return names, deprecated


def parse_csv(p: Path) -> tuple[list[str], list[dict]]:
    rows = list(csv.reader(io.StringIO(read_text(p))))
    head = [h.strip() for h in rows[0]]
    data = []
    for r in rows[1:]:
        if not r or not (r[0] or "").strip():
            continue
        data.append({head[i]: (r[i] if i < len(r) else "") for i in range(len(head))})
    return head, data


def match_col(name: str, head: list[str]) -> str | None:
    """sidecar 的 name 按表头前缀匹配（表头常带括注）。"""
    exact = [h for h in head if h == name]
    if exact:
        return exact[0]
    pref = [h for h in head if h.startswith(name)]
    return pref[0] if len(pref) == 1 else None


def check_value(col: dict, v: str) -> str | None:
    v = (v or "").strip()
    t = col.get("type")
    if v == "":
        return None if (col.get("classification") == "interpretive" or col.get("optional")) else "空"
    if t == "range":
        return None if re.fullmatch(r"\s*[\d.]+\s*[~～]\s*[\d.]+\s*", v) else f"不是区间：{v[:20]}"
    if t == "number":
        try:
            float(v)
            return None
        except ValueError:
            m = re.fullmatch(r"\s*([\d.]+)\s*秒?\s*", v)
            return None if m else f"不是数：{v[:20]}"
    if t == "enum":
        keys = list((col.get("values") or {}).keys())
        return None if any(v.startswith(k) for k in keys) else f"不在枚举 {keys}：{v[:20]}"
    if t == "id":
        return None if re.fullmatch(r"Fish_[A-Za-z0-9]+", v) else f"ID 格式：{v[:20]}"
    return None


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=None, help="Markdown 报告落点；不给只打印")
    args = ap.parse_args(argv)

    sc = yaml.safe_load(read_text(SIDECAR))
    mirror = ROOT / sc["table"]["mirror"]
    head, data = parse_csv(mirror)
    fields, deprecated = header_fields(read_text(HEADER))
    pkg = json.loads(read_text(PACKAGE))
    man = json.loads(read_text(MANIFEST))
    sheet = next((s for s in man.get("sheets", []) if s.get("sheet_name") == sc["table"]["sheet_name"]
                  and s.get("node_token") == sc["table"]["wiki_node_token"]), {})

    out: list[str] = []
    P = out.append
    P(f"# 鱼表格发布映射校验（镜像 revision {sheet.get('revision', '?')}，导出 {man.get('exported_at', '?')}）\n")
    P("文本级校验：sidecar `Knowledge/Schema/鱼表格.第一版.yaml` × 镜像 CSV × `CatFishDefinition.h` × 输入包。资产实际值不在范围内。\n")

    problems = 0
    # ---- 列 ↔ sidecar
    P("## 一、表列与 sidecar 声明\n")
    declared_names = [c["name"] for c in sc["columns"]]
    unmatched = [c["name"] for c in sc["columns"] if match_col(c["name"], head) is None]
    extra = [h for h in head if h.strip() and not any(h.startswith(n) or h == n for n in declared_names)]
    P(f"- 表头 {len([h for h in head if h.strip()])} 列；sidecar 声明 {len(declared_names)} 列。")
    if unmatched:
        problems += len(unmatched); P(f"- ❌ sidecar 有、表里找不到（或前缀歧义）：{unmatched}")
    if extra:
        problems += len(extra); P(f"- ❌ 表有、sidecar 没声明：{extra}")
    if not unmatched and not extra:
        P("- ✅ 列集合一致。")
    P("")

    # ---- ① publish 目标是否存在
    P("## 二、publish 目标 ↔ UCatFishDefinition 字段\n")
    P("| 列 | 分类 | publish | 头文件里 | 备注 |\n|---|---|---|---|---|")
    covered: set[str] = set()
    for c in sc["columns"]:
        pub = c.get("publish")
        targets = pub if isinstance(pub, list) else ([] if pub in (None, "excluded") else [pub])
        status = "—"
        if targets:
            missing = [t for t in targets if t not in fields]
            dep = [t for t in targets if t in deprecated]
            if missing:
                problems += 1; status = f"❌ 不存在 {missing}"
            elif dep:
                problems += 1; status = f"⚠️ 已 Deprecated {dep}"
            else:
                status = "✅"
            covered.update(targets)
        note = c.get("gap") or c.get("transform") or c.get("note") or ""
        P(f"| {c['name']} | {c.get('classification')}{'/' + c['owner'] if c.get('owner') else ''} | {pub if pub else 'excluded'} | {status} | {str(note).replace('|', '／')[:90]} |")
    P("")

    # ---- ② 代码字段无列
    P("## 三、代码字段里表没有的\n")
    known = sc.get("code_only_fields") or {}
    uncovered = [f for f in fields if f not in covered]
    P("| 字段 | 已弃用 | sidecar 的说法 |\n|---|---|---|")
    for f in uncovered:
        say = known.get(f)
        if say is None and f not in deprecated:
            problems += 1
        P(f"| {f} | {'是' if f in deprecated else ''} | {say or '❌ 未说明'} |")
    P("")

    # ---- ③ revision 差
    P("## 四、输入包与镜像的 revision\n")
    pkg_rev = ((pkg.get("sources") or {}).get("fish_table") or {}).get("revision")
    req = pkg.get("ue_required_fields") or []
    P(f"- 输入包按表 revision **{pkg_rev}** 生成（{((pkg.get('sources') or {}).get('fish_table') or {}).get('live_read_at_local', '?')}）；镜像现在是 **{sheet.get('revision', '?')}**。")
    if pkg_rev and sheet.get("revision") and int(sheet["revision"]) > int(pkg_rev):
        problems += 1; P("- ❌ 资产落后于表：表在输入包之后又改了（含 09-08 体力系数、09-09 fish_id），资产未重生成。")
    req_missing = [f for f in req if f not in fields]
    if req_missing:
        problems += 1; P(f"- ❌ 输入包要求的字段头文件里没有：{req_missing}")
    else:
        P(f"- ✅ 输入包 ue_required_fields {len(req)} 个都在头文件里。")
    P("")

    # ---- ④ 行级
    P("## 五、行级检查（declarative 列按 type 解析）\n")
    idcol = match_col(sc["table"]["identity_column"], head)
    ids = [r.get(idcol, "").strip() for r in data] if idcol else []
    if not idcol:
        problems += 1; P(f"- ❌ 身份列 {sc['table']['identity_column']} 不在表头")
    else:
        dup = {i for i in ids if ids.count(i) > 1}
        bad = [i for i in ids if not re.fullmatch(r"Fish_[A-Za-z0-9]+", i)]
        P(f"- 身份列 `{idcol}`：{len(ids)} 行，重复 {sorted(dup) or '无'}，格式不对 {bad or '无'}")
        problems += len(dup) + len(bad)
    bad_cells: list[str] = []
    for c in sc["columns"]:
        h = match_col(c["name"], head)
        if not h or c.get("classification") != "declarative":
            continue
        for r in data:
            err = check_value(c, r.get(h, ""))
            if err:
                bad_cells.append(f"{r.get(head[0], '?')[:6]} / {c['name']}：{err}")
    if bad_cells:
        problems += len(bad_cells)
        P(f"- ❌ 单元格解析不过 {len(bad_cells)} 处：")
        for b in bad_cells[:30]:
            P(f"  - {b}")
    else:
        P("- ✅ declarative 列全部按声明类型解析通过。")
    P("")
    P(f"合计问题 {problems} 处。问题里「代码字段表没有」「publish 目标不存在」是发布映射缺口，不是表填错；行级解析失败才是表要改的。")

    text = "\n".join(out)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text + "\n", encoding="utf-8")
        print(f"写入 {args.out}（问题 {problems} 处）")
    else:
        print(text)
    return 1 if problems else 0


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    raise SystemExit(main())
