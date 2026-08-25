"""只读核对 FishingPlayerEntry 的 Lake、正式 BP 输入装配和可钓水域，不保存或修改资产。"""

import re
import unreal


EXPECTED_GAMEPLAY_MAP_PACKAGE = "/Game/Catfishing/Maps/Lake"
EXPECTED_GAMEPLAY_MAP_OBJECT = "/Game/Catfishing/Maps/Lake.Lake"
EXPECTED_INPUT_CONFIG = "/Game/Data/Abilities/DA_CatAbilityInputConfig"
EXPECTED_ABILITY_SET = "/Game/Data/Abilities/DA_CatAbilitySet_Default"
EXPECTED_FISHING_TAGS = {
    "Cat.Input.Fishing.RodInteract",
    "Cat.Input.Fishing.Primary",
    "Cat.Input.Fishing.Slack",
    "Cat.Input.Fishing.Cancel",
    "Cat.Input.Fishing.Scoop",
    "Cat.Input.Fishing.Chum",
}
HELD_FISHING_TAGS = {
    "Cat.Input.Fishing.Primary",
    "Cat.Input.Fishing.Slack",
    "Cat.Input.Fishing.Chum",
}
EXPECTED_ABILITY_CLASSES = {
    "Cat.Input.Fishing.RodInteract": "/Game/Blueprint/Abilities/BP_GA_RodInteract.BP_GA_RodInteract_C",
    "Cat.Input.Fishing.Primary": "/Game/Blueprint/Abilities/BP_GA_Primary.BP_GA_Primary_C",
    "Cat.Input.Fishing.Slack": "/Game/Blueprint/Abilities/BP_GA_Slack.BP_GA_Slack_C",
    "Cat.Input.Fishing.Cancel": "/Game/Blueprint/Abilities/BP_GA_Cancel.BP_GA_Cancel_C",
    "Cat.Input.Fishing.Scoop": "/Game/Blueprint/Abilities/BP_GA_Scoop.BP_GA_Scoop_C",
    "Cat.Input.Fishing.Chum": "/Game/Blueprint/Abilities/BP_GA_Chum.BP_GA_Chum_C",
}


def _path_text(value) -> str:
    """把 SoftObject、SoftObjectPath、UObject 或 Python 包装值统一成可比较路径文本；验证脚本只读，不解析资产本体状态。"""
    if value is None:
        return ""
    for method_name in ("to_soft_object_path", "to_string", "get_asset_path_string", "get_path_name"):
        method = getattr(value, method_name, None)
        if callable(method):
            try:
                return str(method())
            except Exception:
                pass
    return str(value)


def _tag_text(value) -> str:
    """读取 GameplayTag 的稳定 TagName 文本；避免 wrapper 的 __str__ 差异影响唯一性判断。"""
    if value is None:
        return ""
    for member_name in ("tag_name", "TagName"):
        try:
            member_value = getattr(value, member_name)
            if member_value:
                return str(member_value).strip()
        except Exception:
            pass
        try:
            member_value = value.get_editor_property(member_name)
            if member_value:
                return str(member_value).strip()
        except Exception:
            pass
    for method_name in ("get_tag_name", "to_string", "export_text"):
        method = getattr(value, method_name, None)
        if callable(method):
            try:
                text = str(method()).strip()
                if text and not text.startswith("<Struct 'GameplayTag'"):
                    return text
            except Exception:
                pass
    text = str(value).strip()
    match = re.search(r"TagName=[\"']([^\"']+)[\"']", text)
    return match.group(1).strip() if match else text


def _policy_text(value) -> str:
    """把 Ability 激活策略标准化为小写连续文本，兼容 UE Python 枚举显示名差异。"""
    return str(value).replace("_", "").replace(":", "").replace(".", "").lower()


