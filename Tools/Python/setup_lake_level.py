"""在真实 Unreal Editor 进程中给 Lake 摆放玩法宿主 Actor（水域 / 营地 / 共享鱼缸 / 地面 / 出生点）。

为什么需要这个脚本：`Lake.umap` 原本只有一个出生点和一块 20m 见方的地面，没有任何玩法宿主，于是
水域查询恒 `RegionNotFound`（钓鱼与投窝全拒）、营地四条命令没有宿主、共享鱼缸在运行时根本不存在。
这些 Actor 的位置和尺寸是**关卡事实**，不是配置，只能落在 umap 里，所以用脚本生成而不是写 ini。

与 `Scripts/create_stage_a_maps.py` 的关系：那个脚本有"Lake 总 Actor 必须等于 2"的门禁，摆了新 Actor
以后它会失败——它属于阶段 A，已经完成使命，不要再对 Lake 跑它。本脚本接管 Lake 的内容。

幂等：按 Actor 标签认领已有实例，存在就复用并重设属性，不存在才生成；重复运行不会累积 Actor。

跑法（编辑器不能同时开着）：
    & 'D:\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' 'D:\\UnreaProjects\\Catfishing\\Catfishing.uproject' ^
      -run=pythonscript -script="D:\\UnreaProjects\\Catfishing\\Tools\\Python\\setup_lake_level.py" -unattended -nosplash

几何口径（**工程暂定的灰盒布局**，不是策划拍定的关卡设计，见 Docs/Development/工程自补决策记录.md D-11）：
单位是厘米（UE 默认），1 米 = 100。地面顶面在 z=0。

  ┌─────────────────────────────────────────┐  +Y
  │  River      Y ∈ [ +500, +4500]          │
  ├─────────────────────────────────────────┤
  │  营地 / 出生点   Y ∈ [-2000, +500]       │
  ├─────────────────────────────────────────┤
  │  ForestLake Y ∈ [-6000, -2000]          │  -Y
  └─────────────────────────────────────────┘

两片水域**不重叠**——`UCatWaterQuerySubsystem::QueryWaterRegion` 要求唯一命中，重叠会被判 `AmbiguousRegion`。

水域包围盒**刻意向岸上多延伸一段**：投窝的可达判定（`IsChumDropReachable`）要求"落点与投掷者同属一片水域"，
如果盒子只盖住水面，站在岸上的猫就永远投不了窝。灰盒阶段用一个盒子同时表示"水面 + 可投窝的岸边"，
等有了真实岸线几何再拆开。
"""

import unreal

LAKE_PACKAGE = "/Game/Catfishing/Maps/Lake"

# 水域几何：(标签, RegionId, 包围盒世界中心, 包围盒半尺寸)。RegionId 必须与鱼表「出没地点」列一致，
# 当前 12 条鱼全部含 River、其中 8 条同时含 ForestLake；写错名字的后果是那片水里一条鱼也抽不出来。
#
# 注意"世界中心"写进的是 `LocalCenterOffset` 而不是 Actor 位置：`ACatWaterRegion` 构造时**没有创建根组件**，
# 因此它的 `GetActorLocation()` 永远是原点，`SetActorLocation` 无效——几何只能靠偏移表达。
# 这是代码侧的一个可用性缺陷（关卡作者没法在编辑器里拖动水域），已登记在需求对齐差距清单，
# 修好之前本脚本按"Actor 留在原点 + 偏移承载中心"的方式写，与 `ContainsWorldPoint` 的实际算法一致。
WATER_REGIONS = [
    ("Lake_WaterRegion_River", "River", unreal.Vector(0.0, 2500.0, 0.0), unreal.Vector(6000.0, 2000.0, 400.0)),
    ("Lake_WaterRegion_ForestLake", "ForestLake", unreal.Vector(0.0, -4000.0, 0.0), unreal.Vector(2500.0, 2000.0, 400.0)),
]

# 水面视觉占位：没有水体材质，就用压扁的引擎方块标出水面位置，否则 PIE 里分不清哪里是水。
# 这些只是看的，玩法判定一律走 ACatWaterRegion 的包围盒。
WATER_VISUALS = [
    ("Lake_WaterVisual_River", unreal.Vector(0.0, 3000.0, -10.0), unreal.Vector(120.0, 30.0, 0.2)),
    ("Lake_WaterVisual_ForestLake", unreal.Vector(0.0, -4000.0, -10.0), unreal.Vector(50.0, 40.0, 0.2)),
]

GROUND_LABEL = "StageA_Ground"
PLAYER_START_LABEL = "StageA_PlayerStart"
CAMP_LABEL = "Lake_CampHub"
TANK_LABEL = "Lake_SharedFishTank"

