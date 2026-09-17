"""检查正式语音模式和麦克风控件；加 -ApplyMicrophoneAssets 时定点迁移语音控件，不重建布局。

在 UE PythonScript commandlet 中执行，结果写入 Saved/Tests/MicrophoneAssets.json。
"""
import hashlib
import json
import shutil
import uuid
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
apply = '-ApplyMicrophoneAssets' in unreal.SystemLibrary.get_command_line()
targets = ('/Game/UI/Frontend/WBP_CatFrontendSettings', '/Game/UI/Save/WBP_CatLakeMainMenu')
dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
assert not dirty.intersection(targets), 'Unsaved edits in target widgets'
results = []
for package in targets:
    bp = unreal.load_asset(package)
    assert bp, package
    def control(name, cls):
        obj = unreal.find_object(None, bp.get_path_name() + ':WidgetTree.' + name)
        assert isinstance(obj, cls), f'{package}: missing {name}'
        return obj
    combo = control('MicrophoneComboBox', unreal.ComboBoxString)
    hint = control('MicrophoneUnavailableText', unreal.TextBlock)
    refresh = control('RefreshAudioOutputDevicesButton', unreal.Button)
    label = refresh.get_child_at(0)
    assert isinstance(label, unreal.TextBlock), 'Unexpected refresh button content'
    mode = control('VoiceInputModeComboBox', unreal.ComboBoxString)
    mode_hint = control('VoiceInputModeUnavailableText', unreal.TextBlock)
    old_row = unreal.find_object(None, bp.get_path_name() + ':WidgetTree.VoiceChatRowBounds')
    control('AudioOutputDeviceComboBox', unreal.ComboBoxString)
    if apply:
        src = root / 'Content' / (package.removeprefix('/Game/') + '.uasset')
        backup = root / 'Saved/Automation/Microphone/Backups' / (src.stem + '-' + hashlib.sha256(src.read_bytes()).hexdigest() + '.uasset')
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            shutil.copy2(src, backup)
        if old_row:
            removed = []
            def collect(widget):
                removed.append(widget)
                if isinstance(widget, unreal.PanelWidget):
                    for i in range(widget.get_children_count()):
                        collect(widget.get_child_at(i))
            collect(old_row)
            old_row.remove_from_parent()
            # UE ForEachSourceWidget 按 Outer 枚举；只脱离父控件会留下待保存的孤立 GUID。
            # 移出已确认删除的旧行及其子控件，再让既有编译入口注销 GUID。
            for widget in removed:
                assert widget.rename(name='RemovedVoice_' + uuid.uuid4().hex, outer=unreal.get_transient_package())
        mode.set_editor_property('default_options', ['禁用', '常开', '按住 V 说话'])
        mode.clear_options()
        for option in ('禁用', '常开', '按住 V 说话'):
            mode.add_option(option)
        mode.set_selected_index(0)
        mode_hint.set_text('按住 V 说话：松开即停。禁用只关闭自己的麦克风。')
        hint.set_text('设备列表将在打开设置后加载；选择麦克风后点击应用。')
        combo.set_editor_property('default_options', ['设备列表待加载'])
        combo.clear_options()
        combo.add_option('设备列表待加载')
        combo.set_selected_option('设备列表待加载')
        combo.set_is_enabled(False)
        label.set_text('刷新音频设备')
        assert unreal.CatFrontendWidgetAuthoringLibrary.compile_styled_frontend_widget(bp)
        assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    assert not old_row or not old_row.get_parent(), 'Old voice checkbox still attached'
    assert mode.get_option_count() == 3
    results.append(dict(voice_modes=[mode.get_option_at_index(i) for i in range(3)], package=package, combo=combo.get_path_name(), hint=str(hint.get_text()), refresh=str(label.get_text())))

# 通过正式 Root 实际控件树引用确认前端仍消费原设置页；不从源码搜索推断二进制引用。
bp = unreal.load_asset('/Game/UI/Frontend/WBP_CatFrontendRoot')
page = unreal.find_object(None, bp.get_path_name() + ':WidgetTree.FrontendSettingsPage')
assert page and page.get_class().get_path_name() == targets[0] + '.WBP_CatFrontendSettings_C'
result = dict(result='PASS', evidence_layer='contract', applied=apply,
              root_settings_class=page.get_class().get_path_name(), widgets=results,
              real_microphone_roundtrip_verified=False)
output = root / 'Saved/Tests/MicrophoneAssets.json'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('MICROPHONE_ASSETS PASS ' + json.dumps(result, ensure_ascii=False))
