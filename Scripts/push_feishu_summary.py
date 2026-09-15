#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把一份本地 Markdown（规则版摘要）推到飞书对应的设计页，整页覆盖，页顶带「规则版摘要」横幅。

    python Scripts/push_feishu_summary.py --doc 多人钓鱼附篇 --file 摘要.md
    python Scripts/push_feishu_summary.py --dir <摘要目录>            # 目录里按 manifest 的 file 相对路径放摘要，有几份推几份
    加 --dry-run 只打印计划

背景（李前臻 2026-09-15 定）：飞书上的设计页不再镜像仓库正文，只保留每份文档的规则版摘要，
帮读者更快建立心智模型，不要求与仓库一致；正文与出处只在仓库 Knowledge/Design/。

规则：
- 目标页＝ Knowledge/Design/_manifest.json 的 docs（node_token 定飞书页）。标题不动，只写正文。
- 摘要文件是普通 Markdown；加粗会被飞书解析器在全角标点前吃掉、表格单元格里更会变成字面星号，
  所以正文里的 **x** 自动换成 <b>x</b>，表格行里的加粗去掉标记。
- 含内嵌表的页（现只有商店两张，王甜甜在飞书填）不能整页覆盖（sheet 标签会复制出新表、旧表消失）：
  先按同父连续段逐段删掉旧正文块，只剩表格，再把摘要插在表格前面。
