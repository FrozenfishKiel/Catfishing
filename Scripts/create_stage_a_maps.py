"""在真实 Unreal Editor 进程中生成阶段 A 的 Frontend 与 Lake 地图资产。"""

import unreal


def _save_current_map(package_name: str) -> None:
    """保存已由 LevelEditorSubsystem 创建并命名的当前 World；失败时携带目标包名终止命令行。"""
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.save_current_level():
        raise RuntimeError(f"无法保存地图: {package_name}")


def _set_game_mode(game_mode_path: str) -> None:
    """把当前 WorldSettings 的默认 GameMode 写成给定原生类，使 umap 自身携带装配事实而不依赖全局配置。"""
    world = unreal.EditorLevelLibrary.get_editor_world()
    settings = world.get_world_settings()
    settings.set_editor_property("default_game_mode", unreal.load_class(None, game_mode_path))


def _open_or_create_map(package_name: str) -> None:
    """打开已存在的生成资产或创建新地图，使脚本在首次失败后重跑时仍能覆盖同一目标而不删除二进制。"""
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    succeeded = level_editor.load_level(package_name) if unreal.EditorAssetLibrary.does_asset_exist(package_name) else level_editor.new_level(package_name)
    if not succeeded:
        raise RuntimeError(f"无法打开或创建地图: {package_name}")


def create_frontend() -> None:
    """创建空 Frontend 地图，写入无 Pawn 的前台 GameMode 后保存为真实 umap。"""
    _open_or_create_map("/Game/Catfishing/Maps/Frontend")
    _set_game_mode("/Script/Catfishing.CatFrontendGameMode")
    _save_current_map("/Game/Catfishing/Maps/Frontend")


def create_lake() -> None:
    """加载 Lake 后删除由阶段 A 统一治理的全部 PlayerStart，但地面只认领 StageA_Ground 标签；重建后通过 1/1/2 数量门禁才保存，重复执行不会累积资产。"""
    _open_or_create_map("/Game/Catfishing/Maps/Lake")
    _set_game_mode("/Script/Catfishing.CatfishingGameModeBase")
    editor_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing_actors = editor_actors.get_all_level_actors()
    # PlayerStart 全部属于阶段 A 脚本的唯一出生点；地面只按稳定标签认领，其他 Actor 一律保留且不重建地图。
    owned_actors = [actor for actor in existing_actors if isinstance(actor, unreal.PlayerStart) or actor.get_actor_label() == "StageA_Ground"]
    if owned_actors and not editor_actors.destroy_actors(owned_actors):
        raise RuntimeError("无法清理阶段 A 拥有的 Lake Actor")

    player_start = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0.0, 0.0, 150.0))
    if player_start is None:
        raise RuntimeError("无法生成 StageA_PlayerStart")
    player_start.set_actor_label("StageA_PlayerStart")
    # 先生成明确的 StaticMeshActor，再把 Engine Cube 写入组件；资产对象本身不是可直接生成的 Actor 类。
    cube = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, -50.0))
    if cube is None:
        raise RuntimeError("无法生成 StageA_Ground")
    cube.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"))
    cube.set_actor_scale3d(unreal.Vector(20.0, 20.0, 1.0))
    cube.set_actor_label("StageA_Ground")

    # 保存前重新查询真实关卡内容，强制 PlayerStart/Ground/总 Actor 分别为 1/1/2；其他 Actor 不会被误删，但会让门禁失败，避免把污染地图覆盖落盘。
    final_actors = editor_actors.get_all_level_actors()
    final_player_starts = [actor for actor in final_actors if isinstance(actor, unreal.PlayerStart)]
    final_grounds = [actor for actor in final_actors if actor.get_actor_label() == "StageA_Ground"]
    if len(final_player_starts) != 1 or len(final_grounds) != 1 or len(final_actors) != 2:
        raise RuntimeError(
            f"Lake Actor 计数异常: PlayerStart={len(final_player_starts)} Ground={len(final_grounds)} Total={len(final_actors)}"
        )
    _save_current_map("/Game/Catfishing/Maps/Lake")


def main() -> None:
    """按前台后湖泊顺序生成并保存两张地图，最后显式保存所有脏包以便命令行退出前落盘。"""
    create_frontend()
    create_lake()
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.log("Catfishing stage A maps created")


main()
