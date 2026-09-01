"""只读核对第四模块在正式 Lake 与项目配置中的运行装配事实。

脚本不保存资源、不启动 PIE，也不提交任何交易。它只证明正式地图、Equipment 定义、商店 DataTable
和营地公共仓库 View 配置可以加载；购物车支付、公共仓库入库与玩家取用仍必须由 Automation/PIE 单独证明。
"""

import math
import unreal


def _get_property(obj, *names):
    """读取 Unreal 对象或结构体属性，并兼容 Python 风格与 C++ 风格字段名。

    验证脚本只读编辑器对象；任一候选命名成功就返回，全部失败时抛出最后一次异常，让日志暴露真实字段缺口。
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


def _enum_contains(value, expected: str) -> bool:
    """判断 Unreal Python 枚举是否表达指定 C++ 枚举项。

    不同 UE 版本的 Python repr 可能是 `CatEquipmentKind.ROD` 或裸 `Rod`；只要求枚举文本包含目标 token，避免脚本绑定到某一种 repr。
    """
    def normalize(text: str) -> str:
        return "".join(ch.lower() for ch in text if ch.isalnum())

    return normalize(expected) in normalize(str(value))


def _require(condition: bool, message: str) -> None:
    """统一失败出口；Runtime 探针只有明确事实成立才继续写 PASS 标记。"""
    if not condition:
        raise RuntimeError(message)


def _load_asset(path: str):
    """同步加载验证所需资产；失败立即抛错，避免把缺资产误写成配置通过。"""
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError(f"无法加载资产: {path}")
    return asset


def _load_class(path: str):
    """按脚本路径加载 C++ 或蓝图类；不用 unreal.<短名> 是为了避开 Python 绑定导出差异。"""
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError(f"无法加载类: {path}")
    return cls


def _definition_ref_contains(settings, asset_name: str) -> bool:
    """确认 EquipmentSettings.Definitions 显式引用某个正式数据资产。

    运行期目录不扫描资产目录，只消费 Settings 中列出的软引用；因此仅能加载资产还不够，还必须证明它在正式目录里。
    """
    definitions = _get_property(settings, "definitions", "Definitions")
    return any(asset_name in str(reference) for reference in definitions)


def _validate_equipment_definition(settings, definition_id: str, kind: str, asset_name: str, asset_path: str):
    """验证一条正式 EquipmentDefinition 可被运行目录消费。

    流程先确认 Settings 显式引用资产，再读取资产身份、类别和类别专属关键数值；这些字段决定 Shop 能否把装备交给本人 Equipment，FishingSession 能否读取装备效果。
    """
    _require(_definition_ref_contains(settings, asset_name), f"EquipmentSettings 缺少定义引用: {asset_name}")
    definition = _load_asset(asset_path)
    actual_id = _as_name(_get_property(definition, "equipment_definition_id", "EquipmentDefinitionId"))
    actual_kind = _get_property(definition, "kind", "Kind")
    enabled = bool(_get_property(definition, "b_enable_runtime_definition", "bEnableRuntimeDefinition"))
    route = _as_name(_get_property(definition, "functional_route_id", "FunctionalRouteId"))
    _require(enabled, f"{definition_id} 未启用运行定义")
    _require(actual_id == definition_id, f"{asset_name} ID 不匹配: actual={actual_id} expected={definition_id}")
    _require(_enum_contains(actual_kind, kind), f"{definition_id} 类别不匹配: actual={actual_kind} expected={kind}")
    _require(route not in ("", "None"), f"{definition_id} 缺 FunctionalRouteId")
    _require(not route.startswith("FakeBait_"), f"{definition_id} 仍使用测试味 FunctionalRouteId: {route}")

    if kind == "Rod":
        _require(float(_get_property(definition, "maximum_rod_durability", "MaximumRodDurability")) > 0.0,
                 f"{definition_id} Rod 最大耐久无效")
        _require(float(_get_property(definition, "fishing_strength", "FishingStrength")) > 0.0,
                 f"{definition_id} FishingStrength 无效")
        _require(float(_get_property(definition, "maximum_line_length_centimeters", "MaximumLineLengthCentimeters")) > 0.0,
                 f"{definition_id} 线长无效")
        _require("None" not in str(_get_property(definition, "use_actor_class", "UseActorClass")),
                 f"{definition_id} 缺少部署时使用的 UseActorClass")
    elif kind == "Bait":
        _require(bool(_get_property(definition, "b_run_consumable", "bRunConsumable")),
                 f"{definition_id} 鱼饵必须作为数量型物品进入统一库存")
        _require(float(_get_property(definition, "bite_rate_multiplier", "BiteRateMultiplier")) > 0.0,
                 f"{definition_id} BiteRateMultiplier 无效")
        _require(float(_get_property(definition, "minimum_bite_delay_multiplier", "MinimumBiteDelayMultiplier")) > 0.0,
                 f"{definition_id} MinimumBiteDelayMultiplier 无效")
    elif kind == "Float":
        _require(float(_get_property(definition, "maximum_cast_distance_centimeters", "MaximumCastDistanceCentimeters")) > 0.0,
                 f"{definition_id} 抛投距离无效")
        _require(float(_get_property(definition, "bite_signal_stability", "BiteSignalStability")) > 0.0,
                 f"{definition_id} 咬钩信号稳定度无效")
    elif kind == "ScoopNet":
        _require(float(_get_property(definition, "scoop_reach_centimeters", "ScoopReachCentimeters")) > 0.0,
                 f"{definition_id} 抄网距离无效")
    elif kind == "Chum":
        _require(bool(_get_property(definition, "b_run_consumable", "bRunConsumable")),
                 f"{definition_id} 窝料必须作为数量型物品进入统一库存")
        influence = _get_property(definition, "chum_influence", "ChumInfluence")
        _require(float(_get_property(influence, "radius_centimeters", "RadiusCentimeters")) > 0.0,
                 f"{definition_id} 窝料半径无效")
        _require(float(_get_property(influence, "duration_seconds", "DurationSeconds")) > 0.0,
                 f"{definition_id} 窝料持续时间无效")
        _require(int(_get_property(influence, "maximum_quantity_per_placement", "MaximumQuantityPerPlacement")) > 0,
                 f"{definition_id} 单次窝料数量上限无效")
        _require("None" not in str(_get_property(influence, "distance_falloff_curve", "DistanceFalloffCurve")),
                 f"{definition_id} 缺少距离衰减曲线")
        _require("None" not in str(_get_property(influence, "time_falloff_curve", "TimeFalloffCurve")),
                 f"{definition_id} 缺少时间衰减曲线")
    return definition


def _validate_shop_catalog_table(settings):
    """确认正式商店目录已经迁入项目默认 DataTable，并包含当前正式商品行。"""
    configured_table = _get_property(settings, "default_shop_catalog_table", "DefaultShopCatalogTable")
    _require("DT_ShopCatalog_Default" in str(configured_table),
             f"默认商店 DataTable 配置不匹配: actual={configured_table}")
    table = _load_asset("/Game/Catfishing/Data/Shop/DT_ShopCatalog_Default")
    row_names = {_as_name(name) for name in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)}
    expected_rows = {
        "Rod_StarterT1", "Rod_ShopT2",
        "Bait_Bug", "Bait_Flashing", "Bait_Fruit", "Bait_GiantLure", "Bait_Meat",
        "Bait_Moonlight", "Bait_Nectar", "Bait_Sound",
        "Chum_Bug", "Chum_FermentedGrain", "Chum_FruitFragrance", "Chum_HolyLight",
    }
    missing_rows = sorted(expected_rows - row_names)
    _require(not missing_rows, f"正式商店 DataTable 缺少商品行: {missing_rows}")
    return row_names


def main() -> None:
    """执行第四模块 Lake Runtime 探针。

    先只读加载 Lake 并核对正式 GameMode/Controller，再验证全部正式 EquipmentDefinition、starter 装备、窝料、商店 DataTable 和公共仓库 View。
    PASS 标记同时输出关键 ID 与数量，供 PowerShell Runtime 模式确认这不是旧日志或空跑。
    """
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.load_level("/Game/Catfishing/Maps/Lake"):
        raise RuntimeError("无法只读加载 Lake 地图")

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    player_starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    regions = [actor for actor in actors if isinstance(actor, unreal.CatWaterRegion)]
    world_settings = unreal.EditorLevelLibrary.get_editor_world().get_world_settings()
    actual_game_mode = world_settings.get_editor_property("default_game_mode")
    expected_game_mode = _load_class("/Game/Game/BP_CatFishingGamemode.BP_CatFishingGamemode_C")
    _require(actual_game_mode == expected_game_mode, f"Lake GameMode 不匹配: actual={actual_game_mode} expected={expected_game_mode}")
    _require(len(player_starts) == 1 and len(regions) >= 1,
             f"Lake 入口或水域缺失: PlayerStart={len(player_starts)} Regions={len(regions)}")

    game_mode_cdo = unreal.get_default_object(expected_game_mode)
    controller_class = game_mode_cdo.get_editor_property("player_controller_class")
    expected_controller = _load_class("/Game/Player/BP_CatFishingController.BP_CatFishingController_C")
    _require(controller_class == expected_controller, f"Lake Controller 不匹配: actual={controller_class} expected={expected_controller}")

    equipment_settings = unreal.get_default_object(_load_class("/Script/Catfishing.CatEquipmentSettings"))
    _require(not bool(_get_property(equipment_settings, "b_auto_configure_starter_loadout", "bAutoConfigureStarterLoadout")),
             "开发期 Starter Loadout 自动装配必须关闭，第四模块只能由商店或解锁授权等正式入口交付装备")
    _require(bool(_get_property(equipment_settings, "b_auto_grant_starter_scoop_net", "bAutoGrantStarterScoopNet")),
             "当前开发期默认抄网发放必须显式打开；正式获取接入后应连同本断言一起关闭")
    _require(int(_get_property(equipment_settings, "starter_chum_quantity", "StarterChumQuantity")) > 0,
             "StarterChumQuantity 必须为正")

    expected_definitions = [
        ("StarterRodT1", "Rod", "Equip_Rod_StarterT1", "/Game/Catfishing/Data/Equipment/Equip_Rod_StarterT1.Equip_Rod_StarterT1"),
        ("ShopRodT2", "Rod", "Equip_Rod_ShopT2", "/Game/Catfishing/Data/Equipment/Equip_Rod_ShopT2.Equip_Rod_ShopT2"),
        ("StarterScoopNet", "ScoopNet", "Equip_ScoopNet_Starter", "/Game/Catfishing/Data/Equipment/Equip_ScoopNet_Starter.Equip_ScoopNet_Starter"),
        ("BugBait", "Bait", "Equip_Bait_Bug", "/Game/Catfishing/Data/Equipment/Equip_Bait_Bug.Equip_Bait_Bug"),
        ("FlashingBait", "Bait", "Equip_Bait_Flashing", "/Game/Catfishing/Data/Equipment/Equip_Bait_Flashing.Equip_Bait_Flashing"),
        ("FruitBait", "Bait", "Equip_Bait_Fruit", "/Game/Catfishing/Data/Equipment/Equip_Bait_Fruit.Equip_Bait_Fruit"),
        ("GiantLureBait", "Bait", "Equip_Bait_GiantLure", "/Game/Catfishing/Data/Equipment/Equip_Bait_GiantLure.Equip_Bait_GiantLure"),
        ("MeatBait", "Bait", "Equip_Bait_Meat", "/Game/Catfishing/Data/Equipment/Equip_Bait_Meat.Equip_Bait_Meat"),
        ("MoonlightBait", "Bait", "Equip_Bait_Moonlight", "/Game/Catfishing/Data/Equipment/Equip_Bait_Moonlight.Equip_Bait_Moonlight"),
        ("NectarBait", "Bait", "Equip_Bait_Nectar", "/Game/Catfishing/Data/Equipment/Equip_Bait_Nectar.Equip_Bait_Nectar"),
        ("SoundBait", "Bait", "Equip_Bait_Sound", "/Game/Catfishing/Data/Equipment/Equip_Bait_Sound.Equip_Bait_Sound"),
        ("FeatherFloat", "Float", "Equip_Float_Feather", "/Game/Catfishing/Data/Equipment/Equip_Float_Feather.Equip_Float_Feather"),
        ("YarnBallFloat", "Float", "Equip_Float_YarnBall", "/Game/Catfishing/Data/Equipment/Equip_Float_YarnBall.Equip_Float_YarnBall"),
        ("BellFloat", "Float", "Equip_Float_Bell", "/Game/Catfishing/Data/Equipment/Equip_Float_Bell.Equip_Float_Bell"),
        ("BugChum", "Chum", "Equip_Chum_Bug", "/Game/Catfishing/Data/Equipment/Equip_Chum_Bug.Equip_Chum_Bug"),
        ("FermentedGrainChum", "Chum", "Equip_Chum_FermentedGrain", "/Game/Catfishing/Data/Equipment/Equip_Chum_FermentedGrain.Equip_Chum_FermentedGrain"),
        ("FruitFragranceChum", "Chum", "Equip_Chum_FruitFragrance", "/Game/Catfishing/Data/Equipment/Equip_Chum_FruitFragrance.Equip_Chum_FruitFragrance"),
        ("HolyLightChum", "Chum", "Equip_Chum_HolyLight", "/Game/Catfishing/Data/Equipment/Equip_Chum_HolyLight.Equip_Chum_HolyLight"),
    ]
    for definition_id, kind, asset_name, asset_path in expected_definitions:
        _validate_equipment_definition(equipment_settings, definition_id, kind, asset_name, asset_path)

    starter_ids = {
        "StarterRodT1": _get_property(equipment_settings, "starter_rod_definition_id", "StarterRodDefinitionId"),
        "BugBait": _get_property(equipment_settings, "starter_bait_definition_id", "StarterBaitDefinitionId"),
        "FeatherFloat": _get_property(equipment_settings, "starter_float_definition_id", "StarterFloatDefinitionId"),
        "StarterScoopNet": _get_property(equipment_settings, "starter_scoop_net_definition_id", "StarterScoopNetDefinitionId"),
        "BugChum": _get_property(equipment_settings, "starter_chum_definition_id", "StarterChumDefinitionId"),
    }
    for expected, actual in starter_ids.items():
        _require(_as_name(actual) == expected, f"Starter ID 不匹配: actual={actual} expected={expected}")

    shop_settings = unreal.get_default_object(_load_class("/Script/Catfishing.CatShopEconomySettings"))
    _require(bool(_get_property(shop_settings, "b_enable_shop_economy_runtime", "bEnableShopEconomyRuntime")),
             "ShopEconomy runtime 未启用")
    _require(int(_get_property(shop_settings, "starting_team_wallet_balance", "StartingTeamWalletBalance")) >= 5,
             "团队公款初始余额不足以购买正式鱼竿和正式鱼漂")
    catalog_rows = _validate_shop_catalog_table(shop_settings)

    camp_inventory_cdo = unreal.get_default_object(_load_class("/Script/Catfishing.CatCampInventoryActor"))
    inventory_view_class = _get_property(camp_inventory_cdo, "inventory_view_class", "InventoryViewClass")
    _require("WBP_CatCampInventory" in str(inventory_view_class),
             f"营地公共仓库未绑定独立库存页面: actual={inventory_view_class}")

    price_policy = _get_property(shop_settings, "fish_purchase_price_policy", "FishPurchasePricePolicy")
    _require(_enum_contains(price_policy, "Unset"),
             f"当前售鱼价格策略应保持未裁 fail-closed: actual={price_policy}")

    unreal.log(
        "EQUIPMENT_SHOP_RUNTIME_PASS "
        f"LakeGameMode={actual_game_mode} Controller={controller_class} "
        "Definitions=StarterRodT1,ShopRodT2,StarterScoopNet,BugBait,FlashingBait,FruitBait,GiantLureBait,MeatBait,"
        "MoonlightBait,NectarBait,SoundBait,FeatherFloat,YarnBallFloat,BellFloat,BugChum,"
        "FermentedGrainChum,FruitFragranceChum,HolyLightChum "
        f"CatalogTable=DT_ShopCatalog_Default CatalogRows={len(catalog_rows)} "
        f"CampInventoryView={inventory_view_class} PlayerStarts={len(player_starts)} Regions={len(regions)} PricePolicy={price_policy}"
    )


main()
