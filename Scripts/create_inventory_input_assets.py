"""创建或修复局内背包的独立 TAB 输入资产。

执行方式：
    UnrealEditor-Cmd.exe Catfishing.uproject -unattended -nop4 -NullRHI -ExecutePythonScript=Scripts/create_inventory_input_assets.py
"""

import unreal


# 背包 InputAction 的正式资产入口；脚本会在这里复用或创建 IA_Inventory，CatUISettings 和库存 PageController 会读取它作为 TAB 背包职责。
INVENTORY_ACTION_DIR = "/Game/Input/InputAction"

# 背包 Action 的资产名；改名会同时影响软路径默认值、IMC 映射和后续 Cook 资源收集。
INVENTORY_ACTION_NAME = "IA_Inventory"

# 背包 Action 的完整包路径；脚本保存这个资产，但不在这里维护按键，按键仍由 IMC_InputContext 承担。
INVENTORY_ACTION_PATH = f"{INVENTORY_ACTION_DIR}/{INVENTORY_ACTION_NAME}"

# 局内 ESC 菜单的正式 Action；脚本只移除它对 Tab 的旧占用，并确保它仍映射到 Escape。
LAKE_MENU_ACTION_PATH = "/Game/Input/InputAction/IA_LakeMenu"

# 项目唯一 Gameplay IMC；脚本只在这张资产里修复 Inventory/Tab 与 LakeMenu/Escape 的映射关系。
GAMEPLAY_CONTEXT_PATH = "/Game/Input/InputContext/IMC_InputContext"

# UI 设置类的反射路径；脚本只读它的 CDO，确认运行入口已经把背包 Action 指到 IA_Inventory。
UI_SETTINGS_CLASS_PATH = "/Script/Catfishing.CatUISettings"


def _require(condition: bool, message: str) -> None:
    """统一失败出口；当前检查不成立时立刻中断，让调用方停止后续保存步骤或成功日志。"""
    if not condition:
        raise RuntimeError(message)


def _get_property(obj, *names):
    """按候选名只读反射属性；全部失败时抛出最后错误，不修改被检查对象。"""
    last_error = None
    for name in names:
        try:
            return obj.get_editor_property(name)
        except Exception as exc:  # noqa: BLE001 - Unreal Python 对字段读取失败会抛通用异常。
            last_error = exc
    raise RuntimeError(f"无法读取 {obj} 的属性 {names}: {last_error}")


def _load_asset(path: str):
    """按固定资产路径加载对象；资产不存在时通过 _require 中断，本函数只读加载，不保存或修改资源。"""
    asset = unreal.load_asset(path)
    _require(asset is not None, f"无法加载资产: {path}")
    return asset


def _load_class(path: str):
    """按反射路径加载项目类；失败代表运行设置入口不可见，不能把资产修复误报为可用。"""
    cls = unreal.load_class(None, path)
    _require(cls is not None, f"无法加载类: {path}")
    return cls


def _make_key(key_name: str):
    """把项目约定的按键名转成临时 FKey；这里不验证键位合法性也不写资产，映射复核会暴露非法键名。"""
    key = unreal.Key()
    key.set_editor_property("key_name", key_name)
    return key


def _same_key(left, right) -> bool:
    """比较两个 FKey 的稳定内部名；UE Python 的结构体显示文本不能直接当作键名。"""
    return _key_name(left) == _key_name(right)


def _key_name(key) -> str:
    """读取 FKey 的内部 FName；失败时退回结构体字符串用于错误日志定位。"""
    try:
        return str(key.get_editor_property("key_name"))
    except Exception:  # noqa: BLE001 - UE Python 对不存在字段只抛通用异常。
        return str(key)


def _soft_path(value) -> str:
    """只读规范化软路径文本；UE 包装差异时依次退回对象路径和原始字符串，保留可诊断内容。"""
    try:
        return str(value.to_soft_object_path())
    except Exception:  # noqa: BLE001 - UE 软引用 Python 包装在不同版本里不完全一致。
        try:
            return value.get_path_name()
        except Exception:  # noqa: BLE001 - 失败时保留原始字符串，便于日志定位实际包装类型。
            return str(value)


def _get_default_mappings(mapping_context):
    """读取 IMC 默认映射数组；本项目 UI 输入只修改默认映射，不写用户自定义 Profile。"""
    return list(mapping_context.get_editor_property("default_key_mappings").get_editor_property("mappings"))


def _has_mapping(mapping_context, action, key) -> bool:
    """确认指定 Action 已经由 IMC 默认映射到目标按键。"""
    for mapping in _get_default_mappings(mapping_context):
        if mapping.get_editor_property("action") == action and _same_key(mapping.get_editor_property("key"), key):
            return True
    return False


