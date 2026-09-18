"""Build an independent rigged whip concept in a factory-startup Blender process.

Run: blender --background --factory-startup --python Scripts/Art/create_whip.py
Existing deliveries are protected: pass -- --replace to regenerate them.
"""
import bpy
import math
import json
import sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'SourceArt/Props/Whip'
PREVIEW = OUT / 'Previews'
if (OUT / 'Whip.blend').exists() and '--replace' not in sys.argv:
    raise RuntimeError('Whip.blend exists; save manual edits separately before using --replace.')
if bpy.data.filepath or any(o.type == 'ARMATURE' for o in bpy.data.objects):
    raise RuntimeError('Run in a separate factory-startup Blender process.')
OUT.mkdir(parents=True, exist_ok=True)
PREVIEW.mkdir(exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
bpy.context.preferences.filepaths.save_version = 0
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1.0
scene.render.fps = 30
asset = bpy.data.collections.new('WHIP_ASSET')
scene.collection.children.link(asset)
studio = bpy.data.collections.new('PREVIEW_STUDIO')
scene.collection.children.link(studio)

def relocate(obj, collection):
    for c in list(obj.users_collection):
        c.objects.unlink(obj)
    collection.objects.link(obj)

def material(name, color, roughness, metallic=0, grain=False):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    nt = m.node_tree
    p = nt.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value = (*color, 1)
    p.inputs['Roughness'].default_value = roughness
    p.inputs['Metallic'].default_value = metallic
    if grain:
        noise = nt.nodes.new('ShaderNodeTexNoise')
        noise.inputs['Scale'].default_value = 170
        noise.inputs['Detail'].default_value = 2
        coord = nt.nodes.new('ShaderNodeTexCoord')
        nt.links.new(coord.outputs['Generated'], noise.inputs['Vector'])
        bump = nt.nodes.new('ShaderNodeBump')
        bump.inputs['Strength'].default_value = 0.19
        bump.inputs['Distance'].default_value = 0.0005
        nt.links.new(noise.outputs['Fac'], bump.inputs['Height'])
        nt.links.new(bump.outputs['Normal'], p.inputs['Normal'])
    return m

leather = material('M_Whip_ChestnutLeather', (0.17, 0.055, 0.021), .56, grain=True)
gripmat = material('M_Whip_DarkLeather', (.057, .024, .013), .67, grain=True)
wood = material('M_Whip_Walnut', (.12, .054, .024), .43, grain=True)
brass = material('M_Whip_AgedBrass', (.32, .22, .09), .39, .72)
seammat = material('M_Whip_Seam', (.25, .13, .058), .7)
parts = []

def mesh_obj(name, verts, faces, mat):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    asset.objects.link(obj)
    obj.data.materials.append(mat)
    for p in mesh.polygons:
        p.use_smooth = True
    parts.append(obj)
    return obj

def lathe(name, rings, mat, sides=24, flatten=1):
    verts = [(x, r*math.cos(j*math.tau/sides), r*math.sin(j*math.tau/sides)*flatten)
             for x, r in rings for j in range(sides)]
    faces = []
    for i in range(len(rings)-1):
        for j in range(sides):
            a=i*sides+j; b=i*sides+(j+1)%sides
            faces.append((a,b,b+sides,a+sides))
    faces.extend([tuple(reversed(range(sides))), tuple((len(rings)-1)*sides+j for j in range(sides))])
    obj = mesh_obj(name, verts, faces, mat)
    uv = obj.data.uv_layers.new(name='UVMap')
    # Cylindrical seam per face: no UV interpolation through the wrap seam.
    for poly in obj.data.polygons:
        js=[obj.data.loops[li].vertex_index % sides for li in poly.loop_indices]
        wrap = 0 in js and sides-1 in js
        for li in poly.loop_indices:
            vi=obj.data.loops[li].vertex_index
            i,j=divmod(vi,sides)
            uv.data[li].uv=(i/(len(rings)-1), 1.0 if wrap and j==0 else j/sides)
    return obj

def rigid(obj):
    obj.vertex_groups.new(name='root').add(list(range(len(obj.data.vertices))), 1, 'REPLACE')

grip=lathe('Handle_Core',[(-.1,.008),(-.097,.017),(-.086,.024),(-.072,.023),(-.06,.016),
                        (-.052,.017),(.068,.017),(.082,.019),(.096,.017),(.1,.011)],wood)
rigid(grip)
for name, a,b,r,mat in [('Grip',-.053,.073,.018,gripmat),('PommelBand',-.063,-.053,.019,brass),
                        ('Collar',.078,.099,.020,brass)]:
    obj=lathe(name,[(a,r*.91),(a+.002,r),(b-.002,r),(b,r*.91)],mat)
    rigid(obj)

def cord(name, points, radius, mat):
    verts=[]; faces=[]; sides=6
    for i,p in enumerate(points):
        p=Vector(p)
        tangent=(Vector(points[min(i+1,len(points)-1)])-Vector(points[max(0,i-1)])).normalized()
        n=tangent.cross(Vector((1,0,0))).normalized()
        if n.length < .1: n=tangent.cross(Vector((0,1,0))).normalized()
        b=tangent.cross(n).normalized()
        for j in range(sides):
            v=p+radius*(math.cos(j*math.tau/sides)*n+math.sin(j*math.tau/sides)*b)
            verts.append(tuple(v))
    for i in range(len(points)-1):
        for j in range(sides):
            a=i*sides+j; b=i*sides+(j+1)%sides
            faces.append((a,b,b+sides,a+sides))
    faces.extend([tuple(reversed(range(sides))),tuple((len(points)-1)*sides+j for j in range(sides))])
    obj=mesh_obj(name,verts,faces,mat)
    rigid(obj)
    return obj

cord('Grip_Wrap_Seam', [(-.05+.122*i/180,.0183*math.cos(i/180*math.tau*5),
                       .0183*math.sin(i/180*math.tau*5)) for i in range(181)], .00085,seammat)

# Low relief paw seal on the side of the wooden pommel.
for i,(x,z,sx,sz) in enumerate([(-.085,-.003,.006,.005),(-.091,.005,.0023,.003),
                              (-.085,.008,.0023,.003),(-.078,.006,.0023,.003)]):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12,ring_count=6,location=(x,-.023,z))
    obj=bpy.context.object; obj.name=f'Paw_Seal_{i}'
    obj.scale=(sx,.001,sz)
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    relocate(obj,asset); obj.data.materials.append(brass)
    parts.append(obj); rigid(obj)

