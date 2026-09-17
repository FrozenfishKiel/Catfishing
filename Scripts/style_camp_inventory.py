"""将团队库存装配为羊皮纸界面；复用正式库存与拖放控件，不改物品、存档或个人背包资产。"""
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
INK = unreal.LinearColor(.19, .12, .055, 1)


def widget(bp, name):
    """按稳定控件名取资产模板；缺失时中止，不静默保存断开的接线。"""
    value = unreal.find_object(None, bp.get_path_name() + ':WidgetTree.' + name)
    if not value:
        raise RuntimeError('Missing widget: ' + name)
    return value


def ensure(bp, name, cls):
    """复用同名控件或在本树创建；类型冲突保留原资产并报错。"""
    value = unreal.find_object(None, bp.get_path_name() + ':WidgetTree.' + name)
    if value and not (isinstance(value, cls) if isinstance(cls, type) else value.get_class() == cls):
        raise RuntimeError('Widget type conflict: ' + name)
    return value or unreal.new_object(cls, outer=unreal.find_object(None, bp.get_path_name()+':WidgetTree'), name=name)


def attach(parent, child):
    """保留控件对象和业务名，仅改变父容器；返回新槽供布局。"""
    child.remove_from_parent()
    return parent.add_child(child)


def place(canvas, child, x, y, width, height):
    """以参考图的 1536×1024 坐标设置槽位；外层 ScaleBox 负责屏幕适配。"""
    slot = attach(canvas, child)
    slot.set_position(unreal.Vector2D(x, y))
    slot.set_size(unreal.Vector2D(width, height))
    return slot


def text(bp, name, value, size):
    """生成动态文本控件的默认外观，使用项目中文字体并避免文字拦截鼠标。"""
    obj = ensure(bp, name, unreal.TextBlock)
    obj.set_text(value)
    font = obj.get_editor_property('font')
    font.set_editor_property('font_object', unreal.load_asset('/Game/UI/Shop/F_CatShopChinese'))
    font.set_editor_property('size', size)
    obj.set_font(font)
    obj.set_color_and_opacity(unreal.SlateColor(specified_color=INK))
    obj.set_editor_property('justification', unreal.TextJustify.CENTER)
    obj.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    return obj


def frame(border):
    """独立边框使用圆角描线和透纸底色；框内物品图始终由运行时读物品定义。"""
    brush = unreal.SlateBrush()
    brush.set_editor_property('draw_as', unreal.SlateBrushDrawType.ROUNDED_BOX)
    brush.set_editor_property('tint_color', unreal.SlateColor(specified_color=unreal.LinearColor(.80,.63,.38,.12)))
    outline = brush.get_editor_property('outline_settings')
    outline.set_editor_property('corner_radii', unreal.Vector4(6,6,6,6))
    outline.set_editor_property('rounding_type', unreal.SlateBrushRoundingType.FIXED_RADIUS)
    outline.set_editor_property('width', 2)
    outline.set_editor_property('color', unreal.SlateColor(specified_color=unreal.LinearColor(.37,.24,.10,.65)))
    brush.set_editor_property('outline_settings', outline)
    border.set_brush(brush)
    border.set_padding(unreal.Margin(10,10,10,10))


def save(bp):
    """编译后再保存正式包；失败中止，不能把未编译模板作为可交付资产。"""
    if not unreal.CatCampInventoryAuthoringLibrary.finalize_camp_layout(bp):
        raise RuntimeError('Compile failed: '+bp.get_path_name())
    if not unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False):
        raise RuntimeError('Save failed: '+bp.get_path_name())


def duplicate(source, target):
    """首次复制既有业务控件，重跑复用目标；不修改来源共享控件。"""
    return unreal.load_asset(target) if unreal.EditorAssetLibrary.does_asset_exist(target) else unreal.EditorAssetLibrary.duplicate_asset(source, target)


def style_slot(name, size):
    """沿用库存格类及其拖放/悬停行为，独立设置大小、边框、图像等比缩放和数量角标。"""
    bp = duplicate('/Game/UI/InventorySlot/WBP_CatInventorySlot', '/Game/UI/InventorySlot/'+name)
    root = widget(bp, 'SizeBox_43')
    root.set_width_override(size)
    root.set_height_override(size)
    overlay = widget(bp, 'Overlay_74')
    root.clear_children()
    border = ensure(bp, 'PaperSlotFrame', unreal.Border)
    frame(border)
    attach(root, border)
    attach(border, overlay)
    image = widget(bp, 'ThumbnailImage')
    scale = ensure(bp, 'ItemImageScale', unreal.ScaleBox)
    scale.set_stretch(unreal.Stretch.SCALE_TO_FIT)
    attach(scale, image)
    attach(overlay, scale)
    quantity = text(bp, 'QuantityTextBlock', '', 16 if size < 150 else 21)
    qslot = attach(overlay, quantity)
    qslot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_RIGHT)
    qslot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_BOTTOM)
    hover = widget(bp, 'OverShow')
    hover.set_brush_color(unreal.LinearColor(.65,.40,.12,.12))
    hover_slot = attach(overlay, hover)
    hover_slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    hover_slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
    save(bp)
    return bp.generated_class()


