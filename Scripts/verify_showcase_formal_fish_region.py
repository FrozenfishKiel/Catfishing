"""只读验证 Showcase2 已接入正式 River 鱼库所需的水域合同。"""

import unreal


MAP_PATH = "/Game/NaturePackage/Maps/Showcase2"


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def _normalized(value) -> str:
    return str(value).strip().lower()


def main() -> None:
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    _require(level_editor.load_level(MAP_PATH), f"无法加载地图: {MAP_PATH}")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    regions = [actor for actor in actors if isinstance(actor, unreal.CatWaterRegion)]
    boundaries = [actor for actor in actors if isinstance(actor, unreal.CatWaterBoundarySplineActor)]
    anchors = [actor for actor in actors if isinstance(actor, unreal.CatChumFieldAnchor)]
    _require(len(regions) == 1, f"Showcase2 水域数量错误: {len(regions)}")
    _require(len(boundaries) == 1, f"Showcase2 边界数量错误: {len(boundaries)}")
    _require(len(anchors) == 0, f"Showcase2 聚鱼锚点数量错误: {len(anchors)}")

    region = regions[0]
    boundary = boundaries[0]
    _require(_normalized(region.get_editor_property("region_id")) == "river", "Showcase2 RegionId 不是 River")
    _require(boundary.get_editor_property("owning_region") == region, "边界 OwningRegion 未指向唯一水域")
    _require(boundary in region.get_editor_property("boundary_actors"), "水域 BoundaryActors 未包含唯一边界")
    _require(region.has_valid_baked_geometry(), "Showcase2 River 水域没有有效烘焙缓存")

    handle = region.get_water_region_handle()
    handle_region = _normalized(handle.get_editor_property("region_id"))
    revision = int(handle.get_editor_property("geometry_revision"))
    _require(handle_region == "river" and revision > 0, "Showcase2 River 水域 Handle 无效")
    unreal.log(
        "SHOWCASE_FORMAL_FISH_REGION_VERIFY_PASS "
        f"Map={MAP_PATH} Region=River GeometryRevision={revision} "
        f"Regions={len(regions)} Boundaries={len(boundaries)} Anchors={len(anchors)}"
    )


main()
