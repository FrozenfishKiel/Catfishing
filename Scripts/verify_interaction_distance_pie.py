"""在独立、有渲染的 Editor 进程验证准星身体半径；不保存任何资产或地图。

用 -ExecCmds="py <本文件>" 执行，配置 IPNetDriver、PIE ListenServer、2 players、
RunUnderOneProcess。仅用于独立测试进程：脚本结束 PIE 后退出编辑器。
以 INTERACTION_RANGE COMPLETE 和逐项 PASS 为成功证据，进程退出码不能替代断言。
"""
import time
import traceback
import unreal

state = dict(stage=0, case=0, at=time.monotonic(), started=time.monotonic(), ended=False, failures=[])
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.EditorLoadingAndSavingUtils.load_map('/Game/NaturePackage/Maps/Showcase2')
kiosk = next(a for a in actors.get_all_level_actors() if isinstance(a, unreal.CatShopKioskActor))
# 移动真实摊位靠近已生成的猫；不更改碰撞、接口、WBP、库存或角色运动模式。
camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 10500))
blocker = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -10000))
blocker.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
blocker.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
blocker.set_actor_scale3d(unreal.Vector(.5, .5, .5))
names = dict(kiosk=kiosk.get_name(), camera=camera.get_name(), blocker=blocker.get_name())
cases = [
    ('near_camera', unreal.Vector(0, -40, 320), False, False, True),
    ('look_down', unreal.Vector(0, -80, 600), False, False, True),
    ('distant_camera', unreal.Vector(0, -120, 900), False, False, True),
    ('out_of_reach', unreal.Vector(0, -40, 350), True, False, False),
    ('wall_occlusion', unreal.Vector(0, -80, 600), False, True, False),
    ('restored_focus', unreal.Vector(0, -80, 600), False, False, True),
]


def check(condition, message):
    """累计每个端点的失败，继续核对负向边界；末尾必须汇总为 FAIL。"""
    if condition:
        unreal.log('INTERACTION_RANGE PASS ' + message)
    else:
        state['failures'].append(message)
        unreal.log_error('INTERACTION_RANGE FAIL ' + message)


def named(world, cls, name):
    return next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, cls) if a.get_name() == name)


def local_players(world):
    return [p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerController)
            if p.is_local_controller() and p.get_controlled_pawn()]


def configure(worlds):
    """只移动摊位与测试镜头；猫保留真实地形支撑，不改交互组件或查询设置。"""
    name, offset, far, blocked, expected = cases[state['case']]
    for world in worlds:
        unreal.log('INTERACTION_RANGE CONFIG {} {} Step=MoveTarget'.format(name, world.get_path_name()))
        target = named(world, unreal.CatShopKioskActor, names['kiosk'])
        if world.get_path_name() not in state:
            pawns = [p.get_controlled_pawn() for p in local_players(world)]
            state[world.get_path_name()] = pawns[0].get_actor_location()
        sphere = target.get_component_by_class(unreal.SphereComponent)
        # 正式 BP 的查询球包含缩放，不能假设等于 C++ 默认 75 cm。
        # 把球顶放在猫附近；沿同一低头方向拉远镜头，保证命中面仍在 150 cm 内。
        center = state[world.get_path_name()] + unreal.Vector(0, 0,
                    500 if far else 65 - sphere.get_scaled_sphere_radius())
        target.set_actor_location(target.get_actor_location() + center - sphere.get_world_location(), False, True)
        view = named(world, unreal.CameraActor, names['camera'])
        view.set_actor_location(center + offset, False, True)
        view.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(center + offset, center), False)
        obstacle = named(world, unreal.StaticMeshActor, names['blocker'])
        obstacle.set_actor_location(center + offset * .5 if blocked else unreal.Vector(0, 0, -10000), False, True)
        unreal.log('INTERACTION_RANGE CONFIG {} {} Step=SetView'.format(name, world.get_path_name()))
        for player in local_players(world):
            player.set_view_target_with_blend(view, 0.0)
    unreal.log('INTERACTION_RANGE CASE ' + name)


