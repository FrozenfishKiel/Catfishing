"""只读核对 Run / Environment / Social 在正式 Lake 和项目配置中的运行闭环事实。

脚本不启动 PIE、不保存资源，也不模拟玩家操作。它验证的是本模块能进入运行面的前置事实：
RunFlow/成功结算策略、Environment 自然事件、ChumField 锚点、Social 三条正向 gate、
正式 Lake 的 GameMode/Controller 和唯一 River WaterRegion 都能从当前项目配置与地图中读到。
"""

import pathlib

import unreal


EXPECTED_GAME_MODE = "/Game/Game/BP_CatFishingGamemode.BP_CatFishingGamemode_C"
EXPECTED_CONTROLLER = "/Game/Player/BP_CatFishingController.BP_CatFishingController_C"
EXPECTED_RUN_FLOW = "/Game/Data/StateTrees/ST_RunFlow.ST_RunFlow"
EXPECTED_ANCHOR_LABEL = "RunEnvSocial_NaturalChumAnchor"
EXPECTED_ANCHOR_ID = "LakeAAnchor"
EXPECTED_EVENT_ID = "RainBloom"
EXPECTED_NATURAL_CHUM = "Chum_Basic"
EXPECTED_WATER_REGION = "River"
CONFIG_PATH = pathlib.Path(unreal.Paths.project_config_dir()) / "DefaultGame.ini"
RUN_SECTION = "/Script/Catfishing.CatRunSettings"
ENVIRONMENT_SECTION = "/Script/Catfishing.CatEnvironmentSettings"
SOCIAL_SECTION = "/Script/Catfishing.CatSocialSettings"


def _get_property(obj, *names):
    """按候选名顺序读取 Unreal 属性。

    Runtime 探针同时兼容 Python snake_case 与 C++ CamelCase 暴露名；全部失败时抛出最后一个异常，
    让日志明确暴露字段缺口，而不是把缺配置误写成通过。
    """
    last_error = None
    for name in names:
        try:
            return obj.get_editor_property(name)
        except Exception as exc:  # noqa: BLE001 - Unreal Python 对字段读取失败会抛通用异常。
            last_error = exc
    raise RuntimeError(f"无法读取 {obj} 的属性 {names}: {last_error}")


def _require(condition: bool, message: str) -> None:
    """统一失败出口；只有明确事实成立时才允许继续写 PASS 标记。"""
    if not condition:
        raise RuntimeError(message)


def _load_class(path: str):
    """加载 C++ 或蓝图类；缺类立即失败，避免把软引用空值当成运行时可用。"""
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError(f"无法加载类: {path}")
    return cls


def _enum_text(value) -> str:
    """把 Unreal enum 值压成稳定字符串，便于同时兼容原始名和带命名空间的显示名。"""
    return str(value).split("::")[-1].split(".")[-1]


def _config_value(section: str, key: str) -> str:
    """从 DefaultGame.ini 读取简单 key=value 配置，专门绕开 Unreal Python 对项目 enum 的转换缺陷。"""
    current_section = ""
    target_key = key.lower()
    with CONFIG_PATH.open("r", encoding="utf-8-sig") as handle:
        for line in handle:
            stripped = line.strip()
            if not stripped or stripped.startswith(";") or stripped.startswith("#"):
                continue
            if stripped.startswith("[") and stripped.endswith("]"):
                current_section = stripped[1:-1]
                continue
            if current_section == section:
                raw_key, separator, raw_value = stripped.partition("=")
                if separator and raw_key.lstrip("+").strip().lower() == target_key:
                    return raw_value.strip()
    raise RuntimeError(f"DefaultGame.ini 缺少配置: [{section}] {key}")


def _config_enum(section: str, key: str) -> str:
    """读取 enum 配置文本并压成稳定尾名；DefaultGame 中的 Enabled/FixedQuotaTarget 可直接比较。"""
    return _enum_text(_config_value(section, key))


def _soft_path(value) -> str:
    """把 SoftObjectPath / SoftObjectPtr / 资产对象转成稳定路径字符串。"""
    try:
        return str(value.to_soft_object_path())
    except Exception:  # noqa: BLE001 - Unreal 软引用类型在不同版本 Python 胶水里接口不一致。
        return str(value)


def _handle_fields(handle):
    """读取 WaterRegion handle 的 RegionId 与 GeometryRevision，不依赖 C++ 非 UFUNCTION 方法。"""
    return (
        str(_get_property(handle, "region_id", "RegionId")),
        int(_get_property(handle, "geometry_revision", "GeometryRevision")),
    )


