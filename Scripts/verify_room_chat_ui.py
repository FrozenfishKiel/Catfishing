"""正式聊天 WBP 的受控表现检查；样例消息不代表 Steam 双端收发证据。"""
import json
import time
import traceback
from pathlib import Path
import unreal

out=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))/'Automation/RoomChat/UI'
out.mkdir(parents=True,exist_ok=True)
state={'phase':0,'deadline':time.monotonic()+180,'checks':{}}

def child(name):
    root=state['root']
    return next(o for o in unreal.ObjectIterator(unreal.Widget) if o.get_name()==name and o.get_path_name().startswith(root.get_path_name()+'.'))

def message(name,text,local=False,system=False,pending=False,failed=False):
    value=unreal.CatRoomChatMessage()
    value.set_editor_properties({'message_id':unreal.GuidLibrary.new_guid(),'sender_name':name,'text':text,'local':local,'system':system,'pending':pending,'failed':failed})
    return value

def shot(name):
    unreal.SystemLibrary.execute_console_command(state['root'].get_world(),'Shot showui filename='+str(out/(name+'.png')).replace('\\','/'))

def finish(error=None):
    state['checks']['error']=error
    (out/'checks.json').write_text(json.dumps(state['checks'],ensure_ascii=False,indent=2),encoding='utf8')
    unreal.unregister_slate_post_tick_callback(state['handle'])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    if error: unreal.log_error('Event=room_chat_ui_verified Result=Failed '+error)
    else: unreal.log('Event=room_chat_ui_verified Result=Passed')
    if '-RoomChatQuit' in unreal.SystemLibrary.get_command_line(): unreal.SystemLibrary.execute_console_command(None,'QUIT_EDITOR')

