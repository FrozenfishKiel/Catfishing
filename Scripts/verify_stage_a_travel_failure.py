"""在安全副本移除 Lake 资产后，验证当前 Create→TravelFailure→Destroy 补偿与新 Request 重试。"""

import time
import unreal


# 只保存有限阶段、代际和回调句柄；Online 子系统每 tick 重新枚举，避免跨失败旅行持旧 UObject。
_context = {
    # 当前缺包补偿阶段；每个 tick 只执行匹配分支。
    "stage": "BOOTSTRAP",
    # 当前阶段的 monotonic 截止时间；超时转统一失败清理。
    "deadline": 0.0,
    # Slate pre-tick 句柄；pass/fail 退出前必须注销。
    "callback": None,
    # 结果是否已记录；避免后续 tick 重复日志与退出。
    "terminal_reported": False,
    # 首次 Create 的 RequestId 规范文本键；补偿完成前 Snapshot 必须一直与同一 GUID 值关联，不保存 wrapper 对象。
    "first_request_key": None,
    # 首次 Create 前的 operation epoch；用于证明失败补偿确实使旧代际失效。
    "first_epoch": -1,
    # 是否已观察到 TravelFailed+无 Session 的补偿终态。
    "failure_observed": False,
    # 补偿后的新 Create 是否以新 RequestId/新 epoch 受理。
    "retry_accepted": False,
}


def _guid_key(value) -> str:
    """通过 UE 5.8 正式 to_string ScriptMethod 提取稳定 GUID 文本；不依赖 wrapper identity、相等实现或 __str__。"""
    return value.to_string().strip().lower()


def _set_stage(stage: str, timeout_seconds: float = 60.0) -> None:
    """切换阶段并建立有界等待；缺包失败、Destroy 补偿和 EndPIE 都不得无限挂起。"""
    _context["stage"] = stage
    _context["deadline"] = time.monotonic() + timeout_seconds


def _active_online():
    """取得唯一非 CDO Online 子系统；多 PIE GameInstance 时返回空而不是混读回调代际。"""
    default_object = unreal.get_default_object(unreal.CatOnlineSubsystem)
    candidates = [obj for obj in unreal.ObjectIterator(unreal.CatOnlineSubsystem) if obj != default_object]
    return candidates[0] if len(candidates) == 1 else None


def _finish_editor() -> None:
    """注销 Slate 回调并正常退出 Editor，确保失败终态不会继续轮询已销毁 World。"""
    if _context["callback"] is not None:
        unreal.unregister_slate_pre_tick_callback(_context["callback"])
        _context["callback"] = None
    unreal.SystemLibrary.quit_editor()


def _fail(reason: str) -> None:
    """只写一次结构化失败；PIE 存在时先请求 EndPIE 再退出。"""
    if _context["terminal_reported"]:
        return
    _context["terminal_reported"] = True
    unreal.log_error(f"STAGE_A_TRAVEL_FAILURE_FAIL:{reason}")
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if level_editor.is_in_play_in_editor():
        level_editor.editor_request_end_play()
        _set_stage("FAIL_WAIT_END", 30.0)
    else:
        _finish_editor()


def _pass() -> None:
    """确认失败补偿回到无 Session、代际前进且新 Request 已受理后报告通过。"""
    if _context["terminal_reported"]:
        return
    _context["terminal_reported"] = True
    unreal.log("STAGE_A_TRAVEL_FAILURE_PASS:late_callback_covered_by_epoch_observation_only=true")
    _finish_editor()


