"""Small reviewable modeling stages executed through Blender MCP.

This owns only the independent CreamCat_Live.blend document. The prior live
scene is backed up before rebuilding; no UE files or preferences are changed.
"""
import importlib
import sys
from datetime import datetime
from pathlib import Path

import bpy
from mathutils import Vector

STATE={}


def viewport(target=(0,0,.64),direction=(2.8,-5,1.6),distance=2.1,clay=False):
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                space=area.spaces.active
                space.region_3d.view_location=target
                space.region_3d.view_rotation=Vector(direction).to_track_quat('Z','Y')
                space.region_3d.view_distance=distance
                space.region_3d.view_perspective='ORTHO'
                space.overlay.show_overlays=False
                space.shading.type='SOLID' if clay else 'MATERIAL'
                if clay:
                    space.shading.color_type='SINGLE'
                    space.shading.single_color=(.63,.58,.49)
                    space.shading.light='STUDIO'
                    space.shading.show_cavity=True
                area.tag_redraw()


def begin():
    if bpy.app.background or Path(bpy.data.filepath).name!='CreamCat_Live.blend':
        raise RuntimeError('Use the separate CreamCat_Live.blend interactive document')
    project=Path(__file__).resolve().parents[2]
    backup=project/'Saved/Art/CreamCat'/('Before_Refine_'+datetime.now().strftime('%Y%m%d_%H%M%S')+'.blend')
    if bpy.context.scene.objects:
        bpy.ops.wm.save_as_mainfile(filepath=str(backup),copy=True)
    sys.dont_write_bytecode=True
    for name in ('cream_cat_body','cream_cat_face','cream_cat_surface','cream_cat_prop'):
        if name in sys.modules:importlib.reload(sys.modules[name])
    import create_cream_cat
    builder=importlib.reload(create_cream_cat)
    if bpy.context.object and bpy.context.object.mode!='OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    for obj in list(bpy.context.scene.objects):bpy.data.objects.remove(obj,do_unlink=True)
    for action in list(bpy.data.actions):
        if action.name.startswith(('AN_CreamCat_','SHOWCASE_')):bpy.data.actions.remove(action)
    for collection in (bpy.data.meshes,bpy.data.curves,bpy.data.armatures,bpy.data.materials):
        for data in list(collection):
            if data.users==0:collection.remove(data)
    for key,color,rough in [('Fur','cream',.72),('Cocoa','cocoa',.68),('Biscuit','biscuit',.7),
                            ('Eye','eye',.19),('Highlight','white',.3),('InnerEar','pink',.74),('Scarf','sage',.86)]:
        builder.material(key,builder.COLORS[color],rough,vertex=True)
    builder.material('Backdrop',builder.linear((.88,.85,.77)),.8)
    STATE.clear();STATE['builder']=builder;STATE['stage']='prepared'
    builder.log('CreamCatLiveBegin',Backup=backup)
    viewport()
    return {'stage':STATE['stage'],'backup':str(backup)}


def body():
    b=STATE['builder']
    STATE['body']=b.build_body(b.ellipsoid,b.capsule,b.finish,b.curve_mesh,b.COLORS,b.TAIL)
    STATE['stage']='body'
    viewport(clay=True)
    return {'stage':'body','vertices':len(STATE['body'].data.vertices)}


def face():
    b=STATE['builder']
    STATE['head']=b.build_face(b.ellipsoid,b.curve_mesh,b.finish,b.COLORS)
    STATE['stage']='face'
    return {'stage':'face','objects':len(b.PARTS)}


def silhouette():
    b=STATE['builder']
    STATE['body']=b.fuse_head_and_body(STATE['body'],STATE.pop('head'),b.finish,b.PARTS,b.WEIGHTS)
    b.apply_chubby_proportions()
    for obj in b.PARTS:
        if obj.name.startswith('Scarf'):obj.hide_set(True);obj.hide_render=True
    STATE['stage']='silhouette'
    b.active(STATE['body'])
    viewport(clay=True)
    return {'stage':'silhouette','continuous_body_vertices':len(STATE['body'].data.vertices)}


def rig():
    b=STATE['builder']
    STATE['rig']=b.make_rig()
    b.skin(STATE['rig']);b.bind_blink(STATE['rig'])
    b.reset_pose(STATE['rig']);b.pose_tucked_paws(STATE['rig'])
    STATE['rig'].hide_set(True)
    b.active(STATE['body'])
    STATE['stage']='posed'
    viewport()
    return {'stage':'posed','bones':len(STATE['rig'].data.bones)}


def animations():
    b=STATE['builder'];rig=STATE['rig']
    rig.hide_set(False)
    b.make_actions(rig);b.validate(rig);b.set_action(rig,'Idle')
    rig.hide_set(True)
    STATE['stage']='animated'
    return {'stage':'animated','actions':list(b.ACTIONS)}


def studio():
    b=STATE['builder']
    STATE['camera']=b.studio()
    for obj in bpy.context.scene.objects:
        if obj.type in {'CAMERA','LIGHT'} or obj.name=='STUDIO_Backdrop':obj.hide_set(True)
    viewport()
    return {'stage':'studio'}


def save():
    b=STATE['builder'];rig=STATE['rig'];scene=bpy.context.scene
    scene['ArtStage']='Chubby short-paw source model; UE assets unchanged'
    scene['AnimationGuide']='Idle 1-121; Walk 1-33; FishingWait 1-151; 30 fps. Select RIG_CreamCat in the Outliner to edit.'
    rig['AnimationGuide']=scene['AnimationGuide'];rig['ForwardAxis']='-Y';rig['HeightMeters']=1.27
    viewport()
    bpy.ops.wm.save_as_mainfile(filepath=str(b.ART/'CreamCat_Live.blend'))
    bpy.ops.wm.save_as_mainfile(filepath=str(b.ART/'CreamCat.blend'),copy=True)
    b.log('CreamCatLiveSaved',Stage=STATE['stage'])
    return {'saved':bpy.data.filepath,'stage':STATE['stage']}
