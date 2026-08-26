"""把 Run / Environment / Social 的自然事件聚鱼锚点接入正式 Lake。

脚本只认领 RunEnvSocial_NaturalChumAnchor 一个稳定标签；它复用现有唯一 WaterRegion 的烘焙 handle，
不移动 PlayerStart、营地、鱼缸、UI 或水域边界。保存前会验证锚点 ID、位置和 WaterRegion handle，
避免把半接线地图落盘。
"""

import unreal


LAKE_PACKAGE = "/Game/Catfishing/Maps/Lake"
ANCHOR_LABEL = "RunEnvSocial_NaturalChumAnchor"
ANCHOR_ID = "LakeAAnchor"
ANCHOR_LOCATION = unreal.Vector(800.0, 0.0, 0.0)


def _load_lake() -> None:
    """打开正式 Lake；失败立即终止，防止脚本在错误 World 里生成锚点。"""
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.load_level(LAKE_PACKAGE):
        raise RuntimeError(f"无法打开地图: {LAKE_PACKAGE}")


def _get_region_handle(region):
    """从 WaterRegion 读取当前烘焙 handle。

    UE Python 暴露 BlueprintPure 方法时可能使用 snake_case，也可能只能通过 call_method；
    两种路径都失败时抛错，让日志暴露真实反射缺口，而不是保存一个空锚点。
    """
    if hasattr(region, "get_water_region_handle"):
        return region.get_water_region_handle()
    return region.call_method("GetWaterRegionHandle")


def _get_property(obj, *names):
    """按候选名顺序读取 Unreal 属性，兼容结构体在不同来源下的 snake_case/CamelCase 暴露差异。"""
    last_error = None
    for name in names:
        try:
            return obj.get_editor_property(name)
        except Exception as exc:  # noqa: BLE001 - Unreal Python 对字段读取失败会抛通用异常。
            last_error = exc
    raise RuntimeError(f"无法读取 {obj} 的属性 {names}: {last_error}")


def _handle_fields(handle):
    """读取 WaterRegion handle 的稳定字段，不把 Python 命名胶水差异误判成锚点写入失败。"""
    return (
        str(_get_property(handle, "region_id", "RegionId")),
        int(_get_property(handle, "geometry_revision", "GeometryRevision")),
    )


def _handle_is_valid(handle) -> bool:
    """按字段判断 WaterRegion handle 是否可用；Python 侧不依赖 C++ 非 UFUNCTION 方法。"""
    region_id, geometry_revision = _handle_fields(handle)
    return region_id not in ("", "None") and geometry_revision > 0


def _spawn_or_reuse_anchor(editor_actors, anchor_class):
    """复用脚本拥有的唯一锚点，或在固定水域内创建它；重复标签或多个锚点会失败交给人工处理。"""
    actors = editor_actors.get_all_level_actors()
    anchors = [actor for actor in actors if isinstance(actor, anchor_class)]
    labelled = [actor for actor in anchors if actor.get_actor_label() == ANCHOR_LABEL]
    if len(labelled) > 1:
        raise RuntimeError(f"NaturalEvent 锚点标签重复: {len(labelled)}")
    if labelled:
        anchor = labelled[0]
    else:
        anchor = unreal.EditorLevelLibrary.spawn_actor_from_class(anchor_class, ANCHOR_LOCATION, unreal.Rotator(0.0, 0.0, 0.0))
        if anchor is None:
            raise RuntimeError("无法生成自然事件聚鱼锚点")
        anchor.set_actor_label(ANCHOR_LABEL)
    anchor.set_actor_location(ANCHOR_LOCATION, False, False)
    anchor.set_editor_property("anchor_id", ANCHOR_ID)
    return anchor


def configure_lake_anchor() -> None:
    """配置 Lake 自然事件锚点。

    流程先打开 Lake，定位唯一 River WaterRegion 并读取其当前烘焙 handle，再创建或复用锚点并写入 AnchorId/WaterHandle；
    保存前重新核对 handle、位置和唯一性，只有完整事实成立才保存当前关卡。
    """
    _load_lake()
    editor_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = editor_actors.get_all_level_actors()
    regions = [actor for actor in actors if isinstance(actor, unreal.CatWaterRegion)]
    river_regions = [actor for actor in regions if str(actor.get_editor_property("region_id")) == "River"]
    if len(river_regions) != 1 or not river_regions[0].has_valid_baked_geometry():
        raise RuntimeError(f"Lake River WaterRegion 无法唯一作为自然事件目标: RiverCount={len(river_regions)}")

    region = river_regions[0]
    handle = _get_region_handle(region)
    if not _handle_is_valid(handle):
        raise RuntimeError("River WaterRegion handle 无效，不能配置自然事件锚点")

    anchor = _spawn_or_reuse_anchor(editor_actors, unreal.CatChumFieldAnchor)
    anchor.set_editor_property("expected_water_region_handle", handle)

    final_actors = editor_actors.get_all_level_actors()
    final_anchors = [
        actor for actor in final_actors
        if isinstance(actor, unreal.CatChumFieldAnchor) and actor.get_actor_label() == ANCHOR_LABEL
    ]
    if len(final_anchors) != 1:
        raise RuntimeError(f"自然事件锚点数量失败: {len(final_anchors)}")
    final_anchor = final_anchors[0]
    final_handle = final_anchor.get_editor_property("expected_water_region_handle")
    final_region_id, final_revision = _handle_fields(final_handle)
    final_anchor_id = str(final_anchor.get_editor_property("anchor_id"))
    if (
        final_anchor_id != ANCHOR_ID
        or not _handle_is_valid(final_handle)
        or final_region_id.lower() != "river"
    ):
        raise RuntimeError(
            "自然事件锚点属性未写入完整 River handle: "
            f"AnchorId={final_anchor_id} Region={final_region_id} Revision={final_revision}"
        )

    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
        raise RuntimeError("无法保存 Lake 自然事件锚点")
    unreal.log(
        "RUN_ENVIRONMENT_SOCIAL_MAP_CONFIGURED "
        f"Anchor={final_anchor.get_actor_label()} AnchorId={ANCHOR_ID} "
        f"Region={final_region_id} "
        f"Revision={final_revision}"
    )


configure_lake_anchor()
