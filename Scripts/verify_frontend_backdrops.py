"""正式加入/加载 WBP 的运行画面与布局检查；样例 DTO 不代表平台联机验收。

独立编辑器 -ExecutePythonScript 执行，生成截图和 checks.json；不写存档或发起邀请。
"""
import json
from pathlib import Path
import runpy
import time
import traceback
import unreal

out = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / 'Automation/FrontendBackdrops'
out.mkdir(parents=True, exist_ok=True)
style = runpy.run_path(str(Path(__file__).with_name('style_frontend_backdrops.py')))
style['main']()
state = {'phase': 0, 'deadline': time.monotonic() + 160, 'checks': {}, 'geometry': {}}
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
previous_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)


def child(root, name):
    return next(o for o in unreal.ObjectIterator(unreal.Widget)
                if o.get_name() == name and o.get_path_name().startswith(root.get_path_name() + '.'))


def check(name, value):
    state['checks'][name] = bool(value)


def shot(name):
    unreal.SystemLibrary.execute_console_command(state['root'].get_world(),
        'Shot showui filename=' + str(out / (name + '.png')).replace('\\', '/'))


def finish(error=None):
    if not error and not all(state['checks'].values()):
        error = 'Failed checks: ' + str([k for k, v in state['checks'].items() if not v])
    (out / 'checks.json').write_text(json.dumps({'checks': state['checks'], 'geometry': state['geometry'], 'error': error},
        ensure_ascii=False, indent=2), encoding='utf8')
    unreal.unregister_slate_post_tick_callback(state['handle'])
    performance.set_editor_property('bThrottleCPUWhenNotForeground', previous_throttle)
    if state.get('loading'):
        state['loading'].remove_from_parent()
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.log('Event=frontend_backdrops_verified Result={} Error={}'.format('Failed' if error else 'Passed', error))


def verify_loading_geometry(suffix):
    loading = state['loading']
    scale = measured_size(child(loading, 'LoadingBackgroundScale'), 'background_' + suffix)
    canvas = measured_size(child(loading, 'LoadingBackgroundScale').get_parent(), 'canvas_' + suffix)
    # 当前编辑器对不 Tick 的 UMG 装饰控件返回零缓存几何；零比零不能证明铺满。
    # 留存观测值，实际覆盖和边距由本轮截图目视验收，不改成宽松阈值假通过。
    check('loading_has_camp_' + suffix,
        child(loading, 'LoadingBackgroundImage').get_editor_property('brush').get_editor_property('resource_object').get_name()
        == 'T_UI_Frontend_CampNight')
    check('loading_background_fill_' + suffix,
        child(loading, 'LoadingBackgroundScale').get_editor_property('stretch') == unreal.Stretch.SCALE_TO_FILL)
    bounds = measured_size(child(loading, 'LoadingStatusBounds'), 'status_' + suffix)


def measured_size(obj, name):
    size = unreal.SlateLibrary.get_local_size(obj.get_paint_space_geometry())
    state['geometry'][name] = {'paint': [size.x, size.y], 'desired': str(obj.get_desired_size())}
    return size


