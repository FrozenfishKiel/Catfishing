"""Render an independent three-character Cream Cat animation showcase.

Run in a separate Blender process:
    blender --background --factory-startup --python Scripts/Art/preview_cream_cat.py

Reads CreamCat.blend; saves Animation_Showcase.blend without saving the source.
The reference rod is intentionally absent: FishingWait demonstrates a hold pose.
No UE assets, preferences, or automatic script execution settings are changed.
"""

import hashlib
import json
import math
import time
import traceback
from pathlib import Path

import bpy
from bpy_extras import anim_utils
from mathutils import Vector


PROJECT = Path(__file__).resolve().parents[2]
ART = PROJECT / 'SourceArt/Characters/CreamCat'
SOURCE = ART / 'CreamCat.blend'
SHOWCASE = ART / 'Animation_Showcase.blend'
PREVIEW = ART / 'Previews'
REPORT = PROJECT / 'Saved/Art/CreamCat'
REPORT_FILE = REPORT / 'showcase_report.json'
LOG_FILE = REPORT / 'showcase.log'
VIDEO = PREVIEW / 'CreamCat_Animation_Showcase.mp4'
FRAME_COUNT = 192
RENDER_SAMPLES = 32
WALK_TIME_SCALE = 1.5
SAMPLES = (1, 97, FRAME_COUNT)
CLIPS = (('Idle', -1.45, 120), ('Walk', 0.0, 32), ('FishingWait', 1.45, 150))


def log(event, **fields):
    line = 'Event=' + event + ' ' + ' '.join(f'{k}={v}' for k, v in fields.items())
    print(line, flush=True)
    with LOG_FILE.open('a', encoding='utf-8') as stream:
        stream.write(line + '\n')


def digest(path):
    sha = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            sha.update(chunk)
    return sha.hexdigest()


def write_report(report):
    REPORT_FILE.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')


def principled_shader(material):
    """Node names are localized in a Chinese UI; the node type is stable."""
    shader = next((node for node in material.node_tree.nodes if node.type == 'BSDF_PRINCIPLED'), None)
    if shader is None:
        raise RuntimeError('Material has no Principled BSDF node: ' + material.name)
    return shader


def is_source_prop(obj):
    """References and optional static source props are absent from this showcase."""
    return bool(obj.get('reference_only') or obj.get('source_art_prop')
                or obj.name.startswith(('REFERENCE_', 'PROP_')))


