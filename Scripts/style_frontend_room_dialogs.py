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

def brush(r,g,b,a=.96,radius=6):
    value=unreal.SlateBrush()
    value.set_editor_property('draw_as',unreal.SlateBrushDrawType.ROUNDED_BOX)
    value.set_editor_property('tint_color',slate(color(r,g,b,a)))
    outline=unreal.SlateBrushOutlineSettings()
    outline.set_editor_property('corner_radii',unreal.Vector4(radius,radius,radius,radius))
    outline.set_editor_property('rounding_type',unreal.SlateBrushRoundingType.FIXED_RADIUS)
    outline.set_editor_property('color',slate(color(.10,.37,.31,.9)))
    outline.set_editor_property('width',1.0)
    value.set_editor_property('outline_settings',outline)
    return value

def surface(border,r,g,b):
    border.set_brush(brush(r,g,b));border.set_brush_color(color(1,1,1,1))

def button_style(button,primary=False):
    value=button.get_editor_property('widget_style')
    for state,tint in {'normal':(.025,.17,.14) if primary else (.004,.026,.022),'hovered':(.05,.26,.21),'pressed':(.02,.12,.10),'disabled':(.005,.016,.014)}.items():
        value.set_editor_property(state,brush(*tint))
    button.set_style(value)
    compact_button(button)

def btn(bp,parent,name,label,primary=False):
    b=ensure(bp,name,unreal.Button)
    attach(b,text(bp,name+'Label',label,14,color(.82,.91,.86)))
    button_style(b,primary)
    compact_button(b)
    attach(parent,b)
    return b

def compact_button(button):
    style=button.get_editor_property('widget_style')
    # Python FMargin uses left/top/right/bottom fields, not C++ convenience constructors.
    style.set_editor_property('normal_padding',unreal.Margin(14,8,14,8))
    style.set_editor_property('pressed_padding',unreal.Margin(14,8,14,8))
    button.set_style(style)
    if button.get_content():
        slot=button.get_content().get_editor_property('slot')
        slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
        slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
        if isinstance(button.get_content(),unreal.TextBlock):
            button.get_content().set_editor_property('justification',unreal.TextJustify.CENTER)

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
        outer=ensure(bp,'RoomCopy'+prefix,unreal.Border);outer.set_brush_color(color(.54,.80,.73));outer.set_padding(unreal.Margin(1.4,1.4,1.4,1.4))
        inner=ensure(bp,'RoomCopy'+prefix+'Inner',unreal.Border);inner.set_brush_color(color(.015,.055,.048));attach(outer,inner)
        canvas_slot(icon,outer,(0,0),(x,y,12,13))
    style=button.get_editor_property('widget_style')
    style.set_editor_property('normal_padding',unreal.Margin(6,6,6,6));style.set_editor_property('pressed_padding',unreal.Margin(6,6,6,6))
    button.set_style(style);button.set_tool_tip_text('复制房间 ID')

def frame(bp,layer,name,title,width,height,right=False):
    bounds=ensure(bp,name,unreal.SizeBox);bounds.set_width_override(width);bounds.set_height_override(height)
    canvas_slot(layer,bounds,(1,.5) if right else (.5,.5),(-36,0,width,height) if right else (0,0,width,height),(1,.5) if right else (.5,.5),2)
    outline=ensure(bp,name+'Outline',unreal.Border);surface(outline,.002,.012,.011);outline.set_padding(unreal.Margin(0,0,0,0))
    inner=ensure(bp,name+'Surface',unreal.Border);inner.set_brush_color(color(0,0,0,0));inner.set_padding(unreal.Margin(20,18,20,18))
    attach(bounds,outline);attach(outline,inner)
    column=ensure(bp,name+'Column',unreal.VerticalBox);column.clear_children();attach(inner,column)
    header=ensure(bp,name+'Header',unreal.HorizontalBox);header.clear_children();attach(column,header).set_padding(unreal.Margin(0,0,0,16))
    fill(attach(header,text(bp,name+'Title',title,20,color(.87,.91,.80))))
    close_name={'RoomInviteDialog':'CloseRoomInviteButton','RoomSettingsDialog':'CloseRoomSettingsButton','RoomNoticeDialog':'CloseRoomNoticeButton'}[name]
    close=btn(bp,header,close_name,'×')
    style=close.get_editor_property('widget_style');style.set_editor_property('normal_padding',unreal.Margin(10,2,10,2));style.set_editor_property('pressed_padding',unreal.Margin(10,2,10,2));close.set_style(style)
    close.set_tool_tip_text('关闭（Esc）')
    return column

