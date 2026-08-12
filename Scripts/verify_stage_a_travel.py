"""在已配置 Online 策略的安全 PIE 中验证当前 Session/World/Transport/Operation 四事实两轮收口。"""

import time
import unreal


# 状态机只保存标量和 Slate 句柄；旅行后每个 tick 重新枚举 UObject，避免跨 World 强持旧对象。
_context = {
    # 当前驱动阶段；所有 tick 分支只能读写它表示的一步。
    "stage": "BOOTSTRAP",
    # 当前阶段的 monotonic 截止时间；超时统一进入失败清理。
    "deadline": 0.0,
    # 已完成的 Host→Leave 往返数；达到 2 才允许 EndPIE。
    "round": 0,
    # Slate pre-tick 注册句柄；任何 pass/fail 出口都必须精确注销。
    "callback": None,
    # 终态日志是否已发布；防止连续 tick 重复 pass/fail。
    "terminal_reported": False,
    # 最近观察的 Online operation epoch；用于拒绝迟到回调造成的代际倒退。
    "last_epoch": -1,
    # 是否真实观察到 pending 重复命令拒绝；同步 OSS 可以合法保持 false。
    "pending_rejection_observed": False,
}


def _guid_key(value) -> str:
    """通过 UE 5.8 正式 to_string ScriptMethod 提取稳定 GUID 文本；不依赖 wrapper identity、相等实现或 __str__。"""
    return value.to_string().strip().lower()


def _set_stage(stage: str, timeout_seconds: float = 60.0) -> None:
    """切换阶段并刷新有界截止时间；任何平台回调或旅行迟到都只能在当前阶段内被观察。"""
    _context["stage"] = stage
    _context["deadline"] = time.monotonic() + timeout_seconds


def _active_online():
    """返回唯一非 CDO 的 PIE Online 子系统；多实例时拒绝猜测 GameInstance。"""
    default_object = unreal.get_default_object(unreal.CatOnlineSubsystem)
    candidates = [obj for obj in unreal.ObjectIterator(unreal.CatOnlineSubsystem) if obj != default_object]
    return candidates[0] if len(candidates) == 1 else None


def _active_widget_count() -> int:
    """统计当前视口中的 TravelWidget；每轮旅行都要求最多一个，EndPIE 后要求归零。"""
    return sum(1 for widget in unreal.ObjectIterator(unreal.CatTravelWidget) if widget.is_in_viewport())


def _finish_editor() -> None:
    """成对注销 Slate 回调并正常退出 Editor，避免终态后继续访问已销毁 PIE 对象。"""
    if _context["callback"] is not None:
        unreal.unregister_slate_pre_tick_callback(_context["callback"])
        _context["callback"] = None
    unreal.SystemLibrary.quit_editor()


def _fail(reason: str) -> None:
    """只报告一次结构化失败；PIE 仍在运行时先请求正常结束。"""
    if _context["terminal_reported"]:
        return
    _context["terminal_reported"] = True
    unreal.log_error(f"STAGE_A_TRAVEL_FAIL:{reason}")
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if level_editor.is_in_play_in_editor():
        level_editor.editor_request_end_play()
        _set_stage("FAIL_WAIT_END", 30.0)
    else:
        _finish_editor()


def _pass() -> None:
    """两轮 Host/Leave 均收口后报告成功；同步 OSS 不保证留下可观察的 pending 窗口，因此该覆盖单独注明。"""
    if _context["terminal_reported"]:
        return
    _context["terminal_reported"] = True
    unreal.log(
        "STAGE_A_TRAVEL_PASS:pending_rejection_observed="
        f"{_context['pending_rejection_observed']} sync_completion_supported=true"
    )
    _finish_editor()


def _check_snapshot_epoch(snapshot) -> bool:
    """确认 OperationEpoch 只单调前进；迟到回调若回写旧代际会被此检查捕获为倒退。"""
    if snapshot.operation_epoch < _context["last_epoch"]:
        _fail(f"epoch_regressed actual={snapshot.operation_epoch} previous={_context['last_epoch']}")
        return False
    _context["last_epoch"] = snapshot.operation_epoch
    return True


def _submit_create(online) -> None:
    """提交当前 Create 接口并核对 RequestId；若异步窗口仍存在，再验证重复请求只被 pending gate 拒绝。"""
    result = online.request_create_session()
    snapshot = online.get_snapshot()
    if (not result.accepted or result.error != unreal.CatOnlineError.NONE
            or _guid_key(result.request_id) != _guid_key(snapshot.request_id)):
        _fail(f"create_submit_mismatch accepted={result.accepted} error={result.error}")
        return
    if snapshot.active_operation == unreal.CatOnlineOperation.CREATE:
        duplicate = online.request_create_session()
        if duplicate.accepted or duplicate.error != unreal.CatOnlineError.COMMAND_ALREADY_PENDING:
            _fail(f"create_pending_gate_mismatch accepted={duplicate.accepted} error={duplicate.error}")
            return
        _context["pending_rejection_observed"] = True
    _set_stage("WAIT_LAKE")