def clone_character(source_rig, label, offset):
    """Copy objects and armature data; share unchanged mesh/material data."""
    scene = bpy.context.scene
    action = bpy.data.actions.get('AN_CreamCat_' + label)
    if action is None:
        raise RuntimeError('Missing source Action: ' + label)
    if label == 'Walk':
        # Slow only this showcase's copy, preserving the original source Action.
        action = action.copy()
        action.name = 'SHOWCASE_AN_CreamCat_Walk'
        bag = anim_utils.action_get_channelbag_for_slot(action, action.slots[0])
        if not bag:
            raise RuntimeError('Walk Action has no channelbag to retime')
        for curve in bag.fcurves:
            for point in curve.keyframe_points:
                point.co.x = 1 + (point.co.x - 1) * WALK_TIME_SCALE
                point.handle_left.x = 1 + (point.handle_left.x - 1) * WALK_TIME_SCALE
                point.handle_right.x = 1 + (point.handle_right.x - 1) * WALK_TIME_SCALE
            curve.update()
        action['loop_frames'] = int(round(action.get('loop_frames', 32) * WALK_TIME_SCALE))
        action['showcase_time_scale'] = WALK_TIME_SCALE
    slots = [slot for slot in action.slots if slot.target_id_type in {'OBJECT', 'UNSPECIFIED'}]
    if len(slots) != 1:
        raise RuntimeError(f'{action.name}: expected one Object Action slot, found {len(slots)}')
    bag = anim_utils.action_get_channelbag_for_slot(action, slots[0])
    if not bag or not bag.fcurves:
        raise RuntimeError('Action has no curves: ' + action.name)
    if any(not any(mod.type == 'CYCLES' for mod in curve.modifiers) for curve in bag.fcurves):
        raise RuntimeError('Source Action needs cyclic curves: ' + action.name)

    rig = source_rig.copy()
    rig.data = source_rig.data.copy()
    rig.name = 'SHOWCASE_RIG_' + label
    rig.data.name = 'SHOWCASE_Skeleton_' + label
    scene.collection.objects.link(rig)
    rig.animation_data_clear()
    animation = rig.animation_data_create()
    animation.action = action
    animation.action_slot = slots[0]
    animation.use_nla = False
    rig.location = source_rig.location + Vector((offset, 0, 0))
    rig.hide_render = False
    rig.hide_viewport = False
    rig.hide_set(False)

    source_meshes = [obj for obj in source_rig.children_recursive
                     if obj.type == 'MESH' and not is_source_prop(obj)]
    if not source_meshes:
        raise RuntimeError('Source rig has no skinned child meshes')
    clones = {}
    for original in source_meshes:
        if original.parent != source_rig:
            raise RuntimeError('Unsupported nested source mesh parent: ' + original.name)
        copied = original.copy()
        if original.data.shape_keys:
            copied.data = original.data.copy()
            keys_animation = copied.data.shape_keys.animation_data
            if keys_animation:
                for curve in keys_animation.drivers:
                    for variable in curve.driver.variables:
                        for target in variable.targets:
                            if target.id == source_rig:
                                target.id = rig
        copied.name = 'SHOWCASE_' + label + '_' + original.name
        scene.collection.objects.link(copied)
        copied.parent = rig
        copied.matrix_parent_inverse = original.matrix_parent_inverse.copy()
        copied.matrix_basis = original.matrix_basis.copy()
        armature_modifiers = [mod for mod in copied.modifiers if mod.type == 'ARMATURE']
        if not armature_modifiers:
            raise RuntimeError('Source mesh lacks an Armature modifier: ' + original.name)
        for modifier in armature_modifiers:
            if modifier.object != source_rig:
                raise RuntimeError('Unexpected source armature on ' + original.name)
            modifier.object = rig
        # Honor the saved design, including the intentionally hidden scarf.
        # Unhiding the rig for playback must not reveal discarded accessories.
        copied.hide_render = original.hide_render
        copied.hide_viewport = original.hide_viewport
        copied.hide_set(original.hide_get())
        clones[original.name] = copied
    log('CreamCatShowcaseClone',Action=action.name, Rig=rig.name, Meshes=len(clones),
        HiddenInRender=sum(obj.hide_render for obj in clones.values()))
    return rig, len(clones)


def add_labels(camera):
    mat = bpy.data.materials.new('SHOWCASE_Label_Cocoa')
    mat.diffuse_color = (.075, .040, .020, 1)
    mat.use_nodes = True
    shader = principled_shader(mat)
    shader.inputs['Base Color'].default_value = mat.diffuse_color
    shader.inputs['Roughness'].default_value = 1
    for label, x, _ in CLIPS:
        if label == 'FishingWait':
            lines = [('FISHING', 1.78, .125), ('HOLD POSE', 1.64, .065)]
        elif label == 'Walk':
            lines = [('WALK + MOVE', 1.76, .120)]
        else:
            lines = [('IDLE', 1.76, .125)]
        for text, height, size in lines:
            data = bpy.data.curves.new('SHOWCASE_Label_' + text, 'FONT')
            data.body = text
            data.align_x = 'CENTER'
            data.align_y = 'CENTER'
            data.size = size
            data.extrude = .001
            data.materials.append(mat)
            obj = bpy.data.objects.new(data.name, data)
            bpy.context.scene.collection.objects.link(obj)
            obj.location = (x, -.015, height)
            obj.rotation_euler = (camera.location - obj.location).to_track_quat('Z', 'Y').to_euler()


