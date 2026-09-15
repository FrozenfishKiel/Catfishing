"""加入页湖景与独立加载页营地背景；只修改表现，保留原业务控件与状态。

在独立 UE Editor Python 进程执行。生成器/公开房间迁移完成后可重复运行。
"""
from pathlib import Path
import hashlib
import runpy
import shutil
import unreal

shared = runpy.run_path(str(Path(__file__).with_name('style_frontend_room_dialogs.py')))
color, slate, widget, font, ensure, attach, brush, button_style = (
    shared[key] for key in ('color', 'slate', 'widget', 'font', 'ensure', 'attach', 'brush', 'button_style'))
pages = runpy.run_path(str(Path(__file__).with_name('style_frontend_pages.py')))
JOIN = '/Game/UI/Frontend/WBP_CatFrontendJoin'
LOADING = '/Game/UI/Frontend/WBP_CatFrontendLoading'
CAMP = '/Game/UI/Texture/Frontend/T_UI_Frontend_CampNight'


def full_canvas(canvas, child, z):
    slot = attach(canvas, child)
    slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(0, 0), maximum=unreal.Vector2D(1, 1)))
    slot.set_offsets(unreal.Margin(0, 0, 0, 0))
    slot.set_z_order(z)


def style_join(bp):
    shade = widget(bp, 'JoinRootShade', unreal.Border)
    shade.set_brush_color(color(0, 0, 0, 0))
    shade.set_padding(unreal.Margin(40, 32, 40, 20))
    for obj in pages['controls'](bp):
        name = obj.get_name()
        if isinstance(obj, unreal.TextBlock):
            size = 30 if name == 'JoinTitleText' else 22 if 'Title' in name else 16 if name.endswith('Label') else 14
            font(obj, size, color(.92, .79, .48) if name == 'JoinTitleText' else color(.79, .87, .82))
            obj.set_auto_wrap_text(not (name.endswith('Label') or 'Title' in name))
        elif isinstance(obj, unreal.Button):
            button_style(obj, name in ('JoinLinkButton', 'SubmitRoomPasswordButton'))
        elif isinstance(obj, unreal.EditableTextBox):
            style = obj.get_editor_property('widget_style')
            style.set_editor_property('padding', unreal.Margin(12, 9, 12, 9))
            for key in ('background_image_normal', 'background_image_hovered', 'background_image_focused'):
                style.set_editor_property(key, brush(.009, .044, .037, .92))
            style_text = style.get_editor_property('text_style')
            face = style_text.get_editor_property('font')
            face.set_editor_property('size', 15)
            style_text.set_editor_property('font', face)
            style.set_editor_property('text_style', style_text)
            obj.set_editor_property('widget_style', style)
        elif isinstance(obj, unreal.ScrollBox):
            obj.set_scrollbar_thickness(unreal.Vector2D(4, 4))

    widget(bp, 'JoinTitleText').get_editor_property('slot').set_padding(unreal.Margin(0, 0, 0, 8))
    widget(bp, 'JoinSubtitleText').get_editor_property('slot').set_padding(unreal.Margin(0, 0, 0, 12))
    columns = widget(bp, 'JoinColumns')
    # 兼容原两栏和新版三栏；只包裹已存在的列，不生成第二套业务入口。
    for name in ('PublicRoomsColumn', 'JoinFriendsColumn', 'JoinLinkColumn'):
        if unreal.find_object(None, bp.get_path_name() + ':WidgetTree.' + name):
            pages['panel'](bp, name)
            surface = widget(bp, name + 'Surface')
            surface.set_brush(brush(.004, .022, .019, .76, 8))
            surface.set_brush_color(color(1, 1, 1, 1))
            surface.set_padding(unreal.Margin(18, 18, 18, 18))
    children = list(columns.get_all_children())
    for index, child in enumerate(children):
        slot = child.get_editor_property('slot')
        slot.set_size(unreal.SlateChildSize(size_rule=unreal.SlateSizeRule.FILL))
        slot.set_padding(unreal.Margin(0 if index == 0 else 8, 0, 0 if index == len(children)-1 else 8, 0))
        slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
    columns.get_editor_property('slot').set_padding(unreal.Margin(0, 0, 0, 8))
    for name in ('PublicRoomsTitleText', 'JoinFriendsTitleText', 'JoinLinkTitleText'):
        title = unreal.find_object(None, bp.get_path_name() + ':WidgetTree.' + name)
        if title:
            title.get_editor_property('slot').set_padding(unreal.Margin(0, 0, 0, 8))
    # 右栏密码展开仍须容纳在底板内；减少说明与表单的留白，不裁掉确认/取消。
    for name in ('JoinLinkHelpText', 'JoinPermissionText'):
        widget(bp, name).get_editor_property('slot').set_padding(unreal.Margin(0, 0, 0, 6))
    font(widget(bp, 'JoinPermissionText'), 12, color(.66, .77, .71))
    widget(bp, 'JoinLinkActions').get_editor_property('slot').set_padding(unreal.Margin(0, 6, 0, 8))
    password = unreal.find_object(None, bp.get_path_name() + ':WidgetTree.JoinPasswordPanel')
    if password:
        password.get_editor_property('slot').set_padding(unreal.Margin(0, 10, 0, 0))
        font(widget(bp, 'JoinPasswordTitle'), 18, color(.79, .87, .82))
        actions = ensure(bp, 'JoinPasswordActions', unreal.HorizontalBox)
        for index, name in enumerate(('CancelRoomPasswordButton', 'SubmitRoomPasswordButton')):
            slot = attach(actions, widget(bp, name))
            slot.set_size(unreal.SlateChildSize(size_rule=unreal.SlateSizeRule.FILL))
            slot.set_padding(unreal.Margin(0, 0, 8 if index == 0 else 0, 0))
        attach(password, actions).set_padding(unreal.Margin(0, 8, 0, 0))


