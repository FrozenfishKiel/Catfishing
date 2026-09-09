"""Build the independent Cream Cat art prototype in Blender 5.2.

Run Blender --background --factory-startup --python <this file>.
Writes only SourceArt/Characters/CreamCat and Saved/Art/CreamCat.
This is an art source project, not a replacement for the gameplay character.
"""
import bpy
import json
import math
import sys
import traceback
from pathlib import Path
from mathutils import Vector, Quaternion
from bpy_extras import anim_utils

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cream_cat_body import build_body
from cream_cat_face import build_face, bind_blink
from cream_cat_prop import build_fishing_prop
from cream_cat_surface import fuse_head_and_body

PROJECT = Path(__file__).resolve().parents[2]
ART = PROJECT / 'SourceArt/Characters/CreamCat'
REPORT = PROJECT / 'Saved/Art/CreamCat'
PREVIEW = ART / 'Previews'
for folder in (ART, REPORT, PREVIEW):
    folder.mkdir(parents=True, exist_ok=True)

def log(event, **fields):
    line = 'Event=' + event + ' ' + ' '.join(f'{k}={v}' for k, v in fields.items())
    print(line, flush=True)
    with (REPORT / 'build.log').open('a', encoding='utf-8') as stream:
        stream.write(line + '\n')

def smoothstep(a, b, value):
    t = max(0.0, min(1.0, (value-a)/(b-a)))
    return t*t*(3-2*t)

def mix(a, b, t):
    return tuple(x*(1-t)+y*t for x,y in zip(a,b))

def shape_point(point):
    """One rest-space transformation shared by anatomy, rig and pose targets."""
    x,y,z=point
    trunk=1-smoothstep(.62,.98,z)
    return Vector((x*(1+.18*trunk),.07+(y-.07)*(1+.15*trunk),
                   z-.21*smoothstep(0,.85,z)))

def apply_chubby_proportions():
    for obj in PARTS:
        if obj.data.shape_keys:
            for key in obj.data.shape_keys.key_blocks:
                for vertex in key.data:vertex.co=shape_point(vertex.co)
        else:
            for vertex in obj.data.vertices:vertex.co=shape_point(vertex.co)
        obj.data.update()

def linear(rgb):
    return tuple(((v+0.055)/1.055)**2.4 if v > .04045 else v/12.92 for v in rgb)

COLORS = {k: linear(v) for k,v in {
    'cream':(.94,.835,.65), 'milk':(1.0,.945,.81),
    'ginger':(.76,.50,.27), 'biscuit':(.87,.655,.39),
    'cocoa':(.245,.145,.086), 'eye':(.125,.076,.045),
    'pink':(.82,.48,.40), 'blush':(.90,.66,.51),
    'sage':(.46,.52,.34), 'sage_light':(.57,.62,.42),
    'white':(1.0,.98,.89), 'gold':(.76,.57,.28),
}.items()}

PARTS = []
WEIGHTS = {}
MATERIALS = {}

def material(name, color, roughness=.7, vertex=False):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color,1)
    m.use_nodes = True
    shader = next((node for node in m.node_tree.nodes if node.type == 'BSDF_PRINCIPLED'), None)
    if shader is None:
        shader = m.node_tree.nodes.new('ShaderNodeBsdfPrincipled')
        output = next((node for node in m.node_tree.nodes if node.type == 'OUTPUT_MATERIAL'), None)
        if output is None:
            output = m.node_tree.nodes.new('ShaderNodeOutputMaterial')
        m.node_tree.links.new(shader.outputs['BSDF'], output.inputs['Surface'])
    shader.inputs['Base Color'].default_value = (*color,1)
    shader.inputs['Roughness'].default_value = roughness
    if vertex:
        attr = m.node_tree.nodes.new('ShaderNodeVertexColor')
        attr.layer_name = 'Color'
        m.node_tree.links.new(attr.outputs['Color'], shader.inputs['Base Color'])
    if name == 'Fur':
        shader.inputs['Subsurface Weight'].default_value = .065
    MATERIALS[name] = m
    return m

