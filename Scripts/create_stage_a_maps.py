"""在真实 Unreal Editor 进程中更新 Fishing 玩家入口所需的 Lake 地图；Frontend 不再由本脚本维护。"""

import unreal


def _save_current_map(package_name: str) -> None:
    """保存已由 LevelEditorSubsystem 创建并命名的当前 World；失败时携带目标包名终止命令行。"""
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.save_current_level():
        raise RuntimeError(f"无法保存地图: {package_name}")


def _set_game_mode(game_mode_path: str) -> None:
    """把当前 WorldSettings 写成给定正式 GameMode；加载失败立即终止，避免保存一个回退到全局默认值的地图。"""
    world = unreal.EditorLevelLibrary.get_editor_world()
    settings = world.get_world_settings()
    game_mode_class = unreal.load_class(None, game_mode_path)
    if game_mode_class is None:
        raise RuntimeError(f"无法加载 GameMode: {game_mode_path}")
    settings.set_editor_property("default_game_mode", game_mode_class)


def _open_or_create_map(package_name: str) -> None:
    """打开已存在的生成资产或创建新地图，使脚本在首次失败后重跑时仍能覆盖同一目标而不删除二进制。"""
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    succeeded = level_editor.load_level(package_name) if unreal.EditorAssetLibrary.does_asset_exist(package_name) else level_editor.new_level(package_name)
    if not succeeded:
        raise RuntimeError(f"无法打开或创建地图: {package_name}")


def create_lake() -> None:
    """幂等装配正式 Lake：清理脚本拥有的旧 Actor，接入 BP GameMode、唯一出生点、岸地、水面和可烘焙水域；全部运行前置成立后才保存。"""
    _open_or_create_map("/Game/Catfishing/Maps/Lake")
    _set_game_mode("/Game/Game/BP_CatFishingGamemode.BP_CatFishingGamemode_C")
    editor_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing_actors = editor_actors.get_all_level_actors()
    # 本脚本只认领稳定 StageA 标签、唯一出生点和正式水域类型；未知表现 Actor 保留，避免重跑覆盖并发资产工作。
    owned_labels = {"StageA_Ground", "StageA_WaterSurface", "StageA_WaterRegion", "StageA_WaterBoundary"}
    owned_actors = [
        actor for actor in existing_actors
        if isinstance(actor, unreal.PlayerStart)
        or isinstance(actor, unreal.CatWaterRegion)
        or isinstance(actor, unreal.CatWaterBoundarySplineActor)
        or actor.get_actor_label() in owned_labels
    ]
    if owned_actors and not editor_actors.destroy_actors(owned_actors):
        raise RuntimeError("无法清理脚本拥有的 Lake Actor")

    player_start = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(-250.0, 0.0, 150.0), unreal.Rotator(0.0, 0.0, 0.0)
    )
    if player_start is None:
        raise RuntimeError("无法生成 StageA_PlayerStart")
    player_start.set_actor_label("StageA_PlayerStart")
    # 先生成明确的 StaticMeshActor，再把 Engine Cube 写入组件；资产对象本身不是可直接生成的 Actor 类。
    cube = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(-500.0, 0.0, -50.0))
    if cube is None:
        raise RuntimeError("无法生成 StageA_Ground")
    cube.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"))
    cube.set_actor_scale3d(unreal.Vector(5.0, 10.0, 1.0))
    cube.set_actor_label("StageA_Ground")

    water_surface = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(800.0, 0.0, -10.0)
    )
    if water_surface is None:
        raise RuntimeError("无法生成 StageA_WaterSurface")
    water_surface.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"))
    water_surface.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    water_surface.set_actor_scale3d(unreal.Vector(8.0, 10.0, 0.1))
    water_surface.set_actor_label("StageA_WaterSurface")

    region = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.CatWaterRegion, unreal.Vector(0.0, 0.0, 0.0))
    boundary = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.CatWaterBoundarySplineActor, unreal.Vector(0.0, 0.0, 0.0)
    )
    if region is None or boundary is None:
        raise RuntimeError("无法生成正式 WaterRegion 或 Boundary")
    region.set_actor_label("StageA_WaterRegion")
    boundary.set_actor_label("StageA_WaterBoundary")
    region.set_editor_property("region_id", "River")
    region.set_editor_property("water_surface_z", 0.0)
    region.set_editor_property("water_point_vertical_tolerance_cm", 200.0)
    region.set_editor_property("bank_height_tolerance_cm", 250.0)
    region.set_editor_property("max_landing_correction_cm", 100.0)
    region.set_editor_property("minimum_water_inset_cm", 10.0)
    boundary.set_editor_property("boundary_id", "RiverMain")
    boundary.set_editor_property("owning_region", region)
    spline = boundary.get_editor_property("boundary_spline")
    spline.clear_spline_points(False)
    for point in (
        unreal.Vector(0.0, -1000.0, 0.0),
        unreal.Vector(1600.0, -1000.0, 0.0),
        unreal.Vector(1600.0, 1000.0, 0.0),
        unreal.Vector(0.0, 1000.0, 0.0),
    ):
        spline.add_spline_point(point, unreal.SplineCoordinateSpace.WORLD, False)
    spline.set_closed_loop(True, True)
    region.set_editor_property("boundary_actors", [boundary])
    # CallInEditor 方法没有自动生成 Python 胶水，必须经反射调用；它会把边界采样结果写回 Region 的烘焙缓存。
    region.call_method("BakeGeometry")

    # 保存前重新读取真实关卡状态；数量、双向所有权和 Bake 任一失败都不允许把不可钓地图落盘。
    final_actors = editor_actors.get_all_level_actors()
    final_player_starts = [actor for actor in final_actors if isinstance(actor, unreal.PlayerStart)]
    final_grounds = [actor for actor in final_actors if actor.get_actor_label() == "StageA_Ground"]
    final_regions = [actor for actor in final_actors if isinstance(actor, unreal.CatWaterRegion)]
    final_boundaries = [actor for actor in final_actors if isinstance(actor, unreal.CatWaterBoundarySplineActor)]
    if (
        len(final_player_starts) != 1
        or len(final_grounds) != 1
        or len(final_regions) != 1
        or len(final_boundaries) != 1
        or not region.has_valid_baked_geometry()
    ):
        raise RuntimeError(
            "Lake 装配异常: "
            f"PlayerStart={len(final_player_starts)} Ground={len(final_grounds)} "
            f"Region={len(final_regions)} Boundary={len(final_boundaries)} "
            f"Baked={region.has_valid_baked_geometry()}"
        )
    _save_current_map("/Game/Catfishing/Maps/Lake")


def main() -> None:
    """只更新本模块拥有的 Lake，并在关卡保存成功后落盘相关脏包；Frontend 由既有资产和 UIReach 独立维护。"""
    create_lake()
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.log("Catfishing stage A maps created")


main()