def _ensure_input_action():
    """保证背包拥有独立 InputAction。

    已有 IA_Inventory 时直接复用；缺失时用 Enhanced Input 的正式工厂创建。
    工厂缺失、创建失败或资产类型不符都会中断；成功后只保存 InputAction 资产，不修改 IMC 映射。
    """
    if unreal.EditorAssetLibrary.does_asset_exist(INVENTORY_ACTION_PATH):
        inventory_action = _load_asset(INVENTORY_ACTION_PATH)
    else:
        factory_type = getattr(unreal, "InputAction_Factory", None) or getattr(unreal, "InputActionFactory", None)
        _require(factory_type is not None, "当前 Editor Python 环境没有暴露 InputAction 工厂")
        factory = factory_type()
        if hasattr(factory, "set_editor_property"):
            factory.set_editor_property("input_action_class", unreal.InputAction)
        inventory_action = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            INVENTORY_ACTION_NAME, INVENTORY_ACTION_DIR, unreal.InputAction, factory
        )
        _require(inventory_action is not None, "创建 IA_Inventory 失败")
    _require(isinstance(inventory_action, unreal.InputAction), "IA_Inventory 不是 InputAction 资产")
    unreal.EditorAssetLibrary.save_loaded_asset(inventory_action, only_if_is_dirty=False)
    return inventory_action


def _restore_inventory_tab_mapping(inventory_action, lake_menu_action, mapping_context) -> None:
    """把 Tab 还给背包并保证 Escape 仍归局内菜单。

    流程是先修改 IMC，移除 LakeMenu 对 Tab 的旧占用，再确保 LakeMenu/Escape 与 Inventory/Tab 两条正式映射存在。
    复核失败会抛错并阻止成功日志；复核通过后才保存 MappingContext，避免半修复映射被误认为可用。
    """
    tab_key = _make_key("Tab")
    escape_key = _make_key("Escape")

    mapping_context.modify()
    mapping_context.unmap_key(lake_menu_action, tab_key)
    if not _has_mapping(mapping_context, lake_menu_action, escape_key):
        mapping_context.map_key(lake_menu_action, escape_key)
    if not _has_mapping(mapping_context, inventory_action, tab_key):
        mapping_context.map_key(inventory_action, tab_key)

    _require(_has_mapping(mapping_context, lake_menu_action, escape_key), "IA_LakeMenu 未映射到 Escape")
    _require(_has_mapping(mapping_context, inventory_action, tab_key), "IA_Inventory 未映射到 Tab")
    _require(not _has_mapping(mapping_context, lake_menu_action, tab_key), "IA_LakeMenu 仍占用 Tab")
    unreal.EditorAssetLibrary.save_loaded_asset(mapping_context, only_if_is_dirty=False)


def _verify_ui_settings_paths() -> tuple[str, str, str]:
    """确认运行时 UI 设置会读取新的 Action 分工。

    这一步只读取 DefaultGame.ini 叠加后的 CatUISettings CDO，依次复核背包 Action、菜单 Action 和 Gameplay IMC。
    复核要求背包指向 IA_Inventory、菜单指向 IA_LakeMenu、两者不共用 Action，且 IMC 仍为 IMC_InputContext。
    失败不会回滚前面已经保存的资产或 IMC，只会阻止成功日志；通过后返回三条运行入口路径供日志记录。
    """
    settings_class = _load_class(UI_SETTINGS_CLASS_PATH)
    settings = unreal.get_default_object(settings_class)
    inventory_action_path = _soft_path(_get_property(settings, "inventory_toggle_action", "InventoryToggleAction"))
    main_menu_action_path = _soft_path(_get_property(settings, "main_menu_toggle_action", "MainMenuToggleAction"))
    gameplay_context_path = _soft_path(_get_property(settings, "gameplay_input_mapping_context", "GameplayInputMappingContext"))

    _require("IA_Inventory" in inventory_action_path, f"CatUISettings 背包 Action 仍未指向 IA_Inventory: {inventory_action_path}")
    _require("IA_LakeMenu" in main_menu_action_path, f"CatUISettings 菜单 Action 未指向 IA_LakeMenu: {main_menu_action_path}")
    _require(
        inventory_action_path != main_menu_action_path,
        f"CatUISettings 背包和菜单仍共用同一个 Action: {inventory_action_path}",
    )
    _require("IMC_InputContext" in gameplay_context_path, f"CatUISettings Gameplay IMC 配置异常: {gameplay_context_path}")
    return inventory_action_path, main_menu_action_path, gameplay_context_path


def main() -> None:
    """执行背包输入资产恢复。

    先准备 IA_Inventory，再加载 LakeMenu Action 和 Gameplay IMC，接着修复映射，最后复核运行设置。
    任一步失败都不会输出成功日志；实际资产写入由准备 Action 和修复 IMC 两个下游函数完成。
    """
    inventory_action = _ensure_input_action()
    lake_menu_action = _load_asset(LAKE_MENU_ACTION_PATH)
    mapping_context = _load_asset(GAMEPLAY_CONTEXT_PATH)
    _restore_inventory_tab_mapping(inventory_action, lake_menu_action, mapping_context)
    settings_inventory_action, settings_main_menu_action, settings_gameplay_context = _verify_ui_settings_paths()
    unreal.log(
        "CAT_INVENTORY_INPUT_RESTORED "
        f"InventoryAction={INVENTORY_ACTION_PATH} InventoryKey=Tab "
        f"LakeMenuAction={LAKE_MENU_ACTION_PATH} LakeMenuKey=Escape "
        f"GameplayContext={GAMEPLAY_CONTEXT_PATH} "
        f"SettingsInventoryAction={settings_inventory_action} "
        f"SettingsMainMenuAction={settings_main_menu_action} "
        f"SettingsGameplayContext={settings_gameplay_context}"
    )


if __name__ == "__main__":
    main()
