# Fishing Phase A Authority Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立正式钓鱼竖切的阶段 A 权威契约、并发防线、Water/Equipment 事务基础和 owner-only 结果通道，并提前提供 Rod/Hook/Fish 原生 Blueprintable Actor 契约，使后续皮肤、网格和动画接入不需要改写权威继承层。

**Architecture:** Session 继续作为会话唯一真相；阶段 A 只建立可独立编译与测试的协议和基础设施，不实现 GAS 输入、抛物线、咬口、搏鱼模拟或 StateTree 新节点。Water 将几何版本与聚鱼版本分离；Equipment 用单活动预留和终态 tombstone 保证鱼饵、磨损与断杆幂等；Command Component 只传输请求者私有结果，不决定阶段。Rod/Hook/Fish Actor 只复制表现事实并调用蓝图事件，任何皮肤、动画或 Mesh 都不能修改 Session、Equipment、Items 或权威判定几何。

**Tech Stack:** Unreal Engine 5.8、C++、GameplayTags、Actor/ActorComponent replication、UE Automation Tests、StateTree 现有宿主、Git 窄补丁与部分暂存。

## Global Constraints

- 当前分支是 `feature/fishing-system`，工作目录是 `D:\develop\Catfishing`。
- 禁止对工作树执行 `git reset`、`git stash`、`git checkout --`、`git clean` 或批量格式化。
- 下列用户改动必须原样保留：`Config/DefaultEditor.ini`、`Config/DefaultEngine.ini`、`Source/Catfishing/Framework/Game/CatGameplayTypes.h/.cpp` 的 Enhanced Input/Move/Look/Jump/Sprint 改动、`Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp` 的现有注释，以及所有未跟踪 `Content/` 资产。
- 阶段 A 不修改任何 `.ini`、`.uproject`、`.uasset` 或 `.umap`，不打开或保存 `BP_CatFishingController` 与 TestMap。
- 每次修改 `CatGameplayTypes.h/.cpp` 前重新读取 `git diff --`；只允许 Command Component 构造/Getter 和 Water AggregationRevision 接缝。不得触碰头文件当前 423–503 附近、实现当前 1258–1455 附近的用户输入代码。
- 当前 `CatGameplayTypes.cpp:190` 和 `DefaultEditor.ini` 已存在用户侧 whitespace 问题；验证新代码时使用限定文件清单，不顺手清理这些行。
- 现有旧 `Start/Assist/Scoop` RPC 在阶段 A 保留原样；阶段 B 才将其改为 Command Component 兼容转发。
- `CommitFishingFailure` 与旧 FailureBudget 路径保留兼容，但活动新式 Fishing reservation 必须阻止旧惩罚入口写入；新 StateTree 不再调用旧路径。
- 原生表现 Actor 必须 `Blueprintable`。蓝图只接入 Mesh、材质、皮肤、AnimBP、Montage/VFX/SFX 和表现事件；C++ 保持 ID、Revision、canonical anchors、Session/Attempt 关联与服务器 Transform 的唯一写权限。
- Rod 的 `RodTipAnchor`、`StandAnchor`、`GripAnchor` 是 canonical authority anchors；Stage C 从 Rod 功能定义覆盖其 Transform。皮肤只能挂到 `VisualRoot`，蓝图默认值不得成为抛竿、线长、操作站位或 IK 的服务器判定来源。
- FishEncounter Actor 的服务器 Transform 将是鱼世界位置唯一真相；Session Snapshot 不增加鱼位置字段。
- 所有新增测试先观察红灯，再写最小实现；每一任务绿灯后才进入下一任务。
- 新/干净文件按任务提交。对已有用户改动的 `CatGameplayTypes.h/.cpp` 只部分暂存本任务 hunk；提交前必须证明 cached diff 不包含 Move/Look/Jump/Sprint 或其他用户改动。

---

## Task 0: 固化用户工作树的可复验基线

**Artifacts (not committed):**

- Create: `Saved/CodexBaseline/phase-a/status-porcelain.txt`
- Create: `Saved/CodexBaseline/phase-a/tracked/` 下 5 个 dirty tracked 文件的逐字节副本
- Create: `Saved/CodexBaseline/phase-a/content-sha256.tsv`
- Create: `Saved/CodexBaseline/phase-a/controller-user-diff.patch`

本任务必须在任何源码修改前完成。Config/UI/Content 在阶段 A 完全不改，因此最终必须与这里的哈希完全一致。Controller 有两组明确允许的新增改动，因此最终用 baseline copy 对当前文件做 no-index diff，差异只能是 Water AggregationRevision 与 Command Component 的白名单行；任何原用户行删除或改写都算失败。

Baseline 是一次写入的保护证据：若 `Saved/CodexBaseline/phase-a/COMPLETE` 已存在，只验证已有文件并复用，禁止覆盖；若目录存在但没有 COMPLETE，说明上次固化中断，立即停止并人工核对，不能把当前工作树重新定义成基线。

- [ ] **Step 1: 建立 baseline 目录并保存完整状态**

```powershell
$baselineRoot = 'D:\develop\Catfishing\Saved\CodexBaseline\phase-a'
if (Test-Path "$baselineRoot\COMPLETE") { Write-Output 'Phase A baseline already complete; validate and reuse it without overwriting.'; return }
if (Test-Path $baselineRoot) { throw 'Incomplete Phase A baseline exists; stop for manual inspection.' }
New-Item -ItemType Directory -Force -Path 'D:\develop\Catfishing\Saved\CodexBaseline' | Out-Null
New-Item -ItemType Directory -Path "$baselineRoot\tracked" | Out-Null
git status --porcelain=v1 -uall | Out-File -Encoding utf8 -NoClobber "$baselineRoot\status-porcelain.txt"
```

- [ ] **Step 2: 复制 dirty tracked 文件并保存原始 Controller diff**

使用 `[System.IO.File]::Copy($source, $destination, $false)` 逐字节复制 `Config/DefaultEditor.ini`、`Config/DefaultEngine.ini`、`Source/Catfishing/Framework/Game/CatGameplayTypes.h`、`Source/Catfishing/Framework/Game/CatGameplayTypes.cpp`、`Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp` 到 `tracked/` 的同名扁平副本；第三个参数固定为 false，目标存在即抛错且绝不覆盖。随后执行：

```powershell
git diff --binary -- Source/Catfishing/Framework/Game/CatGameplayTypes.h Source/Catfishing/Framework/Game/CatGameplayTypes.cpp | Out-File -Encoding utf8 -NoClobber "$baselineRoot\controller-user-diff.patch"
Get-ChildItem "$baselineRoot\tracked" -File | Get-FileHash -Algorithm SHA256 | Sort-Object Path | Format-Table -AutoSize
```

- [ ] **Step 3: 为整个 Content 建立 path+SHA256 manifest**

```powershell
Get-ChildItem 'D:\develop\Catfishing\Content' -Recurse -File |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring('D:\develop\Catfishing\'.Length)
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        "$relative`t$hash"
    } | Out-File -Encoding utf8 -NoClobber "$baselineRoot\content-sha256.tsv"
