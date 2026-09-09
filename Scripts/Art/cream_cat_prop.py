"""Standalone fishing prop for the Cream Cat Blender demonstration.

Coordinates and dimensions are metres. This module creates mesh objects only;
it never registers character parts, bones, animation, or Unreal assets. The
caller owns the returned objects and their lifetime. ``material_callback`` is
reserved for caller compatibility; deliberately independent Principled
materials keep this optional prop outside the character material registry.
"""
import math

import bpy
from mathutils import Matrix, Vector


def build_fishing_prop(lower_grip, upper_grip, material_callback=None):
    """Return a warm, round fishing rod aligned through both paw centres.

    ``lower_grip`` and ``upper_grip`` are world-space vectors in metres. The
    lower paw sits 0.14 m from the butt; the shaft's local +Z points from the
    lower paw toward the upper paw. Geometry is static and intentionally
    separate from the rig. Repeated calls create another independent prop.
    """
    lower = Vector(lower_grip)
    upper = Vector(upper_grip)
    if not all(math.isfinite(value) for value in (*lower, *upper)):
        raise ValueError('Fishing prop grip centres must be finite')
    direction = upper - lower
    if direction.length < 0.005:
        raise ValueError('Fishing prop needs two distinct grip centres')
    direction.normalize()
    base = lower - direction * 0.14
    # Retain a consistent lateral direction when the rod leans toward the cat.
    right = Vector((1.0, 0.0, 0.0))
    right -= direction * right.dot(direction)
    if right.length < 0.05:
        right = Vector((0.0, -1.0, 0.0))
        right -= direction * right.dot(direction)
    right.normalize()
    forward = direction.cross(right).normalized()
    rotation = Matrix((right, forward, direction)).transposed().to_quaternion()
    inverse = rotation.inverted()
    objects = []

    def material(name, srgb, roughness=0.62, metallic=0.0):
        color = tuple(((v + 0.055) / 1.055) ** 2.4 if v > 0.04045 else v / 12.92
                      for v in srgb)
        mat = bpy.data.materials.new('PROP_' + name)
        mat.use_nodes = True
        mat.diffuse_color = (*color, 1.0)
        shader = next((node for node in mat.node_tree.nodes if node.type == 'BSDF_PRINCIPLED'), None)
        if shader is None:
            shader = mat.node_tree.nodes.new('ShaderNodeBsdfPrincipled')
            output = mat.node_tree.nodes.new('ShaderNodeOutputMaterial')
            mat.node_tree.links.new(shader.outputs['BSDF'], output.inputs['Surface'])
        shader.inputs['Base Color'].default_value = (*color, 1.0)
        shader.inputs['Roughness'].default_value = roughness
        shader.inputs['Metallic'].default_value = metallic
        return mat

    bamboo = material('WarmBamboo', (0.69, 0.45, 0.235))
    cork = material('SoftCork', (0.75, 0.565, 0.35), 0.83)
    sage = material('SageGrip', (0.42, 0.51, 0.355), 0.76)
    dark = material('ReelBrown', (0.33, 0.25, 0.165), 0.64)
    brass = material('MutedBrass', (0.69, 0.56, 0.34), 0.42, 0.24)
    line_mat = material('FishingLine', (0.77, 0.72, 0.57), 0.5)
    coral = material('FloatCoral', (0.84, 0.46, 0.37), 0.59)
    milk = material('FloatMilk', (0.96, 0.905, 0.77), 0.64)

    def mesh(name, vertices, faces, mat, flat_faces=()):
        data = bpy.data.meshes.new('PROP_' + name + '_Mesh')
        data.from_pydata(vertices, [], faces)
        data.update()
        obj = bpy.data.objects.new('PROP_' + name, data)
        bpy.context.collection.objects.link(obj)
        obj.location = base
        obj.rotation_mode = 'QUATERNION'
        obj.rotation_quaternion = rotation
        data.materials.append(mat)
        for polygon in data.polygons:
            polygon.use_smooth = polygon.index not in flat_faces
        objects.append(obj)
        return obj

    def lathe(name, profile, mat, center=(0, 0, 0), axis=(0, 0, 1), sides=48):
        """Round closed surface; profile entries are axial offset and radius."""
        orient = Vector((0, 0, 1)).rotation_difference(Vector(axis))
        center = Vector(center)
        vertices = []
        for height, radius in profile:
            for index in range(sides):
                angle = 2.0 * math.pi * index / sides
                point = Vector((radius * math.cos(angle), radius * math.sin(angle), height))
                vertices.append(tuple(center + orient @ point))
        faces = []
        for ring in range(len(profile) - 1):
            for index in range(sides):
                following = (index + 1) % sides
                faces.append((ring * sides + index, ring * sides + following,
                              (ring + 1) * sides + following, (ring + 1) * sides + index))
        faces.extend((tuple(reversed(range(sides))),
                      tuple((len(profile) - 1) * sides + index for index in range(sides))))
        return mesh(name, vertices, faces, mat, (len(faces) - 2, len(faces) - 1))

    def tube(name, points, radii, mat, sides=48):
        points = [Vector(point) for point in points]
        if isinstance(radii, (int, float)):
            radii = [radii] * len(points)
        vertices = []
        for index, point in enumerate(points):
            tangent = points[min(index + 1, len(points) - 1)] - points[max(index - 1, 0)]
            tangent.normalize()
            reference = Vector((1, 0, 0)) if abs(tangent.x) < 0.9 else Vector((0, 1, 0))
            axis_a = tangent.cross(reference).normalized()
            axis_b = tangent.cross(axis_a).normalized()
            for side in range(sides):
                angle = 2.0 * math.pi * side / sides
                vertices.append(tuple(point + radii[index] *
                                      (axis_a * math.cos(angle) + axis_b * math.sin(angle))))
        faces = []
        for index in range(len(points) - 1):
            for side in range(sides):
                following = (side + 1) % sides
                faces.append((index * sides + side, index * sides + following,
                              (index + 1) * sides + following, (index + 1) * sides + side))
        faces.extend((tuple(reversed(range(sides))),
                      tuple((len(points) - 1) * sides + side for side in range(sides))))
        return mesh(name, vertices, faces, mat, (len(faces) - 2, len(faces) - 1))

    def sphere(name, center, scale, mat, axis=(0, 0, 1)):
        # Small polar radii avoid degenerate faces at the ends of a lathe.
        profile = [(math.cos(math.pi * i / 24) * scale[2],
                    max(0.00001, math.sin(math.pi * i / 24) * scale[0]))
                   for i in range(24, -1, -1)]
        return lathe(name, profile, mat, center, axis)

    # Leave the hand span straight; the top half bends gently under its own weight.
    shaft_points = []
    shaft_radii = []
    for index in range(37):
        t = index / 36.0
        bend = max(0.0, (t - 0.57) / 0.43)
        shaft_points.append((0.045 * bend * bend, 0.0, t * 1.80))
        shaft_radii.append(0.009 * (1.0 - t) + 0.003 * t)
    tube('BambooShaft', shaft_points, shaft_radii, bamboo)

    lathe('CorkHandle', [(0.018, 0.014), (0.022, 0.019), (0.029, 0.020),
                         (0.262, 0.020), (0.274, 0.017), (0.278, 0.012)], cork)
    lathe('ButtCap', [(0.012, 0.014), (0.017, 0.0195), (0.034, 0.021),
                     (0.039, 0.0195)], sage)
    lathe('UpperGripBand', [(0.242, 0.0204), (0.247, 0.021),
                           (0.259, 0.021), (0.264, 0.0198)], sage)
    lathe('GripCollar', [(0.278, 0.012), (0.287, 0.012), (0.292, 0.0095)], brass)

    # A compact round reel sits close to the lower grip, with no box-shaped casing.
    tube('ReelFoot', [(0, 0, 0.082), (0, -0.025, 0.082), (0, -0.054, 0.087)],
         0.0065, brass, sides=32)
    reel_center = (0, -0.056, 0.088)
    lathe('RoundReel', [(-0.025, 0.026), (-0.021, 0.041), (-0.014, 0.043),
                        (0.014, 0.043), (0.021, 0.041), (0.025, 0.026)], dark,
          reel_center, (1, 0, 0))
    lathe('ReelSpool', [(-0.028, 0.022), (-0.025, 0.034), (-0.018, 0.034),
                        (0.018, 0.034), (0.025, 0.034), (0.028, 0.022)], brass,
          reel_center, (1, 0, 0))
    lathe('ReelHub', [(-0.032, 0.010), (0.032, 0.010)], sage,
          reel_center, (1, 0, 0))
    tube('ReelCrank', [(0.033, -0.056, 0.088), (0.038, -0.074, 0.105),
                       (0.046, -0.074, 0.105)], 0.0037, brass, sides=24)
    sphere('ReelKnob', (0.048, -0.074, 0.105), (0.007, 0.007, 0.009), cork, (1, 0, 0))

    # Three restrained thread bands describe the bamboo construction.
    for index, (height, radius) in enumerate(((0.64, 0.0071), (1.10, 0.0056), (1.52, 0.0043))):
        t = height / 1.8
        offset = 0.045 * max(0.0, (t - 0.57) / 0.43) ** 2
        lathe('ShaftBinding_' + str(index + 1), [(height - 0.004, radius + 0.0007),
                                               (height + 0.004, radius + 0.0007)], sage,
              center=(offset, 0, 0), sides=32)

    tip = Vector(shaft_points[-1])
    tip_world = base + rotation @ tip
    # A free-hanging line is vertical in world space, beyond the rod tip and face.
    float_world = tip_world + Vector((0.065, -0.015, 0.0))
    float_world.z = 0.30
    line_points = []
    for index in range(33):
        t = index / 32.0
        point = tip_world.lerp(float_world, t)
        point.x += 0.018 * math.sin(math.pi * t)
        line_points.append(inverse @ (point - base))
    tube('HangingLine', line_points, 0.0007, line_mat, sides=12)
    float_center = inverse @ (float_world - base)
    gravity_axis = inverse @ Vector((0, 0, 1))
    lathe('FloatCoral', [(-0.028, 0.001), (-0.024, 0.009), (-0.012, 0.016),
                         (0.003, 0.019), (0.007, 0.0185)], coral,
          float_center, gravity_axis)
    lathe('FloatCream', [(0.007, 0.0185), (0.019, 0.015), (0.028, 0.007),
                         (0.031, 0.001)], milk, float_center, gravity_axis)
    lathe('FloatStem', [(-0.041, 0.0025), (0.043, 0.0025)], bamboo,
          float_center, gravity_axis, sides=24)
    return objects