def _validate_configured_assets() -> None:
    """核对正式入口配置指向 Lake、默认 InputConfig 和默认 AbilitySet；脚本随后再加载这些资产做内容级检查。"""
    online_settings_class = unreal.load_class(None, "/Script/Catfishing.CatOnlineSettings")
    ability_settings_class = unreal.load_class(None, "/Script/Catfishing.CatAbilitySettings")
    online_settings = unreal.get_default_object(online_settings_class)
    ability_settings = unreal.get_default_object(ability_settings_class)
    gameplay_map = _path_text(online_settings.get_editor_property("GameplayMap"))
    input_config = _path_text(ability_settings.get_editor_property("AbilityInputConfig"))
    ability_set = _path_text(ability_settings.get_editor_property("DefaultAbilitySet"))
    if EXPECTED_GAMEPLAY_MAP_PACKAGE not in gameplay_map and EXPECTED_GAMEPLAY_MAP_OBJECT not in gameplay_map:
        raise RuntimeError(f"GameplayMap 未指向正式 Lake: {gameplay_map}")
    if EXPECTED_INPUT_CONFIG not in input_config:
        raise RuntimeError(f"默认 AbilityInputConfig 不是正式资产: {input_config}")
    if EXPECTED_ABILITY_SET not in ability_set:
        raise RuntimeError(f"默认 AbilitySet 不是正式资产: {ability_set}")


def _validate_input_config(ability_config, mapped_actions) -> dict:
    """验证 InputConfig 正好暴露六个稳定 Fishing Tag，且每个 Tag 的 Action 都在唯一 IMC 中有映射。"""
    if ability_config is None:
        raise RuntimeError("无法加载正式 AbilityInputConfig")
    ability_actions = ability_config.get_editor_property("ability_input_actions")
    input_action_by_tag = {}
    seen_actions = set()
    for entry in ability_actions:
        tag = _tag_text(entry.get_editor_property("input_tag"))
        action = entry.get_editor_property("input_action")
        if not tag or action is None or tag in input_action_by_tag or action in seen_actions:
            raise RuntimeError(f"AbilityInputConfig 存在空值或重复映射: Tag={tag} Action={action}")
        input_action_by_tag[tag] = action
        seen_actions.add(action)
    if set(input_action_by_tag.keys()) != EXPECTED_FISHING_TAGS:
        raise RuntimeError(f"AbilityInputConfig Tag 集合失败: actual={sorted(input_action_by_tag.keys())}")
    missing_mapped_tags = [tag for tag, action in input_action_by_tag.items() if action not in mapped_actions]
    if missing_mapped_tags:
        raise RuntimeError(f"IMC 未映射正式 Fishing Action: {missing_mapped_tags}")
    return input_action_by_tag


def _validate_default_ability_set(ability_set) -> dict:
    """验证默认 AbilitySet 为同一组六个 Fishing Tag 授予对应能力，并保持按住型与离散型策略边界。"""
    if ability_set is None:
        raise RuntimeError("无法加载正式 AbilitySet")
    ability_entries = ability_set.get_editor_property("granted_abilities")
    ability_entry_by_tag = {}
    seen_abilities = set()
    for entry in ability_entries:
        ability = entry.get_editor_property("ability")
        tag = _tag_text(entry.get_editor_property("input_tag"))
        if ability is None or ability in seen_abilities:
            raise RuntimeError(f"AbilitySet 存在空 Ability 或重复 Ability: Tag={tag} Ability={ability}")
        seen_abilities.add(ability)
        if tag:
            if tag in ability_entry_by_tag:
                raise RuntimeError(f"AbilitySet 输入 Tag 重复: {tag}")
            ability_entry_by_tag[tag] = entry
    missing_tags = EXPECTED_FISHING_TAGS.difference(ability_entry_by_tag.keys())
    if missing_tags:
        raise RuntimeError(f"AbilitySet 缺少正式 Fishing Ability: {sorted(missing_tags)}")
    for tag, expected_class_path in EXPECTED_ABILITY_CLASSES.items():
        entry = ability_entry_by_tag[tag]
        actual_class = entry.get_editor_property("ability")
        expected_class = unreal.load_class(None, expected_class_path)
        if actual_class != expected_class:
            raise RuntimeError(f"AbilitySet Ability 类失败: Tag={tag} Actual={actual_class} Expected={expected_class}")
        policy = _policy_text(entry.get_editor_property("activation_policy"))
        expected_policy = "whileinputactive" if tag in HELD_FISHING_TAGS else "oninputtriggered"
        if expected_policy not in policy:
            raise RuntimeError(f"AbilitySet 激活策略失败: Tag={tag} Policy={policy} Expected={expected_policy}")
    return ability_entry_by_tag


