"""原存档页及动态行应用湖畔样式；保留所有业务控件和请求绑定。"""
from pathlib import Path
import hashlib
import runpy
import shutil
import unreal

shared = runpy.run_path(str(Path(__file__).with_name("style_frontend_menu.py")))
color, slate, widget, font, button_style, full_canvas = (shared[k] for k in
    ("color", "slate", "widget", "font", "button_style", "full_canvas"))
PAGE = "/Game/UI/Frontend/WBP_CatFrontendSaveList"
ROW = "/Game/UI/Frontend/WBP_CatSaveSlotRow"
ROOT = "/Game/UI/Frontend/WBP_CatFrontendRoot"


def ensure(bp, name, cls):
    obj = unreal.find_object(None, bp.get_path_name() + ":WidgetTree." + name)
    if obj is not None and not isinstance(obj, cls):
        raise RuntimeError("Unexpected style control type: " + name)
    obj = obj or unreal.new_object(cls, outer=unreal.find_object(None, bp.get_path_name()+":WidgetTree"), name=name)
    return obj


def attach(parent, child):
    if child.get_parent() != parent:
        child.remove_from_parent()
        parent.add_child(child)
    return child.get_editor_property("slot")


def text(bp, name, content, size=16, tint=None):
    obj = ensure(bp, name, unreal.TextBlock)
    obj.set_text(content)
    obj.set_auto_wrap_text(False)
    font(obj, size, tint or color(.65,.76,.70))
    face = obj.get_editor_property("font")
    face.set_editor_property("typeface_font_name","Regular")
    obj.set_font(face)
    return obj


def style_button(bp, name, primary=False):
    obj = widget(bp, name, unreal.Button)
    button_style(obj, primary)
    label = widget(bp, name+"Label", unreal.TextBlock)
    font(label, 17, color(.86,.88,.81))
    label.set_auto_wrap_text(False)
    foreground = unreal.SlateColor()
    foreground.set_editor_property("color_use_rule", unreal.SlateColorStylingMode.USE_COLOR_FOREGROUND)
    label.set_color_and_opacity(foreground)
    return obj


