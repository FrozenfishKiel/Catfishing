"""正式 WBP 的只读表现检查；样例摘要不代表真实平台联机验收。"""
import json
from pathlib import Path
import time
import unreal

out=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))/'Automation/PublicRooms/UI'
out.mkdir(parents=True,exist_ok=True)
state={'phase':0,'deadline':time.monotonic()+120,'checks':{}}
def child(root,name):
 return next(o for o in unreal.ObjectIterator(unreal.Widget) if o.get_name()==name and o.get_path_name().startswith(root.get_path_name()+'.'))
def finish(error=None):
 state['checks']['error']=error
 (out/'checks.json').write_text(json.dumps(state['checks'],ensure_ascii=False,indent=2),encoding='utf8')
 unreal.unregister_slate_post_tick_callback(state['handle'])
 unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
 unreal.EditorPythonScripting.set_keep_python_script_alive(False)
 if error: unreal.log_error('Event=public_room_ui_verified Result=Failed '+error)
 else: unreal.log('Event=public_room_ui_verified Result=Passed')
def shot(name):
 unreal.SystemLibrary.execute_console_command(state['root'].get_world(),'Shot showui filename='+str(out/(name+'.png')).replace('\\','/'))
def tick(delta):
 try:
  if time.monotonic()>state['deadline']: raise RuntimeError('UI verification timeout')
  if time.monotonic()<state.get('next',0): return
  if state['phase']==0:
   roots=[r for r in unreal.ObjectIterator(unreal.CatFrontendRootWidget) if r.is_in_viewport()]
   if not roots: return
   r=roots[0];state['root']=r
   child(r,'FrontendPageSwitcher').set_active_widget(child(r,'JoinPage'))
   rows=[]
   for name,owner,count,locked,playing in [('镜湖营地','小白',1,False,False),('朋友的钓鱼夜','月牙',2,True,False),('满员房间','松果',4,True,False),('午后钓鱼','阿吉',3,False,True)]:
    row=unreal.CatSessionSearchSummary();row.set_editor_properties({'RoomName':name,'OwnerDisplayName':owner,'CurrentPlayers':count,'MaxPlayers':4,'bHasPassword':locked,'bInProgress':playing,'bCanJoin':count<4});rows.append(row)
   snapshot=unreal.CatOnlineSnapshot();snapshot.set_editor_property('search_results',rows)
   state['snapshot']=snapshot;r.refresh_public_room_presentation(snapshot)
   state['checks']['public_rows']=child(r,'PublicRoomsScrollBox').get_children_count()==4
   state['checks']['password_hidden_initially']=child(r,'JoinPasswordPanel').get_visibility()==unreal.SlateVisibility.COLLAPSED
   state['phase']=1;state['next']=time.monotonic()+3
  elif state['phase']==1:
   shot('PublicRooms')
   state['phase']=2;state['next']=time.monotonic()+1
  elif state['phase']==2:
   state['snapshot'].set_editor_property('password_requested',True)
   state['root'].refresh_public_room_presentation(state['snapshot'])
   secret=child(state['root'],'JoinPasswordInput');secret.set_text('fixture-secret')
   state['checks']['password_masked']=secret.get_editor_property('is_password')
   state['checks']['password_visible']=child(state['root'],'JoinPasswordPanel').get_visibility()==unreal.SlateVisibility.VISIBLE
   state['phase']=3;state['next']=time.monotonic()+2
  elif state['phase']==3:
   shot('RoomPassword')
   state['phase']=4;state['next']=time.monotonic()+1
  else:
   state['snapshot'].set_editor_property('password_requested',False)
   state['root'].refresh_public_room_presentation(state['snapshot'])
   state['checks']['password_cleared']=str(child(state['root'],'JoinPasswordInput').get_text())==''
   if not all(state['checks'].values()): raise RuntimeError('Presentation assertion failed')
   finish()
 except Exception as error: finish(str(error))

unreal.EditorLoadingAndSavingUtils.load_map('/Game/Catfishing/Maps/Frontend')
state['handle']=unreal.register_slate_post_tick_callback(tick)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
