"""把 Scripts/Data/work01_runtime_catalog.json 里每条装备定义的 run_consumable / special_bait 两位写回真实 DataAsset。

为什么需要这个脚本：飞书钓鱼规则 §3.4（2026-08-18 拍定）把普通饵也定成一局消耗品（"进入咬钩后无论结局均消耗 1 份饵"），
Content/Catfishing/Data/Equipment/ 下四条普通饵资产（Bug/Meat/Fruit/Nectar）的 bRunConsumable 还是旧口径的 false，
运行目录校验（UCatEquipmentDefinition::IsRuntimeDefinitionReady）按新口径已要求 Bait 一律为 true，不改资产整份目录就 fail-closed。
资产是二进制，不手改；这里按 manifest 逐条写回，只改这两位，不碰其他字段，也不重写 DefaultGame.ini
（Scripts/import_work01_data_catalog.py 会整段重写 ini，那对本轮来说面太大）。

幂等：每条资产先读当前值，和 manifest 一致就跳过；只有真的改了才标脏并保存。重复运行不会产生第二次改动。

跑法（编辑器不能同时开着）：
    & 'D:\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' 'D:\\UnreaProjects\\Catfishing\\Catfishing.uproject' ^
      -run=pythonscript -script="D:\\UnreaProjects\\Catfishing\\Tools\\Python\\apply_equipment_consumable_flags.py" -unattended -nosplash
改完跑 -run=CatDataCatalogValidation 确认目录仍合法。
"""

import json
import pathlib

import unreal

PROJECT_DIR = pathlib.Path(__file__).resolve().parents[2]
MANIFEST_PATH = PROJECT_DIR / "Scripts" / "Data" / "work01_runtime_catalog.json"


def _apply_flags(asset, row) -> bool:
    """把一条 manifest 行的两位写进资产；返回是否真的改了值。Unreal Python 对 bXxx 属性暴露的名字是去掉 b 前缀的 snake_case。"""
    changed = False
    for prop, key in (("run_consumable", "run_consumable"), ("special_bait", "special_bait")):
        wanted = bool(row.get(key, False))
        current = bool(asset.get_editor_property(prop))
        if current != wanted:
            asset.set_editor_property(prop, wanted)
            changed = True
    return changed


def main() -> None:
    """读 manifest，逐条装备资产对齐两位，保存脏包，打印改了哪些。"""
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
        if _apply_flags(asset, row):
            unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
            changed_assets.append(asset_path)
    unreal.log(
        f"Catfishing equipment consumable flags applied: changed={len(changed_assets)} "
        f"missing={len(missing_assets)} manifest={MANIFEST_PATH}"
    )
    for path in changed_assets:
        unreal.log(f"  changed: {path}")
    for path in missing_assets:
        unreal.log_warning(f"  missing asset (not created here, run import_work01_data_catalog.py): {path}")


main()
