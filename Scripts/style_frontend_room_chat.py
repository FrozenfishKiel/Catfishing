"""正式房间聊天控件；房间主生成器调用，也可单独迁移当前房间资产。"""
from pathlib import Path
import runpy
import unreal

def style(page,ui):
    ensure,attach,text,font,color,widget,btn,fill,surface=[ui[k] for k in ('ensure','attach','text','font','color','widget','btn','fill','surface')]
    column=widget(page,'RoomChatColumn');column.clear_children()
    # 旧占位文本无业务绑定，显式移除对象，重复生成不会再恢复旧口径。
    for name in ('RoomChatTitle','RoomChatUnavailable'):
        old=unreal.find_object(None,page.get_path_name()+':WidgetTree.'+name)
        if old:
            old.remove_from_parent()
            assert old.rename(outer=unreal.find_object(None,'/Engine/Transient')), 'Old chat placeholder removal failed'
    open_button=btn(page,column,'RoomChatOpenButton','房间聊天')
    button_style=open_button.get_editor_property('widget_style')
    button_style.set_editor_property('normal_padding',unreal.Margin(0,2,0,5))
    button_style.set_editor_property('pressed_padding',unreal.Margin(0,2,0,5))
    for state in ('normal','hovered','pressed'):
        brush=button_style.get_editor_property(state)
        brush.set_editor_property('draw_as',unreal.SlateBrushDrawType.NO_DRAW_TYPE)
        button_style.set_editor_property(state,brush)
    open_button.set_style(button_style)
    label=widget(page,'RoomChatOpenButtonLabel');font(label,12,color(.70,.86,.77))
    label.get_editor_property('slot').set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_LEFT)
    summary=text(page,'RoomChatSummary','开启联机后可与伙伴聊天',11,color(.43,.61,.54))
    summary.set_text_overflow_policy(unreal.TextOverflowPolicy.ELLIPSIS);attach(column,summary)
    open_button.set_tool_tip_text('展开房间聊天（Enter）')
    canvas=widget(page,'RoomRootShade').get_parent()
    expanded=ensure(page,'RoomChatExpanded',unreal.SizeBox)
    expanded.set_width_override(340);expanded.set_height_override(308)
    ui['canvas_slot'](canvas,expanded,(0,1),(24,-126,340,308),(0,1),10)
    expanded.set_visibility(unreal.SlateVisibility.COLLAPSED)
    panel=ensure(page,'RoomChatPanel',unreal.Border);panel.set_brush(ui['brush'](.003,.020,.017,.99));panel.set_brush_color(color(1,1,1))
    panel.set_padding(unreal.Margin(16,14,16,12));attach(expanded,panel)
    content=ensure(page,'RoomChatContent',unreal.VerticalBox);content.clear_children();attach(panel,content)
    header=ensure(page,'RoomChatHeader',unreal.HorizontalBox);header.clear_children();attach(content,header)
    fill(attach(header,text(page,'RoomChatHeading','房间聊天',17,color(.90,.85,.68))))
    close=btn(page,header,'RoomChatCloseButton','收起',False)
    close_style=close.get_editor_property('widget_style')
    close_style.set_editor_property('normal_padding',unreal.Margin(8,3,8,3));close_style.set_editor_property('pressed_padding',unreal.Margin(8,3,8,3));close.set_style(close_style)
    font(widget(page,'RoomChatCloseButtonLabel'),11,color(.58,.75,.66))
    attach(content,text(page,'RoomChatStatus','仅房间成员可见',10,color(.42,.61,.54))).set_padding(unreal.Margin(0,3,0,10))
    overlay=ensure(page,'RoomChatMessageOverlay',unreal.Overlay);overlay.clear_children();fill(attach(content,overlay))
    messages=ensure(page,'RoomChatMessages',unreal.ScrollBox);messages.set_scrollbar_thickness(unreal.Vector2D(3,3));messages.set_allow_overscroll(False)
    bar=messages.get_editor_property('widget_bar_style')
    for name,tint in {'normal_thumb_image':(.10,.27,.22),'hovered_thumb_image':(.22,.43,.34),'dragged_thumb_image':(.32,.57,.44)}.items():
        bar.set_editor_property(name,ui['brush'](*tint,1,2))
    messages.set_editor_property('widget_bar_style',bar)
    slot=attach(overlay,messages);slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL);slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
    empty=text(page,'RoomChatEmpty','篝火已经点亮\n和伙伴打个招呼吧',13,color(.42,.61,.54));empty.set_editor_property('justification',unreal.TextJustify.CENTER)
    slot=attach(overlay,empty);slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER);slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    latest=btn(page,overlay,'RoomChatLatestButton','有新消息 · 回到最新')
    slot=latest.get_editor_property('slot');slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER);slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_BOTTOM)
    latest.set_visibility(unreal.SlateVisibility.COLLAPSED)
    prototype=text(page,'RoomChatRowStyle','',12,color(.86,.89,.80));prototype.set_visibility(unreal.SlateVisibility.COLLAPSED);attach(content,prototype)
    feedback=text(page,'RoomChatFeedback','',10,color(.94,.56,.38));attach(content,feedback).set_padding(unreal.Margin(0,3,0,3))
    entry=ensure(page,'RoomChatEntry',unreal.HorizontalBox);entry.clear_children();attach(content,entry)
    field=ensure(page,'RoomChatInput',unreal.EditableTextBox);field.set_hint_text('和伙伴说点什么…')
    field.set_editor_property('clear_keyboard_focus_on_commit',False);field.set_editor_property('select_all_text_when_focused',False)
    field.set_editor_property('revert_text_on_escape',False)
    field_style=field.get_editor_property('widget_style')
    for state,tint in {'background_image_normal':(.004,.033,.027),'background_image_hovered':(.012,.07,.055),'background_image_focused':(.018,.09,.071),'background_image_read_only':(.008,.025,.021)}.items():
        field_style.set_editor_property(state,ui['brush'](*tint))
    field_style.set_editor_property('padding',unreal.Margin(10,7,10,7))
    field_style.set_editor_property('foreground_color',ui['slate'](color(.86,.89,.80)))
    text_style=field_style.get_editor_property('text_style');text_style.set_editor_property('font',prototype.get_editor_property('font'))
    field_style.set_editor_property('text_style',text_style)
    field.set_editor_property('widget_style',field_style);fill(attach(entry,field)).set_padding(unreal.Margin(0,0,8,0))
    send=btn(page,entry,'RoomChatSendButton','发送',True)
    attach(content,text(page,'RoomChatKeyHint','Enter 发送   ·   Esc 收起   ·   最多 200 字',9,color(.38,.55,.48))).set_padding(unreal.Margin(0,7,0,0))