def style(page,friend,row,texture):
    canvas=widget(page,'RoomRootShade').get_parent()
    runpy.run_path(str(Path(__file__).with_name('style_frontend_room_scene.py')))['style'](page,row,texture,globals())
    layer=ensure(page,'RoomDialogLayer',unreal.CanvasPanel)
    slot=attach(canvas,layer);slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(0,0),maximum=unreal.Vector2D(1,1)));slot.set_offsets(unreal.Margin(0));slot.set_z_order(20)
    backdrop=ensure(page,'RoomDialogBackdrop',unreal.Button)
    style=backdrop.get_editor_property('widget_style')
    for state in ('normal','hovered','pressed'):
        backdrop_brush=unreal.SlateBrush();backdrop_brush.set_editor_property('draw_as',unreal.SlateBrushDrawType.IMAGE);backdrop_brush.set_editor_property('tint_color',slate(color(0,0,0,.48)));style.set_editor_property(state,backdrop_brush)
    backdrop.set_style(style)
    slot=attach(layer,backdrop);slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(0,0),maximum=unreal.Vector2D(1,1)));slot.set_offsets(unreal.Margin(0))
    invite=frame(page,layer,'RoomInviteDialog','邀请好友',500,640)
    tabs_header=ensure(page,'RoomInviteTabHeader',unreal.HorizontalBox);tabs_header.clear_children();attach(invite,tabs_header).set_padding(unreal.Margin(0,0,0,14))
    fill(btn(page,tabs_header,'RoomInviteFriendsTabButton','Steam 好友',True).get_editor_property('slot')).set_padding(unreal.Margin(0,0,8,0))
    fill(btn(page,tabs_header,'RoomInviteIdTabButton','房间 ID').get_editor_property('slot'))
    recent=btn(page,tabs_header,'RoomInviteRecentTabButton','最近玩家');fill(recent.get_editor_property('slot')).set_padding(unreal.Margin(8,0,0,0));recent.set_is_enabled(False);recent.set_tool_tip_text('最近玩家列表尚未接入')
    tabs=ensure(page,'RoomInviteTabs',unreal.WidgetSwitcher);tabs.clear_children();fill(attach(invite,tabs))
    friends_surface=widget(page,'FriendsColumnSurface');friends_surface.set_padding(unreal.Margin(0));friends_surface.set_brush_color(color(0,0,0,0));attach(tabs,friends_surface)
    friends=widget(page,'FriendsColumn');friends.clear_children()
    old_title=widget(page,'FriendsTitleText');old_title.set_visibility(unreal.SlateVisibility.COLLAPSED);attach(friends,old_title)
    search=widget(page,'FriendSearchTextBox');search.set_hint_text('搜索好友…');attach(friends,search).set_padding(unreal.Margin(0,0,0,12))
    search_style=search.get_editor_property('widget_style')
    search_text=search_style.get_editor_property('text_style');search_font=search_text.get_editor_property('font');search_font.set_editor_property('size',14)
    search_text.set_editor_property('font',search_font);search_style.set_editor_property('text_style',search_text);search_style.set_editor_property('padding',unreal.Margin(12,7,12,7))
    search.set_editor_property('widget_style',search_style)
    for key in ('background_image_normal','background_image_hovered','background_image_focused'):
        search_style.set_editor_property(key,brush(.003,.017,.015))
    search.set_editor_property('widget_style',search_style)
    rows=widget(page,'FriendsScrollBox');fill(attach(friends,rows));rows.set_scrollbar_thickness(unreal.Vector2D(4,4))
    refresh=widget(page,'RefreshFriendsButton');attach(friends,refresh).set_padding(unreal.Margin(0,12,0,0))
    button_style(refresh);font(widget(page,'RefreshFriendsButtonLabel'),14,color(.82,.91,.86))
    id_panel=ensure(page,'RoomInviteIdPanel',unreal.VerticalBox);id_panel.clear_children();attach(tabs,id_panel)
    space(page,id_panel,'RoomIdTopSpace')
    label=text(page,'RoomIdShareHint','把房间 ID 发给朋友',18);label.set_editor_property('justification',unreal.TextJustify.CENTER);attach(id_panel,label).set_padding(unreal.Margin(0,0,0,22))
    id_value=text(page,'RoomModalIdText','等待房间 ID',26,color(.86,.88,.70));id_value.set_editor_property('justification',unreal.TextJustify.CENTER);attach(id_panel,id_value).set_padding(unreal.Margin(0,0,0,20))
    btn(page,id_panel,'CopyModalRoomIdButton','复制房间 ID',True)
    id_hint=text(page,'RoomIdPermissionHint','私密房间仅接受邀请。\n通过房间 ID 加入时，仍需满足房间权限和密码要求。',13);id_hint.set_auto_wrap_text(True);attach(id_panel,id_hint).set_padding(unreal.Margin(0,20,0,0));space(page,id_panel,'RoomIdBottomSpace')
    feedback=text(page,'RoomInviteFeedbackText','',13,color(.58,.80,.70));feedback.set_auto_wrap_text(True);feedback.set_visibility(unreal.SlateVisibility.COLLAPSED);attach(invite,feedback).set_padding(unreal.Margin(0,10,0,0))
    btn(page,invite,'CopyRoomLinkButton','复制房间链接').get_editor_property('slot').set_padding(unreal.Margin(0,10,0,0))
    attach(invite,text(page,'RoomInviteFooter','已邀请的好友可通过 Steam 邀请加入房间。',12)).set_padding(unreal.Margin(0,8,0,0))
    settings=frame(page,layer,'RoomSettingsDialog','房间设置',450,570,True)
    for label,name,kind in [('房间名称','RoomNameInput',unreal.EditableTextBox),('人数上限','RoomCapacityInput',unreal.ComboBoxString),('加入权限','RoomAccessInput',unreal.ComboBoxString),('房间密码','RoomPasswordInput',unreal.EditableTextBox)]:
        setting_row=ensure(page,name+'Row',unreal.HorizontalBox);setting_row.clear_children();attach(settings,setting_row).set_padding(unreal.Margin(0,0,0,10))
        label_bounds=ensure(page,name+'LabelBounds',unreal.SizeBox);label_bounds.set_width_override(116);attach(setting_row,label_bounds).set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
        attach(label_bounds,text(page,name+'Label',label,13,color(.64,.78,.70)))
        control=ensure(page,name,kind);fill(attach(setting_row,control))
        if kind==unreal.ComboBoxString:
            options=['1','2','3','4'] if name=='RoomCapacityInput' else ['公开可加入','仅好友可加入','私密 · 仅邀请']
            control.set_editor_property('default_options',options)
            control.clear_options()
            for option in options: control.add_option(option)
            control.set_selected_index(0)
            source=unreal.load_asset('/Game/UI/Frontend/WBP_CatFrontendSettings')
            template=widget(source,'FullscreenModeComboBox')
            for prop in ('widget_style','item_style','font','foreground_color','content_padding'): control.set_editor_property(prop,template.get_editor_property(prop))
            combo_font=control.get_editor_property('font');combo_font.set_editor_property('size',14);control.set_editor_property('font',combo_font)
            control.set_editor_property('content_padding',unreal.Margin(12,7,12,7))
            combo_style=control.get_editor_property('widget_style');combo_button=combo_style.get_editor_property('combo_button_style');button_skin=combo_button.get_editor_property('button_style')
            for key in ('normal','hovered','pressed','disabled'): button_skin.set_editor_property(key,brush(.003,.017,.015))
            combo_button.set_editor_property('button_style',button_skin);combo_style.set_editor_property('combo_button_style',combo_button);control.set_editor_property('widget_style',combo_style)
        else:
            control.set_editor_property('widget_style',search.get_editor_property('widget_style'))
            control.set_hint_text('输入房间名称' if name=='RoomNameInput' else '留空不修改密码')
            if name=='RoomPasswordInput': control.set_is_password(True)
    clear=ensure(page,'RoomClearPasswordCheckBox',unreal.CheckBox);attach(clear,text(page,'RoomClearPasswordLabel','移除现有密码',13));attach(settings,clear)
    for name,label,value in [('RoomVoice','语音聊天','暂未开放'),('RoomMicrophone','麦克风模式','暂未开放'),('RoomInvitePermission','邀请权限','仅房主')]:
        line=ensure(page,name+'Row',unreal.HorizontalBox);line.clear_children();attach(settings,line).set_padding(unreal.Margin(0,8,0,0))
        fill(attach(line,text(page,name+'Label',label,13)))
        attach(line,text(page,name+'Value',value,13,color(.38,.53,.47)))
    rules=text(page,'RoomPasswordRulesText','Steam 邀请免密码；房间 ID 加入需要密码。',12);rules.set_auto_wrap_text(True);attach(settings,rules).set_padding(unreal.Margin(0,10,0,0))
    space(page,settings,'RoomSettingsSpace')
    feedback=text(page,'RoomSettingsFeedbackText','',13,color(.62,.80,.72));feedback.set_auto_wrap_text(True);attach(settings,feedback).set_padding(unreal.Margin(0,10,0,12))
    footer=ensure(page,'RoomSettingsFooter',unreal.HorizontalBox);footer.clear_children();attach(settings,footer)
    fill(btn(page,footer,'CopySettingsRoomIdButton','复制房间 ID').get_editor_property('slot')).set_padding(unreal.Margin(0,0,10,0))
    fill(btn(page,footer,'SaveRoomSettingsButton','保存设置',True).get_editor_property('slot'))
    btn(page,settings,'DismissRoomButton','解散房间').get_editor_property('slot').set_padding(unreal.Margin(0,10,0,0))
    readonly=text(page,'RoomSettingsReadonlyText','仅房主可以修改房间设置',12,color(.63,.72,.69));readonly.set_editor_property('justification',unreal.TextJustify.CENTER);attach(settings,readonly).set_padding(unreal.Margin(0,10,0,0))
    notice=frame(page,layer,'RoomNoticeDialog','邀请已发送！',360,300)
    success=ensure(page,'RoomNoticeSuccessMark',unreal.SizeBox);success.set_width_override(36);success.set_height_override(36);attach(notice,success).set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
    success_canvas=ensure(page,'RoomNoticeSuccessCanvas',unreal.CanvasPanel);attach(success,success_canvas)
    circle=ensure(page,'RoomNoticeSuccessCircle',unreal.Border);circle.set_brush(brush(.16,.70,.56,1,18));circle.set_brush_color(color(1,1,1));canvas_slot(success_canvas,circle,(0,0),(0,0,36,36))
    for name,rect,angle in [('Short',(9,19,9,3),45),('Long',(15,17,15,3),-45)]:
        stroke=ensure(page,'RoomNoticeSuccess'+name,unreal.Border);stroke.set_brush_color(color(.002,.032,.022));canvas_slot(success_canvas,stroke,(0,0),rect);stroke.set_render_transform_angle(angle)
    success.set_visibility(unreal.SlateVisibility.COLLAPSED)
    message=text(page,'RoomNoticeMessageText','',15);message.set_auto_wrap_text(True);message.set_editor_property('justification',unreal.TextJustify.CENTER);attach(notice,message).set_padding(unreal.Margin(0,20,0,14))
    value=text(page,'RoomNoticeValueText','',13,color(.80,.87,.75));value.set_auto_wrap_text(True);value.set_editor_property('justification',unreal.TextJustify.CENTER);attach(notice,value)
    space(page,notice,'RoomNoticeSpace');btn(page,notice,'ConfirmRoomNoticeButton','确定',True)
    widget(page,'RoomNoticeDialog').set_visibility(unreal.SlateVisibility.COLLAPSED)
    layer.set_visibility(unreal.SlateVisibility.COLLAPSED)
    # 好友行继续消费原句柄，扩大内容区后让姓名获得剩余宽度。
    row_surface=widget(friend,'RoomFriendRowRootBackground');row_surface.set_padding(unreal.Margin(12,7,12,7));surface(row_surface,.003,.019,.016)
    bounds=row_surface.get_parent();bounds.clear_width_override();bounds.clear_height_override();bounds.set_min_desired_height(58)
    column=widget(friend,'FriendTextColumn');fill(column.get_editor_property('slot'))
    font(widget(friend,'FriendNameText'),15,color(.85,.90,.79));font(widget(friend,'FriendStatusText'),12,color(.46,.72,.60))
    widget(friend,'FriendNameText').set_text_overflow_policy(unreal.TextOverflowPolicy.ELLIPSIS)
    button_style(widget(friend,'InviteFriendButton'))
    compact_button(widget(friend,'InviteFriendButton'));font(widget(friend,'InviteFriendButtonLabel'),14,color(.82,.91,.86))
    row_content=column.get_parent();row_content.clear_children()
    avatar=ensure(friend,'FriendAvatarBounds',unreal.SizeBox);avatar.set_width_override(34);avatar.set_height_override(34)
    avatar_slot=attach(row_content,avatar);avatar_slot.set_padding(unreal.Margin(0,0,12,0));avatar_slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    avatar_surface=ensure(friend,'FriendAvatarSurface',unreal.Border);avatar_surface.set_brush(brush(.12,.32,.27,1,17));avatar_surface.set_padding(unreal.Margin(0,0,0,0));attach(avatar,avatar_surface)
    # 通用猫脸图标，不冒充尚未接入的 Steam 头像；几何形状避免字体缺字。
    avatar_surface.clear_children()
    face=ensure(friend,'FriendAvatarFace',unreal.CanvasPanel);attach(avatar_surface,face)
    for name,rect,radius,tint,angle in [('Head',(7,11,20,17),7,(.68,.86,.75),0),('EarLeft',(8,7,7,9),1,(.68,.86,.75),-18),('EarRight',(19,7,7,9),1,(.68,.86,.75),18),('EyeLeft',(11,17,3,3),1.5,(.02,.09,.07),0),('EyeRight',(21,17,3,3),1.5,(.02,.09,.07),0),('Nose',(16,22,3,2),1,(.02,.09,.07),0)]:
        part=ensure(friend,'FriendAvatar'+name,unreal.Border);part.set_brush(brush(*tint,1,radius));part.set_brush_color(color(1,1,1));canvas_slot(face,part,(0,0),rect);part.set_render_transform_angle(angle)
    fill(attach(row_content,column));attach(row_content,widget(friend,'InviteFriendButton')).set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
