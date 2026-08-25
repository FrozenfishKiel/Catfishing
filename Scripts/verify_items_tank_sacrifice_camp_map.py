"""只读核对第三原子模块在 Lake 中的 CampHub、共享鱼缸引用、位置和营地半径。

脚本只通过编辑器对象 API 读取事实并写入唯一 PASS 标记，不保存资源、不启动 PIE，也不替代 Automation 对命令链路的验证。
"""

import unreal


EXPECTED_CAMP_LABEL = "StageC_CampHub"
EXPECTED_TANK_LABEL = "StageC_FishTank"
EXPECTED_CAMP_LOCATION = (-750.0, 350.0, 0.0)
EXPECTED_TANK_LOCATION = (-575.0, 350.0, 0.0)
EXPECTED_RADIUS = 400.0
LOCATION_TOLERANCE = 0.1
RADIUS_TOLERANCE = 0.1


def _get_property(obj, *names):
    """读取一个可能存在命名差异的 Unreal 属性。

    验证脚本按 Python 风格名和 C++ 风格名依次尝试；所有候选都失败时抛出最后一次异常，让日志暴露真实属性缺口而不是吞掉装配问题。
    """
    last_error = None
    for name in names:
        try:
            return obj.get_editor_property(name)
        except Exception as exc:  # noqa: BLE001 - Unreal Python 会抛通用异常，验证脚本需保留最后原因。
            last_error = exc
    raise RuntimeError(f"无法读取 {obj} 的属性 {names}: {last_error}")


def _format_vector(vector) -> str:
    """把编辑器返回的 FVector 压成稳定日志字段。

    Runtime 验证只需要可复查的位置证据；固定一位小数可以避免对象 repr 和浮点噪声影响 PowerShell 对唯一 PASS 标记的检查。
    """
    return f"({vector.x:.1f},{vector.y:.1f},{vector.z:.1f})"


def _require_label(actor, expected_label: str) -> None:
    """核对地图验收使用的稳定 Actor 标签。

    标签是交接和人工复查时定位对象的低成本锚点；数量和引用正确但标签漂移时也要失败，避免后续脚本或人类误认对象。
    """
    actual_label = actor.get_actor_label()
    if actual_label != expected_label:
        raise RuntimeError(f"Actor 标签错误: actual={actual_label} expected={expected_label}")


def _require_location(actor, expected_location) -> None:
    """核对固定营地验收位置，并只允许浮点序列化造成的极小误差。

    A4 关闭依据不是“地图里有任意 Hub/Tank”，而是正式 Lake 中稳定位置的 Hub/Tank 接线；位置漂移会直接改变玩家可达性和人工复查口径。
    """
    actual = actor.get_actor_location()
    actual_values = (actual.x, actual.y, actual.z)
    if any(abs(actual_values[index] - expected_location[index]) > LOCATION_TOLERANCE for index in range(3)):
        raise RuntimeError(
            f"Actor 位置错误: {actor.get_actor_label()} actual={_format_vector(actual)} "
            f"expected=({expected_location[0]:.1f},{expected_location[1]:.1f},{expected_location[2]:.1f})"
        )


def _require_radius(radius: float) -> None:
    """核对当前模块声明的营地交互半径。

    半径是 Lake 玩家入口能否触发营地命令的运行事实；这里只接受本轮 DefaultGame 配置的 400cm，避免未来只凭正数半径误报 A4 完成。
    """
    if abs(radius - EXPECTED_RADIUS) > RADIUS_TOLERANCE:
        raise RuntimeError(f"CampSettings 半径错误: actual={radius:.1f} expected={EXPECTED_RADIUS:.1f}")


def main() -> None:
    """加载 Lake 并读取第三模块需要的固定营地事实。

    流程先只读打开地图，再检查唯一 CampHub 与唯一 FishTank、共享鱼缸引用、营地运行开关、有效半径和两者位置；任一事实缺失都会抛错，只有完整事实成立才写入 PASS 标记。
    """
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.load_level("/Game/Catfishing/Maps/Lake"):
        raise RuntimeError("无法只读加载 Lake 地图")

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    hubs = [actor for actor in actors if isinstance(actor, unreal.CatCampHubActor)]
    tanks = [actor for actor in actors if isinstance(actor, unreal.CatFishTankActor)]
    if len(hubs) != 1 or len(tanks) != 1:
        raise RuntimeError(f"Lake Camp/Tank 数量失败: CampHub={len(hubs)} FishTank={len(tanks)}")

    hub = hubs[0]
    tank = tanks[0]
    _require_label(hub, EXPECTED_CAMP_LABEL)
    _require_label(tank, EXPECTED_TANK_LABEL)
    shared_tank = _get_property(hub, "shared_fish_tank", "SharedFishTank")
    if shared_tank != tank:
        raise RuntimeError(f"CampHub SharedFishTank 未指向唯一鱼缸: actual={shared_tank} expected={tank}")

    settings_class = unreal.load_class(None, "/Script/Catfishing.CatCampSettings")
    if settings_class is None:
        raise RuntimeError("无法加载 CatCampSettings 类")
    settings = unreal.get_default_object(settings_class)
    runtime_enabled = _get_property(settings, "b_enable_camp_runtime", "bEnableCampRuntime")
    radius = float(_get_property(settings, "interaction_radius_centimeters", "InteractionRadiusCentimeters"))
    if not runtime_enabled or radius <= 0.0:
        raise RuntimeError(f"CampSettings 未开放有效半径: Runtime={runtime_enabled} Radius={radius}")
    _require_radius(radius)

    hub_location = hub.get_actor_location()
    tank_location = tank.get_actor_location()
    _require_location(hub, EXPECTED_CAMP_LOCATION)
    _require_location(tank, EXPECTED_TANK_LOCATION)
    unreal.log(
        "ITEMS_TANK_SACRIFICE_CAMP_MAP_PASS "
        f"CampHub={hub.get_actor_label()} HubLocation={_format_vector(hub_location)} "
        f"FishTank={tank.get_actor_label()} TankLocation={_format_vector(tank_location)} "
        f"Radius={radius:.1f}"
    )


main()