def main() -> None:
    """加载 Lake 后按正式入口顺序核对 BP GameMode/Controller、唯一 IMC、出生点、水域所有权、Bake 与岸线可达性；任一缺口立即失败。"""
    _validate_configured_assets()
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.load_level("/Game/Catfishing/Maps/Lake"):
        raise RuntimeError("无法只读加载 Lake 地图")

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    player_starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    grounds = [actor for actor in actors if actor.get_actor_label() == "StageA_Ground"]
    regions = [actor for actor in actors if isinstance(actor, unreal.CatWaterRegion)]
    boundaries = [actor for actor in actors if isinstance(actor, unreal.CatWaterBoundarySplineActor)]
    world_settings = unreal.EditorLevelLibrary.get_editor_world().get_world_settings()
    actual_game_mode = world_settings.get_editor_property("default_game_mode")
    expected_game_mode = unreal.load_class(None, "/Game/Game/BP_CatFishingGamemode.BP_CatFishingGamemode_C")

    if len(player_starts) != 1 or len(grounds) != 1 or len(regions) != 1 or len(boundaries) != 1:
        raise RuntimeError(
            f"Lake Actor 数量失败: PlayerStart={len(player_starts)} Ground={len(grounds)} "
            f"Region={len(regions)} Boundary={len(boundaries)}"
        )
    if actual_game_mode != expected_game_mode:
        raise RuntimeError(f"Lake GameMode 失败: actual={actual_game_mode} expected={expected_game_mode}")

    game_mode_cdo = unreal.get_default_object(expected_game_mode)
    controller_class = game_mode_cdo.get_editor_property("player_controller_class")
    expected_controller = unreal.load_class(None, "/Game/Player/BP_CatFishingController.BP_CatFishingController_C")
    controller_cdo = unreal.get_default_object(controller_class)
    mapping_context = controller_cdo.get_editor_property("default_mapping_context")
    expected_mapping_context = unreal.load_asset("/Game/Input/InputContext/IMC_InputContext")
    default_mappings = mapping_context.get_editor_property("default_key_mappings").get_editor_property("mappings")
    movement_actions = [
        controller_cdo.get_editor_property("move_action"),
        controller_cdo.get_editor_property("look_action"),
        controller_cdo.get_editor_property("jump_action"),
        controller_cdo.get_editor_property("sprint_action"),
    ]
    ability_config = unreal.load_asset("/Game/Data/Abilities/DA_CatAbilityInputConfig")
    ability_set = unreal.load_asset("/Game/Data/Abilities/DA_CatAbilitySet_Default")
    mapped_actions = {mapping.get_editor_property("action") for mapping in default_mappings}
    input_action_by_tag = _validate_input_config(ability_config, mapped_actions)
    ability_entry_by_tag = _validate_default_ability_set(ability_set)
    required_actions = movement_actions + list(input_action_by_tag.values())
    if (
        controller_class != expected_controller
        or mapping_context != expected_mapping_context
        or any(action is None or action not in mapped_actions for action in required_actions)
    ):
        raise RuntimeError(
            f"正式输入装配失败: Controller={controller_class} IMC={mapping_context} "
            f"Mappings={len(default_mappings)} AbilityActions={len(input_action_by_tag)}"
        )

    region = regions[0]
    boundary = boundaries[0]
    start_distance = abs(player_starts[0].get_actor_location().x - boundary.get_actor_location().x)
    if (
        str(region.get_editor_property("region_id")) != "River"
        or boundary.get_editor_property("owning_region") != region
        or boundary not in region.get_editor_property("boundary_actors")
        or not region.has_valid_baked_geometry()
        or start_distance > 500.0
    ):
        raise RuntimeError(
            f"Lake 水域失败: RegionId={region.get_editor_property('region_id')} "
            f"Owner={boundary.get_editor_property('owning_region')} Baked={region.has_valid_baked_geometry()} "
            f"StartDistance={start_distance}"
        )
    unreal.log(
        "FISHING_PLAYER_ENTRY_MAP_PASS "
        f"PlayerStart={len(player_starts)} Region=River Baked=True Mappings={len(default_mappings)} "
        f"AbilityActions={len(input_action_by_tag)} AbilitySetActions={len(ability_entry_by_tag)} "
        f"GameplayMap=Lake GameMode={actual_game_mode}"
    )


main()
