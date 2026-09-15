"""定向同步基础放线恢复；默认审计，传 -StaminaApply 才保存正式搏斗平衡包。

来源：数值成长 §5。只修改 slack_stamina_regen_per_second；食用效果暂不接入。
"""
import json
from pathlib import Path
import unreal


def main():
    root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    apply = '-StaminaApply' in unreal.SystemLibrary.get_command_line()
    changes = []
    packages = []
    package = '/Game/Catfishing/Data/Fishing/DA_FishingFightBalance_Default'
    balance = unreal.load_asset(package)
    if not balance:
        raise RuntimeError('Missing fight balance')
    packages.append((package, balance, 'slack_stamina_regen_per_second', 0.0))
    dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    for package, asset, field, target in packages:
        if package in dirty:
            raise RuntimeError('Unsaved edits in migration target: ' + package)
        before = float(asset.get_editor_property(field))
        if apply and before != target:
            asset.set_editor_property(field, target)
            if hasattr(asset, 'is_runtime_definition_ready') and not asset.is_runtime_definition_ready():
                raise RuntimeError('Migration would invalidate definition: ' + package)
            if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
                raise RuntimeError('Save failed: ' + package)
        after = float(asset.get_editor_property(field))
        changes.append(dict(package=package, field=field, before=before, target=target,
                            after=after, ready=asset.is_runtime_definition_ready() if hasattr(asset, 'is_runtime_definition_ready') else None,
                            readiness_evidence='Native test required' if not hasattr(asset, 'is_runtime_definition_ready') else 'Python binding',
                            matches=after == target))
    blueprints = {}
    for package in ['/Game/UI/HUD/WBP_CatHUD', '/Game/Character/BP_CuteCatCharacter',
                    '/Game/Character/BP_CatCharacter', '/Game/Game/BP_CatFishingGamemode']:
        cls = unreal.EditorAssetLibrary.load_blueprint_class(package)
        blueprints[package] = cls.get_path_name() if cls else None
    report = dict(applied=apply, changes=changes, blueprint_classes_loaded=blueprints)
    out = root / 'Saved/StaminaValidation'
    out.mkdir(parents=True, exist_ok=True)
    (out / ('data-migration.json' if apply else 'data-audit.json')).write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('Event=stamina_data_audited ' + json.dumps(report, ensure_ascii=False))
    if not all(x['matches'] and x['ready'] is not False for x in changes) or not all(blueprints.values()):
        raise RuntimeError('Stamina data/blueprint audit incomplete; inspect report')


main()
