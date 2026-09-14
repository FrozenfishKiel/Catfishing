"""在正式首页应用湖畔样式；UE Editor Python 中执行，保留全部业务绑定。

背景原稿位于 ArtSource/UI/Frontend。只保存首页、Root 和这张背景贴图。
生成器补齐缺失页面后也调用此入口；正常重复执行不重建控件树。
"""

from pathlib import Path
import hashlib
import shutil

import unreal


MENU = "/Game/UI/Frontend/WBP_CatFrontendMenu"
ROOT = "/Game/UI/Frontend/WBP_CatFrontendRoot"
TEXTURE = "/Game/UI/Texture/Frontend/T_UI_Frontend_LakeNight"
BUTTONS = ("StartGameButton", "JoinPartyButton", "FrontendSettingsButton", "ExitGameButton")


def color(r, g, b, a=1.0):
    return unreal.LinearColor(r, g, b, a)


def slate(value):
    return unreal.SlateColor(specified_color=value)


def widget(bp, name, expected=None):
    value = unreal.find_object(None, bp.get_path_name() + ":WidgetTree." + name)
    if value is None or (expected and not isinstance(value, expected)):
        raise RuntimeError("Missing or unexpected control: " + name)
    return value


def full_canvas(canvas, child, z):
    slot = child.get_editor_property("slot") if child.get_parent() == canvas else canvas.add_child_to_canvas(child)
    slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(0, 0), maximum=unreal.Vector2D(1, 1)))
    slot.set_offsets(unreal.Margin(0, 0, 0, 0))
    slot.set_z_order(z)
    return slot


def font(text, size, tint):
    value = text.get_editor_property("font")
    value.set_editor_property("size", size)
    value.set_editor_property("font_object", unreal.load_asset("/Game/UI/Shop/F_CatShopChinese"))
    text.set_font(value)
    text.set_color_and_opacity(slate(tint))
    text.set_shadow_color_and_opacity(color(0, 0, 0, 0.55))
    text.set_shadow_offset(unreal.Vector2D(0, 1))
    text.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)


def button_style(button, primary=False):
    value = button.get_editor_property("widget_style")
    tints = {
        "normal": color(0.022, 0.09, 0.08, 0.68 if primary else 0.18),
        "hovered": color(0.045, 0.22, 0.19, 0.92),
        "pressed": color(0.025, 0.13, 0.12, 0.98),
        "disabled": color(0.015, 0.025, 0.024, 0.45),
    }
    for state, tint in tints.items():
        brush = value.get_editor_property(state)
        brush.set_editor_property("draw_as", unreal.SlateBrushDrawType.IMAGE)
        brush.set_editor_property("resource_object", None)
        brush.set_editor_property("tint_color", slate(tint))
        value.set_editor_property(state, brush)
    for state, tint in {
        "normal_foreground": color(0.86, 0.88, 0.81),
        "hovered_foreground": color(1, 0.97, 0.83),
        "pressed_foreground": color(0.55, 0.91, 0.81),
        "disabled_foreground": color(0.31, 0.37, 0.35),
    }.items():
        value.set_editor_property(state, slate(tint))
    value.set_editor_property("normal_padding", unreal.Margin(20, 12, 20, 12))
    value.set_editor_property("pressed_padding", unreal.Margin(20, 12, 20, 12))
    button.set_style(value)


