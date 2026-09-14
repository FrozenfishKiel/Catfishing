"""房间角色席位及准备按钮；保留原控件身份和 Online 数据入口。"""
from pathlib import Path
import hashlib
import runpy
import shutil
import unreal

shared = runpy.run_path(str(Path(__file__).with_name('style_frontend_save.py')))
color, slate, widget, font, button_style, ensure, attach, text = (shared[k] for k in
    ('color','slate','widget','font','button_style','ensure','attach','text'))
PAGE = '/Game/UI/Frontend/WBP_CatFrontendRoom'
ROW = '/Game/UI/Frontend/WBP_CatRoomPlayerSlot'
MATERIAL = '/Game/UI/Frontend/M_UI_RoomCharacterPreview'
FRIEND = '/Game/UI/Frontend/WBP_CatRoomFriendRow'

def preview_material():
    mat = unreal.load_asset(MATERIAL)
    lib = unreal.MaterialEditingLibrary
    if mat:
        opacity=lib.get_material_property_input_node(mat,unreal.MaterialProperty.MP_OPACITY)
        parameters=[str(p) for p in lib.get_texture_parameter_names(mat)]
        if (opacity and any(lib.get_inputs_for_material_expression(mat,opacity))
                and 'CharacterMaskTexture' in parameters
                and mat.get_editor_property('blend_mode')==unreal.BlendMode.BLEND_ALPHA_COMPOSITE):
            return mat
    if not mat:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            MATERIAL.rsplit('/',1)[1], MATERIAL.rsplit('/',1)[0], unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI)
    # Final color is already composited over black; preserve its antialiased edge coverage.
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ALPHA_COMPOSITE)
    lib.delete_all_material_expressions(mat)
    tex = lib.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    tex.set_editor_property('parameter_name','CharacterTexture')
    tex.set_editor_property('texture',unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    mask = lib.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -400, 240)
    mask.set_editor_property('parameter_name','CharacterMaskTexture')
    mask.set_editor_property('texture',unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    inverse = lib.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -180, 160)
    assert lib.connect_material_expressions(mask,'A',inverse,'')
    assert lib.connect_material_property(tex,'RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert lib.connect_material_property(inverse,'',unreal.MaterialProperty.MP_OPACITY)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat

def main():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE first'
    dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert not dirty.intersection((PAGE,ROW,MATERIAL,FRIEND)), 'Unsaved target asset edits'
    project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    backup = project/'Saved/Automation/RoomStage/Backups'
    backup.mkdir(parents=True,exist_ok=True)
    for package in (PAGE,ROW,MATERIAL,FRIEND):
        source = project/'Content'/(package.removeprefix('/Game/')+'.uasset')
        if source.exists():
            target = backup/(source.stem+'-'+hashlib.sha256(source.read_bytes()).hexdigest()+'.uasset')
            if not target.exists(): shutil.copy2(source,target)
    page,row = unreal.load_asset(PAGE),unreal.load_asset(ROW)
    page_style=runpy.run_path(str(Path(__file__).with_name('style_frontend_pages.py')))
    for name in ('FriendsColumn','PlayersColumn'):
        page_style['panel'](page,name)
    widget(page,'RoomRootShade').set_brush_color(color(0,0,0,0))
    widget(page,'RoomRootShade').set_padding(unreal.Margin(68,48,68,48))
    for control in page_style['controls'](page):
        if isinstance(control,unreal.TextBlock):
            font(control,21 if 'Title' in control.get_name() else 16,color(.73,.82,.76))
        elif isinstance(control,unreal.Button): button_style(control,control.get_name()=='StartRoomGameButton')
    material = preview_material()
    surface = widget(row,'RoomPlayerSlotRootBackground',unreal.Border)
    bounds = surface.get_parent()
    bounds.set_width_override(190)
    bounds.set_height_override(300)
    surface.set_brush_color(color(0,0,0,0))
    surface.set_padding(unreal.Margin(6,12,6,0))
    content = ensure(row,'RoomSeatContent',unreal.VerticalBox)
    old_row=widget(row,'RoomPlayerSlotRoot')
    old_column=widget(row,'PlayerTextColumn')
    old_row.clear_children()
    attach(old_row,content)
    attach(content,old_column)
    # 既有姓名/角色/状态控件原样迁入席位，HBox 保留为外层容器。
    for name in ('PlayerNameText','PlayerRoleText'):
        control = widget(row,name)
        attach(old_column,control).set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
        font(control,18 if name=='PlayerNameText' else 13,color(.92,.86,.63) if name=='PlayerNameText' else color(.64,.77,.70))
        control.set_editor_property('wrap_text_at',172.0)
        control.set_auto_wrap_text(True)
    mark = ensure(row,'ReadyMark',unreal.CanvasPanel)
    mark_bounds = ensure(row,'ReadyMarkBounds',unreal.SizeBox)
    mark_bounds.set_width_override(24); mark_bounds.set_height_override(24)
    attach(mark_bounds,mark)
    attach(content,mark_bounds).set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
    for name,pos,size,angle in [('ReadyShort',(2,12),(10,4),45),('ReadyLong',(8,10),(18,4),-45)]:
        stroke = ensure(row,name,unreal.Border)
        stroke.set_brush_color(color(.35,.86,.51))
        slot=attach(mark,stroke)
        slot.set_position(unreal.Vector2D(*pos)); slot.set_size(unreal.Vector2D(*size))
        stroke.set_render_transform_angle(angle)
    mark.set_visibility(unreal.SlateVisibility.HIDDEN)
    image_bounds = ensure(row,'RoomCharacterBounds',unreal.SizeBox)
    image_bounds.set_width_override(167); image_bounds.set_height_override(200)
    overlay = ensure(row,'RoomCharacterOverlay',unreal.Overlay)
    attach(image_bounds,overlay)
    attach(content,image_bounds).set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
    image = ensure(row,'CharacterPreviewImage',unreal.Image)
    image_slot=attach(overlay,image)
    image_slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    image_slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
    empty=text(row,'EmptySeatMark','+',48,color(.55,.72,.65,.65))
    slot=attach(overlay,empty)
    slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
    slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    state=widget(row,'PlayerSlotStateText')
    attach(content,state).set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
    font(state,14,color(.60,.73,.66))
    # 原有布局容器继续用于包裹席位与姓名，保留现有资产 GUID 和绑定身份。
    defaults=unreal.get_default_object(row.generated_class())
    defaults.set_editor_property('preview_character_class',unreal.load_object(None,'/Game/Character/BP_CuteCatCharacter.BP_CuteCatCharacter_C'))
    defaults.set_editor_property('preview_animation',unreal.load_asset('/Game/Characters/CuteCat/Meshes/SK_CuteCat_Anim_Armature_idle_A_0'))
    defaults.set_editor_property('preview_material',material)

    scroll=widget(page,'PlayersScrollBox',unreal.ScrollBox)
    scroll.set_orientation(unreal.Orientation.ORIENT_HORIZONTAL)
    scroll.set_editor_property('scroll_bar_visibility',unreal.SlateVisibility.COLLAPSED)
    ensure(page,'RoomFriendsBounds',unreal.SizeBox)
    widget(page,'PlayersColumnSurface').set_brush_color(color(.005,.022,.018,.32))
    widget(page,'PlayersColumnSurface').set_padding(unreal.Margin(12,20,12,0))
    widget(page,'PlayersTitleText').set_text('营地伙伴')
    font(widget(page,'RoomTitleText'),32,color(.92,.79,.48))
    actions=widget(page,'RoomActions')
    ready=ensure(page,'ReadyRoomButton',unreal.Button)
    attach(ready,text(page,'ReadyRoomButtonLabel','准备',17,color(.86,.88,.81)))
    attach(actions,ready)
    button_style(ready,True)
    hint=text(page,'RoomReadinessHint','等待其他队员准备 · 房主点击开始即视为准备',14,color(.64,.77,.70))
    attach(widget(page,'PlayersColumn'),hint).set_padding(unreal.Margin(0,8,0,12))
    hint.set_auto_wrap_text(True)
    friend=unreal.load_asset(FRIEND)
    runpy.run_path(str(Path(__file__).with_name('style_frontend_room_dialogs.py')))['style'](page,friend)
    for bp in (row,page,friend):
        if not unreal.CatFrontendWidgetAuthoringLibrary.compile_styled_frontend_widget(bp): raise RuntimeError('WBP compile failed: '+bp.get_name())
        if not unreal.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=False): raise RuntimeError('Save failed')
    unreal.log('Event=frontend_room_stage_authored Assets=3 Character=BP_CuteCatCharacter')

if __name__=='__main__': main()
