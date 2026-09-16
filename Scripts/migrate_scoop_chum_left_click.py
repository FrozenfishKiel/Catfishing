"""在 UE Python commandlet 中核查统一左键配置，安全删除无引用的旧 F/Q 入口。"""

import json
from pathlib import Path
import unreal

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
options = unreal.AssetRegistryDependencyOptions(True, True, True, True, True)
legacy = [
    '/Game/Blueprint/Abilities/BP_GA_Scoop',
    '/Game/Blueprint/Abilities/BP_GA_Chum',
    '/Game/Input/InputAction/IA_CatchFish',
    '/Game/Input/InputAction/IA_BaitSpot',
]
report = {'references': {}, 'ability_sets': {}, 'mappings': [], 'deleted': []}
for path in legacy:
    refs = [str(x) for x in (registry.get_referencers(path, options) or [])]
    report['references'][path] = refs
    if refs:
        raise RuntimeError(f'旧入口仍有消费者，停止删除：{path}: {refs}')
for asset in registry.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Catfishing', 'CatAbilitySet'), True):
    obj = asset.get_asset()
    classes = [str(row.get_editor_property('ability')) for row in obj.get_editor_property('granted_abilities')]
    report['ability_sets'][str(asset.package_name)] = classes
    if any('FishingScoop' in value or 'BP_GA_Scoop' in value or 'BP_GA_Chum' in value for value in classes):
        raise RuntimeError(f'AbilitySet 仍授予旧入口：{asset.package_name}')
context = unreal.load_asset('/Game/Input/InputContext/IMC_InputContext')
if not context:
    raise RuntimeError('缺少正式 IMC')
for mapping in context.get_editor_property('default_key_mappings').get_editor_property('mappings'):
    action = mapping.get_editor_property('action')
    row = {'action': action.get_path_name() if action else '', 'key': mapping.get_editor_property('key').export_text()}
    report['mappings'].append(row)
    if any(path in row['action'] for path in legacy):
        raise RuntimeError(f'正式 IMC 仍绑定旧动作：{row}')
if not any('LeftMouseButton' in row['key'] for row in report['mappings']):
    raise RuntimeError('正式 IMC 没有左键映射')
for path in legacy:
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        if not unreal.EditorAssetLibrary.delete_asset(path):
            raise RuntimeError(f'删除失败：{path}')
        # Commandlet 的删除返回值不保证包文件已落盘移除；引用审计通过后核对精确文件。
        package_file = Path(unreal.Paths.project_content_dir()) / (path.removeprefix('/Game/') + '.uasset')
        if package_file.exists():
            package_file.unlink()
        report['deleted'].append(path)
out = Path(unreal.Paths.project_saved_dir()) / 'ScoopChum'
out.mkdir(parents=True, exist_ok=True)
(out / 'InputMigration.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('Event=scoop_chum_input_cleanup_completed Deleted=' + str(len(report['deleted'])))
