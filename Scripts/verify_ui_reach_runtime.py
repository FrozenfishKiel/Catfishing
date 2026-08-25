"""只读核对 UIReach 在正式 Lake 中默认不把白盒根暴露给玩家。

脚本不启动 PIE、不保存资源，也不创建任何玩法交易。Automation 负责验证 LocalPlayer UI 协调器和根 View 的代码合同；
这里补的是运行入口证据：正式 Lake、GameMode/Controller、UI Settings、LakeReach 根 View 与 Fishing 只读桥都能被项目运行配置找到，
同时正式默认配置不会自动装配 LakeReach 白盒状态 View。
"""

import unreal


def _get_property(obj, *names):
    """按候选名顺序读取 Unreal 属性。

    Runtime 探针同时兼容 Python snake_case 和 C++ CamelCase 暴露名；每个候选读取失败后继续尝试下一个，
    全部失败时把最后一次 Unreal 异常带入 RuntimeError，让证据日志明确是字段缺失而不是入口通过。
    """
    last_error = None
    for name in names:
        try:
            return obj.get_editor_property(name)
        except Exception as exc:  # noqa: BLE001 - Unreal Python 对字段读取失败会抛通用异常。
            last_error = exc
    raise RuntimeError(f"无法读取 {obj} 的属性 {names}: {last_error}")


def _as_name(value) -> str:
    """把 FName、枚举或 SoftObjectPath 转成稳定字符串，方便日志和等值判断。"""
    return str(value).strip()


def _require(condition: bool, message: str) -> None:
    """统一失败出口；只有明确事实成立时才允许继续写 PASS 标记。"""
    if not condition:
        raise RuntimeError(message)


def _load_class(path: str):
    """按脚本路径加载 C++ 或蓝图类；失败立即抛错，避免把缺类写成可用入口。"""
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError(f"无法加载类: {path}")
    return cls


def main() -> None:
    """执行 UIReach Lake Runtime 探针。

    先只读加载 Lake 并核对正式框架类，再读取 UI Settings 与 UIReach 类反射。
    PASS 标记输出关键类名、默认可见性和地图事实，供 PowerShell 模式确认本轮证据不是旧日志或空跑。
    """
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.load_level("/Game/Catfishing/Maps/Lake"):
        raise RuntimeError("无法只读加载 Lake 地图")

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    player_starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    regions = [actor for actor in actors if isinstance(actor, unreal.CatWaterRegion)]
    _require(len(player_starts) == 1, f"Lake PlayerStart 数量异常: {len(player_starts)}")
    _require(len(regions) >= 1, f"Lake 缺少正式水域: {len(regions)}")

    world_settings = unreal.EditorLevelLibrary.get_editor_world().get_world_settings()
    actual_game_mode = world_settings.get_editor_property("default_game_mode")
    expected_game_mode = _load_class("/Game/Game/BP_CatFishingGamemode.BP_CatFishingGamemode_C")
    _require(actual_game_mode == expected_game_mode, f"Lake GameMode 不匹配: actual={actual_game_mode} expected={expected_game_mode}")

    game_mode_cdo = unreal.get_default_object(expected_game_mode)
    controller_class = game_mode_cdo.get_editor_property("player_controller_class")
    expected_controller = _load_class("/Game/Player/BP_CatFishingController.BP_CatFishingController_C")
    _require(controller_class == expected_controller, f"Lake Controller 不匹配: actual={controller_class} expected={expected_controller}")

    ui_settings_class = _load_class("/Script/Catfishing.CatUISettings")
    ui_settings = unreal.get_default_object(ui_settings_class)
    enabled = bool(_get_property(ui_settings, "b_enable_lake_status_view", "bEnableLakeStatusView"))
    menu_key = _as_name(_get_property(ui_settings, "lake_menu_toggle_key_name", "LakeMenuToggleKeyName"))
    menu_priority = int(_get_property(ui_settings, "lake_menu_input_priority", "LakeMenuInputPriority"))
    _require(not enabled, "LakeReach 白盒状态 View 不应在正式默认配置中启用")
    _require(menu_key not in ("", "None"), f"Lake 菜单键名无效: {menu_key}")
    _require(menu_priority >= 0, f"Lake 菜单输入优先级无效: {menu_priority}")

    local_ui_class = _load_class("/Script/Catfishing.CatLocalPlayerUISubsystem")
    lake_reach_class = _load_class("/Script/Catfishing.CatLakeReachWidget")
    fishing_bridge_class = _load_class("/Script/Catfishing.CatFishingViewBridge")
    _require("CatLakeReachWidget" in lake_reach_class.get_name(), "LakeReach 根 View 类未加载到项目模块")
    _require("CatLocalPlayerUISubsystem" in local_ui_class.get_name(), "UI 协调器类未加载到项目模块")
    _require("CatFishingViewBridge" in fishing_bridge_class.get_name(), "Fishing 只读桥类未加载到项目模块")

    unreal.log(
        "UI_REACH_RUNTIME_PASS "
        f"LakeGameMode={actual_game_mode} Controller={controller_class} "
        f"LocalUI={local_ui_class.get_name()} RootView={lake_reach_class.get_name()} "
        f"FishingBridge={fishing_bridge_class.get_name()} LakeStatusDefault=Disabled "
        f"MenuKey={menu_key} MenuPriority={menu_priority} "
        f"PlayerStarts={len(player_starts)} Regions={len(regions)}"
    )


main()
