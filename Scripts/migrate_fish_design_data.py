"""UE 内定向迁移鱼表已批准数据。默认输出预览，-FishDesignApply 保存，-FishDesignVerify 只读核验。

保留运行 ID、模型/动画引用与既有区域；不编造食性概率、GE 或新增鱼种。
工程根由 UE project_dir 推导。全部计划在任何保存前构建，脏包一律拒绝。
"""
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import unreal


def column(row, prefix):
    keys = [k for k in row if k == prefix or k.startswith(prefix + '（') or k.startswith(prefix + '(')]
    if len(keys) != 1:
        raise ValueError('Missing/ambiguous column: ' + prefix)
    return row[keys[0]].strip()


def number(raw, label, zero=False):
    value = float(raw)
    if not math.isfinite(value) or value < 0 or (not zero and value == 0):
        raise ValueError('Invalid numeric value: ' + label)
    return value


def interval(raw):
    values = [number(v.strip(), raw) for v in raw.split('~')]
    if len(values) != 2 or values[1] < values[0]:
        raise ValueError('Invalid interval: ' + raw)
    return unreal.Vector2D(*values)


def serial(value):
    if isinstance(value, (str, bool, int, float)) or value is None:
        return value
    if isinstance(value, unreal.Vector2D):
        return [value.x, value.y]
    if isinstance(value, unreal.Vector):
        return [value.x, value.y, value.z]
    if isinstance(value, unreal.CatFishBodyGeometry):
        return {field: serial(value.get_editor_property(field)) for field in (
            'mouth_local_position_centimeters', 'center_of_mass_local_position_centimeters',
            'scale_origin_local_centimeters', 'yaw_radius_of_gyration_centimeters')}
    if isinstance(value, unreal.CatChumVector):
        return [value.fishy, value.fragrant, value.fermented]
    if isinstance(value, (unreal.Array, list, tuple)):
        return [serial(v) for v in value]
    if isinstance(value, unreal.CatBaitWeightMultiplier):
        return dict(bait=str(value.bait_definition_id), multiplier=value.multiplier)
    return str(value)


