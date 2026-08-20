"""
生成 Catfishing Lake 基础输入资产：/Game/Catfishing/Input/IA_Move、IA_Look、IA_Jump、IA_FishingPull、IA_FishingRelease、IMC_Lake。

运行方式（编辑器不能同时开着同一个项目）：
  UnrealEditor-Cmd.exe Catfishing.uproject -run=pythonscript -script="Tools/Python/create_input_assets.py" -unattended -nosplash -nullrhi

幂等约定：
  - 资产已存在时复用同一个包，不再重复 create_asset；
  - 每次运行都先 UnmapAll 清空 IMC 映射，再按本文件的表重建，所以重复运行不会叠加映射；
  - 末尾自检每个 InputAction 的映射条数与修饰器数量，不符合预期直接 raise，让命令行以非零退出。

键位表（工程手感暂定，与 UE 第三人称模板同构；bEnableLegacyInputScales=True 下 Pitch 内部乘 -2.5，
所以 Look 的 Y 轴要 Negate 才是“鼠标/摇杆上推 = 抬头”）：
  IA_Move (Axis2D): W=Swizzle(YXZ) S=Negate+Swizzle(YXZ) A=Negate D=无  Gamepad_Left2D=无
  IA_Look (Axis2D): Mouse2D=Negate(Y) Gamepad_Right2D=Negate(Y)
  IA_Jump (Boolean): SpaceBar  Gamepad_FaceButton_Bottom
  IA_FishingPull (Boolean): LeftMouseButton  Gamepad_RightTrigger   —— 飞书钓鱼规则 §4.3：左键=拖/提竿
  IA_FishingRelease (Boolean): RightMouseButton  Gamepad_LeftTrigger —— 飞书钓鱼规则 §4.3：右键=松/放线
"""

import unreal

PACKAGE_PATH = "/Game/Catfishing/Input"

# 每个 InputAction 的名字、值类型，以及 (键名, 修饰器构造列表) 的映射表；自检按这张表逐条核对。
ACTIONS = {
    "IA_Move": (
        unreal.InputActionValueType.AXIS2D,
        [
            ("W", ["swizzle"]),
            ("S", ["negate", "swizzle"]),
            ("A", ["negate"]),
            ("D", []),
            ("Gamepad_Left2D", []),
        ],
    ),
    "IA_Look": (
        unreal.InputActionValueType.AXIS2D,
        [
            ("Mouse2D", ["negate_y"]),
            ("Gamepad_Right2D", ["negate_y"]),
        ],
    ),
    "IA_Jump": (
        unreal.InputActionValueType.BOOLEAN,
        [
            ("SpaceBar", []),
            ("Gamepad_FaceButton_Bottom", []),
        ],
    ),
    "IA_FishingPull": (
        unreal.InputActionValueType.BOOLEAN,
        [
            ("LeftMouseButton", []),
            ("Gamepad_RightTrigger", []),
        ],
    ),
    "IA_FishingRelease": (
        unreal.InputActionValueType.BOOLEAN,
        [
            ("RightMouseButton", []),
            ("Gamepad_LeftTrigger", []),
        ],
    ),
}
IMC_NAME = "IMC_Lake"


def load_or_create(name, asset_class, factory):
    """资产存在就加载复用，不存在才用工厂新建；返回加载后的对象。"""
    object_path = f"{PACKAGE_PATH}/{name}.{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(object_path):
        asset = unreal.EditorAssetLibrary.load_asset(object_path)
        unreal.log(f"[create_input_assets] reuse {object_path}")
    else:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, PACKAGE_PATH, asset_class, factory)
        unreal.log(f"[create_input_assets] create {object_path}")
    if asset is None:
        raise RuntimeError(f"cannot load or create {object_path}")
    return asset


def make_key(key_name):
    """按键名构造 FKey；KeyName 是 EditAnywhere 属性，可以直接写。"""
    key = unreal.Key()
    key.set_editor_property("key_name", key_name)
    return key