def build():
    """导入空白纸底，制作独立大小格，再重排团队页和专属四格装配页；推荐 ID 保持策划原值。"""
    task = unreal.AssetImportTask()
    task.filename = str(ROOT/'SourceArt/UI/CampInventory/CampInventoryPaper.png')
    task.destination_path = '/Game/UI/Inventory/Art'
    task.destination_name = 'T_CampInventoryPaper'
    task.automated = True
    task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset('/Game/UI/Inventory/Art/T_CampInventoryPaper')
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property('never_stream', True)
    unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False)
    task.filename = str(ROOT/'SourceArt/UI/CampInventory/CampMascot.png')
    task.destination_name = 'T_CampMascot'
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mascot = unreal.load_asset('/Game/UI/Inventory/Art/T_CampMascot')
    mascot.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    mascot.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    mascot.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    unreal.EditorAssetLibrary.save_loaded_asset(mascot, only_if_is_dirty=False)
    small = style_slot('WBP_CatCampStorageSlot', 106)
    large = style_slot('WBP_CatCampLoadoutSlot', 200)
    loadout = duplicate('/Game/UI/Inventory/WBP_CatInventory', '/Game/UI/Inventory/WBP_CatCampLoadout')
    root = widget(loadout, 'SafeZone_0')
    root.clear_children()
    wrap = widget(loadout, 'InventorySlotWrapBox')
    wrap.set_editor_property('explicit_wrap_size', True)
    wrap.set_editor_property('wrap_size', 424)
    wrap.set_inner_slot_padding(unreal.Vector2D(24,16))
    # 团队页现有图表读取这个 Border 调整背景；保留真实消费者需要的名字和类型，仅把底色设为透明。
    background = ensure(loadout, 'BackPackBackGround', unreal.Border)
    background.set_brush_color(unreal.LinearColor(0,0,0,0))
    background.set_padding(unreal.Margin(0,0,0,0))
    attach(root, background)
    attach(background, wrap)
    unreal.get_default_object(loadout.generated_class()).set_editor_property('inventory_slot_widget_class', large)
    save(loadout)
    bp = unreal.load_asset('/Game/UI/Inventory/WBP_CatCampInventory')
    root = widget(bp, 'SafeZone_42')
    root.clear_children()
    scale = ensure(bp, 'CampPageScale', unreal.ScaleBox)
    scale.set_stretch(unreal.Stretch.SCALE_TO_FIT)
    attach(root, scale)
    bounds = ensure(bp, 'CampPageBounds', unreal.SizeBox)
    bounds.set_width_override(1536)
    bounds.set_height_override(1024)
    attach(scale, bounds)
    canvas = ensure(bp, 'CampPageCanvas', unreal.CanvasPanel)
    canvas.clear_children()
    attach(bounds, canvas)
    paper = ensure(bp, 'CampPaper', unreal.Image)
    paper.set_brush_from_texture(texture)
    paper.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    place(canvas,paper,0,0,1536,1024)
    emblem = ensure(bp,'CampMascot',unreal.Image)
    emblem.set_brush_from_texture(mascot)
    emblem.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    place(canvas,emblem,565,0,110,110)
    place(canvas,text(bp,'CampTitle','营地仓库',36),662,38,290,64)
    place(canvas,text(bp,'CampSubtitle','整理你的钓鱼装备，为下一次出发做好准备',19),360,108,816,40)
    close = ensure(bp,'CloseInventoryButton',unreal.Button)
    close.set_background_color(unreal.LinearColor(0,0,0,0))
    attach(close,text(bp,'CampCloseLabel','×',40))
    place(canvas,close,1440,26,66,66)
    place(canvas,text(bp,'CampPrepareTitle','准备出发',29),94,190,426,48)
    place(canvas,text(bp,'CampPrepareHint','选择最多4件装备随身携带',18),70,246,474,36)
    nested = ensure(bp,'CampLoadout',loadout.generated_class())
    place(canvas,nested,94,296,424,416)
    place(canvas,text(bp,'CampStorageTitle','营地仓库',27),878,198,330,46)
    place(canvas,text(bp,'CapacityText','0 / 0',18),1325,203,130,40)
    scroll = ensure(bp,'CampStorageScroll',unreal.ScrollBox)
    place(canvas,scroll,644,266,820,590)
    wrap = widget(bp,'InventorySlotWrapBox')
    wrap.set_editor_property('explicit_wrap_size', True)
    wrap.set_editor_property('wrap_size', 804)
    wrap.set_inner_slot_padding(unreal.Vector2D(10,12))
    attach(scroll,wrap)
    panel = ensure(bp,'TrackingPanel',unreal.CanvasPanel)
    panel.clear_children()
    place(canvas,panel,64,719,504,230)
    place(panel,text(bp,'TrackingNameText','',21),0,0,504,42)
    for prefix,label,x in [('Bait','推荐鱼饵',0),('Chum','推荐鱼窝',258)]:
        border = ensure(bp,'Recommended'+prefix+'Frame',unreal.Border)
        frame(border)
        place(panel,border,x,46,244,176)
        col = ensure(bp,'Recommended'+prefix+'Column',unreal.VerticalBox)
        col.clear_children()
        attach(border,col)
        attach(col,text(bp,'Recommended'+prefix+'Label',label,19))
        fit = ensure(bp,'Recommended'+prefix+'Scale',unreal.ScaleBox)
        fit.set_stretch(unreal.Stretch.SCALE_TO_FIT)
        slot = attach(col,fit)
        slot.set_size(unreal.SlateChildSize(value=1,size_rule=unreal.SlateSizeRule.FILL))
        image = ensure(bp,'Recommended'+prefix+'Image',unreal.Image)
        attach(fit,image)
    panel.set_visibility(unreal.SlateVisibility.COLLAPSED)
    place(canvas,text(bp,'CampDragHint','拖拽装备到左侧格子中',18),780,894,554,40)
    place(canvas,text(bp,'InventoryActionResultText','',16),644,943,820,34)
    unreal.get_default_object(bp.generated_class()).set_editor_property('inventory_slot_widget_class',small)
    save(bp)
    unreal.log('Event=camp_inventory_style_saved RecommendationIds=DesignerOwned')


build()
