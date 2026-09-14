"""由 style_frontend_room.py 调用：房间信息、邀请与设置弹窗，不单独保存资产。"""
from pathlib import Path
import runpy
import unreal

base=runpy.run_path(str(Path(__file__).with_name('style_frontend_save.py')))
color,slate,widget,font,button_style,ensure,attach,text=(base[k] for k in
    ('color','slate','widget','font','button_style','ensure','attach','text'))

def fill(slot):
    slot.set_size(unreal.SlateChildSize(size_rule=unreal.SlateSizeRule.FILL))
    return slot

def btn(bp,parent,name,label,primary=False):
    b=ensure(bp,name,unreal.Button)
    attach(b,text(bp,name+'Label',label,14,color(.82,.91,.86)))
    button_style(b,primary)
    compact_button(b)
    attach(parent,b)
    return b

def compact_button(button):
    style=button.get_editor_property('widget_style')
    style.set_editor_property('normal_padding',unreal.Margin(14,8))
    style.set_editor_property('pressed_padding',unreal.Margin(14,8))
    button.set_style(style)

def space(bp,parent,name):
    return fill(attach(parent,ensure(bp,name,unreal.Spacer)))

def canvas_slot(parent,child,anchor,offset,alignment=(0,0),z=0):
    slot=attach(parent,child)
    slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(*anchor),maximum=unreal.Vector2D(*anchor)))
    slot.set_offsets(unreal.Margin(*offset));slot.set_alignment(unreal.Vector2D(*alignment));slot.set_z_order(z)
    return slot

def copy_icon(bp,button):
    button.clear_children()
    bounds=ensure(bp,'RoomCopyIconBounds',unreal.SizeBox)
    bounds.set_width_override(22);bounds.set_height_override(22)
    icon=ensure(bp,'RoomCopyIcon',unreal.CanvasPanel);attach(bounds,icon);attach(button,bounds)
    # 双纸张轮廓，避免依赖中文字体是否包含复制符号。
    for prefix,x,y in [('Back',2,2),('Front',7,7)]:
        outer=ensure(bp,'RoomCopy'+prefix,unreal.Border);outer.set_brush_color(color(.54,.80,.73));outer.set_padding(unreal.Margin(1.4))
        inner=ensure(bp,'RoomCopy'+prefix+'Inner',unreal.Border);inner.set_brush_color(color(.015,.055,.048));attach(outer,inner)
        canvas_slot(icon,outer,(0,0),(x,y,12,13))
    style=button.get_editor_property('widget_style')
    style.set_editor_property('normal_padding',unreal.Margin(6));style.set_editor_property('pressed_padding',unreal.Margin(6))
    button.set_style(style);button.set_tool_tip_text('复制房间 ID')

def frame(bp,layer,name,title,width,height,right=False):
    bounds=ensure(bp,name,unreal.SizeBox);bounds.set_width_override(width);bounds.set_height_override(height)
    canvas_slot(layer,bounds,(1,.5) if right else (.5,.5),(-36,0,width,height) if right else (0,0,width,height),(1,.5) if right else (.5,.5),2)
    outline=ensure(bp,name+'Outline',unreal.Border);outline.set_brush_color(color(.10,.30,.25,.98));outline.set_padding(unreal.Margin(1))
    surface=ensure(bp,name+'Surface',unreal.Border);surface.set_brush_color(color(.002,.010,.008,.98));surface.set_padding(unreal.Margin(24,20,24,20))
    attach(bounds,outline);attach(outline,surface)
    column=ensure(bp,name+'Column',unreal.VerticalBox);column.clear_children();attach(surface,column)
    header=ensure(bp,name+'Header',unreal.HorizontalBox);header.clear_children();attach(column,header).set_padding(unreal.Margin(0,0,0,16))
    fill(attach(header,text(bp,name+'Title',title,20,color(.87,.91,.80))))
    close=btn(bp,header,'CloseRoomInviteButton' if name=='RoomInviteDialog' else 'CloseRoomSettingsButton','×')
    style=close.get_editor_property('widget_style');style.set_editor_property('normal_padding',unreal.Margin(10,2));style.set_editor_property('pressed_padding',unreal.Margin(10,2));close.set_style(style)
    close.set_tool_tip_text('关闭（Esc）')
    return column