def configure_scene():
    scene = bpy.context.scene
    # Blender 5.2 uses BLENDER_EEVEE (the older EEVEE_NEXT identifier is gone).
    scene.render.engine = 'BLENDER_EEVEE'
    scene.eevee.taa_render_samples = RENDER_SAMPLES
    scene.render.resolution_x = 1280
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    scene.render.fps = 30
    scene.render.fps_base = 1.0
    scene.frame_start = 1
    scene.frame_end = FRAME_COUNT
    scene.frame_step = 1
    scene.render.film_transparent = False
    scene.render.use_file_extension = True
    scene.render.use_stamp = False
    scene.render.use_sequencer = False
    if hasattr(scene.render, 'save_output'):
        scene.render.save_output = True
    camera = scene.camera
    if camera is None:
        raise RuntimeError('Source studio has no camera')
    camera.location = (2.4, -8, 2.95)
    camera.rotation_euler = (Vector((0, -.05, .82)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = 5.05
    camera.data.lens = 50
    add_labels(camera)
    for obj in scene.objects:
        if obj.type == 'LIGHT' or obj.name == 'STUDIO_Backdrop':
            # Keep source viewport hide_set flags; explicitly retain studio in renders.
            obj.hide_render = False
            obj.hide_viewport = False
    scene['AnimationGuide'] = '192 frames at 30 fps. Idle and FishingWait loop; Walk makes one continuous forward stroll with foot-matched travel. The whole movie is not a loop. Fishing is a hold pose without the reference rod.'
    scene['SourceBlend'] = str(SOURCE)
    scene['ReferenceRodIncluded'] = False
    return scene


def add_walk_travel(rig):
    """Cancel the planted foot's backward motion with forward actor translation.

    Sample the actual retimed pose, rather than assuming a stride length. The
    low foot drives each frame's advance; there is no direction flip or reset.
    """
    scene = bpy.context.scene
    feet = ('foot.L', 'foot.R')
    if any(name not in rig.pose.bones for name in feet):
        raise RuntimeError('Walk travel requires foot.L and foot.R pose bones')
    samples = {}
    for frame in range(1, FRAME_COUNT + 1):
        scene.frame_set(frame)
        bpy.context.view_layer.update()
        samples[frame] = {name: rig.pose.bones[name].matrix.translation.copy() for name in feet}
    minimum = {name: min(pose[name].z for pose in samples.values()) for name in feet}
    distances, contacts, residuals = [0.0], [], []
    for frame in range(2, FRAME_COUNT + 1):
        previous, current = samples[frame - 1], samples[frame]
        # A rising swing foot loses to the lower support foot; at foot changes
        # the lower two-frame average avoids snapping between transient poses.
        foot = min(feet, key=lambda name: (previous[name].z + current[name].z) * .5 - minimum[name])
        change = current[foot] - previous[foot]
        advance = max(0.0, change.y)
        if advance > .035:
            raise RuntimeError(f'Walk support foot jumps {advance:.4f} m at frame {frame}')
        distances.append(distances[-1] + advance)
        residuals.append(Vector((change.x, change.y - advance, change.z)).length)
        contacts.append(foot)
    total = distances[-1]
    if total < .30 or total > 2.5:
        raise RuntimeError(f'Walk derived travel {total:.3f} m is outside the showcase stage range')

    carrier = bpy.data.objects.new('SHOWCASE_WalkTravel', None)
    scene.collection.objects.link(carrier)
    carrier.empty_display_type = 'PLAIN_AXES'
    carrier.empty_display_size = .12
    angle = math.radians(18)
    carrier.rotation_euler.z = angle
    direction = Vector((math.sin(angle), -math.cos(angle), 0))
    rig.parent = carrier
    start = direction * (-total * .5)
    for frame, distance in enumerate(distances, 1):
        carrier.location = start + direction * distance
        carrier.keyframe_insert(data_path='location', frame=frame, group='Forward travel')
    animation = carrier.animation_data
    animation.action.name = 'SHOWCASE_ForwardTravel'
    bag = anim_utils.action_get_channelbag_for_slot(animation.action, animation.action_slot)
    for curve in bag.fcurves:
        for point in curve.keyframe_points:
            point.interpolation = 'LINEAR'
    carrier['motion'] = 'single_forward_pass_without_turnaround'
    carrier['travel_meters'] = total
    carrier['source'] = 'backward displacement of the lower hind foot'
    add_path_marks(start, direction, total)
    scene.frame_set(1)
    bpy.context.view_layer.update()
    first_world_position = rig.matrix_world.translation.copy()
    scene.frame_set(FRAME_COUNT)
    bpy.context.view_layer.update()
    last_world_position = rig.matrix_world.translation.copy()
    measured_travel = (last_world_position - first_world_position).length
    if abs(measured_travel - total) > .001:
        raise RuntimeError(f'Walk carrier failed to move the rig: expected {total:.4f}, measured {measured_travel:.4f} m')
    scene.frame_set(1)
    bpy.context.view_layer.update()
    report = {'carrier': carrier.name, 'travel_meters': round(total, 4),
              'measured_rig_world_displacement_meters': round(measured_travel, 4),
              'world_start': list(first_world_position), 'world_end': list(last_world_position),
              'duration_seconds': (FRAME_COUNT - 1) / 30,
              'average_speed_meters_per_second': round(total / ((FRAME_COUNT - 1) / 30), 4),
              'walk_cycle_frames': rig.animation_data.action.get('loop_frames'),
              'action_time_scale': WALK_TIME_SCALE, 'whole_movie_loops': False,
              'maximum_support_point_residual_meters_per_frame': round(max(residuals), 6),
              'support_switches': sum(a != b for a, b in zip(contacts, contacts[1:]))}
    log('CreamCatShowcaseWalkTravel',Meters=report['travel_meters'], Frames=FRAME_COUNT,
        MaxSupportResidual=report['maximum_support_point_residual_meters_per_frame'])
    return report


def add_path_marks(start, direction, length):
    """Small matte floor dashes provide a fixed reference for visible movement."""
    mat = bpy.data.materials.new('SHOWCASE_Path_Sage')
    mat.diffuse_color = (.20, .24, .14, 1)
    mat.use_nodes = True
    shader = principled_shader(mat)
    shader.inputs['Base Color'].default_value = mat.diffuse_color
    shader.inputs['Roughness'].default_value = 1
    sideways = Vector((-direction.y, direction.x, 0))
    for i in range(8):
        position = start + direction * (length * i / 7) + sideways * .33
        curve = bpy.data.curves.new('SHOWCASE_PathDash', 'CURVE')
        curve.dimensions = '3D'
        curve.bevel_depth = .0035
        curve.bevel_resolution = 2
        spline = curve.splines.new('POLY')
        spline.points.add(1)
        for point, offset in zip(spline.points, (-.023, .023)):
            p = position + direction * offset
            point.co = (p.x, p.y, bpy.data.objects['STUDIO_Backdrop'].location.z + .004, 1)
        curve.materials.append(mat)
        obj = bpy.data.objects.new(curve.name, curve)
        bpy.context.scene.collection.objects.link(obj)


def validate_clones(clones):
    scene = bpy.context.scene
    result = {}
    for (label, _, cycle), (rig, mesh_count) in zip(CLIPS, clones):
        cycle = int(rig.animation_data.action.get('loop_frames', cycle))
        scene.frame_set(1)
        bpy.context.view_layer.update()
        first = {bone.name: bone.matrix.copy() for bone in rig.pose.bones}
        scene.frame_set(cycle + 1)
        bpy.context.view_layer.update()
        delta = max(abs(first[bone.name][i][j] - bone.matrix[i][j])
                    for bone in rig.pose.bones for i in range(4) for j in range(4))
        if delta > .001:
            raise RuntimeError(f'{label}: clone loop mismatch {delta}')
        result[label] = {'rig': rig.name, 'action': rig.animation_data.action.name,
                         'mesh_count': mesh_count, 'cycle_frames': cycle,
                         'loop_matrix_max_delta': delta}
    scene.frame_set(1)
    bpy.context.view_layer.update()
    return result


def configure_png(scene, path):
    # file_format's enum depends on media_type, including after a video failure.
    scene.render.image_settings.media_type = 'IMAGE'
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.color_depth = '8'
    scene.render.filepath = str(path)


def configure_video(scene, path):
    # Match Blender 5.2's Video_Editing startup template: enable the video
    # media type before selecting FFMPEG in the dependent file_format enum.
    scene.render.image_settings.media_type = 'VIDEO'
    scene.render.image_settings.file_format = 'FFMPEG'
    scene.render.ffmpeg.format = 'MPEG4'
    scene.render.ffmpeg.codec = 'H264'
    scene.render.ffmpeg.constant_rate_factor = 'HIGH'
    scene.render.ffmpeg.ffmpeg_preset = 'GOOD'
    scene.render.ffmpeg.audio_codec = 'NONE'
    scene.render.ffmpeg.use_autosplit = False
    scene.render.ffmpeg.use_max_b_frames = False
    scene.render.filepath = str(path)


def save_showcase(scene):
    scene.frame_set(1)
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == 'VIEW_3D':
                area.spaces.active.region_3d.view_perspective = 'CAMERA'
                area.spaces.active.overlay.show_overlays = False
    if SHOWCASE.resolve() == SOURCE.resolve():
        raise RuntimeError('Showcase must not overwrite the source blend')
    result = bpy.ops.wm.save_as_mainfile(filepath=str(SHOWCASE))
    if 'FINISHED' not in result:
        raise RuntimeError('Showcase save did not finish')


def main():
    if not bpy.app.background:
        raise RuntimeError('Run in a separate --background process to preserve the interactive Blender scene')
    REPORT.mkdir(parents=True, exist_ok=True)
    PREVIEW.mkdir(parents=True, exist_ok=True)
    LOG_FILE.write_text('', encoding='utf-8')
    started = time.time()
    report = {'status': 'running', 'source': str(SOURCE), 'showcase': str(SHOWCASE),
              'video': None, 'samples': [], 'blender': bpy.app.version_string,
              'resolution': [1280, 720], 'fps': 30, 'frames': FRAME_COUNT, 'duration_seconds': FRAME_COUNT / 30,
              'engine': 'BLENDER_EEVEE', 'render_samples': RENDER_SAMPLES,
              'reference_rod_included': False, 'fishing_presentation': 'hold_pose_without_rod',
              'source_unchanged': None, 'ue_assets_modified': False}
    source_hash = None
    scene = None
    try:
        if not SOURCE.is_file():
            raise FileNotFoundError(SOURCE)
        source_hash = digest(SOURCE)
        log('CreamCatShowcaseStart',Source=SOURCE, SourceSHA256=source_hash)
        bpy.ops.wm.open_mainfile(filepath=str(SOURCE), load_ui=False, use_scripts=False)
        bpy.context.preferences.filepaths.save_version = 0
        source_rig = bpy.data.objects.get('RIG_CreamCat')
        if source_rig is None or source_rig.type != 'ARMATURE':
            raise RuntimeError('Missing RIG_CreamCat in the source blend')
        clones = [clone_character(source_rig, label, x) for label, x, _ in CLIPS]
        # Hide only the source character and references in this new in-memory scene.
        for obj in [source_rig, *source_rig.children_recursive]:
            obj.hide_render = True
            obj.hide_set(True)
        for obj in bpy.context.scene.objects:
            if is_source_prop(obj):
                obj.hide_render = True
                obj.hide_set(True)
        scene = configure_scene()
        report['characters'] = validate_clones(clones)
        report['walk_travel'] = add_walk_travel(clones[1][0])
        for frame in SAMPLES:
            path = PREVIEW / f'CreamCat_Showcase_Frame_{frame:03d}.png'
            scene.frame_set(frame)
            configure_png(scene, path)
            log('CreamCatShowcaseSampleStart',Frame=frame, File=path)
            result = bpy.ops.render.render(write_still=True)
            if 'FINISHED' not in result or not path.is_file() or path.stat().st_size == 0:
                raise RuntimeError(f'Sample render did not produce {path}')
            report['samples'].append({'frame': frame, 'file': str(path), 'bytes': path.stat().st_size})
            write_report(report)

        # A unique staging directory prevents a previous MP4 being reported as new.
        staging = REPORT / ('showcase_video_' + str(time.time_ns()))
        staging.mkdir()
        staged_video = staging / VIDEO.name
        try:
            configure_video(scene, VIDEO)
            save_showcase(scene)
            configure_video(scene, staged_video)
            log('CreamCatShowcaseVideoStart',Frames=FRAME_COUNT, FPS=30, File=staged_video)
            result = bpy.ops.render.render(animation=True)
            candidates = [path for path in staging.glob('*.mp4') if path.stat().st_size > 0]
            if 'FINISHED' not in result or len(candidates) != 1:
                raise RuntimeError('Render did not produce exactly one nonempty MP4 in this run')
            candidates[0].replace(VIDEO)
            report['video'] = {'file': str(VIDEO), 'bytes': VIDEO.stat().st_size,
                               'render_finished': True, 'container': 'MPEG4', 'codec': 'H264'}
            scene.render.filepath = str(VIDEO)
            report['status'] = 'complete'
            log('CreamCatShowcaseVideoComplete',File=VIDEO, Bytes=VIDEO.stat().st_size)
        except Exception:
            report['status'] = 'video_failed_png_available'
            report['video_error'] = traceback.format_exc()
            configure_png(scene, PREVIEW / 'CreamCat_Showcase_Fallback.png')
            scene['VideoStatus'] = 'MP4 failed; see Saved/Art/CreamCat/showcase_report.json. Three sample PNGs remain available.'
            save_showcase(scene)
            raise
    except Exception:
        if report['status'] == 'running':
            report['status'] = 'failed'
        report['error'] = traceback.format_exc()
        log('CreamCatShowcaseFailed',Status=report['status'], Error=report['error'])
        raise
    finally:
        if source_hash is not None and SOURCE.is_file():
            report['source_sha256_before'] = source_hash
            report['source_sha256_after'] = digest(SOURCE)
            report['source_unchanged'] = report['source_sha256_after'] == source_hash
            if not report['source_unchanged']:
                report['status'] = 'source_changed_during_run'
                log('CreamCatShowcaseSourceChanged',File=SOURCE)
        report['elapsed_seconds'] = round(time.time() - started, 3)
        write_report(report)


if __name__ == '__main__':
    main()
