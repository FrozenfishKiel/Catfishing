"""把 Scripts/Data/work01_runtime_catalog.json 里每条鱼竿定义的 rod_strength / max_line_meters 写回真实 DataAsset。

为什么需要这个脚本：飞书装备册与裁决同步已拍定鱼竿有两个彼此独立的字段——强度（静态，25/60/130，参与遛鱼判定表 4.3 第①条）与
放线上限 L_max（60/80/100 米，4.3 最后一行"L ≥ L_max 时右键失效"），而 Content/Catfishing/Data/Equipment/ 下两根竿资产只有耐久。
UCatEquipmentDefinition 已新增 RodStrength / MaximumLineLengthMeters，运行目录校验要求 Rod 两者为正，不写资产整份目录就 fail-closed。
资产是二进制，不手改；这里按 manifest 逐条写回，只改这两个字段，不碰其他字段，也不重写 DefaultGame.ini
（Scripts/import_work01_data_catalog.py 会整段重写 ini，面太大）。3 级竿没有资产，这里不会凭空造。

幂等：每条资产先读当前值，和 manifest 一致就跳过；只有真的改了才标脏并保存。重复运行不会产生第二次改动。

跑法（编辑器不能同时开着）：
    & 'D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/UnreaProjects/Catfishing/Catfishing.uproject' ^
      -run=pythonscript -script="D:/UnreaProjects/Catfishing/Tools/Python/apply_rod_fight_stats.py" -unattended -nosplash
改完跑 -run=CatDataCatalogValidation 确认目录仍合法。
"""

import json
import pathlib

import unreal

PROJECT_DIR = pathlib.Path(__file__).resolve().parents[2]
MANIFEST_PATH = PROJECT_DIR / "Scripts" / "Data" / "work01_runtime_catalog.json"


def _apply_rod_stats(asset, row) -> bool:
    """把一条 manifest 行的强度与放线上限写进资产；返回是否真的改了值。非 Rod 行 manifest 里没有这两个键，按 0 对齐。"""
    changed = False
    for prop, key in (("rod_strength", "rod_strength"), ("maximum_line_length_meters", "max_line_meters")):
        wanted = float(row.get(key, 0.0))
        current = float(asset.get_editor_property(prop))
        if abs(current - wanted) > 1e-9:
            asset.set_editor_property(prop, wanted)
            changed = True
    return changed


def main() -> None:
    """读 manifest，逐条装备资产对齐两个竿字段，保存脏包，打印改了哪些。"""
    with MANIFEST_PATH.open("r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    catalog = manifest["catalogs"]["equipment"]
    asset_root = catalog["asset_root"]
    changed_assets = []
    missing_assets = []
    for row in catalog["definitions"]:
        asset_path = f"{asset_root}/{row['asset_name']}"
        if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            missing_assets.append(asset_path)
            continue
        asset = unreal.load_asset(asset_path)
        if _apply_rod_stats(asset, row):
            unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
            changed_assets.append(asset_path)
    unreal.log(
        f"Catfishing rod fight stats applied: changed={len(changed_assets)} "
        f"missing={len(missing_assets)} manifest={MANIFEST_PATH}"
    )
    for path in changed_assets:
        unreal.log(f"  changed: {path}")
    for path in missing_assets:
        unreal.log_warning(f"  missing asset (not created here, run import_work01_data_catalog.py): {path}")


main()