def make_modifier(kind, outer):
    """按简写构造修饰器实例；修饰器是 Instanced 对象，Outer 必须是 IMC 本身才能随资产序列化。"""
    if kind == "swizzle":
        return unreal.new_object(unreal.InputModifierSwizzleAxis, outer=outer)  # 默认 Order=YXZ
    if kind == "negate":
        return unreal.new_object(unreal.InputModifierNegate, outer=outer)  # 默认 X/Y/Z 全取反
    if kind == "negate_y":
        modifier = unreal.new_object(unreal.InputModifierNegate, outer=outer)
        modifier.set_editor_property("x", False)
        modifier.set_editor_property("y", True)
        modifier.set_editor_property("z", False)
        return modifier
    raise ValueError(f"unknown modifier kind {kind}")


def main():
    actions = {}
    for name, (value_type, _mappings) in ACTIONS.items():
        action = load_or_create(name, unreal.InputAction, unreal.InputAction_Factory())
        action.set_editor_property("value_type", value_type)
        actions[name] = action

    imc = load_or_create(IMC_NAME, unreal.InputMappingContext, unreal.InputMappingContext_Factory())
    imc.unmap_all()

    # 先用 BlueprintCallable MapKey 建立条目（顺序即 DefaultKeyMappings.Mappings 顺序），再整体读改写 modifiers。
    expected = []  # [(action_name, key_name, modifier_count)]
    for name, (_value_type, mappings) in ACTIONS.items():
        for key_name, modifier_kinds in mappings:
            imc.map_key(actions[name], make_key(key_name))
            expected.append((name, key_name, modifier_kinds))

    data = imc.get_editor_property("default_key_mappings")
    rows = list(data.get_editor_property("mappings"))
    if len(rows) != len(expected):
        raise RuntimeError(f"mapping count mismatch after MapKey: {len(rows)} != {len(expected)}")
    for row, (_name, _key_name, modifier_kinds) in zip(rows, expected):
        row.set_editor_property("modifiers", [make_modifier(kind, imc) for kind in modifier_kinds])
    data.set_editor_property("mappings", rows)
    imc.set_editor_property("default_key_mappings", data)

    for asset in list(actions.values()) + [imc]:
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError(f"save failed: {asset.get_path_name()}")

    # 自检：重新读回已保存的 IMC，逐条核对 Action、Key 与修饰器数量。
    saved = unreal.EditorAssetLibrary.load_asset(f"{PACKAGE_PATH}/{IMC_NAME}.{IMC_NAME}")
    saved_rows = list(saved.get_editor_property("default_key_mappings").get_editor_property("mappings"))
    per_action = {name: [0, 0] for name in ACTIONS}  # name -> [mapping_count, modifier_count]
    for row in saved_rows:
        action = row.get_editor_property("action")
        action_name = action.get_name() if action else "<none>"
        modifiers = list(row.get_editor_property("modifiers"))
        key_name = str(row.get_editor_property("key").get_editor_property("key_name"))
        unreal.log(f"[create_input_assets] {action_name} <- {key_name} modifiers={len(modifiers)}")
        if action_name in per_action:
            per_action[action_name][0] += 1
            per_action[action_name][1] += len(modifiers)
    for name, (_value_type, mappings) in ACTIONS.items():
        want_mappings = len(mappings)
        want_modifiers = sum(len(kinds) for _key, kinds in mappings)
        got_mappings, got_modifiers = per_action[name]
        unreal.log(f"[create_input_assets] {name}: mappings={got_mappings}/{want_mappings} modifiers={got_modifiers}/{want_modifiers}")
        if (got_mappings, got_modifiers) != (want_mappings, want_modifiers):
            raise RuntimeError(f"{name} self-check failed: mappings={got_mappings}/{want_mappings} modifiers={got_modifiers}/{want_modifiers}")
    for name, (value_type, _mappings) in ACTIONS.items():
        saved_action = unreal.EditorAssetLibrary.load_asset(f"{PACKAGE_PATH}/{name}.{name}")
        if saved_action.get_editor_property("value_type") != value_type:
            raise RuntimeError(f"{name} value_type mismatch")
    unreal.log("[create_input_assets] OK")


main()
