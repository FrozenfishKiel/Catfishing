"""在 UE Python 中标定正式 16 鱼的静态嘴点和惯量几何。

默认只预览；-FishBodyGeometryApply 仅保存鱼定义的 FightBodyGeometry；
-FishBodyGeometryVerify 只读检查磁盘资产是否匹配当前参考模型。工程根由脚本位置推导。
例如通过 $env:UE_ROOT 下的 UnrealEditor-Cmd.exe -run=pythonscript 运行本脚本。

Bone014 已经逐鱼参考网格核对为上颚末端，使用参考姿态静态点，不读取播放中的动画。
质心采用 Mesh 原点这一游戏近似，经 EncounterTransform 映射；不是实测鱼质量中心。
偏航回转半径采用均匀长方体近似 sqrt((L^2+W^2)/12)，含网格基础比例，不含本场重量缩放。
正式名册沿现有鱼表 fish_id 列取，资产引用沿 FishDefinition -> Presentation -> Mesh 读取。
"""
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import traceback
import unreal


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'Saved/FishBodyGeometry'
TABLE = ROOT / 'Knowledge/Design/GDD 系统分册/鱼/鱼表格/第一版.csv'
FIELD = 'fight_body_geometry'
MOUTH_BONE = 'Bone014'


def vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def hash_file(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def loaded(value):
    return value if isinstance(value, unreal.Object) else unreal.load_asset(str(value))


def geometry_values(value):
    return {
        'mouth_local_position_centimeters': vec(value.get_editor_property('mouth_local_position_centimeters')),
        'center_of_mass_local_position_centimeters': vec(value.get_editor_property('center_of_mass_local_position_centimeters')),
        'scale_origin_local_centimeters': vec(value.get_editor_property('scale_origin_local_centimeters')),
        'yaw_radius_of_gyration_centimeters': float(value.get_editor_property('yaw_radius_of_gyration_centimeters')),
    }


def matches(left, right):
    return all(all(math.isclose(a, b, abs_tol=1e-6, rel_tol=1e-9) for a, b in zip(left[key], right[key]))
               if isinstance(right[key], list) else math.isclose(left[key], right[key], abs_tol=1e-6, rel_tol=1e-9)
               for key in right)


def text_snapshot(asset, name, phase):
    """导出所有序列化属性；排除唯一获准变更的几何字段后比较，避免只检查几个引用。"""
    path = OUTPUT / 'PropertySnapshots' / (name + '-' + phase + '.t3d')
    path.parent.mkdir(parents=True, exist_ok=True)
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = str(path)
    task.exporter = unreal.ObjectExporterT3D()
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    if not unreal.Exporter.run_asset_export_task(task):
        raise RuntimeError('Cannot export complete fish property snapshot: ' + name)
    raw = path.read_bytes()
    text = raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')
    preserved = '\n'.join(line.rstrip() for line in text.splitlines()
                          if not re.match(r'^\s*FightBodyGeometry\s*=', line))
    return hashlib.sha256(preserved.encode('utf-8')).hexdigest()


def reference_bone_position(mesh):
    component = unreal.new_object(unreal.SkeletalMeshComponent)
    component.set_skeletal_mesh_asset(mesh)
    index = component.get_bone_index(MOUTH_BONE)
    if index < 0 or str(component.get_parent_bone(MOUTH_BONE)) != 'Bone013':
        raise ValueError('Reference upper-lip bone changed; inspect new mesh before calibration: ' + mesh.get_path_name())
    point = unreal.Vector(0.0, 0.0, 0.0)
    for _ in range(component.get_num_bones() + 1):
        point = unreal.MathLibrary.transform_location(component.get_ref_pose_transform(index), point)
        index = component.get_bone_index(component.get_parent_bone(component.get_bone_name(index)))
        if index < 0:
            return point
    raise ValueError('Reference skeleton contains a parent cycle')


def build_target(fish):
    presentation = loaded(fish.get_editor_property('presentation_definition'))
    if not presentation:
        raise ValueError('Fish presentation is missing: ' + fish.get_path_name())
    mesh = loaded(presentation.get_editor_property('skeletal_mesh'))
    if not mesh:
        raise ValueError('Fish reference mesh is missing: ' + fish.get_path_name())
    base = presentation.get_editor_property('encounter_mesh_relative_transform')
    # 当前 16 鱼均无轴向旋转。将来更换成倾斜模型时须重新审查平面惯量，不能静默套长宽公式。
    forward = unreal.MathLibrary.transform_direction(base, unreal.Vector(1.0, 0.0, 0.0))
    right = unreal.MathLibrary.transform_direction(base, unreal.Vector(0.0, 1.0, 0.0))
    if abs(forward.z) > 1e-5 or abs(right.z) > 1e-5:
        raise ValueError('Tilted fish mesh needs a reviewed planar inertia calibration: ' + mesh.get_path_name())
    mouth_mesh = reference_bone_position(mesh)
    mouth = unreal.MathLibrary.transform_location(base, mouth_mesh)
    center = unreal.MathLibrary.transform_location(base, unreal.Vector(0.0, 0.0, 0.0))
    bounds = mesh.get_imported_bounds()
    length = 2.0 * bounds.box_extent.x * base.scale3d.x
    width = 2.0 * bounds.box_extent.y * base.scale3d.y
    radius = math.sqrt((length * length + width * width) / 12.0)
    if not all(math.isfinite(v) for v in vec(mouth) + vec(center) + [length, width, radius]) or min(length, width, radius) <= 0.0:
        raise ValueError('Invalid fish geometry: ' + mesh.get_path_name())
    if math.hypot(mouth.x - center.x, mouth.y - center.y) <= 0.01:
        raise ValueError('Fish mouth lever is absent: ' + mesh.get_path_name())
    target = unreal.CatFishBodyGeometry(mouth_local_position_centimeters=mouth,
        center_of_mass_local_position_centimeters=center, scale_origin_local_centimeters=center,
        yaw_radius_of_gyration_centimeters=radius)
    return target, {
        'presentation': presentation.get_path_name(), 'mesh': mesh.get_path_name(),
        'mouth_bone': MOUTH_BONE, 'mouth_reference_mesh_cm': vec(mouth_mesh),
        'reference_length_cm': length, 'reference_width_cm': width,
        'center_model': 'Mesh origin transformed to Actor local space; gameplay approximation',
        'inertia_model': 'I = mass * ((reference length)^2 + (reference width)^2) / 12; units converted by simulation',
        'scale_contract': 'ScaleOrigin + (point - ScaleOrigin) * VisualScale; radius * VisualScale',
    }


def run():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    command = unreal.SystemLibrary.get_command_line()
    apply = '-FishBodyGeometryApply' in command
    verify = '-FishBodyGeometryVerify' in command
    if apply and verify:
        raise ValueError('Choose either Apply or Verify')
    mode = 'applied' if apply else 'verified' if verify else 'preview'
    report = {'mode': mode, 'table': str(TABLE.relative_to(ROOT)), 'fish': [], 'saved_packages': [], 'success': False}
    try:
        with TABLE.open(encoding='utf-8-sig', newline='') as stream:
            reader = csv.DictReader(stream)
            id_columns = [column for column in reader.fieldnames
                          if column == 'fish_id' or column.startswith(('fish_id（', 'fish_id('))]
            if len(id_columns) != 1:
                raise ValueError('Formal fish asset ID column is missing or ambiguous')
            names = [row[id_columns[0]].strip() for row in reader]
        if len(names) != 16 or len(set(names)) != 16 or any(not re.fullmatch(r'Fish_[A-Za-z0-9]+', name) for name in names):
            raise ValueError('Formal fish roster changed; review calibration scope')
        dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
        plan = []
        for name in names:
            package = '/Game/Catfishing/Data/Fish/' + name
            fish = unreal.load_asset(package)
            if not isinstance(fish, unreal.CatFishDefinition) or package in dirty:
                raise ValueError('Missing, incorrect or already dirty fish definition: ' + package)
            target, source = build_target(fish)
            row = {'package': package, 'source': source, 'before': geometry_values(fish.get_editor_property(FIELD)),
                   'target': geometry_values(target), 'other_properties_sha256': text_snapshot(fish, name, 'before')}
            path = ROOT / 'Content/Catfishing/Data/Fish' / (name + '.uasset')
            row['before_asset_sha256'] = hash_file(path)
            row['needed_change'] = not matches(row['before'], row['target'])
            report['fish'].append(row)
            plan.append((fish, target, name, path, row))
        # 所有目标与备份先准备，再逐个保存；只触碰正式鱼定义，不保存表现/网格/动画/测试资产。
        if apply:
            for fish, target, name, path, row in plan:
                if row['needed_change']:
                    backup = OUTPUT / 'Backups' / row['before_asset_sha256'] / path.name
                    backup.parent.mkdir(parents=True, exist_ok=True)
                    if not backup.exists():
                        shutil.copy2(path, backup)
            for fish, target, name, path, row in plan:
                if row['needed_change']:
                    fish.set_editor_property(FIELD, target)
                    if text_snapshot(fish, name, 'changed') != row['other_properties_sha256']:
                        raise RuntimeError('Unrelated fish properties changed before save: ' + row['package'])
                    if not unreal.EditorAssetLibrary.save_loaded_asset(fish, only_if_is_dirty=True):
                        raise RuntimeError('Failed to save fish geometry: ' + row['package'])
                    report['saved_packages'].append(row['package'])
        for fish, target, name, path, row in plan:
            row['after'] = geometry_values(fish.get_editor_property(FIELD))
            row['matches'] = matches(row['after'], row['target'])
            row['other_properties_preserved'] = text_snapshot(fish, name, 'after') == row['other_properties_sha256']
            row['after_asset_sha256'] = hash_file(path)
            if not row['other_properties_preserved']:
                raise RuntimeError('Unrelated fish properties changed: ' + row['package'])
        report['matches'] = all(row['matches'] for row in report['fish'])
        if (apply or verify) and not report['matches']:
            raise RuntimeError('Fish body geometry does not match reference calibration')
        report['success'] = True
    except Exception:
        report['error'] = traceback.format_exc()
        raise
    finally:
        (OUTPUT / ('fish-body-geometry-' + mode + '.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        unreal.log('Event=fish_body_geometry_calibration Mode={} Count={} Saved={} Result={}'.format(
            mode, len(report['fish']), len(report['saved_packages']), 'Succeeded' if report['success'] else 'Failed'))


run()