def _validate_lake() -> tuple[int, int]:
    """加载 Lake 并核对正式 GameMode、Controller、River 水域和自然事件锚点。

    地图事实是本模块 Runtime 证据的核心：Environment 配置只有落到唯一锚点和有效 WaterRegion handle，
    才不会变成一个悬空的自然事件字符串。
    """
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.load_level("/Game/Catfishing/Maps/Lake"):
        raise RuntimeError("无法只读加载 Lake 地图")

    world_settings = unreal.EditorLevelLibrary.get_editor_world().get_world_settings()
    actual_game_mode = world_settings.get_editor_property("default_game_mode")
    expected_game_mode = _load_class(EXPECTED_GAME_MODE)
    _require(actual_game_mode == expected_game_mode, f"Lake GameMode 不匹配: actual={actual_game_mode} expected={expected_game_mode}")
    controller_class = unreal.get_default_object(expected_game_mode).get_editor_property("player_controller_class")
    expected_controller = _load_class(EXPECTED_CONTROLLER)
    _require(controller_class == expected_controller, f"Lake Controller 不匹配: actual={controller_class} expected={expected_controller}")

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    player_starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    regions = [actor for actor in actors if isinstance(actor, unreal.CatWaterRegion)]
    river_regions = [actor for actor in regions if str(actor.get_editor_property("region_id")) == EXPECTED_WATER_REGION]
    anchors = [
        actor for actor in actors
        if isinstance(actor, unreal.CatChumFieldAnchor) and actor.get_actor_label() == EXPECTED_ANCHOR_LABEL
    ]
    _require(len(player_starts) == 1, f"Lake PlayerStart 数量异常: {len(player_starts)}")
    _require(len(river_regions) == 1, f"Lake River WaterRegion 数量异常: {len(river_regions)}")
    _require(river_regions[0].has_valid_baked_geometry(), "Lake River WaterRegion 未烘焙")
    _require(len(anchors) == 1, f"Lake 自然事件锚点数量异常: {len(anchors)}")
    anchor = anchors[0]
    _require(str(_get_property(anchor, "anchor_id", "AnchorId")) == EXPECTED_ANCHOR_ID, "自然事件锚点 ID 不匹配")
    region_id, geometry_revision = _handle_fields(_get_property(anchor, "expected_water_region_handle", "ExpectedWaterRegionHandle"))
    _require(region_id.lower() == EXPECTED_WATER_REGION.lower() and geometry_revision > 0,
             f"自然事件锚点 WaterHandle 无效: Region={region_id} Revision={geometry_revision}")
    return len(player_starts), len(river_regions)


def _validate_run_settings() -> None:
    """核对 RunFlow、白天额度、夜晚准入和成功结算 gate 均为显式配置。"""
    settings = unreal.get_default_object(_load_class("/Script/Catfishing.CatRunSettings"))
    _require(bool(_get_property(settings, "b_enable_run_runtime", "bEnableRunRuntime")), "Run runtime 未启用")
    _require(EXPECTED_RUN_FLOW in _soft_path(_get_property(settings, "run_flow_state_tree", "RunFlowStateTree")),
             "RunFlow StateTree 未指向正式资产")
    _require(float(_get_property(settings, "day_length_seconds", "DayLengthSeconds")) > 0.0, "白天时长未配置")
    _require(int(_get_property(settings, "quota_target", "QuotaTarget")) > 0, "固定额度未配置")
    _require(_config_enum(RUN_SECTION, "PlayerScalingPolicy") == "FixedQuotaTarget",
             "人数缩放策略不是显式 FixedQuotaTarget")
    _require(_config_enum(RUN_SECTION, "NightJoinReadyPolicy") == "Enabled",
             "夜晚加入 ready 策略未启用")
    _require(_config_enum(RUN_SECTION, "NightReconnectReadyPolicy") == "Enabled",
             "夜晚重连 ready 策略未启用")
    _require(_config_enum(RUN_SECTION, "SuccessSettlementPolicy") == "Enabled",
             "成功结算夜策略未启用")