def main():
    if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError("Stop PIE before authoring")
    targets = (PAGE, ROW, ROOT)
    dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    if dirty.intersection(targets):
        raise RuntimeError("Unsaved target edits: " + str(dirty.intersection(targets)))
    page, row, root = [unreal.load_asset(p) for p in targets]
    for name, cls in {"SaveListRootShade":unreal.Border, "SaveListRoot":unreal.VerticalBox,
                      "SaveRowsScrollBox":unreal.ScrollBox, "CreateSaveNameTextBox":unreal.EditableTextBox,
                      "DeleteConfirmationRow":unreal.HorizontalBox, "SaveListActions":unreal.HorizontalBox,
                      "DeleteConfirmationText":unreal.TextBlock}.items():
        widget(page,name,cls)
    for name in ("CreateSaveButton","LoadSelectedSaveButton","DeleteSelectedSaveButton","ConfirmDeleteSaveButton","CancelSaveButton"):
        widget(page,name,unreal.Button)
    widget(row,"SelectSaveSlotButton",unreal.Button)
    project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    backup = project/"Saved/Automation/FrontendSaveStyle/Backups"
    backup.mkdir(parents=True,exist_ok=True)
    for package in targets:
        path = project/"Content"/(package.removeprefix("/Game/")+".uasset")
        destination = backup/(path.stem+"-"+hashlib.sha256(path.read_bytes()).hexdigest()+".uasset")
        if not destination.exists():
            shutil.copy2(path,destination)

    shade = widget(page,"SaveListRootShade")
    canvas = shade.get_parent()
    shade.set_brush_color(color(0,0,0,0))
    shade.set_padding(unreal.Margin(68,48,400,48))
    column = widget(page,"SaveListRoot")
    title = widget(page,"SaveListTitleText")
    font(title,32,color(.92,.79,.48))
    title.set_auto_wrap_text(False)
    title.get_editor_property("slot").set_padding(unreal.Margin(0,0,0,22))

    # Overlay 保留真实 ScrollBox，并将空状态置于同一列表区域。
    list_overlay = ensure(page,"SaveListOverlay",unreal.Overlay)
    rows = widget(page,"SaveRowsScrollBox")
    row_slot = attach(list_overlay,rows)
    row_slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    row_slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
    rows.set_scrollbar_thickness(unreal.Vector2D(5,5))
    bar = rows.get_editor_property("widget_bar_style")
    for key,tint in {"normal_thumb_image":color(.16,.30,.25,.85),
                     "hovered_thumb_image":color(.40,.55,.43),
                     "dragged_thumb_image":color(.70,.65,.40)}.items():
        brush = bar.get_editor_property(key)
        brush.set_editor_property("tint_color",slate(tint))
        bar.set_editor_property(key,brush)
    rows.set_editor_property("widget_bar_style",bar)
    empty = text(page,"SaveEmptyText","尚无存档\n为新的旅程取个名字吧",20)
    empty.set_editor_property("justification",unreal.TextJustify.CENTER)
    slot = attach(list_overlay,empty)
    slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
    slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    # 固定原有栏目顺序；仅重排保留的控件，不删除业务对象。
    create = widget(page,"CreateSaveRow")
    feedback = widget(page,"SaveResultTextBlock")
    actions = widget(page,"SaveListActions")
    for child in (title,list_overlay,create,feedback,actions):
        child.remove_from_parent()
    for child in (title,list_overlay,create,feedback,actions):
        column.add_child(child)
    title.get_editor_property("slot").set_padding(unreal.Margin(0,0,0,22))
    list_overlay.get_editor_property("slot").set_size(unreal.SlateChildSize(size_rule=unreal.SlateSizeRule.FILL))
    list_overlay.get_editor_property("slot").set_padding(unreal.Margin(0,0,0,20))
    font(feedback,15,color(.70,.80,.75))
    feedback.set_auto_wrap_text(True)
    feedback.get_editor_property("slot").set_padding(unreal.Margin(0,12,0,12))
    field = widget(page,"CreateSaveNameTextBox")
    style = field.get_editor_property("widget_style")
    text_style = style.get_editor_property("text_style")
    field_font = text_style.get_editor_property("font")
    field_font.set_editor_property("size",17)
    field_font.set_editor_property("font_object",unreal.load_asset("/Game/UI/Shop/F_CatShopChinese"))
    text_style.set_editor_property("font",field_font)
    style.set_editor_property("text_style",text_style)
    for key,tint in {"background_image_normal":color(.012,.038,.033,.92),
                     "background_image_hovered":color(.025,.09,.075,.96),
                     "background_image_focused":color(.04,.14,.11,.98)}.items():
        brush = style.get_editor_property(key)
        brush.set_editor_property("draw_as",unreal.SlateBrushDrawType.IMAGE)
        brush.set_editor_property("resource_object",None)
        brush.set_editor_property("tint_color",slate(tint))
        style.set_editor_property(key,brush)
    style.set_editor_property("padding",unreal.Margin(18,12,18,12))
    style.set_editor_property("foreground_color",slate(color(.86,.88,.81)))
    field.set_editor_property("widget_style",style)
    for name in ("CreateSaveButton","LoadSelectedSaveButton","DeleteSelectedSaveButton","ConfirmDeleteSaveButton","CancelSaveButton"):
        style_button(page,name,name in ("CreateSaveButton","LoadSelectedSaveButton"))
    widget(page,"CreateSaveButton").get_editor_property("slot").set_padding(unreal.Margin(12,0,0,0))
    widget(page,"DeleteSelectedSaveButton").get_editor_property("slot").set_padding(unreal.Margin(12,0,0,0))

    # 复用已有确认文案与确认按钮；取消按钮仍提交 Controller 的 RequestCancel。
    overlay = ensure(page,"SaveDeleteOverlay",unreal.Overlay)
    full_canvas(canvas,overlay,5)
    surface = ensure(page,"SaveDeleteSurface",unreal.Border)
    surface.set_brush_color(color(.016,.055,.048,.99))
    surface.set_padding(unreal.Margin(32,28,32,28))
    bounds = ensure(page,"SaveDeleteBounds",unreal.SizeBox)
    bounds.set_width_override(520)
    bounds.set_height_override(250)
    slot = attach(overlay,bounds)
    slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
    slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    attach(bounds,surface)
    dialog = ensure(page,"SaveDeleteColumn",unreal.VerticalBox)
    attach(surface,dialog)
    attach(dialog,text(page,"SaveDeleteTitle","删除存档？",28,color(.92,.79,.48)))
    warning = widget(page,"DeleteConfirmationText")
    attach(dialog,warning).set_padding(unreal.Margin(0,12,0,24))
    font(warning,17,color(.70,.80,.75))
    warning.set_auto_wrap_text(False)
    buttons = widget(page,"DeleteConfirmationRow")
    attach(dialog,buttons)
    cancel = ensure(page,"CancelDeleteSaveButton",unreal.Button)
    attach(cancel,text(page,"CancelDeleteSaveButtonLabel","保留存档",17))
    attach(buttons,cancel).set_padding(unreal.Margin(12,0,0,0))
    for name in ("ConfirmDeleteSaveButton","CancelDeleteSaveButton"):
        style_button(page,name,name=="CancelDeleteSaveButton")
        widget(page,name).get_editor_property("slot").set_size(unreal.SlateChildSize(size_rule=unreal.SlateSizeRule.FILL))
    overlay.set_visibility(unreal.SlateVisibility.COLLAPSED)
    root_canvas = widget(root,"FrontendPageScale").get_parent()
    scrim = ensure(root,"FrontendSaveDeleteScrim",unreal.Border)
    scrim.set_brush_color(color(0,0,0,.76))
    scrim.set_visibility(unreal.SlateVisibility.COLLAPSED)
    full_canvas(root_canvas,scrim,3)

    surface = widget(row,"SaveSlotRowRootBackground",unreal.Border)
    surface.set_padding(unreal.Margin(22,17,22,17))
    surface.set_brush_color(color(.016,.055,.048,.78))
    bounds = surface.get_parent()
    bounds.clear_height_override()
    bounds.set_min_desired_height(96)
    # 卡片间距由自身根框留出，不给 ScrollBox 创建第二套行逻辑。
    surface.get_editor_property("slot").set_padding(unreal.Margin(0,0,0,10))
    font(widget(row,"SaveSlotNameText"),21,color(.91,.85,.65))
    font(widget(row,"SaveSlotMetaText"),13,color(.60,.73,.68))
    style_button(row,"SelectSaveSlotButton")
    widget(row,"SelectSaveSlotButton").get_editor_property("slot").set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    for bp in (row,page,root):
        if not unreal.CatFrontendWidgetAuthoringLibrary.compile_styled_frontend_widget(bp):
            raise RuntimeError("Unable to compile "+bp.get_path_name())
        if not unreal.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=False):
            raise RuntimeError("Unable to save "+bp.get_path_name())
    unreal.log("Event=frontend_save_style_applied Page={} Row={} Backup={}".format(PAGE,ROW,backup))


if __name__ == "__main__":
    main()
