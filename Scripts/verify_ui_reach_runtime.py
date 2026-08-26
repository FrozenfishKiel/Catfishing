"""只读核对 UIReach 在正式 Lake 中使用正式 WBP 前端和 MVC 后端入口。

脚本不启动 PIE、不保存资源，也不创建任何玩法交易。Automation 负责验证 LocalPlayer UI 协调器和根 View 的代码合同；
这里补的是运行入口证据：正式 Lake、GameMode/Controller、UI Settings、LakeReach C++ View 基类、配置的 WBP 前端与 MVC 后端类都能被项目运行配置找到。
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


def _same_class(left, right) -> bool:
    """用对象、路径和类名三层身份判断两个 UClass 是否指向同一个反射类。"""
    if left == right:
        return True
    for getter_name in ("get_path_name", "get_full_name", "get_name"):
        if hasattr(left, getter_name) and hasattr(right, getter_name):
            left_value = getattr(left, getter_name)()
            right_value = getattr(right, getter_name)()
            if left_value and left_value == right_value:
                return True
    return False


def _is_child_of(cls, parent) -> bool:
    """沿 UClass 父类链判断继承关系；用于证明 WBP 前端没有脱离正式 C++ View 基类。"""
    if hasattr(cls, "is_child_of"):
        return bool(cls.is_child_of(parent))
    current = cls
    while current:
        if _same_class(current, parent):
            return True
        if hasattr(current, "get_super_struct"):
            current = current.get_super_struct()
            continue
        try:
            current = current.get_editor_property("super_struct")
        except Exception:  # noqa: BLE001 - UE Python 对不存在的反射字段没有专用异常类型。
            current = None
    return False


def _class_chain_names(cls) -> str:
    """把 UE Python 可见的父类链压成日志字符串；Runtime 失败时用它定位反射对象差异。"""
    names = []
    current = cls
    while current:
        names.append(current.get_name() if hasattr(current, "get_name") else str(current))
        if hasattr(current, "get_super_struct"):
            current = current.get_super_struct()
            continue
        try:
            current = current.get_editor_property("super_struct")
        except Exception:  # noqa: BLE001 - UE Python 对不存在的反射字段没有专用异常类型。
            current = None
    return " -> ".join(names)


def _is_default_object_instance_of(cls, unreal_type_name: str) -> bool:
    """用蓝图生成类 CDO 的 Python 类型证明它能被当作指定 C++ View 基类实例使用。"""
    unreal_type = getattr(unreal, unreal_type_name, None)
    if unreal_type is None:
        return False
    return isinstance(unreal.get_default_object(cls), unreal_type)


def main() -> None:
    """执行 UIReach Lake Runtime 探针。

    先只读加载 Lake 并核对正式框架类，再读取 UI Settings 与 UIReach 类反射。
    PASS 标记输出关键类名、WBP 配置和地图事实，供 PowerShell 模式确认本轮证据不是旧日志或空跑。
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
    enabled = bool(_get_property(ui_settings, "b_enable_lake_reach_view", "bEnableLakeReachView"))
    configured_wbp = _as_name(_get_property(ui_settings, "lake_reach_widget_class", "LakeReachWidgetClass"))
    menu_action_path = _as_name(_get_property(ui_settings, "lake_menu_toggle_action", "LakeMenuToggleAction"))
    menu_context_path = _as_name(_get_property(ui_settings, "lake_menu_input_mapping_context", "LakeMenuInputMappingContext"))
    _require(enabled, "LakeReach 正式 WBP View 应在默认配置中允许装配")
    _require("WBP_CatLakeReach" in configured_wbp, f"LakeReach 默认 WBP 配置异常: {configured_wbp}")
    _require("IA_LakeMenu" in menu_action_path, f"Lake 菜单 Action 配置异常: {menu_action_path}")
    _require("IMC_InputContext" in menu_context_path, f"Lake 菜单 IMC 配置异常: {menu_context_path}")
    _require(unreal.load_asset("/Game/Input/InputAction/IA_LakeMenu") is not None, "无法加载 IA_LakeMenu 输入资产")
    _require(unreal.load_asset("/Game/Input/InputContext/IMC_InputContext") is not None, "无法加载 IMC_InputContext 输入资产")

    local_ui_class = _load_class("/Script/Catfishing.CatLocalPlayerUISubsystem")
    model_class = _load_class("/Script/Catfishing.CatLakeReachModel")
    page_controller_class = _load_class("/Script/Catfishing.CatLakeReachPageController")
    lake_reach_base_class = _load_class("/Script/Catfishing.CatLakeReachWidget")
    lake_reach_wbp_class = _load_class("/Game/UI/WBP_CatLakeReach.WBP_CatLakeReach_C")
    _require("CatLocalPlayerUISubsystem" in local_ui_class.get_name(), "UI 根生命周期类未加载到项目模块")
    _require("CatLakeReachModel" in model_class.get_name(), "UIReach Model 类未加载到项目模块")
    _require("CatLakeReachPageController" in page_controller_class.get_name(), "UIReach PageController 类未加载到项目模块")
    _require("CatLakeReachWidget" in lake_reach_base_class.get_name(), "LakeReach C++ View 基类未加载到项目模块")
    _require("WBP_CatLakeReach_C" in lake_reach_wbp_class.get_name(), "LakeReach WBP 前端类未加载")
    _require(lake_reach_wbp_class != lake_reach_base_class, "LakeReach 玩家前端不能是原生 C++ View 基类")
    wbp_inherits_lake_reach_base = (
        _is_child_of(lake_reach_wbp_class, lake_reach_base_class)
        or _is_default_object_instance_of(lake_reach_wbp_class, "CatLakeReachWidget")
    )
    _require(
        wbp_inherits_lake_reach_base,
        "LakeReach WBP 前端没有继承正式 View 基类: "
        f"chain={_class_chain_names(lake_reach_wbp_class)} parent={lake_reach_base_class.get_name()}",
    )

    unreal.log(
        "UI_REACH_RUNTIME_PASS "
        f"LakeGameMode={actual_game_mode} Controller={controller_class} "
        f"LocalUI={local_ui_class.get_name()} Model={model_class.get_name()} PageController={page_controller_class.get_name()} "
        f"RootViewBase={lake_reach_base_class.get_name()} RootClass={lake_reach_wbp_class.get_name()} "
        f"LakeReachDefault=Enabled ConfiguredWBP={configured_wbp} "
        f"MenuAction={menu_action_path} MenuContext={menu_context_path} "
        f"PlayerStarts={len(player_starts)} Regions={len(regions)}"
    )


main()