def style_loading(bp):
    texture = unreal.load_asset(CAMP)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError('Missing camp background: run style_frontend_room.py first')
    shade = widget(bp, 'LoadingRootShade', unreal.Border)
    # 旧资产残留已作废的献祭文案；不再显示。二进制外部引用未确认，保留具名控件。
    legacy = unreal.find_object(None, bp.get_path_name() + ':WidgetTree.LoadingSacrificeProgressTextBlock')
    if legacy:
        legacy.set_text('')
        legacy.set_visibility(unreal.SlateVisibility.COLLAPSED)
    # 原 Canvas 始终承担完整视口，内容新增独立设计画布；反复运行不嵌套缩放盒。
    scale = ensure(bp, 'LoadingContentScale', unreal.ScaleBox)
    canvas = scale.get_parent() or shade.get_parent()
    background_scale = ensure(bp, 'LoadingBackgroundScale', unreal.ScaleBox)
    background_scale.set_stretch(unreal.Stretch.SCALE_TO_FILL)
    background_scale.set_clipping(unreal.WidgetClipping.CLIP_TO_BOUNDS)
    background_scale.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    background = ensure(bp, 'LoadingBackgroundImage', unreal.Image)
    background.set_brush_from_texture(texture, True)
    background.set_color_and_opacity(color(1, 1, 1, 1))
    attach(background_scale, background)
    full_canvas(canvas, background_scale, 0)
    veil = ensure(bp, 'LoadingBackdropVeil', unreal.Border)
    veil.set_brush_color(color(.002, .009, .008, .14))
    veil.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    full_canvas(canvas, veil, 1)

    shade.remove_from_parent()
    design = ensure(bp, 'LoadingDesignCanvas', unreal.CanvasPanel)
    bounds = ensure(bp, 'LoadingDesignBounds', unreal.SizeBox)
    bounds.set_width_override(1280)
    bounds.set_height_override(720)
    attach(bounds, design)
    attach(scale, bounds)
    scale.set_stretch(unreal.Stretch.SCALE_TO_FIT)
    full_canvas(canvas, scale, 2)
    full_canvas(design, shade, 0)
    shade.set_brush_color(color(0, 0, 0, 0))
    shade.set_padding(unreal.Margin(48, 36, 48, 32))
    title = widget(bp, 'LoadingTitleText')
    title.set_text('秘境同行')
    font(title, 32, color(.92, .79, .48))

    status_bounds = ensure(bp, 'LoadingStatusBounds', unreal.SizeBox)
    status_bounds.set_width_override(650)
    surface = ensure(bp, 'LoadingStatusSurface', unreal.Border)
    surface.set_brush(brush(.003, .018, .016, .84, 8))
    surface.set_brush_color(color(1, 1, 1, 1))
    surface.set_padding(unreal.Margin(24, 18, 24, 18))
    status = ensure(bp, 'LoadingStatusColumn', unreal.VerticalBox)
    attach(status_bounds, surface)
    attach(surface, status)
    # 原生代码仍按这些名字写入真实阶段和百分比；不修改默认进度或隐藏规则。
    for name, size, tint in (
        ('LoadingDayTextBlock', 23, color(.88, .91, .80)),
        ('LoadingProgressTextBlock', 17, color(.61, .88, .79)),
        ('LoadingDetailTextBlock', 13, color(.71, .79, .74)),
        ('LoadingHintText', 12, color(.55, .68, .62))):
        # 旧 Loading 资产可能缺少新增的 Detail 文本；补齐原生已读取的同名接收方。
        obj = ensure(bp, name, unreal.TextBlock)
        font(obj, size, tint)
        obj.set_auto_wrap_text(True)
        attach(status, obj).set_padding(unreal.Margin(0, 0, 0, 7))
    progress_bounds = ensure(bp, 'LoadingProgressBounds', unreal.SizeBox)
    progress_bounds.set_height_override(6)
    attach(status, progress_bounds).set_padding(unreal.Margin(0, 7, 0, 0))
    progress = widget(bp, 'LoadingProgressBar')
    attach(progress_bounds, progress)
    style = progress.get_editor_property('widget_style')
    style.set_editor_property('background_image', brush(.016, .070, .057, 1, 3))
    style.set_editor_property('fill_image', brush(1, 1, 1, 1, 3))
    progress.set_editor_property('widget_style', style)
    progress.set_fill_color_and_opacity(color(.12, .67, .53))
    attach(widget(bp, 'LoadingRoot'), status_bounds).set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_LEFT)