def active(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active=obj

def apply(obj, mod):
    active(obj)
    bpy.ops.object.modifier_apply(modifier=mod.name)

def finish(obj, mat, weight=None, paint=None):
    active(obj)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj.data.materials.clear()
    obj.data.materials.append(MATERIALS[mat])
    for face in obj.data.polygons:
        face.use_smooth = True
    col = obj.data.color_attributes.new(name='Color', type='FLOAT_COLOR', domain='POINT')
    base = tuple(MATERIALS[mat].diffuse_color[:3])
    for i, vert in enumerate(obj.data.vertices):
        col.data[i].color = (*(paint(vert.co) if paint else base),1)
    obj.data.color_attributes.active_color = col
    if weight is not None:
        PARTS.append(obj)
        WEIGHTS[obj.name] = weight
    return obj

def ellipsoid(name, center, scale, mat='Fur', weight='head', paint=None, segments=48, rings=32):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments, ring_count=rings, location=center)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    return finish(obj, mat, weight, paint)

def curve_mesh(name, points, radius, mat, weight='head', radii=None):
    curve = bpy.data.curves.new(name, 'CURVE')
    curve.dimensions = '3D'
    curve.resolution_u=16
    curve.bevel_depth=radius
    curve.bevel_resolution=4
    curve.use_fill_caps=True
    spline=curve.splines.new('BEZIER')
    spline.bezier_points.add(len(points)-1)
    for i,(p,co) in enumerate(zip(spline.bezier_points, points)):
        p.co=co
        p.handle_left_type='AUTO'
        p.handle_right_type='AUTO'
        if radii:
            p.radius=radii[i]
    obj=bpy.data.objects.new(name,curve)
    bpy.context.collection.objects.link(obj)
    active(obj)
    bpy.ops.object.convert(target='MESH')
    return finish(obj,mat,weight)

def capsule(name, a, b, radius, depth_scale=1):
    center=(Vector(a)+Vector(b))*.5
    obj=ellipsoid(name, center, (radius,radius*depth_scale,(Vector(b)-Vector(a)).length*.5+radius*.7), weight=None)
    # Its geometry was baked by finish; align a fresh centered ellipsoid instead.
    for v in obj.data.vertices:
        v.co-=center
        v.co=(Vector((0,0,1)).rotation_difference(Vector(b)-Vector(a)) @ v.co)+center
    return obj

BONES={}
TAIL=[(0,.20,.36),(.12,.34,.405),(.30,.43,.54),(.40,.44,.74),(.43,.40,.90),(.37,.33,1.03)]

def make_rig():
    arm=bpy.data.armatures.new('CreamCat_Skeleton')
    rig=bpy.data.objects.new('RIG_CreamCat',arm)
    bpy.context.collection.objects.link(rig)
    active(rig)
    bpy.ops.object.mode_set(mode='EDIT')
    def bone(name,a,b,parent=None,deform=True):
        a,b=shape_point(a),shape_point(b)
        v=arm.edit_bones.new(name);v.head=a;v.tail=b;v.use_deform=deform
        if parent:v.parent=arm.edit_bones[parent]
        BONES[name]=(Vector(a),Vector(b))
    bone('root',(0,0,0),(0,0,.16),deform=False)
    bone('pelvis',(0,.035,.39),(0,.04,.57),'root')
    bone('spine',(0,.04,.57),(0,0,.73),'pelvis')
    bone('chest',(0,0,.73),(0,-.02,.87),'spine')
    bone('neck',(0,-.02,.87),(0,0,.945),'chest')
    bone('head',(0,0,.945),(0,0,1.24),'neck')
    bone('Mouth',(0,-.35,.989),(0,-.42,.989),'head',False)
    for sign,suffix in [(1,'L'),(-1,'R')]:
        bone('ear.'+suffix,(sign*.245,0,1.22),(sign*.277,0,1.44),'head')
        bone('eye.'+suffix,(sign*.145,-.300,1.097),(sign*.145,-.300,1.157),'head')
        bone('upper_arm.'+suffix,(sign*.20,-.03,.81),(sign*.27,-.07,.725),'chest')
        bone('forearm.'+suffix,(sign*.27,-.07,.725),(sign*.285,-.15,.655),'upper_arm.'+suffix)
        bone('hand.'+suffix,(sign*.285,-.15,.655),(sign*.287,-.245,.565),'forearm.'+suffix)
        bone('thigh.'+suffix,(sign*.155,.05,.33),(sign*.175,.08,.195),'pelvis')
        bone('shin.'+suffix,(sign*.175,.08,.195),(sign*.175,-.015,.090),'thigh.'+suffix)
        bone('foot.'+suffix,(sign*.175,-.015,.090),(sign*.175,-.185,.05),'shin.'+suffix)
        bone('grip.'+suffix,(sign*.287,-.19,.606),(sign*.287,-.25,.606),'hand.'+suffix,False)
    for i in range(5):bone(f'tail.{i+1:02d}',TAIL[i],TAIL[i+1], 'pelvis' if i==0 else f'tail.{i:02d}')
    bpy.ops.object.mode_set(mode='OBJECT')
    rig.show_in_front=True
    arm.display_type='OCTAHEDRAL'
    for b in rig.pose.bones:b.rotation_mode='QUATERNION'
    for side in ('L','R'):rig.pose.bones['eye.'+side]['blink']=0.0
    return rig

