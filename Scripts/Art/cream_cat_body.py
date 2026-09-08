"""Compact feline body, rounded tail and pet bandana for the Cream Cat source art.

The caller owns the rig and export flow.  Construction helpers bake coordinates
in metres; -Y is forward.  AUTO_BODY is consumed by the caller's heat binding.
"""
import math

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree


def _smooth(a, b, value):
    t = max(0.0, min(1.0, (value - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def _mix(a, b, value):
    return tuple(x * (1.0 - value) + y * value for x, y in zip(a, b))


def _active(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def _apply(obj, modifier):
    _active(obj)
    bpy.ops.object.modifier_apply(modifier=modifier.name)


def _union(parts, name, voxel, iterations, reduction):
    bpy.ops.object.select_all(action='DESELECT')
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = name
    remesh = obj.modifiers.new('Continuous feline surface', 'REMESH')
    remesh.mode = 'VOXEL'
    remesh.voxel_size = voxel
    _apply(obj, remesh)
    smooth = obj.modifiers.new('Soft anatomy', 'SMOOTH')
    smooth.factor = 0.85
    smooth.iterations = iterations
    _apply(obj, smooth)
    decimate = obj.modifiers.new('Prototype density', 'DECIMATE')
    decimate.ratio = reduction
    _apply(obj, decimate)
    subdivision = obj.modifiers.new('Rounded silhouette', 'SUBSURF')
    subdivision.levels = 1
    _apply(obj, subdivision)
    for attr in list(obj.data.color_attributes):
        obj.data.color_attributes.remove(attr)
    return obj


def _profile_surface(name, profile):
    """Closed smooth loft from (z, center_x, center_y, radius_x, radius_y).

    Radius and center derivatives span adjoining sections.  There are no sphere
    intersection ridges at the chest, waist, hocks or shoulders.
    """
    sections = []
    for index in range(len(profile) - 1):
        a, b = profile[index], profile[index + 1]
        before = profile[max(0, index - 1)]
        after = profile[min(len(profile) - 1, index + 2)]
        span = b[0] - a[0]
        for step in range(5):
            t = step / 5.0
            values = [a[0] + span * t]
            for component in range(1, 5):
                ma = (b[component] - before[component]) / (b[0] - before[0])
                mb = (after[component] - a[component]) / (after[0] - a[0])
                value = ((2*t**3 - 3*t*t + 1) * a[component]
                         + (t**3 - 2*t*t + t) * span * ma
                         + (-2*t**3 + 3*t*t) * b[component]
                         + (t**3 - t*t) * span * mb)
                values.append(max(.001, value) if component >= 3 else value)
            sections.append(values)
    sections.append(profile[-1])
    sides = 48
    vertices = [(profile[0][1], profile[0][2], profile[0][0])]
    for z, cx, cy, rx, ry in sections[1:-1]:
        vertices.extend((cx + rx * math.cos(index * 2 * math.pi / sides),
                         cy + ry * math.sin(index * 2 * math.pi / sides), z)
                        for index in range(sides))
    rings = len(sections) - 2
    top = len(vertices)
    vertices.append((profile[-1][1], profile[-1][2], profile[-1][0]))
    faces = [(0, 1 + (index + 1) % sides, 1 + index) for index in range(sides)]
    for ring in range(rings - 1):
        for index in range(sides):
            current = 1 + ring * sides + index
            following = 1 + ring * sides + (index + 1) % sides
            faces.append((current, following, following + sides, current + sides))
    last = 1 + (rings - 1) * sides
    faces.extend((top, last + index, last + (index + 1) % sides) for index in range(sides))
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def _swept_foreleg(name, stations):
    """Sweep round sections normal to a curved 3D limb, not horizontal Z slices."""
    centers = [Vector(point) for point, radius in stations]
    radii = [radius for point, radius in stations]
    sections = []
    for segment in range(len(stations) - 1):
        a, b, c, d = [centers[min(max(segment + offset, 0), len(centers) - 1)]
                      for offset in (-1, 0, 1, 2)]
        ra, rb, rc, rd = [radii[min(max(segment + offset, 0), len(radii) - 1)]
                          for offset in (-1, 0, 1, 2)]
        for step in range(8):
            t = step / 8
            center = .5 * (2*b + (-a+c)*t + (2*a-5*b+4*c-d)*t*t + (-a+3*b-3*c+d)*t**3)
            tangent = .5 * ((-a+c) + 2*(2*a-5*b+4*c-d)*t + 3*(-a+3*b-3*c+d)*t*t)
            radius = .5 * (2*rb + (-ra+rc)*t + (2*ra-5*rb+4*rc-rd)*t*t + (-ra+3*rb-3*rc+rd)*t**3)
            if tangent.length < 1e-8:
                raise ValueError('Foreleg has a degenerate centerline tangent')
            sections.append((center, tangent.normalized(), max(.001, radius)))
    sides = 40
    vertices = [tuple(centers[0])]
    right = Vector((1, 0, 0))
    for center, tangent, radius in sections[1:]:
        # Parallel-transport the section frame to avoid arbitrary twisting.
        right = right - tangent * right.dot(tangent)
        if right.length < 1e-6:
            right = Vector((0, 1, 0)) - tangent * tangent.y
        right.normalize()
        up = tangent.cross(right).normalized()
        for index in range(sides):
            angle = index * 2 * math.pi / sides
            vertices.append(tuple(center + radius * (right * math.cos(angle) + up * math.sin(angle))))
    rings = len(sections) - 1
    tip = len(vertices)
    vertices.append(tuple(centers[-1]))
    faces = [(0, 1 + (index + 1) % sides, 1 + index) for index in range(sides)]
    faces += [(1 + ring*sides + index, 1 + ring*sides + (index+1)%sides,
               1 + (ring+1)*sides + (index+1)%sides, 1 + (ring+1)*sides + index)
              for ring in range(rings-1) for index in range(sides)]
    last = 1 + (rings - 1) * sides
    faces += [(tip, last + index, last + (index+1)%sides) for index in range(sides)]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def build_body(ellipsoid, capsule, finish, curve_mesh, colors, tail_points):
    """Register one continuous body/tail and the bandana; return the body mesh."""
    points = [Vector(point) for point in tail_points]
    if len(points) != 6:
        raise ValueError('Cream Cat tail requires six points for tail.01 through tail.05')
    points[0] = Vector((0, .20, .36))

    def body_paint(vertex):
        x, y, z = vertex
        bib = (x / .218) ** 2 + ((z - .60) / .294) ** 2
        milk = (1.0 - _smooth(.76, 1.08, bib)) * (1.0 - _smooth(-.08, .045, y))
        color = _mix(colors['cream'], colors['milk'], milk)
        # A single asymmetric saddle wraps the back and one flank, like fur.
        patch = ((x + .102) / .254) ** 2 + ((z - .565) / .255) ** 2
        saddle = (1.0 - _smooth(.74, 1.13, patch)) * _smooth(.065, .19, y)
        flank = math.exp(-((x + .246) / .084) ** 2 - ((z - .50) / .17) ** 2)
        flank *= _smooth(-.085, .095, y)
        color = _mix(color, _mix(colors['biscuit'], colors['ginger'], .24), max(saddle, flank * .52) * .76)
        # Restrict the soft tip mark to the distal tail, never the nearby back.
        nearest = (float('inf'), 0.0)
        for index, (start, end) in enumerate(zip(points, points[1:])):
            segment = end - start
            t = max(0.0, min(1.0, (vertex - start).dot(segment) / segment.length_squared))
            distance = (vertex - start - t * segment).length
            if distance < nearest[0]:
                nearest = (distance, (index + t) / 5.0)
        tail = (1.0 - _smooth(.085, .13, nearest[0])) * _smooth(.62, .86, nearest[1])
        tip = (1.0 - _smooth(.055, .16, (vertex - points[-1]).length)) * tail
        rings = sum(math.exp(-((nearest[1] - center) / .038) ** 2) for center in (.47, .65, .82))
        tail_only = (1.0 - _smooth(.092, .13, nearest[0])) * _smooth(.30, .40, nearest[1])
        color = _mix(color, _mix(colors['biscuit'], colors['ginger'], .28), min(1.0, rings) * tail_only * .79)
        # Subtle warm shadows reinforce the actual sculpted toe grooves.
        for sign in (-1, 1):
            for offset in (-.024, .024):
                groove = math.exp(-((x - sign * .287 - offset) / .0055) ** 2)
                groove *= math.exp(-((z - .578) / .032) ** 2) * (1.0 - _smooth(-.235, -.206, y))
                hind = math.exp(-((x - sign * .175 - offset) / .0055) ** 2)
                hind *= math.exp(-((y + .148) / .040) ** 2) * _smooth(.050, .075, z) * (1.0 - _smooth(.095, .13, z))
                color = _mix(color, colors['biscuit'], max(groove, hind) * .25)
        return _mix(color, colors['milk'], tip * .42)

    # One pear-shaped contour owns the whole torso, from haunch to hidden neck.
    # The front radius changes continuously; there is no separate chest/belly.
    parts = [_profile_surface('ContinuousPearTorso', [
        (.185, 0, .070, 0, 0),
        (.230, 0, .066, .135, .105),
        (.300, 0, .061, .230, .173),
        (.400, 0, .045, .288, .221),
        (.500, 0, .027, .301, .235),
        (.600, 0, .010, .287, .226),
        (.700, 0, -.003, .255, .201),
        (.800, 0, -.014, .212, .168),
        (.900, 0, -.021, .146, .124),
        (.967, 0, -.021, .080, .073),
        (1.003, 0, -.021, 0, 0),
    ])]
    for side in (-1, 1):
        parts.extend([
            _swept_foreleg('ContinuousForeleg', [
                ((side * .167, .008, .874), 0),
                ((side * .185, -.015, .841), .082),
                ((side * .200, -.030, .810), .083),
                ((side * .270, -.070, .725), .070),
                ((side * .285, -.150, .655), .062),
                ((side * .287, -.190, .606), .059),
                ((side * .287, -.230, .580), 0),
            ]),
            ellipsoid('FrontPaw', (side * .287, -.19, .606), (.079, .094, .084), weight=None),
            # The broad upper sections are submerged in the torso.  The exposed
            # contour tapers steadily down to the hock instead of making knees.
            _profile_surface('ContinuousHindleg', [
                (.038, side * .175, -.025, 0, 0),
                (.075, side * .175, -.016, .060, .070),
                (.120, side * .175, .004, .066, .082),
                (.190, side * .173, .045, .078, .093),
                (.260, side * .166, .058, .099, .114),
                (.330, side * .156, .060, .115, .137),
                (.400, side * .148, .052, .132, .149),
                (.458, side * .142, .045, .108, .120),
                (.501, side * .139, .043, 0, 0),
            ]),
            ellipsoid('HindPaw', (side * .175, -.055, .062), (.099, .120, .062), weight=None),
        ])
        # Overlapping toe lobes soften the paw edge; no fingers or thumbs.
        for offset in (-.043, 0.0, .043):
            parts.append(ellipsoid('FrontToe', (side * .287 + offset, -.247, .591),
                                   (.033, .043, .043), weight=None, segments=28, rings=20))
            parts.append(ellipsoid('HindToe', (side * .175 + offset, -.143, .047),
                                   (.037, .043, .039), weight=None, segments=28, rings=20))
    # The submerged root, whole tail and round cap share the body's voxel union.
    # A single AUTO_BODY bind includes the tail bones supplied by the caller.
    tail = curve_mesh('TailConstruction', points, .092, 'Fur', None,
                      [1.0, 1.0, .97, .88, .73, .49])
    cap = ellipsoid('TailRoundCap', points[-1], (.046, .046, .046), weight=None,
                    segments=32, rings=24)
    parts.extend([tail, cap])
    body = _union(parts, 'Cat_Body', .010, 4, .45)
    # Sculpt the final surface, so toe clefts survive the voxel smoothing pass.
    for vertex in body.data.vertices:
        x, y, z = vertex.co
        for sign in (-1, 1):
            for offset in (-.024, .024):
                front = math.exp(-((x - sign * .287 - offset) / .006) ** 2)
                front *= math.exp(-((z - .578) / .036) ** 2) * (1.0 - _smooth(-.234, -.212, y))
                vertex.co.y += .0028 * front
                hind = math.exp(-((x - sign * .175 - offset) / .006) ** 2)
                hind *= math.exp(-((y + .149) / .043) ** 2) * _smooth(.040, .063, z) * (1.0 - _smooth(.097, .13, z))
                vertex.co.z -= .0026 * hind
                vertex.co.y += .0018 * hind * (1.0 - _smooth(-.17, -.13, y))
    body.data.update()
    finish(body, 'Fur', 'AUTO_BODY', body_paint)

    # Project shallow, closed skin patches onto the actual unified paw surface.
    # Their rim sits inside the fur; the palm pads never float like pink beads.
    body.data.calc_loop_triangles()
    positions = [vertex.co.copy() for vertex in body.data.vertices]

    def paw_surface(center, normal, bounds):
        """Include only local paw triangles facing this paw's underside."""
        triangles = []
        for face in body.data.loop_triangles:
            vertices = [positions[index] for index in face.vertices]
            centroid = sum(vertices, Vector()) / 3
            delta = centroid - center
            if any(abs(delta[axis]) > bounds[axis] for axis in range(3)):
                continue
            outward = (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0])
            if outward.length < 1e-10 or outward.normalized().dot(normal) < .15:
                continue
            triangles.append(tuple(face.vertices))
        if not triangles:
            raise RuntimeError('No local paw surface triangles near ' + str(tuple(center)))
        return BVHTree.FromPolygons(positions, triangles, all_triangles=True)

    def pad(name, center, normal, tangent, width, height, weight, surface, central=False):
        normal, tangent = Vector(normal).normalized(), Vector(tangent).normalized()
        vertical = normal.cross(tangent).normalized()
        center = Vector(center)
        vertices, faces = [], []
        steps = 32
        # Center and two concentric rings form a subtly domed, fitted patch.
        for radius in (0.0, .52, 1.0):
            for step in range(steps):
                angle = step * 2.0 * math.pi / steps
                # A rounded three-lobed main pad reads as feline metacarpal skin.
                lobe = 1.0 + (.11 * math.cos(3 * angle + math.pi / 2) if central else 0.0)
                probe = center + tangent * (width * radius * math.cos(angle) * lobe)
                probe += vertical * (height * radius * math.sin(angle) * lobe)
                # The probe is inside the paw. Cast out to its first local exit;
                # a remote origin behind the paw could hit the abdomen first.
                hit, hit_normal, face, distance = surface.ray_cast(probe, normal, .14)
                if hit is None or hit_normal.dot(normal) < .15:
                    raise RuntimeError('Paw pad missed its bounded local surface: ' + name)
                vertices.append(tuple(hit + normal * (.0013 * (1.0 - radius * radius) - .00025)))
        # Collapse coincident center samples into one clean center vertex.
        vertices = [vertices[0]] + vertices[steps:]
        for index in range(steps):
            following = (index + 1) % steps
            faces.append((0, 1 + index, 1 + following))
            faces.append((1 + index, 1 + steps + index, 1 + steps + following, 1 + following))
        bottom = len(vertices)
        vertices.append(tuple(Vector(vertices[0]) - normal * .002))
        for index in range(steps):
            faces.append((bottom, 1 + steps + (index + 1) % steps, 1 + steps + index))
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(vertices, [], faces)
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.collection.objects.link(obj)
        finish(obj, 'InnerEar', weight, lambda v: _mix(colors['pink'], colors['milk'], .24))

    for sign, side in ((1, 'L'), (-1, 'R')):
        front_center = Vector((sign * .287, -.19, .606))
        palm_normal = Vector((0, .69, -.724)).normalized()
        toe_axis = Vector((0, -.724, -.69)).normalized()
        front_surface = paw_surface(front_center, palm_normal, (.115, .127, .110))
        pad('FrontPalmPad.' + side, front_center - toe_axis * .008, palm_normal, (1, 0, 0),
            .033, .027, 'hand.' + side, front_surface, True)
        for index, offset in enumerate((-.039, 0.0, .039)):
            center = front_center + Vector((offset, 0, 0)) + toe_axis * (.038 if index == 1 else .032)
            pad('FrontToePad.' + side + str(index), center, palm_normal, (1, 0, 0),
                .014, .017, 'hand.' + side, front_surface)
        hind_center = Vector((sign * .175, -.055, .062))
        hind_normal = Vector((0, 0, -1))
        hind_surface = paw_surface(hind_center, hind_normal, (.130, .160, .095))
        pad('HindPalmPad.' + side, hind_center + Vector((0, .007, 0)), (0, 0, -1), (1, 0, 0),
            .038, .032, 'foot.' + side, hind_surface, True)
        for index, offset in enumerate((-.043, 0.0, .043)):
            center = hind_center + Vector((offset, -.073 if index == 1 else -.063, 0))
            pad('HindToePad.' + side + str(index), center, (0, 0, -1), (1, 0, 0),
                .018, .020, 'foot.' + side, hind_surface)

    # A small pet bandana stays below the cheeks and follows neck/chest bones.
    collar = [(.128 * math.cos(a), -.012 + .128 * math.sin(a), .867 + .008 * math.cos(2 * a))
              for a in [i * 2 * math.pi / 16 for i in range(17)]]
    curve_mesh('ScarfCollar', collar, .022, 'Scarf', 'neck')
    vertices = [(-.112, -.124, .878), (.112, -.124, .878), (.111, -.170, .831),
                (.042, -.201, .748), (0, -.207, .731), (-.046, -.198, .759),
                (-.117, -.159, .838), (0, -.185, .825)]
    mesh = bpy.data.meshes.new('PetBandana')
    mesh.from_pydata(vertices, [], [(7, i, (i + 1) % 7) for i in range(7)])
    scarf = bpy.data.objects.new('ScarfBib', mesh)
    bpy.context.collection.objects.link(scarf)
    solid = scarf.modifiers.new('Soft fabric thickness', 'SOLIDIFY')
    solid.thickness = .012
    _apply(scarf, solid)
    bevel = scarf.modifiers.new('Rounded fabric edge', 'BEVEL')
    bevel.width = .007
    bevel.segments = 3
    _apply(scarf, bevel)
    sub = scarf.modifiers.new('Pet bandana folds', 'SUBSURF')
    sub.levels = 2
    _apply(scarf, sub)
    finish(scarf, 'Scarf', lambda v: {'neck': _smooth(.78, .88, v.z),
                                    'chest': 1.0 - _smooth(.78, .88, v.z)})
    ellipsoid('ScarfKnot', (.04, .12, .866), (.035, .03, .026), 'Scarf', 'neck')
    return body
