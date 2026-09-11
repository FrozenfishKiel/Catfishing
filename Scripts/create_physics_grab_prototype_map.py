"""幂等生成独立物理抓握试验地图；只保存本脚本拥有的新地图，不切换正式游戏配置。"""

import unreal


MAP_PACKAGE = "/Game/Catfishing/Prototypes/PhysicsGrabPrototype"
GAME_MODE = "/Script/Catfishing.CatPhysicsPrototypeGameMode"
OWNED_TAG = "PhysicsGrabPrototypeMapLighting"


def _spawn_light(editor_actors, actor_class, label, location, rotation=None):
    actor = editor_actors.spawn_actor_from_class(
        actor_class, location, rotation or unreal.Rotator()
    )
    if actor is None:
        raise RuntimeError(f"Cannot create prototype lighting: {label}")
    actor.set_actor_label(label)
    actor.set_editor_property("tags", [unreal.Name(OWNED_TAG)])
    return actor


def main():
    mode_class = unreal.load_class(None, GAME_MODE)
    if mode_class is None:
        raise RuntimeError(f"Compile the prototype GameMode before creating its map: {GAME_MODE}")
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    exists = unreal.EditorAssetLibrary.does_asset_exist(MAP_PACKAGE)
    if not (levels.load_level(MAP_PACKAGE) if exists else levels.new_level(MAP_PACKAGE)):
        raise RuntimeError(f"Cannot load or create prototype map: {MAP_PACKAGE}")
    world = unreal.EditorLevelLibrary.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", mode_class)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    # 原生 GameMode 在服务器创建试验场、道具和角色。地图只负责环境光，不重复摆放运行时几何。
    owned = [
        actor for actor in actors.get_all_level_actors()
        if unreal.Name(OWNED_TAG) in actor.get_editor_property("tags")
    ]
    if owned and not actors.destroy_actors(owned):
        raise RuntimeError("Cannot replace script-owned prototype lights")
    sun = _spawn_light(
        actors, unreal.DirectionalLight, "PhysicsPrototype_Sun",
        unreal.Vector(0.0, 0.0, 400.0), unreal.Rotator(-45.0, -30.0, 0.0)
    )
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_editor_property("intensity", 3.0)
    sky = _spawn_light(
        actors, unreal.SkyLight, "PhysicsPrototype_SkyLight", unreal.Vector(0.0, 0.0, 300.0)
    )
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("intensity", 1.2)
    _spawn_light(
        actors, unreal.SkyAtmosphere, "PhysicsPrototype_Atmosphere", unreal.Vector()
    )
    # 保存确切目标地图；不调用 save_all_dirty_packages，以免把其他资产的并行修改一并落盘。
    if not levels.save_current_level():
        raise RuntimeError(f"Cannot save prototype map: {MAP_PACKAGE}")
    settings_mode = world.get_world_settings().get_editor_property("default_game_mode")
    if settings_mode != mode_class:
        raise RuntimeError("Prototype map did not retain its independent GameMode")
    unreal.log(f"PHYSICS_GRAB_MAP_PASS Map={MAP_PACKAGE} GameMode={GAME_MODE} LightingActors=3 RuntimeArena=GameMode")


main()