def skin(rig):
    body_bones = {'pelvis','spine','chest','neck','head'} | {f'tail.{i:02d}' for i in range(1,6)} | {
        part+'.'+side for part in ('upper_arm','forearm','hand','thigh','shin','foot')
        for side in ('L','R')}
    for obj in PARTS:
        rule=WEIGHTS[obj.name]
        if rule == 'AUTO_BODY':
            # Heat diffusion follows the connected surface at the armpit.
            deform={b.name:b.use_deform for b in rig.data.bones}
            for b in rig.data.bones:b.use_deform=b.name in body_bones
            active(rig);obj.select_set(True)
            bpy.ops.object.parent_set(type='ARMATURE_AUTO')
            for b in rig.data.bones:b.use_deform=deform[b.name]
            for v in obj.data.vertices:
                total=sum(g.weight for g in v.groups)
                if total < .00001:
                    raise RuntimeError(f'Bone heat left body vertex {v.index} unweighted')
                # Keep the eyes, nose and mouth on the same rigid head frame;
                # the lower neck blends into the continuous shoulder surface.
                head_mix=smoothstep(.65,.78,v.co.z)
                values={obj.vertex_groups[g.group].name:g.weight/total*(1-head_mix) for g in v.groups}
                values['head']=values.get('head',0.0)+head_mix
                # The round belly belongs to the pelvis. Leg heat must fade
                # through the haunch instead of pulling a horizontal belly fold.
                hind_mix=(1-smoothstep(.20,.36,v.co.z))*smoothstep(.07,.17,abs(v.co.x))
                transferred=0.0
                for name in list(values):
                    if name.startswith(('thigh.','shin.','foot.')):
                        transferred+=values[name]*(1-hind_mix)
                        values[name]*=hind_mix
                values['pelvis']=values.get('pelvis',0.0)+transferred
                for name,value in values.items():
                    group=obj.vertex_groups.get(name) or obj.vertex_groups.new(name=name)
                    group.add([v.index],value,'REPLACE')
            log('CreamCatBodyHeatBound',Vertices=len(obj.data.vertices))
        else:
            for i,v in enumerate(obj.data.vertices):
                values={rule:1.0} if isinstance(rule,str) else rule(v.co)
                values={k:w for k,w in values.items() if w>.00001}
                total=sum(values.values())
                for name,w in values.items():
                    group=obj.vertex_groups.get(name) or obj.vertex_groups.new(name=name)
                    group.add([i],w/total,'REPLACE')
            mod=obj.modifiers.new('CreamCat deformation','ARMATURE');mod.object=rig
            obj.parent=rig
        for mod in obj.modifiers:
            if mod.type=='ARMATURE':mod.use_deform_preserve_volume=True
        if rule=='AUTO_BODY':
            correction=obj.modifiers.new('Soft joint volume','CORRECTIVE_SMOOTH')
            correction.factor=.75
            correction.iterations=12
            correction.smooth_type='LENGTH_WEIGHTED'


