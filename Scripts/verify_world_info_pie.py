"""在隔离 UE 编辑器进程运行真实双端 WorldInfo 回归；不保存地图、配置或测试鱼。

需要 PIE ListenServer、2 clients、单进程和 IPNetDriver，由调用进程提供临时 -ini 配置。
断言以 WORLD_INFO_PROBE COMPLETE/FAIL 落盘；退出码不能代替该完成标记。
"""
import time
import traceback
import unreal

# 本次未保存 PIE 的阶段记忆；只驱动测试与超时清理，不作为生产功能状态。
state = {'stage': 0, 'at': time.monotonic(), 'started': time.monotonic(), 'ended': False}
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.EditorLoadingAndSavingUtils.load_map('/Game/NaturePackage/Maps/Showcase2')
altar_template = next(a for a in editor_actors.get_all_level_actors() if isinstance(a, unreal.CatAltarActor))
tank_template = next(a for a in editor_actors.get_all_level_actors() if isinstance(a, unreal.CatFishTankActor))
camera = editor_actors.spawn_actor_from_class(unreal.CameraActor, altar_template.get_actor_location() + unreal.Vector(-650, -300, 220))
camera_name = camera.get_name()


def check(condition, message):
    """把当前断言结果写入新日志，失败立即交统一清理，不继续生成后续成功证据。"""
    if not condition:
        raise RuntimeError(message)
    unreal.log('WORLD_INFO_PROBE PASS {}'.format(message))


def finish(error=None):
    """结束本次未保存 PIE；恢复由独立进程退出完成，不保存任何测试中的地图或库存。"""
    if error:
        unreal.log_error('WORLD_INFO_PROBE FAIL {}'.format(error))
    else:
        unreal.log('WORLD_INFO_PROBE COMPLETE')
    level.editor_request_end_play()
    state.update(ended=True, at=time.monotonic())


def set_camera(player, target, offset):
    """移动本端测试相机并看向物体中下部；只调整观察位置，不修改生产信息组件锚点或遮挡配置。"""
    world = player.get_world()
    view = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor) if a.get_name() == camera_name)
    origin = target.get_actor_location()
    position = origin + offset
    view.set_actor_location(position, False, True)
    view.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position, origin + unreal.Vector(0, 0, 40)), False)
    player.set_view_target_with_blend(view, 0.0)


def child(widget, cls, name):
    """沿 UObject 所属链读取运行中的具名 UMG 控件，不开放或改写生产类的受保护字段。"""
    for candidate in unreal.ObjectIterator(cls):
        if candidate.get_name() != name:
            continue
        owner = candidate.get_outer()
        while owner:
            if owner == widget:
                return candidate
            owner = owner.get_outer()
    raise RuntimeError('Runtime WBP child missing: {}'.format(name))


def views(world):
    """读取实际创建的正式 WBP 及其列表载荷，日志区分可见状态与内容，不能用数据存在冒充屏幕可见。"""
    result = []
    for widget in unreal.ObjectIterator(unreal.CatWorldInfoWidget):
        if widget.get_world() != world:
            continue
        title = str(child(widget, unreal.TextBlock, 'TitleText').get_text())
        summary_title = str(child(widget, unreal.TextBlock, 'SummaryTitleText').get_text())
        detail = child(widget, unreal.WidgetSwitcher, 'DetailSwitcher').get_active_widget_index()
        listing = child(widget, unreal.ListView, 'InfoList' if detail == 1 else 'SummaryInfoList')
        rows = {str(item.get_editor_property('data').id): str(item.get_editor_property('data').value) for item in listing.get_list_items()}
        result.append({'widget': widget, 'title': title or summary_title, 'detail': detail, 'rows': rows,
            'visible': widget.get_visibility() == unreal.SlateVisibility.HIT_TEST_INVISIBLE and widget.is_in_viewport()})
    return result


def find_view(world, title):
    """按实际 WBP 已绑定标题定位本端视图；不存在即返回空，不创建替代实例帮助断言通过。"""
    return next((view for view in views(world) if view['title'] == title), None)


