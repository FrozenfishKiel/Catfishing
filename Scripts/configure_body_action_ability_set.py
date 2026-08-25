"""把默认 AbilitySet 补齐正式 BodyAction GameplayAbility。

脚本只修改 `/Game/Data/Abilities/DA_CatAbilitySet_Default` 的 `GrantedAbilities`：
如果 `UCatGA_BodyActionCommand` 已存在，就校正为无输入、等级 1、离散触发策略；
如果不存在，就追加一条同样配置。它不碰 InputConfig、不创建新资产，也不修改任何 Fishing Ability。
"""

import unreal


ABILITY_SET_PATH = "/Game/Data/Abilities/DA_CatAbilitySet_Default"
BODY_ACTION_CLASS_PATH = "/Script/Catfishing.CatGA_BodyActionCommand"


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


def _is_body_action_entry(entry, body_action_class):
    """判断 AbilitySet 条目是否已经指向 BodyAction 类；比较对象和值路径，兼容 Python 代理对象差异。"""
    ability = entry.get_editor_property("ability")
    return ability == body_action_class or (
        ability is not None and ability.get_path_name() == body_action_class.get_path_name()
    )


def _make_body_action_entry(body_action_class):
    """创建事件触发网关条目：只设置 Ability/等级/策略，输入 Tag 保持空，避免被 EnhancedInput 直接绑定。"""
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
    """加载默认 AbilitySet，幂等追加或校正 BodyAction 条目，保存后输出机器可检索的 PASS 标记。"""
    ability_set = _load_required_asset(ABILITY_SET_PATH)
    body_action_class = _load_required_class(BODY_ACTION_CLASS_PATH)
    granted_abilities = list(ability_set.get_editor_property("granted_abilities"))

    matched = [entry for entry in granted_abilities if _is_body_action_entry(entry, body_action_class)]
    if len(matched) > 1:
        raise RuntimeError(f"BodyAction Ability 条目重复: count={len(matched)}")
    granted_abilities = [
        entry for entry in granted_abilities
        if not _is_body_action_entry(entry, body_action_class)
    ]
    granted_abilities.append(_make_body_action_entry(body_action_class))
    ability_set.set_editor_property("granted_abilities", granted_abilities)

    if not unreal.EditorAssetLibrary.save_asset(ABILITY_SET_PATH, only_if_is_dirty=False):
        raise RuntimeError(f"无法保存资产: {ABILITY_SET_PATH}")
    unreal.log(
        "BODY_ACTION_ABILITY_SET_CONFIGURED "
        f"AbilitySet={ABILITY_SET_PATH} Ability={BODY_ACTION_CLASS_PATH} Count={len(granted_abilities)}"
    )


configure_body_action_ability_set()