def make_model():
    body=build_body(ellipsoid,capsule,finish,curve_mesh,COLORS,TAIL)
    head=build_face(ellipsoid,curve_mesh,finish,COLORS)
    fuse_head_and_body(body,head,finish,PARTS,WEIGHTS)
    apply_chubby_proportions()
    for obj in PARTS:
        if obj.name.startswith('Scarf'):
            obj.hide_set(True)
            obj.hide_render=True

def world_rotation(rig,name,axis,angle):
    bone=rig.pose.bones[name]
    rot=bone.bone.matrix_local.to_3x3()
    bone.rotation_quaternion=Quaternion(rot.inverted()@Vector(axis),angle)

def reset_pose(rig):
    for b in rig.pose.bones:
        b.location=(0,0,0);b.rotation_quaternion=(1,0,0,0);b.scale=(1,1,1)

def point_bone(rig,name,target):
    b=rig.pose.bones[name]
    bpy.context.view_layer.update()
    matrix=b.matrix.copy()
    start=matrix.translation
    delta=(Vector(target)-start).normalized()
    # Preserve the roll of the currently inherited frame while aiming local +Y.
    q=matrix.to_quaternion()
    q= (q@Vector((0,1,0))).rotation_difference(delta) @ q
    matrix=q.to_matrix().to_4x4();matrix.translation=start
    b.matrix=matrix
    bpy.context.view_layer.update()

def pose_front_paws(rig,targets):
    for side,target in targets.items():
        hand_end=shape_point(Vector(target)+Vector((0,-.025,-.085)))
        target=shape_point(target)
        upper=rig.pose.bones['upper_arm.'+side]
        bpy.context.view_layer.update()
        start=upper.matrix.translation.copy()
        l1=(BONES['upper_arm.'+side][1]-BONES['upper_arm.'+side][0]).length
        l2=(BONES['forearm.'+side][1]-BONES['forearm.'+side][0]).length
        vec=target-start
        distance=min(vec.length,l1+l2-.004)
        direction=vec.normalized()
        along=(l1*l1-l2*l2+distance*distance)/(2*distance)
        height=math.sqrt(max(.00001,l1*l1-along*along))
        bend=Vector((.5 if side=='L' else -.5,.25,-1))
        bend=(bend-direction*bend.dot(direction)).normalized()
        elbow=start+direction*along+bend*height
        point_bone(rig,'upper_arm.'+side,elbow)
        point_bone(rig,'forearm.'+side,start+direction*distance)
        # Compact hooked forepaw, without a thumb or palm-up human gesture.
        point_bone(rig,'hand.'+side,hand_end)


def pose_fishing(rig, phase=0):
    pose_front_paws(rig,{'L':(.10,-.235,.70),'R':(-.10,-.245,.82)})
    world_rotation(rig,'head',(1,0,0),math.radians(3+1.2*math.sin(phase)))


def pose_tucked_paws(rig,phase=0):
    pose_front_paws(rig,{side:(sign*.22,-.19,.665+.004*math.sin(phase))
                        for sign,side in ((1,'L'),(-1,'R'))})

def pose_walk(rig,phase):
    pelvis=rig.pose.bones['pelvis']
    lift=-.012+.004*(1-math.cos(phase*2))
    pelvis.location=pelvis.bone.matrix_local.to_3x3().inverted()@Vector((0,0,lift))
    bpy.context.view_layer.update()
    for sign,side in [(1,'L'),(-1,'R')]:
        p=phase+(0 if sign==1 else math.pi)
        target=shape_point((sign*.175,-.015-.05*math.cos(p),.090+.040*max(0,-math.sin(p))))
        upper=rig.pose.bones['thigh.'+side]
        start=upper.matrix.translation.copy()
        l1=(BONES['thigh.'+side][1]-BONES['thigh.'+side][0]).length
        l2=(BONES['shin.'+side][1]-BONES['shin.'+side][0]).length
        delta=target-start;d=min(delta.length,l1+l2-.002);direction=delta.normalized()
        along=(l1*l1-l2*l2+d*d)/(2*d)
        bend=Vector((0,1,0));bend=(bend-direction*bend.dot(direction)).normalized()
        knee=start+direction*along+bend*math.sqrt(max(.00001,l1*l1-along*along))
        point_bone(rig,'thigh.'+side,knee)
        point_bone(rig,'shin.'+side,start+direction*d)
        foot=rig.pose.bones['foot.'+side]
        matrix=foot.bone.matrix_local.copy();matrix.translation=foot.matrix.translation
        foot.matrix=matrix
    pose_tucked_paws(rig,phase)
    world_rotation(rig,'spine',(0,1,0),.02*math.sin(phase))
    world_rotation(rig,'head',(0,1,0),-.015*math.sin(phase))