```

- [ ] **Step 4: 核对 baseline 可读且不进入 Git**

确认 5 个副本、Controller patch 和 Content manifest 均非空后，最后执行 `New-Item -ItemType File -Path "$baselineRoot\COMPLETE"` 写完成标记；标记存在后所有 worker 只读复用。`git status --short Saved/CodexBaseline` 不得出现可提交文件。若 Saved 未被忽略，继续保留这些诊断文件但在每次 `git add` 中只使用显式源码路径，绝不暂存 baseline。

---

## Task 1: Fishing 核心类型、命令结果与 16 个 Native Event Tags

**Files:**

- Create: `Source/Catfishing/Fishing/CatFishingGameplayTags.h`
- Create: `Source/Catfishing/Fishing/CatFishingGameplayTags.cpp`
- Create: `Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.h`
- Create: `Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.cpp`
- Create: `Source/Catfishing/Fishing/Tests/CatFishingContractTests.cpp`
- Modify: `Source/Catfishing/Fishing/CatFishingTypes.h`

**Public contract:**

`ECatFishingPhase` 保留既有序列化 ordinal，并显式指定数值：

```cpp
enum class ECatFishingPhase : uint8
{
    Created = 0,
    Probe = 1,
    TrueBiteWindow = 2,
    HookedFight = 3,
    NearShore = 4,
    Resolved = 5,
    Terminated = 6,
    CastFlight = 7,
    Waiting = 8,
    AutoHauling = 9
};
```

逻辑顺序由 StateTree 明确编排，不依赖枚举大小。新增：

```cpp
enum class ECatFishingOutcome : uint8
{
    None,
    Caught,
    EmptyHook,
    HookWindowExpired,
    Escaped,
    RodBroken,
    CatInWater,
    Cancelled,
    Invalidated
};

enum class ECatFishMotionIntent : uint8
{
    None,
    CalmOrInward,
    StrugglingOutward,
    AutoHauling
};
```

`Fishing/Integration/CatFishingCommandTypes.h` 定义：

```cpp
enum class ECatFishingCommandType : uint8
{
    None,
    PlaceRod,
    OperateRod,
    LeaveRod,
    PackRod,
    ChangeRodSkin,
    BeginCast,
    RequestHook,
    SetReeling,
    PrimaryReleased,
    CancelFishing,
    RequestScoop,
    AssistFight,
    ContributeChum,
    TailRescue
};

enum class ECatFishingCommandError : uint8
{
    None,
    FeatureDisabled,
    RunClosed,
    CommandsClosed,
    InvalidIdentity,
    InvalidPayload,
    DependencyUnavailable,
    NoRod,
    RodOccupied,
    RodBroken,
    EquipmentRevisionConflict,
    RodActorRevisionConflict,
    InvalidWaterTarget,
    CastOutOfRange,
    WaterNotFound,
    AmbiguousWater,
    ActiveSessionExists,
    SessionNotFound,
    NotFisher,
    RevisionConflict,
    CastAttemptConflict,
    InputSequenceStale,
    InputSequenceGapTooLarge,
    InvalidPhase,
    WindowClosed,
    AlreadyResolved,
    NotNearShore,
    StaleScoopTarget,
    ScoopGeometryFailed,
    CooldownActive,
    GuardCapacityExceeded,
    CaptureAlreadyCommitted
};
```

命令上下文与关键命令固定为：

```cpp
USTRUCT(BlueprintType)
struct FCatRodCommandContext
{
    GENERATED_BODY()
    FGuid RequestId;
    FGuid RodActorId;
    int64 ExpectedEquipmentRevision = 0;
    int64 ExpectedRodActorRevision = 0;
};

USTRUCT(BlueprintType)
struct FCatFishingSessionCommandContext
{
    GENERATED_BODY()
    FGuid RequestId;
    FGuid FishingSessionId;
    int64 ExpectedRevision = 0;
    FGuid CastAttemptId;
};

