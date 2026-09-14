"""编辑器内受控 WBP 画面检查；通过 -ExecutePythonScript 运行，不写业务数据。"""
import json
from pathlib import Path
import time
import traceback
import runpy
import unreal

out=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))/'Automation/RoomStage'
out.mkdir(parents=True,exist_ok=True)
state={'phase':0,'deadline':time.monotonic()+120,'checks':{},'rows':[]}
state['member_ids']=[unreal.GuidLibrary.new_guid() for _ in range(4)]

def child(root,name):
    return next(o for o in unreal.ObjectIterator(unreal.Widget)
                if o.get_name()==name and o.get_path_name().startswith(root.get_path_name()+'.'))

def member(index,ready=False):
    m=unreal.CatOnlineRoomMember()
    m.set_editor_properties({'member_id':state['member_ids'][index],'display_name':['小白','阿吉','月牙','松果'][index],
                            'is_lobby_owner':index==0,'is_local_player':index==0,'is_ready':ready})
    return m

def shot(name):
    unreal.SystemLibrary.execute_console_command(state['root'].get_world(),'Shot showui filename='+str(out/(name+'.png')).replace('\\','/'))

def finish(error=None):
    if error: state['checks']['error']=error
    (out/'RuntimeChecks.json').write_text(json.dumps(state['checks'],ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(state['handle'])
    performance.set_editor_property('bThrottleCPUWhenNotForeground',state['previous_throttle'])
    if state.get('root'):
        unreal.SystemLibrary.execute_console_command(state['root'].get_world(),'cat.Fishing.Stats '+str(state.get('previous_stats',1)))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.log('Event=room_party_ui_fixture_finished Error='+str(error is not None))

def tick(delta):
    try:
        if time.monotonic()>state['deadline']: raise RuntimeError('UI fixture timed out')
        if time.monotonic()<state.get('next',0): return
        phase=state['phase']
        if phase==0:
            roots=[r for r in unreal.ObjectIterator(unreal.CatFrontendRootWidget) if r.is_in_viewport()]
            if not roots: return
            r=roots[0];state['root']=r
            state['checks']['viewport']=str(unreal.WidgetLayoutLibrary.get_viewport_size(r))
            unreal.SystemLibrary.execute_console_command(r.get_world(),'cat.Fishing.Stats 0')
            child(r,'FrontendPageSwitcher').set_active_widget(child(r,'RoomPage'))
            scroll=child(r,'PlayersScrollBox');scroll.clear_children()
            cls=unreal.load_asset('/Game/UI/Frontend/WBP_CatRoomPlayerSlot').generated_class()
            for i in range(4):
                row=unreal.CatFrontendWidgetAuthoringLibrary.create_widget_preview(r.get_world(),cls,r.get_owning_player())
                if i<2: row.configure_row(member(i,i==1))
                else: row.configure_empty_slot()
                scroll.add_child(row);row.set_preview_active(True);state['rows'].append(row)
            state['phase']=1;state['next']=time.monotonic()+5
        elif phase==1:
            rows=state['rows']
            state['checks']['unready_mark_hidden']=child(rows[0],'ReadyMark').get_visibility()==unreal.SlateVisibility.HIDDEN
            state['checks']['ready_mark_visible']=child(rows[1],'ReadyMark').get_visibility()==unreal.SlateVisibility.HIT_TEST_INVISIBLE
            state['checks']['two_empty_seats']=all(str(child(row,'PlayerSlotStateText').get_text())=='等待加入' for row in rows[2:])
            actors=[a for a in unreal.GameplayStatics.get_all_actors_of_class(state['root'].get_world(),unreal.CatFrontendCharacterPreview)]
            state['checks']['preview_count']=len(actors)
            state['preview_names']=[a.get_name() for a in actors]
            if actors:
                state['preview_mesh']=actors[0].get_component_by_class(unreal.SkeletalMeshComponent)
                state['animation_position']=state['preview_mesh'].get_position()
                state['checks']['idle_playing']=state['preview_mesh'].is_playing()
                state['checks']['preview_materials']=[str(m) for m in state['preview_mesh'].get_materials()]
            if actors:
                capture=actors[0].get_component_by_class(unreal.SceneCaptureComponent2D)
                result=unreal.RenderingLibrary.read_render_target(state['root'].get_world(),capture.get_editor_property('texture_target'))
                if isinstance(result,tuple) and len(result)==2:
                    ok,samples=result if isinstance(result[0],bool) else (result[1],result[0])
                else:
                    samples=result;ok=result is not None
                state['checks']['capture_read_ok']=ok
                state['checks']['capture_alpha_range']=[min(c.a for c in samples),max(c.a for c in samples)] if samples else []
                state['checks']['capture_opaque_rgb']=[max(c.r for c in samples if c.a<128),max(c.g for c in samples if c.a<128),max(c.b for c in samples if c.a<128)]
                mat=unreal.load_asset('/Game/UI/Frontend/M_UI_RoomCharacterPreview')
                state['checks']['material_parameters']=str(unreal.MaterialEditingLibrary.get_texture_parameter_names(mat))
                state['checks']['material_opacity_input']=str(unreal.MaterialEditingLibrary.get_material_property_input_node(mat,unreal.MaterialProperty.MP_OPACITY))
            shot('RoomTwoMembersWide')
            state['phase']=11;state['next']=time.monotonic()+2
        elif phase==11:
            rows=state['rows']
            state['checks']['idle_time_advances']=abs(state['preview_mesh'].get_position()-state['animation_position'])>.001
            shot('RoomIdleSecondFrame')
            rows[0].configure_row(member(0,True))
            state['phase']=2;state['next']=time.monotonic()+2
        elif phase==2:
            actors=unreal.GameplayStatics.get_all_actors_of_class(state['root'].get_world(),unreal.CatFrontendCharacterPreview)
            state['checks']['ready_refresh_reuses_preview']=sorted(a.get_name() for a in actors)==sorted(state['preview_names'])
            state['rows'][1].configure_empty_slot()
            state['phase']=3;state['next']=time.monotonic()+2
        elif phase==3:
            actors=unreal.GameplayStatics.get_all_actors_of_class(state['root'].get_world(),unreal.CatFrontendCharacterPreview)
            state['checks']['leave_releases_preview']=len(actors)==1
            state['root'].request_cancel()
            state['phase']=4;state['next']=time.monotonic()+2
        elif phase==4:
            r=state['root']
            actors=unreal.GameplayStatics.get_all_actors_of_class(r.get_world(),unreal.CatFrontendCharacterPreview)
            state['checks']['menu_releases_previews']=len(actors)==0
            cls=unreal.load_asset('/Game/UI/Save/WBP_CatLakeMainMenu').generated_class()
            menu=unreal.CatFrontendWidgetAuthoringLibrary.create_widget_preview(r.get_world(),cls,r.get_owning_player())
            state['controller']=unreal.CatFrontendWidgetAuthoringLibrary.bind_lake_menu_preview(menu,r.get_owning_player())
            state['menu']=menu
            state['phase']=5;state['next']=time.monotonic()+2
        elif phase==5:
            shot('LakeCommandWide')
            state['phase']=15;state['next']=time.monotonic()+2
        elif phase==15:
            menu=state['menu']
            child(menu,'PauseRequestButton').get_editor_property('on_clicked').broadcast()
            state['checks']['pause_placeholder_feedback']='暂未开放' in str(child(menu,'StatusTextBlock').get_text())
            child(menu,'PartyButton').get_editor_property('on_clicked').broadcast()
            state['checks']['party_button_opens_panel']=child(menu,'LakeMainMenuPageSwitcher').get_active_widget()==child(menu,'LakePartyPanel')
            scroll=child(menu,'PartyMembersScrollBox')
            cls=unreal.load_asset('/Game/UI/Party/WBP_CatPartyMemberRow').generated_class()
            for i in range(4):
                row=unreal.CatFrontendWidgetAuthoringLibrary.create_widget_preview(state['root'].get_world(),cls,state['root'].get_owning_player())
                if i<2: row.configure_row(member(i))
                else: row.configure_empty_slot()
                scroll.add_child(row)
            child(menu,'PartyCountText').set_text('当前队伍 2 / 4')
            state['phase']=6;state['next']=time.monotonic()+2
        elif phase==6:
            shot('LakePartyWide')
            state['checks']['scope']='受控布局/角色生命周期，不证明 Steam 双端邀请或准备同步'
            state['phase']=7;state['next']=time.monotonic()+2
        else:
            menu=state['menu']
            child(menu,'PartyBackButton').get_editor_property('on_clicked').broadcast()
            state['checks']['party_back_opens_command']=child(menu,'LakeMainMenuPageSwitcher').get_active_widget()==child(menu,'LakeCommandPanel')
            child(menu,'CloseButton').get_editor_property('on_clicked').broadcast()
            state['checks']['close_removes_menu']=not menu.is_in_viewport()
            unreal.CatFrontendWidgetAuthoringLibrary.release_lake_menu_preview(state['controller'])
            finish()
    except Exception:
        unreal.log_error(traceback.format_exc())
        finish(traceback.format_exc())

performance=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
state['previous_throttle']=performance.get_editor_property('bThrottleCPUWhenNotForeground')
state['previous_stats']=unreal.SystemLibrary.get_console_variable_int_value('cat.Fishing.Stats')
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
runpy.run_path(str(Path(__file__).with_name('style_frontend_room.py')),run_name='__main__')
runpy.run_path(str(Path(__file__).with_name('style_lake_party.py')),run_name='__main__')
state['checks']['frontend_fonts']=unreal.CatFrontendWidgetAuthoringLibrary.validate_frontend_widget_blueprint_fonts()
registry=unreal.AssetRegistryHelpers.get_asset_registry()
state['checks']['party_cook_reference']='/Game/UI/Party/WBP_CatPartyMemberRow' in [str(n) for n in registry.get_dependencies('/Game/UI/Save/WBP_CatLakeMainMenu',unreal.AssetRegistryDependencyOptions())]
slot=unreal.get_default_object(unreal.load_asset('/Game/UI/Frontend/WBP_CatRoomPlayerSlot').generated_class())
state['checks']['cute_cat_class']=slot.get_editor_property('preview_character_class').get_path_name()
state['checks']['idle_animation']=slot.get_editor_property('preview_animation').get_path_name()
performance.set_editor_property('bThrottleCPUWhenNotForeground',False)
state['handle']=unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
