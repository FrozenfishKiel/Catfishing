"""在独立编辑器进程运行双端 PIE，验证提前入夜不会把旧截止回调带入下一天。

通过 -ExecCmds="py <本文件绝对路径>" 启动；PIE/IP 设置由进程参数提供。
CAT_DAY_DEADLINE_CASE=manual（默认）或 natural；只改内存中的设置和未保存的关卡。
调用方必须检查 DAY_DEADLINE_PROBE COMPLETE/FAIL，不能只看编辑器退出码。
"""

import os
import time
import traceback
import unreal


case = os.environ.get('CAT_DAY_DEADLINE_CASE', 'manual')
assert case in ('manual', 'natural')
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
# 本脚本验证一房主一客户端；先保存端数，避免其他三端用例留下的编辑器偏好令等待条件永远无法满足。
play_settings = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.LevelEditorPlaySettings'))
original_client_count = play_settings.get_editor_property('PlayNumberOfClients')
play_settings.set_editor_property('PlayNumberOfClients', 2)
settings = unreal.get_default_object(unreal.load_class(None, '/Script/Catfishing.CatRunSettings'))
original_length = settings.get_editor_property('DayLengthSeconds')
original_progress = settings.get_editor_property('InitialWorldProgress')
settings.set_editor_property('DayLengthSeconds', 6.0)
settings.set_editor_property('InitialWorldProgress', 10)
unreal.EditorLoadingAndSavingUtils.load_map('/Game/NaturePackage/Maps/Showcase2')
camp = next(actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.CatCampHubActor))
origin = camp.get_actor_location() + unreal.Vector(-350, -100, 100)
altar = actors.spawn_actor_from_class(unreal.CatAltarActor, origin)
altar.get_component_by_class(unreal.StaticMeshComponent).set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
altar.set_actor_scale3d(unreal.Vector(0.5, 0.5, 0.5))
altar_name = altar.get_name()
started = time.monotonic()
state = {'stage': 0, 'ended': False}


def require(condition, message):
    """断言失败立即中断本场验证；成功只记录真实观察结果，不写玩法状态。"""
    if not condition:
        raise RuntimeError(message)
    unreal.log('DAY_DEADLINE_PROBE PASS case={} {}'.format(case, message))


def snapshot(world):
    """读取该端正式 GameState 快照，不从另一端复制结果来代替网络观察。"""
    return unreal.GameplayStatics.get_game_state(world).get_run_public_state()


def phase(run):
    """把引擎阶段枚举转换为可断言的名称，保持快照只读。"""
    return str(run.phase.phase)


def finish(error=None):
    """记录唯一终态后结束 PIE，并还原内存设置；稍后退出进程，不保存地图或用户配置。"""
    if error:
        unreal.log_error('DAY_DEADLINE_PROBE FAIL case={} {}'.format(case, error))
    else:
        unreal.log('DAY_DEADLINE_PROBE COMPLETE case={}'.format(case))
    level.editor_request_end_play()
    settings.set_editor_property('DayLengthSeconds', original_length)
    settings.set_editor_property('InitialWorldProgress', original_progress)
    play_settings.set_editor_property('PlayNumberOfClients', original_client_count)
    state.update(ended=True, ended_at=time.monotonic())


