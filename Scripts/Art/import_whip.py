"""Import and configure the whip prototype in UE; preserves unrelated assets/IDs.

Run with UE PythonScript commandlet. Set WHIP_VERIFY_ONLY=1 to cold-load checks.
Re-running reuses generated packages and only updates this item's configuration.
"""
import json
import os
from pathlib import Path
import unreal

PROJECT=Path(__file__).resolve().parents[2]
SOURCE=PROJECT/'SourceArt/Props/Whip'
DEST='/Game/Catfishing/Items/Whip'
ITEM='/Game/Catfishing/Data/Items/Item_Whip'
CATALOG='/Game/Catfishing/Data/Items/DT_ItemCatalog'
ITEM_ID=51
LIB=unreal.EditorAssetLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()

def require(ok,message):
    if not ok: raise RuntimeError(message)

def save(asset):
    require(LIB.save_loaded_asset(asset,only_if_is_dirty=False),'Save failed: '+asset.get_path_name())

def load(path):
    return unreal.load_asset(path) if LIB.does_asset_exist(path) else None

def import_fbx(name,filename,skeleton=None):
    old=load(DEST+'/'+name)
    if old and old.get_editor_property('skeleton'): return old
    opt=unreal.FbxImportUI()
    opt.automated_import_should_detect_type=False
    opt.import_as_skeletal=True
    opt.import_mesh=skeleton is None
    opt.mesh_type_to_import=unreal.FBXImportType.FBXIT_ANIMATION if skeleton else unreal.FBXImportType.FBXIT_SKELETAL_MESH
    opt.import_animations=skeleton is not None
    opt.skeleton=skeleton
    opt.import_materials=False; opt.import_textures=False; opt.create_physics_asset=False
    opt.skeletal_mesh_import_data.convert_scene=True
    opt.skeletal_mesh_import_data.convert_scene_unit=True
    opt.anim_sequence_import_data.convert_scene=True
    opt.anim_sequence_import_data.convert_scene_unit=True
    task=unreal.AssetImportTask()
    task.filename=str(SOURCE/filename); task.destination_path=DEST; task.destination_name=name
    task.automated=True; task.save=True; task.options=opt; task.factory=unreal.FbxFactory()
    task.replace_existing=old is not None
    task.replace_existing_settings=True
    TOOLS.import_asset_tasks([task])
    objects=task.get_objects()
    cls=unreal.AnimSequence if skeleton else unreal.SkeletalMesh
    result=next((o for o in objects if isinstance(o,cls)),None)
    require(result,'FBX import returned no '+name)
    generated_skeleton=result.get_editor_property('skeleton')
    require(generated_skeleton,'Importer did not create/bind skeleton: '+name)
    save(generated_skeleton)
    save(result)
    if result.get_name()!=name:
        require(LIB.rename_asset(result.get_path_name(),DEST+'/'+name),'Rename import failed')
    return load(DEST+'/'+name)

def create_asset(name,cls,factory,path=DEST):
    return load(path+'/'+name) or TOOLS.create_asset(name,path,cls,factory)

def make_material(name,color,roughness,metallic):
    existing=load(DEST+'/'+name)
    if existing:
        existing.set_editor_property('used_with_skeletal_mesh',True)
        save(existing)
        return existing
    mat=TOOLS.create_asset(name,DEST,unreal.Material,unreal.MaterialFactoryNew())
    mat.set_editor_property('used_with_skeletal_mesh',True)
    edit=unreal.MaterialEditingLibrary
    base=edit.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector,-300,-120)
    base.constant=unreal.LinearColor(*color,1)
    edit.connect_material_property(base,'',unreal.MaterialProperty.MP_BASE_COLOR)
    for value,prop,y in [(roughness,unreal.MaterialProperty.MP_ROUGHNESS,40),(metallic,unreal.MaterialProperty.MP_METALLIC,150)]:
        scalar=edit.create_material_expression(mat,unreal.MaterialExpressionConstant,-300,y)
        scalar.r=value; edit.connect_material_property(scalar,'',prop)
    edit.recompile_material(mat); save(mat)
    return mat