def _validate_environment_settings() -> None:
    """核对 Environment 正向配置能把公共自然事件解析到唯一窝料和锚点。"""
    settings = unreal.get_default_object(_load_class("/Script/Catfishing.CatEnvironmentSettings"))
    _require(bool(_get_property(settings, "b_enable_environment_runtime", "bEnableEnvironmentRuntime")),
             "Environment runtime 未启用")
    _require(_config_enum(ENVIRONMENT_SECTION, "ConfiguredWeather") != "Unknown",
             "天气仍为 Unknown")
    _require(float(_get_property(settings, "morning_end_fraction", "MorningEndFraction")) > 0.0,
             "MorningEndFraction 未配置")
    _require(float(_get_property(settings, "dusk_start_fraction", "DuskStartFraction")) > 0.0,
             "DuskStartFraction 未配置")
    _require(str(_get_property(settings, "active_event_id", "ActiveEventId")) == EXPECTED_EVENT_ID,
             "ActiveEventId 未指向本轮自然事件")
    _require(str(_get_property(settings, "natural_chum_definition_id", "NaturalChumDefinitionId")) == EXPECTED_NATURAL_CHUM,
             "自然事件窝料定义未配置")
    _require(str(_get_property(settings, "natural_chum_anchor_id", "NaturalChumAnchorId")) == EXPECTED_ANCHOR_ID,
             "自然事件锚点未配置")
    _require(unreal.load_asset("/Game/Data/Equipment/DA_Chum_Basic") is not None,
             "自然事件引用的 Chum_Basic 装备资产不可加载")


def _validate_chum_settings() -> None:
    """核对共享 ChumField 运行边界为显式正值。"""
    settings = unreal.get_default_object(_load_class("/Script/Catfishing.CatChumFieldSettings"))
    _require(bool(_get_property(settings, "b_enable_chum_field_runtime", "bEnableChumFieldRuntime")),
             "ChumField runtime 未启用")
    _require(int(_get_property(settings, "max_active_fields_per_region", "MaxActiveFieldsPerRegion")) > 0,
             "ChumField 容量未配置")
    _require(float(_get_property(settings, "max_placement_range_centimeters", "MaxPlacementRangeCentimeters")) > 0.0,
             "玩家打窝距离未配置")


def _validate_social_settings() -> None:
    """核对 Social 偷鱼、普通恶作剧和手动求助三条正向 gate 都已显式启用。"""
    settings = unreal.get_default_object(_load_class("/Script/Catfishing.CatSocialSettings"))
    _require(bool(_get_property(settings, "b_enable_social_runtime", "bEnableSocialRuntime")), "Social runtime 未启用")
    _require(_config_enum(SOCIAL_SECTION, "TheftPermission") == "Enabled",
             "偷鱼权限未启用")
    _require(float(_get_property(settings, "theft_eating_window_seconds", "TheftEatingWindowSeconds")) > 0.0,
             "偷鱼追回窗口未配置")
    _require(float(_get_property(settings, "theft_interaction_range_centimeters", "TheftInteractionRangeCentimeters")) > 0.0,
             "偷鱼交互距离未配置")
    _require(float(_get_property(settings, "theft_catch_range_centimeters", "TheftCatchRangeCentimeters")) > 0.0,
             "偷鱼追回距离未配置")
    _require(_config_enum(SOCIAL_SECTION, "MischiefPermission") == "Enabled",
             "普通恶作剧权限未启用")
    _require(float(_get_property(settings, "mischief_cooldown_seconds", "MischiefCooldownSeconds")) > 0.0,
             "普通恶作剧冷却未配置")
    _require(float(_get_property(settings, "mischief_interaction_range_centimeters", "MischiefInteractionRangeCentimeters")) > 0.0,
             "普通恶作剧距离未配置")
    _require(float(_get_property(settings, "protection_sign_radius_centimeters", "ProtectionSignRadiusCentimeters")) > 0.0,
             "ProtectionSign 保护半径未配置")
    _require(float(_get_property(settings, "protection_sign_placement_range_centimeters", "ProtectionSignPlacementRangeCentimeters")) > 0.0,
             "ProtectionSign 放置距离未配置")
    _require(float(_get_property(settings, "manual_help_radius_centimeters", "ManualHelpRadiusCentimeters")) > 0.0,
             "手动求助半径未配置")
    _require(float(_get_property(settings, "manual_help_cooldown_seconds", "ManualHelpCooldownSeconds")) > 0.0,
             "手动求助冷却未配置")


def main() -> None:
    """执行 RunEnvironmentSocial Runtime 探针并写入唯一 PASS 标记。"""
    player_starts, regions = _validate_lake()
    _validate_run_settings()
    _validate_environment_settings()
    _validate_chum_settings()
    _validate_social_settings()
    unreal.log(
        "RUN_ENVIRONMENT_SOCIAL_RUNTIME_PASS "
        "RunRuntime=Enabled SuccessSettlement=Enabled Social=Enabled "
        f"NaturalEvent={EXPECTED_EVENT_ID} NaturalChum={EXPECTED_NATURAL_CHUM} "
        f"Anchor={EXPECTED_ANCHOR_ID} Region={EXPECTED_WATER_REGION} "
        f"PlayerStarts={player_starts} Regions={regions}"
    )


main()
