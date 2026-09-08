"""Inspect an already opened CreamCat.blend without saving or rebuilding it.

Usage: blender --background <CreamCat.blend> --python <this script>
Only Saved/Art/CreamCat/inspection.json is written. Units are metres.
Geometry measurements are evidence, not a substitute for visual review or UE QA.
"""
import json
import math
import traceback
from pathlib import Path

import bmesh
import bpy
from bpy_extras import anim_utils
from mathutils import Vector
from mathutils.bvhtree import BVHTree


REPORT = Path(__file__).resolve().parents[2] / 'Saved/Art/CreamCat/inspection.json'
PRIMARY = ('Cat_Body', 'Ear.L', 'Ear.R')
EYE_PARTS = ('EyeIris', 'EyePupil', 'Eyelids', 'EyeGlint0', 'EyeGlint1')
FACE_PARTS = ('Nose', 'Philtrum', 'Lip.L', 'Lip.R',
              'Whisker.L0', 'Whisker.L1', 'Whisker.R0', 'Whisker.R1')


def eye_name(part, side):
    return 'EyeGlint.' + side + part[-1] if part.startswith('EyeGlint') else part + '.' + side


def blink_contract(obj, rig, side):
    """Inspect the saved shape-key/driver wiring without assigning a pose."""
    keys = obj.data.shape_keys
    basis = keys.key_blocks.get('Basis') if keys else None
    blink = keys.key_blocks.get('Blink') if keys else None
    result = {'has_basis': basis is not None, 'has_blink': blink is not None,
              'expected_control': 'pose.bones["eye.' + side + '"]["blink"]'}
    if not basis or not blink:
        result['valid'] = False
        return result
    result['point_count_matches_mesh'] = len(basis.data) == len(blink.data) == len(obj.data.vertices)
    result['coordinates_finite'] = finite(vertex.co for key in (basis, blink) for vertex in key.data)
    result['maximum_shape_displacement_m'] = max(
        ((a.co - b.co).length for a, b in zip(basis.data, blink.data)), default=0.0) \
        if result['coordinates_finite'] else None
    expected_path = blink.path_from_id('value')
    drivers = keys.animation_data.drivers if keys.animation_data else []
    matching = [curve for curve in drivers if curve.data_path == expected_path and curve.array_index == 0]
    result['matching_driver_count'] = len(matching)
    result['drivers'] = []
    for curve in matching:
        driver = curve.driver
        variables = [{'name': variable.name, 'type': variable.type,
                      'targets': [{'id': target.id.name if target.id else None,
                                   'is_expected_rig': target.id == rig,
                                   'data_path': target.data_path} for target in variable.targets]}
                     for variable in driver.variables]
        result['drivers'].append({'type': driver.type, 'expression': driver.expression,
                                  'variables': variables})
    correct_driver = False
    if len(matching) == 1:
        driver = matching[0].driver
        variable = driver.variables[0] if len(driver.variables) == 1 else None
        correct_driver = (driver.type == 'SCRIPTED'
            and ''.join(driver.expression.split()) == 'max(0.0,min(1.0,b))'
            and variable is not None and variable.name == 'b' and variable.type == 'SINGLE_PROP'
            and variable.targets[0].id == rig
            and variable.targets[0].data_path == result['expected_control'])
    result['valid'] = bool(result['point_count_matches_mesh'] and result['coordinates_finite']
                           and result['maximum_shape_displacement_m'] > 1e-6 and correct_driver)
    return result