def verify():
    mesh=load(DEST+'/SK_Whip'); anim=load(DEST+'/AN_Whip_Attack'); idle=load(DEST+'/AN_Whip_Idle')
    bp=load(DEST+'/BP_Whip'); item=load(ITEM); table=load(CATALOG)
    require(all([mesh,anim,idle,bp,item,table]),'Missing whip delivery asset')
    require(mesh.skeleton,'Missing saved skeleton')
    require(anim.get_editor_property('skeleton')==mesh.skeleton and idle.get_editor_property('skeleton')==mesh.skeleton,'Skeleton mismatch')
    require(1.5<anim.get_play_length()<1.8,'Attack length invalid')
    cdo=unreal.get_default_object(bp.generated_class())
    require(cdo.get_editor_property('attack_animation')==anim,'Actor animation not linked')
    require(cdo.get_editor_property('skeletal_mesh').skeletal_mesh_asset==mesh,'Actor mesh not linked')
    require(cdo.get_editor_property('item_definition')==item,'Pickup source missing')
    require(cdo.get_editor_property('impulse_newton_seconds')==18.0,'Whip horizontal impulse not migrated')
    require(cdo.get_editor_property('upward_impulse_newton_seconds')==12.0,'Whip upward impulse not migrated')
    for slot in mesh.get_editor_property('materials'):
        require(slot.material_interface.get_editor_property('used_with_skeletal_mesh'),'Material lacks skeletal usage')
    fragment=item.get_editor_property('fragments')[0]
    require(isinstance(fragment,unreal.CatWhipUseFragment),'Missing source-bound whip ability')
    require(fragment.get_editor_property('ability_class')==unreal.CatGA_UseWhip.static_class(),'Wrong ability')
    require(fragment.get_editor_property('consume_count')==0,'Whip must remain reusable')
    rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    matches=[r for r in rows if r['ItemId']==ITEM_ID]
    require(len(matches)==1 and matches[0]['ItemDefinition'].startswith(ITEM+'.'),'Catalog identity mismatch')
    registry=unreal.AssetRegistryHelpers.get_asset_registry()
    deps=registry.get_dependencies(ITEM,unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,include_soft_package_references=True))
    report={'item_id':ITEM_ID,'catalog_rows':len(rows),'mesh':mesh.get_path_name(),'skeleton':mesh.skeleton.get_path_name(),
            'attack_seconds':anim.get_play_length(),'actor':bp.get_path_name(),'definition_dependencies':[str(d) for d in deps],
            'verified':True,'ue_runtime_test':'separate automation required'}
    evidence=PROJECT/'Saved/Art/Whip'; evidence.mkdir(parents=True,exist_ok=True)
    (evidence/'ue_assets.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.log('Event=whip_assets_verified '+json.dumps(report))

if os.environ.get('WHIP_VERIFY_ONLY')!='1':
    mesh=import_fbx('SK_Whip','SK_Whip.fbx')
    attack=import_fbx('AN_Whip_Attack','AN_Whip_Attack.fbx',mesh.skeleton)
    idle=import_fbx('AN_Whip_Idle','AN_Whip_Idle.fbx',mesh.skeleton)
    palettes={
        'Walnut':((.12,.054,.024),.43,0), 'DarkLeather':((.057,.024,.013),.67,0),
        'AgedBrass':((.32,.22,.09),.39,.72), 'Seam':((.25,.13,.058),.7,0),
        'ChestnutLeather':((.17,.055,.021),.56,0)}
    materials=list(mesh.get_editor_property('materials'))
    for slot in materials:
        name=str(slot.get_editor_property('material_slot_name'))
        key=next((k for k in palettes if k in name),None)
        require(key,'Unknown source material '+name)
        color,rough,metal=palettes[key]
        slot.material_interface=make_material('M_Whip_'+key,color,rough,metal)
    mesh.set_editor_property('materials',materials); save(mesh)
    factory=unreal.BlueprintFactory(); factory.set_editor_property('parent_class',unreal.CatWhipActor)
    bp=create_asset('BP_Whip',unreal.Blueprint,factory)
    cdo=unreal.get_default_object(bp.generated_class())
    cdo.get_editor_property('skeletal_mesh').set_skeletal_mesh_asset(mesh)
    cdo.set_editor_property('attack_animation',attack)
    cdo.set_editor_property('impulse_newton_seconds',18.0)
    cdo.set_editor_property('upward_impulse_newton_seconds',12.0)
    item_factory=unreal.DataAssetFactory(); item_factory.set_editor_property('data_asset_class',unreal.CatInventoryItemDefinition)
    item=create_asset('Item_Whip',unreal.CatInventoryItemDefinition,item_factory,'/Game/Catfishing/Data/Items')
    item.set_editor_property('item_id',ITEM_ID)
    item.set_editor_property('inventory_display_name','皮鞭')
    item.set_editor_property('inventory_description','朝队友挥一下。抽中的方向会影响它的站位；可重复使用。')
    item.set_editor_property('world_actor_class',bp.generated_class())
    actions=[]
    for tag,label in [('Inventory.Action.Use','挥鞭'),('Inventory.Action.Drop','丢弃')]:
        # Match existing native tags rather than inventing a parallel input route.
        row=unreal.CatInventoryActionDefinition()
        value=unreal.GameplayTag()
        value.import_text('(TagName="'+tag+'")')
        row.set_editor_property('action',value)
        row.set_editor_property('label',label); actions.append(row)
    item.set_editor_property('inventory_actions',actions)
    fragment=unreal.new_object(unreal.CatWhipUseFragment,outer=item,name='WhipUse') if not item.get_editor_property('fragments') else item.get_editor_property('fragments')[0]
    require(isinstance(fragment,unreal.CatWhipUseFragment),'Unexpected existing item fragments')
    fragment.set_editor_property('ability_class',unreal.CatGA_UseWhip.static_class())
    fragment.set_editor_property('consume_count',0); fragment.set_editor_property('commit_delay',0)
    fragment.set_editor_property('swing_actor_class',bp.generated_class())
    montage=load('/Game/Animalia/Cat/AM_Attack_Agressive_Legs_01-IP_Montage')
    require(montage,'Existing cast montage missing')
    fragment.set_editor_property('montage',montage)
    item.set_editor_property('fragments',[fragment])
    cdo.set_editor_property('item_definition',item)
    icon=load(DEST+'/T_Whip_Icon')
    if not icon:
        task=unreal.AssetImportTask(); task.filename=str(SOURCE/'Previews/Whip_Hero.png')
        task.destination_path=DEST; task.destination_name='T_Whip_Icon'; task.automated=True; task.save=True
        TOOLS.import_asset_tasks([task]); icon=load(DEST+'/T_Whip_Icon')
    if icon:
        icon.set_editor_property('max_texture_size',256); save(icon)
        item.set_editor_property('inventory_thumbnail',icon)
    save(item); save(bp)
    table=load(CATALOG)
    rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    existing=next((r for r in rows if r['ItemId']==ITEM_ID),None)
    path=ITEM+'.Item_Whip'
    require(existing is None or existing['ItemDefinition']==path,'Item '+str(ITEM_ID)+' already belongs to another definition')
    before=[dict(r) for r in rows if r['ItemId']!=ITEM_ID]
    if existing is None: rows.append({'Name':str(ITEM_ID),'ItemId':ITEM_ID,'ItemDefinition':path})
    require(unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(rows)),'Catalog update failed')
    after=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    require([r for r in after if r['ItemId']!=ITEM_ID]==before,'Existing catalog rows changed')
    save(table)
verify()