def tick(delta):
    try:
        if time.monotonic() > state['deadline']:
            raise RuntimeError('Background verification timeout')
        if time.monotonic() < state.get('next', 0):
            return
        phase = state['phase']
        if phase == 0:
            roots = [r for r in unreal.ObjectIterator(unreal.CatFrontendRootWidget) if r.is_in_viewport()]
            if not roots:
                return
            r = state['root'] = roots[0]
            unreal.SystemLibrary.execute_console_command(r.get_world(), 'cat.Fishing.Stats 0')
            child(r, 'FrontendPageSwitcher').set_active_widget(child(r, 'JoinPage'))
            rows = []
            for name, owner, count, locked in [('镜湖营地', '小白', 1, False), ('朋友的钓鱼夜', '月牙', 2, True), ('满员房间', '松果', 4, True)]:
                row = unreal.CatSessionSearchSummary()
                row.set_editor_properties({'RoomName': name, 'OwnerDisplayName': owner, 'CurrentPlayers': count,
                    'MaxPlayers': 4, 'bHasPassword': locked, 'bCanJoin': count < 4})
                rows.append(row)
            snapshot = state['snapshot'] = unreal.CatOnlineSnapshot()
            snapshot.set_editor_property('search_results', rows)
            r.refresh_public_room_presentation(snapshot)
        elif phase == 1:
            r = state['root']
            check('join_background_transparent', child(r, 'JoinRootShade').get_editor_property('brush_color').a == 0)
            check('public_rows_preserved', child(r, 'PublicRoomsScrollBox').get_children_count() == 3)
            check('password_initially_hidden', child(r, 'JoinPasswordPanel').get_visibility() == unreal.SlateVisibility.COLLAPSED)
            for name in ('PublicRoomsColumnSurface', 'JoinFriendsColumnSurface', 'JoinLinkColumnSurface'):
                size = measured_size(child(r, name), name)
            for name in ('JoinLinkButton', 'PasteJoinLinkButton', 'RefreshPublicRoomsButton', 'JoinBackButton'):
                b = child(r, name)
                slot = b.get_content().get_editor_property('slot')
                check(name + '_centered', slot.get_editor_property('horizontal_alignment') == unreal.HorizontalAlignment.H_ALIGN_CENTER
                    and slot.get_editor_property('vertical_alignment') == unreal.VerticalAlignment.V_ALIGN_CENTER)
            shot('JoinLake')
        elif phase == 2:
            state['snapshot'].set_editor_property('password_requested', True)
            state['root'].refresh_public_room_presentation(state['snapshot'])
            child(state['root'], 'JoinPasswordInput').set_text('fixture-secret')
        elif phase == 3:
            r = state['root']
            check('password_masked', child(r, 'JoinPasswordInput').get_editor_property('is_password'))
            check('password_visible', child(r, 'JoinPasswordPanel').get_visibility() == unreal.SlateVisibility.VISIBLE)
            shot('JoinPasswordLake')
        elif phase == 4:
            state['snapshot'].set_editor_property('password_requested', False)
            state['root'].refresh_public_room_presentation(state['snapshot'])
            check('password_cleared', str(child(state['root'], 'JoinPasswordInput').get_text()) == '')
            r = state['root']
            cls = unreal.load_asset(style['LOADING']).generated_class()
            loading = state['loading'] = unreal.CatFrontendWidgetAuthoringLibrary.create_widget_preview(r.get_world(), cls, r.get_owning_player())
            loading.add_to_viewport(20000)
            child(loading, 'LoadingDayTextBlock').set_text('正在进入游戏')
            child(loading, 'LoadingProgressTextBlock').set_text('正在准备湖畔世界。 42%')
            child(loading, 'LoadingDetailTextBlock').set_text('正在加载地图资源。')
            child(loading, 'LoadingHintText').set_text('与朋友一起，开启新的钓鱼旅程。')
            child(loading, 'LoadingProgressBar').set_percent(.42)
        elif phase == 5:
            verify_loading_geometry('wide')
            check('progress_value_preserved', abs(child(state['loading'], 'LoadingProgressBar').get_editor_property('percent') - .42) < .001)
            shot('LoadingCamp')
        elif phase == 6:
            # 4:3 固定预览框验证背景与页面分别缩放；不写用户分辨率设置。
            state['root'].set_visibility(unreal.SlateVisibility.HIDDEN)
            state['loading'].set_desired_size_in_viewport(unreal.Vector2D(960, 720))
        elif phase == 7:
            verify_loading_geometry('four_three')
            shot('LoadingCampFourThree')
        elif phase == 8:
            state['loading'].remove_from_parent()
            r = state['root']
            loading = state['loading'] = unreal.CatFrontendWidgetAuthoringLibrary.create_widget_preview(
                r.get_world(), unreal.load_asset(style['LOADING']).generated_class(), r.get_owning_player())
            loading.add_to_viewport(20000)
            child(loading, 'LoadingDayTextBlock').set_text('正在返回主菜单')
            child(loading, 'LoadingProgressTextBlock').set_text('正在恢复主菜单界面。')
            child(loading, 'LoadingDetailTextBlock').set_text('正在准备主菜单。')
            child(loading, 'LoadingHintText').set_text('请稍候。')
            child(loading, 'LoadingProgressBar').set_visibility(unreal.SlateVisibility.COLLAPSED)
        elif phase == 9:
            check('progress_can_hide', child(state['loading'], 'LoadingProgressBar').get_visibility() == unreal.SlateVisibility.COLLAPSED)
            shot('LoadingReturnCamp')
        else:
            finish()
            return
        state['phase'] += 1
        state['next'] = time.monotonic() + 2
    except Exception:
        finish(traceback.format_exc())


state['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