def topology(obj):
    mesh = bmesh.new()
    mesh.from_mesh(obj.data)
    mesh.verts.ensure_lookup_table()
    remaining = set(mesh.verts)
    components = []
    while remaining:
        stack = [remaining.pop()]
        count = 0
        while stack:
            vertex = stack.pop()
            count += 1
            for edge in vertex.link_edges:
                other = edge.other_vert(vertex)
                if other in remaining:
                    remaining.remove(other)
                    stack.append(other)
        components.append(count)
    result = {'vertices': len(mesh.verts), 'edges': len(mesh.edges), 'faces': len(mesh.faces),
              'boundary_edges': sum(edge.is_boundary for edge in mesh.edges),
              'nonmanifold_edges': sum(not edge.is_manifold for edge in mesh.edges),
              'wire_edges': sum(edge.is_wire for edge in mesh.edges),
              'inconsistent_winding_edges': sum(edge.is_manifold and not edge.is_contiguous for edge in mesh.edges),
              'degenerate_faces': sum(face.calc_area() < 1e-12 for face in mesh.faces),
              'components_vertex_counts': sorted(components, reverse=True),
              'rest_signed_volume_m3': mesh.calc_volume(signed=True)}
    mesh.free()
    return result


def coordinates(obj, graph):
    evaluated = obj.evaluated_get(graph)
    mesh = evaluated.to_mesh()
    try:
        return [evaluated.matrix_world @ vertex.co for vertex in mesh.vertices]
    finally:
        evaluated.to_mesh_clear()


def finite(points):
    return all(math.isfinite(value) for point in points for value in point)


def volume(points, triangles):
    return abs(sum(points[a].dot(points[b].cross(points[c])) for a, b, c in triangles) / 6.0)


def quantile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int((len(ordered) - 1) * fraction))] if ordered else None


def keyframes(action, slot):
    bag = anim_utils.action_get_channelbag_for_slot(action, slot)
    if bag is None:
        raise RuntimeError(f'{action.name} has no channel bag for the selected rig slot')
    frames = {round(point.co.x, 6) for curve in bag.fcurves for point in curve.keyframe_points}
    if not frames:
        raise RuntimeError(f'{action.name} contains no keyframes in its selected slot')
    return sorted(frames)