ACTIONS={}
def make_actions(rig):
    for label,frames in [('Idle',120),('Walk',32),('FishingWait',150)]:
        action=bpy.data.actions.new('AN_CreamCat_'+label);action.use_fake_user=True
        slot=action.slots.new(rig.id_type,rig.name)
        rig.animation_data_create();rig.animation_data.action=action;rig.animation_data.action_slot=slot
        bpy.context.scene.frame_start=1;bpy.context.scene.frame_end=frames+1
        for frame in range(1,frames+2,2):
            bpy.context.scene.frame_set(frame)
            reset_pose(rig)
            t=(frame-1)/frames;phase=t*2*math.pi
            if label=='Walk':
                pose_walk(rig,phase)
            elif label=='FishingWait':
                world_rotation(rig,'spine',(1,0,0),.018*math.sin(phase))
                bpy.context.view_layer.update()
                pose_fishing(rig,phase)
            else:
                world_rotation(rig,'spine',(1,0,0),.014*math.sin(phase))
                world_rotation(rig,'head',(0,0,1),.035*math.sin(phase))
                pose_tucked_paws(rig,phase)
            rig.pose.bones['chest'].scale=(1+.007*math.sin(phase),1+.010*math.sin(phase),1+.004*math.sin(phase))
            for i in range(5):
                world_rotation(rig,f'tail.{i+1:02d}',(0,0,1),.06*math.sin(phase-i*.5))
            for s,side in [(1,'L'),(-1,'R')]:
                world_rotation(rig,'ear.'+side,(0,1,0),s*.018*math.sin(phase))
                blink_center=.65 if label=='Idle' else .64
                blink=max(0,1-abs(t-blink_center)/.042) if label!='Walk' else 0
                # Surface-projected shape keys close the eye without sinking into the face.
                eye=rig.pose.bones['eye.'+side]
                eye['blink']=blink
                eye.keyframe_insert(data_path='["blink"]',frame=frame,group=eye.name)
            for bone in rig.pose.bones:
                bone.keyframe_insert(data_path='location',frame=frame,group=bone.name)
                bone.keyframe_insert(data_path='rotation_quaternion',frame=frame,group=bone.name)
                bone.keyframe_insert(data_path='scale',frame=frame,group=bone.name)
        bag=anim_utils.action_get_channelbag_for_slot(action,slot)
        for curve in bag.fcurves:
            for point in curve.keyframe_points:point.interpolation='LINEAR'
            curve.modifiers.new('CYCLES')
        action['loop_frames']=frames
        action['fps']=30
        action['motion']='in_place'
        ACTIONS[label]=(action,slot,frames)
        log('CreamCatActionBuilt',Action=action.name,Frames=frames+1,FPS=30)

def set_action(rig,label):
    action,slot,frames=ACTIONS[label]
    rig.animation_data.action=action;rig.animation_data.action_slot=slot
    bpy.context.scene.frame_start=1;bpy.context.scene.frame_end=frames+1
    bpy.context.scene.frame_set(1)
    bpy.context.view_layer.update()

def make_rod(rig):
    # Keep the existing game's export as a hidden, dimensionally intact reference.
    path=ART/'Reference/SM_Rod_Reference.fbx'
    if path.exists():
        before=set(bpy.data.objects)
        bpy.ops.import_scene.fbx(filepath=str(path),use_anim=False)
        for obj in [o for o in bpy.data.objects if o not in before]:
            obj.name='REFERENCE_ExistingFishingRod'
            obj['source_asset']='/Game/Catfishing/Fishing/Presentation/SM_Rod_BendSource'
            obj['reference_only']=True
            obj.hide_render=True
            obj.hide_set(True)
    set_action(rig,'FishingWait')
    centers={}
    for sign,side in [(1,'L'),(-1,'R')]:
        bone=rig.pose.bones['hand.'+side]
        centers[side]=bone.matrix @ bone.bone.matrix_local.inverted() @ shape_point((sign*.287,-.19,.606))
    props=build_fishing_prop(centers['L'],centers['R'],material)
    for obj in props:obj['source_art_prop']=True
    return props

