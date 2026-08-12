"""只读打开并核对 Lake 地图的阶段 A Actor 数量与 GameMode 装配，不保存或修改资产。"""

import unreal


def main() -> None:
    """加载 Lake 后统计唯一出生点、稳定标签地面和总 Actor，并核对原生 GameMode；任一事实不符即让命令行失败。"""
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.load_level("/Game/Catfishing/Maps/Lake"):
        raise RuntimeError("无法只读加载 Lake 地图")

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    player_starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    grounds = [actor for actor in actors if actor.get_actor_label() == "StageA_Ground"]
    world_settings = unreal.EditorLevelLibrary.get_editor_world().get_world_settings()
    actual_game_mode = world_settings.get_editor_property("default_game_mode")
    expected_game_mode = unreal.load_class(None, "/Script/Catfishing.CatfishingGameModeBase")

    if len(player_starts) != 1 or len(grounds) != 1 or len(actors) != 2 or actual_game_mode != expected_game_mode:
        raise RuntimeError(
            f"Lake 查询失败: PlayerStart={len(player_starts)} Ground={len(grounds)} Total={len(actors)} GameMode={actual_game_mode}"
        )
    unreal.log(
        f"STAGE_A_MAP_QUERY_PASS PlayerStart={len(player_starts)} Ground={len(grounds)} Total={len(actors)} GameMode={actual_game_mode}"
    )


main()
