"""核验并重存正式鱼定义，清除已退役字段的序列化载荷。

默认只导出；-FishRetiredFieldsApply 才保存。先备份全部包，保存前后逐项比较
完整反射属性导出，任何现存属性变化都报错。脚本不修改数值或资产引用。
通过 -FishMergePhase=<名称> 区分合并审查快照；工程根从本脚本位置推导。
"""
import hashlib
import json
from pathlib import Path
import re
import shutil
import unreal


ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def export_properties(asset, output):
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = str(output)
    task.exporter = unreal.ObjectExporterT3D()
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    if not unreal.Exporter.run_asset_export_task(task):
        raise RuntimeError('Cannot export fish: ' + asset.get_path_name())
    raw = output.read_bytes()
    return raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')


def main():
    command = unreal.SystemLibrary.get_command_line()
    apply = '-FishRetiredFieldsApply' in command
    phase_match = re.search(r'-FishMergePhase=([A-Za-z0-9_-]+)', command)
    phase = phase_match[1] if phase_match else ('applied' if apply else 'verified')
    output = ROOT / 'Saved/FishRetiredFields' / phase
    output.mkdir(parents=True, exist_ok=True)
    package = json.loads((ROOT / '.harness/formal-fish-asset-input-package.json').read_text(encoding='utf-8-sig'))
    roster = package['generated_assets']['fish_definitions']
    if len(roster) != 16 or len(set(roster)) != 16 or any(
            not re.fullmatch(r'/Game/Catfishing/Data/Fish/Fish_[A-Za-z0-9]+', path) for path in roster):
        raise RuntimeError('Formal fish roster changed; review scope before saving')
    dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    plan = []
    identities = set()
    for path in roster:
        asset = unreal.load_asset(path)
        if not isinstance(asset, unreal.CatFishDefinition) or path in dirty:
            raise RuntimeError('Missing/wrong type/dirty fish: ' + path)
        identity = str(asset.get_editor_property('fish_definition_id'))
        if not identity or identity == 'None' or identity in identities:
            raise RuntimeError('Missing or duplicate identity: ' + path)
        identities.add(identity)
        relative = Path('Content') / (path.removeprefix('/Game/') + '.uasset')
        disk = ROOT / relative
        before = export_properties(asset, output / (disk.stem + '-before.t3d'))
        plan.append((asset, relative, before, digest(disk)))
    # 接收方必须已经移除旧字段；拒绝在旧模块下重存，避免误以为完成迁移。
    retired = ('FoodSafety', 'YellowStaminaGrant', 'EatingTimedEffect',
               'EatingTimedEffectDurationSeconds', 'bEatingTimedEffectConfigured')
    header = (ROOT / 'Source/Catfishing/Data/CatFishDefinition.h').read_text(encoding='utf-8-sig')
    if any(re.search(r'\b' + field + r'\s*(?:=|;)', header) for field in retired):
        raise RuntimeError('Build the retired-field-free definition before migration')
    for asset, _, _, _ in plan:
        for field in ('food_safety', 'yellow_stamina_grant', 'eating_timed_effect',
                      'eating_timed_effect_duration_seconds', 'eating_timed_effect_configured'):
            try:
                asset.get_editor_property(field)
            except Exception:
                continue
            raise RuntimeError('Loaded module still exposes retired field: ' + field)
    for asset, relative, before, before_hash in plan:
        if apply:
            backup = output / 'Backups' / before_hash / relative.name
            backup.parent.mkdir(parents=True, exist_ok=True)
            if not backup.exists():
                shutil.copy2(ROOT / relative, backup)
    report = {'applied': apply, 'phase': phase, 'fish': []}
    for asset, relative, before, before_hash in plan:
        if apply and not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError('Failed to save: ' + str(relative))
        after = export_properties(asset, output / (relative.stem + '-after.t3d'))
        if before != after:
            raise RuntimeError('Existing reflected properties changed: ' + str(relative))
        report['fish'].append({'asset': relative.as_posix(), 'before_sha256': before_hash,
                               'sha256': digest(ROOT / relative), 'properties_preserved': True,
                               'properties_sha256': hashlib.sha256(after.encode('utf-8')).hexdigest()})
    (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    unreal.log('Event=fish_retired_fields_verified Phase=' + phase + ' Count=' + str(len(plan))
               + ' Applied=' + str(apply) + ' PropertiesPreserved=true')


main()