def main():
    if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError("Stop PIE before authoring the menu")
    project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    source = project / "ArtSource/UI/Frontend/T_UI_Frontend_LakeNight.png"
    if not source.exists():
        raise RuntimeError("Missing project background source: " + str(source))
    targets = {MENU, ROOT, TEXTURE}
    dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    if dirty & targets:
        raise RuntimeError("Target packages have unsaved edits: " + str(dirty & targets))
    menu, root = unreal.load_asset(MENU), unreal.load_asset(ROOT)
    # 全部必需控件先核对；不以重建整个 WBP 掩盖已有资产的合同变化。
    required = {
        "MenuRootShade": unreal.Border, "MenuRoot": unreal.VerticalBox,
        "NorthStarTitleText": unreal.TextBlock, "MenuSubtitleText": unreal.TextBlock,
        "MenuOpenSpace": unreal.Spacer, "MenuCommandBounds": unreal.SizeBox,
        "MenuCommands": unreal.VerticalBox, "ExitConfirmationOverlay": unreal.Overlay,
        "ExitDialogSurface": unreal.Border, "ExitDialogBounds": unreal.SizeBox,
        "ExitDialogTitleText": unreal.TextBlock, "ExitConfirmationText": unreal.TextBlock,
        "ExitConfirmationScrim": unreal.Border,
    }
    required.update({name: unreal.Button for name in BUTTONS + ("ConfirmExitButton", "CancelExitButton")})
    for name, kind in required.items():
        widget(menu, name, kind)
    bounds = widget(root, "FrontendPageBounds", unreal.SizeBox)
    root_background = widget(root, "StaticBackgroundImage", unreal.Image)
    visual_shade = widget(root, "FrontendVisualShade", unreal.Border)
    tree = unreal.find_object(None, menu.get_path_name() + ":WidgetTree")
    shade = widget(menu, "MenuRootShade")
    canvas = shade.get_parent()
    if not isinstance(canvas, unreal.CanvasPanel):
        raise RuntimeError("Menu no longer uses its expected root canvas")
    backup_dir = project / "Saved/Automation/FrontendMenu/Backups"
    backup_dir.mkdir(parents=True, exist_ok=True)
    for package in sorted(targets):
        path = project / "Content" / (package.removeprefix("/Game/") + ".uasset")
        if path.exists():
            backup = backup_dir / (path.stem + "-" + hashlib.sha256(path.read_bytes()).hexdigest() + ".uasset")
            if not backup.exists():
                shutil.copy2(path, backup)

    texture = unreal.load_asset(TEXTURE) if unreal.EditorAssetLibrary.does_asset_exist(TEXTURE) else None
    if texture is None:
        task = unreal.AssetImportTask()
        # 指定同步 TextureFactory，避免 Rider 游戏线程回调内等待 Interchange 任务造成递归调度断言。
        task.set_editor_property("factory", unreal.TextureFactory())
        task.set_editor_properties({"filename": str(source), "destination_path": "/Game/UI/Texture/Frontend",
                                    "destination_name": "T_UI_Frontend_LakeNight", "automated": True,
                                    "replace_existing": False, "save": False})
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = unreal.load_asset(TEXTURE)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError("Unable to import background Texture2D")
    texture.set_editor_properties({"lod_group": unreal.TextureGroup.TEXTUREGROUP_UI,
                                   "compression_settings": unreal.TextureCompressionSettings.TC_EDITOR_ICON,
                                   "mip_gen_settings": unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
                                   "srgb": True, "never_stream": True})

    root_tree = unreal.find_object(None, root.get_path_name() + ":WidgetTree")
    root_canvas = widget(root, "FrontendPageScale").get_parent()

    def ensure_root(name, cls):
        existing = unreal.find_object(None, root.get_path_name() + ":WidgetTree." + name)
        if existing is not None and not isinstance(existing, cls):
            raise RuntimeError("Style control type changed: " + name)
        return existing or unreal.new_object(cls, outer=root_tree, name=name)

    # 背景铺满完整视口；页面另走 ScaleToFit，4:3 不再把风景也压成带上下空边的矩形。
    background_scale = ensure_root("FrontendBackgroundScale", unreal.ScaleBox)
    background_scale.set_stretch(unreal.Stretch.SCALE_TO_FILL)
    background_scale.set_clipping(unreal.WidgetClipping.CLIP_TO_BOUNDS)
    background_scale.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    root_background.set_brush_from_texture(texture, True)
    root_background.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    if root_background.get_parent() != background_scale:
        root_background.remove_from_parent()
        background_scale.add_child(root_background)
    root_background.get_editor_property("slot").set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_RIGHT)
    full_canvas(root_canvas, background_scale, 0)
    visual_shade.set_brush_color(color(0.005, 0.012, 0.01, 0.12))
    visual_shade.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    exit_scrim = ensure_root("FrontendExitScrim", unreal.Border)
    exit_scrim.set_brush_color(color(0, 0, 0, 0.76))
    exit_scrim.set_visibility(unreal.SlateVisibility.COLLAPSED)
    full_canvas(root_canvas, exit_scrim, 3)
    widget(root, "FrontendPageScale").get_editor_property("slot").set_z_order(4)
    widget(menu, "ExitConfirmationScrim").set_brush_color(color(0, 0, 0, 0))
    # 仅清理本轮曾创建的无绑定装饰控件；原业务控件全部留在源 WidgetTree。
    for obsolete in ("LakeNightBackgroundImage", "LakeNightBackgroundScale"):
        old = unreal.find_object(None, menu.get_path_name() + ":WidgetTree." + obsolete)
        if old:
            old.remove_from_parent()
            if not old.rename(outer=unreal.find_object(None, "/Engine/Transient")):
                raise RuntimeError("Unable to detach obsolete style control: " + obsolete)
    shade.set_brush_color(color(0, 0, 0, 0))
    shade.set_padding(unreal.Margin(68, 55, 68, 50))
    title = widget(menu, "NorthStarTitleText")
    title.set_text("秘境同行")
    title.set_auto_wrap_text(False)
    font(title, 40, color(0.92, 0.79, 0.48))
    subtitle = widget(menu, "MenuSubtitleText")
    font(subtitle, 15, color(0.60, 0.73, 0.68))
    subtitle.set_auto_wrap_text(True)
    subtitle.set_editor_property("wrap_text_at", 560.0)
    subtitle.get_editor_property("slot").set_padding(unreal.Margin(2, 10, 0, 0))
    space = widget(menu, "MenuOpenSpace")
    space.set_size(unreal.Vector2D(0, 58))
    space.get_editor_property("slot").set_size(unreal.SlateChildSize(size_rule=unreal.SlateSizeRule.AUTOMATIC))
    widget(menu, "MenuCommandBounds").set_width_override(300)
    for index, name in enumerate(BUTTONS):
        button = widget(menu, name)
        button_style(button, index == 0)
        button.get_editor_property("slot").set_padding(unreal.Margin(0, 0, 0, 12))
        label = widget(menu, name + "Label", unreal.TextBlock)
        font(label, 21, color(0.86, 0.88, 0.81))
        # 左对齐标签不能继承 AddText 的自动换行，否则窄窗口首次布局可能按过小期望宽度拆字。
        label.set_auto_wrap_text(False)
        # 前景继承按钮状态，悬停、按下及禁用时不被固定文本颜色覆盖。
        foreground = unreal.SlateColor()
        foreground.set_editor_property("color_use_rule", unreal.SlateColorStylingMode.USE_COLOR_FOREGROUND)
        label.set_color_and_opacity(foreground)
        label.get_editor_property("slot").set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_LEFT)
    widget(menu, "MenuCommands").set_is_enabled(True)
    dialog = widget(menu, "ExitDialogSurface")
    dialog.set_brush_color(color(0.016, 0.055, 0.048, 0.98))
    dialog.set_padding(unreal.Margin(32, 28, 32, 28))
    widget(menu, "ExitDialogBounds").set_width_override(500)
    widget(menu, "ExitDialogBounds").set_height_override(250)
    font(widget(menu, "ExitDialogTitleText"), 28, color(0.92, 0.79, 0.48))
    font(widget(menu, "ExitConfirmationText"), 17, color(0.70, 0.80, 0.75))
    for name in ("ConfirmExitButton", "CancelExitButton"):
        button_style(widget(menu, name), name == "CancelExitButton")
        font(widget(menu, name + "Label"), 18, color(0.86, 0.88, 0.81))
        widget(menu, name + "Label").set_auto_wrap_text(False)
    widget(menu, "ExitConfirmationOverlay").set_visibility(unreal.SlateVisibility.COLLAPSED)
    bounds.set_min_desired_width(1280)
    bounds.set_min_desired_height(720)
    root_background.set_color_and_opacity(color(1, 1, 1, 1))
    # 原具名绑定控件没有删除或改名；新背景仅作装饰，不需要 C++ BindWidget。
    for bp in (menu, root):
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    generated = unreal.find_object(None, ROOT + ".WBP_CatFrontendRoot_C:WidgetTree.StaticBackgroundImage")
    if generated is None or generated.get_editor_property("brush").get_editor_property("resource_object") != texture:
        raise RuntimeError("Compiled Root did not retain the background reference")
    for obj in (texture, menu, root):
        if not unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False):
            raise RuntimeError("Unable to save " + obj.get_path_name())
    unreal.log("Event=frontend_menu_style_applied Menu={} Background={} DesignSize=1280x720 Backup={}".format(
        MENU, TEXTURE, backup_dir))


if __name__ == "__main__":
    main()