def tick(delta):
    try:
        if time.monotonic()>state['deadline']: raise RuntimeError('Chat presentation timed out')
        if time.monotonic()<state.get('next',0): return
        if state.get('pending_shot'):
            shot(state.pop('pending_shot'))
            state['next']=time.monotonic()+1
            return
        phase=state['phase']
        if phase==0:
            roots=[r for r in unreal.ObjectIterator(unreal.CatFrontendRootWidget) if r.is_in_viewport()]
            if not roots: return
            root=roots[0];state['root']=root
            unreal.SystemLibrary.execute_console_command(root.get_world(),'cat.Fishing.Stats 0')
            child('FrontendPageSwitcher').set_active_widget(child('RoomPage'))
            snapshot=unreal.CatOnlineSnapshot()
            snapshot.set_editor_properties({'room_name':'镜湖营地','lobby_id':'109775241384567890','current_players':2,'max_players':4,'is_host':True,'session_state':unreal.CatOnlineSessionState.HOST,'world_state':unreal.CatOnlineWorldState.FRONTEND})
            snapshot.set_editor_property('session_access',unreal.CatSessionAccessPolicy.FRIENDS_ONLY)
            root.render_room_snapshot(snapshot)
            players=child('PlayersScrollBox');players.clear_children();state['seats']=[]
            player_class=unreal.load_asset('/Game/UI/Frontend/WBP_CatRoomPlayerSlot').generated_class()
            for index in range(4):
                seat=unreal.CatFrontendWidgetAuthoringLibrary.create_widget_preview(root.get_world(),player_class,root.get_owning_player())
                if index<2:
                    member=unreal.CatOnlineRoomMember()
                    member.set_editor_properties({'member_id':unreal.GuidLibrary.new_guid(),'display_name':'我' if index==0 else '小橘','is_local_player':index==0,'is_lobby_owner':index==0,'is_ready':True})
                    seat.configure_row(member)
                else: seat.configure_empty_slot()
                players.add_child(seat);seat.set_preview_active(True);state['seats'].append(seat)
            state['messages']=[message('', '小橘加入了房间',system=True),message('小橘','等我换一下鱼竿，今晚想试试湖边的新钓点。'),message('我','好呀，准备好了说一声。',local=True),message('月牙','我带了鱼饵，一起出发吧！')]
            root.render_room_chat(state['messages'],True)
            state['checks']['input_enabled']=child('RoomChatInput').get_is_enabled()
            state['checks']['collapsed_initially']=child('RoomChatExpanded').get_visibility()==unreal.SlateVisibility.COLLAPSED
            state['phase']=1;state['pending_shot']='Collapsed';state['next']=time.monotonic()+5
        elif phase==1:
            state['root'].set_keyboard_focus()
            unreal.CatFrontendWidgetAuthoringLibrary.press_frontend_preview_key('Enter')
            state['checks']['expanded']=child('RoomChatExpanded').get_visibility()==unreal.SlateVisibility.VISIBLE
            state['checks']['rows_rendered']=child('RoomChatMessages').get_children_count()==4
            state['checks']['enter_focuses_input']=child('RoomChatInput').has_keyboard_focus() or child('RoomChatInput').has_focused_descendants()
            child('RoomChatInput').set_text('断线时保留草稿')
            unreal.CatFrontendWidgetAuthoringLibrary.press_frontend_preview_key('Enter')
            state['checks']['rejected_send_preserves_draft']=str(child('RoomChatInput').get_text())=='断线时保留草稿'
            state['checks']['rejected_send_feedback']=bool(str(child('RoomChatFeedback').get_text()))
            child('RoomChatInput').set_text('');child('RoomChatFeedback').set_text('')
            state['phase']=2;state['pending_shot']='Expanded';state['next']=time.monotonic()+3
        elif phase==2:
            state['messages'].extend([message('小橘','这是较长的中文消息，用来确认在小窗口内可以自然换行。'*5),message('我','刚才这条暂时未能确认送达',local=True,failed=True),message('我','正在发送的消息',local=True,pending=True)])
            state['root'].render_room_chat(state['messages'],True)
            state['phase']=3;state['pending_shot']='LongAndDeliveryStates';state['next']=time.monotonic()+2
        elif phase==3:
            state['checks']['long_message_reaches_latest']=abs(child('RoomChatMessages').get_scroll_offset()-child('RoomChatMessages').get_scroll_offset_of_end())<2
            child('RoomChatMessages').set_scroll_offset(0)
            state['root'].call_method('HandleChatScrolled',args=(0.0,))
            state['messages'].append(message('月牙','又有一条新消息'))
            state['root'].render_room_chat(state['messages'],True)
            state['checks']['unread_while_scrolled']=child('RoomChatLatestButton').get_visibility()==unreal.SlateVisibility.VISIBLE
            state['phase']=4;state['pending_shot']='Unread';state['next']=time.monotonic()+2
        elif phase==4:
            state['root'].call_method('RequestChatLatest')
            state['checks']['latest_clears_unread']=child('RoomChatLatestButton').get_visibility()==unreal.SlateVisibility.COLLAPSED
            state['root'].render_room_chat([],False)
            state['checks']['offline_input_disabled']=not child('RoomChatInput').get_is_enabled()
            state['checks']['offline_send_disabled']=not child('RoomChatSendButton').get_is_enabled()
            state['checks']['empty_visible']=child('RoomChatEmpty').get_visibility()!=unreal.SlateVisibility.COLLAPSED
            state['phase']=5;state['pending_shot']='Offline';state['next']=time.monotonic()+2
        elif phase==5:
            state['root'].set_keyboard_focus()
            unreal.CatFrontendWidgetAuthoringLibrary.press_frontend_preview_key('Escape')
            state['checks']['close_preserves_room']=child('FrontendPageSwitcher').get_active_widget()==child('RoomPage')
            state['checks']['closed']=child('RoomChatExpanded').get_visibility()==unreal.SlateVisibility.COLLAPSED
            font=unreal.load_asset('/Game/UI/Shop/F_CatShopChinese')
            state['checks']['chat_font_contract']=child('RoomChatRowStyle').get_editor_property('font').get_editor_property('font_object')==font
            state['checks']['chat_input_font_contract']=child('RoomChatInput').get_editor_property('widget_style').get_editor_property('text_style').get_editor_property('font').get_editor_property('font_object')==font
            registry=unreal.AssetRegistryHelpers.get_asset_registry()
            state['checks']['room_font_cook_reference']='/Game/UI/Shop/F_CatShopChinese' in [str(p) for p in registry.get_dependencies('/Game/UI/Frontend/WBP_CatFrontendRoom',unreal.AssetRegistryDependencyOptions())]
            if not all(state['checks'].values()): raise RuntimeError('Chat presentation assertion failed')
            state['phase']=6;state['next']=time.monotonic()+2
        else: finish()
    except Exception: finish(traceback.format_exc())

unreal.EditorLoadingAndSavingUtils.load_map('/Game/Catfishing/Maps/Frontend')
state['handle']=unreal.register_slate_post_tick_callback(tick)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
assert unreal.CatFrontendWidgetAuthoringLibrary.begin_floating_frontend_preview(1280,720)
