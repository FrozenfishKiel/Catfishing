"""局内派对菜单、真实组队页及共用设置样式。"""
from pathlib import Path
import hashlib
import runpy
import shutil
import unreal

base=runpy.run_path(str(Path(__file__).with_name('style_frontend_save.py')))
color,slate,widget,font,button_style,ensure,attach,text=(base[k] for k in
    ('color','slate','widget','font','button_style','ensure','attach','text'))
PAGE='/Game/UI/Save/WBP_CatLakeMainMenu'
MEMBER='/Game/UI/Party/WBP_CatPartyMemberRow'

def size_rule(slot,fill=False):
    size=unreal.SlateChildSize()
    size.set_editor_property('size_rule',unreal.SlateSizeRule.FILL if fill else unreal.SlateSizeRule.AUTOMATIC)
    slot.set_size(size)
    return slot

def button(bp,parent,name,label,primary=False):
    b=ensure(bp,name,unreal.Button)
    attach(b,text(bp,name+'Label',label,17,color(.87,.89,.82)))
    button_style(b,primary)
    attach(parent,b).set_padding(unreal.Margin(0,5,0,5))
    return b

def main():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    dirty={p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert not dirty.intersection((PAGE,MEMBER)), 'Unsaved target edits'
    project=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    backup=project/'Saved/Automation/LakeParty/Backups'
    backup.mkdir(parents=True,exist_ok=True)
    for p in (PAGE,MEMBER):
        src=project/'Content'/(p.removeprefix('/Game/')+'.uasset')
        if src.exists():
            dst=backup/(src.stem+'-'+hashlib.sha256(src.read_bytes()).hexdigest()+'.uasset')
            if not dst.exists(): shutil.copy2(src,dst)
    member=unreal.load_asset(MEMBER)
    if not member: member=unreal.EditorAssetLibrary.duplicate_asset('/Game/UI/Frontend/WBP_CatRoomPlayerSlot',MEMBER)
    surface=widget(member,'RoomPlayerSlotRootBackground')
    surface.get_parent().set_width_override(450)
    surface.get_parent().set_height_override(70)
    surface.set_brush_color(color(.025,.085,.068,.9))
    surface.set_padding(unreal.Margin(18,12,18,12))
    for n in ('RoomCharacterBounds','ReadyMarkBounds'):
        widget(member,n).set_visibility(unreal.SlateVisibility.COLLAPSED)
    row=widget(member,'RoomPlayerSlotRoot')
    names=widget(member,'PlayerTextColumn')
    attach(row,names)
    size_rule(names.get_editor_property('slot'),True)
    attach(row,widget(member,'PlayerSlotStateText')).set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    widget(member,'RoomSeatContent').set_visibility(unreal.SlateVisibility.COLLAPSED)
    # 空位文案独占一行，不能保留两行空姓名的布局高度。
    widget(member,'PlayerNameText').set_auto_wrap_text(False)
    widget(member,'PlayerRoleText').set_auto_wrap_text(False)
    for name in ('PlayerNameText','PlayerRoleText'):
        slot=widget(member,name).get_editor_property('slot')
        slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_LEFT)
        slot.set_padding(unreal.Margin(0,0,0,0))
    font(widget(member,'PlayerNameText'),17,color(.92,.86,.63))
    font(widget(member,'PlayerRoleText'),12,color(.64,.77,.70))
    font(widget(member,'PlayerSlotStateText'),14,color(.60,.73,.66))
    defaults=unreal.get_default_object(member.generated_class())
    defaults.set_editor_property('preview_character_class',None)
    defaults.set_editor_property('preview_animation',None)
    defaults.set_editor_property('preview_material',None)
    assert unreal.CatFrontendWidgetAuthoringLibrary.compile_styled_frontend_widget(member)
    unreal.EditorAssetLibrary.save_loaded_asset(member)
    bp=unreal.load_asset(PAGE)
    controls=runpy.run_path(str(Path(__file__).with_name('style_frontend_pages.py')))['controls']
    for o in controls(bp):
        name=o.get_name()
        if isinstance(o,unreal.TextBlock):
            font(o,30 if name.endswith('TitleText') else 16,color(.92,.79,.48) if name.endswith('TitleText') else color(.74,.83,.76))
        elif isinstance(o,unreal.Button): button_style(o,name in ('CloseButton','ApplySettingsButton'))
        elif isinstance(o,unreal.SizeBox) and name.endswith('RowBounds'):
            o.clear_height_override(); o.set_min_desired_height(64)
        elif isinstance(o,unreal.Slider):
            o.set_editor_property('slider_bar_color',color(.12,.26,.21))
            o.set_editor_property('slider_handle_color',color(.80,.74,.49))
        elif isinstance(o,unreal.ComboBoxString):
            face=o.get_editor_property('font'); face.set_editor_property('size',17); o.set_editor_property('font',face)
            o.set_editor_property('content_padding',unreal.Margin(8,4,8,4))
    for n in ('LakeMainMenuSurface','LakeSettingsSurface'):
        widget(bp,n).set_brush_color(color(.008,.028,.024,.96))
        widget(bp,n).set_padding(unreal.Margin(28,22,28,22))
    widget(bp,'LakeMainMenuBounds').set_width_override(460)
    widget(bp,'LakeMainMenuTitleText').set_text('派对菜单')
    column=widget(bp,'LakeMainMenuColumn')
    subtitle=text(bp,'LakeMenuSubtitle','游戏仍在进行 · 与朋友一起探索',14)
    party=button(bp,column,'PartyButton','组队管理')
    pause=button(bp,column,'PauseRequestButton','申请暂停')
    pause.set_tool_tip_text('暂未开放')
    collection=button(bp,column,'PartyCollectionButton','钓鱼图鉴 · 暂未开放')
    collection.set_is_enabled(False)
    ordered=['LakeMainMenuTitleText','LakeMenuSubtitle','CloseButton','SettingsButton','PartyButton','PauseRequestButton',
             'PartyCollectionButton','SaveButton','ReturnToMainMenuButton','ExitGameButton','StatusTextBlock']
    column.clear_children()
    for n in ordered:
        o=widget(bp,n); attach(column,o).set_padding(unreal.Margin(0,4,0,4))
    widget(bp,'CloseButtonLabel').set_text('返回游戏')
    widget(bp,'SettingsButtonLabel').set_text('派对设置')
    widget(bp,'ReturnToMainMenuButtonLabel').set_text('离开房间')
    widget(bp,'StatusTextBlock').set_auto_wrap_text(True)
    # 新组队页属于现有 Switcher，沿用同一个模态输入生命周期。
    panel=ensure(bp,'LakePartyPanel',unreal.Overlay)
    attach(widget(bp,'LakeMainMenuPageSwitcher'),panel)
    scale=ensure(bp,'LakePartyScale',unreal.ScaleBox)
    scale.set_stretch(unreal.Stretch.SCALE_TO_FIT)
    scale.set_stretch_direction(unreal.StretchDirection.DOWN_ONLY)
    scale_slot=attach(panel,scale)
    scale_slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    scale_slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
    bounds=ensure(bp,'LakePartyBounds',unreal.SizeBox)
    bounds.set_width_override(1100); bounds.set_height_override(720)
    attach(scale,bounds)
    bg=ensure(bp,'LakePartySurface',unreal.Border)
    bg.set_brush_color(color(.008,.028,.024,.97)); bg.set_padding(unreal.Margin(32,24,32,24))
    attach(bounds,bg)
    root=ensure(bp,'LakePartyColumn',unreal.VerticalBox); attach(bg,root)
    attach(root,text(bp,'PartyTitle','组队管理',30,color(.92,.79,.48)))
    attach(root,text(bp,'PartySubtitle','通过 Steam 好友邀请伙伴加入当前房间',15)).set_padding(unreal.Margin(0,6,0,20))
    columns=ensure(bp,'PartyColumns',unreal.HorizontalBox)
    size_rule(attach(root,columns),True)
    left=ensure(bp,'PartyTeamColumn',unreal.VerticalBox)
    right=ensure(bp,'PartyFriendsColumn',unreal.VerticalBox)
    size_rule(attach(columns,left),True).set_padding(unreal.Margin(0,0,22,0))
    size_rule(attach(columns,right),True).set_padding(unreal.Margin(22,0,0,0))
    attach(left,text(bp,'PartyCountText','正在确认队伍信息',20))
    members=ensure(bp,'PartyMembersScrollBox',unreal.ScrollBox)
    size_rule(attach(left,members),True).set_padding(unreal.Margin(0,12,0,12))
    attach(left,text(bp,'PartyAccessText','加入权限：待确认',15))
    button(bp,left,'PartyCopyLinkButton','复制房间邀请链接')
    attach(right,text(bp,'PartyFriendsTitle','Steam 好友',20))
    search=ensure(bp,'PartySearchTextBox',unreal.EditableTextBox)
    search.set_hint_text('搜索好友')
    style=search.get_editor_property('widget_style')
    ts=style.get_editor_property('text_style')
    face=widget(bp,'PartySubtitle').get_editor_property('font'); face.set_editor_property('size',16)
    ts.set_editor_property('font',face); style.set_editor_property('text_style',ts)
    style.set_editor_property('padding',unreal.Margin(12,10,12,10))
    tint_brush=runpy.run_path(str(Path(__file__).with_name('style_frontend_pages.py')))['brush_tint']
    for key in ('background_image_normal','background_image_hovered','background_image_focused'):
        style.set_editor_property(key,tint_brush(style.get_editor_property(key),color(.025,.09,.075,.95)))
    style.set_editor_property('foreground_color',slate(color(.78,.86,.79)))
    search.set_editor_property('widget_style',style)
    attach(right,search).set_padding(unreal.Margin(0,12,0,12))
    friends=ensure(bp,'PartyFriendsScrollBox',unreal.ScrollBox)
    for scroll in (members,friends):
        scroll.set_scrollbar_thickness(unreal.Vector2D(4,4))
        bar=scroll.get_editor_property('widget_bar_style')
        for key in ('normal_thumb_image','hovered_thumb_image','dragged_thumb_image'):
            bar.set_editor_property(key,tint_brush(bar.get_editor_property(key),color(.23,.40,.32)))
        scroll.set_editor_property('widget_bar_style',bar)
    size_rule(attach(right,friends),True)
    button(bp,right,'PartyRefreshButton','刷新好友',True)
    status=text(bp,'PartyStatusText','',14)
    status.set_auto_wrap_text(True)
    attach(root,status).set_padding(unreal.Margin(0,10,0,8))
    button(bp,root,'PartyBackButton','返回派对菜单')
    panel.set_visibility(unreal.SlateVisibility.COLLAPSED)
    defaults=unreal.get_default_object(bp.generated_class())
    defaults.set_editor_property('party_friend_row_class',unreal.load_asset('/Game/UI/Frontend/WBP_CatRoomFriendRow').generated_class())
    defaults.set_editor_property('party_member_row_class',member.generated_class())
    assert unreal.CatFrontendWidgetAuthoringLibrary.compile_styled_frontend_widget(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=False)
    unreal.log('Event=lake_party_style_authored')

if __name__=='__main__': main()
