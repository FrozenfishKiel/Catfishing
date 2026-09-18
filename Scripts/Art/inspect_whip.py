"""Reopen the whip, check deformation/FBX round trips, render a short preview.

Run in background factory startup, after create_whip.py. Output evidence is local
Saved/Art/Whip; deliverable animation frames are encoded separately for preview.
"""
import bpy
import bmesh
import json
import traceback
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'SourceArt/Props/Whip'
EVIDENCE=ROOT/'Saved/Art/Whip'
EVIDENCE.mkdir(parents=True,exist_ok=True)
report={}
try:
    bpy.ops.wm.open_mainfile(filepath=str(OUT/'Whip.blend'))
    scene=bpy.context.scene
    rig=bpy.data.objects['RIG_Whip']; obj=bpy.data.objects['SK_Whip']
    assert len(rig.data.bones)==17
    assert abs(max(v.co.x for v in obj.data.vertices)-min(v.co.x for v in obj.data.vertices)-1.2)<1e-5
    bm=bmesh.new(); bm.from_mesh(obj.data)
    report['nonmanifold_edges']=sum(not e.is_manifold for e in bm.edges)
    assert report['nonmanifold_edges']==0
    bm.free()
    ends={}
    bounds=[]
    for name,last in [('AN_Whip_Idle',61),('AN_Whip_Attack',49)]:
        rig.animation_data.action=bpy.data.actions[name]
        poses=[]
        for frame in range(1,last+1):
            scene.frame_set(frame); bpy.context.view_layer.update()
            tip=rig.pose.bones['lash_16'].tail.copy()
            poses.append(tip)
            assert all(abs(b.scale.x-1)<1e-6 and abs(b.scale.y-1)<1e-6 and abs(b.scale.z-1)<1e-6 for b in rig.pose.bones)
            ev=obj.evaluated_get(bpy.context.evaluated_depsgraph_get()); mesh=ev.to_mesh()
            bounds.extend(ev.matrix_world @ v.co for v in mesh.vertices)
            ev.to_mesh_clear()
        assert (poses[0]-poses[-1]).length<1e-5
        report[name]={'frames':last,'closed_endpoints':True,'tip_excursion_m':max((v-poses[0]).length for v in poses)}
    assert report['AN_Whip_Attack']['tip_excursion_m']>.5
    report['source_reopen']='passed'
    # Capture a common bounding box so the camera never follows/crops the lash.
    xmin=min(p.x for p in bounds); xmax=max(p.x for p in bounds)
    zmin=min(p.z for p in bounds); zmax=max(p.z for p in bounds)
    report['animated_bounds_xz']=[xmin,xmax,zmin,zmax]

    for filename in ['SK_Whip.fbx','AN_Whip_Idle.fbx','AN_Whip_Attack.fbx']:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=str(OUT/filename),use_anim=True)
        rigs=[o for o in bpy.context.scene.objects if o.type=='ARMATURE']
        meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
        assert len(rigs)==1 and len(meshes)==1
        assert len(rigs[0].data.bones)==17
        assert all(abs(sum(g.weight for g in v.groups)-1)<1e-4 for v in meshes[0].data.vertices)
        if filename=='SK_Whip.fbx':
            coords=[meshes[0].matrix_world @ v.co for v in meshes[0].data.vertices]
            extent=max(max(v[i] for v in coords)-min(v[i] for v in coords) for i in range(3))
            assert abs(extent-1.2)<.001,extent
            report['fbx_rest_length_m']=extent
        else:
            assert rigs[0].animation_data and rigs[0].animation_data.action
            imported_action=rigs[0].animation_data.action
            tips=[]
            for frame in [1,10,21,49 if 'Attack' in filename else 61]:
                bpy.context.scene.frame_set(frame); bpy.context.view_layer.update()
                tips.append(rigs[0].pose.bones['lash_16'].tail.copy())
            assert max((t-tips[0]).length for t in tips)>.001
        report[filename]='roundtrip_passed'
    report['status']='passed'
    (EVIDENCE/'inspection.json').write_text(json.dumps(report,indent=2),encoding='utf-8')

    bpy.ops.wm.open_mainfile(filepath=str(OUT/'Whip.blend'))
    scene=bpy.context.scene; cam=scene.camera
    cam.location=((xmin+xmax)/2,-3,(zmin+zmax)/2)
    target=Vector(((xmin+xmax)/2,0,(zmin+zmax)/2))
    cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler()
    cam.data.ortho_scale=max(xmax-xmin,zmax-zmin)*1.18
    scene.render.resolution_x=800; scene.render.resolution_y=800
    scene.cycles.samples=12
    scene.render.film_transparent=False
    # Warm neutral world for a self-contained, visible video background.
    scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.19,.17,.145,1)
    frames=EVIDENCE/'Frames'; frames.mkdir(exist_ok=True)
    for frame in range(1,50,2):
        scene.frame_set(frame)
        scene.render.filepath=str(frames/f'{frame:04d}.png')
        bpy.ops.render.render(write_still=True)
    report['preview_frames']=25
    (EVIDENCE/'inspection.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('WHIP_INSPECTION_OK')
except Exception:
    report['status']='failed'; report['error']=traceback.format_exc()
    (EVIDENCE/'inspection.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    raise