def main(targets=(JOIN, LOADING)):
    if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError('Stop PIE before authoring')
    dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    if dirty.intersection(targets):
        raise RuntimeError('Unsaved target edits: ' + str(dirty.intersection(targets)))
    project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    backup = project / 'Saved/Automation/FrontendBackdrops/Backups'
    backup.mkdir(parents=True, exist_ok=True)
    for package in targets:
        bp = unreal.load_asset(package)
        if not bp:
            raise RuntimeError('Missing formal widget: ' + package)
        before = {o.get_name(): o.get_class() for o in pages['controls'](bp)}
        unreal.log('Event=frontend_backdrop_before Package={} Controls={}'.format(package, sorted(before)))
        source = project / 'Content' / (package.removeprefix('/Game/') + '.uasset')
        saved = backup / (source.stem + '-' + hashlib.sha256(source.read_bytes()).hexdigest() + '.uasset')
        if not saved.exists():
            shutil.copy2(source, saved)
        (style_join if package == JOIN else style_loading)(bp)
        after = {o.get_name(): o.get_class() for o in pages['controls'](bp)}
        if any(after.get(name) != cls for name, cls in before.items()):
            raise RuntimeError('Original control contract changed: ' + package)
        if not unreal.CatFrontendWidgetAuthoringLibrary.compile_styled_frontend_widget(bp):
            raise RuntimeError('Compile failed: ' + package)
        if not unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False):
            raise RuntimeError('Save failed: ' + package)
        unreal.log('Event=frontend_backdrop_applied Package={} OriginalControls={} Controls={} Backup={}'.format(
            package, len(before), len(after), saved))


if __name__ == '__main__':
    main()
