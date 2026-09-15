"""Prepare reviewed fish-data patches without writing Content or editing design sources.

Default: read the current fish table, report missing optional columns, emit JSON in Saved.
--approved-base-pool accepts approved fish_id/基础池概率 or fish_id/权重 columns;
headers explicitly marked 占位 are rejected. CSV fish_id is an asset filename, not a save ID.
The editor importer resolves and preserves the existing internal fish ID.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TABLE = ROOT / "Knowledge/Design/GDD 系统分册/鱼/鱼表格/第一版.csv"
TIME_VALUES = {"早晨": "Morning", "中午": "Day", "黄昏": "Dusk"}
WEATHER_VALUES = {"晴": "Clear", "雨": "Rain", "雾": "Fog"}


def read_rows(path: Path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        return list(reader.fieldnames or []), list(reader)


def column(headers, name):
    matches = [h for h in headers if h == name or h.startswith(name + "（") or h.startswith(name + "(")]
    if len(matches) > 1:
        raise ValueError(f"ambiguous column: {name}")
    return matches[0] if matches else None


def positive_number(raw, label):
    value = float(raw)
    if not math.isfinite(value) or value <= 0:
        raise ValueError(f"{label}: expected a finite positive value")
    return value


def prepare(table: Path, approved_pool: Path | None = None):
    headers, rows = read_rows(table)
    identity = column(headers, "fish_id")
    if not identity:
        raise ValueError("fish_id column required")
    result = {"schema": "cat-fish-runtime-patch/1", "fish": [], "base_pool": [], "gaps": []}
    seen = set()
    fields = {"试探期": "ProbeDurationSeconds", "真咬响应窗": "TrueBiteWindowSeconds"}
    for row in rows:
        fish_id = (row.get(identity) or "").strip()
        if not fish_id:
            continue
        if fish_id in seen or not fish_id.startswith("Fish_"):
            raise ValueError(f"invalid/duplicate identity: {fish_id}")
        seen.add(fish_id)
        # CSV fish_id 是资产文件名。运行 ID 必须在 UE 读取，不能把文件名写进存档身份。
        patch = {"AssetName": fish_id}
        for name, target in fields.items():
            key = column(headers, name)
            raw = (row.get(key) or "").strip() if key else ""
            if not raw:
                # Null means no authored value; never copy any Bite template into this column.
                patch[target] = None
                result["gaps"].append({"fish": fish_id, "field": target, "reason": "missing_value" if key else "missing_column"})
            else:
                patch[target] = positive_number(raw, f"{fish_id}.{name}")
                if target == "TrueBiteWindowSeconds" and not 8 <= patch[target] <= 15:
                    raise ValueError(f"{fish_id}.{name}: ordinary response must be 8 to 15 seconds")
        for name, target, allowed in (("时段", "TimeOfDay", TIME_VALUES), ("天气", "Weather", WEATHER_VALUES)):
            key = column(headers, name)
            raw = (row.get(key) or "").strip() if key else ""
            values = []
            if raw:
                for value in raw.replace("，", ",").replace("、", ",").split(","):
                    value = value.strip()
                    if value not in allowed:
                        raise ValueError(f"{fish_id}.{name}: unknown value {value}")
                    values.append(allowed[value])
            else:
                result["gaps"].append({"fish": fish_id, "field": target, "reason": "missing_value" if key else "missing_column"})
            patch[target] = values
        result["fish"].append(patch)
    if approved_pool:
        pool_headers, pool_rows = read_rows(approved_pool)
        weight_column = column(pool_headers, "基础池概率") or column(pool_headers, "权重")
        if "fish_id" not in pool_headers or not weight_column or any("占位" in h for h in pool_headers):
            raise ValueError("base pool must be an approved fish_id/权重 table; placeholder probabilities cannot be published")
        pool_ids = set()
        for row in pool_rows:
            fish_id = row["fish_id"].strip()
            if not fish_id or fish_id not in seen or fish_id in pool_ids:
                raise ValueError(f"unknown/duplicate base-pool fish: {fish_id}")
            pool_ids.add(fish_id)
            result["base_pool"].append({"AssetName": fish_id, "Probability": positive_number(row[weight_column], fish_id)})
        if not pool_ids:
            raise ValueError("approved base pool is empty")
    else:
        result["gaps"].append({"field": "BasePool", "owner": "张佳", "reason": "awaiting_approved_members_and_weights"})
    return result


def import_into_editor_memory(patch, unreal_module=None):
    """Apply a reviewed patch to loaded UE definitions only; never saves packages.

    All identities are preflighted before modifying any object. The caller must review/save
    in the editor separately. Template assets, enable gates and catalog filters are untouched.
    """
    if unreal_module is None:
        import unreal as unreal_module
    bindings = {
        "ProbeDurationSeconds": "probe_duration_seconds",
        "TrueBiteWindowSeconds": "true_bite_window_seconds",
        "TimeOfDay": "time_of_day",
        "Weather": "weather",
    }
    loaded = []
    for row in patch["fish"]:
        fish_id = row["AssetName"]
        asset = unreal_module.load_asset(f"/Game/Catfishing/Data/Fish/{fish_id}")
        if asset is None or str(asset.get_editor_property("fish_definition_id")) in ("", "None"):
            raise ValueError(f"asset missing or identity mismatch: {fish_id}")
        values = {}
        runtime_id = str(asset.get_editor_property("fish_definition_id"))
        if any(existing_id == runtime_id for _, _, existing_id in loaded):
            raise ValueError(f"duplicate runtime identity: {runtime_id}")
        for source, target in bindings.items():
            value = row[source]
            if source in ("TimeOfDay", "Weather"):
                enum = getattr(unreal_module, "CatEnvironmentTimeOfDay" if source == "TimeOfDay" else "CatEnvironmentWeather")
                value = [getattr(enum, item.upper()) for item in value]
            # 空列表示未发布，不清空已有生态限制或窗口。
            if value is not None and value != []:
                values[target] = value
        loaded.append((asset, values, runtime_id))
    for asset, values, _ in loaded:
        for field, value in values.items():
            asset.set_editor_property(field, value)
    # BasePool uses a separate config mapping, never silently edits DefaultGame.ini.
    return len(loaded)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--table", type=Path, default=TABLE)
    parser.add_argument("--approved-base-pool", type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "Saved/Migration/Batch7C/fish-runtime-patch.json")
    parser.add_argument("--import-in-editor-memory", action="store_true", help="requires UE Python; modifies loaded assets but never saves Content")
    args = parser.parse_args()
    result = prepare(args.table, args.approved_base_pool)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"fish={len(result['fish'])} base_pool={len(result['base_pool'])} configuration_gaps={len(result['gaps'])}; no assets written")
    if args.import_in_editor_memory:
        print(f"loaded assets updated in memory: {import_into_editor_memory(result)}; not saved")


if __name__ == "__main__":
    main()
