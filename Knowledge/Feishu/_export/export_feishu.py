#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把飞书知识库导出成本地镜像：docx → Markdown（带 front matter），sheet → CSV，docx 内嵌的 sheet → 旁路 CSV，bitable 跳过。

    python Knowledge/Feishu/_export/export_feishu.py <space_id> <输出目录>

只读飞书、只写输出目录。需要 lark-cli 已登录公司账号（`lark-cli auth status --json --verify`）。
镜像不是编辑面：改内容去飞书，改完重跑本脚本。

约定：
- 有子节点的页存成 `目录/同名.md`，叶子页存成 `<父目录>/<标题>.md`
- sheet 只取 `current_region`（真实数据区）；annotated_csv 的 `[row=N] ` 前缀在 **csv 解析之前** 按物理行剥掉
  （先解析再剥会把首格带引号的记录切坏）；CSV 行号 = 表格行号；表尾全空记录丢弃
- docx 正文里的 `<sheet token=… sheet-id=…>` 内嵌表也导出，放在 `<文档名>.embedded/<sheet_id>.csv`（内嵌表没有人起的名字，表头记进清单）
- 全部导到临时目录，**没有错误才替换**正式目录并清掉上次遗留（改名/删节点的旧文件）；有错误退出 1，正式目录原样不动
- 标题冲突（净化后同名）加 node_token 后缀；Windows 保留名与 `.`/`..` 前置下划线
- `_manifest.json` 记录全部节点、revision、导出时间、warnings（内嵌表拿不到之类的非致命问题）
"""
from __future__ import annotations

import csv
import io
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

CLI = shutil.which("lark-cli.cmd") or shutil.which("lark-cli") or "lark-cli.cmd"
WIKI_BASE = "https://qcniqd0mjfwg.feishu.cn/wiki/"
ROW_PREFIX = re.compile(r"(?m)^\[row=\d+\] ?")
EMBED_TAG = re.compile(r"<sheet\b[^>]*>")
ATTR = re.compile(r'([\w-]+)="([^"]*)"')
RESERVED = {"CON", "PRN", "AUX", "NUL"} | {f"COM{i}" for i in range(1, 10)} | {f"LPT{i}" for i in range(1, 10)}
KEEP_AT_ROOT = {"README.md"}      # 正式目录里不属于导出产物、替换时不动的文件
KEEP_DIRS = {"_export"}


class ExportError(Exception):
    pass


def run(args: list[str]):
    """跑 lark-cli，返回 (data, error)。成功信封在 stdout，错误信封在 stderr。"""
    r = subprocess.run([CLI, *args, "--as", "user", "--format", "json"],
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    try:
        d = json.loads(r.stdout or r.stderr)
    except Exception:
        return None, "非 JSON 输出：" + (r.stderr or r.stdout)[:160]
    if not d.get("ok"):
        return None, json.dumps(d.get("error"), ensure_ascii=False)[:200]
    return d["data"], None


def safe(title: str) -> str:
    s = re.sub(r'[\\/:*?"<>|]', "_", str(title)).strip().rstrip(".")
    if not s or s in (".", "..") or s.split(".")[0].upper() in RESERVED:
        s = "_" + (s or "untitled")
    return s


def col_letter(n: int) -> str:
    s = ""
    while n > 0:
        n, r = divmod(n - 1, 26)
        s = chr(65 + r) + s
    return s


def parse_annotated_csv(text: str) -> list[list[str]]:
    """先按物理行剥 [row=N] 前缀，再交给 csv 解析；对齐表头宽度；丢弃表尾全空记录。"""
    rows = list(csv.reader(io.StringIO(ROW_PREFIX.sub("", text))))
    if not rows:
        return []
    width = len(rows[0])
    rows = [r[:width] + [""] * (width - len(r)) for r in rows]
    while rows and not any(c.strip() for c in rows[-1]):
        rows.pop()
    return rows


def walk(space_id: str) -> list[dict]:
    tree = []

    def ls(parent):
        args = ["wiki", "+node-list", "--space-id", space_id, "--page-all"]
        if parent:
            args += ["--parent-node-token", parent]
        data, err = run(args)
        if err:
            raise ExportError(f"node-list 失败（parent={parent}）：{err}")
        return data["nodes"]

    def rec(parent, depth, path):
        for n in ls(parent):
            item = {"title": n["title"], "node_token": n["node_token"], "obj_token": n["obj_token"],
                    "obj_type": n["obj_type"], "depth": depth, "path": path + [n["title"]],
                    "has_child": bool(n.get("has_child"))}
            tree.append(item)
            if item["has_child"]:
                rec(n["node_token"], depth + 1, item["path"])

    rec(None, 0, [])
    return tree


class Layout:
    """把 wiki 路径映射成文件路径；净化后同名的节点加 token 后缀。"""

    def __init__(self):
        self.seen: dict[str, str] = {}

    def dir_for(self, item: dict) -> Path:
        return Path(*[safe(p) for p in item["path"]]) if item["has_child"] else Path(*[safe(p) for p in item["path"][:-1]])

    def doc_path(self, item: dict) -> Path:
        base = self.dir_for(item) / (safe(item["path"][-1]) + ".md")
        return self._dedupe(base, item["node_token"])

    def sheet_dir(self, item: dict) -> Path:
        return self._dedupe(Path(*[safe(p) for p in item["path"]]), item["node_token"])

    def _dedupe(self, p: Path, token: str) -> Path:
        key = p.as_posix().lower()
        if key in self.seen and self.seen[key] != token:
            p = p.with_name(p.stem + "-" + token[:6] + p.suffix)
            key = p.as_posix().lower()
        self.seen[key] = token
        return p


def fetch_sheet_csv(locator: list[str], sid: str, sh: dict):
    """两步取：先按网格拿 current_region，再按它精确取。locator = ['--url', …] 或 ['--spreadsheet-token', …]"""
    grid = f"A1:{col_letter(max(int(sh.get('column_count') or 1), 1))}{max(int(sh.get('row_count') or 1), 1)}"
    probe, err = run(["sheets", "+csv-get", *locator, "--sheet-id", sid, "--range", grid])
    if err:
        return None, None, None, err
    region = probe.get("current_region") or grid
    got, err = run(["sheets", "+csv-get", *locator, "--sheet-id", sid, "--range", region])
    if err:
        return None, None, None, err
    return parse_annotated_csv(got["annotated_csv"]), region, got.get("revision"), None


def write_csv(p: Path, rows: list[list[str]]):
    p.parent.mkdir(parents=True, exist_ok=True)
    with open(p, "w", encoding="utf-8-sig", newline="") as f:
        csv.writer(f).writerows(rows)


def export_docx(item: dict, out: Path, layout: Layout, warnings: list[str]):
    data, err = run(["docs", "+fetch", "--doc", WIKI_BASE + item["node_token"], "--doc-format", "markdown"])
    if err:
        raise ExportError(f"docx 拉取失败 {' / '.join(item['path'])}：{err}")
    doc = data["document"]
    rel = layout.doc_path(item)
    p = out / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    body = doc.get("content", "").rstrip("\n") + "\n"
    fm = ("---\n"
          f"source: {WIKI_BASE}{item['node_token']}\n"
          f"node_token: {item['node_token']}\n"
          f"obj_token: {item['obj_token']}\n"
          f"revision_id: {doc.get('revision_id')}\n"
          f"fetched_at: {time.strftime('%Y-%m-%dT%H:%M:%S')}\n"
          f"wiki_path: {' / '.join(item['path'])}\n"
          "generated: true  # 由飞书导出，勿手改；改内容去飞书\n"
          "---\n\n")
    p.write_text(fm + body, encoding="utf-8", newline="\n")
    record = {**item, "file": rel.as_posix(), "revision_id": doc.get("revision_id"), "lines": body.count("\n")}
    print(f"  ✓ {record['lines']:5d} 行  rev {str(doc.get('revision_id')):>5}  {' / '.join(item['path'])}")

    # 内嵌 sheet
    embedded = []
    seen = set()
    for tag in EMBED_TAG.findall(body):
        attrs = dict(ATTR.findall(tag))
        token, sid = attrs.get("token"), attrs.get("sheet-id")
        if not token or not sid or (token, sid) in seen:
            continue
        seen.add((token, sid))
        info, err = run(["sheets", "+workbook-info", "--spreadsheet-token", token])
        if err:
            warnings.append(f"内嵌表拿不到 {' / '.join(item['path'])} token={token} sheet-id={sid}：{err}")
            continue
        sh = next((s for s in info["sheets"] if s["sheet_id"] == sid), None)
        if sh is None:
            warnings.append(f"内嵌表 sheet-id 不在工作簿里 {' / '.join(item['path'])} token={token} sheet-id={sid}")
            continue
        rows, region, revision, err = fetch_sheet_csv(["--spreadsheet-token", token], sid, sh)
        if err:
            warnings.append(f"内嵌表读取失败 {' / '.join(item['path'])} {sh['sheet_name']}：{err}")
            continue
        # 内嵌表没有人起的名字（sheet_name 就是 id），文件按 id 命名，清单里记表头帮人辨认
        name = safe(sh["sheet_name"]) if sh["sheet_name"] != sid else sid
        epath = rel.with_suffix("").as_posix() + ".embedded/" + f"{name}.csv"
        write_csv(out / epath, rows)
        embedded.append({"in_doc": rel.as_posix(), "doc_node_token": item["node_token"], "token": token,
                         "sheet_id": sid, "sheet_name": sh["sheet_name"], "workbook_title": info.get("title"),
                         "header": [c[:40] for c in (rows[0] if rows else [])],
                         "region": region, "revision": revision, "file": epath,
                         "rows": len(rows), "cols": len(rows[0]) if rows else 0})
        print(f"      ↳ 内嵌表 {len(rows):3d} 行 × {len(rows[0]) if rows else 0:2d} 列  {info.get('title')} / {sh['sheet_name']}")
    return record, embedded


def export_sheet(item: dict, out: Path, layout: Layout) -> list[dict]:
    url = WIKI_BASE + item["node_token"]
    info, err = run(["sheets", "+workbook-info", "--url", url])
    if err:
        raise ExportError(f"workbook-info 失败 {' / '.join(item['path'])}：{err}")
    d = layout.sheet_dir(item)
    results = []
    for sh in info["sheets"]:
        rows, region, revision, err = fetch_sheet_csv(["--url", url], sh["sheet_id"], sh)
        if err:
            raise ExportError(f"子表读取失败 {' / '.join(item['path'])} / {sh['sheet_name']}：{err}")
        rel = d / (safe(sh["sheet_name"]) + ".csv")
        write_csv(out / rel, rows)
        results.append({"node": item["title"], "path": item["path"], "node_token": item["node_token"],
                        "obj_token": item["obj_token"], "sheet_id": sh["sheet_id"], "sheet_name": sh["sheet_name"],
                        "region": region, "revision": revision, "file": rel.as_posix(),
                        "rows": len(rows), "cols": len(rows[0]) if rows else 0})
        print(f"  ✓ {len(rows):3d} 行 × {len(rows[0]) if rows else 0:2d} 列  区域 {region:<8} rev {revision}  "
              f"{' / '.join(item['path'])} / {sh['sheet_name']}")
    return results


def swap_in(build: Path, out: Path, produced: set[str]):
    """成功后：删掉正式目录里上次遗留的导出产物，再把新产物搬进去。"""
    for p in list(out.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(out).as_posix()
        if rel in KEEP_AT_ROOT or rel.split("/")[0] in KEEP_DIRS:
            continue
        if p.suffix.lower() in (".md", ".csv", ".json") and rel not in produced:
            p.unlink()
    for p in build.rglob("*"):
        if p.is_file():
            dst = out / p.relative_to(build)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, dst)
    for d in sorted((x for x in out.rglob("*") if x.is_dir()), key=lambda x: -len(x.parts)):
        if d.name not in KEEP_DIRS and not any(d.iterdir()):
            d.rmdir()
    shutil.rmtree(build, ignore_errors=True)


def main(argv=None) -> int:
    argv = argv if argv is not None else sys.argv[1:]
    if len(argv) != 2:
        print(__doc__)
        return 2
    space_id, out = argv[0], Path(argv[1])
    out.mkdir(parents=True, exist_ok=True)
    build = out.parent / (out.name + ".building")
    if build.exists():
        shutil.rmtree(build)
    build.mkdir()
    warnings: list[str] = []
    layout = Layout()
    try:
        print("== 枚举 ==")
        tree = walk(space_id)
        print(f"  节点 {len(tree)}")
        print("== docx ==")
        docs, embedded = [], []
        for it in tree:
            if it["obj_type"] == "docx":
                rec, emb = export_docx(it, build, layout, warnings)
                docs.append(rec)
                embedded.extend(emb)
        print("== sheet ==")
        sheets = [r for it in tree if it["obj_type"] == "sheet" for r in export_sheet(it, build, layout)]
    except ExportError as e:
        print(f"\n!! 导出失败，正式目录未动：{e}\n   半成品在 {build}", file=sys.stderr)
        return 1
    skipped = [{"title": t["title"], "obj_type": t["obj_type"], "node_token": t["node_token"],
                "why": "bitable 不是设计文档，不导出"} for t in tree if t["obj_type"] not in ("docx", "sheet")]
    manifest = {"space_id": space_id, "exported_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
                "tree": tree, "docs": docs, "sheets": sheets, "embedded_sheets": embedded,
                "skipped": skipped, "warnings": warnings}
    (build / "_manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=1), encoding="utf-8")
    produced = {d["file"] for d in docs} | {s["file"] for s in sheets} | {e["file"] for e in embedded} | {"_manifest.json"}
    swap_in(build, out, produced)
    for w in warnings:
        print("  ⚠", w)
    print(f"\n  docx {len(docs)} / sheet 子表 {len(sheets)} / 内嵌表 {len(embedded)} / 跳过 {len(skipped)} / warnings {len(warnings)}"
          f" → {out / '_manifest.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
