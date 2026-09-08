"""在正式 HUD 原资产内补齐同竿总体力控件；编辑器 Python / UIReach WBPCreate 共用入口。"""

import hashlib
from pathlib import Path
import shutil

import unreal


HUD_PACKAGE = "/Game/UI/HUD/WBP_CatHUD"
HUD_OBJECT = HUD_PACKAGE + ".WBP_CatHUD"
CONTROL_TYPES = {
    "CatStaminaTextBlock": unreal.TextBlock,
    "CatStaminaProgressBar": unreal.ProgressBar,
}


def main():
    # 不保存用户尚未落盘的同包更改；备份仅写 Saved，重复执行不会制造新业务资产。
    dirty = {package.get_name() for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    if HUD_PACKAGE in dirty:
        raise RuntimeError("HUD package has unsaved edits; save or discard those edits before migration")
    project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    source = project / "Content/UI/HUD/WBP_CatHUD.uasset"
    original_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    backup_dir = project / "Saved/Automation/UIReach/CooperativeHUD"
    backup_dir.mkdir(parents=True, exist_ok=True)
    backup = backup_dir / ("WBP_CatHUD-" + original_hash + ".uasset")
    if not backup.exists():
        shutil.copy2(source, backup)

    blueprint = unreal.load_asset(HUD_PACKAGE)
    if blueprint is None:
        raise RuntimeError("Unable to load formal HUD")
    tree = unreal.find_object(None, HUD_OBJECT + ":WidgetTree")
    root = unreal.find_object(None, HUD_OBJECT + ":WidgetTree.HUDRoot")
    if tree is None or not isinstance(root, unreal.CanvasPanel):
        raise RuntimeError("Formal HUD requires its existing CanvasPanel HUDRoot")

    changed = []
    for name, expected_type in CONTROL_TYPES.items():
        widget = unreal.find_object(None, HUD_OBJECT + ":WidgetTree." + name)
        if widget is not None:
            if not isinstance(widget, expected_type) or widget.get_parent() != root:
                raise RuntimeError("Existing HUD control has an unexpected type or parent: " + name)
            continue
        widget = unreal.new_object(expected_type, outer=tree, name=name)
        slot = root.add_child_to_canvas(widget)
        slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(0.5, 1.0), maximum=unreal.Vector2D(0.5, 1.0)))
        slot.set_alignment(unreal.Vector2D(0.5, 0.0))
        slot.set_auto_size(False)
        slot.set_z_order(10)
        widget.set_visibility(unreal.SlateVisibility.COLLAPSED)
        if name == "CatStaminaTextBlock":
            slot.set_position(unreal.Vector2D(0.0, -166.0))
            slot.set_size(unreal.Vector2D(380.0, 32.0))
            widget.set_text("总体力")
            font = widget.get_editor_property("font")
            font.set_editor_property("size", 20)
            widget.set_font(font)
            widget.set_color_and_opacity(unreal.SlateColor(specified_color=unreal.LinearColor(1.0, 0.95, 0.8, 1.0)))
        else:
            slot.set_position(unreal.Vector2D(0.0, -126.0))
            slot.set_size(unreal.Vector2D(380.0, 20.0))
            widget.set_percent(0.0)
            widget.set_fill_color_and_opacity(unreal.LinearColor(0.3, 0.78, 0.53, 1.0))
        changed.append(name)

    if changed:
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
            raise RuntimeError("Unable to save migrated formal HUD")

    # 从实际生成类模板再核对一遍，防止只改源树却没进入运行时实例。
    generated_tree_path = HUD_PACKAGE + ".WBP_CatHUD_C:WidgetTree."
    for name, expected_type in CONTROL_TYPES.items():
        widget = unreal.find_object(None, generated_tree_path + name)
        if not isinstance(widget, expected_type):
            raise RuntimeError("Compiled formal HUD is missing " + name)
    unreal.log("COOPERATIVE_HUD_MIGRATION_PASS "
               + "HUD=" + HUD_PACKAGE
               + " Changed=" + str(changed)
               + " OriginalSHA256=" + original_hash
               + " Backup=" + str(backup)
               + " TotalStaminaText=CatStaminaTextBlock TotalStaminaBar=CatStaminaProgressBar")


main()