rings=[]
for i in range(97):
    t=i/96
    radius=.011*(1-t)**.85+.0012
    # A small soft flattened spatulate tip, continuous with the lash.
    if t>.94: radius+=.0045*math.sin((t-.94)/.06*math.pi)
    if i==96: radius=.0007
    rings.append((.1+t,radius))
lash=lathe('Lash',rings,leather,sides=12,flatten=.86)
for i in range(16): lash.vertex_groups.new(name=f'lash_{i+1:02d}')
for v in lash.data.vertices:
    t=(v.co.x-.1)*16-.5
    lo=math.floor(t); f=t-lo
    weights={}
    for idx,w in [(max(0,min(15,lo)),1-f),(max(0,min(15,lo+1)),f)]: weights[idx]=weights.get(idx,0)+w
    for idx,w in weights.items():
        if w>1e-7: lash.vertex_groups[f'lash_{idx+1:02d}'].add([v.index],w,'REPLACE')

bpy.ops.object.select_all(action='DESELECT')
for obj in parts: obj.select_set(True)
bpy.context.view_layer.objects.active=grip
bpy.ops.object.join()
mesh=bpy.context.object; mesh.name='SK_Whip'
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

arm=bpy.data.armatures.new('SKEL_Whip')
rig=bpy.data.objects.new('RIG_Whip',arm); asset.objects.link(rig)
bpy.ops.object.select_all(action='DESELECT'); rig.select_set(True)
bpy.context.view_layer.objects.active=rig
bpy.ops.object.mode_set(mode='EDIT')
root=arm.edit_bones.new('root'); root.head=(-.1,0,0); root.tail=(.1,0,0); root.align_roll(Vector((0,0,1)))
previous=root
for i in range(16):
    b=arm.edit_bones.new(f'lash_{i+1:02d}')
    b.head=(.1+i/16,0,0); b.tail=(.1+(i+1)/16,0,0)
    b.parent=previous; b.use_connect=True; b.align_roll(Vector((0,0,1))); previous=b
bpy.ops.object.mode_set(mode='OBJECT')
rig.show_in_front=True; arm.display_type='STICK'
mesh.parent=rig
mod=mesh.modifiers.new('Whip_Skin','ARMATURE'); mod.object=rig
# Classic linear skinning matches conventional game-engine bone weighting.
mod.use_deform_preserve_volume=False
rig['concept_only']=True
rig['length_m']=1.2
rig['grip_hint']='Handle center at origin, rest lash along +X, up +Z.'

