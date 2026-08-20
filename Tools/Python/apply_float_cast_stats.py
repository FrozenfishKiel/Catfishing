"""把 Scripts/Data/work01_runtime_catalog.json 里每条鱼漂定义的射程、精准度偏移半径与功能路线写回真实 DataAsset。

为什么需要这个脚本：飞书装备册已裁定三种漂的射程（羽毛漂 3／毛线球漂 5／铃铛漂 7 米），精准度只给了高/中/低三档
定性描述、没有数值（数值归数值阶段，见 Docs/Development/工程自补决策记录.md D-23）。
`UCatEquipmentDefinition` 已新增 `FloatCastRangeMeters` / `FloatAccuracyOffsetRadiusMeters`，
运行目录校验要求 Float 两者为正，不写资产整份目录就 fail-closed；同时抛竿落点与遛鱼开局的 D₀ 都要读这两项。
另外三条漂的 `FunctionalRouteId` 原本把射程和精准度编进了路线名（`FloatRange3_PrecisionHigh` 这种），
manifest 的 route_id_policy 明确"专用字段已经拥有的数值不能再进路线名"，所以这轮一并改成纯身份名。

资产是二进制，不手改；这里按 manifest 逐条写回，只改这三个字段，不碰其他字段，也不重写 DefaultGame.ini
（Scripts/import_work01_data_catalog.py 会整段重写 ini，面太大）。manifest 里没有这几个键的行（竿、饵、窝料）一律跳过，
不会被顺手清零。

幂等：每条资产先读当前值，和 manifest 一致就跳过；只有真的改了才标脏并保存。重复运行不会产生第二次改动。

跑法（编辑器不能同时开着）：
    & 'D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/UnreaProjects/Catfishing/Catfishing.uproject' ^
      -run=pythonscript -script="D:/UnreaProjects/Catfishing/Tools/Python/apply_float_cast_stats.py" -unattended -nosplash
改完跑 -run=CatDataCatalogValidation 确认目录仍合法。
"""

import json
import pathlib

import unreal

PROJECT_DIR = pathlib.Path(__file__).resolve().parents[2]
MANIFEST_PATH = PROJECT_DIR / "Scripts" / "Data" / "work01_runtime_catalog.json"


def _apply_float_stats(asset, row) -> bool:
    """把一条 manifest 行的漂射程、精准度半径与路线写进资产；返回是否真的改了值。

    只处理 manifest 里显式给了 float_range_meters 的行，也就是三条漂；其余类别一律原样不动，
    避免"把非漂资产的漂字段对齐成 0"这种看起来无害、实际会静默改内容 Hash 的副作用。
    """
    if "float_range_meters" not in row:
        return False
    changed = False
    for prop, key in (
        ("float_cast_range_meters", "float_range_meters"),
        ("float_accuracy_offset_radius_meters", "float_accuracy_radius_meters"),
    ):
        wanted = float(row[key])
        current = float(asset.get_editor_property(prop))
        if abs(current - wanted) > 1e-9:
            asset.set_editor_property(prop, wanted)
            changed = True
    wanted_route = unreal.Name(row["route"])
    if str(asset.get_editor_property("functional_route_id")) != str(wanted_route):
        asset.set_editor_property("functional_route_id", wanted_route)
        changed = True
    return changed


def main() -> None:
    """读 manifest，逐条鱼漂资产对齐三个字段，保存脏包，打印改了哪些。"""
    with MANIFEST_PATH.open("r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    catalog = manifest["catalogs"]["equipment"]
    asset_root = catalog["asset_root"]
    changed_assets = []
    missing_assets = []
    for row in catalog["definitions"]:
        if "float_range_meters" not in row:
            continue
        asset_path = f"{asset_root}/{row['asset_name']}"
        if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            missing_assets.append(asset_path)
            continue
        asset = unreal.load_asset(asset_path)
        if _apply_float_stats(asset, row):
            unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
            changed_assets.append(asset_path)
    unreal.log(
        f"Catfishing float cast stats applied: changed={len(changed_assets)} "
        f"missing={len(missing_assets)} manifest={MANIFEST_PATH}"
    )
    for path in changed_assets:
        unreal.log(f"  changed: {path}")
    for path in missing_assets:
        unreal.log_warning(f"  missing asset (not created here, run import_work01_data_catalog.py): {path}")


main()
