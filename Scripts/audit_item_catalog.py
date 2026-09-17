"""在 UE Python 命令行中只读盘点物品身份和资产引用，输出到 Saved/ItemCatalog。

不保存任何 UE 资产；核对数字总表、冻结迁移映射和真实定义，不重新分配编号。
"""
import json
from pathlib import Path

import unreal


def audit():
    """只读扫描物品及资产引用；遗漏、重复身份或冻结映射漂移直接报错，不改历史编号。"""
    root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    entries = []
    identities = {}
    frozen = json.loads((root / 'Config/ItemIdMigration.json').read_text(encoding='utf-8-sig'))
    expected = {entry['definition']: entry for entry in frozen}
    table = unreal.load_asset('/Game/Catfishing/Data/Items/DT_ItemCatalog')
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    catalog = {}
    for row in rows:
        item_id = int(row['ItemId'])
        if item_id <= 0 or row['Name'] != str(item_id) or item_id in catalog:
            raise RuntimeError('Invalid/duplicate catalog row: ' + str(row))
        catalog[item_id] = row['ItemDefinition']
    for data in registry.get_assets_by_path('/Game', recursive=True):
        # 只加载可能携带项目物品定义的资产，不加载地图、贴图及第三方场景资源。
        class_name = str(data.asset_class_path.asset_name)
        if class_name not in ('CatFishDefinition', 'CatEquipmentDefinition', 'CatInventoryItemDefinition'):
            continue
        asset = data.get_asset()
        item_id = int(asset.get_editor_property('item_id'))
        path = asset.get_path_name()
        if item_id <= 0 or item_id in identities or catalog.get(item_id) != path:
            raise RuntimeError('Missing/duplicate/unindexed identity: ' + str(item_id) + ' ' + path)
        if path in expected and expected[path]['item_id'] != item_id:
            raise RuntimeError('Frozen identity changed: ' + path)
        identities[item_id] = path
        entry = dict(item_id=item_id, definition=path,
                     referencers=sorted(str(p) for p in registry.get_referencers(
                         data.package_name, unreal.AssetRegistryDependencyOptions(
                             include_soft_package_references=True, include_hard_package_references=True,
                             include_searchable_names=True, include_soft_management_references=True,
                             include_hard_management_references=True))))
        if isinstance(asset, unreal.CatFishDefinition):
            vector = asset.get_editor_property('chum_preference')
            entry['chum'] = [vector.fishy, vector.fragrant, vector.fermented]
            entry['bait_weights'] = [dict(item_id=v.bait_item_id, multiplier=v.multiplier)
                                     for v in asset.get_editor_property('bait_weight_multipliers')]
        entries.append(entry)
    entries.sort(key=lambda entry: entry['item_id'])
    if len(entries) != len(catalog):
        raise RuntimeError('Catalog contains missing or unsupported definition assets')
    if not entries:
        raise RuntimeError('No item definitions found; migration cannot proceed.')
    output = root / 'Saved/ItemCatalog/current-catalog.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(entries, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('ItemCatalogAudit: %d definitions; output=%s' % (len(entries), output))


audit()