def main():
    page=unreal.load_asset('/Game/UI/Frontend/WBP_CatFrontendRoom')
    if '-run=pythonscript' not in unreal.SystemLibrary.get_command_line().lower():
        assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE first'
    assert '/Game/UI/Frontend/WBP_CatFrontendRoom' not in {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}, 'Unsaved room asset edits'
    ui=runpy.run_path(str(Path(__file__).with_name('style_frontend_room_dialogs.py')))
    style(page,ui)
    # 同一入口重复生成必须幂等；保留控件身份且不遗留旧占位。
    before={o.get_name() for o in unreal.ObjectIterator(unreal.Widget) if o.get_path_name().startswith(page.get_path_name()+':WidgetTree.')}
    style(page,ui)
    after={o.get_name() for o in unreal.ObjectIterator(unreal.Widget) if o.get_path_name().startswith(page.get_path_name()+':WidgetTree.')}
    assert before==after and not {'RoomChatTitle','RoomChatUnavailable'}.intersection(after), 'Chat widget migration is not idempotent'
    assert unreal.CatFrontendWidgetAuthoringLibrary.compile_styled_frontend_widget(page), 'Room WBP compile failed'
    assert unreal.EditorAssetLibrary.save_loaded_asset(page,only_if_is_dirty=False), 'Room WBP save failed'
    unreal.log('Event=room_chat_asset_authored Result=Success')

if __name__=='__main__': main()
