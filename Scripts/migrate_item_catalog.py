"""在 UE Python 中执行数字物品身份迁移；默认在内存中迁移预检，-ItemCatalogApply 才保存包。

使用冻结映射，不重新编号。既有包在首次写入前备份到 Saved/ItemCatalog/Backup。
预检会写入内存编号并清除旧字段，应在独立命令行编辑器进程运行，结束后退出，不手工保存该会话。
"""
import json
from pathlib import Path
import shutil
import unreal


def backup(root, asset):
    """保存前备份原包；已有备份不覆盖，保留第一次迁移前的字节。"""
    package = asset.get_path_name().split('.')[0]
    relative = Path('Content') / (package.removeprefix('/Game/') + '.uasset')
    source, target = root / relative, root / 'Saved/ItemCatalog/Backup' / relative
    if source.exists() and not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def migrate():
    """先检查所有身份与表行并在内存转换，再备份、创建缺失的扩容定义并保存；任何未知身份都中止。

    预检会修改已加载对象，但不写包；预检会话不得随后手工保存。
    应用中保存失败会立即中止并保留备份，不宣称跨包保存原子性。
    """
    root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    entries = json.loads((root / 'Config/ItemIdMigration.json').read_text(encoding='utf-8-sig'))
    apply = unreal.SystemLibrary.parse_param(unreal.SystemLibrary.get_command_line(), 'ItemCatalogApply')
    ids = {row['legacy_id']: row['item_id'] for row in entries}
    if len(ids) != len(entries) or len(set(ids.values())) != len(entries) or min(ids.values()) <= 0:
        raise RuntimeError('Invalid frozen identity map')
    prepared, new_entries = [], []
    for entry in entries:
        path = entry['definition']
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            if entry['legacy_id'] not in ('FishTankCapacityT2', 'FishTankCapacityT3'):
                raise RuntimeError('Missing existing definition: ' + path)
            new_entries.append(entry)
            continue
        asset = unreal.load_asset(path)
        if not isinstance(asset, unreal.CatInventoryItemDefinition):
            raise RuntimeError('Wrong item definition class: ' + path)
        if asset.get_editor_property('item_id') not in (0, entry['item_id']):
            raise RuntimeError('Conflicting identity: ' + path)
        # Python 将 bool 返回值作为成功标记：成功返回 OutError，失败返回 None。
        error = unreal.CatInventorySettings.migrate_legacy_item_references(asset)
        if error is None or error:
            raise RuntimeError('Cannot migrate item reference: ' + path + ' ' + str(error))
        asset.set_editor_property('item_id', entry['item_id'])
        prepared.append(asset)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    for data in registry.get_assets_by_path('/Game', recursive=True):
        if str(data.asset_class_path.asset_name) == 'CatRodSkinDefinition':
            asset = data.get_asset()
            error = unreal.CatInventorySettings.migrate_legacy_item_references(asset)
            if error is None or error:
                raise RuntimeError('Cannot migrate rod skin: ' + asset.get_path_name())
            prepared.append(asset)
    # 商店曾将巨物诱饵误写为 GiantLure；仅修复这一明确表行，不把它注册成可接受的旧存档别名。
    shop = unreal.load_asset('/Game/Catfishing/Data/Shop/DT_ShopCatalog_Default')
    shop_rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(shop))
    for row in shop_rows:
        legacy = row.get('DefinitionId', 'None')
        if legacy == 'GiantLure' and row['Name'] == 'Bait_GiantLure':
            legacy = 'GiantLureBait'
        if legacy not in ('None', ''):
            if legacy not in ids or row.get('ItemId', 0) not in (0, ids[legacy]):
                raise RuntimeError('Unknown/conflicting shop item: ' + repr(row))
            row['ItemId'] = ids[legacy]
            row['DefinitionId'] = 'None'
        if row.get('ItemId', 0) not in ids.values():
            raise RuntimeError('Shop row has no catalog identity: ' + row['Name'])
    prices = unreal.load_asset('/Game/Catfishing/Data/Shop/DT_CatFishSalePrices')
    price_rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(prices))
    for row in price_rows:
        old = row['Name']
        if old in ids:
            row['Name'] = str(ids[old])
        elif not old.isdigit() or int(old) not in ids.values():
            raise RuntimeError('Unknown fish price row: ' + old)
    prepared += [shop, prices]
    if not apply:
        unreal.log('ItemCatalogMigration: preview validated %d packages, %d new definitions' % (len(prepared), len(new_entries)))
        return
    table = unreal.load_asset('/Game/Catfishing/Data/Items/DT_ItemCatalog')
    for asset in prepared + [table]:
        backup(root, asset)
    for entry in new_entries:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.CatInventoryItemDefinition)
        package, name = entry['definition'].split('.')
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, package.rsplit('/', 1)[0], unreal.CatInventoryItemDefinition, factory)
        if not asset:
            raise RuntimeError('Cannot create capacity definition: ' + name)
        asset.set_editor_property('item_id', entry['item_id'])
        tier = 2 if entry['item_id'] == 42 else 3
        asset.set_editor_property('inventory_display_name', unreal.Text('鱼缸扩容（%d 级）' % tier))
        asset.set_editor_property('inventory_description', unreal.Text('共享鱼缸容量提升至 %d 条。' % (20 if tier == 2 else 30)))
        # 扩容仍由交易分支直接执行，不把服务商品变成可丢弃、放置的实物。
        asset.set_editor_property('inventory_actions', [])
        prepared.append(asset)
    rows = [dict(Name=str(entry['item_id']), ItemId=entry['item_id'], ItemDefinition=entry['definition']) for entry in entries]
    for asset, data in [(table, rows), (shop, shop_rows), (prices, price_rows)]:
        if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(asset, json.dumps(data, ensure_ascii=False)):
            raise RuntimeError('Cannot fill table: ' + asset.get_path_name())
    for asset in prepared + [table]:
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError('Cannot save migrated asset: ' + asset.get_path_name())
    unreal.log('ItemCatalogMigration: saved %d catalog identities and migrated references' % len(entries))


migrate()
