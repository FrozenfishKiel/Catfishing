"""统一加入、房间、设置及其动态行外观，保留原页面结构与业务绑定。"""
from pathlib import Path
import hashlib
import runpy
import shutil
import unreal

base = runpy.run_path(str(Path(__file__).with_name("style_frontend_save.py")))
color, slate, widget, font, button_style, ensure, attach = (base[k] for k in
    ("color", "slate", "widget", "font", "button_style", "ensure", "attach"))
NAMES = ("WBP_CatFrontendJoin", "WBP_CatFrontendRoom", "WBP_CatFrontendSettings",
         "WBP_CatJoinFriendRow", "WBP_CatRoomFriendRow", "WBP_CatRoomPlayerSlot")


def controls(bp):
    return [o for o in unreal.ObjectIterator(unreal.Widget)
            if o.get_path_name().startswith(bp.get_path_name()+":WidgetTree.")]


def brush_tint(brush,tint):
    brush.set_editor_property("tint_color",slate(tint))
    return brush


def panel(bp, child_name):
    child = widget(bp,child_name)
    surface = ensure(bp,child_name+"Surface",unreal.Border)
    surface.set_brush_color(color(.008,.028,.024,.80))
    surface.set_padding(unreal.Margin(22,20,22,20))
    if child.get_parent()==surface:
        return
    parent=child.get_parent()
    children=list(parent.get_all_children())
    properties=("padding","size","horizontal_alignment","vertical_alignment")
    slots=[{key:c.get_editor_property('slot').get_editor_property(key) for key in properties} for c in children]
    parent.clear_children()
    attach(surface,child)
    for old,props in zip(children,slots):
        slot=parent.add_child(surface if old==child else old)
        slot.set_editor_properties(props)