- 覆盖会丢掉飞书页上的评论。
- 仓库在 Windows 上检出是 CRLF，读文件先归一成 LF。
"""
from __future__ import annotations

import argparse
import datetime as dt
import io
import json
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DESIGN = PROJECT_ROOT / "Knowledge" / "Design"
MANIFEST = DESIGN / "_manifest.json"
TMP_NAME = "_publish.tmp.md"
WIKI_BASE = "https://qcniqd0mjfwg.feishu.cn/wiki/"
CLI = shutil.which("lark-cli.cmd") or shutil.which("lark-cli") or "lark-cli.cmd"
CRLF = "\r\n"
LF = "\n"
FRONT = re.compile(r"^---\n.*?\n---\n", re.S)
TITLE = re.compile(r"^\s*<title>.*?</title>\s*\n", re.S)
BOLD = re.compile(r"(?<!\\)\*\*(.+?)(?<!\\)\*\*")


def run(args: list[str], cwd: Path, timeout: int = 900):
    r = subprocess.run([CLI, *args, "--as", "user", "--format", "json"], cwd=str(cwd),
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
    raw = (r.stdout or "").strip() or (r.stderr or "").strip()
    try:
        env = json.loads(raw)
    except Exception:
        return False, {"parse_error": raw[:800]}
    if env.get("ok"):
        data = env.get("data", {})
        if isinstance(data, dict) and data.get("result") == "failed":
            return False, data
        return True, data
    return False, env.get("error", env)


def load_json(p: Path, default):
    return json.loads(io.open(p, encoding="utf-8").read()) if p.exists() else default


def git_head() -> str:
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=str(PROJECT_ROOT),
                              capture_output=True, text=True).stdout.strip() or "?"
    except Exception:
        return "?"


def emphasis_fix(text: str) -> str:
    out = []
    for line in text.split(LF):
        if line.lstrip().startswith("|"):
            line = BOLD.sub(lambda m: m.group(1), line)
        else:
            line = BOLD.sub(lambda m: "<b>" + m.group(1) + "</b>", line)
        out.append(line)
    return LF.join(out)


def read_summary(p: Path) -> str:
    text = io.open(p, "rb").read().decode("utf-8-sig").replace(CRLF, LF)
    text = FRONT.sub("", text, count=1)
    text = TITLE.sub("", text, count=1)
    return emphasis_fix(text.strip(LF)) + LF


def banner(doc: dict, head: str) -> str:
    ts = dt.datetime.now().strftime("%Y-%m-%d")
    return (f"> <b>规则版摘要</b>（{ts}，据仓库 {head}）。本页帮你快速建立心智模型，不保证与仓库同步；"
            f"判定细节、数值与出处以仓库 `Knowledge/Design/{doc['file']}` 为准，改设计只改仓库。"
            + LF + LF)


def doc_url(doc: dict) -> str:
    return WIKI_BASE + doc["node_token"]


def write_tmp(content: str) -> Path:
    p = DESIGN / TMP_NAME
    io.open(p, "w", encoding="utf-8", newline=LF).write(content)
    return p


def fetch_blocks(doc: dict):
    """(title_id, [(block_id, tag, parent)])：列表 ul/ol 外壳没有 id，真正的块是 li，parent 记成列表序号。"""
    ok, data = run(["docs", "+fetch", "--doc", doc_url(doc), "--doc-format", "xml", "--detail", "with-ids"], cwd=DESIGN)
    if not ok:
        raise RuntimeError(f"fetch 失败：{json.dumps(data, ensure_ascii=False)[:300]}")
    content = (data.get("document") or {}).get("content") or ""
    fixed = re.sub(r"&(?!(amp|lt|gt|quot|apos|#\d+|#x[0-9a-fA-F]+);)", "&amp;", content)
    root = ET.fromstring("<root>" + fixed + "</root>")
    title_id, out, n_list = None, [], 0
    for child in root:
        bid = child.attrib.get("id")
        if child.tag == "title":
            title_id = bid
            continue
        if bid:
            out.append((bid, child.tag, "page"))
        elif child.tag in ("ul", "ol"):
            n_list += 1
            for li in child:
                lid = li.attrib.get("id")
                if lid:
                    out.append((lid, "li", f"list{n_list}"))
        else:
            raise RuntimeError(f"顶层块 <{child.tag}> 没有 block id，不知道怎么删")
    return title_id, out


def page_has_sheets(doc: dict) -> int:
    _, blocks = fetch_blocks(doc)
    return sum(1 for _, t, _ in blocks if t == "sheet")


def push_overwrite(doc: dict, content: str):
    tmp = write_tmp(content)
    try:
        ok, data = run(["docs", "+update", "--doc", doc_url(doc), "--command", "overwrite",
                        "--doc-format", "markdown", "--content", f"@./{TMP_NAME}"], cwd=DESIGN)
        if not ok:
            raise RuntimeError(f"overwrite 失败：{json.dumps(data, ensure_ascii=False)[:400]}")
        return data
    finally:
        tmp.unlink(missing_ok=True)


def push_keep_sheets(doc: dict, content: str):
    """删光非表格块，再把摘要插到页首（表格留在摘要后面）。"""
    n_deleted = 0
    while True:
        title_id, blocks = fetch_blocks(doc)
        lo = next((k for k, (_, t, _) in enumerate(blocks) if t != "sheet"), None)
        if lo is None:
            break
        hi = lo
        while hi + 1 < len(blocks) and blocks[hi + 1][1] != "sheet" and blocks[hi + 1][2] == blocks[lo][2]:
            hi += 1
        args = ["docs", "+update", "--doc", doc_url(doc), "--command", "block_delete"]
        args += ["--block-id", blocks[lo][0]] if lo == hi else ["--start-block-id", blocks[lo][0], "--end-block-id", blocks[hi][0]]
        ok, data = run(args, cwd=DESIGN)
        if not ok:
            raise RuntimeError(f"删除块 {lo}..{hi} 失败：{json.dumps(data, ensure_ascii=False)[:300]}")
        n_deleted += hi - lo + 1
    title_id, blocks = fetch_blocks(doc)
    tmp = write_tmp(content)
    try:
        ok, data = run(["docs", "+update", "--doc", doc_url(doc), "--command", "block_insert_after",
                        "--block-id", title_id or "0", "--doc-format", "markdown", "--content", f"@./{TMP_NAME}"], cwd=DESIGN)
        if not ok:
            raise RuntimeError(f"插入失败：{json.dumps(data, ensure_ascii=False)[:300]}")
        print(f"  （删旧块 {n_deleted}，表格 {sum(1 for _, t, _ in blocks if t == 'sheet')} 张保留）")
        return data
    finally:
        tmp.unlink(missing_ok=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--doc", help="manifest 标题或文件名（不带 .md）")
    ap.add_argument("--file", help="摘要 Markdown 文件")
    ap.add_argument("--dir", help="摘要目录：按 manifest 的 file 相对路径找摘要，找到几份推几份")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    docs = load_json(MANIFEST, {}).get("docs", [])
    jobs: list[tuple[dict, Path]] = []
    if a.doc and a.file:
        hit = [d for d in docs if d.get("title") == a.doc or Path(d["file"]).stem == a.doc or d["file"] == a.doc]
        if not hit:
            print("没有匹配的页；manifest 里的标题：", [d.get("title") for d in docs]); return 2
        jobs.append((hit[0], Path(a.file)))
    elif a.dir:
        base = Path(a.dir)
        for d in docs:
            p = base / d["file"]
            if p.is_file():
                jobs.append((d, p))
        if not jobs:
            print("目录里没有任何与 manifest 对应的摘要文件"); return 2
    else:
        ap.print_help(); return 2
    head = git_head()
    n_fail = 0
    for doc, path in jobs:
        content = banner(doc, head) + read_summary(path)
        try:
            n_sheets = page_has_sheets(doc)
            print(f"→ {doc['file']}  摘要 {path.name} {len(content)} chars  内嵌表 {n_sheets}")
            if a.dry_run:
                continue
            data = push_keep_sheets(doc, content) if n_sheets else push_overwrite(doc, content)
            print(f"  ✓ result={data.get('result')} rev={(data.get('document') or {}).get('revision_id')} warnings={data.get('warnings') or []}")
        except Exception as e:  # noqa: BLE001
            n_fail += 1
            print(f"  ✗ {e}")
    print(f"完成：{len(jobs) - n_fail} 成功，{n_fail} 失败")
    return 1 if n_fail else 0


if __name__ == "__main__":
    raise SystemExit(main())