def _submit_leave(online) -> None:
    """提交当前 Leave 接口；Host 路径必须先完成 Run teardown，再 DestroySession 并回 Frontend。"""
    result = online.request_leave()
    snapshot = online.get_snapshot()
    if (not result.accepted or result.error != unreal.CatOnlineError.NONE
            or _guid_key(result.request_id) != _guid_key(snapshot.request_id)):
        _fail(f"leave_submit_mismatch accepted={result.accepted} error={result.error}")
        return
    if snapshot.active_operation == unreal.CatOnlineOperation.LEAVE:
        duplicate = online.request_leave()
        if duplicate.accepted or duplicate.error != unreal.CatOnlineError.COMMAND_ALREADY_PENDING:
            _fail(f"leave_pending_gate_mismatch accepted={duplicate.accepted} error={duplicate.error}")
            return
        _context["pending_rejection_observed"] = True
    _set_stage("WAIT_FRONTEND")


def _on_slate_pre_tick(delta_seconds: float) -> None:
    """逐 tick 驱动两轮旅行。先处理失败/超时与已结束 PIE，再重取 Online 并校验 epoch/Widget，最后只在四份事实同时收敛时提交下一命令。"""
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    stage = _context["stage"]
    if stage == "BOOTSTRAP":
        # 冷启动只允许从非 PIE 状态发起，避免混入人工会话。
        if level_editor.is_in_play_in_editor():
            _fail("PIE_already_running")
            return
        level_editor.editor_request_begin_play()
        _set_stage("WAIT_INITIAL_FRONTEND")
        return
    if stage == "FAIL_WAIT_END":
        # 失败后只等待 EndPIE；即使 EndPIE 本身超时也必须注销回调并退出。
        if not level_editor.is_in_play_in_editor():
            _finish_editor()
        elif time.monotonic() > _context["deadline"]:
            _finish_editor()
        return
    if time.monotonic() > _context["deadline"]:
        # 所有正常阶段共用一个有界失败口，不允许静默挂起。
        _fail(f"timeout_stage={stage}")
        return
    if stage == "WAIT_END_PIE":
        # EndPIE 期间只等待；结束后 Online 可能已经销毁，因此必须先核对 View 生命周期再报告通过。
        if level_editor.is_in_play_in_editor():
            return
        if _active_widget_count() != 0:
            _fail(f"post_end_widget_count={_active_widget_count()}")
            return
        _pass()
        return
    online = _active_online()
    if online is None:
        return
    snapshot = online.get_snapshot()
    if not _check_snapshot_epoch(snapshot):
        return
    if _active_widget_count() > 1:
        _fail(f"duplicate_travel_widgets={_active_widget_count()}")
        return
    if stage == "WAIT_INITIAL_FRONTEND":
        # 首页必须是无 Session/无操作；先证明非法 Leave 被拒绝，再发 Create。
        if snapshot.world_state != unreal.CatOnlineWorldState.FRONTEND:
            return
        if snapshot.session_state != unreal.CatOnlineSessionState.NO_SESSION or snapshot.active_operation != unreal.CatOnlineOperation.NONE:
            _fail(f"initial_fact_mismatch session={snapshot.session_state} operation={snapshot.active_operation}")
            return
        invalid_leave = online.request_leave()
        if invalid_leave.accepted or invalid_leave.error != unreal.CatOnlineError.INVALID_STATE:
            _fail(f"frontend_leave_not_rejected accepted={invalid_leave.accepted} error={invalid_leave.error}")
            return
        _submit_create(online)
        return
    if stage == "WAIT_LAKE":
        # Create 的平台或旅行终态错误立即失败；只有 Host+Connected+Idle 才发 Leave。
        if snapshot.last_error != unreal.CatOnlineError.NONE and snapshot.active_operation == unreal.CatOnlineOperation.NONE:
            _fail(f"create_terminal_error={snapshot.last_error}")
            return
        if snapshot.world_state != unreal.CatOnlineWorldState.LAKE:
            return
        if snapshot.session_state != unreal.CatOnlineSessionState.HOST or snapshot.transport_state != unreal.CatOnlineTransportState.CONNECTED or snapshot.active_operation != unreal.CatOnlineOperation.NONE:
            _fail(f"lake_fact_mismatch session={snapshot.session_state} transport={snapshot.transport_state} operation={snapshot.active_operation}")
            return
        _submit_leave(online)
        return
    if stage == "WAIT_FRONTEND":
        # Leave 必须把 Session/Transport/Operation 全部收敛；首轮重新 Create，第二轮请求 EndPIE。
        if snapshot.last_error != unreal.CatOnlineError.NONE and snapshot.active_operation == unreal.CatOnlineOperation.NONE:
            _fail(f"leave_terminal_error={snapshot.last_error}")
            return
        if snapshot.world_state != unreal.CatOnlineWorldState.FRONTEND:
            return
        if snapshot.session_state != unreal.CatOnlineSessionState.NO_SESSION or snapshot.transport_state != unreal.CatOnlineTransportState.IDLE or snapshot.active_operation != unreal.CatOnlineOperation.NONE:
            _fail(f"frontend_fact_mismatch session={snapshot.session_state} transport={snapshot.transport_state} operation={snapshot.active_operation}")
            return
        _context["round"] += 1
        if _context["round"] < 2:
            _submit_create(online)
        else:
            level_editor.editor_request_end_play()
            _set_stage("WAIT_END_PIE")
        return
def main() -> None:
    """注册唯一 Slate pre-tick 回调；脚本本身不阻塞 Editor 主线程。"""
    _context["callback"] = unreal.register_slate_pre_tick_callback(_on_slate_pre_tick)
    unreal.log("Stage A current Online travel verification armed")


main()
