#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 Knowledge/Design/_manifest.json 里 docs 的内容哈希刷到与仓库文件一致。

为什么要有这个脚本：`docs[].revision_id` 与 `content_sha1` 是仓库文件的内容哈希，
`collect_gap.py --plan` 拿它判断哪个系统要重跑（见 Knowledge/Design/README.md 第 3 条）。
它得在每次改完设计正文后刷新，而这件事靠人记得——2026-09-13 查的时候 27 份文档里有 6 份
已经过期（钓鱼规则、营地、吃鱼效果、联机社交、印记、参数页），最早一份从 09-12 就没更新。
过期的后果不是报错而是**静默误判**：下一轮 --plan 会把改过的册子判成「文档未变」并沿用旧报告。

所以：改完设计文档就跑一次；`--check` 只报不写，适合当提交前检查。

    python Scripts/refresh_design_manifest_hashes.py            # 刷新
    python Scripts/refresh_design_manifest_hashes.py --check    # 只检查，有过期就退 1

只动 docs 条目。sheets / embedded_sheets 的 revision 是飞书原值（表仍在飞书填），不碰；
node_token 是稳定要求键的 doc 身份，任何情况下都不改。
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path

# 工程根由脚本自己按自身位置推导，不依赖调用时的工作目录（AGENTS.md「路径写法（协作项目）」）。
PROJECT_ROOT = Path(__file__).resolve().parent.parent
DESIGN_ROOT = PROJECT_ROOT / "Knowledge" / "Design"
MANIFEST = DESIGN_ROOT / "_manifest.json"
HASH_CHARS = 12


def resolve(parts) -> Path | None:
    """manifest 的 path 是路径段数组。嵌套镜像的索引页落在 <目录>/<同名>.md。"""
    segments = parts if isinstance(parts, list) else [parts]
    direct = DESIGN_ROOT.joinpath(*segments).with_suffix(".md")
    if direct.is_file():
        return direct
    nested = DESIGN_ROOT.joinpath(*segments, segments[-1]).with_suffix(".md")
    return nested if nested.is_file() else None


def content_hash(path: Path) -> str:
    # Git 在 Windows 可检出 CRLF；内容版本不能随检出平台变化。
    return hashlib.sha1(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()[:HASH_CHARS]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="只报告，不写入；有过期条目时退出码 1")
    args = parser.parse_args()
    manifest = json.loads(io.open(MANIFEST, encoding="utf-8").read())
    stale, missing = [], []
    for entry in manifest.get("docs", []):
        path = resolve(entry.get("path"))
        if path is None:
            missing.append(entry.get("path"))
            continue
        current = content_hash(path)
        if entry.get("revision_id") != current or entry.get("content_sha1") != current:
            stale.append((path, entry.get("revision_id"), current))
            if not args.check:
                entry["revision_id"] = current
                entry["content_sha1"] = current
    for parts in missing:
        print(f"⚠ 找不到文件，未处理：{parts}")
    for path, old, new in stale:
        print(f"{'过期' if args.check else '已刷新'} {path.as_posix()}  {old} -> {new}")
    print(f"docs 共 {len(manifest.get('docs', []))} 条；过期 {len(stale)}；找不到文件 {len(missing)}")
    if stale and not args.check:
        io.open(MANIFEST, "w", encoding="utf-8", newline="").write(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
        print(f"已写回 {MANIFEST.as_posix()}")
    return 1 if (stale and args.check) or missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
