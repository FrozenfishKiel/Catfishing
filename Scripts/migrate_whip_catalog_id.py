"""解决皮鞭与小鱼干的44号冲突：远端44–50保持，皮鞭迁至51。

UE Python默认只读核验；显式-WhipCatalogApply才保存。此迁移不转换旧存档；
仅适用于已确认无须保留44号皮鞭存档的开发合并。先全量预检并备份两包。
"""
import hashlib
import json
from pathlib import Path
import shutil
import unreal

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'Saved/ItemCatalog/Whip51Migration'
CATALOG = '/Game/Catfishing/Data/Items/DT_ItemCatalog'
WHIP = '/Game/Catfishing/Data/Items/Item_Whip'
KEYS = ('DriedFish', 'PawGloves', 'LuckyClover', 'WaterSprayer', 'FakeFish', 'Horn', 'CastNet')


def rows_of(table):
    return json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def export_whip(asset, label):
    path = OUT / (label + '.t3d')
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = str(path)
    task.exporter = unreal.ObjectExporterT3D()
    task.automated = task.replace_identical = True
    task.prompt = False
    if not unreal.Exporter.run_asset_export_task(task): raise RuntimeError('Cannot export whip definition')
    raw = path.read_bytes()
    text = raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')
    return '\n'.join(line for line in text.splitlines() if not line.strip().startswith('ItemId='))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    apply = '-WhipCatalogApply' in unreal.SystemLibrary.get_command_line()
    table, whip = unreal.load_asset(CATALOG), unreal.load_asset(WHIP)
    if not isinstance(table, unreal.DataTable) or not isinstance(whip, unreal.CatInventoryItemDefinition):
        raise RuntimeError('Missing catalog or whip definition')
    dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    if CATALOG in dirty or WHIP in dirty: raise RuntimeError('Refusing dirty packages')
    original = rows_of(table)
    expected = {}
    seen = set()
    for row in original:
        number = row['ItemId']
        if number <= 0 or row['Name'] != str(number) or number in seen:
            raise RuntimeError('Invalid existing row: ' + str(row))
        seen.add(number)
        if number <= 43: expected[number] = row
    if set(expected) != set(range(1, 44)): raise RuntimeError('Historical roster changed')
    for number, key in enumerate(KEYS, 44):
        path = '/Game/Catfishing/Data/Items/Item_' + key
        asset = unreal.load_asset(path)
        if not isinstance(asset, unreal.CatInventoryItemDefinition) or asset.get_editor_property('item_id') != number:
            raise RuntimeError('Remote identity changed: ' + path)
        expected[number] = {'Name': str(number), 'ItemId': number, 'ItemDefinition': asset.get_path_name()}
    expected[51] = {'Name': '51', 'ItemId': 51, 'ItemDefinition': whip.get_path_name()}
    for row in original:
        if row != expected.get(row['ItemId']) and row != {'Name': '44', 'ItemId': 44, 'ItemDefinition': whip.get_path_name()}:
            raise RuntimeError('Unreviewed catalog change: ' + str(row))
    if whip.get_editor_property('item_id') not in (44, 51): raise RuntimeError('Unexpected whip identity')
    merged = [expected[n] for n in sorted(expected)]
    before_properties = export_whip(whip, 'before' if apply else 'reloaded')
    hashes = {}
    for package in (CATALOG, WHIP):
        disk = ROOT / ('Content/' + package.removeprefix('/Game/') + '.uasset')
        sha = hashlib.sha256(disk.read_bytes()).hexdigest()
        hashes[str(disk.relative_to(ROOT)).replace('\\', '/')] = {'before': sha}
        if apply:
            backup = OUT / 'Backups' / sha / disk.name
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(disk, backup)
    if apply:
        if (OUT / 'applied.json').exists(): raise RuntimeError('Already applied; use read-only verification')
        whip.set_editor_property('item_id', 51)
        if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(merged)):
            raise RuntimeError('Cannot fill merged catalog')
        if export_whip(whip, 'after') != before_properties: raise RuntimeError('Other whip properties changed')
        for asset in (whip, table):
            if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
                raise RuntimeError('Save failed; restore backed-up packages before retry')
    if sorted(rows_of(table), key=lambda row: row['ItemId']) != merged or whip.get_editor_property('item_id') != 51:
        raise RuntimeError('Migration not applied or saved rows differ')
    paths = set()
    for row in merged:
        asset = unreal.load_asset(row['ItemDefinition'])
        if not asset or asset.get_editor_property('item_id') != row['ItemId'] or row['ItemDefinition'] in paths:
            raise RuntimeError('Definition missing, mismatched, or duplicated: ' + str(row))
        paths.add(row['ItemDefinition'])
    for relative in hashes:
        hashes[relative]['after'] = hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()
    baseline_path = OUT / 'applied.json'
    if not apply and baseline_path.exists():
        baseline = json.loads(baseline_path.read_text(encoding='utf-8'))
        if baseline['whip_preserved_properties'] != before_properties: raise RuntimeError('Whip reload changed properties')
        if any(item['after'] != baseline['hashes'][path]['after'] for path, item in hashes.items()):
            raise RuntimeError('Package changed since migration')
    report = {'success': True, 'applied': apply, 'rows': merged, 'hashes': hashes,
              'whip_preserved_properties': before_properties, 'save_migration': 'Not requested; no old whip saves to retain'}
    (OUT / ('applied.json' if apply else 'verified.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('Event=whip_catalog_merge_verified Rows=51 WhipId=51 RemoteIds=44-50 ExistingIds=1-43 Applied=' + str(apply))


main()
