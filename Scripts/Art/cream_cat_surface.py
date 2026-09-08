"""Fuse only the requested head/body meshes and restore their vertex colors.

The surviving object is Cat_Body; Cat_Head is removed by Blender's join.
Consumers that raycast or look up Cat_Head must use the returned body instead.
"""

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree


def fuse_head_and_body(body, head, finish, parts, weights):
    """Return one registered AUTO_BODY surface; leave all other objects intact."""
    if body == head or any(obj.type != 'MESH' for obj in (body, head)):
        raise ValueError('Fusion requires two distinct mesh objects')
    if any(obj.data.shape_keys for obj in (body, head)):
        raise ValueError('Head/body fusion must precede their shape keys')
    if bpy.context.object and bpy.context.object.mode != 'OBJECT':
        raise RuntimeError('Fusion must run in Object mode')
    original_names = (body.name, head.name)
    original_parts = (body, head)
    head_matrix = head.matrix_world.copy()
    head_surface = BVHTree.FromPolygons(
        [head_matrix @ vertex.co for vertex in head.data.vertices],
        [tuple(face.vertices) for face in head.data.polygons])
    samples, colors = [], []
    for obj in original_parts:
        color = obj.data.color_attributes.get('Color')
        if color is None or color.domain != 'POINT' or color.data_type != 'FLOAT_COLOR':
            raise ValueError(f'{obj.name} requires POINT FLOAT_COLOR named Color')
        matrix = obj.matrix_world.copy()
        for vertex in obj.data.vertices:
            samples.append(matrix @ vertex.co)
            colors.append(tuple(color.data[vertex.index].color[:3]))
    if not samples:
        raise ValueError('Cannot fuse empty meshes')
    tree = KDTree(len(samples))
    for index, point in enumerate(samples):
        tree.insert(point, index)
    tree.balance()

    def paint(point):
        nearby = tree.find_n(point, min(4, len(samples)))
        if nearby[0][2] < .000001:
            return colors[nearby[0][1]]
        influences = [(index, 1.0 / max(distance, .000001)) for _, index, distance in nearby]
        total = sum(weight for _, weight in influences)
        return tuple(sum(colors[index][axis] * weight for index, weight in influences) / total
                     for axis in range(3))

    # Remove references before join invalidates the old head Python wrapper.
    parts[:] = [obj for obj in parts if obj not in original_parts and obj.name not in original_names]
    for name in original_names:
        weights.pop(name, None)
    for key in ('FaceDesign', 'BlinkControl'):
        if key in head:
            body[key] = head[key]
    bpy.ops.object.select_all(action='DESELECT')
    body.select_set(True)
    head.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.join()
    body.name = 'Cat_Body'
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    def apply(modifier):
        bpy.context.view_layer.objects.active = body
        result = bpy.ops.object.modifier_apply(modifier=modifier.name)
        if 'FINISHED' not in result:
            raise RuntimeError('Failed to apply fusion modifier')

    modifier = body.modifiers.new('Unified head and body', 'REMESH')
    modifier.mode = 'VOXEL'
    # A finer uniform surface replaces collapse-decimate followed by subdivision.
    # Subdividing irregular collapsed triangles amplified tiny shading dimples and
    # multiplied the mesh density without adding useful anatomical detail.
    modifier.voxel_size = .006
    modifier.use_smooth_shade = True
    apply(modifier)
    modifier = body.modifiers.new('Soften anatomical join', 'SMOOTH')
    modifier.factor = .45
    modifier.iterations = 5
    apply(modifier)

    def smoothstep(a, b, value):
        t = max(0.0, min(1.0, (value - a) / (b - a)))
        return t * t * (3 - 2 * t)

    # Eyelids, nose and their Blink targets were built on the original head.
    # Preserve that surface in the face; blend to the fused neck outside it.
    projected, maximum_projection = 0, 0.0
    for vertex in body.data.vertices:
        x, y, z = vertex.co
        strength = ((1 - smoothstep(.245, .335, abs(x)))
                    * smoothstep(.880, .940, z)
                    * (1 - smoothstep(1.230, 1.315, z))
                    * (1 - smoothstep(-.090, -.020, y)))
        if strength <= .0001:
            continue
        hit, normal, _, _ = head_surface.ray_cast(Vector((x, -2.0, z)), Vector((0, 1, 0)), 4.0)
        if hit is None or normal.y > -.15 or abs(hit.y - y) > .025:
            continue
        correction = (hit.y - y) * strength
        vertex.co.y += correction
        maximum_projection = max(maximum_projection, abs(correction))
        projected += 1
    body.data.update()
    if len(body.data.vertices) > 250000:
        raise RuntimeError('Fused surface exceeded the 250000-vertex budget')
    for color in list(body.data.color_attributes):
        body.data.color_attributes.remove(color)
    finish(body, 'Fur', 'AUTO_BODY', paint)
    body['FusedSurfaceSources'] = ', '.join(original_names)
    body['FusionVoxelMeters'] = .006
    body['FusionPipeline'] = 'uniform_voxel_smooth_face_projection'
    body['FusionFaceProjectionVertices'] = projected
    body['FusionFaceMaxProjectionMeters'] = maximum_projection
    if 'FusionDecimateRatio' in body:
        del body['FusionDecimateRatio']
    return body