def main():
    if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError("Stop PIE before authoring")
    packages=["/Game/UI/Frontend/"+n for n in NAMES]
    dirty={p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    if dirty.intersection(packages):
        raise RuntimeError("Unsaved target edits: "+str(dirty.intersection(packages)))
    assets=[unreal.load_asset(p) for p in packages]
    if not all(assets):
        raise RuntimeError("Required frontend asset missing")
    project=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    backup=project/"Saved/Automation/FrontendPagesStyle/Backups"
    backup.mkdir(parents=True,exist_ok=True)
    for package in packages:
        source=project/"Content"/(package.removeprefix("/Game/")+".uasset")
        target=backup/(source.stem+"-"+hashlib.sha256(source.read_bytes()).hexdigest()+".uasset")
        if not target.exists(): shutil.copy2(source,target)
    for bp in assets:
        before={o.get_name():o.get_class() for o in controls(bp)}
        for o in controls(bp):
            name=o.get_name()
            if isinstance(o,unreal.TextBlock):
                size=32 if name in ("JoinTitleText","RoomTitleText","SettingsTitleText") else 21 if 'Title' in name else 16
                if 'Status' in name or 'Role' in name or 'Unavailable' in name: size=14
                font(o,size,color(.92,.79,.48) if 'Title' in name else color(.73,.82,.76))
                if 'Label' in name or 'Title' in name: o.set_auto_wrap_text(False)
                if name.endswith('LabelText'):
                    o.set_min_desired_width(170)
            elif isinstance(o,unreal.Button):
                button_style(o,name in ('JoinLinkButton','StartRoomGameButton','ApplySettingsButton'))
                label=unreal.find_object(None,bp.get_path_name()+':WidgetTree.'+name+'Label')
                if label:
                    foreground=unreal.SlateColor()
                    foreground.set_editor_property('color_use_rule',unreal.SlateColorStylingMode.USE_COLOR_FOREGROUND)
                    label.set_color_and_opacity(foreground)
            elif isinstance(o,unreal.EditableTextBox):
                style=o.get_editor_property('widget_style')
                ts=style.get_editor_property('text_style')
                face=ts.get_editor_property('font'); face.set_editor_property('size',16)
                ts.set_editor_property('font',face); style.set_editor_property('text_style',ts)
                style.set_editor_property('padding',unreal.Margin(14,10,14,10))
                for key in ('background_image_normal','background_image_hovered','background_image_focused'):
                    style.set_editor_property(key,brush_tint(style.get_editor_property(key),color(.025,.09,.075,.95)))
                o.set_editor_property('widget_style',style)
            elif isinstance(o,unreal.ComboBoxString):
                face=o.get_editor_property('font'); face.set_editor_property('size',17)
                o.set_editor_property('font',face)
                style=o.get_editor_property('widget_style')
                combo=style.get_editor_property('combo_button_style')
                button=combo.get_editor_property('button_style')
                for state,tint in {'normal':color(.028,.09,.075,.92),'hovered':color(.045,.20,.16),
                                   'pressed':color(.03,.13,.10),'disabled':color(.018,.04,.03,.80)}.items():
                    button.set_editor_property(state,brush_tint(button.get_editor_property(state),tint))
                button.set_editor_property('normal_padding',unreal.Margin(10,4,10,4))
                button.set_editor_property('pressed_padding',unreal.Margin(10,4,10,4))
                combo.set_editor_property('button_style',button)
                style.set_editor_property('combo_button_style',combo)
                o.set_editor_property('widget_style',style)
                o.set_editor_property('content_padding',unreal.Margin(8,2,8,2))
            elif isinstance(o,unreal.SizeBox) and name.endswith('RowBounds'):
                o.clear_height_override(); o.set_min_desired_height(64)
            elif isinstance(o,unreal.Slider):
                o.set_editor_property('slider_bar_color',color(.12,.26,.21))
                o.set_editor_property('slider_handle_color',color(.80,.74,.49))
            elif isinstance(o,unreal.ScrollBox):
                o.set_scrollbar_thickness(unreal.Vector2D(5,5))
                bar=o.get_editor_property('widget_bar_style')
                for key in ('normal_thumb_image','hovered_thumb_image','dragged_thumb_image'):
                    bar.set_editor_property(key,brush_tint(bar.get_editor_property(key),color(.23,.40,.32)))
                o.set_editor_property('widget_bar_style',bar)

        if bp.get_name() in NAMES[:3]:
            prefix={'WBP_CatFrontendJoin':'Join','WBP_CatFrontendRoom':'Room','WBP_CatFrontendSettings':'Settings'}[bp.get_name()]
            shade=widget(bp,prefix+'RootShade',unreal.Border)
            shade.set_brush_color(color(0,0,0,0))
            shade.set_padding(unreal.Margin(68,48,68,48))
            title=widget(bp,prefix+'TitleText')
            title.get_editor_property('slot').set_padding(unreal.Margin(0,0,0,20))
        else:
            surface=next(o for o in controls(bp) if isinstance(o,unreal.Border) and o.get_name().endswith('Background'))
            surface.set_brush_color(color(.025,.085,.068,.82))
            surface.set_padding(unreal.Margin(16,14,16,14))
            surface.get_parent().clear_height_override()
            surface.get_parent().set_min_desired_height(84)
            surface.get_editor_property('slot').set_padding(unreal.Margin(0,0,0,8))
        panels={'WBP_CatFrontendJoin':('JoinFriendsColumn','JoinLinkColumn'),
                'WBP_CatFrontendRoom':('FriendsColumn','PlayersColumn'),
                'WBP_CatFrontendSettings':('SettingsDetailsScrollBox','SettingsDescriptionBounds')}
        for name in panels.get(bp.get_name(),()): panel(bp,name)
        if bp.get_name()=='WBP_CatFrontendSettings':
            widget(bp,'SettingsDescriptionBounds').set_width_override(240)
            widget(bp,'SettingsDescriptionTextBlock').set_editor_property('wrap_text_at',230.0)
            for name in ('GameSettingsCategoryButton','GraphicsSettingsCategoryButton','AudioSettingsCategoryButton','ControlsSettingsCategoryButton'):
                widget(bp,name).get_editor_property('slot').set_padding(unreal.Margin(0,0,12,0))
        after={o.get_name():o.get_class() for o in controls(bp)}
        if any(after.get(n)!=cls for n,cls in before.items()):
            raise RuntimeError('Original control contract changed: '+bp.get_name())
        if not unreal.CatFrontendWidgetAuthoringLibrary.compile_styled_frontend_widget(bp):
            raise RuntimeError('Compile failed: '+bp.get_name())
        if not unreal.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=False):
            raise RuntimeError('Save failed: '+bp.get_name())
    if not unreal.CatFrontendWidgetAuthoringLibrary.validate_frontend_widget_blueprint_fonts():
        raise RuntimeError('Frontend font contract failed')
    unreal.log('Event=frontend_pages_style_applied Assets={} Backup={}'.format(len(assets),backup))


if __name__=='__main__': main()
