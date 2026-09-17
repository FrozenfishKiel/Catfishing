"""核查配置的正式玩法地图和语音装配，不保存资产；使用 UE PythonScript commandlet 执行。

检查结果写入 Saved/Tests/VoiceAssets.json；这只证明资产/配置接线，不证明双机语音传输或听感。
"""
import configparser
import json
from pathlib import Path
import unreal


root = Path(unreal.Paths.project_dir()).resolve()
game = configparser.ConfigParser(strict=False, interpolation=None)
game.read(root / 'Config/DefaultGame.ini', encoding='utf-8-sig')
engine = configparser.ConfigParser(strict=False, interpolation=None)
engine.read(root / 'Config/DefaultEngine.ini', encoding='utf-8-sig')
map_path = game['/Script/Catfishing.CatOnlineSettings']['GameplayMap'].split('.')[0]
world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
assert world, 'Configured gameplay map failed to load'
mode_class = world.get_world_settings().get_editor_property('default_game_mode')
if not mode_class:
    mode_class = unreal.load_class(None, engine['/Script/EngineSettings.GameMapsSettings']['GlobalDefaultGameMode'])
assert mode_class, 'Gameplay GameMode is missing'
mode = unreal.get_default_object(mode_class)
state_class = mode.get_editor_property('player_state_class')
state = unreal.get_default_object(state_class)
voice = state.get_component_by_class(unreal.CatProximityVoiceComponent)
assert voice, 'Configured PlayerState does not install proximity voice'
assert mode.get_editor_property('default_pawn_class'), 'Gameplay mode does not spawn a Pawn'
voice_class_path = engine['/Script/Engine.AudioSettings']['VoiPSoundClass']
assert unreal.load_asset(voice_class_path), 'Existing voice SoundClass is missing'
settings = unreal.get_default_object(unreal.load_class(None, '/Script/Catfishing.CatVoiceSettings'))
near = settings.get_editor_property('FullVolumeDistanceCm')
far = settings.get_editor_property('SilentDistanceCm')
assert 0 <= near < far, 'Invalid distance range'
assert engine['OnlineSubsystem']['bHasVoiceEnabled'].lower() == 'true'
assert engine['Voice']['bEnabled'].lower() == 'true'
result = dict(gameplay_map=map_path, game_mode=mode_class.get_path_name(),
              player_state=state_class.get_path_name(), voice_component=voice.get_class().get_path_name(),
              voice_sound_class=voice_class_path, full_volume_cm=near, silent_cm=far,
              result='PASS', evidence_layer='contract', steam_audio_roundtrip_verified=False)
output = root / 'Saved/Tests/VoiceAssets.json'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('PROXIMITY_VOICE_ASSETS PASS ' + json.dumps(result, ensure_ascii=False))
