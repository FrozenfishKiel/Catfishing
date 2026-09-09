"""把既有 ShopRodT2 正式定义收口为玻璃纤维竿。

商店 DataTable 已经使用 Rod_ShopT2 -> ShopRodT2，并显示“玻璃纤维竿”。
本脚本补齐 EquipmentDefinition 自身的玩家可见名称和说明，使背包、营地公共
仓库及其他不读取商店覆盖文案的界面也使用同一口径。现有玩法数值不在本脚本中
重平衡。
"""

import json

import unreal


ROD_ASSET_PATH = "/Game/Catfishing/Data/Equipment/Equip_Rod_ShopT2"
SHOP_TABLE_PATH = "/Game/Catfishing/Data/Shop/DT_ShopCatalog_Default"
SHOP_ROW_NAME = "Rod_ShopT2"
DEFINITION_ID = "ShopRodT2"
DISPLAY_NAME = "玻璃纤维竿"
DESCRIPTION = "玻璃纤维材质的进阶鱼竿，提供更高的钓鱼强度。"


def _load(path):
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError(f"无法加载资产: {path}")
    return asset


def _verify_shop_mapping():
    table = _load(SHOP_TABLE_PATH)
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    row = next((entry for entry in rows if entry.get("Name") == SHOP_ROW_NAME), None)
    if row is None:
        raise RuntimeError(f"商店缺少鱼竿行: {SHOP_ROW_NAME}")
    if row.get("DefinitionId") != DEFINITION_ID:
        raise RuntimeError(
            f"商店鱼竿映射错误: row={SHOP_ROW_NAME} actual={row.get('DefinitionId')} "
            f"expected={DEFINITION_ID}"
        )
    if DISPLAY_NAME not in row.get("DisplayNameOverride", ""):
        raise RuntimeError(
            f"商店鱼竿显示名未统一为 {DISPLAY_NAME}: {row.get('DisplayNameOverride')}"
        )


def main():
    _verify_shop_mapping()
    rod = _load(ROD_ASSET_PATH)
    actual_id = str(rod.get_editor_property("equipment_definition_id"))
    if actual_id != DEFINITION_ID:
        raise RuntimeError(
            f"鱼竿定义 ID 错误: actual={actual_id} expected={DEFINITION_ID}"
        )

    previous_name = str(rod.get_editor_property("display_name"))
    previous_description = str(rod.get_editor_property("description"))
    rod.set_editor_property("display_name", DISPLAY_NAME)
    rod.set_editor_property("description", DESCRIPTION)
    if not unreal.EditorAssetLibrary.save_asset(ROD_ASSET_PATH, only_if_is_dirty=False):
        raise RuntimeError(f"无法保存鱼竿定义: {ROD_ASSET_PATH}")

    saved_name = str(rod.get_editor_property("display_name"))
    saved_description = str(rod.get_editor_property("description"))
    if saved_name != DISPLAY_NAME or saved_description != DESCRIPTION:
        raise RuntimeError(
            f"鱼竿定义保存后字段不匹配: name={saved_name} description={saved_description}"
        )
    unreal.log(
        "GLASS_FIBER_ROD_CONFIGURED "
        f"DefinitionId={DEFINITION_ID} ShopRow={SHOP_ROW_NAME} "
        f"DisplayName={saved_name} PreviousName={previous_name} "
        f"PreviousDescription={previous_description} GameplayValuesChanged=false"
    )


main()