def _on_slate_pre_tick(delta_seconds: float) -> None:
    """逐 tick 驱动缺包失败。先处理冷启动、失败/超时与已结束 PIE，再重取 Online；首次 Create 记下关联键，补偿阶段验证四事实与 epoch，最后以新请求证明旧操作已释放。"""
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    stage = _context["stage"]
    if stage == "BOOTSTRAP":
        # 必须从非 PIE 冷启动，避免混入人工运行的 Session/World 事实。
        if level_editor.is_in_play_in_editor():
            _fail("PIE_already_running")
            return
        level_editor.editor_request_begin_play()
        _set_stage("WAIT_FRONTEND")
        return
    if stage == "FAIL_WAIT_END":
        # 任何失败都先正常 EndPIE；EndPIE 卡住时仍有界注销并退出。
        if not level_editor.is_in_play_in_editor():
            _finish_editor()
        elif time.monotonic() > _context["deadline"]:
            _finish_editor()
        return
    if time.monotonic() > _context["deadline"]:
        # 补偿、重试和结束阶段不允许无界等待。
        _fail(f"timeout_stage={stage}")
        return
    if stage == "WAIT_END_PIE":
        # EndPIE 期间只等待；结束后 Online 可能已经销毁，直接用已冻结的补偿/重试事实完成最终断言。
        if level_editor.is_in_play_in_editor():
            return
        if not _context["failure_observed"] or not _context["retry_accepted"]:
            _fail("required_failure_checks_missing")
            return
        _pass()
        return
    online = _active_online()
    if online is None:
        return
    snapshot = online.get_snapshot()
    if stage == "WAIT_FRONTEND":
        # 首页收敛且无活动操作后才快照 epoch 并发起首次 Create。
        if snapshot.world_state != unreal.CatOnlineWorldState.FRONTEND or snapshot.active_operation != unreal.CatOnlineOperation.NONE:
            return
        _context["first_epoch"] = snapshot.operation_epoch
        result = online.request_create_session()
        if not result.accepted or result.error != unreal.CatOnlineError.NONE:
            _fail(f"first_create_rejected accepted={result.accepted} error={result.error}")
            return
        _context["first_request_key"] = _guid_key(result.request_id)
        _set_stage("WAIT_FAILURE_COMPENSATION")
        return
    if stage == "WAIT_FAILURE_COMPENSATION":
        # 先锁定首次 RequestId，再等操作结束；终态必须是 Frontend/NoSession/Failed/TravelFailed 且 epoch 已前进。
        if _guid_key(snapshot.request_id) != _context["first_request_key"]:
            _fail("request_id_changed_before_first_terminal")
            return
        if snapshot.active_operation != unreal.CatOnlineOperation.NONE:
            return
        if snapshot.last_error != unreal.CatOnlineError.TRAVEL_FAILED:
            _fail(f"unexpected_terminal_error={snapshot.last_error}")
            return
        if snapshot.world_state != unreal.CatOnlineWorldState.FRONTEND or snapshot.session_state != unreal.CatOnlineSessionState.NO_SESSION or snapshot.transport_state != unreal.CatOnlineTransportState.FAILED:
            _fail(f"compensation_fact_mismatch world={snapshot.world_state} session={snapshot.session_state} transport={snapshot.transport_state}")
            return
        if snapshot.operation_epoch <= _context["first_epoch"]:
            _fail(f"epoch_not_invalidated start={_context['first_epoch']} terminal={snapshot.operation_epoch}")
            return
        _context["failure_observed"] = True
        retry = online.request_create_session()
        retry_snapshot = online.get_snapshot()
        if (not retry.accepted or retry.error != unreal.CatOnlineError.NONE
                or _guid_key(retry.request_id) == _context["first_request_key"]):
            _fail(f"retry_rejected accepted={retry.accepted} error={retry.error}")
            return
        if retry_snapshot.operation_epoch <= snapshot.operation_epoch:
            _fail(f"retry_epoch_not_advanced previous={snapshot.operation_epoch} retry={retry_snapshot.operation_epoch}")
            return
        _context["retry_accepted"] = True
        level_editor.editor_request_end_play()
        _set_stage("WAIT_END_PIE")
        return
def main() -> None:
    """注册唯一 Slate pre-tick 回调；实际缺包准备仍由安全项目副本的人类步骤完成。"""
    _context["callback"] = unreal.register_slate_pre_tick_callback(_on_slate_pre_tick)
    unreal.log("Stage A current Online failure verification armed")


main()