def verify(worlds):
    """读生产 Timer 刷新的焦点、真实射线与正式提示；不直接设置 CurrentTarget。"""
    name, offset, far, blocked, expected = cases[state['case']]
    for world in worlds:
        target = named(world, unreal.CatShopKioskActor, names['kiosk'])
        for player in local_players(world):
            label = '{} {}'.format(name, world.get_path_name())
            targeting = player.get_component_by_class(unreal.CatInteractionTargetingComponent)
            check((targeting.get_current_target() == target) == expected, label + ' executable focus')
            check(targeting.get_observed_target() == target if not blocked else targeting.get_observed_target() != target,
                  label + ' first visible observation')
            pawn = player.get_controlled_pawn()
            size = player.get_viewport_size()
            ray = player.deproject_screen_position_to_world(size[0] * .5, size[1] * .5)
            hit = unreal.SystemLibrary.line_trace_single(world, ray[0], ray[0] + ray[1] * 1500,
                    unreal.TraceTypeQuery.ECC_VISIBILITY, True, [pawn], unreal.DrawDebugTrace.NONE)
            if hit:
                data = hit.to_tuple()
                distance = (data[5] - pawn.get_actor_location()).length()
                unreal.log('INTERACTION_RANGE SAMPLE {} RayCm={:.3f} ReachCm={:.3f} Hit={}'.format(label, data[3], distance, data[9].get_name()))
                check((distance <= 150) == (not far) if not blocked else True, label + ' fixture reach boundary')
            else:
                check(False, label + ' fixture ray hit')
            prompts = [widget for widget in unreal.ObjectIterator(unreal.CatInteractionPromptWidget)
                       if widget.get_world() == world and widget.get_owning_player() == player and widget.is_in_viewport()]
            check(bool(prompts), label + ' formal prompt mounted')
            if prompts:
                check(any(p.get_last_prompt_view_state().visible for p in prompts) == expected, label + ' formal prompt visibility')
            if name == 'look_down':
                endpoint = 'Host' if unreal.GameplayStatics.get_game_mode(world) else 'Client'
                unreal.SystemLibrary.execute_console_command(world,
                    'Shot SHOWUI filename=InteractionRange-{}.png -nosuffix'.format(endpoint), player)


def finish():
    if state['failures']:
        unreal.log_error('INTERACTION_RANGE FAILED Count={}'.format(len(state['failures'])))
    else:
        unreal.log('INTERACTION_RANGE COMPLETE')
    level.editor_request_end_play()
    state.update(ended=True, at=time.monotonic())


def tick(_delta):
    # 移动镜头/打开 WBP 可能泵送 Slate；禁止同一验证阶段重入。
    if state.get('busy'):
        return
    state['busy'] = True
    try:
        advance()
    finally:
        state['busy'] = False


def advance():
    now = time.monotonic()
    if state['ended']:
        if now - state['at'] > 2:
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.SystemLibrary.quit_editor()
        return
    try:
        if now - state['started'] > 120:
            raise RuntimeError('timeout stage={}'.format(state['stage']))
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if len(worlds) != 2 or sum(len(local_players(w)) for w in worlds) != 2:
            return
        if state['stage'] == 0:
            if now - state['started'] < 10:
                return
            check(all(p.get_controlled_pawn().get_class().get_name() == 'BP_CuteCatCharacter_C'
                      for w in worlds for p in local_players(w)), 'formal CuteCat on both endpoints')
            configure(worlds)
            state.update(stage=1, at=now)
        elif state['stage'] == 1 and now - state['at'] > 1:
            verify(worlds)
            state['case'] += 1
            if state['case'] < len(cases):
                configure(worlds)
                state['at'] = now
            else:
                finish()
    except Exception:
        check(False, traceback.format_exc())
        finish()


handle = unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
unreal.log('INTERACTION_RANGE START')
