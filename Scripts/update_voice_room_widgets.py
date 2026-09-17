"""定点迁移房间语音只读标签，不重建布局；加 -ApplyVoiceRoomAssets 才保存。
UE PythonScript commandlet 入口，核查结果写入 Saved/Tests/VoiceRoomAssets.json。
"""
import hashlib
import json
import shutil
from pathlib import Path
import unreal
root = Path(unreal.Paths.project_dir()).resolve()
package = '/Game/UI/Frontend/WBP_CatFrontendRoom'
apply = '-ApplyVoiceRoomAssets' in unreal.SystemLibrary.get_command_line()
assert package not in {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
bp = unreal.load_asset(package)
assert bp
values = dict(RoomVoiceLabel='语音状态', RoomMicrophoneLabel='我的输入模式',
              RoomVoiceValue='等待语音状态', RoomMicrophoneValue='读取已应用设置')
if apply:
    src = root / 'Content/UI/Frontend/WBP_CatFrontendRoom.uasset'
    backup = root / 'Saved/Automation/VoiceUI/Backups' / (src.stem + '-' + hashlib.sha256(src.read_bytes()).hexdigest() + '.uasset')
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists(): shutil.copy2(src, backup)
for name, value in values.items():
    obj = unreal.find_object(None, bp.get_path_name() + ':WidgetTree.' + name)
    assert isinstance(obj, unreal.TextBlock) and obj.get_parent(), name
    if apply: obj.set_text(value)
    assert str(obj.get_text()) == value, name + ': stale voice text'
    # 当前状态文案允许换行，保持原房间对话框宽度。
    if apply: obj.set_auto_wrap_text(True)
if apply:
    assert unreal.CatFrontendWidgetAuthoringLibrary.compile_styled_frontend_widget(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
root_bp = unreal.load_asset('/Game/UI/Frontend/WBP_CatFrontendRoot')
page = unreal.find_object(None, root_bp.get_path_name() + ':WidgetTree.RoomPage')
assert page and page.get_class().get_path_name() == package + '.WBP_CatFrontendRoom_C'
# 对实际加载的全部正式 UI 文本检查旧语音占位；不能以源码未命中代替资产核查。
for asset in unreal.EditorAssetLibrary.list_assets('/Game/UI', recursive=True, include_folder=False):
    if 'WBP_' in asset: unreal.load_asset(asset)
stale = []
for text in unreal.ObjectIterator(unreal.TextBlock):
    if text.get_path_name().startswith('/Game/UI/') and any(k in text.get_name() for k in ('Voice','Microphone')):
        if any(k in str(text.get_text()) for k in ('暂未开放','语音未启用','麦克风已启用')):
            stale.append(text.get_path_name())
assert not stale, stale
result = dict(result='PASS', applied=apply, room_class=page.get_class().get_path_name(), values=values, stale_voice_text=stale)
output = root / 'Saved/Tests/VoiceRoomAssets.json'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('VOICE_ROOM_ASSETS PASS')
