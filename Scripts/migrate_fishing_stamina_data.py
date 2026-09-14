"""定向同步黄色体力与基础放线恢复；默认审计，传 -StaminaApply 才保存四个正式包。

来源：鱼表「限时Buff」列、数值成长 §4/§5。只修改 yellow_stamina_grant
和 slack_stamina_regen_per_second，不把鱼表其他新模型覆盖到现有鱼资产。
"""
import csv
import hashlib
import json
from pathlib import Path
import re
import unreal


def main():
    root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    table = root / 'Knowledge/Design/GDD 系统分册/鱼/鱼表格/第一版.csv'
    rows = list(csv.DictReader(table.open(encoding='utf-8-sig', newline='')))
    id_column = next(key for key in rows[0] if key.startswith('fish_id'))
    grants = {}
    for row in rows:
        buff = row['限时Buff']
        if '黄色体力' not in buff:
            continue
        match = re.search(r'黄色体力\s*\+(\d+(?:\.\d+)?)', buff)
        if not match:
            raise RuntimeError('Unresolved yellow stamina data: ' + row[id_column])
        grants[row[id_column]] = float(match.group(1))
    if set(grants) != {'Fish_LittleColor', 'Fish_Windbell', 'Fish_Blackfish'}:
        raise RuntimeError('Fish table changed: review migration scope before saving')
    apply = '-StaminaApply' in unreal.SystemLibrary.get_command_line()
    changes = []
    packages = []
    for fish_id, grant in grants.items():
        package = '/Game/Catfishing/Data/Fish/' + fish_id
        fish = unreal.load_asset(package)
        if not isinstance(fish, unreal.CatFishDefinition) or not str(fish.get_editor_property('fish_definition_id')):
            raise RuntimeError('Missing fish definition: ' + package)
        # 表内 Fish_* 是正式包名；本地既有运行 ID（如 LittleColorFish）继续保留，避免破坏库存/存档引用。
        unreal.log('Event=stamina_fish_id_mapping TableId=' + fish_id + ' Package=' + package
                   + ' RuntimeId=' + str(fish.get_editor_property('fish_definition_id')))
        packages.append((package, fish, 'yellow_stamina_grant', grant))
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
    report = dict(applied=apply, table=str(table), table_sha256=hashlib.sha256(table.read_bytes()).hexdigest(),
                  grants=grants, changes=changes, blueprint_classes_loaded=blueprints)
    out = root / 'Saved/StaminaValidation'
    out.mkdir(parents=True, exist_ok=True)
    (out / ('data-migration.json' if apply else 'data-audit.json')).write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('Event=stamina_data_audited ' + json.dumps(report, ensure_ascii=False))
    if not all(x['matches'] and x['ready'] is not False for x in changes) or not all(blueprints.values()):
        raise RuntimeError('Stamina data/blueprint audit incomplete; inspect report')


main()
