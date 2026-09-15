"""独立编辑器进程执行；只迁移加入页，并增量补齐房间短码控件，保留已有房间布局。"""
from pathlib import Path
import runpy
import unreal

base=runpy.run_path(str(Path(__file__).with_name('style_frontend_save.py')))
widget,ensure,attach,text=(base[k] for k in ('widget','ensure','attach','text'))
dialogs=runpy.run_path(str(Path(__file__).with_name('style_frontend_room_dialogs.py')))
targets={'/Game/UI/Frontend/WBP_CatFrontendJoin','/Game/UI/Frontend/WBP_CatFrontendRoom'}
dirty={p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
if dirty.intersection(targets): raise RuntimeError('目标资产存在未保存改动')
if not unreal.CatFrontendWidgetAuthoringLibrary.rebuild_public_room_browser(): raise RuntimeError('加入页迁移失败')
room=unreal.load_asset('/Game/UI/Frontend/WBP_CatFrontendRoom')
panel=widget(room,'RoomInviteIdPanel')
if not panel: raise RuntimeError('正式房间邀请码面板缺失')
attach(panel,text(room,'RoomShortCodeText','邀请码由房主分享',22)).set_padding(unreal.Margin(0,16,0,8))
dialogs['btn'](room,panel,'CopyRoomShortCodeButton','复制免密邀请码',True)
rules=widget(room,'RoomPasswordRulesText')
rules.set_text('房主主动邀请和 6 位邀请码免密码；列表和房间 ID 按设置校验密码。')
if not unreal.CatFrontendWidgetAuthoringLibrary.compile_styled_frontend_widget(room): raise RuntimeError('房间控件编译失败')
if not unreal.EditorAssetLibrary.save_loaded_asset(room,False): raise RuntimeError('房间控件保存失败')
join=unreal.load_asset('/Game/UI/Frontend/WBP_CatFrontendJoin')
for name in ['PublicRoomsScrollBox','RefreshPublicRoomsButton','JoinPasswordPanel','JoinPasswordInput','SubmitRoomPasswordButton','CancelRoomPasswordButton']:
 if not widget(join,name): raise RuntimeError('控件缺失 '+name)
if not widget(join,'JoinPasswordInput').get_editor_property('is_password'): raise RuntimeError('密码输入没有遮罩')
unreal.log('Event=public_room_assets_upgraded Result=Passed')
# 增量迁移或重建三栏后恢复湖畔背景；保留新准入控件与唯一联机入口。
runpy.run_path(str(Path(__file__).with_name('style_frontend_backdrops.py')))['main'](
    ('/Game/UI/Frontend/WBP_CatFrontendJoin',))
