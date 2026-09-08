"""把默认 AbilitySet 补齐六个正式 BodyAction GameplayAbility。

脚本只替换 `/Game/Data/Abilities/DA_CatAbilitySet_Default` 中的 BodyAction 条目：
每个专用 Ability 都写成空输入标签、等级 1、`ON_INPUT_TRIGGERED` 激活策略；
实际启动仍走 GameplayEvent，不碰 InputConfig、Fishing Ability 或库存事务。
"""

import unreal


# 默认 AbilitySet 的唯一落盘资产路径；脚本始终只读取和保存这一个 DataAsset，避免误改其他 AbilitySet。
ABILITY_SET_PATH = "/Game/Data/Abilities/DA_CatAbilitySet_Default"
# 本轮应该写回的六个身体动作 Ability 类路径；入口函数逐个加载它们，并按这份清单重建授予位。
BODY_ACTION_CLASS_PATHS = (
    "/Script/Catfishing.CatGA_BodyActionCampRest",
    "/Script/Catfishing.CatGA_BodyActionCampfirePlayback",
    "/Script/Catfishing.CatGA_BodyActionRescueCharacterToCamp",
    "/Script/Catfishing.CatGA_BodyActionRequestManualHelp",
    "/Script/Catfishing.CatGA_BodyActionRequestMischief",
    "/Script/Catfishing.CatGA_BodyActionPlaceProtectionSign",
)


def _load_required_asset(path):
    """读取指定资产；失败立即抛错，避免脚本用 None 写回导致默认 AbilitySet 损坏。"""
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise RuntimeError(f"无法加载资产: {path}")
    return asset


def _load_required_class(path):
    """读取指定 UClass；失败立即抛错，避免把空 Ability 写入 GrantedAbilities。"""
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError(f"无法加载 Ability 类: {path}")
    return cls


def _is_body_action_entry(entry):
    """判断条目是否属于本次要重建的 BodyAction 授予位；空 Ability 代表已删除 C++ 类留下的失效引用，也随本轮清理移除。"""
    ability = entry.get_editor_property("ability")
    if ability is None:
        return True
    return ability.get_path_name().startswith("/Script/Catfishing.CatGA_BodyAction")


def _make_body_action_entry(body_action_class):
    """创建 BodyAction 专用授予条目：只设置 Ability/等级/策略，输入 Tag 保持空，避免被 EnhancedInput 直接绑定。"""
    entry = unreal.CatAbilitySetAbility()
    entry.set_editor_property("ability", body_action_class)
    entry.set_editor_property("input_tag", unreal.GameplayTag())
    entry.set_editor_property("level", 1)
    entry.set_editor_property(
        "activation_policy",
        unreal.CatAbilityActivationPolicy.ON_INPUT_TRIGGERED,
    )
    return entry


def configure_body_action_ability_set():
    """按固定清单重写默认 AbilitySet 里的 BodyAction 授予位。

    1. 先加载唯一目标资产和六个身体动作 Ability 类；任何类缺失都立即失败，避免把半套条目写回资产。
    2. 再删除旧 BodyAction 条目和删类后解析成空的失效授予位，只保留非 BodyAction 能力。
    3. 最后追加新的专用条目并保存资产，输出统计日志供批处理检索结果。
    """
    ability_set = _load_required_asset(ABILITY_SET_PATH)
    body_action_classes = [_load_required_class(path) for path in BODY_ACTION_CLASS_PATHS]
    granted_abilities = list(ability_set.get_editor_property("granted_abilities"))
    previous_count = len(granted_abilities)
    granted_abilities = [entry for entry in granted_abilities if not _is_body_action_entry(entry)]
    granted_abilities.extend(_make_body_action_entry(cls) for cls in body_action_classes)
    ability_set.set_editor_property("granted_abilities", granted_abilities)

    if not unreal.EditorAssetLibrary.save_asset(ABILITY_SET_PATH, only_if_is_dirty=False):
        raise RuntimeError(f"无法保存资产: {ABILITY_SET_PATH}")
    unreal.log(
        "BODY_ACTION_ABILITY_SET_CONFIGURED "
        f"AbilitySet={ABILITY_SET_PATH} BodyActionCount={len(body_action_classes)} "
        f"RemovedCount={previous_count + len(body_action_classes) - len(granted_abilities)} "
        f"Count={len(granted_abilities)}"
    )


configure_body_action_ability_set()