def pose(angles, root_angle=0):
    prev=root_angle
    rig.pose.bones['root'].rotation_mode='XYZ'
    rig.pose.bones['root'].rotation_euler=(math.radians(root_angle),0,0)
    for i,a in enumerate(angles):
        b=rig.pose.bones[f'lash_{i+1:02d}']; b.rotation_mode='XYZ'
        b.rotation_euler=(math.radians(a-prev),0,0); prev=a

def key(frame):
    for b in rig.pose.bones:
        b.keyframe_insert('rotation_euler',frame=frame,group=b.name)

idle=[-35-60*(i/15)**.6 for i in range(16)]
pose(idle)
for frame in range(1,62):
    phase=(frame-1)/60*math.tau
    pose([a+3*math.sin(phase-i*.18)*(i/15) for i,a in enumerate(idle)])
    key(frame)
idle_action=rig.animation_data.action; idle_action.name='AN_Whip_Idle'; idle_action.use_fake_user=True
rig.animation_data.action=None
phases=[(1,0,idle),(10,35,[55+180*(i/15) for i in range(16)]),
        (16,-12,[-8+180*(i/15)**2 for i in range(16)]),
        (21,-8,[-8+18*math.sin(i/15*math.tau) for i in range(16)]),
        (26,-2,[-15-25*(i/15)+24*math.sin(i/15*math.pi) for i in range(16)]),
        (36,5,[-25-90*(i/15)+20*math.sin(i/15*math.pi) for i in range(16)]),(49,0,idle)]
for frame in range(1,50):
    pair=next((a,b) for a,b in zip(phases,phases[1:]) if a[0]<=frame<=b[0])
    a,b=pair; t=(frame-a[0])/(b[0]-a[0]); t=t*t*(3-2*t)
    pose([x+(y-x)*t for x,y in zip(a[2],b[2])],a[1]+(b[1]-a[1])*t); key(frame)
attack=rig.animation_data.action; attack.name='AN_Whip_Attack'; attack.use_fake_user=True
scene.frame_start=1; scene.frame_end=49
for name,fr in [('Idle',1),('Windup',10),('Strike_Visual_Only',21),('FollowThrough',26),('Recovered',49)]:
    scene.timeline_markers.new(name,frame=fr)

# Structural and actual evaluated-animation measurements before delivery.
report={'blender':bpy.app.version_string,'bones':len(arm.bones),'vertices':len(mesh.data.vertices),
        'triangles':sum(len(p.vertices)-2 for p in mesh.data.polygons),'length_m':1.2,
        'actions':{'AN_Whip_Idle':[1,61],'AN_Whip_Attack':[1,49]},'checks':{}}
assert len(arm.bones)==17
assert all(abs(sum(g.weight for g in v.groups)-1)<1e-5 for v in mesh.data.vertices)
assert all(len(v.groups)<=2 for v in mesh.data.vertices)
report['checks']['normalized_weights']=True
report['checks']['max_two_influences']=True
report['checks']['all_bone_scales_one']=all(tuple(b.scale)==(1,1,1) for b in rig.pose.bones)
samples=[]
for action,end in [(idle_action,61),(attack,49)]:
    rig.animation_data.action=action
    for fr in range(1,end+1):
        scene.frame_set(fr); bpy.context.view_layer.update()
        ev=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get()); em=ev.to_mesh()
        assert all(math.isfinite(c) for v in em.vertices for c in v.co)
        tip=rig.pose.bones['lash_16'].tail
        samples.append({'action':action.name,'frame':fr,'tip':[round(c,5) for c in tip]})
        ev.to_mesh_clear()
report['checks']['evaluated_frames_finite']=len(samples)
report['tip_samples']=samples
rig.animation_data.action=attack; scene.frame_set(21)

def point_at(obj,point): obj.rotation_euler=(Vector(point)-obj.location).to_track_quat('-Z','Y').to_euler()
def camera(name, loc, target, scale):
    data=bpy.data.cameras.new(name); obj=bpy.data.objects.new(name,data); studio.objects.link(obj)
    obj.location=loc; point_at(obj,target); data.type='ORTHO'; data.ortho_scale=scale
    return obj
