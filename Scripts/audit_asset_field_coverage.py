"""只读盘点 UPROPERTY 声明、ini 绑定及资产内属性名。命中仅证明名字存在，不推断属性值或蓝图调用。

改名过的字段要连旧名一起找：资产里存的是序列化时的旧名，靠 DefaultEngine.ini 的 PropertyRedirects
在反序列化时映射到新名。只按新名找会对每一个改过名的字段报「资产零命中」——2026-09-13 就这样
把 FishFightStaminaPerKilogram 与 EatingExperiencePerKilogram 误判成未配置，而它们其实都配了。
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re

MODULES = ("Fishing", "UI", "Data", "AbilitySystem", "Growth", "Condition",
           "Framework", "Save", "FishContainers", "Camp", "Collection", "ShopEconomy", "Interaction")
DEPENDENCIES = ("Equipment", "Run", "Profile", "Inventory", "Character")

def declarations(root: Path):
    result = []
    for module in MODULES + DEPENDENCIES:
        for path in sorted((root / "Source/Catfishing" / module).rglob("*.h")):
            if "Tests" in path.parts:
                continue
            source = path.read_text(encoding="utf-8-sig")
            for found in re.finditer(r"\bUPROPERTY\s*\(", source):
                start = found.end()
                end, depth = start, 1
                while end < len(source) and depth:
                    depth += (source[end] == "(") - (source[end] == ")")
                    end += 1
                declaration = source[end:source.find(";", end)].strip()
                match = re.search(r"\b(\w+)\s*(?:=\s*([\s\S]+))?$", declaration)
                if not match:
                    continue
                result.append(dict(field=match[1], default=match[2] or "(implicit)",
                                   source=path.relative_to(root).as_posix(),
                                   line=source.count("\n", 0, found.start()) + 1,
                                   specifiers=source[start:end-1]))
    return result

def property_redirects(root: Path):
    """从 Config/*.ini 读 PropertyRedirects，返回 新名 -> 该名在资产里可能出现的全部旧名。"""
    chain = {}
    for path in sorted((root / "Config").glob("*.ini")):
        for line in path.read_text(encoding="utf-8-sig").splitlines():
            if line.lstrip().startswith(";"):
                continue
            found = re.search(r'PropertyRedirects\s*=\s*\(\s*OldName\s*=\s*"([^"]+)"\s*,\s*NewName\s*=\s*"([^"]+)"', line)
            if found:
                chain.setdefault(found[2].rsplit(".", 1)[-1], set()).add(found[1].rsplit(".", 1)[-1])
    # 顺着链条展开：A->B->C 时 C 也要认 A
    for _ in range(len(chain)):
        for new, olds in chain.items():
            for old in list(olds):
                olds |= chain.get(old, set())
    return chain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--fields", default=".*", help="按属性名正则过滤")
    args = parser.parse_args()
    root = args.root.resolve()
    selected = [row for row in declarations(root) if re.search(args.fields, row["field"])]
    redirects = property_redirects(root)
    for row in selected:
        row["legacy_names"] = sorted(redirects.get(row["field"], ()))
    names = sorted({row["field"] for row in selected} | {n for row in selected for n in row["legacy_names"]})
    if not names:
        raise SystemExit("No matching declarations")
    matcher = re.compile(rb"(?<![A-Za-z0-9_])(?:" + b"|".join(name.encode("ascii") for name in names) + rb")(?![A-Za-z0-9_])")
    matches = {name: [] for name in names}
    asset_count = 0
    for path in sorted((root / "Content").rglob("*")):
        if path.suffix not in (".uasset", ".umap"):
            continue
        asset_count += 1
        data = path.read_bytes()
        seen = {match.group().decode("ascii") for match in matcher.finditer(data)}
        for name in seen:
            matches[name].append(path.relative_to(root).as_posix())
    configs = [(p, p.read_text(encoding="utf-8-sig").splitlines()) for p in sorted((root / "Config").glob("*.ini"))]
    for row in selected:
        row["asset_name_hits"] = matches[row["field"]]
        # 旧名命中同样算「资产配过这个字段」——CoreRedirects 会在反序列化时把它映射到新名。
        row["legacy_name_hits"] = {name: matches[name] for name in row["legacy_names"] if matches[name]}
        row["configured_in_assets"] = bool(row["asset_name_hits"]) or bool(row["legacy_name_hits"])
        row["config_bindings"] = []
        for path, lines in configs:
            for line_no, line in enumerate(lines, 1):
                if re.search(r"(?<!\w)" + re.escape(row["field"]) + r"\s*=", line) and not line.lstrip().startswith(";"):
                    row["config_bindings"].append(f"{path.relative_to(root).as_posix()}:{line_no}:{line.strip()}")
    print(json.dumps(dict(asset_files_checked=asset_count, modules=MODULES, fields=selected,
                         limits="属性名命中不是反序列化结果，也不证明 Blueprint 图调用；不使用版本库，字段引入日期需另证。"
                         ", configured_in_assets 把 PropertyRedirects 的旧名命中也算进去；"
                              "它仍然只说明名字被序列化过，不说明值是多少。"),
                     ensure_ascii=False, indent=2))

if __name__ == "__main__":
    main()