def track(obj,target):obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()

def studio():
    scene=bpy.context.scene
    scene.render.engine='CYCLES';scene.cycles.samples=48
    scene.cycles.use_denoising=True
    scene.render.resolution_x=1400;scene.render.resolution_y=1400;scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG'
    scene.render.film_transparent=False
    scene.render.fps=30
    scene.world.color=(.25,.25,.25)
    scene.world.use_nodes=True
    background=next((node for node in scene.world.node_tree.nodes if node.type=='BACKGROUND'),None)
    if background is None:
        background=scene.world.node_tree.nodes.new('ShaderNodeBackground')
        output=scene.world.node_tree.nodes.new('ShaderNodeOutputWorld')
        scene.world.node_tree.links.new(background.outputs[0],output.inputs[0])
    background.inputs[0].default_value=(.70,.76,.86,1)
    background.inputs[1].default_value=.35
    scene.view_settings.view_transform='AgX'
    floor=min(v.co.z for v in bpy.data.objects['Cat_Body'].data.vertices)-.001
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,floor))
    ground=bpy.context.object;ground.name='STUDIO_Backdrop';ground.data.materials.append(MATERIALS['Backdrop'])
    for name,loc,energy,size,color in [('Key',(-3,-4,5),500,4,(1,.88,.73)),('Fill',(3,-1,3),300,3,(.83,.90,1)),('Rim',(1,3,4),600,3,(1,.85,.66))]:
        data=bpy.data.lights.new('STUDIO_'+name,'AREA');data.energy=energy;data.shape='DISK';data.size=size;data.color=color
        obj=bpy.data.objects.new(data.name,data);scene.collection.objects.link(obj);obj.location=loc;track(obj,(0,0,.8))
    data=bpy.data.cameras.new('CAM_Hero');cam=bpy.data.objects.new('CAM_Hero',data);scene.collection.objects.link(cam)
    cam.location=(2.8,-5,2.1);data.type='ORTHO';data.ortho_scale=1.66;track(cam,(0,0,.63));scene.camera=cam
    scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1.0
    return cam

def render(path,cam,location,target,scale):
    cam.location=location;track(cam,target);cam.data.ortho_scale=scale
    scene=bpy.context.scene;scene.render.filepath=str(path)
    log('CreamCatRenderStart',File=path.name)
    bpy.ops.render.render(write_still=True)
    log('CreamCatRenderComplete',File=path.name)

