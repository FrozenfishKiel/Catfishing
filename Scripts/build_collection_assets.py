"""接入图鉴输入资产并导入独立书页底图；文字、鱼图和卡片仍动态绘制。"""
from pathlib import Path
import csv
import unreal


def build():
    """补齐图鉴输入入口，再导入独立底图和缺失名称，最后迁移正式 WBP；不覆盖已有输入选择。"""
    root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    # 页面控制器只绑定既有 Action，不会自行创建按键资产；缺失时必须在作者流程接入正式 IMC。
    context = unreal.load_asset('/Game/Input/InputContext/IMC_InputContext')
    if not context:
        raise RuntimeError('Missing gameplay input context')
    action_path = '/Game/Input/InputAction/IA_Collection'
    action = unreal.load_asset(action_path) if unreal.EditorAssetLibrary.does_asset_exist(action_path) else None
    mappings = context.get_editor_property('default_key_mappings').get_editor_property('mappings')
    mapped = action and any(m.action == action for m in mappings)
    key = unreal.Key()
    key.import_text('M')
    # 已有映射由策划维护；只给完全未映射的动作补 M，冲突时中止而不是抢占其他功能。
    if not mapped and any(m.key == key for m in mappings):
        raise RuntimeError('M is already assigned; preserve existing input bindings')
    if not action:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.InputAction)
        action = unreal.AssetToolsHelpers.get_asset_tools().create_asset('IA_Collection', '/Game/Input/InputAction', unreal.InputAction, factory)
    if not action or action.get_editor_property('value_type') != unreal.InputActionValueType.BOOLEAN:
        raise RuntimeError('Collection action must be Boolean')
    if not mapped:
        context.map_key(action, key)
        # 先保存 Action 再保存引用它的 IMC；第二步失败时保留已创建资产，下次重跑可继续接线。
        if not unreal.EditorAssetLibrary.save_loaded_asset(action, only_if_is_dirty=False) or not unreal.EditorAssetLibrary.save_loaded_asset(context, only_if_is_dirty=False):
            raise RuntimeError('Collection input asset save failed')
    unreal.log('CollectionAssets: collection input mapping ready')
    # 展示名缺失会让图鉴和库存同时出现空白；只补策划已有名称，不触碰重量、权重或其他玩法数值。
    fish_table = root / 'Knowledge/Design/GDD 系统分册/鱼/鱼表格/第一版.csv'
    rows = list(csv.DictReader(fish_table.open(encoding='utf-8-sig', newline='')))
    identity_column = next(key for key in rows[0] if key.startswith('fish_id'))
    names = [('/Game/Catfishing/Data/Fish/' + row[identity_column], row['名字']) for row in rows]
    names += [('/Game/Catfishing/Data/Equipment/Equip_Bait_' + suffix, name) for suffix, name in
              [('Bug', '虫虫饵'), ('Meat', '肉块饵'), ('Fruit', '果实饵'), ('Nectar', '花蜜饵'), ('Moonlight', '月光饵')]]
    for package, name in names:
        asset = unreal.load_asset(package)
        if not asset:
            raise RuntimeError('Missing formal definition: ' + package)
        if not str(asset.get_editor_property('display_name')).strip():
            asset.set_editor_property('display_name', name)
            if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
                raise RuntimeError('Display name save failed: ' + package)
    task = unreal.AssetImportTask()
    task.filename = str(root / 'SourceArt/UI/Collection/CollectionBook.png')
    task.destination_path = '/Game/UI/Collection'
    task.destination_name = 'T_CollectionBook'
    task.automated = True
    task.replace_existing = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset('/Game/UI/Collection/T_CollectionBook')
    if not texture:
        raise RuntimeError('Collection book texture import failed')
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property('never_stream', True)
    if not unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False):
        raise RuntimeError('Collection book texture save failed')
    unreal.log('CollectionAssets: imported independent book background')
    if not unreal.CatCollectionAuthoringLibrary.migrate_collection_widget():
        raise RuntimeError('Collection WBP has custom content or migration failed; original package preserved unless saved successfully')


build()