def style(page,friend):
    shade=widget(page,'RoomRootShade');canvas=shade.get_parent()
    shade.set_padding(unreal.Margin(36,24,36,24))
    root=widget(page,'RoomRoot');root.clear_children()
    header=ensure(page,'RoomHeader',unreal.HorizontalBox);header.clear_children();attach(root,header).set_padding(unreal.Margin(0,0,0,22))
    title=widget(page,'RoomTitleText');title.set_text('房间');font(title,17,color(.73,.82,.76));attach(header,title)
    space(page,header,'RoomHeaderSpace')
    btn(page,header,'OpenRoomSettingsButton','房间设置')
    lists=widget(page,'RoomLists');lists.clear_children();fill(attach(root,lists))
    summary_bounds=widget(page,'RoomFriendsBounds');summary_bounds.clear_children();summary_bounds.set_width_override(280)
    attach(lists,summary_bounds).set_padding(unreal.Margin(0,0,26,0))
    summary_surface=ensure(page,'RoomSummarySurface',unreal.Border);summary_surface.set_brush_color(color(.005,.022,.018,.48));summary_surface.set_padding(unreal.Margin(18,18,18,18));attach(summary_bounds,summary_surface)
    summary=ensure(page,'RoomSummaryColumn',unreal.VerticalBox);summary.clear_children();attach(summary_surface,summary)
    name=text(page,'RoomNameText','房间',27,color(.92,.85,.65));name.set_auto_wrap_text(True);attach(summary,name).set_padding(unreal.Margin(0,0,0,10))
    attach(summary,text(page,'RoomIdLabel','房间 ID',12,color(.44,.63,.56)))
    id_row=ensure(page,'RoomIdRow',unreal.HorizontalBox);id_row.clear_children();attach(summary,id_row).set_padding(unreal.Margin(0,2,0,10))
    ident=widget(page,'RoomInviteCodeText');font(ident,13,color(.69,.83,.77));ident.set_auto_wrap_text(False);fill(attach(id_row,ident)).set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    copy=widget(page,'CopyInviteCodeButton');button_style(copy);attach(id_row,copy);copy_icon(page,copy)
    attach(summary,text(page,'RoomMemberCountText','0 / 0 位伙伴',15))
    access=widget(page,'RoomAccessPolicyText');font(access,14,color(.50,.71,.63));attach(summary,access).set_padding(unreal.Margin(0,5,0,18))
    attach(summary,text(page,'RoomGatherTitle','围炉等你',20,color(.86,.83,.65))).set_padding(unreal.Margin(0,22,0,10))
    hint=text(page,'RoomGatherHint','邀请朋友一起出发。\n队员准备后，由房主开始游戏。',13);hint.set_auto_wrap_text(True);attach(summary,hint).set_padding(unreal.Margin(0,0,0,22))
    btn(page,summary,'OpenRoomInviteButton','邀请好友',True)
    space(page,summary,'RoomSummarySpace')
    players=widget(page,'PlayersColumnSurface');players.set_padding(unreal.Margin(8,20,8,0));players.set_brush_color(color(0,0,0,0));fill(attach(lists,players))
    widget(page,'PlayersTitleText').set_text('营地伙伴')
    result=widget(page,'RoomResultTextBlock');font(result,14,color(.61,.80,.70));attach(root,result).set_padding(unreal.Margin(0,8,0,8))
    actions=widget(page,'RoomActions');attach(root,actions)
    layer=ensure(page,'RoomDialogLayer',unreal.CanvasPanel)
    slot=attach(canvas,layer);slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(0,0),maximum=unreal.Vector2D(1,1)));slot.set_offsets(unreal.Margin(0));slot.set_z_order(20)
    backdrop=ensure(page,'RoomDialogBackdrop',unreal.Button);button_style(backdrop)
    style=backdrop.get_editor_property('widget_style')
    for state in ('normal','hovered','pressed'):
        brush=style.get_editor_property(state);brush.set_editor_property('tint_color',slate(color(0,0,0,.38)));style.set_editor_property(state,brush)
    backdrop.set_style(style)
    slot=attach(layer,backdrop);slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(0,0),maximum=unreal.Vector2D(1,1)));slot.set_offsets(unreal.Margin(0))
    invite=frame(page,layer,'RoomInviteDialog','邀请好友',580,640)
    tabs_header=ensure(page,'RoomInviteTabHeader',unreal.HorizontalBox);tabs_header.clear_children();attach(invite,tabs_header).set_padding(unreal.Margin(0,0,0,14))
    fill(btn(page,tabs_header,'RoomInviteFriendsTabButton','Steam 好友',True).get_editor_property('slot')).set_padding(unreal.Margin(0,0,8,0))
    fill(btn(page,tabs_header,'RoomInviteIdTabButton','房间 ID').get_editor_property('slot'))
    tabs=ensure(page,'RoomInviteTabs',unreal.WidgetSwitcher);tabs.clear_children();fill(attach(invite,tabs))
    friends_surface=widget(page,'FriendsColumnSurface');friends_surface.set_padding(unreal.Margin(0));friends_surface.set_brush_color(color(0,0,0,0));attach(tabs,friends_surface)
    friends=widget(page,'FriendsColumn');friends.clear_children()
    old_title=widget(page,'FriendsTitleText');old_title.set_visibility(unreal.SlateVisibility.COLLAPSED);attach(friends,old_title)
    search=widget(page,'FriendSearchTextBox');search.set_hint_text('搜索好友…');attach(friends,search).set_padding(unreal.Margin(0,0,0,12))
    search_style=search.get_editor_property('widget_style')
    search_text=search_style.get_editor_property('text_style');search_font=search_text.get_editor_property('font');search_font.set_editor_property('size',14)
    search_text.set_editor_property('font',search_font);search_style.set_editor_property('text_style',search_text);search_style.set_editor_property('padding',unreal.Margin(12,7))
    search.set_editor_property('widget_style',search_style)
    rows=widget(page,'FriendsScrollBox');fill(attach(friends,rows));rows.set_scrollbar_thickness(unreal.Vector2D(4,4))
    refresh=widget(page,'RefreshFriendsButton');attach(friends,refresh).set_padding(unreal.Margin(0,12,0,0))
    compact_button(refresh);font(widget(page,'RefreshFriendsButtonLabel'),14,color(.82,.91,.86))
    id_panel=ensure(page,'RoomInviteIdPanel',unreal.VerticalBox);id_panel.clear_children();attach(tabs,id_panel)
    space(page,id_panel,'RoomIdTopSpace')
    label=text(page,'RoomIdShareHint','把房间 ID 发给朋友',18);label.set_editor_property('justification',unreal.TextJustify.CENTER);attach(id_panel,label).set_padding(unreal.Margin(0,0,0,22))
    id_value=text(page,'RoomModalIdText','等待房间 ID',26,color(.86,.88,.70));id_value.set_editor_property('justification',unreal.TextJustify.CENTER);attach(id_panel,id_value).set_padding(unreal.Margin(0,0,0,20))
    btn(page,id_panel,'CopyModalRoomIdButton','复制房间 ID',True)
    id_hint=text(page,'RoomIdPermissionHint','私密房间仅接受邀请。\n通过房间 ID 加入时，仍需满足房间权限和密码要求。',13);id_hint.set_auto_wrap_text(True);attach(id_panel,id_hint).set_padding(unreal.Margin(0,20,0,0));space(page,id_panel,'RoomIdBottomSpace')
    feedback=text(page,'RoomInviteFeedbackText','',13,color(.58,.80,.70));feedback.set_auto_wrap_text(True);attach(invite,feedback).set_padding(unreal.Margin(0,10,0,0))
    attach(invite,text(page,'RoomInviteFooter','已邀请的好友可通过 Steam 邀请加入房间。',12)).set_padding(unreal.Margin(0,8,0,0))
    settings=frame(page,layer,'RoomSettingsDialog','房间设置',460,610,True)
    for label,name,kind in [('房间名称','RoomNameInput',unreal.EditableTextBox),('人数上限','RoomCapacityInput',unreal.ComboBoxString),('加入权限','RoomAccessInput',unreal.ComboBoxString),('房间密码','RoomPasswordInput',unreal.EditableTextBox)]:
        attach(settings,text(page,name+'Label',label,14,color(.64,.78,.70))).set_padding(unreal.Margin(0,5,0,5))
        control=ensure(page,name,kind);attach(settings,control).set_padding(unreal.Margin(0,0,0,8))
        if kind==unreal.ComboBoxString:
            options=['1','2','3','4'] if name=='RoomCapacityInput' else ['公开可加入','仅好友可加入','私密 · 仅邀请']
            control.set_editor_property('default_options',options)
            control.clear_options()
            for option in options: control.add_option(option)
            control.set_selected_index(0)
            source=unreal.load_asset('/Game/UI/Frontend/WBP_CatFrontendSettings')
            template=widget(source,'FullscreenModeComboBox')
            for prop in ('widget_style','item_style','font','foreground_color','content_padding'): control.set_editor_property(prop,template.get_editor_property(prop))
        else:
            control.set_editor_property('widget_style',search.get_editor_property('widget_style'))
            control.set_hint_text('输入房间名称' if name=='RoomNameInput' else '留空不修改密码')
            if name=='RoomPasswordInput': control.set_is_password(True)
    clear=ensure(page,'RoomClearPasswordCheckBox',unreal.CheckBox);attach(clear,text(page,'RoomClearPasswordLabel','移除现有密码',13));attach(settings,clear)
    rules=text(page,'RoomPasswordRulesText','Steam 邀请免密码；房间 ID 加入需要密码。',12);rules.set_auto_wrap_text(True);attach(settings,rules).set_padding(unreal.Margin(0,10,0,0))
    space(page,settings,'RoomSettingsSpace')
    feedback=text(page,'RoomSettingsFeedbackText','',13,color(.62,.80,.72));feedback.set_auto_wrap_text(True);attach(settings,feedback).set_padding(unreal.Margin(0,10,0,12))
    footer=ensure(page,'RoomSettingsFooter',unreal.HorizontalBox);footer.clear_children();attach(settings,footer)
    fill(btn(page,footer,'CopySettingsRoomIdButton','复制房间 ID').get_editor_property('slot')).set_padding(unreal.Margin(0,0,10,0))
    fill(btn(page,footer,'SaveRoomSettingsButton','保存设置',True).get_editor_property('slot'))
    layer.set_visibility(unreal.SlateVisibility.COLLAPSED)
    # 好友行继续消费原句柄，扩大内容区后让姓名获得剩余宽度。
    surface=widget(friend,'RoomFriendRowRootBackground');surface.set_padding(unreal.Margin(16,10,16,10));surface.set_brush_color(color(.019,.065,.054,.92))
    bounds=surface.get_parent();bounds.clear_width_override();bounds.clear_height_override();bounds.set_min_desired_height(64)
    column=widget(friend,'FriendTextColumn');fill(column.get_editor_property('slot'))
    font(widget(friend,'FriendNameText'),15,color(.85,.90,.79));font(widget(friend,'FriendStatusText'),12,color(.46,.72,.60))
    widget(friend,'FriendNameText').set_text_overflow_policy(unreal.TextOverflowPolicy.ELLIPSIS)
    button_style(widget(friend,'InviteFriendButton'))
    compact_button(widget(friend,'InviteFriendButton'));font(widget(friend,'InviteFriendButtonLabel'),14,color(.82,.91,.86))