def run():
    root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    out = root / 'Saved/FishAlignment'
    out.mkdir(parents=True, exist_ok=True)
    table = root / 'Knowledge/Design/GDD 系统分册/鱼/鱼表格/第一版.csv'
    bait_table = table.with_name('饵权重.csv')
    rows = list(csv.DictReader(table.open(encoding='utf-8-sig', newline='')))
    bait_rows = {r['fish_id']: r for r in csv.DictReader(bait_table.open(encoding='utf-8-sig', newline='')) if r['fish_id']}
    if len(rows) != 16 or len(bait_rows) != 16:
        raise ValueError('Fish roster changed; review migration scope')
    rarity = {'普通': 'Common', '少见': 'Uncommon', '稀有': 'Rare', '珍稀': 'VeryRare'}
    diets = {'食肉': unreal.CatFishDiet.CARNIVORE, '杂食': unreal.CatFishDiet.OMNIVORE, '素食': unreal.CatFishDiet.HERBIVORE}
    bait_ids = {}
    for name, suffix in [('虫虫饵','Bug'),('肉块饵','Meat'),('果实饵','Fruit'),('花蜜饵','Nectar'),('月光饵','Moonlight')]:
        asset = unreal.load_asset('/Game/Catfishing/Data/Equipment/Equip_Bait_' + suffix)
        if not asset:
            raise ValueError('Missing bait: ' + suffix)
        bait_ids[name] = str(asset.get_editor_property('equipment_definition_id'))
    dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    plan, identities, runtime_ids = [], {}, set()
    for row in rows:
        name = column(row, 'fish_id')
        if not re.fullmatch(r'Fish_[A-Za-z0-9]+', name) or name in identities:
            raise ValueError('Invalid/duplicate package name: ' + name)
        package = '/Game/Catfishing/Data/Fish/' + name
        fish = unreal.load_asset(package)
        if not isinstance(fish, unreal.CatFishDefinition) or package in dirty:
            raise ValueError('Missing/wrong type/dirty fish package: ' + package)
        identity = str(fish.get_editor_property('fish_definition_id'))
        if not identity or identity == 'None' or identity in runtime_ids:
            raise ValueError('Missing/duplicate runtime ID: ' + package)
        runtime_ids.add(identity)
        identities[name] = identity
        weights = interval(row['重量/KG'].strip())
        axis = row['需求窝料属性'].strip()
        if axis not in ('腥','香','酵'):
            raise ValueError('Unknown chum class: ' + name)
        chum = unreal.CatChumVector()
        for field, category in [('fishy','腥'),('fragrant','香'),('fermented','酵')]:
            chum.set_editor_property(field, 1.0 if axis == category else 0.0)
        edible = name not in ('Fish_Salted','Fish_LakeGiantShadow')
        values = {
            'minimum_weight_kilograms': weights.x,
            'maximum_weight_kilograms': weights.y,
            'fish_strength_per_kilogram': number(column(row,'力量系数K'), name),
            'fish_fight_stamina_per_kilogram': number(column(row,'体力系数'), name),
            'eating_experience_per_kilogram': number(column(row,'经验系数'), name, zero=not edible),
            'rarity_tier_id': rarity[row['稀有度'].strip()],
            'diet': diets[column(row,'食性')],
            'outward_segment_duration_range_seconds': interval(column(row,'发力段长')),
            'rest_segment_duration_range_seconds': interval(column(row,'休息段长')),
            'swim_speed_coefficient': number(column(row,'游速系数'),name),
            'scoop_target_radius_centimeters': number(column(row,'可捞圈半径'),name,zero=True),
            'probe_duration_seconds': number(column(row,'试探期'),name),
            'true_bite_window_seconds': number(column(row,'真咬响应窗'),name),
            'chum_preference': chum,
        }
        if not 8 <= values['true_bite_window_seconds'] <= 15:
            raise ValueError('Response window outside approved range: ' + name)
        bait_values=[]
        for bait_name, runtime_bait in bait_ids.items():
            item=unreal.CatBaitWeightMultiplier(bait_definition_id=runtime_bait,
                                               multiplier=number(bait_rows[name][bait_name],name))
            bait_values.append(item)
        values['bait_weight_multipliers']=bait_values
        # 食用效果已退役；仅迁移成长系数，保留鱼体标定和表现引用。
        reference_fields=['fish_definition_id','presentation_definition','thumbnail','fight_personality_id','bite_personality_id','region_ids','fight_body_geometry']
        before={k: serial(fish.get_editor_property(k)) for k in values}
        refs={k: serial(fish.get_editor_property(k)) for k in reference_fields}
        target={k: serial(v) for k,v in values.items()}
        plan.append(dict(asset=fish, package=package, file_name=name, values=values, before=before, target=target, refs=refs))
    cmd=unreal.SystemLibrary.get_command_line()
    apply='-FishDesignApply' in cmd
    verify='-FishDesignVerify' in cmd
    report={'source': str(table.relative_to(root)), 'source_sha256':hashlib.sha256(table.read_bytes()).hexdigest(),
            'applied':apply, 'identities':identities, 'fish':[]}
    # 全部旧包先备份，之后只保存定向字段。备份不进版本管理。
    if apply:
        for item in plan:
            relative=Path('Content/Catfishing/Data/Fish')/(item['file_name']+'.uasset')
            backup=out/'BeforeMigration'/relative
            if not backup.exists():
                backup.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(root/relative,backup)
        for item in plan:
            if item['before'] != item['target']:
                for k,v in item['values'].items():
                    item['asset'].set_editor_property(k,v)
                if not unreal.EditorAssetLibrary.save_loaded_asset(item['asset'],only_if_is_dirty=False):
                    raise RuntimeError('Save failed: '+item['package'])
    for item in plan:
        after={k:serial(item['asset'].get_editor_property(k)) for k in item['values']}
        refs={k:serial(item['asset'].get_editor_property(k)) for k in item['refs']}
        if refs!=item['refs']:
            raise RuntimeError('Identity/presentation reference changed: '+item['package'])
        report['fish'].append({k:item[k] for k in ['package','before','target','refs']} | {'after':after,'matches':after==item['target']})
    report['matches']=all(r['matches'] for r in report['fish'])
    mode='applied' if apply else 'verified' if verify else 'preview'
    (out/('fish-data-'+mode+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    (out/'fish-runtime-identities.json').write_text(json.dumps(identities,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.log('Event=fish_design_data_report Mode='+mode+' Count='+str(len(plan))+' Matches='+str(report['matches']))
    if (apply or verify) and not report['matches']:
        raise RuntimeError('Fish migration verification failed')


run()