def inspect():
    if not bpy.app.background:
        raise RuntimeError('Inspection must run in a separate background Blender process')
    if not bpy.data.filepath:
        raise RuntimeError('Open the completed source .blend before running inspection')
    rig = bpy.data.objects.get('RIG_CreamCat')
    if rig is None:
        raise RuntimeError('RIG_CreamCat is missing from the opened file')
    rig.hide_set(False)
    rig.animation_data_create()
    scene = bpy.context.scene
    failures, cautions = [], []
    report = {'source_blend': bpy.data.filepath, 'blender': bpy.app.version_string,
              'units': 'metres', 'source_saved_or_modified_on_disk': False,
              'contract': {}, 'runtime_behavior': {},
              'presentation_delivery': {'status': 'not_verified',
                  'visual_review': 'Not performed by this measurement script.',
                  'ue_import_and_playback': 'Not performed.',
                  'limitations': ['Manifold checks do not prove absence of self-intersection.',
                      'Eye clearance samples up to 256 vertices per part against the fused head region; '
                      'it is a projection proxy, not rendered visibility or a complete intersection test.',
                      'Shape-key drivers are checked in Blender only; UE driver/morph delivery is not verified.',
                      'Animation sampling covers authored keyframes, not every interpolated time.',
                      'Foot heights and edge stretch do not establish natural movement or planted feet.']},
              'failures': failures, 'cautions': cautions}
    meshes = [obj for obj in bpy.data.objects if obj.type == 'MESH'
              and any(mod.type == 'ARMATURE' and mod.object == rig for mod in obj.modifiers)]
    report['contract']['skinned_mesh_count'] = len(meshes)
    if not meshes:
        failures.append('No meshes are bound to RIG_CreamCat')
    if bpy.data.objects.get('Cat_Head') is not None:
        failures.append('Cat_Head still exists; expected one fused Cat_Body surface')
    report['contract']['primary_meshes'] = {}
    for name in PRIMARY:
        obj = bpy.data.objects.get(name)
        if obj is None or obj.type != 'MESH':
            failures.append(f'Missing primary mesh {name}')
            continue
        result = topology(obj)
        report['contract']['primary_meshes'][name] = result
        if result['nonmanifold_edges'] or result['inconsistent_winding_edges']:
            failures.append(f'{name}: nonmanifold or inconsistent winding')
        if len(result['components_vertex_counts']) != 1:
            cautions.append(f'{name}: multiple connected components')
    report['contract']['face_meshes'] = {}
    for name in FACE_PARTS + tuple(eye_name(part, side) for side in ('L', 'R') for part in EYE_PARTS):
        obj = bpy.data.objects.get(name)
        present = obj is not None and obj.type == 'MESH'
        bound = present and obj in meshes
        report['contract']['face_meshes'][name] = {'present_mesh': present, 'bound_to_rig': bool(bound)}
        if not bound:
            failures.append(f'{name}: required face mesh is absent or not bound to the rig')
    report['contract']['blink'] = {}
    for side in ('L', 'R'):
        bone = rig.pose.bones.get('eye.' + side)
        if bone is None or 'blink' not in bone:
            failures.append(f'eye.{side}: missing blink bone property')
        for part in EYE_PARTS:
            name = eye_name(part, side)
            obj = bpy.data.objects.get(name)
            if obj is None or obj.type != 'MESH':
                continue
            result = blink_contract(obj, rig, side)
            report['contract']['blink'][name] = result
            if not result['valid']:
                failures.append(f'{name}: invalid Blink shape-key/driver contract')
    weights = {}
    for obj in meshes:
        unknown, empty, nonfinite, max_delta, max_influences = set(), 0, 0, 0.0, 0
        for vertex in obj.data.vertices:
            values = [group.weight for group in vertex.groups]
            if not all(math.isfinite(value) for value in values):
                nonfinite += 1
            total = sum(values)
            empty += total < 1e-6
            max_delta = max(max_delta, abs(total - 1.0))
            max_influences = max(max_influences, len(values))
            for group in vertex.groups:
                name = obj.vertex_groups[group.group].name
                if name not in rig.data.bones:
                    unknown.add(name)
        weights[obj.name] = {'unweighted_vertices': empty, 'nonfinite_vertices': nonfinite,
                            'max_sum_error': max_delta, 'maximum_influences': max_influences,
                            'unknown_bone_groups': sorted(unknown)}
        if empty or nonfinite or max_delta > .002 or unknown:
            failures.append(f'{obj.name}: invalid weight contract')
    report['contract']['weights'] = weights
    body = bpy.data.objects.get('Cat_Body')
    if body is None or body not in meshes or rig.pose.bones.get('head') is None:
        failures.append('Fused skinned body or head bone unavailable for deformation measurements')
        report['contract']['status'] = 'failed'
        report['runtime_behavior']['status'] = 'not_measured_invalid_contract'
        return report
    report['contract']['fused_surface'] = {
        'object': body.name, 'source_metadata': body.get('FusedSurfaceSources'),
        'fusion_voxel_m': body.get('FusionVoxelMeters'),
        'source_head_object_absent': bpy.data.objects.get('Cat_Head') is None}
    body.data.calc_loop_triangles()
    triangles = [tuple(face.vertices) for face in body.data.loop_triangles]
    rest_points = [body.matrix_world @ vertex.co for vertex in body.data.vertices]
    rest_volume = volume(rest_points, triangles)
    if not finite(rest_points) or not math.isfinite(rest_volume) or rest_volume < 1e-10:
        failures.append('Fused body rest geometry is nonfinite or has no measurable enclosed volume')
        report['contract']['status'] = 'failed'
        report['runtime_behavior']['status'] = 'not_measured_invalid_contract'
        return report
    edges = [(edge.vertices[0], edge.vertices[1],
              (rest_points[edge.vertices[0]] - rest_points[edge.vertices[1]]).length)
             for edge in body.data.edges]
    feet = {side: [vertex.index for vertex in body.data.vertices
                  if vertex.co.z < .055 and sign * vertex.co.x > .065]
            for side, sign in [('L', 1), ('R', -1)]}
    report['contract']['foot_probe_vertices'] = {side: len(indices) for side, indices in feet.items()}
    if any(not indices for indices in feet.values()):
        failures.append('A foot probe has no vertices; ground measurements are unavailable for that side')
    # The fused surface carries rigid head weights above the neck. Selecting its
    # head-weighted triangles avoids treating the torso or lifted paws as skull.
    head_group = body.vertex_groups.get('head')
    head_vertices = {vertex.index for vertex in body.data.vertices
                     if head_group and any(group.group == head_group.index and group.weight >= .98
                                           for group in vertex.groups)}
    head_triangles = [face for face in triangles if all(index in head_vertices for index in face)]
    report['contract']['head_projection_surface'] = {
        'object': body.name, 'selection': 'All triangle vertices have head weight >= 0.98',
        'vertices': len(head_vertices), 'triangles': len(head_triangles)}
    if not head_triangles:
        failures.append('Fused body has no rigid-head triangles for the eye clearance proxy')
    contract_failure_count = len(failures)
    ground = bpy.data.objects.get('STUDIO_Backdrop')
    ground_z = ground.matrix_world.translation.z if ground else 0.0
    report['runtime_behavior']['ground_z_m'] = ground_z
    report['runtime_behavior']['body_reference_volume_m3'] = rest_volume
    primary_triangles, primary_volumes = {}, {}
    for name in PRIMARY:
        obj = bpy.data.objects.get(name)
        if obj is not None and obj.type == 'MESH':
            obj.data.calc_loop_triangles()
            primary_triangles[name] = [tuple(face.vertices) for face in obj.data.loop_triangles]
            primary_volumes[name] = volume([obj.matrix_world @ vertex.co for vertex in obj.data.vertices],
                                           primary_triangles[name])
    report['runtime_behavior']['primary_reference_volumes_m3'] = primary_volumes
    report['runtime_behavior']['actions'] = {}
    actions = [action for action in bpy.data.actions if action.name.startswith('AN_CreamCat_')]
    if not actions:
        failures.append('No Cream Cat actions found')
    for action in actions:
        if not action.slots:
            failures.append(f'{action.name}: no animation slot')
            continue
        slot = next((entry for entry in action.slots if entry.target_id_type == rig.id_type), None)
        if slot is None:
            failures.append(f'{action.name}: no slot targeting the rig object type')
            continue
        rig.animation_data.action = action
        rig.animation_data.action_slot = slot
        frames = keyframes(action, slot)
        samples = []
        for frame in frames:
            whole = math.floor(frame)
            scene.frame_set(whole, subframe=frame - whole)
            bpy.context.view_layer.update()
            graph = bpy.context.evaluated_depsgraph_get()
            vertices = {obj.name: coordinates(obj, graph) for obj in meshes}
            bad = [name for name, points in vertices.items() if not finite(points)]
            if bad:
                failures.append(f'{action.name} frame {frame}: nonfinite vertices in {bad}')
                samples.append({'frame': frame, 'nonfinite_meshes': bad})
                continue
            points = vertices[body.name]
            if len(points) != len(rest_points):
                raise RuntimeError('Body evaluated topology differs; index-based deformation inspection is invalid')
            stretch = [((points[a] - points[b]).length / length, a, b)
                       for a, b, length in edges if length > 1e-8]
            worst = max(stretch) if stretch else (0.0, None, None)
            foot_heights = {side: {'minimum_z_m': min(points[index].z for index in indices),
                                   'clearance_above_ground_m': min(points[index].z for index in indices) - ground_z,
                                   'median_z_m': quantile([points[index].z for index in indices], .5)}
                            for side, indices in feet.items() if indices}
            head_bone = rig.pose.bones['head']
            head_inverse = (rig.matrix_world @ head_bone.matrix @ head_bone.bone.matrix_local.inverted()).inverted()
            head_local = [head_inverse @ point for point in points]
            head_tree = BVHTree.FromPolygons(head_local, head_triangles, all_triangles=True) if head_triangles else None
            eyes = {}
            for side in ('L', 'R'):
                eye_bone = rig.pose.bones.get('eye.' + side)
                if eye_bone is None:
                    continue
                control = float(eye_bone.get('blink', 0.0))
                scale = list(eye_bone.scale)
                if not math.isfinite(control) or not all(math.isfinite(value) for value in scale):
                    failures.append(f'{action.name} frame {frame}: eye.{side} has nonfinite controls')
                    continue
                if max(abs(value - 1.0) for value in scale) > .0001:
                    failures.append(f'{action.name} frame {frame}: eye.{side} scale must remain one')
                eye_result = {'blink_control': control, 'eye_bone_local_scale': scale, 'parts': {}}
                expected_blink = max(0.0, min(1.0, control))
                for part in EYE_PARTS:
                    name = eye_name(part, side)
                    if name not in vertices or not vertices[name]:
                        continue
                    obj = bpy.data.objects[name]
                    eye_points = [head_inverse @ point for point in vertices[name]]
                    sample_indices = sorted({int(i * (len(eye_points) - 1) / 255)
                                             for i in range(256)})
                    clearances, visible_z = [], []
                    for index in sample_indices:
                        point = eye_points[index]
                        if head_tree is None:
                            continue
                        hit, normal, face, distance = head_tree.ray_cast(
                            Vector((point.x, -10, point.z)), Vector((0, 1, 0)))
                        if hit is not None:
                            clearance = hit.y - point.y
                            clearances.append(clearance)
                            if clearance > .0001:
                                visible_z.append(point.z)
                    source_keys = obj.data.shape_keys
                    keys = source_keys.evaluated_get(graph) if source_keys else None
                    blink = keys.key_blocks.get('Blink') if keys else None
                    observed = float(blink.value) if blink else None
                    error = abs(observed - expected_blink) if observed is not None and math.isfinite(observed) else None
                    if error is None or error > .0001:
                        failures.append(f'{action.name} frame {frame}: {name} Blink does not follow its bone control')
                    path = source_keys.key_blocks['Blink'].path_from_id('value') if source_keys and source_keys.key_blocks.get('Blink') else ''
                    driver = next((curve for curve in source_keys.animation_data.drivers if curve.data_path == path), None) \
                        if source_keys and source_keys.animation_data else None
                    eye_result['parts'][part] = {
                        'mesh': name, 'evaluated_blink_value': observed if error is not None else None,
                        'blink_value_error': error,
                        'driver_is_valid': bool(driver and driver.driver.is_valid),
                        'height_in_head_frame_m': max(p.z for p in eye_points) - min(p.z for p in eye_points),
                        'sampled_projected_visible_height_m': max(visible_z) - min(visible_z) if visible_z else 0.0,
                        'sampled_front_vertex_fraction': len(visible_z) / max(1, len(clearances)),
                        'front_clearance_max_m': max(clearances) if clearances else None,
                        'front_clearance_min_m': min(clearances) if clearances else None,
                        'projection_vertex_samples': len(sample_indices),
                        'head_ray_hits': len(clearances)}
                    if driver is not None and not driver.driver.is_valid:
                        failures.append(f'{action.name} frame {frame}: {name} Blink driver is invalid')
                eyes[side] = eye_result
            current_volume = volume(points, triangles)
            volumes = {name: volume(vertices[name], indices) for name, indices in primary_triangles.items()
                       if name in vertices}
            samples.append({'frame': frame, 'all_skinned_vertices_finite': True,
                            'body_volume_m3': current_volume,
                            'body_volume_ratio_to_rest': current_volume / rest_volume if rest_volume else None,
                            'primary_mesh_volumes_m3': volumes,
                            'body_max_edge_stretch_ratio': worst[0],
                            'body_max_edge_vertices': [worst[1], worst[2]],
                            'body_edge_stretch_p99': quantile([entry[0] for entry in stretch], .99),
                            'foot_heights': foot_heights, 'eyes': eyes})
        valid = [sample for sample in samples if sample.get('all_skinned_vertices_finite')]
        summary = {'sampled_keyframes': len(frames), 'samples': samples}
        if valid:
            summary.update({'volume_ratio_range': [min(s['body_volume_ratio_to_rest'] for s in valid),
                                                    max(s['body_volume_ratio_to_rest'] for s in valid)],
                            'maximum_body_edge_stretch_ratio': max(s['body_max_edge_stretch_ratio'] for s in valid),
                            'minimum_foot_ground_clearance_m': min(f['clearance_above_ground_m']
                                for s in valid for f in s['foot_heights'].values()) if any(s['foot_heights'] for s in valid) else None})
            if summary['maximum_body_edge_stretch_ratio'] > 2.0:
                cautions.append(f'{action.name}: body edge stretch exceeds 2x; inspect armpits and joints visually')
            if summary['minimum_foot_ground_clearance_m'] is not None and summary['minimum_foot_ground_clearance_m'] < -.008:
                cautions.append(f'{action.name}: paw geometry penetrates ground by more than 8 mm')
            low, high = summary['volume_ratio_range']
            if low < .65 or high > 1.45:
                cautions.append(f'{action.name}: body volume departs from rest by more than 35/45 percent')
            summary['primary_mesh_volume_ranges_m3'] = {
                name: [min(s['primary_mesh_volumes_m3'][name] for s in valid),
                       max(s['primary_mesh_volumes_m3'][name] for s in valid)] for name in primary_triangles
                if all(name in s['primary_mesh_volumes_m3'] for s in valid)}
            summary['eyes'] = {}
            for side in ('L', 'R'):
                eye_samples = [sample['eyes'][side] for sample in valid if side in sample['eyes']]
                if not eye_samples:
                    continue
                side_summary = {'blink_control_range': [min(s['blink_control'] for s in eye_samples),
                                                        max(s['blink_control'] for s in eye_samples)],
                                'parts': {}}
                for part in EYE_PARTS:
                    part_samples = [s['parts'][part] for s in eye_samples if part in s['parts']]
                    if not part_samples:
                        continue
                    errors = [s['blink_value_error'] for s in part_samples if s['blink_value_error'] is not None]
                    side_summary['parts'][part] = {
                        'height_range_m': [min(s['height_in_head_frame_m'] for s in part_samples),
                                           max(s['height_in_head_frame_m'] for s in part_samples)],
                        'sampled_front_vertex_fraction_range': [min(s['sampled_front_vertex_fraction'] for s in part_samples),
                                                               max(s['sampled_front_vertex_fraction'] for s in part_samples)],
                        'maximum_blink_value_error': max(errors) if errors else None,
                        'all_sampled_drivers_valid': all(s['driver_is_valid'] for s in part_samples)}
                summary['eyes'][side] = side_summary
        report['runtime_behavior']['actions'][action.name] = summary
    report['contract']['status'] = 'failed' if contract_failure_count else 'passed_measured_checks'
    report['runtime_behavior']['status'] = 'failed' if len(failures) > contract_failure_count else 'measured_requires_visual_review'
    return report


if __name__ == '__main__':
    try:
        result = inspect()
    except Exception:
        result = {'source_blend': bpy.data.filepath, 'status': 'inspection_failed',
                  'error': traceback.format_exc(), 'presentation_delivery': {'status': 'not_verified'}}
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(result, indent=2, allow_nan=False), encoding='utf-8')
    print('Event=CreamCatInspectionComplete Report=' + str(REPORT), flush=True)
    if result.get('status') == 'inspection_failed' or result.get('failures'):
        raise RuntimeError('Cream Cat inspection failed; see inspection.json')