def validate(rig):
    failures=[]
    for obj in PARTS:
        for v in obj.data.vertices:
            total=sum(g.weight for g in v.groups)
            if abs(total-1)>.002:failures.append(f'{obj.name} vertex {v.index} weights={total}');break
    loops={}
    motion={}
    for label,(action,slot,frames) in ACTIONS.items():
        set_action(rig,label)
        bpy.context.scene.frame_set(1);bpy.context.view_layer.update()
        first={b.name:b.matrix.copy() for b in rig.pose.bones}
        bpy.context.scene.frame_set(frames+1);bpy.context.view_layer.update()
        diff=max(abs(first[b.name][i][j]-b.matrix[i][j]) for b in rig.pose.bones for i in range(4) for j in range(4))
        loops[label]=diff
        if diff>.001:failures.append(f'{label} loop delta {diff}')
        bpy.context.scene.frame_set(frames//4+1);bpy.context.view_layer.update()
        motion[label]=max(abs(first[b.name][i][j]-b.matrix[i][j]) for b in rig.pose.bones for i in range(4) for j in range(4))
        if motion[label]<.005:failures.append(f'{label} has no appreciable pose motion')
    report={'blender':bpy.app.version_string,'units':'meters','fps':30,'bones':len(rig.data.bones),
            'meshes':len(PARTS),'vertices':sum(len(o.data.vertices) for o in PARTS),
            'triangles':sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in PARTS),
            'actions':{label:{'name':a.name,'frames':f+1,'duration_seconds':f/30} for label,(a,s,f) in ACTIONS.items()},
            'loop_matrix_max_delta':loops,'quarter_cycle_matrix_delta':motion,'failures':failures,
            'gameplay_assets_modified':False,'ue_imported':False,
            'reference_rod_asset':'/Game/Catfishing/Fishing/Presentation/SM_Rod_BendSource'}
    (REPORT/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    (ART/'cream_cat_manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    if failures:raise RuntimeError('; '.join(failures))
    log('CreamCatValidationPass',Bones=report['bones'],Vertices=report['vertices'],Triangles=report['triangles'])

def main():
    if not bpy.app.background:
        raise RuntimeError('Run in a separate --background --factory-startup Blender process; keep the interactive scene intact')
    (REPORT/'build.log').write_text('',encoding='utf-8')
    log('CreamCatBuildStart',Version=bpy.app.version_string)
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    for key,color,rough in [('Fur','cream',.72),('Cocoa','cocoa',.68),('Biscuit','biscuit',.7),('Eye','eye',.19),('Highlight','white',.3),('InnerEar','pink',.74),('Scarf','sage',.86)]:
        material(key,COLORS[color],rough,vertex=True)
    material('RodReference',linear((.34,.24,.14)),.56)
    material('Backdrop',linear((.88,.85,.77)),.8)
    make_model();rig=make_rig();skin(rig);bind_blink(rig);make_actions(rig)
    rods=make_rod(rig);cam=studio()
    validate(rig)
    set_action(rig,'Idle')
    for o in rods:o.hide_render=True;o.hide_set(True)
    if '--quick' not in sys.argv:
        render(PREVIEW/'CreamCat_Hero.png',cam,(2.8,-5,2.1),(0,0,.63),1.66)
        render(PREVIEW/'CreamCat_Front.png',cam,(0,-6,1.15),(0,0,.63),1.57)
        render(PREVIEW/'CreamCat_Side.png',cam,(6,-.1,1.2),(0,.10,.63),1.68)
        render(PREVIEW/'CreamCat_Back.png',cam,(-3,5,2.0),(0,.06,.63),1.74)
        bpy.context.scene.frame_set(79)
        render(PREVIEW/'CreamCat_Blink.png',cam,(1.5,-5,1.9),(0,0,.75),1.30)
    set_action(rig,'FishingWait')
    for o in rods:o.hide_render=False;o.hide_set(False)
    render(PREVIEW/'CreamCat_Fishing.png',cam,(3.1,-5,2.4),(-.08,-.10,1.12),2.75)
    # Open in the approachable neutral hero pose; reference collection stays available.
    set_action(rig,'Idle')
    for o in rods:o.hide_render=True;o.hide_set(True)
    cam.location=(2.8,-5,2.1);track(cam,(0,0,.63));cam.data.ortho_scale=1.66
    scene=bpy.context.scene
    scene['ArtStage']='Blender prototype; existing UE assets remain unchanged'
    scene['AnimationGuide']='Select RIG_CreamCat; Action Editor: Idle (1-121), Walk (1-33), FishingWait (1-151). 30 fps; in-place loops.'
    rig['AnimationGuide']=scene['AnimationGuide']
    rig['ForwardAxis']='-Y';rig['HeightMeters']=1.27
    # Friendly default viewport, with cameras/lights hidden from the modeling view.
    for o in scene.objects:
        if o.type in {'LIGHT','CAMERA'} or o.name=='STUDIO_Backdrop':o.hide_set(True)
    rig.hide_set(True)
    active(bpy.data.objects['Cat_Body'])
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.region_3d.view_distance=3.0
                area.spaces.active.region_3d.view_location=(0,0,.8)
                area.spaces.active.region_3d.view_rotation=Quaternion((.861,.504,.034,.057)).normalized()
                area.spaces.active.shading.type='MATERIAL'
    bpy.ops.wm.save_as_mainfile(filepath=str(ART/'CreamCat.blend'))
    log('CreamCatBuildComplete',Blend=ART/'CreamCat.blend')

if __name__=='__main__':
    try:
        main()
    except Exception:
        (REPORT/'error.txt').write_text(traceback.format_exc(),encoding='utf-8')
        log('CreamCatBuildFailed',Error=traceback.format_exc())
        raise