def tick(delta):
    """等待真实网络世界后依次观察日间祭坛、远距离鱼缸焦点和服务器库存变动；所有显示均由生产组件刷新。"""
    now = time.monotonic()
    if state['ended']:
        if now - state['at'] > 2:
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.SystemLibrary.quit_editor()
        return
    try:
        if now - state['started'] > 90:
            raise RuntimeError('timeout stage={}'.format(state['stage']))
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        server = next((w for w in worlds if unreal.GameplayStatics.get_game_mode(w)), None)
        client = next((w for w in worlds if not unreal.GameplayStatics.get_game_mode(w)), None)
        if not server or not client:
            return
        players = unreal.GameplayStatics.get_all_actors_of_class(server, unreal.CatfishingPlayerController)
        remote = unreal.GameplayStatics.get_player_controller(client, 0)
        if len(players) != 2 or not remote or any(not p.get_controlled_pawn() for p in players):
            return
        host = next(p for p in players if p.is_local_controller())
        remote_server = next(p for p in players if not p.is_local_controller())
        server_state = unreal.GameplayStatics.get_game_state(server)
        client_state = unreal.GameplayStatics.get_game_state(client)
        if not server_state or not client_state or client_state.get_run_public_state().phase.day_index == 0:
            return
        altar = next(iter(unreal.GameplayStatics.get_all_actors_of_class(server, unreal.CatAltarActor)), None)
        tank = next(iter(unreal.GameplayStatics.get_all_actors_of_class(server, unreal.CatFishTankActor)), None)
        client_altar = next(iter(unreal.GameplayStatics.get_all_actors_of_class(client, unreal.CatAltarActor)), None)
        client_tank = next(iter(unreal.GameplayStatics.get_all_actors_of_class(client, unreal.CatFishTankActor)), None)
        if not all([altar, tank, client_altar, client_tank]):
            return
        stage = state['stage']
        if stage == 0:
            for index, player in enumerate(players):
                player.get_controlled_pawn().set_actor_location(altar.get_actor_location() + unreal.Vector(-350, index * 100, 50), False, True)
            set_camera(host, altar, unreal.Vector(-650, -300, 220))
            set_camera(remote, client_altar, unreal.Vector(-650, -300, 220))
            unreal.SystemLibrary.execute_console_command(server, 'DisableAllScreenMessages', host)
            unreal.SystemLibrary.execute_console_command(client, 'DisableAllScreenMessages', remote)
            unreal.SystemLibrary.execute_console_command(server, 'cat.Fishing.Stats 0', host)
            unreal.SystemLibrary.execute_console_command(client, 'cat.Fishing.Stats 0', remote)
            state.update(stage=1, at=now)
        elif stage == 1 and now - state['at'] > 1.0:
            expected_camera = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(client, unreal.CameraActor) if a.get_name() == camera_name)
            if remote.get_view_target() != expected_camera:
                set_camera(remote, client_altar, unreal.Vector(-650, -300, 220))
                state['at'] = now
                return
            for world in [server, client]:
                found = find_view(world, '圣猫祭坛')
                unreal.log('WORLD_INFO_PROBE INITIAL World={} Views={}'.format(world.get_name(), views(world)))
                player = host if world == server else remote
                source = altar if world == server else client_altar
                unreal.log('WORLD_INFO_PROBE GEOMETRY Viewport={} Desired={} Anchor={} Camera={} Projection={}'.format(
                    player.get_viewport_size(), found['widget'].get_desired_size() if found else None,
                    source.get_component_by_class(unreal.CatAltarWorldInfoComponent).get_world_location(),
                    player.player_camera_manager.get_camera_location(),
                    player.project_world_location_to_screen(source.get_component_by_class(unreal.CatAltarWorldInfoComponent).get_world_location(), True)))
                check(found and found['visible'], 'daytime altar WBP is visibly attached on {}'.format(world.get_path_name()))
                check(found['detail'] == 1 and found['rows'].get('TankReserve') == '0 点', 'daytime full altar uses ready tank zero, not missing fallback')
                check(found['rows'].get('DailyTarget') == '13 点', 'altar target reads formal Run state')
            unreal.SystemLibrary.execute_console_command(server, 'Shot SHOWUI filename=WorldInfo_DayAltar.png -nosuffix', host)
            state.update(stage=1.5, at=now)
        elif stage == 1.5 and now - state['at'] > 0.5:
            for index, player in enumerate(players):
                player.get_controlled_pawn().set_actor_location(tank.get_actor_location() + unreal.Vector(-350, index * 100, 80), False, True)
            set_camera(host, tank, unreal.Vector(-450, 0, 180))
            set_camera(remote, client_tank, unreal.Vector(-450, 0, 180))
            state.update(stage=2, at=now)
        elif stage == 2 and now - state['at'] > 1.0:
            for world, player, local_tank in [(server, host, tank), (client, remote, client_tank)]:
                targeting = player.get_component_by_class(unreal.CatInteractionTargetingComponent)
                check(targeting.get_observed_target() == local_tank, 'same ray observes tank beyond original 3m on {}'.format(world.get_path_name()))
                check(targeting.get_current_target() is None, 'observation does not expand executable 3m target')
                found = find_view(world, '共享鱼缸')
                unreal.log('WORLD_INFO_PROBE TANK World={} View={}'.format(world.get_name(), found))
                check(found and found['visible'] and found['detail'] == 1, 'focused tank shows full formal WBP')
            unreal.SystemLibrary.execute_console_command(server, 'Shot SHOWUI filename=WorldInfo_TankFull.png -nosuffix', host)
            # 已有调试入口仅生成实物鱼；后续使用真实嘴叼、鱼护入库与客户端拖放 RPC，不直接改写库存或摘要字段。
            pawn = remote_server.get_controlled_pawn()
            pawn.set_actor_location(tank.get_actor_location() + unreal.Vector(-180, -100, 80), False, True)
            guard = next(iter(unreal.GameplayStatics.get_all_actors_of_class(server, unreal.CatFishGuardActor)))
            guard.set_actor_location(tank.get_actor_location() + unreal.Vector(-80, -210, 0), False, True)
            definition = unreal.load_asset('/Game/Catfishing/Data/Fish/Fish_Blackfish')
            check(definition.minimum_weight_kilograms <= 5.0 <= definition.maximum_weight_kilograms, 'fixture weight lies within real fish definition bounds')
            unreal.SystemLibrary.execute_console_command(server, 'cat.Fishing.Debug.GiveFish {} 5 {}'.format(definition.fish_definition_id, players.index(remote_server)), host)
            fish = next(iter(unreal.GameplayStatics.get_all_actors_of_class(server, unreal.CatFishPickupActor)))
            fish.set_actor_location(tank.get_actor_location() + unreal.Vector(-180, -50, 80), False, True)
            check(fish.interact(remote_server, unreal.GuidLibrary.new_guid()), 'real world fish enters mouth via authority interaction')
            check(guard.interact(remote_server, unreal.GuidLibrary.new_guid()), 'real carried fish submits to explicit guard')
            state.update(stage=3, at=now, guard_name=guard.get_name())
        elif stage == 3 and now - state['at'] > 1.0:
            client_guard = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(client, unreal.CatFishGuardActor) if a.get_name() == state['guard_name'])
            entries = client_guard.get_fish_inventory_component().get_inventory_entries()
            source_slot = next(i for i, entry in enumerate(entries) if entry.instance)
            unreal.SystemLibrary.execute_console_command(client, 'cat.WorldInfo.Probe.Move {} {} {} 0'.format(client_guard.get_name(), source_slot, client_tank.get_name()), remote)
            state.update(stage=4, at=now, source_slot=source_slot)
        elif stage == 4 and now - state['at'] > 1.0:
            for world in [server, client]:
                found = find_view(world, '共享鱼缸')
                check(found and found['rows'].get('OfferingReserve') == '4 点', '5kg tank reserve is four points on {}'.format(world.get_path_name()))
                check(found['rows'].get('FishCountCapacity') == '1 / 20', 'tank count and real capacity agree')
                check(found['rows'].get('OfferingShortfall') == '9 点', 'tank shortfall uses current target')
            client_guard = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(client, unreal.CatFishGuardActor) if a.get_name() == state['guard_name'])
            unreal.SystemLibrary.execute_console_command(client, 'cat.WorldInfo.Probe.Move {} 0 {} {}'.format(client_tank.get_name(), client_guard.get_name(), state['source_slot']), remote)
            state.update(stage=5, at=now)
        elif stage == 5 and now - state['at'] > 1.0:
            for world in [server, client]:
                found = find_view(world, '共享鱼缸')
                check(found and found['rows'].get('OfferingReserve') == '0 点', 'removal updates both replicated tank summaries')
            for player in [host, remote]:
                camera = player.get_view_target()
                rotation = camera.get_actor_rotation()
                rotation.yaw += 180.0
                camera.set_actor_rotation(rotation, False)
            state.update(stage=6, at=now)
        elif stage == 6 and now - state['at'] > 0.5:
            for world in [server, client]:
                check(not any(view['visible'] for view in views(world)), 'offscreen sources do not attach cards to screen edges')
            set_camera(host, tank, unreal.Vector(-450, 0, 180))
            set_camera(remote, client_tank, unreal.Vector(-450, 0, 180))
            tank.get_component_by_class(unreal.CatFishTankWorldInfoComponent).info_enabled = False
            state.update(stage=7, at=now)
        elif stage == 7 and now - state['at'] > 0.5:
            check(not find_view(server, '共享鱼缸')['visible'], 'local display switch hides only its own view')
            check(find_view(client, '共享鱼缸')['visible'], 'other endpoint keeps independent display state')
            tank.get_component_by_class(unreal.CatFishTankWorldInfoComponent).info_enabled = True
            state.update(stage=8, at=now)
        elif stage == 8 and now - state['at'] > 0.5:
            check(find_view(server, '共享鱼缸')['visible'], 're-enabling source restores original view')
            for local_tank in [tank, client_tank]:
                local_tank.get_component_by_class(unreal.CatFishTankWorldInfoComponent).destroy_component(local_tank)
            state.update(stage=9, at=now)
        elif stage == 9 and now - state['at'] > 0.5:
            for world in [server, client]:
                check(not any(view['widget'].is_in_viewport() and view['title'] == '共享鱼缸' for view in views(world)), 'destroying source removes its attached card')
            finish()
    except Exception:
        finish(traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
unreal.log('WORLD_INFO_PROBE START')