USTRUCT(BlueprintType)
struct FCatBeginCastCommand
{
    GENERATED_BODY()
    FGuid RequestId;
    FGuid RodActorId;
    int64 ExpectedEquipmentRevision = 0;
    int64 ExpectedRodActorRevision = 0;
    FVector ClientCandidateWorldPoint = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FCatSetReelingCommand
{
    GENERATED_BODY()
    FGuid RequestId;
    FGuid FishingSessionId;
    FGuid CastAttemptId;
    FGuid ActivationCorrelationId;
    int64 InputSequence = 0;
    bool bReeling = false;
};
```

Place 不能引用尚未生成的 RodActor，固定使用独立 `FCatPlaceRodCommand { RequestId, ExpectedEquipmentRevision }`；Operate/Leave/Pack 使用 `FCatRodCommandContext`；ChangeSkin 另增加 `RodSkinDefinitionId`；Hook/Cancel/Scoop/Assist 使用 `FCatFishingSessionCommandContext`，其中 Scoop 另带仅供诊断的候选世界点；PrimaryReleased 与 SetReeling 使用相同 Session/Attempt/Correlation/InputSequence，但每个输入边沿有独立 RequestId。

统一结构化结果：

```cpp
USTRUCT(BlueprintType)
struct FCatFishingCommandResult
{
    GENERATED_BODY()
    ECatFishingCommandType CommandType = ECatFishingCommandType::None;
    bool bCommitted = false;
    ECatFishingCommandError Error = ECatFishingCommandError::DependencyUnavailable;
    FGuid RequestId;
    FGuid FishingSessionId;
    int64 Revision = 0;
    int64 SnapshotSequence = 0;
    int64 PhaseEpoch = 0;
    FGuid CastAttemptId;
    FGuid RodActorId;
    int64 RodActorRevision = 0;
    int64 EquipmentRevision = 0;
    FGuid SuggestedFishingSessionId;
};
```

新增 `FCatBeginCastResult` 包装统一结果。当前 `CatFishingTypes.h` 中使用 `FCatDomainCommandResult` 的 legacy `FCatScoopResult` 原样保留，阶段 A 不定义第二个同名类型，也不切换现有 Session/Items 捕获调用方；Task 8 的私有邮箱先传输统一 `FCatFishingCommandResult`，最终 Scoop 捕获结果在阶段 F 原子结算接入时一次迁移。`MapDomainCommandError` 只用于现有 Domain API 过渡，不向通用 `ECatDomainCommandError` 增加 Fishing 细分项。

Native tags 命名空间使用 `CatFishingGameplayTags`，精确注册：`CastLanded`、`CastFailed`、`ProbeTriggered`、`ProbeCompleted`、`FishSelectionFailed`、`EarlyHook`、`HookAccepted`、`WindowExpired`、`FishStaminaDepleted`、`CatStaminaDepleted`、`CatOverpowered`、`RodBroken`、`AutoHaulReachedShore`、`AutoHaulFailed`、`ScoopCommitted`、`Interrupted`，字符串均为 `Cat.Fishing.Event.<Name>`。

- [ ] **Step 1: 写契约红测**

在 `CatFishingContractTests.cpp` 添加：

- `Catfishing.Unit.Fishing.Contracts.NativeEventTagsAreRegisteredAndExact`
- `Catfishing.Unit.Fishing.Contracts.PhaseOutcomeAndCommandDefaultsAreFailClosed`
- `Catfishing.Unit.Fishing.Contracts.BeginCastDiscreteAndReelingCommandsExposeDifferentConcurrencyFields`
- `Catfishing.Unit.Fishing.Contracts.StructuredResultCarriesCurrentServerConcurrencyIdentity`

测试必须逐一比较 16 个 tag 的精确字符串；验证枚举默认 Outcome=None；验证 BeginCast 不含 SessionId 的语义；验证离散命令有 ExpectedRevision；验证 Reeling 不携带 ExpectedRevision 且默认 InputSequence 为 0。带 typed Rod 引用的 Attempt Snapshot 在 Task 5 的 Actor 类型可链接后加入并测试。

- [ ] **Step 2: 运行红灯构建**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
```

预期失败：UHT/C++ 报告 `ECatFishingOutcome`、命令结构或 `CatFishingGameplayTags` 尚未声明；不得通过删除测试获得绿灯。

- [ ] **Step 3: 实现最小契约**

使用 `UE_DECLARE_GAMEPLAY_TAG_EXTERN`/`UE_DEFINE_GAMEPLAY_TAG`，为所有 USTRUCT 字段增加 `UPROPERTY(BlueprintReadWrite)` 或只读结果的 `BlueprintReadOnly`。旧 `FCatFishingStartResult` 和旧 `FCatScoopCommand` 在兼容期保留，增加转换函数，不删除现有调用方。

- [ ] **Step 4: 构建并运行契约测试**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
New-Item -ItemType Directory -Force -Path 'D:\develop\Catfishing\Saved\Automation\phase-a-contracts\Report' | Out-Null
& 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\develop\Catfishing\Catfishing.uproject' -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache '-ExecCmds=Automation RunTests Catfishing.Unit.Fishing.Contracts;Quit' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=D:\develop\Catfishing\Saved\Automation\phase-a-contracts\Report' '-abslog=D:\develop\Catfishing\Saved\Automation\phase-a-contracts\Automation.log'
```

确认导出报告无 failed/notRun/inProcess，并检查日志中测试队列完整结束。

- [ ] **Step 5: 提交任务 1**

```powershell
git add Source/Catfishing/Fishing/CatFishingGameplayTags.h Source/Catfishing/Fishing/CatFishingGameplayTags.cpp Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.h Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.cpp Source/Catfishing/Fishing/CatFishingTypes.h Source/Catfishing/Fishing/Tests/CatFishingContractTests.cpp
git diff --cached --check
git commit -m "Add fishing authority contracts"
```

---

## Task 2: 独立的 Request/Attempt/InputSequence 幂等账本

**Files:**

- Create: `Source/Catfishing/Fishing/Integration/CatFishingCommandLedger.h`
- Create: `Source/Catfishing/Fishing/Integration/CatFishingCommandLedger.cpp`
- Create: `Source/Catfishing/Fishing/Tests/CatFishingCommandLedgerTests.cpp`

**Contract:**

`FCatFishingCommandLedger` 是服务器内存值类型，不是 UObject、Subsystem 或第二份 Session。它只保存每个 authority identity 的首次终态结果，以及每个 Session/Attempt/Correlation 的最后 Reeling InputSequence。

```cpp
class FCatFishingCommandLedger
{
public:
    bool TryGetTerminalResult(const FString& AuthorityIdentity, FGuid RequestId,
        FCatFishingCommandResult& OutResult) const;
    bool RecordTerminalResult(const FString& AuthorityIdentity,
        const FCatFishingCommandResult& Result);
    ECatFishingCommandError ValidateDiscrete(
        const FCatFishingSessionCommandContext& Command,
        FGuid CurrentFishingSessionId,
        int64 CurrentRevision,
        FGuid CurrentCastAttemptId) const;
    ECatFishingCommandError ValidateReeling(const FString& AuthorityIdentity,
        const FCatSetReelingCommand& Command,
        FGuid CurrentFishingSessionId,
        FGuid CurrentCastAttemptId,
        int64 MaximumInputSequenceAdvance = 1024) const;
    ECatFishingCommandError TryCommitReelingSequence(
        const FString& AuthorityIdentity,
        const FCatSetReelingCommand& Command,
        FGuid CurrentFishingSessionId,
        FGuid CurrentCastAttemptId,
        int64 MaximumInputSequenceAdvance = 1024);
    void Reset();
};
```

Terminal key 是 `AuthorityIdentity|RequestId`。Reeling key 是 `AuthorityIdentity|FishingSessionId|CastAttemptId|ActivationCorrelationId`。离散校验依次检查有效 Request/Session/Attempt、SessionId、Attempt 和 ExpectedRevision；Reeling 不读 Revision，只检查 SessionId、Attempt、正 InputSequence、严格递增和单次最大增量 1024。`ValidateReeling` 是只读预检；正式写入只能调用原子的 `TryCommitReelingSequence`，它复用相同校验并且只在结果为 `None` 时推进序号，不公开未校验写入口。失败不更新任何序号。

- [ ] **Step 1: 写三条红测**

- `Catfishing.Unit.Fishing.CommandLedger.FirstTerminalResultWinsAndReplayIsSideEffectFree`
- `Catfishing.Unit.Fishing.CommandLedger.StaleRevisionAndAttemptAreRejected`
- `Catfishing.Unit.Fishing.CommandLedger.ReelingSequenceIsMonotonicBoundedAndRevisionIndependent`

第三条测试顺序固定为：接受 seq 1；拒绝重复 1；拒绝 0；接受 1025；拒绝 2050 的越界跳号；随后接受 1026，证明越界请求没有污染账本。

同一测试还必须证明：相同 RequestId 在不同 authority 下彼此隔离；相同 Session/Attempt 的不同 authority 或 ActivationCorrelationId 可分别从 seq 1 开始；`Reset()` 同时清除 terminal replay 与 Reeling sequence；直接调用原子提交处理越界输入也不能污染下一条合法序号。

- [ ] **Step 2: 运行红灯构建**

运行 Task 1 的 Editor build，预期因 `CatFishingCommandLedger.h`/类型不存在而失败。

- [ ] **Step 3: 实现账本并保持纯内存**

不得读取 World、Controller、PlayerState 或客户端提供的 StableNetId；authority identity 由未来调用方在 RPC 边界重建后传入。记录结果时 RequestId 无效或 identity 为空必须返回 false。

- [ ] **Step 4: 构建并运行测试**

使用 filter `Catfishing.Unit.Fishing.CommandLedger`，报告路径 `Saved/Automation/phase-a-ledger/Report`。

- [ ] **Step 5: 提交任务 2**

```powershell
git add Source/Catfishing/Fishing/Integration/CatFishingCommandLedger.h Source/Catfishing/Fishing/Integration/CatFishingCommandLedger.cpp Source/Catfishing/Fishing/Tests/CatFishingCommandLedgerTests.cpp
git diff --cached --check
git commit -m "Add fishing command concurrency ledger"
```

---

## Task 3: Water GeometryRevision 与 AggregationRevision 分离

**Files:**

- Modify: `Source/Catfishing/Environment/CatWaterTypes.h`
- Modify: `Source/Catfishing/Environment/CatWaterRegion.h`
- Modify: `Source/Catfishing/Environment/CatWaterRegion.cpp`
- Modify: `Source/Catfishing/Environment/Tests/CatWaterRegionTests.cpp`
- Modify: `Source/Catfishing/Environment/Tests/CatWaterQuerySubsystemTests.cpp`
- Modify narrowly: `Source/Catfishing/Framework/Game/CatGameplayTypes.h`
- Modify narrowly: `Source/Catfishing/Framework/Game/CatGameplayTypes.cpp`
- Modify: `Source/Catfishing/Framework/Game/Tests/CatGameplayTypesTests.cpp`
- Modify: `Source/Catfishing/Fishing/CatFishingSession.cpp`

**Contract:**

```cpp
struct FCatWaterRegionSnapshot
{
    FName RegionId;
    int64 GeometryRevision = 0;
    int64 AggregationRevision = 0;
    FVector WorldCenter;
    FVector HalfExtent;
    FCatChumVector ChumPool;
};

struct FCatAggregationCommand
{
    FCatDomainCommandContext Context; // 只使用 identity 与 RequestId
    FName RegionId;
    int64 ExpectedAggregationRevision = 0;
    FCatChumVector Contribution;
    ECatAggregationSource Source;
};
```

`FCatAggregationResult` 增加显式 `AggregationRevision`。兼容期可同时把 `Command.Revision` 填成相同值，但新调用者不再读 `Context.ExpectedRevision`。

`ACatWaterRegion` 的编辑器字段改名为 `GeometryRevision`，默认 0，保持 runtime gate；运行时 `AggregationRevision` 默认 1，并以 `VisibleInstanceOnly, BlueprintReadOnly, meta=(AllowPrivateAccess="true")` 暴露诊断。现有 Content 扫描未发现已序列化 `RegionRevision`；实现前再次执行 `rg -a -l "RegionRevision|CatWaterRegion" Content`。如果再次为空，直接改名且不改 Config；如果命中，停止本任务并在同一计划内改用 `RegionRevision_DEPRECATED` 的 PostLoad 迁移，禁止静默让几何版本归零。

聚鱼成功只推进 AggregationRevision，GeometryRevision 永不变化。Session 的旧水域初始化检查改读 `GeometryRevision`。

- [ ] **Step 1: 护栏检查和 dirty diff 取证**

```powershell
git diff -- Source/Catfishing/Framework/Game/CatGameplayTypes.h Source/Catfishing/Framework/Game/CatGameplayTypes.cpp
rg -a -l "RegionRevision|CatWaterRegion" Content
```

保存输出到任务记录；不得修改用户输入 hunk。

用户已接受 `b9342f9` 混合提交作为新基线，因此两个 Controller 文件在本任务开始时必须相对当前 HEAD 保持 clean；`stash@{0}` 继续原样保留。若上述 `git diff` 非空，说明出现新的并发修改，立即停止本任务，不得暂存或覆盖。

- [ ] **Step 2: 先改测试形成红灯**

更新现有 Water 测试并新增断言：GeometryRevision 为 7；初始 AggregationRevision 为 1；首次聚鱼后 AggregationRevision 为 2；GeometryRevision 仍为 7；旧 aggregation revision 返回 RevisionConflict；WaterQuery 在聚鱼前后返回相同 GeometryRevision。

Review remediation 还需新增 `Catfishing.Unit.Framework.PlayerChum.InvalidPayloadReportsCurrentAggregationRevision`：WaterRegion 当前 AggregationRevision 为 2 时，Controller 的 InvalidPayload 结果必须同时返回 `AggregationRevision=2` 与兼容的 `Command.Revision=2`；空 WaterRegion 保持 0。

- [ ] **Step 3: 运行 Water 红灯构建/测试**

先运行 Editor build。预期为字段不存在的编译失败；若已编译，则运行 filter `Catfishing.Unit.Environment.Water` 并观察版本断言失败。

- [ ] **Step 4: 实现双版本并窄改现有 Controller 接缝**

Controller 只改以下 Water 接缝：

- `CatGameplayTypes.h` 的 `ServerContributeChum` 参数名为 `ExpectedAggregationRevision`。
- `CatGameplayTypes.cpp` 自然聚鱼读取 `MakeSnapshot().AggregationRevision`。
- 玩家聚鱼实现参数名、`Command.ExpectedAggregationRevision` 和失败结果中的当前 AggregationRevision。
- 玩家聚鱼 InvalidPayload 快路径统一调用私有静态 `MakeInvalidPlayerChumResult`；它只读取非空 WaterRegion 的当前 AggregationRevision，并同时填充结果的两个版本字段。仅允许为该 helper 添加对应 automation test friend，不扩大 Controller 公共 API。

不改旧 Fishing RPC，不改用户输入函数。

- [ ] **Step 5: 构建并运行 Water 测试**

运行 Editor build，再分别运行 `Catfishing.Unit.Environment.WaterRegion`、`Catfishing.Unit.Environment.WaterQuery` 与 `Catfishing.Unit.Framework.PlayerChum`。

- [ ] **Step 6: 显式暂存并审计 Controller 的 Water hunk**

由于用户已接受包含输入代码的 `b9342f9` 为基线，Controller 在本任务开始时是 clean；使用显式文件清单暂存本任务的 Water/Fishing/Controller 文件，然后执行：

```powershell
git diff --cached --name-only
git diff --cached -- Source/Catfishing/Framework/Game/CatGameplayTypes.h Source/Catfishing/Framework/Game/CatGameplayTypes.cpp
git diff --cached --check
```

cached Controller diff 必须只包含以下白名单语义：头文件 `ExpectedRegionRevision`→`ExpectedAggregationRevision`，实现中的同名参数、`MakeSnapshot().AggregationRevision`、`Command.ExpectedAggregationRevision`、失败结果的当前 AggregationRevision，以及 review remediation 所需的私有 `MakeInvalidPlayerChumResult`/单一 automation friend。若出现 `UEnhancedInputLocalPlayerSubsystem`、`UInputAction`、`UInputMappingContext`、`FInputActionValue`、`OnRep_Pawn`、`DefaultMappingContext`、`IA_Move`、`IA_Look`、`IA_Jump`、`IA_Run`、`ServerSetSprinting`、`MoveInput`、`LookInput`、`WalkMaxSpeed` 或 `SprintMaxSpeed`，立即执行 `git restore --staged` 仅撤销这两个 Controller 文件的暂存并停止；不得提交、不得改写工作树。

- [ ] **Step 7: 提交任务 3**

```powershell
git commit -m "Split water geometry and aggregation revisions"
```

---

## Task 4: Equipment 原子 Fishing use reservation

**Files:**

- Modify: `Source/Catfishing/Equipment/CatEquipmentTypes.h`
- Modify: `Source/Catfishing/Equipment/CatEquipmentComponent.h`
- Modify: `Source/Catfishing/Equipment/CatEquipmentComponent.cpp`
- Create: `Source/Catfishing/Equipment/Tests/CatEquipmentFishingUseTests.cpp`

**Public API:**

```cpp
FCatFishingUseReservationResult BeginFishingUse(
    FGuid FishingSessionId,
    FName RodDefinitionId,
    FName BaitDefinitionId,
    FName FloatDefinitionId,
    int64 ExpectedRevision);

FCatFishingUseOperationResult CommitFishingBait(FGuid FishingSessionId);
FCatFishingUseOperationResult SetAccumulatedFishingRodWear(
    FGuid FishingSessionId, int64 WearSequence, double AbsoluteTotal);
FCatFishingUseOperationResult CommitFishingRodWear(FGuid FishingSessionId);
FCatFishingUseOperationResult CommitFishingRodBreak(FGuid FishingSessionId);
FCatFishingUseOperationResult ReleaseFishingUse(FGuid FishingSessionId);
bool HasActiveFishingUse() const;
bool IsFishingUseActive(FGuid FishingSessionId) const;
```

结果显式返回 SessionId、`ECatDomainCommandError`、当前 EquipmentRevision、WearSequence、AbsoluteRodWear、RemainingRodDurability、`bReserved`/`bApplied` 和 `bRodBroken`。

私有 `FCatFishingUseRecord` 保存 Session/Definition IDs、ReservationRevision、LastWearSequence、AbsoluteRodWear、special-bait reserved/committed、wear committed、break committed 和 released。组件持有一个 `ActiveFishingUseSessionId` 与按 SessionId 保存的记录表；释放后的记录作为 tombstone 保留到 Character 生命周期结束，迟到写入只能重放或拒绝。

**Behavior:**

- Begin 在一次调用中验证 authority、有效 SessionId、ExpectedRevision、当前 Rod/Bait/Float 完全匹配、定义 runtime-ready、Rod 未破损、特殊饵库存至少 1、没有其他活动 reservation；失败不留下任何记录。
- Begin 只写私有 reservation，不推进公开 Equipment Revision。
- 活动 reservation 阻止 Configure、Repair 和旧 `CommitFishingFailure`。普通 Consumable 消费仍可进行，但不得把未提交的最后一份 reserved special bait 偷走。
- `CommitFishingBait` 对普通饵只记成功；特殊饵恰好扣 1 并推进一次公开 Revision。重放不再次扣除。
- WearSequence 首次必须为 1，之后必须恰好为上一值 + 1；同 sequence/同 absolute total 重放成功且不写入；低 sequence、跳号、同 sequence 异值、负值、NaN/Inf 或 absolute total 小于已接受值全部拒绝且不污染下一合法序号。
- `SetAccumulatedFishingRodWear` 不推进公开 Revision。
- `CommitFishingRodWear` 只应用一次累计磨损；累计值达到或超过剩余耐久时拒绝普通 wear commit，要求调用 Break。成功后推进一次 Revision。
- `CommitFishingRodBreak` 覆盖待提交普通 wear，耐久置 0、broken=true，只推进一次 Revision。
- Release 清活动占用，不返还已提交鱼饵/耐久；未提交事实自然丢弃；released tombstone 禁止后续产生新副作用。

- [ ] **Step 1: 建立 scoped Equipment fixture 和四条红测**

测试用真实 Game World、authority Pawn、`ACatfishingPlayerState` 和 `UCatEquipmentComponent`。临时修改 `GetMutableDefault<UCatEquipmentSettings>()` 时备份 `Definitions` 与 `ProfileLoadoutTrustPolicy`，作用域结束前恢复，不调用 `SaveConfig`。定义完整 Rod、special Bait 和 Float，RequiredUnlockId 保持 None；通过公开 Configure/Grant API 建立初始事实，不添加测试后门。

测试：

- `Catfishing.Unit.Equipment.FishingUse.BeginIsAtomicExclusiveAndReplaySafe`
- `Catfishing.Unit.Equipment.FishingUse.BaitCommitAndReleaseAreIdempotent`
- `Catfishing.Unit.Equipment.FishingUse.WearSequenceIsAbsoluteMonotonicAndCommittedOnce`
- `Catfishing.Unit.Equipment.FishingUse.RodBreakOverridesWearAndCommitsZeroOnce`
- `Catfishing.Unit.Equipment.FishingUse.ActiveReservationBlocksLegacyMutationsAndProtectsReservedBait`

最后一条在 Begin 成功后依次提交 Configure、Repair、旧 `CommitFishingFailure(DamageRod)` 和对 reserved special bait 的普通 Consume；每个入口都必须拒绝且 Revision、耐久、库存和 reservation 不变。再授予第二份同 bait 后，普通 Consume 只允许扣除未预留的那一份，reserved 的最后一份仍可由 `CommitFishingBait` 恰好提交一次。

- [ ] **Step 2: 运行红灯**

运行 Editor build，预期因 reservation API/结果类型不存在而失败。

- [ ] **Step 3: 实现最小协议并保护旧写入口**

使用小型私有 helper 完成记录查找、reserved bait 可用数量和结果构造。不得把 reservation 放入复制 Snapshot；客户端只观察正式消耗/耐久提交后的 Revision。

- [ ] **Step 4: 构建并运行 Equipment 测试**

运行 filter `Catfishing.Unit.Equipment.FishingUse`，再运行 `Catfishing.Unit.Equipment`，确认旧 Failure/Repair/Definition 测试仍通过。

- [ ] **Step 5: 提交任务 4**

```powershell
git add Source/Catfishing/Equipment/CatEquipmentTypes.h Source/Catfishing/Equipment/CatEquipmentComponent.h Source/Catfishing/Equipment/CatEquipmentComponent.cpp Source/Catfishing/Equipment/Tests/CatEquipmentFishingUseTests.cpp
git diff --cached --check
git commit -m "Add atomic fishing equipment reservations"
```

---

## Task 5: Rod、Hook、Fish Blueprintable Actor 契约壳

**Files:**

- Create: `Source/Catfishing/Fishing/Actors/CatFishingActorTypes.h`
- Create: `Source/Catfishing/Fishing/Actors/CatFishingRodActor.h`
- Create: `Source/Catfishing/Fishing/Actors/CatFishingRodActor.cpp`
- Create: `Source/Catfishing/Fishing/Actors/CatFishingHookActor.h`
- Create: `Source/Catfishing/Fishing/Actors/CatFishingHookActor.cpp`
- Create: `Source/Catfishing/Fishing/Actors/CatFishEncounterActor.h`
- Create: `Source/Catfishing/Fishing/Actors/CatFishEncounterActor.cpp`
- Create: `Source/Catfishing/Fishing/Tests/CatFishingActorContractTests.cpp`
- Modify: `Source/Catfishing/Fishing/CatFishingTypes.h`

**Authority/presentation split:**

- 所有类使用 `UCLASS(Blueprintable)`，开启复制，关闭 Tick。
- Rod 创建 `SceneRoot`、`VisualRoot`、私有 `RodTipAnchor`、`StandAnchor`、`GripAnchor`；Blueprint 只把 Mesh/皮肤组件挂到 `VisualRoot`。三个 canonical 组件设置 `bEditableWhenInherited=false`，不向 Blueprint 返回组件引用；BlueprintPure 接口只返回 `GetRodTipWorldTransform()`、`GetStandWorldTransform()`、`GetGripWorldTransform()` 的 FTransform 值。
- Hook 创建 `SceneRoot`、`VisualRoot`、`HookVisualAnchor`、`BobberVisualAnchor`、`BaitVisualAnchor`。
- Fish 创建 `SceneRoot`、`VisualRoot`，开启 replicated movement；蓝图不得写 Fish 的 authority Transform。
- Actor 不持有鱼体力、猫属性、Equipment 耐久、Items 容器或捕获 Outcome。

三个原生 Actor 类型可链接后，在 `CatFishingTypes.h` 增加 `FCatFishingAttemptSnapshot`，固定包含 `RequestId`、`FishingSessionId`、`CastAttemptId`、Fisher PlayerState、typed Rod Actor、Rod/Float/Bait ID、Equipment reservation Revision、Rod Actor Revision、服务器修正后的落点、WaterRegionId、GeometryRevision 和 `uint64 ServerRandomSeed`。默认对象的所有 ID 无效、引用为空、版本为 0。

Rod 复制状态包含 RodActorId、RodActorRevision、RodDefinitionId、RodSkinDefinitionId、Owner/Operator PlayerState、deployed/broken。Hook 状态包含 SessionId、CastAttemptId 和 `ECatFishingHookPresentationPhase { Unconfigured, CastFlight, Landed, Failed }`。Fish 状态包含 SessionId、CastAttemptId、FishDefinitionId、`ECatFishMotionIntent` 和 CurrentLineLength。

每个 Actor 提供 authority-only 初始化方法，拒绝客户端、无效 ID 和第二次不同身份初始化。Rod 首次初始化 Revision=1；后续 Stage C 的所有权威 mutation 只能通过单入口递增 Revision。Rod 同时保存三个私有 canonical local-transform 值；authority 判定与 BlueprintPure world-transform 结果都从这些值和 Actor Transform 计算，不读取可能被表现代码移动的 SceneComponent Transform。

**Blueprint hooks:**

```cpp
UFUNCTION(BlueprintImplementableEvent, Category="Fishing|Presentation")
void BP_OnRodPresentationChanged(
    const FCatFishingRodPresentationState& Previous,
    const FCatFishingRodPresentationState& Current);

UFUNCTION(BlueprintImplementableEvent, Category="Fishing|Presentation")
void BP_ApplyRodSkin(FName RodSkinDefinitionId);

UFUNCTION(BlueprintImplementableEvent, Category="Fishing|Presentation")
void BP_PlayRodPresentationEvent(FGameplayTag EventTag);

UFUNCTION(BlueprintImplementableEvent, Category="Fishing|Presentation")
void BP_OnHookPresentationChanged(
    const FCatFishingHookPresentationState& Previous,
    const FCatFishingHookPresentationState& Current);

UFUNCTION(BlueprintImplementableEvent, Category="Fishing|Presentation")
void BP_PlayHookPresentationEvent(FGameplayTag EventTag);

UFUNCTION(BlueprintImplementableEvent, Category="Fishing|Presentation")
void BP_OnFishPresentationChanged(
    const FCatFishEncounterPresentationState& Previous,
    const FCatFishEncounterPresentationState& Current);

UFUNCTION(BlueprintImplementableEvent, Category="Fishing|Presentation")
void BP_PlayFishPresentationEvent(FGameplayTag EventTag);
```

RepNotify 和服务器首次初始化都调用相同本地通知 helper。`BP_ApplyRodSkin` 只收到稳定 ID；Stage C 的 Presentation Registry 解析 Mesh/材质/动画集合。加载失败显示蓝图默认外观，不回滚权威 ID。

- [ ] **Step 1: 写 Actor 契约红测**

新增：

- `Catfishing.Unit.Fishing.Actors.NativeBasesAreBlueprintableAndExposePresentationEvents`
- `Catfishing.Unit.Fishing.Actors.RodCanonicalAnchorsAreSeparateFromVisualRoot`
- `Catfishing.Unit.Fishing.Actors.IdentityInitializationIsAuthorityOnlyAndIdempotent`
- `Catfishing.Unit.Fishing.Actors.FishEncounterOwnsReplicatedMovementButNoOutcomeState`
- `Catfishing.Unit.Fishing.Actors.AttemptSnapshotUsesTypedRodAndDefaultsFailClosed`

使用 `UClass::GetBoolMetaDataHierarchical(TEXT("IsBlueprintBase"))` 检查三个原生类可作为 Blueprint 父类，事件含 `FUNC_BlueprintEvent`，状态字段使用 RepNotify；测试只查三个 FTransform 值 Getter，确认没有返回 canonical SceneComponent 的 BlueprintCallable/Pure Getter。Fish state 类型中不得出现 Stamina、Outcome、Capture 或 Item 字段。不要为了测试引入 UnrealEd 或 `FKismetEditorUtilities` 依赖。

- [ ] **Step 2: 运行红灯**

运行 Editor build，预期缺少三个 Actor 类型。

- [ ] **Step 3: 实现组件、复制与蓝图事件**

只实现身份、复制和表现通知，不实现 Place/Operate/Cast/Fight。Rod canonical local-transform 初始 identity；Stage C 从 Rod Definition 设置私有缓存并同步 native anchor 可视化组件，Blueprint skin 不能写入权威缓存。

- [ ] **Step 4: 构建并运行 Actor 测试**

运行 filter `Catfishing.Unit.Fishing.Actors`。

- [ ] **Step 5: 提交任务 5**

```powershell
git add Source/Catfishing/Fishing/Actors Source/Catfishing/Fishing/CatFishingTypes.h Source/Catfishing/Fishing/Tests/CatFishingActorContractTests.cpp
git diff --cached --check
git commit -m "Add blueprintable fishing actor contracts"
```

---

## Task 6: Session 身份、公开 Snapshot、Outcome 和 RepNotify

**Files:**

- Modify: `Source/Catfishing/Fishing/CatFishingTypes.h`
- Modify: `Source/Catfishing/Fishing/CatFishingSession.h`
- Modify: `Source/Catfishing/Fishing/CatFishingSession.cpp`
- Modify: `Source/Catfishing/Fishing/CatFishingService.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingSessionTests.cpp`

**Snapshot contract:**

在既有字段上增加 `SnapshotSequence`、`PhaseEpoch`、`CastAttemptId`、Outcome、PhaseStartedServerTime、WindowEndsServerTime、Fisher PlayerState、typed Rod/Hook/Fish Actor refs、NormalizedFishStamina、bReeling 和 FishMotionIntent。保留现有 CombinedFishingStrength/CombinedFightStamina/FishFightStaminaRemaining 兼容字段，Stage E 再切换正式模拟消费者。Snapshot 不含 Fish world position。

Session 构造设置 `bReplicates=true`、`bAlwaysRelevant=true`、关闭 Tick。Snapshot 改为 `ReplicatedUsing=OnRep_Snapshot`，提供本机原生 multicast delegate。此处不接 UI；Stage F 的 UISubsystem 再订阅。

`InitializeSession` 前置两个服务器分配参数：

```cpp
bool InitializeSession(
    FGuid InFishingSessionId,
    FGuid InCastAttemptId,
    AController* FisherController,
    ACatCharacter* FisherCharacter,
    UCatFishDefinition* FishDefinition,
    FGuid FisherGuardContainerId,
    double FishWeightKilograms,
    const FCatWaterRegionSnapshot& WaterRegion);
```

初始化只接受两个有效且不同的 GUID，Revision=1、PhaseEpoch=1、SnapshotSequence 经首次 Publish 变为 1、Outcome=None。Service 在 Spawn 前分配 IDs、在 Character Transform 生成 Session、`SetOwner(FisherController)` 后调用 Initialize；失败 ID 作废且不复用。

为避免各调用点手写错版本，新增非 Blueprint 的 `ECatFishingSnapshotMutation { HighFrequency, Discrete, PhaseChange }`，由 `FCatFishingSessionSnapshot::AdvanceVersion(ECatFishingSnapshotMutation)` 统一执行：三类都增加 SnapshotSequence；Discrete 额外增加 Revision；PhaseChange 额外增加 Revision 与 PhaseEpoch。Session 的 `PublishSnapshot(Mutation)` 必须先调用该 helper 再 ForceNetUpdate，除初始化设置初始身份外不得再散落 `++Snapshot.Revision`/`++Snapshot.PhaseEpoch`。现有 FightExchange 使用 HighFrequency；Assist 使用 Discrete；阶段进入、捕获和终止使用 PhaseChange。普通 Snapshot 更新绝不改变 PhaseEpoch。

终止接口改为：

```cpp
void TerminateSession(ECatFishingOutcome Outcome, const TCHAR* DiagnosticReason);
```

只接受 EmptyHook、HookWindowExpired、Escaped、RodBroken、CatInWater、Cancelled、Invalidated；拒绝 None/Caught；一旦 Resolved/Terminated，后续调用不得改变 Outcome 或版本。`EnterPhaseFromStateTree` 同时拒绝 Resolved 与 Terminated，StateTree 不能写终态。现有 RetryExhausted 映射 Escaped；Character/Run teardown 映射 Invalidated；成功 Scoop 同一调用栈写 Resolved/Caught。

- [ ] **Step 1: 扩 Session 红测**

新增：

- `Catfishing.Unit.Fishing.Session.PublicSnapshotDefaultsExposeSeparatedConcurrencyIdentity`
- `Catfishing.Unit.Fishing.Session.TerminationRequiresExplicitOutcomeAndIsIrreversible`
- `Catfishing.Unit.Fishing.Session.StateTreeCannotEnterEitherTerminalPhase`
- `Catfishing.Unit.Fishing.Session.ActorIsAlwaysRelevantAndSnapshotUsesRepNotify`
- `Catfishing.Unit.Fishing.Session.SnapshotVersionMutationRulesSeparateHighFrequencyDiscreteAndPhaseChanges`

默认未初始化 Session 仍为 Created/None/零版本；显式 Invalidated 终止只推进一次；重复 Escaped 不能覆盖首次结果。版本测试类作为 `ACatFishingSession` 的窄 friend，在真实 authority Session Actor 上把版本设为 `{Revision=10, SnapshotSequence=20, PhaseEpoch=30}`，直接调用私有 `PublishSnapshot`，依次验证 HighFrequency→`10/21/30`、Discrete→`11/22/30`、PhaseChange→`12/23/31`。friend 不暴露运行时 UFUNCTION，也不能改 Equipment/Items。源码检查再证明 FightExchange 调用 HighFrequency、Assist 调用 Discrete、EnterPhase/捕获/终止调用 PhaseChange。

- [ ] **Step 2: 运行红灯**

运行 Editor build 或 Session filter，预期 Outcome/RepNotify/签名断言失败。

- [ ] **Step 3: 实现 Snapshot 和最小生命周期迁移**

逐个更新现有调用点，不改写旧 Fight/Failure/Scoop 算法主体。`OnRep_Snapshot` 只广播重读信号，不执行业务。构建前执行 `rg -n "\+\+Snapshot\.(Revision|PhaseEpoch|SnapshotSequence)" Source/Catfishing/Fishing/CatFishingSession.cpp`，除 `AdvanceVersion` 实现外不得命中。

- [ ] **Step 4: 构建并运行 Fishing Session 测试**

运行 `Catfishing.Unit.Fishing.Session`，并回归 `Catfishing.Unit.Fishing.StateTree`。

- [ ] **Step 5: 提交任务 6**

```powershell
git add Source/Catfishing/Fishing/CatFishingTypes.h Source/Catfishing/Fishing/CatFishingSession.h Source/Catfishing/Fishing/CatFishingSession.cpp Source/Catfishing/Fishing/CatFishingService.cpp Source/Catfishing/Fishing/Tests/CatFishingSessionTests.cpp
git diff --cached --check
git commit -m "Add fishing session public concurrency snapshot"
```

---

## Task 7: Service 只读 Session 查询与一人一杆 Registry

**Files:**

- Modify: `Source/Catfishing/Fishing/CatFishingService.h`
- Modify: `Source/Catfishing/Fishing/CatFishingService.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingServiceTests.cpp`
- Create: `Source/Catfishing/Fishing/Tests/CatFishingServiceRegistryTests.cpp`

**API:**

```cpp
ACatFishingSession* FindSession(FGuid FishingSessionId);
bool TryGetActiveSessionForController(
    const AController* Controller,
    FGuid& OutFishingSessionId,
    FCatFishingSessionSnapshot& OutSnapshot);
ACatFishingRodActor* FindDeployedRod(const APlayerState* PlayerState);
bool RegisterDeployedRod(APlayerState* PlayerState, ACatFishingRodActor* RodActor);
void UnregisterDeployedRod(
    const APlayerState* PlayerState,
    const ACatFishingRodActor* ExpectedRodActor);
int32 GetTrackedSessionCountForDiagnostics() const;
int32 GetDeployedRodCountForDiagnostics() const;
```

查询前 Compact；unknown、无效弱引用与 terminal Session 返回 null/false，输出重置，不创建 map entry。两个 diagnostics getter 只返回聚合计数，不暴露 key、弱引用或可写容器，供自动化和运行诊断证明查询无副作用。Rod registry 使用 PlayerState weak key/value；首次登记成功，同 Actor 重放成功，不同 Actor 被拒。注销必须比较 ExpectedRodActor，旧 Actor 的迟到 EndPlay 不能删除新登记 Actor。Deinitialize 清 registry。

- [ ] **Step 1: 写查询与 registry 红测**

- `Catfishing.Unit.Fishing.Service.UnknownSessionQueriesAreSideEffectFree`（调用前后 tracked session/rod diagnostics count 必须相同）
- `Catfishing.Unit.Fishing.Service.RodRegistryAllowsOneRodPerPlayerState`
- `Catfishing.Unit.Fishing.Service.StaleRodUnregisterCannotRemoveReplacement`

- [ ] **Step 2: 运行红灯**

运行 Editor build，预期缺少 Service API。

- [ ] **Step 3: 实现查询、压缩与精确注销**

不得为了测试暴露 `Sessions`/maps 或添加 mutable getter。Registry 不遍历 World，不信任客户端 Actor pointer。

- [ ] **Step 4: 构建并运行 Service 测试**

运行 `Catfishing.Unit.Fishing.Service`。

- [ ] **Step 5: 提交任务 7**

```powershell
git add Source/Catfishing/Fishing/CatFishingService.h Source/Catfishing/Fishing/CatFishingService.cpp Source/Catfishing/Fishing/Tests/CatFishingServiceTests.cpp Source/Catfishing/Fishing/Tests/CatFishingServiceRegistryTests.cpp
git diff --cached --check
git commit -m "Add fishing service session and rod registries"
```

---

## Task 8: PlayerController owning-client RPC 静态契约与有界私有结果邮箱

**Files:**

- Create: `Source/Catfishing/Fishing/Integration/CatFishingCommandComponent.h`
- Create: `Source/Catfishing/Fishing/Integration/CatFishingCommandComponent.cpp`
- Create: `Source/Catfishing/Fishing/Tests/CatFishingCommandComponentTests.cpp`
- Modify narrowly: `Source/Catfishing/Framework/Game/CatGameplayTypes.h`
- Modify narrowly: `Source/Catfishing/Framework/Game/CatGameplayTypes.cpp`

**API:**

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FCatFishingCommandResultReceived,
    const FCatFishingCommandResult&, Result);

UCLASS(ClassGroup=(Catfishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatFishingCommandComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCatFishingCommandComponent();
    void DeliverResultFromAuthority(const FCatFishingCommandResult& Result);
    UFUNCTION(BlueprintCallable)
    bool TryGetResult(FGuid RequestId, FCatFishingCommandResult& OutResult) const;
    UFUNCTION(BlueprintCallable)
    void ConsumeResult(FGuid RequestId);
    void ResetTransientCommandState();
    UPROPERTY(BlueprintAssignable)
    FCatFishingCommandResultReceived OnResultReceived;
private:
    UFUNCTION(Client, Reliable)
    void ClientReceiveFishingCommandResult(FCatFishingCommandResult Result);
};
```

组件默认复制、关闭 Tick，只允许 PlayerController Owner。结果必须有有效 RequestId；同 RequestId 第一次结果胜出，重放不重复广播。有界顺序表上限固定 32，插入第 33 个时淘汰最早结果。邮箱不复制、不写 Session Snapshot。Listen Server 本地 Controller 走一次本地接收；远端 owner 走可靠 Client RPC。

`ACatfishingPlayerController` 增加原生构造函数、BlueprintPure Getter 和 `VisibleAnywhere, BlueprintReadOnly` 默认子对象。构造/Getter 声明放在现有 `ServerRequestScoop` 后的 public 区，避开当前 283–287 的 OnPossess/OnRep 用户 hunk；组件属性只追加到类尾。不要在阶段 A 绑定 InputAction，不改旧 Fishing RPC，不在 UI Subsystem 订阅。

- [ ] **Step 1: 重新取证 dirty Controller diff**

```powershell
git diff -- Source/Catfishing/Framework/Game/CatGameplayTypes.h Source/Catfishing/Framework/Game/CatGameplayTypes.cpp
```

- [ ] **Step 2: 写 Command Component 红测**

- `Catfishing.Unit.Fishing.CommandComponent.ClientResultRpcDeclaresReliableOwningControllerContract`
- `Catfishing.Unit.Fishing.CommandComponent.RequestResultsAreCorrelatedDeduplicatedAndBounded`
- `Catfishing.Unit.Fishing.CommandComponent.ControllerCreatesExactlyOneNativeDefaultComponent`

反射断言 RPC 包含 `FUNC_NetClient|FUNC_NetReliable`、组件 Owner 必须是 `APlayerController` 且组件不出现在 Session Snapshot；测试插入 33 个结果后第 1 个不存在、第 2–33 个存在；同 RequestId 第二个不同结果不得覆盖首次结果或再次广播。`FTestWorldWrapper` 单 World 不能证明真实网络隐私，因此本测试只声明静态 owning-controller contract，不声称已经验证远端路由。

- [ ] **Step 3: 运行红灯**

运行 Editor build，预期组件/Controller Getter 不存在。

- [ ] **Step 4: 实现组件和最小 Controller 挂载**

头文件只在现有 `ServerRequestScoop` 声明后增加构造/Getter，在类尾追加组件属性；实现 include 放在现有 include 尾部，构造函数放在首个 GameMode 构造前。不得改 OnPossess、BeginPlay、SetupInputComponent、OnUnPossess、EndPlay 或输入绑定。

- [ ] **Step 5: 构建并运行 Component 测试**

运行 `Catfishing.Unit.Fishing.CommandComponent`，再运行 `Catfishing.Unit.Framework.Game` 回归 Controller 生命周期。

- [ ] **Step 6: 用显式 index-only patch 暂存 Controller 组件 hunk**

先暂存两个新组件文件和测试。随后使用 `apply_patch` 创建 `Saved/CodexBaseline/phase-a/command-component-controller-index.patch`，旧内容必须来自 Task 3 提交后的当前 `HEAD`，且只包含：稳定位置的 `UCatFishingCommandComponent` 前置声明、`ServerRequestScoop` 后的构造/Getter 声明、类尾组件属性、实现 include、首个 GameMode 构造前的 Controller 构造函数和 Getter。执行：

```powershell
git apply --cached --check 'Saved/CodexBaseline/phase-a/command-component-controller-index.patch'
git apply --cached 'Saved/CodexBaseline/phase-a/command-component-controller-index.patch'
git diff --cached -- Source/Catfishing/Framework/Game/CatGameplayTypes.h Source/Catfishing/Framework/Game/CatGameplayTypes.cpp
```

cached diff 必须逐行等于该白名单 patch 的语义，并确认不含 Task 3 所列全部输入标识及 `OnPossess`、`SetupInputComponent`、`OnUnPossess`、`BeginPlay`、`EndPlay`。不满足时用 `git apply --cached -R` 精确撤销该 patch，禁止使用交互式 `git add -p`。

- [ ] **Step 7: 提交任务 8**

```powershell
git commit -m "Add owner-only fishing command results"
```

---

## Task 9: 阶段 A 汇总验证、脏工作树审计与文档回填

**Files:**

- Modify: `Docs/superpowers/plans/2026-08-14-fishing-phase-a-authority-foundation.md`（只勾选实际完成项并记录证据）
- Create: `Docs/FishingActorBlueprintHooks_zh-CN.md`

`FishingActorBlueprintHooks_zh-CN.md` 必须按 Rod/Hook/Fish 分别列出：蓝图父类路径、可挂 Mesh 的 VisualRoot、只读 canonical anchors、复制状态字段、每个 Blueprint event 的触发时机、允许在蓝图中做的皮肤/AnimBP/Montage/VFX/SFX 工作，以及禁止修改 Session/Equipment/Items/authority Transform 的边界。明确 Stage C 资产路径建议：

- `/Game/Catfishing/Fishing/Actors/BP_FishingRod`
- `/Game/Catfishing/Fishing/Actors/BP_FishingHook`
- `/Game/Catfishing/Fishing/Actors/BP_FishEncounter`

- [x] **Step 1: 检查变更范围**

```powershell
git status --short
git diff --name-only
git diff --check -- Source/Catfishing/Fishing Source/Catfishing/Environment Source/Catfishing/Equipment Docs
```

本 Task 9 复验门禁明确 **supersede** 早期“raw diff 必须为空”的字面条件；`Saved/CodexBaseline/phase-a` 中的 Task 0 immutable baseline 本身保持不变，不得为了制造空比较而覆盖或重建。重新计算 Config/UI 哈希、Content path+SHA256 manifest，并对两个 Controller 文件分别执行 `git diff --no-index -- <baseline-copy> <current-file>`：Controller 的期望 raw delta 必须精确等于用户已接受的 `b9342f9` 混合提交增量 + Task 3 AggregationRevision 增量 + Task 8 Command Component 增量；Content 的期望 raw delta 必须精确等于 `Boatyard` 新增 244 条、`Underwater_life` 新增 672 条，以及 `NaturePackage/Maps/Showcase2.umap` 从 Task 0 SHA256 `34CD99FFBB43A809F0F4A87B53588610F62A35982F4AE80417A91905A9CF47D7` 到用户确认 SHA256 `17D3A569CCEB221F5426A93F02E183F28D8DDE3A288CFA156BEF4E84A0E17B13` 的一进一出变更。最终 `Compare-Object`、Controller no-index 和 `git status --porcelain=v1 -uall` 必须精确等于这些授权 delta，且相对 Task 9 补充保护证据不得出现任何额外 Source、Config、UI、Content、stash 或 index 漂移；Config、UI 和 Content 不加入暂存区。

- [x] **Step 2: Editor 与 Game 构建**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' Catfishing Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
```

- [x] **Step 3: 运行阶段 A 域测试**

依次运行 filters：

```text
Catfishing.Unit.Fishing.Contracts
Catfishing.Unit.Fishing.CommandLedger
Catfishing.Unit.Environment.Water
Catfishing.Unit.Equipment.FishingUse
Catfishing.Unit.Fishing.Actors
Catfishing.Unit.Fishing.Session
Catfishing.Unit.Fishing.Service
Catfishing.Unit.Fishing.CommandComponent
```

每批使用独立 `Saved/Automation/phase-a-<domain>/Report` 与 log。每次启动 UnrealEditor-Cmd 前先执行 `New-Item -ItemType Directory -Force -Path '<absolute-run-directory>\Report' | Out-Null`，确保 Report 与 `-abslog` 的共同父目录存在；UE 不负责创建任意 abslog 父目录。解析报告并确认 failed=0、notRun=0、inProcess=0；不能只依据 UnrealEditor-Cmd 退出码。

- [x] **Step 4: 运行全量 Catfishing Automation**

```powershell
New-Item -ItemType Directory -Force -Path 'D:\develop\Catfishing\Saved\Automation\phase-a-all\Report' | Out-Null
& 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\develop\Catfishing\Catfishing.uproject' -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache '-ExecCmds=Automation RunTests Catfishing;Quit' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=D:\develop\Catfishing\Saved\Automation\phase-a-all\Report' '-abslog=D:\develop\Catfishing\Saved\Automation\phase-a-all\Automation.log'
```

- [x] **Step 5: 进行两轮代码审查**

第一轮核对实现是否逐项符合本计划和正式设计；第二轮只查并发、authority、复制、tombstone、Blueprint 边界和用户 dirty hunk 泄漏。所有 P0/P1 修复后重新执行受影响域测试与两个 build。

- [x] **Step 6: 扫描未完成占位和错误依赖**

```powershell
rg -n "T(O)DO|T(B)D|F(I)XME|待[定]|待[补]|implement[ ]later|/Script/Fishing_System|/Script/TP_ThirdPerson|/Script/GFur" Source/Catfishing/Fishing Source/Catfishing/Environment Source/Catfishing/Equipment Docs/FishingActorBlueprintHooks_zh-CN.md
```

代码和交付文档中不得存在未裁接口占位，不得引用 Demo authority 类型或 GFur。

- [x] **Step 7: 提交文档与计划证据**

```powershell
git add Docs/FishingActorBlueprintHooks_zh-CN.md Docs/superpowers/plans/2026-08-14-fishing-phase-a-authority-foundation.md
git diff --cached --check
git commit -m "Document fishing actor blueprint extension points"
```

**Task 9 绝对证据路径（Task 0 原 baseline + 用户确认的 post-baseline delta）：**

- 完整命令、Controller no-index 归因、Config/UI SHA256、Content 611→1527 补充保护清单、提交审计与 2026-08-14/2026-08-17 分轮结果：`D:\develop\Catfishing\.superpowers\sdd\2026-08-14-fishing-phase-a-authority-foundation\task-9-report.md`
- 8 个 domain JSON/abslog：`D:\develop\Catfishing\Saved\Automation\phase-a-task9-contracts`、`D:\develop\Catfishing\Saved\Automation\phase-a-task9-command-ledger`、`D:\develop\Catfishing\Saved\Automation\phase-a-task9-water`、`D:\develop\Catfishing\Saved\Automation\phase-a-task9-equipment-fishing-use`、`D:\develop\Catfishing\Saved\Automation\phase-a-task9-actors`、`D:\develop\Catfishing\Saved\Automation\phase-a-task9-session`、`D:\develop\Catfishing\Saved\Automation\phase-a-task9-service`、`D:\develop\Catfishing\Saved\Automation\phase-a-task9-command-component`
- 2026-08-14 全量 JSON/abslog：`D:\develop\Catfishing\Saved\Automation\phase-a-all\Report\index.json`、`D:\develop\Catfishing\Saved\Automation\phase-a-all\Automation.log`
- 蓝图操作者清单：`D:\develop\Catfishing\Docs\FishingActorBlueprintHooks_zh-CN.md`

阶段 A 完成条件是：两个 target 构建成功、所有阶段 A 域测试和全量 `Catfishing` 自动化成功、cached/working diff 未覆盖用户输入与资产，并且 Rod/Hook/Fish 三个原生父类已经能在 UE Editor 中创建蓝图子类、接 Mesh/皮肤/动画事件而不获得任何权威写入口。此阶段只完成 owning-client RPC 的静态契约；阶段 B 在旧 RPC/Ability 真正接入该通道时，必须先增加 server + owning client + non-owning client 网络测试，验证只有 owner 收到结果且 Listen Server 本地请求只广播一次，未通过前不得宣称 owner-only 传输验收完成。
