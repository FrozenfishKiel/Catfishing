"""把第三原子模块的固定营地和共享鱼缸接入正式 Lake。

脚本只认领 StageC_CampHub 与 StageC_FishTank 两个稳定标签；重跑时只复用或创建这两个 Actor，
不清理其它地图对象，也不改 Fishing 玩家入口、水域或 UI 资产。保存前会验证唯一 Hub、唯一 Tank
和 Hub->SharedFishTank 引用，避免把半接线地图落盘。
"""

import unreal


LAKE_PACKAGE = "/Game/Catfishing/Maps/Lake"
CAMP_LABEL = "StageC_CampHub"
TANK_LABEL = "StageC_FishTank"


def _load_lake() -> None:
    """打开正式 Lake；失败立即终止，防止脚本在错误 World 里生成营地对象。"""
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.load_level(LAKE_PACKAGE):
        raise RuntimeError(f"无法打开地图: {LAKE_PACKAGE}")


def _single_actor(actors, cls, label):
    """按类和稳定标签定位脚本拥有的 Actor；只有一个同类 Actor 时会复用并重新打稳定标签，多于一个或重复标签时抛错交给人工清理。"""
    typed = [actor for actor in actors if isinstance(actor, cls)]
    labelled = [actor for actor in typed if actor.get_actor_label() == label]
    if len(typed) > 1 or len(labelled) > 1:
        raise RuntimeError(f"Lake 中 {cls.__name__} 数量不唯一: total={len(typed)} labelled={len(labelled)}")
    return labelled[0] if labelled else (typed[0] if len(typed) == 1 else None)


def _spawn_or_reuse(editor_actors, cls, label, location):
    """复用现有唯一 Actor 或在固定位置创建它；脚本每次重跑都会回写本模块验收位置，确保 Lake 接线证据稳定。"""
    existing = _single_actor(editor_actors.get_all_level_actors(), cls, label)
    if existing:
        existing.set_actor_label(label)
        existing.set_actor_location(location, False, False)
        return existing
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(cls, location, unreal.Rotator(0.0, 0.0, 0.0))
    if actor is None:
        raise RuntimeError(f"无法生成 {label}")
    actor.set_actor_label(label)
    return actor


def configure_lake_camp() -> None:
    """流程先打开 Lake，复用或创建唯一 CampHub/FishTank 并写入 Hub 引用；保存前再次核对唯一性和引用，成功后只保存当前 Lake 关卡，失败抛错避免半接线落盘。"""
    _load_lake()
    editor_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    camp = _spawn_or_reuse(
        editor_actors,
        unreal.CatCampHubActor,
        CAMP_LABEL,
        unreal.Vector(-750.0, 350.0, 0.0),
    )
    tank = _spawn_or_reuse(
        editor_actors,
        unreal.CatFishTankActor,
        TANK_LABEL,
        unreal.Vector(-575.0, 350.0, 0.0),
    )
    camp.set_editor_property("shared_fish_tank", tank)

    final_actors = editor_actors.get_all_level_actors()
    hubs = [actor for actor in final_actors if isinstance(actor, unreal.CatCampHubActor)]
    tanks = [actor for actor in final_actors if isinstance(actor, unreal.CatFishTankActor)]
    if len(hubs) != 1 or len(tanks) != 1 or hubs[0].get_editor_property("shared_fish_tank") != tanks[0]:
        raise RuntimeError(
            f"营地接线失败: CampHub={len(hubs)} FishTank={len(tanks)} "
            f"Shared={hubs[0].get_editor_property('shared_fish_tank') if hubs else None}"
        )
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
        raise RuntimeError("无法保存 Lake 营地接线")
    unreal.log(
        "ITEMS_TANK_SACRIFICE_CAMP_MAP_CONFIGURED "
        f"CampHub={camp.get_actor_label()} FishTank={tank.get_actor_label()}"
    )


configure_lake_camp()