cam=camera('Camera_Hero',(1.0,-2.8,1.5),(.4,0,-.08),1.6)
scene.camera=cam
for name,loc,power,size in [('Key',(.1,-1.5,2),180,2),('Fill',(1,1,1),110,1.5),('Rim',(-1,.2,.5),90,1)]:
    data=bpy.data.lights.new(name,'AREA'); data.energy=power; data.shape='DISK'; data.size=size
    obj=bpy.data.objects.new(name,data); studio.objects.link(obj); obj.location=loc; point_at(obj,(.4,0,0))
scene.world.color=(.25,.25,.25)
scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.38,.32,.25,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=.5
scene.render.engine='CYCLES'; scene.cycles.samples=32
scene.cycles.use_denoising=True
scene.render.resolution_x=1600; scene.render.resolution_y=900; scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
scene.render.film_transparent=True
scene.view_settings.view_transform='AgX'
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_distance=1.8
            area.spaces.active.region_3d.view_location=(.4,0,-.15)
            area.spaces.active.region_3d.view_rotation=cam.rotation_euler.to_quaternion()
            area.spaces.active.shading.type='MATERIAL'
scene.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Whip.blend'))

# Export mesh in rest pose and a separate baked animation. No extra leaf bones.
bpy.ops.object.select_all(action='DESELECT')
mesh.select_set(True); rig.select_set(True); bpy.context.view_layer.objects.active=rig
rig.animation_data.action=None
pose([0]*16)
bpy.context.view_layer.update()
fbxargs=dict(use_selection=True,object_types={'ARMATURE','MESH'},add_leaf_bones=False,
             axis_forward='-Y',axis_up='Z',apply_unit_scale=True,mesh_smooth_type='FACE',
             use_mesh_modifiers=True,armature_nodetype='NULL',path_mode='AUTO')
bpy.ops.export_scene.fbx(filepath=str(OUT/'SK_Whip.fbx'),bake_anim=False,**fbxargs)
for action,end in [(idle_action,61),(attack,49)]:
    rig.animation_data.action=action; scene.frame_end=end; scene.frame_set(1)
    bpy.ops.export_scene.fbx(filepath=str(OUT/(action.name+'.fbx')),bake_anim=True,
        bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,bake_anim_simplify_factor=0,**fbxargs)
rig.animation_data.action=attack; scene.frame_end=49

def render(name,frame):
    scene.frame_set(frame)
    bpy.context.view_layer.update()
    # Fit actual evaluated geometry, including the drooping tip and windup arc.
    ev=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get()); em=ev.to_mesh()
    inv=cam.rotation_euler.to_matrix().transposed()
    coords=[inv @ (ev.matrix_world @ v.co) for v in em.vertices]
    low=Vector(tuple(min(v[i] for v in coords) for i in range(3)))
    high=Vector(tuple(max(v[i] for v in coords) for i in range(3)))
    center=cam.rotation_euler.to_matrix() @ ((low+high)/2)
    cam.location=center+cam.rotation_euler.to_matrix() @ Vector((0,0,3))
    aspect=scene.render.resolution_x/scene.render.resolution_y
    cam.data.ortho_scale=max(high.x-low.x,(high.y-low.y)*aspect)*1.18
    ev.to_mesh_clear()
    scene.render.filepath=str(PREVIEW/name)
    bpy.ops.render.render(write_still=True)

render('Whip_Hero.png',21)
cam.location=(.3,-3,-.15); point_at(cam,(.3,0,-.15)); cam.data.ortho_scale=2.1
for name,fr in [('Whip_Idle.png',1),('Whip_Windup.png',10),('Whip_Strike.png',21),('Whip_Recovery.png',36)]:
    render(name,fr)
rig.animation_data.action=None; pose([0]*16); bpy.context.view_layer.update()
cam.location=(.5,-3,0); point_at(cam,(.5,0,0)); cam.data.ortho_scale=1.4
render('Whip_Front.png',1)
cam.location=(.5,0,3); point_at(cam,(.5,0,0)); render('Whip_Top.png',1)
cam.location=(3,0,0); point_at(cam,(0,0,0)); cam.data.ortho_scale=.08
render('Whip_Right.png',1)
rig.animation_data.action=attack; scene.frame_set(1)
cam.location=(.45,-3,-.12); point_at(cam,(.45,0,-.12)); cam.data.ortho_scale=1.6
scene.render.filepath='//Previews/Whip_Animation_'
scene.render.resolution_x=960; scene.render.resolution_y=960
scene.cycles.samples=16
scene.frame_end=49
# Save portable references and ready-to-play attack action.
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Whip.blend'))
(OUT/'whip_manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('WHIP_BUILD_OK',json.dumps({k:v for k,v in report.items() if k!='tip_samples'}))
