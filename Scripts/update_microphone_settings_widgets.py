"""检查正式麦克风控件；加 -ApplyMicrophoneAssets 时只迁移提示文字，不重建布局。

在 UE PythonScript commandlet 中执行，结果写入 Saved/Tests/MicrophoneAssets.json。
"""
import hashlib
import json
import shutil
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
    control('VoiceInputModeComboBox', unreal.ComboBoxString)
    control('AudioOutputDeviceComboBox', unreal.ComboBoxString)
    if apply:
        src = root / 'Content' / (package.removeprefix('/Game/') + '.uasset')
        backup = root / 'Saved/Automation/Microphone/Backups' / (src.stem + '-' + hashlib.sha256(src.read_bytes()).hexdigest() + '.uasset')
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            shutil.copy2(src, backup)
        hint.set_text('设备列表将在打开设置后加载；选择麦克风后点击应用。')
        combo.clear_options()
        combo.add_option('设备列表待加载')
        combo.set_selected_option('设备列表待加载')
        combo.set_is_enabled(False)
        label.set_text('刷新音频设备')
        assert unreal.CatFrontendWidgetAuthoringLibrary.compile_styled_frontend_widget(bp)
        assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    results.append(dict(package=package, combo=combo.get_path_name(), hint=str(hint.get_text()), refresh=str(label.get_text())))

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