# 采光：Lake 原本一盏灯都没有，PIE 里整个世界是纯黑的，只看得见 HUD 文字——玩法跑得再对也没法用眼睛验收。
# 这三个是"能看见东西"的最小集合：平行光给方向性照明与阴影，天光给环境补光（否则背光面全黑），
# 大气给天空颜色（否则天是黑的，分不清地平线）。灯光参数属于美术，这里只保证可见，不做打光设计。
LIGHT_LABEL_SUN = "Lake_Light_Sun"
LIGHT_LABEL_SKY = "Lake_Light_Sky"
LIGHT_LABEL_ATMOSPHERE = "Lake_SkyAtmosphere"


def _actors_by_label():
    """把当前关卡里的 Actor 按标签建索引，供后续认领已有实例；重复标签只保留先遇到的一个。"""
    editor_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    index = {}
    for actor in editor_actors.get_all_level_actors():
        label = actor.get_actor_label()
        if label not in index:
            index[label] = actor
    return index


def _claim(index, label, actor_class, location):
    """按标签认领一个 Actor：已存在就移到目标位置复用，不存在才生成并打上标签。

    复用而不是"先删后建"，是因为营地对鱼缸的引用是按 Actor 指针存的；删掉重建会把关卡里已有的
    引用打断，重跑一次脚本就会静默失去共享鱼缸。
    """
    existing = index.get(label)
    if existing is not None:
        existing.set_actor_location(location, False, False)
        return existing, False
    editor_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    spawned = editor_actors.spawn_actor_from_class(actor_class, location, unreal.Rotator(0.0, 0.0, 0.0))
    if spawned is None:
        raise RuntimeError(f"无法生成 Actor: {label} ({actor_class})")
    spawned.set_actor_label(label)
    index[label] = spawned
    return spawned, True


def _claim_static_mesh(index, label, location, scale, mesh_path="/Engine/BasicShapes/Cube.Cube"):
    """认领一个静态网格占位体并重设网格与缩放；纯视觉，不参与任何玩法判定。"""
    actor, _ = _claim(index, label, unreal.StaticMeshActor, location)
    actor.static_mesh_component.set_static_mesh(unreal.load_asset(mesh_path))
    actor.set_actor_scale3d(scale)
    return actor


def setup_water_regions(index):
    """摆放两片水域并写入运行必需的四项配置；任一项缺失都会让 `IsRuntimeConfigured` 判否、水域查询直接落空。"""
    regions = []
    for label, region_id, world_center, half_extent in WATER_REGIONS:
        region, _ = _claim(index, label, unreal.CatWaterRegion, unreal.Vector(0.0, 0.0, 0.0))
        region.set_editor_property("region_id", region_id)
        # 显式 gate：关卡作者确认这个 AABB 可以当灰盒岸线用，代码默认不敢把 Actor 位置当水面。
        region.set_editor_property("enable_prototype_bounds", True)
        # 世界中心全部走偏移（见文件头与 WATER_REGIONS 的说明：这个 Actor 没有根组件，位置写不进去）。
        region.set_editor_property("local_center_offset", world_center)
        region.set_editor_property("half_extent", half_extent)
        # Revision 只随关卡作者改几何而变；运行时不推进它。改了盒子请手动 +1，让在途命令能拒绝陈旧命中。
        region.set_editor_property("region_revision", 1)
        regions.append(region)
    return regions


def setup_camp(index):
    """摆放营地与共享鱼缸，并把鱼缸引用写进营地；引用为空时"鱼入缸"会 fail-closed，不会临时生成鱼缸。"""
    # 鱼缸同样没有根组件（只有一个非场景的容器复制组件），位置写不进去、编辑器里也看不见；
    # 玩法上它只被营地按指针引用、按营地半径判定，所以位置不影响正确性。同样登记为待修的可用性缺陷。
    tank, _ = _claim(index, TANK_LABEL, unreal.CatFishTankActor, unreal.Vector(0.0, 0.0, 0.0))
    camp, _ = _claim(index, CAMP_LABEL, unreal.CatCampHubActor, unreal.Vector(-900.0, -600.0, 0.0))
    camp.set_editor_property("shared_fish_tank", tank)
    return camp, tank


def setup_ground_and_start(index):
    """把地面扩到能容下两片水域与营地，并保证出生点站在营地一侧的干地上。"""
    # 地面：引擎方块原始边长 100，缩放 140 → 140m 见方，覆盖 Y ∈ [-7000, +5000] 的全部内容。
    _claim_static_mesh(index, GROUND_LABEL, unreal.Vector(0.0, -1000.0, -50.0), unreal.Vector(140.0, 140.0, 1.0))
    start, _ = _claim(index, PLAYER_START_LABEL, unreal.PlayerStart, unreal.Vector(0.0, -600.0, 150.0))
    return start