def tick(delta):
    """按真实双端阶段推进：准备发起者身体位置、缩短首日、自然或提前入夜、提交服务器确认。

    确认后持续跨过旧截止时间，核对两端过场没有失败、进度只扣一次且新天获得完整六秒。
    最后等待新天自然到点并继续观察夜晚不自动翻天；超时或断言失败统一清理进程。
    """
    if state['ended']:
        if time.monotonic() - state['ended_at'] > 2:
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.SystemLibrary.quit_editor()
        return
    try:
        if time.monotonic() - started > 80:
            raise RuntimeError('timeout stage={}'.format(state['stage']))
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        servers = [world for world in worlds if unreal.GameplayStatics.get_game_mode(world)]
        clients = [world for world in worlds if not unreal.GameplayStatics.get_game_mode(world)]
        gate = (state['stage'], len(servers), len(clients))
        if state.get('world_gate') != gate:
            state['world_gate'] = gate
            unreal.log('DAY_DEADLINE_PROBE stage/worlds={}'.format(gate))
        if len(servers) != 1 or len(clients) != 1:
            return
        server, client = servers[0], clients[0]
        if not unreal.GameplayStatics.get_game_state(server) or not unreal.GameplayStatics.get_game_state(client):
            return
        players = unreal.GameplayStatics.get_all_actors_of_class(server, unreal.CatfishingPlayerController)
        remote = unreal.GameplayStatics.get_player_controller(client, 0)
        if len(players) != 2 or not remote or any(not player.get_controlled_pawn() for player in players):
            return
        host = next(player for player in players if player.is_local_controller())
        target = next(actor for actor in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.CatAltarActor) if actor.get_name() == altar_name)
        run, replicated = snapshot(server), snapshot(client)
        now = unreal.GameplayStatics.get_time_seconds(server)
        if state['stage'] < 2:
            # 只有发起者需要现场触达；远端确认不再等待摄像机或准心对准祭坛。
            host.get_controlled_pawn().set_actor_location(origin + unreal.Vector(-130, 100, 0), False, True)
        if state['stage'] == 0:
            if 'DAY_ACTIVE' not in phase(run):
                return
            unreal.SystemLibrary.execute_console_command(server, 'cat.RunEnvironmentSocial.DayLength 2', host)
            state['old_deadline'] = snapshot(server).phase.deadline_server_time_seconds
            if case == 'manual':
                unreal.SystemLibrary.execute_console_command(server, 'cat.RunEnvironmentSocial.SkipToNight', host)
            state['stage'] = 1
        elif state['stage'] == 1:
            if 'NORMAL_NIGHT' not in phase(run) or 'NORMAL_NIGHT' not in phase(replicated):
                return
            require(target.interact(host, unreal.GuidLibrary.new_guid()), 'host initiates through altar authority entry')
            state['stage'] = 2
        elif state['stage'] == 2:
            confirmation = replicated.altar_confirmation
            if state.get('confirmation_gate') != str(confirmation.state):
                state['confirmation_gate'] = str(confirmation.state)
                unreal.log('DAY_DEADLINE_PROBE client_confirmation={}'.format(confirmation.state))
            if 'WAITING' not in str(confirmation.state):
                return
            if case == 'manual':
                require(now < state['old_deadline'] - 0.5, 'confirmation occurs before old deadline to reproduce stale callback')
            # 编辑器 Python 的 ScriptExecutionGuard 强制 RPC 本地执行；本脚本在服务器对应 Controller 提交意图。
            # 这里只验证昼夜截止和双端状态复制；真实 owning-client RPC 与 Slate 按键由 FormalThreeEndpoint 覆盖。
            remote_authority = next(player for player in players if not player.is_local_controller())
            remote_authority.call_method('ServerSetAltarConfirmation', args=(confirmation.request_id, True))
            state.update(stage=3, input_at=now)
        elif state['stage'] == 3:
            if run.day_transition.active or replicated.day_transition.active:
                state['saw_transition'] = True
            if not state.get('saw_transition') or run.day_transition.active or replicated.day_transition.active:
                return
            require(not run.day_transition.failed and not replicated.day_transition.failed, 'both endpoints finish transition without failure')
            require(run.phase.day_index == 2 and replicated.phase.day_index == 2
                and 'DAY_ACTIVE' in phase(run) and 'DAY_ACTIVE' in phase(replicated), 'both endpoints enter day two daylight')
            require(run.world_progress == 9 and replicated.world_progress == 9, 'empty offering applies one penalty on both endpoints')
            require(not remote.is_move_input_ignored() and not remote.is_look_input_ignored(), 'client input unlocked')
            require(run.phase.deadline_server_time_seconds - now > 5.5, 'new day receives a full six seconds after transition')
            state.update(stage=4, new_deadline=run.phase.deadline_server_time_seconds)
        elif state['stage'] == 4:
            if now < state['new_deadline'] - 0.05:
                if 'DAY_ACTIVE' not in phase(run) or 'DAY_ACTIVE' not in phase(replicated):
                    raise RuntimeError('new day ended before its own deadline')
            elif 'NORMAL_NIGHT' in phase(run) and 'NORMAL_NIGHT' in phase(replicated):
                require(now >= state['new_deadline'] - 0.05, 'natural night begins only at the new deadline')
                require(now > state['old_deadline'] + 0.5, 'observation crossed the old deadline')
                state.update(stage=5, night_at=now)
        elif state['stage'] == 5 and now > state['night_at'] + 2:
            require(run.phase.day_index == 2 and replicated.phase.day_index == 2
                and 'NORMAL_NIGHT' in phase(run) and 'NORMAL_NIGHT' in phase(replicated), 'night has no automatic next-day timer')
            require(run.world_progress == 9 and replicated.world_progress == 9, 'no duplicate settlement after either deadline')
            finish()
    except Exception:
        finish(traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
level.editor_request_begin_play()
unreal.log('DAY_DEADLINE_PROBE START case={}'.format(case))