def setup_lighting(index):
    """补齐"能看见东西"的最小灯光集合；缺它们时 PIE 里除了 HUD 文字之外全黑，人工验收无从下手。"""
    sun, _ = _claim(index, LIGHT_LABEL_SUN, unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 1000.0))
    # 俯角 -45°、偏航 -60°：让地面和立方体各面都有明暗差，纯顶光会把占位几何压成一片死白。
    sun.set_actor_rotation(unreal.Rotator(0.0, -45.0, -60.0), False)
    sky, _ = _claim(index, LIGHT_LABEL_SKY, unreal.SkyLight, unreal.Vector(0.0, 0.0, 1000.0))
    atmosphere, _ = _claim(index, LIGHT_LABEL_ATMOSPHERE, unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0))
    return sun, sky, atmosphere


def verify(index_after):
    """保存前重新查询真实关卡内容做自检；这里宁可让脚本失败，也不要把只对了一半的地图覆盖落盘。"""
    editor_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = editor_actors.get_all_level_actors()

    regions = [a for a in actors if isinstance(a, unreal.CatWaterRegion)]
    camps = [a for a in actors if isinstance(a, unreal.CatCampHubActor)]
    tanks = [a for a in actors if isinstance(a, unreal.CatFishTankActor)]
    starts = [a for a in actors if isinstance(a, unreal.PlayerStart)]

    if len(regions) != 2:
        raise RuntimeError(f"水域数量异常: {len(regions)}（期望 2）")
    if len(camps) != 1 or len(tanks) != 1:
        raise RuntimeError(f"营地/鱼缸数量异常: camp={len(camps)} tank={len(tanks)}（各期望 1）")
    if len(starts) != 1:
        raise RuntimeError(f"出生点数量异常: {len(starts)}（期望 1）")

    region_ids = sorted(str(r.get_editor_property("region_id")) for r in regions)
    if region_ids != ["ForestLake", "River"]:
        raise RuntimeError(f"RegionId 与鱼表不一致: {region_ids}")

    for region in regions:
        if not region.get_editor_property("enable_prototype_bounds"):
            raise RuntimeError(f"{region.get_actor_label()} 未开启 prototype 包围盒 gate")
        if int(region.get_editor_property("region_revision")) < 1:
            raise RuntimeError(f"{region.get_actor_label()} 的 RegionRevision 未接线")
        extent = region.get_editor_property("half_extent")
        if min(extent.x, extent.y, extent.z) <= 0.0:
            raise RuntimeError(f"{region.get_actor_label()} 的 HalfExtent 非正: {extent}")

    if camps[0].get_editor_property("shared_fish_tank") is None:
        raise RuntimeError("营地没有引用共享鱼缸，鱼入缸会 fail-closed")

    # 灯光缺失不影响玩法判定，但会让人工验收退化成"只能读日志"，所以一并当作硬条件。
    if not any(isinstance(a, unreal.DirectionalLight) for a in actors):
        raise RuntimeError("关卡没有平行光，PIE 会是全黑的")
    if not any(isinstance(a, unreal.SkyLight) for a in actors):
        raise RuntimeError("关卡没有天光，背光面会全黑")

    # 世界 AABB 的算法与 `ACatWaterRegion::ContainsWorldPoint` 保持一致：Actor 位置 + 偏移。
    def world_span_y(region):
        center_y = region.get_actor_location().y + region.get_editor_property("local_center_offset").y
        half_y = region.get_editor_property("half_extent").y
        return center_y - half_y, center_y + half_y

    # 两片水域的 Y 区间不能重叠，否则水域查询会判 AmbiguousRegion，钓鱼与投窝一起失效。
    spans = sorted(world_span_y(region) for region in regions)
    if spans[0][1] >= spans[1][0]:
        raise RuntimeError(f"两片水域 Y 区间重叠: {spans}")

    # 出生点必须落在两片水域之外，否则一进游戏就站在"水里"，Cast 与投窝的语义会失真。
    start_location = starts[0].get_actor_location()
    for region in regions:
        low, high = world_span_y(region)
        if low <= start_location.y <= high:
            raise RuntimeError(f"出生点落在水域 {region.get_actor_label()} 内: y={start_location.y}")

    unreal.log(
        "Catfishing lake layout verified: "
        f"regions={region_ids} camp=1 tank=1 start=1 total_actors={len(actors)}"
    )


def main():
    """打开 Lake、按标签幂等摆放全部玩法宿主、自检通过后保存关卡与脏包。"""
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not unreal.EditorAssetLibrary.does_asset_exist(LAKE_PACKAGE):
        raise RuntimeError(f"Lake 地图不存在: {LAKE_PACKAGE}")
    if not level_editor.load_level(LAKE_PACKAGE):
        raise RuntimeError(f"无法打开地图: {LAKE_PACKAGE}")

    index = _actors_by_label()
    setup_ground_and_start(index)
    setup_water_regions(index)
    setup_camp(index)
    setup_lighting(index)
    for label, location, scale in WATER_VISUALS:
        _claim_static_mesh(index, label, location, scale)

    verify(index)

    if not level_editor.save_current_level():
        raise RuntimeError(f"无法保存地图: {LAKE_PACKAGE}")
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.log("Catfishing lake layout saved")


main()
