# Fishing Phase A.1 Spline Water and Spatial Chum Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 Phase A 中的 AABB 水域与整段河共享 `ChumPool` 替换为固定样条烘焙水域、带洞的权威多边形查询和独立圆形窝料 Field，并把既有 Fishing Session、NearShore、Scoop、Fish 选择接缝迁移到新空间模型。

**Architecture:** `ACatWaterBoundarySplineActor` 只保存编辑输入，`ACatWaterRegion` 烘焙并拥有只读多边形缓存，`UCatWaterQuerySubsystem` 是唯一空间查询入口。`UCatChumFieldSubsystem` 保存服务器权威 Field、预算、幂等结果和局部采样；GameState 上的 FastArray 只复制表现 DTO。Equipment 与 Field 使用同步预留/提交协议；Fishing Session 只冻结 `FCatWaterRegionHandle`、服务器落水点和真咬时的选择输入，不复制边界或第二份 Field 状态。

**Tech Stack:** Unreal Engine 5.8、C++、`USplineComponent`、`FXxHash64Builder`、WorldSubsystem、FastArray replication、StateTree 现有 Task 宿主、UE Automation Tests、Editor 多人 PIE Automation、Git 精确白名单提交。

## Global Constraints

- 工作目录固定为 `D:\develop\Catfishing`，目标分支为 `feature/fishing-system`；计划编写时基线 HEAD 为 `a28fe83b53dc4ef27187bf223a7d657b1841b97d`。
- 本计划是 Phase A.1。GAS Ability、输入标签路由、完整 CastFlight/Waiting/Probe/TrueBite/Fight Runner、正式 `ST_FishingSession` 资产、Montage 与 HUD 不属于本计划验收；它们由 Phase B 计划消费本计划提供的 API。此前确认的“平静收线双方仍耗体力、挣扎状态用更高系数”保留为 Phase B FightSimulator 的明确输入，不在本计划伪造公式。
- Phase A 已完成的 Session 并发版本、Command ledger、FishingUse reservation、Rod/Hook/Fish 原生 Actor、Service registry 和 owner-only result 通道必须保留；不得重写 Items 捕获真相。
- Phase A 后续提交已经修复 `ACatFishingSession::PublishSnapshot` 的 authority 本地通知；Task 1 只扩充现有回归断言并建立 Automation runner，不重复改写生产实现。错误的旧 `ContributeChum` 并发域由 Task 9 的空间 Placement 协议整体替换。
- 运行期岸线固定；不得复制或在运行期修改样条控制点。Region 近似水平，允许 Yaw，拒绝非零 Pitch/Roll 和非单位 Scale。
- 一个 Region 支持多个互不重叠的 Include，以及完全位于一个 Include 内且互不相交/嵌套的 Exclude。自交、重复 ID、非法拓扑和同高度 Region 歧义全部 fail-closed。
- 每次 Placement 只创建一个圆形 Field；`Quantity > 1` 只放大 `BaseContribution × Quantity`，不改变 Radius/Duration，也不创建重合的多条权威记录。
- 不创建每 Field 一个权威 Actor。Field 真相只在 `UCatChumFieldSubsystem`；客户端表现来自 GameState FastArray。
- 权威 Contains、岸距、落点、NearShore 和 Scoop 不依赖 Water Mesh、Landscape、水面碰撞、Decal、Niagara 或表现 Actor。外包 AABB 只做粗筛。
- Blueprint 表现类只能读 DTO、挂 Mesh/材质/Niagara/动画和控制临时可见性；全部 `NoCollision`、不影响 Navigation、不写回 Geometry/Field/Session。
- 现有用户改动必须逐字节保留：`Config/DefaultEditor.ini`、`Docs/FishingActorBlueprintHooks_zh-CN.md` 和全部未跟踪 `Content/`。禁止 `git reset`、`git stash`、`git checkout --`、`git clean`、批量保存资产或全文件格式化。
- `Source/Catfishing/Framework/Game/CatGameplayTypes.h/.cpp` 当前 clean，但 Task 9/11/13 会窄改。每次暂存前都必须用 index-only 或精确白名单核对，不得触碰 Move/Look/Jump/Sprint/IMC 代码。
- 第一轮源码实施不修改 `.ini`、`.uasset`、`.umap` 或现有脏文档；编辑器资产只在 Task 16 由用户按逐路径清单创建。
- 运行模块不增加 GeometryCore、Water、NavigationSystem 或 UnrealEd 依赖；二维多边形内核由项目自己实现。Editor 网络测试单独放入 Editor-only 模块。
- 每个生产改动先写会因缺功能而失败的测试，记录真实 RED，再写最小实现；每个任务独立构建、运行精确 Filter、审查并提交。
- Automation 必须读取 `Report/index.json`，要求 `tests.Count > 0` 且 `failed/notRun/inProcess == 0`；不能只看进程退出码。

---

## File and Responsibility Map

### Environment geometry

- `Environment/CatWaterTypes.h`：公共 Handle、查询错误、Containment、ShoreKind 与只读空间结果。
- `Environment/CatWaterGeometry.h/.cpp`：可序列化烘焙缓存、拓扑校验、点包含、岸距、修正落点和稳定 Revision；不访问 World。
- `Environment/CatWaterBoundarySplineActor.h/.cpp`：单条 Include/Exclude 闭合样条的编辑输入与自适应采样。
- `Environment/CatWaterRegion.h/.cpp`：Region 配置、Boundary 显式引用、编辑期 Bake、缓存和注册生命周期。
- `Environment/CatWaterQuerySubsystem.h/.cpp`：Region registry、粗筛和唯一公开空间查询。

### Spatial chum

- `Environment/CatChumFieldTypes.h/.cpp`：Influence Spec、Placement command/result、Field state/sample/public DTO。
- `Environment/CatChumFieldSettings.h/.cpp`：Field/Placement gate、容量、距离、角度与 LOS 配置。
- `Environment/CatChumFieldSubsystem.h/.cpp`：权威 Field、Pending commit token、预算、终态重放、过期与采样。
- `Environment/CatChumPlacementService.h/.cpp`：Controller 身份、候选点、Water、Equipment 与 Field 的原子协调。
- `Environment/CatChumFieldAnchor.h/.cpp`：自然事件显式空间位置 Provider。
- `Environment/CatChumFieldReplicationComponent.h/.cpp`：GameState FastArray 表现镜像。
- `Environment/Presentation/*`：Water/Chum 只读 Blueprint 表现 Actor 与本地表现 Subsystem。

### Existing systems changed by the cutover

- `Equipment/CatEquipmentDefinition.*`：Chum 定义改为 `FCatChumInfluenceSpec`。
- `Equipment/CatEquipmentTypes.h`、`CatEquipmentComponent.*`：通用 Run consumable reservation/commit/release。
- `Fishing/Integration/CatFishingCommandTypes.*`、`CatFishingCommandComponent.*`：PlaceChum/BeginCast 正式命令与 owner-only 结构化结果。
- `Data/CatFishSelectionTypes.h`、`CatFishDefinition.*`、`CatFishCatalogSettings.*`：局部 Chum、Bait 与饱和选择器。
- `Fishing/CatFishingTypes.h`、`CatFishingService.*`、`CatFishingSession.*`：冻结 Handle/落点、延迟选鱼、Fish Actor Transform 岸距。
- `Fishing/CatFishingStateTreeNodes.*`：EnterPhase 只保留阶段意图，增加真咬选择的有界 Task 接缝。
- `Framework/Game/CatGameplayTypes.*`：GameState 挂 Field replication component；删除旧 Chum RPC/共享池自然事件旁路。
- `CatfishingEditorTests`：Editor-only 两客户端 PIE 网络门禁，不进入 Game target。

---

## Verification Commands Used by Every Task

Editor 与 Game build：

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' Catfishing Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
```

从 Task 1 起运行精确 Automation：

```powershell
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Environment.WaterGeometry' -RunName 'phase-a1-water-geometry'
```

每个任务把 `Filter` 和 `RunName` 替换为任务给出的精确值；最终运行：

```powershell
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing' -RunName 'phase-a1-all'
```

同一测试因修复而重跑时给 `RunName` 追加 `-r2/-r3`，保留此前 RED/GREEN 证据；不得删除或覆盖旧运行目录。

---

### Task 0: 固化不可覆盖的 Phase A.1 工作树基线

**Artifacts (ignored, not committed):**

- Create: `Saved/CodexBaseline/phase-a1/<UTC-run-id>/COMPLETE`
- Create: `Saved/CodexBaseline/phase-a1/<UTC-run-id>/status-porcelain.txt`
- Create: `Saved/CodexBaseline/phase-a1/<UTC-run-id>/tracked-sha256.tsv`
- Create: `Saved/CodexBaseline/phase-a1/<UTC-run-id>/content-sha256.tsv`
- Create: `Saved/CodexBaseline/phase-a1/<UTC-run-id>/controller/`
- Create: `Saved/CodexBaseline/phase-a1/<UTC-run-id>/stash.txt`

**Interfaces:**

- Consumes: 当前 HEAD、dirty tracked 内容、全部 `Content/` 文件与 `refs/stash`。
- Produces: 后续每个任务都能逐字节比较的不可变保护证据；不产生源码接口。

- [ ] **Step 1: 创建一次性目录并拒绝覆盖**

```powershell
$runId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssZ')
$baseline = "D:\develop\Catfishing\Saved\CodexBaseline\phase-a1\$runId"
if (Test-Path $baseline) { throw "Baseline already exists: $baseline" }
New-Item -ItemType Directory -Path "$baseline\controller" | Out-Null
git rev-parse HEAD | Out-File -Encoding utf8 -NoClobber "$baseline\head.txt"
git branch --show-current | Out-File -Encoding utf8 -NoClobber "$baseline\branch.txt"
git status --porcelain=v1 -uall | Out-File -Encoding utf8 -NoClobber "$baseline\status-porcelain.txt"
git stash list --format='%H %gd %s' | Out-File -Encoding utf8 -NoClobber "$baseline\stash.txt"
```

- [ ] **Step 2: 保存受保护 tracked 与 Controller 证据**

```powershell
$protected = @(
  'Config/DefaultEditor.ini',
  'Docs/FishingActorBlueprintHooks_zh-CN.md',
  'Source/Catfishing/Framework/Game/CatGameplayTypes.h',
  'Source/Catfishing/Framework/Game/CatGameplayTypes.cpp'
)
$protected | ForEach-Object {
  $full = Join-Path 'D:\develop\Catfishing' $_
  $hash = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash
  "$($_)`t$hash" | Out-File -Encoding utf8 -Append "$baseline\tracked-sha256.tsv"
}
[IO.File]::Copy('D:\develop\Catfishing\Source\Catfishing\Framework\Game\CatGameplayTypes.h', "$baseline\controller\CatGameplayTypes.h", $false)
[IO.File]::Copy('D:\develop\Catfishing\Source\Catfishing\Framework\Game\CatGameplayTypes.cpp', "$baseline\controller\CatGameplayTypes.cpp", $false)
```

- [ ] **Step 3: 保存全部 Content path+size+SHA256 manifest**

```powershell
Get-ChildItem 'D:\develop\Catfishing\Content' -Recurse -File | Sort-Object FullName | ForEach-Object {
  $relative = $_.FullName.Substring('D:\develop\Catfishing\'.Length)
  $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
  "$relative`t$($_.Length)`t$hash"
} | Out-File -Encoding utf8 -NoClobber "$baseline\content-sha256.tsv"
```

- [ ] **Step 4: 核对证据并最后写 COMPLETE**

```powershell
if ((Get-Item "$baseline\status-porcelain.txt").Length -eq 0) { throw 'Status baseline is empty.' }
if ((Get-Item "$baseline\content-sha256.tsv").Length -eq 0) { throw 'Content manifest is empty.' }
New-Item -ItemType File -Path "$baseline\COMPLETE" | Out-Null
```

Expected: 记录当前两个 tracked dirty 文件、1523 个未跟踪 Content 文件、clean Controller 副本和 stash OID `c3b207533da0947794468fc22ad74c547338dfcd`。若事实不同，记录实际值并停止把旧数字当断言。

---

### Task 1: 自动化执行器与 Phase A authority 本地 Snapshot 通知回归锁定

**Files:**

- Create: `Build/Automation/RunCatAutomation.ps1`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingSessionTests.cpp`

**Interfaces:**

- Consumes: HEAD 中已经由 `NotifySnapshotChanged()` 闭合的 `ACatFishingSession::PublishSnapshot` / `OnRep_Snapshot` 与现有 `FCatFishingSessionSnapshotVersionMutationRulesTest` friend。
- Produces: authority 写入与 remote `OnRep_Snapshot` 各广播一次且不重复推进版本的完整回归断言；后续任务统一使用 Automation runner。本任务不产生 Session 生产代码变化。

- [ ] **Step 1: 扩充既有 authority/RepNotify 回归测试**

在既有 `Catfishing.Unit.Fishing.Session.SnapshotVersionMutationRulesSeparateHighFrequencyDiscreteAndPhaseChanges` 末尾、移除 delegate 之前增加：

```cpp
const int32 BeforeRepNotifySignals = SnapshotSignals;
const int64 BeforeRepNotifySequence = Session->Snapshot.SnapshotSequence;
Session->OnRep_Snapshot();
TestEqual(TEXT("RepNotify emits exactly one additional signal"),
    SnapshotSignals, BeforeRepNotifySignals + 1);
TestEqual(TEXT("RepNotify never advances authority versions"),
    Session->Snapshot.SnapshotSequence, BeforeRepNotifySequence);
```

- [ ] **Step 2: 用原始命令确认当前修复仍为 GREEN**

Run: Editor build command from the global section, then the existing raw `UnrealEditor-Cmd` command for the exact test because the runner does not exist yet.

```powershell
$run = 'D:\develop\Catfishing\Saved\Automation\phase-a1-session-publish-baseline-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssZ')
if (Test-Path $run) { throw "Baseline run already exists: $run" }
New-Item -ItemType Directory -Path "$run\Report" | Out-Null
& 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\develop\Catfishing\Catfishing.uproject' -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache '-ExecCmds=Automation RunTests Catfishing.Unit.Fishing.Session.SnapshotVersionMutationRulesSeparateHighFrequencyDiscreteAndPhaseChanges;Quit' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$run\Report" "-abslog=$run\Automation.log"
```

Expected: test 立即通过。若失败，说明当前 HEAD 与计划基线不同，先按 systematic debugging 处理真实回归；不得把既有正确实现回滚成 RED。本任务没有 Session 生产行为变更，因此这里不制造假失败。

- [ ] **Step 3: 实现 Automation runner**

先创建不存在的父目录；不得用 `-Force` 覆盖已有 runner：

```powershell
foreach ($dir in @('D:\develop\Catfishing\Build', 'D:\develop\Catfishing\Build\Automation')) {
  if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
}
if (Test-Path 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1') {
  throw 'Automation runner already exists; inspect instead of overwriting.'
}
```

随后创建脚本，内容如下：

```powershell
param(
  [Parameter(Mandatory=$true)][string]$Filter,
  [Parameter(Mandatory=$true)][string]$RunName
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = 'D:\develop\Catfishing'
$run = Join-Path $root "Saved\Automation\$RunName"
$report = Join-Path $run 'Report'
if (Test-Path $run) { throw "Automation run already exists: $run" }
New-Item -ItemType Directory -Path $report -Force | Out-Null
& 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$root\Catfishing.uproject" `
  -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
  "-ExecCmds=Automation RunTests $Filter;Quit" `
  '-TestExit=Automation Test Queue Empty' `
  "-ReportExportPath=$report" "-abslog=$run\Automation.log"
$editorExit = $LASTEXITCODE
if (-not (Test-Path "$report\index.json")) { throw "Missing Automation report for $Filter" }
$index = Get-Content -Raw -Encoding UTF8 "$report\index.json" | ConvertFrom-Json
if ($index.tests.Count -le 0) { throw "No tests ran for $Filter" }
if ($index.failed -ne 0 -or $index.notRun -ne 0 -or $index.inProcess -ne 0) {
  throw "Automation failed: failed=$($index.failed) notRun=$($index.notRun) inProcess=$($index.inProcess)"
}
if ($editorExit -ne 0) { throw "UnrealEditor-Cmd exited $editorExit for $Filter" }
if (Select-String -Path "$run\Automation.log" -Pattern 'Fatal error:|Assertion failed:|Unhandled Exception:' -Quiet) {
  throw "Severe engine failure found in Automation.log for $Filter"
}
$index | Select-Object succeeded,succeededWithWarnings,failed,notRun,inProcess,totalDuration
```

- [ ] **Step 4: 证明 Session 生产实现未被重复改动**

```powershell
git diff --exit-code -- Source/Catfishing/Fishing/CatFishingSession.h Source/Catfishing/Fishing/CatFishingSession.cpp
rg -n 'NotifySnapshotChanged\(\)|PublishSnapshot\(' Source/Catfishing/Fishing/CatFishingSession.cpp
```

Expected: Session h/cpp 无工作树 diff；现有 `PublishSnapshot` 继续推进版本、ForceNetUpdate 并本地通知，`OnRep_Snapshot` 只通知且不改版本。

- [ ] **Step 5: 验证并提交**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Fishing.Session' -RunName 'phase-a1-session-publish'
git add -- Build/Automation/RunCatAutomation.ps1 Source/Catfishing/Fishing/Tests/CatFishingSessionTests.cpp
git diff --cached --check
git commit -m "Lock fishing snapshot automation regressions"
```

---

### Task 2: Water 公共 DTO 与纯二维几何内核

**Files:**

- Modify: `Source/Catfishing/Environment/CatWaterTypes.h`
- Create: `Source/Catfishing/Environment/CatWaterGeometry.h`
- Create: `Source/Catfishing/Environment/CatWaterGeometry.cpp`
- Create: `Source/Catfishing/Environment/Tests/CatWaterGeometryTests.cpp`

**Interfaces:**

- Consumes: 仅 Core 数学、`FTransform`、`FBox2D`、`FXxHash64Builder`。
- Produces:

```cpp
UENUM(BlueprintType)
enum class ECatWaterBoundaryOperation : uint8 { Include, Exclude };
UENUM(BlueprintType)
enum class ECatWaterContainment : uint8 { Outside, Boundary, Inside };
UENUM(BlueprintType)
enum class ECatWaterShoreKind : uint8 { None, OuterBoundary, ExcludedBoundary };

USTRUCT(BlueprintType)
struct FCatWaterRegionHandle
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FName RegionId = NAME_None;
    UPROPERTY(BlueprintReadOnly) int64 GeometryRevision = 0;
    bool IsValid() const;
    bool operator==(const FCatWaterRegionHandle& Other) const;
};

USTRUCT(BlueprintType)
struct FCatWaterSpatialResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bSucceeded = false;
    UPROPERTY(BlueprintReadOnly) ECatWaterQueryError Error = ECatWaterQueryError::RegionNotFound;
    UPROPERTY(BlueprintReadOnly) FCatWaterRegionHandle WaterRegion;
    UPROPERTY(BlueprintReadOnly) ECatWaterContainment Containment = ECatWaterContainment::Outside;
    UPROPERTY(BlueprintReadOnly) ECatWaterShoreKind NearestShoreKind = ECatWaterShoreKind::None;
    UPROPERTY(BlueprintReadOnly) double SignedDistanceToShoreCm = 0.0;
    UPROPERTY(BlueprintReadOnly) FVector NearestShoreWorldPoint = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector WaterwardDirection = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector WaterSurfaceWorldPoint = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector WaterSurfaceNormal = FVector::UpVector;
    UPROPERTY(BlueprintReadOnly) double VerticalDeltaCm = 0.0;
};
```

`ECatWaterQueryError` 新增 `InvalidDirection, StaleGeometry, InvalidGeometry, HeightOutOfTolerance`；为保证 Tasks 2–5 逐提交可编译，旧 `FishingClosed/RevisionConflict` 暂留并仅供 legacy wrapper 使用。Task 14 删除 wrapper 时，枚举最终收敛为 `None, InvalidLocation, InvalidDirection, RegionNotFound, AmbiguousRegion, StaleGeometry, InvalidGeometry, HeightOutOfTolerance`。

```cpp
struct FCatWaterPolygonBuildInput
{
    FName BoundaryId;
    ECatWaterBoundaryOperation Operation = ECatWaterBoundaryOperation::Include;
    TArray<FVector2D> Vertices;
};

struct FCatWaterGeometryBuildInput
{
    FName RegionId;
    FTransform PlaneToWorld = FTransform::Identity;
    double WaterPointVerticalToleranceCm = 0.0;
    double BankHeightToleranceCm = 0.0;
    double BoundaryToleranceCm = 0.0;
    double MaxLandingCorrectionCm = 0.0;
    double MinimumWaterInsetCm = 0.0;
    double MaxSampleSegmentLengthCm = 100.0;
    double MaxChordErrorCm = 5.0;
    TArray<FCatWaterPolygonBuildInput> Boundaries;
};

USTRUCT()
struct FCatWaterBakedPolygon
{
    GENERATED_BODY()
    UPROPERTY() FName BoundaryId;
    UPROPERTY() TArray<FVector2D> Vertices;
    UPROPERTY() FBox2D Bounds;
};

USTRUCT()
struct FCatWaterGeometryCache
{
    GENERATED_BODY()
    UPROPERTY() FCatWaterRegionHandle Handle;
    UPROPERTY() FTransform WorldToPlane = FTransform::Identity;
    UPROPERTY() FTransform PlaneToWorld = FTransform::Identity;
    UPROPERTY() TArray<FCatWaterBakedPolygon> IncludePolygons;
    UPROPERTY() TArray<FCatWaterBakedPolygon> ExcludePolygons;
    UPROPERTY() FBox2D Bounds2D;
    UPROPERTY() double WaterSurfaceZ = 0.0;
    UPROPERTY() double WaterPointVerticalToleranceCm = 0.0;
    UPROPERTY() double BankHeightToleranceCm = 0.0;
    UPROPERTY() double BoundaryToleranceCm = 0.0;
    UPROPERTY() double MaxLandingCorrectionCm = 0.0;
    UPROPERTY() double MinimumWaterInsetCm = 0.0;
    UPROPERTY() double MaxSampleSegmentLengthCm = 0.0;
    UPROPERTY() double MaxChordErrorCm = 0.0;
    bool IsRuntimeReady() const;
};

struct FCatWaterGeometryBuildResult
{
    bool bSucceeded = false;
    FCatWaterGeometryCache Cache;
    TArray<FString> Errors;
};

class FCatWaterGeometry
{
public:
    static FCatWaterGeometryBuildResult Build(const FCatWaterGeometryBuildInput& Input);
    static FCatWaterSpatialResult QueryPoint(const FCatWaterGeometryCache& Cache,
        const FVector& WorldPoint, double VerticalToleranceCm);
    static FCatWaterSpatialResult ResolveCandidatePoint(const FCatWaterGeometryCache& Cache,
        const FVector& CandidateWorldPoint);
    static int64 ComputeRevision(const FCatWaterGeometryBuildInput& CanonicalInput);
};
```

- [ ] **Step 1: 写五组失败测试**

测试名固定为：

- `Catfishing.Unit.Environment.WaterGeometry.DefaultsFailClosed`
- `Catfishing.Unit.Environment.WaterGeometry.ConvexConcaveAndHolesClassifyCorrectly`
- `Catfishing.Unit.Environment.WaterGeometry.RejectsSelfIntersectionAndInvalidTopology`
- `Catfishing.Unit.Environment.WaterGeometry.ShoreDistanceDirectionAndTieBreakAreDeterministic`
- `Catfishing.Unit.Environment.WaterGeometry.RevisionIsStableAndConfigurationSensitive`

至少包含这个岛屿方向断言：

```cpp
const FCatWaterSpatialResult Island = FCatWaterGeometry::QueryPoint(Cache, FVector(500, 500, 100), 10.0);
TestEqual(TEXT("Hole is outside water"), Island.Containment, ECatWaterContainment::Outside);
TestEqual(TEXT("Hole shore kind"), Island.NearestShoreKind, ECatWaterShoreKind::ExcludedBoundary);
TestTrue(TEXT("Waterward direction is normalized"), Island.WaterwardDirection.IsNormalized());
const FCatWaterSpatialResult Moved = FCatWaterGeometry::QueryPoint(
    Cache, Island.NearestShoreWorldPoint + Island.WaterwardDirection * 10.0, 10.0);
TestEqual(TEXT("Direction points out of the hole into valid water"),
    Moved.Containment, ECatWaterContainment::Inside);
```

- [ ] **Step 2: 构建并确认 RED**

Expected: compile fails because `CatWaterGeometry.h` and the new DTO do not exist; no unrelated compile error is accepted as RED.

- [ ] **Step 3: 实现规范化、拓扑和 Revision**

Implementation rules:

- 量化到 `0.1 cm`，去除相邻重复点和重复首尾点。
- 所有轮廓规范为 CCW；按规范化 `BoundaryId` 排序。
- 自交检测同时拒绝普通相交、端点之外的接触和共线重叠。
- Include 不能相交/覆盖；Exclude 必须严格位于唯一 Include，且 Exclude 之间不能相交/覆盖/嵌套。
- Revision 字节流包含 RegionId、Plane origin/yaw、全部容差/采样设置、排序后的 Operation/BoundaryId/量化顶点。使用 `FXxHash64Builder::Finalize().Hash`，清最高位并把 0 映射为 1。
- FName 一律以 invariant lowercase UTF-8 的“长度+字节”写入，量化整数显式按 little-endian 写入；禁止哈希 FName comparison index、对象地址、TArray 内存填充或平台原生 struct bytes。

```cpp
const uint64 Raw = Builder.Finalize().Hash & MAX_int64;
return static_cast<int64>(Raw == 0 ? 1 : Raw);
```

- [ ] **Step 4: 实现查询和岸边修正**

Implementation rules:

- `InsideWater = any Include && no Exclude`。
- 距离绝对值取最近线段；同距按 BoundaryId、SegmentIndex。
- 水内为正，外部/Exclude 为负，容差内为 `Boundary/0`。
- OuterBoundary 的 waterward 指向 Include 内；ExcludedBoundary 的 waterward 指向洞外有效水域。
- `ResolveCandidatePoint` 只修正 OuterBoundary 且 `abs(distance) <= MaxLandingCorrectionCm` 的点；沿 waterward 推入 `MinimumWaterInsetCm` 后必须重新查询为 Inside。Exclude 一律拒绝。

- [ ] **Step 5: 运行测试并提交**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Environment.WaterGeometry' -RunName 'phase-a1-water-geometry'
git add -- Source/Catfishing/Environment/CatWaterTypes.h Source/Catfishing/Environment/CatWaterGeometry.h Source/Catfishing/Environment/CatWaterGeometry.cpp Source/Catfishing/Environment/Tests/CatWaterGeometryTests.cpp
git diff --cached --check
git commit -m "Add deterministic spline water geometry kernel"
```

---

### Task 3: Boundary Spline Actor 与自适应采样

**Files:**

- Create: `Source/Catfishing/Environment/CatWaterBoundarySplineActor.h`
- Create: `Source/Catfishing/Environment/CatWaterBoundarySplineActor.cpp`
- Create: `Source/Catfishing/Environment/Tests/CatWaterBoundarySplineActorTests.cpp`

**Interfaces:**

- Consumes: `FCatWaterPolygonBuildInput`、Region 的 `WorldToPlane`、采样长度/弦误差。
- Produces:

```cpp
UCLASS(BlueprintType)
class CATFISHING_API ACatWaterBoundarySplineActor : public AActor
{
    GENERATED_BODY()
public:
    ACatWaterBoundarySplineActor();
    const USplineComponent* GetBoundarySpline() const;
    bool BuildPolygonInput(const FTransform& WorldToPlane,
        double MaxSampleSegmentLengthCm, double MaxChordErrorCm,
        FCatWaterPolygonBuildInput& OutInput, FString& OutError) const;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Water|Boundary") FName BoundaryId;
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Water|Boundary")
    ECatWaterBoundaryOperation Operation = ECatWaterBoundaryOperation::Include;
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Water|Boundary")
    TObjectPtr<ACatWaterRegion> OwningRegion;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USplineComponent> BoundarySpline;
};
```

- [ ] **Step 1: 写以下失败测试**

- `Catfishing.Unit.Environment.WaterBoundary.DefaultBoundaryFailsClosed`
- `Catfishing.Unit.Environment.WaterBoundary.RequiresClosedFiniteUniqueVertices`
- `Catfishing.Unit.Environment.WaterBoundary.SamplingRespectsLengthAndChordError`
- `Catfishing.Unit.Environment.WaterBoundary.SCurveCannotHideChordErrorAtMidpoint`
- `Catfishing.Unit.Environment.WaterBoundary.SamplingProjectsIntoRegionPlane`

反射断言 Actor `BlueprintType`、不 Tick、不复制，Spline 默认闭合；采样输出不能保留重复闭合点。

- [ ] **Step 2: 构建并确认 RED**

Expected: compile fails only because `ACatWaterBoundarySplineActor` is missing.

- [ ] **Step 3: 实现 Actor 和采样**

每个 spline input-key 区间是一个 cubic Hermite 段。对当前子区间 `[t0,t1]` 读取端点位置与对 input key 的导数，转换成 cubic Bézier 控制点 `C1=P0+D0*(t1-t0)/3`、`C2=P1-D1*(t1-t0)/3`；只有世界弦长 `<= MaxSampleSegmentLengthCm` 且两个内部控制点到有限弦段的最大距离 `<= MaxChordErrorCm` 时才终止。这个控制多边形凸包界限才是弦误差保证；禁止只测中点，因为 S 型 cubic 可在中点回到弦上而在 1/4、3/4 处大幅偏离。否则递归二分，最大深度固定 20，达到上限仍不满足就返回错误，不静默使用粗几何。采样点通过 `WorldToPlane.TransformPosition` 转为二维，Z 偏差不作为轮廓点保存。

- [ ] **Step 4: 验证并提交**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Environment.WaterBoundary' -RunName 'phase-a1-water-boundary'
git add -- Source/Catfishing/Environment/CatWaterBoundarySplineActor.h Source/Catfishing/Environment/CatWaterBoundarySplineActor.cpp Source/Catfishing/Environment/Tests/CatWaterBoundarySplineActorTests.cpp
git diff --cached --check
git commit -m "Add authored water boundary splines"
```

---

### Task 4: WaterRegion 烘焙缓存、Data Validation 与表现只读入口

**Files:**

- Modify: `Source/Catfishing/Environment/CatWaterRegion.h`
- Modify: `Source/Catfishing/Environment/CatWaterRegion.cpp`
- Modify: `Source/Catfishing/Environment/CatWaterBoundarySplineActor.h`
- Modify: `Source/Catfishing/Environment/CatWaterBoundarySplineActor.cpp`
- Modify: `Source/Catfishing/Environment/Tests/CatWaterRegionTests.cpp`
- Modify: `Source/Catfishing/Environment/Tests/CatWaterQuerySubsystemTests.cpp`
- Create: `Source/Catfishing/Environment/Tests/CatWaterTestFixtures.h`
- Modify: `Source/Catfishing/Framework/Game/Tests/CatGameplayTypesTests.cpp`
- Create: `Source/Catfishing/Environment/Presentation/CatWaterRegionPresentationActor.h`
- Create: `Source/Catfishing/Environment/Presentation/CatWaterRegionPresentationActor.cpp`
- Create: `Source/Catfishing/Environment/Presentation/CatWaterRegionPresentationSubsystem.h`
- Create: `Source/Catfishing/Environment/Presentation/CatWaterRegionPresentationSubsystem.cpp`
- Create: `Source/Catfishing/Environment/Tests/CatWaterRegionPresentationTests.cpp`

**Interfaces:**

- Consumes: Boundary actor 输出与 `FCatWaterGeometry::Build`。
- Produces:

```cpp
bool HasValidBakedGeometry() const;
FCatWaterRegionHandle GetWaterRegionHandle() const;
const FBox2D& GetBakedBoundsForQuery() const;

#if WITH_EDITOR
UFUNCTION(CallInEditor, Category="Water|Authoring") void BakeGeometry();
virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
virtual void PreSave(FObjectPreSaveContext SaveContext) override;
#endif

#if WITH_DEV_AUTOMATION_TESTS
friend struct FCatWaterRegionTestAccess;
#endif
```

Authoring fields exactly: `RegionId`, `WaterSurfaceZ`, `WaterPointVerticalToleranceCm`, `BankHeightToleranceCm`, `BoundaryToleranceCm=2`, `MaxLandingCorrectionCm`, `MinimumWaterInsetCm`, `MaxSampleSegmentLengthCm=100`, `MaxChordErrorCm=5`, `BoundaryActors[]`, optional `TSoftClassPtr<ACatWaterRegionPresentationActor> WaterPresentationClass`。`GeometryRevision` becomes private `VisibleInstanceOnly`; `FCatWaterGeometryCache BakedGeometry` is serialized and private。

这里的 “exactly” 指最终正式 authoring 字段；为保证 Tasks 4–13 每个提交都能构建，旧 `bEnablePrototypeBounds/LocalCenterOffset/HalfExtent` 暂留 deprecated 且不再参与新查询。旧 `bEnableAggregation/AggregationBudget/AggregationRevision/ChumPool` 与 aggregation 方法也只作为 Task 9/14 前的编译兼容桥，新的 geometry/query/field 路径不得读取它们。兼容 `MakeSnapshot()` 的 WorldCenter/HalfExtent 从 baked Bounds 派生，`ContainsWorldPoint()` 委托 polygon cache；Task 14 在全部调用者迁完后物理删除这些字段与 wrapper。

`ACatWaterRegionPresentationActor` exposes value-copy events only:

```cpp
USTRUCT(BlueprintType)
struct FCatWaterPresentationLoop
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FName BoundaryId;
    UPROPERTY(BlueprintReadOnly) ECatWaterBoundaryOperation Operation = ECatWaterBoundaryOperation::Include;
    UPROPERTY(BlueprintReadOnly) TArray<FVector> WorldPoints;
};

USTRUCT(BlueprintType)
struct FCatWaterPresentationSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FCatWaterRegionHandle WaterRegion;
    UPROPERTY(BlueprintReadOnly) double WaterSurfaceZ = 0.0;
    UPROPERTY(BlueprintReadOnly) FVector2D BoundsMin = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector2D BoundsMax = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadOnly) TArray<FCatWaterPresentationLoop> Loops;
};

UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic)
void BP_ApplyWaterGeometryPresentation(const FCatWaterPresentationSnapshot& Snapshot);
UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic)
void BP_SetWaterPreviewVisible(bool bVisible);

UCLASS()
class CATFISHING_API UCatWaterRegionPresentationSubsystem final : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    bool SetLocalWaterPreviewVisible(
        const FCatWaterRegionHandle& WaterRegion, bool bVisible);
};
```

- [ ] **Step 1: 写 Region 和表现失败测试**

`BakeGeometry`/`IsDataValid` 相关测试必须整体包在 `#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS`；它们不进入 Development Game target。新增 `CatWaterTestFixtures.h` 定义只受上述 private friend 授权的 `FCatWaterRegionTestAccess`：runtime 测试先调用纯 `FCatWaterGeometry::Build` 得到合法 cache，再注入 cache + 非零 source digest；不得把 `BakeGeometry` 移出 `WITH_EDITOR`，也不得新增 UFUNCTION/runtime Blueprint 写口。

- `Catfishing.Unit.Environment.WaterRegion.DefaultRegionHasNoRuntimeGeometry`
- `Catfishing.Unit.Environment.WaterRegion.BakeRejectsDuplicateIdsAndOwnershipMismatch`
- `Catfishing.Unit.Environment.WaterRegion.BakeStoresStableNonZeroHandle`
- `Catfishing.Unit.Environment.WaterRegion.RuntimeUsesBakedCacheNotMutatedSpline`
- `Catfishing.Unit.Environment.WaterRegion.DataValidationRejectsStaleBake`
- `Catfishing.Unit.Environment.WaterRegion.AuthoringMutationInvalidatesRuntimeHandle`
- `Catfishing.Unit.Environment.WaterPresentation.BlueprintHooksAreCosmeticAndCollisionFree`
- `Catfishing.Unit.Environment.WaterPresentation.PreviewDefaultsHiddenAndNeverFeedsAuthority`
- `Catfishing.Unit.Environment.WaterPresentation.SubsystemUsesExactHandleAndNoWorldScan`

- [ ] **Step 2: 构建并确认 RED**

Expected: tests fail on missing Bake/cache/presentation interfaces.

- [ ] **Step 3: 实现 Bake 与 stale-digest 检查**

实现表现类前先用 `New-Item -ItemType Directory -Path 'D:\develop\Catfishing\Source\Catfishing\Environment\Presentation'` 创建当前不存在的父目录；若目录已存在则先审查内容，不得用宽泛删除或覆盖。

`BakeGeometry()`：验证 Region transform、Boundary 双向 ownership、ID/closed loop，采样全部轮廓，构造 `PlaneToWorld=(Yaw, FVector(Actor.X, Actor.Y, WaterSurfaceZ), unit scale)`，调用 Build，成功才原子替换 `BakedGeometry/GeometryRevision/BakedSourceDigest`。Bake 失败立即原子清空可运行 Handle/cache，不得保留旧成功缓存。

Region 中影响几何的 authoring property、Actor transform 或任一关联 Boundary spline/ID/Operation/ownership 改动时，Editor hooks 调 `InvalidateBakedGeometry()`；`PreSave` 以同一 canonical sampled build input 重新计算 source digest，不一致时同样清空可运行缓存并记录 validation error。`HasValidBakedGeometry()` 必须要求非零 Handle、合法 cache、非零 `BakedSourceDigest`，Editor 下还要求 current digest 完全相等；Cook/运行期绝不注册 stale cache。只改 WaterPresentationClass 不改变 GeometryRevision。为保持本任务全模块可编译，旧 `ContainsWorldPoint/MakeSnapshot/Aggregation` 暂存并只读取 baked cache。Editor-only 测试走“配置 Boundary → Bake”；会进入 Game target 的 runtime/query fixture 只走 pure Build → `FCatWaterRegionTestAccess` 注入，绝不引用 `WITH_EDITOR` API。

- [ ] **Step 4: 实现表现 Actor 约束**

构造函数关闭 Tick/replication/collision；创建 `VisualRoot`，调用 `SetCanEverAffectNavigation(false)`。Region 在非 DedicatedServer 的 `BeginPlay` 按 optional soft class 创建一个本地表现 Actor，传入值拷贝 Snapshot 并先调用 `BP_SetWaterPreviewVisible(false)`，随后向本地 presentation subsystem 以 exact Handle 注册；`EndPlay` 精确注销并销毁它。Subsystem 保存 Handle→weak actor，不扫描 World；Phase B 的 Aim/Chum Ability 只调用其 `SetLocalWaterPreviewVisible`。所有 Blueprint getter 返回 Snapshot/Transform 值，不返回可修改权威组件或 cache 指针；表现类缺失/加载失败只跳过表现，不影响注册或查询。默认不自动生成永久不规则覆层，若 Blueprint 临时三角化轮廓，其 Mesh 也不能回流到 authority。

- [ ] **Step 5: 验证并提交**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' Catfishing Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Environment.WaterRegion' -RunName 'phase-a1-water-region'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Environment.WaterQuery' -RunName 'phase-a1-water-query-compatibility'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Environment.WaterPresentation' -RunName 'phase-a1-water-presentation'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Framework' -RunName 'phase-a1-water-framework-regression'
git add -- Source/Catfishing/Environment/CatWaterRegion.h Source/Catfishing/Environment/CatWaterRegion.cpp Source/Catfishing/Environment/CatWaterBoundarySplineActor.h Source/Catfishing/Environment/CatWaterBoundarySplineActor.cpp Source/Catfishing/Environment/Tests/CatWaterRegionTests.cpp Source/Catfishing/Environment/Tests/CatWaterQuerySubsystemTests.cpp Source/Catfishing/Environment/Tests/CatWaterTestFixtures.h Source/Catfishing/Framework/Game/Tests/CatGameplayTypesTests.cpp Source/Catfishing/Environment/Presentation/CatWaterRegionPresentationActor.h Source/Catfishing/Environment/Presentation/CatWaterRegionPresentationActor.cpp Source/Catfishing/Environment/Presentation/CatWaterRegionPresentationSubsystem.h Source/Catfishing/Environment/Presentation/CatWaterRegionPresentationSubsystem.cpp Source/Catfishing/Environment/Tests/CatWaterRegionPresentationTests.cpp
git diff --cached --check
git commit -m "Bake authoritative spline water regions"
```

---

### Task 5: QuerySubsystem registry 与正式空间 API

**Files:**

- Modify: `Source/Catfishing/Environment/CatWaterQuerySubsystem.h`
- Modify: `Source/Catfishing/Environment/CatWaterQuerySubsystem.cpp`
- Modify: `Source/Catfishing/Environment/CatWaterRegion.h`
- Modify: `Source/Catfishing/Environment/CatWaterRegion.cpp`
- Rewrite: `Source/Catfishing/Environment/Tests/CatWaterQuerySubsystemTests.cpp`

**Interfaces:**

- Consumes: Region baked cache and bounds。
- Produces:

```cpp
FCatWaterSpatialResult QueryWaterPoint(const FVector& WorldPoint,
    const FCatWaterRegionHandle& ExpectedHandle) const;
FCatWaterSpatialResult QueryShoreRelation(const FVector& WorldPoint,
    const FCatWaterRegionHandle& ExpectedHandle) const;
FCatWaterSpatialResult QueryNearestShoreForPreview(const FVector& WorldPoint,
    FName OptionalRegionId = NAME_None) const;
FCatWaterSpatialResult ResolveRayToWater(const FVector& RayOrigin,
    const FVector& RayDirection, const FCatWaterRegionHandle& ExpectedHandle) const;
FCatWaterSpatialResult ResolveCandidatePointToWater(const FVector& CandidateWorldPoint,
    const FCatWaterRegionHandle& ExpectedHandle) const;
ECatWaterQueryError FindRegionById(FName RegionId, FCatWaterRegionHandle& OutHandle) const;
```

Private `RegisterRegion(ACatWaterRegion*)` / `UnregisterRegion(const ACatWaterRegion*)` stores `RegionId -> TArray<TWeakObjectPtr<ACatWaterRegion>>`。重复 ID 仍保留为歧义事实，不能忽略第二个 Actor。

- [ ] **Step 1: 写以下失败测试**

- `Catfishing.Unit.Environment.WaterQuery.RegistersAndUnregistersBakedRegions`
- `Catfishing.Unit.Environment.WaterQuery.RejectsStaleExpectedHandle`
- `Catfishing.Unit.Environment.WaterQuery.HeightToleranceAndRegionAmbiguityFailClosed`
- `Catfishing.Unit.Environment.WaterQuery.ExactHandleCannotOverrideOverlappingRegionAmbiguity`
- `Catfishing.Unit.Environment.WaterQuery.ResolveRayUsesHorizontalWaterPlane`
- `Catfishing.Unit.Environment.WaterQuery.CandidateIgnoresClientZAndProjectsToHorizontalPlane`
- `Catfishing.Unit.Environment.WaterQuery.BoundsAreOnlyACoarseFilter`
- `Catfishing.Unit.Environment.WaterQuery.ExactHandleStillReturnsOutsideShoreBeyondBounds`
- `Catfishing.Unit.Environment.WaterQuery.NearestShoreTieBreakIsStable`
- `Catfishing.Unit.Environment.WaterQuery.DataValidationRejectsOverlappingSameHeightRegions`

Runtime query 测试 Actor 用 `SpawnActorDeferred`，通过 Task 4 的 `FCatWaterRegionTestAccess` 注入 pure geometry Build 产出的合法 cache，再 `FinishSpawning`，保证 BeginPlay 注册看到完整缓存且 Game target 不引用 Editor Bake API。`DataValidationRejectsOverlappingSameHeightRegions` 单独包在 `#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS`，只在 Editor target 构造 authoring actor/Bake。

- [ ] **Step 2: 构建并确认 RED**

Expected: compile fails on missing registry/query API.

- [ ] **Step 3: 实现注册、粗筛和查询**

Region `BeginPlay` 注册、`EndPlay` 精确注销。每次查询先 compact invalid weak refs；无 Handle 的预览/发现查询可以用 Bounds2D/height 粗筛。exact Handle API 先按 RegionId 直接解析唯一 Actor并比较完整 Revision，随后一定调用其 polygon kernel，即使点在 Bounds 外也要返回 Outside 的最近岸距；若目标 Region 对该点给出有效水内/边界结果，还必须用 registry 的 Bounds2D/height 粗筛其余 Region 并执行 polygon kernel，任一不同 Region 同时有效就返回 `AmbiguousRegion`，ExpectedHandle 不能授权调用者在重叠水域中任选其一。`QueryWaterPoint` 使用 `WaterPointVerticalToleranceCm`；`QueryShoreRelation` 使用 exact Handle 与 `BankHeightToleranceCm`，供岸外角色判定。`ResolveCandidatePointToWater` 只检查客户端输入有限值，随后按 Expected Region 把候选转换到 plane、强制 local Z=0，再做多边形/岸边修正；客户端 Z 不参与合法性。`ResolveRayToWater` 用服务器 Ray 和水平面交点。ExpectedHandle 必须完整相等；不能查出最新 Handle 后替调用者静默升级。只供客户端预览的 `QueryNearestShoreForPreview` 可以按 optional RegionId 选候选，但任何 authority 消费者禁止调用它。

普通 Editor World 不执行 BeginPlay，因此 `IsDataValid` 不依赖 runtime registry：它在 `WITH_EDITOR` 下只读枚举当前 World 的 WaterRegion，随后复用同一纯几何 overlap predicate 检测同高度有效区域重叠。Actor 枚举只存在于 Editor Data Validation；运行时查询仍禁止 World 扫描并在歧义时返回 `AmbiguousRegion`。

- [ ] **Step 4: 保留单一临时兼容 wrapper**

旧 `QueryWaterRegion(const FCatWaterQuery&)` 在本任务只作为编译接缝：先校验旧 Run DTO，然后调用新 registry/cache；禁止 `TActorIterator` 和 AABB 最终判定。Task 14 删除 wrapper 及旧 DTO。

- [ ] **Step 5: 验证并提交**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Environment.WaterQuery' -RunName 'phase-a1-water-query'
git add -- Source/Catfishing/Environment/CatWaterQuerySubsystem.h Source/Catfishing/Environment/CatWaterQuerySubsystem.cpp Source/Catfishing/Environment/CatWaterRegion.h Source/Catfishing/Environment/CatWaterRegion.cpp Source/Catfishing/Environment/Tests/CatWaterQuerySubsystemTests.cpp
git diff --cached --check
git commit -m "Route water queries through baked region registry"
```

---

### Task 6: 空间窝料类型与 Equipment Definition

**Files:**

- Create: `Source/Catfishing/Environment/CatChumFieldTypes.h`
- Create: `Source/Catfishing/Environment/CatChumFieldTypes.cpp`
- Modify: `Source/Catfishing/Environment/CatWaterTypes.h`
- Modify: `Source/Catfishing/Equipment/CatEquipmentDefinition.h`
- Modify: `Source/Catfishing/Equipment/CatEquipmentDefinition.cpp`
- Modify: `Source/Catfishing/Equipment/Tests/CatEquipmentDefinitionTests.cpp`
- Create: `Source/Catfishing/Environment/Tests/CatChumFieldTypesTests.cpp`

**Interfaces:**

- Consumes: `FCatWaterRegionHandle` 与现有 `ECatEquipmentKind::Chum`。
- Produces:

```cpp
UENUM(BlueprintType)
enum class ECatChumFieldSource : uint8 { Player, NaturalEvent };

UENUM(BlueprintType)
enum class ECatChumFieldError : uint8
{
    None, FeatureDisabled, CommandsClosed, InvalidIdentity, InvalidPayload,
    DefinitionUnavailable, InvalidWaterTarget, StaleGeometry, PlacementOutOfRange,
    PlacementOccluded, EquipmentRevisionConflict, EquipmentUnavailable,
    FieldCapacityExceeded, AlreadyResolved, DependencyUnavailable
};

struct FCatChumRuntimeInfluence;

USTRUCT(BlueprintType)
struct FCatChumInfluenceSpec
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) double RadiusCentimeters = 0.0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) double DurationSeconds = 0.0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FCatChumVector BaseContribution;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UCurveFloat> DistanceFalloffCurve;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UCurveFloat> TimeFalloffCurve;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MaximumQuantityPerPlacement = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName PresentationId = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftClassPtr<AActor> PresentationClass;
    bool IsRuntimeReady() const;
    bool BuildRuntimeInfluence(int32 Quantity, FCatChumRuntimeInfluence& OutRuntime) const;
};

struct FCatChumFalloffTable
{
    static constexpr int32 SampleCount = 65;
    TStaticArray<double, SampleCount> Samples{};
    double Evaluate(double NormalizedInput) const;
    bool IsRuntimeReady() const;
};

struct FCatChumRuntimeInfluence
{
    double RadiusCentimeters = 0.0;
    double DurationSeconds = 0.0;
    FCatChumVector BaseContribution;
    FCatChumFalloffTable DistanceFalloff;
    FCatChumFalloffTable TimeFalloff;
    FName PresentationId = NAME_None;
};

USTRUCT(BlueprintType)
struct FCatPlaceChumCommand
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) FGuid RequestId;
    UPROPERTY(BlueprintReadWrite) FCatWaterRegionHandle ExpectedWaterRegionHandle;
    UPROPERTY(BlueprintReadWrite) int64 ExpectedEquipmentRevision = 0;
    UPROPERTY(BlueprintReadWrite) FName ChumDefinitionId = NAME_None;
    UPROPERTY(BlueprintReadWrite) int32 Quantity = 0;
    UPROPERTY(BlueprintReadWrite) FVector ClientCandidateWorldPoint = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FCatPlaceChumResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FGuid RequestId;
    UPROPERTY(BlueprintReadOnly) bool bCommitted = false;
    UPROPERTY(BlueprintReadOnly) ECatChumFieldError Error = ECatChumFieldError::DependencyUnavailable;
    UPROPERTY(BlueprintReadOnly) FGuid FieldId;
    UPROPERTY(BlueprintReadOnly) FCatWaterRegionHandle WaterRegion;
    UPROPERTY(BlueprintReadOnly) FVector ServerCorrectedCenter = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) double StartServerTime = 0.0;
    UPROPERTY(BlueprintReadOnly) double ExpireServerTime = 0.0;
    UPROPERTY(BlueprintReadOnly) int64 EquipmentRevision = 0;
    UPROPERTY(BlueprintReadOnly) int64 ChumFieldSetRevision = 0;
};

struct FCatChumFieldState
{
    FGuid FieldId;
    FCatWaterRegionHandle WaterRegion;
    FName ChumDefinitionId = NAME_None;
    FVector CenterWorldPoint = FVector::ZeroVector;
    FCatChumRuntimeInfluence Influence;
    double StartServerTime = 0.0;
    double ExpireServerTime = 0.0;
    ECatChumFieldSource Source = ECatChumFieldSource::Player;
    FString OwnerStableNetId;
    bool bPublicationFlushed = false;
};

USTRUCT(BlueprintType)
struct FCatChumSample
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bSucceeded = false;
    UPROPERTY(BlueprintReadOnly) ECatChumFieldError Error = ECatChumFieldError::DependencyUnavailable;
    UPROPERTY(BlueprintReadOnly) FCatWaterRegionHandle WaterRegion;
    UPROPERTY(BlueprintReadOnly) int64 ChumFieldSetRevision = 0;
    UPROPERTY(BlueprintReadOnly) double SampleServerTime = 0.0;
    UPROPERTY(BlueprintReadOnly) FCatChumVector EffectiveChumVector;
    UPROPERTY(BlueprintReadOnly) int32 ContributingFieldCount = 0;
    TArray<FGuid> ContributingFieldIds; // server diagnostics/tests only
};
```

同时给 `FCatChumVector` 增加不修改输入的 `ScaledBy(double)` 和显式 `Accumulate(const FCatChumVector&)`；所有运算检查有限值，避免依赖未定义的 C++ 运算符。

`FCatChumInfluenceSpec::BuildRuntimeInfluence(Quantity, OutRuntime)` 在 placement 前把两条曲线各烘成 65 个 `[0,1]` 等距 double 样本，运行时只在线性插值 LUT；因此曲线资产之后卸载或修改也不会改变既有 Field。Runtime influence 的 `BaseContribution` 已乘 Quantity。`ContributingFieldIds` 不是 UPROPERTY，不复制、不进 Blueprint。`PresentationClass` 暂以 `AActor` soft class 保持本提交闭包；Task 13 创建原生表现父类后才验证并使用其子类。

- [ ] **Step 1: 写 value-type 与 Definition RED 测试**

- `Catfishing.Unit.Environment.ChumField.TypesDefaultsFailClosed`
- `Catfishing.Unit.Environment.ChumField.SpecRequiresContributionRadiusLifetimeCurvesAndQuantity`
- `Catfishing.Unit.Environment.ChumField.QuantityScalesContributionButNotRadiusOrDuration`
- `Catfishing.Unit.Equipment.Definition.ChumRequiresSpatialInfluenceSpec`

曲线 fixture 使用 transient `UCurveFloat`，明确写 `(0,1)`、`(1,0)`；非法 NaN、负输出、空曲线、Quantity 超上限必须拒绝。`PresentationId/PresentationClass` 都是 optional；Task 6 只要求设置的 soft class 能解析为 `AActor`，具体表现父类约束在 Task 13 落地。

- [ ] **Step 2: 构建并确认 RED**

Expected: compile fails on missing `FCatChumInfluenceSpec`/Placement types.

- [ ] **Step 3: 实现 Spec 与 DTO**

把 `FCatChumVector` 三轴改为 `EditAnywhere, BlueprintReadOnly`。曲线输入 clamp 到 `[0,1]`；Build/读取的 65 个样本必须有限且非负，LUT 线性插值后的运行值因此同样合法。Distance/Time 曲线不要求相同，但都要求 `x=0` 有正效果、`x=1` 不为负；运行态永远不直接再次 Evaluate soft curve。

- [ ] **Step 4: 替换 Equipment Definition 字段**

增加正式字段：

```cpp
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Chum")
FCatChumInfluenceSpec ChumInfluence;
```

为保证旧 Controller 在 Task 9 前逐提交可编译，`ChumContribution` 暂留为 deprecated compile bridge，但 `IsRuntimeDefinitionReady()` 和所有新路径只读 `ChumInfluence`。Task 9 删除旧 RPC 时同步删除 `ChumContribution`。Chum readiness 要求 `bRunConsumable=true`、`bSpecialBait=false`、`MaximumRodDurability==0`、`ChumInfluence.IsRuntimeReady()`；非 Chum definition 的 Influence 必须保持默认未配置。

- [ ] **Step 5: 验证并提交**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Environment.ChumField' -RunName 'phase-a1-chum-types'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Equipment.Definition' -RunName 'phase-a1-chum-definition'
git add -- Source/Catfishing/Environment/CatChumFieldTypes.h Source/Catfishing/Environment/CatChumFieldTypes.cpp Source/Catfishing/Environment/CatWaterTypes.h Source/Catfishing/Equipment/CatEquipmentDefinition.h Source/Catfishing/Equipment/CatEquipmentDefinition.cpp Source/Catfishing/Equipment/Tests/CatEquipmentDefinitionTests.cpp Source/Catfishing/Environment/Tests/CatChumFieldTypesTests.cpp
git diff --cached --check
git commit -m "Define spatial chum influence data"
```

---

### Task 7: 通用 Run Consumable 原子预留

**Files:**

- Modify: `Source/Catfishing/Equipment/CatEquipmentTypes.h`
- Modify: `Source/Catfishing/Equipment/CatEquipmentComponent.h`
- Modify: `Source/Catfishing/Equipment/CatEquipmentComponent.cpp`
- Create: `Source/Catfishing/Equipment/Tests/CatEquipmentConsumableUseTests.cpp`

**Interfaces:**

- Consumes: authority Equipment Snapshot、Definition catalog、RequestId/ExpectedRevision。
- Produces:

```cpp
USTRUCT(BlueprintType)
struct FCatRunConsumableUseResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FGuid OperationId;
    UPROPERTY(BlueprintReadOnly) ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;
    UPROPERTY(BlueprintReadOnly) FName DefinitionId = NAME_None;
    UPROPERTY(BlueprintReadOnly) int32 Quantity = 0;
    UPROPERTY(BlueprintReadOnly) int64 EquipmentRevision = 0;
    UPROPERTY(BlueprintReadOnly) bool bReserved = false;
    UPROPERTY(BlueprintReadOnly) bool bCommitted = false;
    UPROPERTY(BlueprintReadOnly) bool bReleased = false;
};

FCatRunConsumableUseResult BeginRunConsumableUse(FGuid OperationId,
    FName DefinitionId, int32 Quantity, int64 ExpectedRevision);
FCatRunConsumableUseResult CommitRunConsumableUse(FGuid OperationId);
FCatRunConsumableUseResult CommitRunConsumableUseDeferred(FGuid OperationId);
void PublishDeferredRunConsumableUse(FGuid OperationId);
FCatRunConsumableUseResult ReleaseRunConsumableUse(FGuid OperationId);
bool HasActiveRunConsumableUse() const;
```

- [ ] **Step 1: 写以下失败测试**

- `Catfishing.Unit.Equipment.ConsumableUse.BeginReservesExactQuantityWithoutPublishingSnapshot`
- `Catfishing.Unit.Equipment.ConsumableUse.CommitConsumesOnceAndReplayReturnsFrozenObservables`
- `Catfishing.Unit.Equipment.ConsumableUse.DeferredCommitIsInvisibleUntilIdempotentPublish`
- `Catfishing.Unit.Equipment.ConsumableUse.ReleasePreservesInventoryAndRejectsLateCommit`
- `Catfishing.Unit.Equipment.ConsumableUse.ActiveReservationBlocksConflictingMutations`
- `Catfishing.Unit.Equipment.ConsumableUse.FishingAndRunReservationsAreMutuallyExclusive`
- `Catfishing.Unit.Equipment.ConsumableUse.ReservedQuantityCannotBeDoubleSpentByLegacyConsume`

测试必须覆盖 `ConfigureLoadoutFromAuthority`、Grant、legacy Consume、`CommitFishingFailure`、`BeginFishingUse` 和 `RepairRodAtCamp` 在活动 reservation 时不改 Snapshot/Revision。

- [ ] **Step 2: 构建并确认 RED**

Expected: compile fails on missing API/types.

- [ ] **Step 3: 实现 private record 与 frozen replay**

```cpp
struct FCatRunConsumableUseRecord
{
    FGuid OperationId;
    FName DefinitionId = NAME_None;
    int32 Quantity = 0;
    int64 ReservationRevision = 0;
    bool bCommitted = false;
    bool bReleased = false;
};
```

只允许一个 active run-consumable reservation，并与 active FishingUse 双向互斥：任何一类已经 active 时，另一类 Begin 无副作用拒绝。Begin 不修改公开 Snapshot；`CommitRunConsumableUseDeferred` 对有效 active record 不重新分配、不重新查 Definition，只扣冻结数量并推进一次 Revision，但不广播/ForceNetUpdate；`PublishDeferredRunConsumableUse` 才恰好一次发布，且只能用于已 deferred-committed 的同一 Operation。普通 `CommitRunConsumableUse` 是“deferred commit + publish”的兼容封装。Release 不扣库存。所有 replay 返回首次 Definition/Quantity/Revision/状态，不返回当前另一条操作的值；record 额外保存 `bCommitPublished`。

- [ ] **Step 4: 验证并提交**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Equipment.ConsumableUse' -RunName 'phase-a1-consumable-use'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Equipment' -RunName 'phase-a1-equipment-regression'
git add -- Source/Catfishing/Equipment/CatEquipmentTypes.h Source/Catfishing/Equipment/CatEquipmentComponent.h Source/Catfishing/Equipment/CatEquipmentComponent.cpp Source/Catfishing/Equipment/Tests/CatEquipmentConsumableUseTests.cpp
git diff --cached --check
git commit -m "Reserve run consumables for atomic effects"
```

---

### Task 8: Chum Field settings、权威 Subsystem、预算、幂等与自然 Anchor

**Files:**

- Create: `Source/Catfishing/Environment/CatChumFieldSettings.h`
- Create: `Source/Catfishing/Environment/CatChumFieldSettings.cpp`
- Create: `Source/Catfishing/Environment/CatChumFieldSubsystem.h`
- Create: `Source/Catfishing/Environment/CatChumFieldSubsystem.cpp`
- Create: `Source/Catfishing/Environment/CatChumFieldAnchor.h`
- Create: `Source/Catfishing/Environment/CatChumFieldAnchor.cpp`
- Create: `Source/Catfishing/Environment/Tests/CatChumFieldSubsystemTests.cpp`
- Create: `Source/Catfishing/Environment/Tests/CatChumFieldSettingsTests.cpp`

**Interfaces:**

- Consumes: exact Water Handle/query、Influence Spec、authority server time。
- Produces:

```cpp
struct FCatChumFieldCommitToken { FGuid Value; bool IsValid() const; };

struct FCatPrepareChumFieldResult
{
    bool bPrepared = false;
    ECatChumFieldError Error = ECatChumFieldError::DependencyUnavailable;
    FCatChumFieldCommitToken CommitToken;
    FGuid FieldId;
    FCatWaterRegionHandle WaterRegion;
    FVector CorrectedCenter = FVector::ZeroVector;
    double StartServerTime = 0.0;
    double ExpireServerTime = 0.0;
};

struct FCatChumRequestKey
{
    FString StableNetId;
    FGuid RequestId;
    bool operator==(const FCatChumRequestKey& Other) const;
    friend uint32 GetTypeHash(const FCatChumRequestKey& Key);
};

struct FCatPrepareChumFieldRequest
{
    FString StableNetId;
    FCatPlaceChumCommand Command;
    FVector ServerCorrectedCenter = FVector::ZeroVector;
    FCatChumInfluenceSpec Influence;
    ECatChumFieldSource Source = ECatChumFieldSource::Player;
    double ServerTime = 0.0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FCatChumFieldActivated, FGuid);
DECLARE_MULTICAST_DELEGATE_OneParam(FCatChumFieldRemoved, FGuid);

UCLASS()
class CATFISHING_API UCatChumFieldSubsystem final : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    FCatPrepareChumFieldResult PrepareField(const FCatPrepareChumFieldRequest& Request);
    FCatPlaceChumResult ActivatePreparedFieldDeferred(
        FCatChumFieldCommitToken Token, int64 EquipmentRevision);
    void PublishActivatedField(FGuid FieldId);
    void AbortPreparedField(FCatChumFieldCommitToken Token);
    bool TryGetTerminalResult(const FString& StableNetId, FGuid RequestId, FCatPlaceChumResult& OutResult) const;
    void StoreTerminalResult(const FString& StableNetId, const FCatPlaceChumResult& Result);
    FCatChumSample SampleChumAtPoint(const FVector& WorldPoint,
        const FCatWaterRegionHandle& ExpectedHandle, double ServerTime) const;
    int32 CleanupExpiredFields(double ServerTime);
    FCatChumFieldActivated OnFieldActivated;
    FCatChumFieldRemoved OnFieldRemoved;
};
```

`UCatChumFieldSettings` exact config fields:

```cpp
bool bEnableChumFieldRuntime = false;
int32 MaxActiveFieldsPerRegion = 0;
double MaxRawContributionPerRegion = 0.0;
double MaxPlacementRangeCentimeters = 0.0;
double MaxAimDeviationDegrees = 0.0;
TEnumAsByte<ECollisionChannel> PlacementLineOfSightChannel = ECC_Visibility;
double ExpiredCleanupIntervalSeconds = 0.0;
```

Runtime readiness 要求 gate=true、两个预算正、Placement range/angle 正且 angle `<=180`、cleanup interval 正、LOS channel 可用；任何未裁字段都使玩家和自然 Field 创建 fail-closed。

`ACatChumFieldAnchor` fields: `AnchorId`, `ExpectedWaterRegionHandle`; Actor location is candidate center, no Tick、no replication、no collision。

Subsystem 无 Tick、无复制；所有 Prepare/Activate/Abort/Store/Cleanup 与 Sample 在 `NM_Client` 都 fail-closed。客户端只能消费 Task 13 的 FastArray 表现镜像。

- [ ] **Step 1: 写 settings/field RED 测试**

- `Catfishing.Unit.Environment.ChumField.SettingsDefaultsFailClosed`
- `Catfishing.Unit.Environment.ChumField.SampleAddsOnlyOverlappingActiveFieldsInSameRegion`
- `Catfishing.Unit.Environment.ChumField.ExcludeAndLandPointsNeverContribute`
- `Catfishing.Unit.Environment.ChumField.DistanceTimeFalloffAndExpiryAreDeterministic`
- `Catfishing.Unit.Environment.ChumField.FieldIdsStabilizeFloatingPointAccumulation`
- `Catfishing.Unit.Environment.ChumField.BudgetReservationAbortAndActivationAreAtomic`
- `Catfishing.Unit.Environment.ChumField.ReplayDoesNotReserveOrCreateTwice`
- `Catfishing.Unit.Environment.ChumField.CleanupAdvancesFieldSetRevisionExactlyOnce`
- `Catfishing.Unit.Environment.ChumField.AuthorityTimerCleansExpiredFieldWithoutSampling`
- `Catfishing.Unit.Environment.ChumField.DistantFieldDoesNotCausePlacementRevisionConflict`

- [ ] **Step 2: 构建并确认 RED**

Expected: compile fails on missing settings/subsystem/anchor.

- [ ] **Step 3: 实现无 Actor 的权威存储**

```cpp
TMap<FGuid, FCatChumFieldState> FieldsById;
TMap<FName, TArray<FGuid>> FieldIdsByRegion;
TMap<FName, int64> FieldSetRevisionByRegion;
TMap<FGuid, FCatPendingChumField> PendingByToken;
TMap<FCatChumRequestKey, FCatPlaceChumResult> TerminalByIdentityAndRequest;
TMap<FName, FCatChumBudgetState> BudgetByRegion;
```

Prepare 先按服务器时间清理已过期记录，再完成 FieldId、容量、贡献预算、索引槽位预留，并调用 `BuildRuntimeInfluence` 把两条 soft curve 冻结成 Pending state 内的纯值 LUT，之后才发 CommitToken。`MaxRawContributionPerRegion` 精确定义为所有 Active+Pending Field 的非负 `Fishy+Fragrant+Fermented` 基础贡献总和。`ActivatePreparedFieldDeferred` 对仍有效 token 只执行不可失败的容器状态切换并推进 FieldSetRevision，但不广播；`PublishActivatedField` 在 terminal 已落账后恰好一次触发 `OnFieldActivated`。它们都不再查资产、不分配 ID、不验证容量。时间衰减不推进版本。

`Initialize` 仅在 authority/Standalone World 中按正 `ExpiredCleanupIntervalSeconds` 注册重复 Timer，回调读取服务器时间并调用 Cleanup；`Deinitialize` 先清 Timer 再清 pending/active/index。Cleanup 删除过期记录、释放预算、每个受影响 Region 只推进一次 FieldSetRevision，并在状态完成后逐 Field 广播一次 `OnFieldRemoved`。客户端不创建 Timer。测试必须推进真实 Test World 时间触发 Timer，不能直接手调 Cleanup 冒充调度。

- [ ] **Step 4: 实现采样**

先调用 `QueryWaterPoint(WorldPoint, ExpectedHandle)` 并要求 Inside；随后只遍历同 Region bucket，过滤 `Start <= time < Expire` 与 2D radius。按 `FGuid.A/B/C/D` 的无符号字典序排序后用 double 累加（不能 `memcmp` 依赖平台字节序）：

```cpp
const double D = FVector2D::Distance(FieldCenter2D, Sample2D)
    / Field.Influence.RadiusCentimeters;
const double T = (ServerTime - Field.StartServerTime) / (Field.ExpireServerTime - Field.StartServerTime);
const double Weight = Field.Influence.DistanceFalloff.Evaluate(D)
    * Field.Influence.TimeFalloff.Evaluate(T);
Out.EffectiveChumVector.Accumulate(Field.Influence.BaseContribution.ScaledBy(Weight));
```

- [ ] **Step 5: 验证并提交**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Environment.ChumField' -RunName 'phase-a1-chum-subsystem'
git add -- Source/Catfishing/Environment/CatChumFieldSettings.h Source/Catfishing/Environment/CatChumFieldSettings.cpp Source/Catfishing/Environment/CatChumFieldSubsystem.h Source/Catfishing/Environment/CatChumFieldSubsystem.cpp Source/Catfishing/Environment/CatChumFieldAnchor.h Source/Catfishing/Environment/CatChumFieldAnchor.cpp Source/Catfishing/Environment/Tests/CatChumFieldSubsystemTests.cpp Source/Catfishing/Environment/Tests/CatChumFieldSettingsTests.cpp
git diff --cached --check
git commit -m "Add authoritative spatial chum fields"
```

---

### Task 9: PlaceChum 原子协调与 owner-only 结果通道

**Files:**

- Create: `Source/Catfishing/Environment/CatChumPlacementService.h`
- Create: `Source/Catfishing/Environment/CatChumPlacementService.cpp`
- Modify: `Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.h`
- Modify: `Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.cpp`
- Modify: `Source/Catfishing/Fishing/Integration/CatFishingCommandComponent.h`
- Modify: `Source/Catfishing/Fishing/Integration/CatFishingCommandComponent.cpp`
- Modify: `Source/Catfishing/Equipment/CatEquipmentDefinition.h`
- Modify: `Source/Catfishing/Equipment/CatEquipmentDefinition.cpp`
- Modify narrowly: `Source/Catfishing/Framework/Game/CatGameplayTypes.h`
- Modify narrowly: `Source/Catfishing/Framework/Game/CatGameplayTypes.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingContractTests.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingCommandComponentTests.cpp`
- Create: `Source/Catfishing/Environment/Tests/CatChumPlacementServiceTests.cpp`
- Modify: `Source/Catfishing/Framework/Game/Tests/CatGameplayTypesTests.cpp`

**Interfaces:**

- Consumes: GameMode command gate、Controller/Pawn identity、Water query、Equipment reservation、Field commit token。
- Produces:

```cpp
UCLASS()
class CATFISHING_API UCatChumPlacementService final : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    FCatPlaceChumResult PlaceChum(
        APlayerController* RequestingController, const FCatPlaceChumCommand& Command);
};

UFUNCTION(BlueprintCallable, Category="Catfishing|Chum")
void UCatFishingCommandComponent::SubmitPlaceChum(const FCatPlaceChumCommand& Command);

UFUNCTION(Server, Reliable)
void ServerSubmitPlaceChum(const FCatPlaceChumCommand& Command);

void DeliverPlaceChumResultFromAuthority(const FCatPlaceChumResult& Result);
bool TryGetPlaceChumResult(FGuid RequestId, FCatPlaceChumResult& OutResult) const;
```

专用 `ClientReceivePlaceChumResult(const FCatPlaceChumResult&)` 为 Reliable Client RPC；收到后同时生成通用 `FCatFishingCommandResult{CommandType=PlaceChum}` 放进现有邮箱，Ability 可以只等通用 RequestId，UI 可以读取空间结果。

PlacementService 无 terminal map、无 Tick、无复制；它只在 authority World 中协调同 World 的 Query/Equipment/Field Subsystem。CommandComponent 的 Server RPC 取得该 WorldSubsystem，调用一次 `PlaceChum`，再调用一次 `DeliverPlaceChumResultFromAuthority`。

- [ ] **Step 1: 写合同与事务 RED 测试**

- `Catfishing.Unit.Fishing.Contracts.PlaceChumUsesWaterAndEquipmentConcurrencyNotSessionIdentity`
- `Catfishing.Unit.Fishing.CommandComponent.PlaceChumResultIsOwnerScopedFirstWinsAndBounded`
- `Catfishing.Unit.Environment.ChumPlacement.ClientIdentityAndActorPointersCannotAuthorizePlacement`
- `Catfishing.Unit.Environment.ChumPlacement.SmallOuterBankMissCorrectsAndHoleOrLargeMissRejects`
- `Catfishing.Unit.Environment.ChumPlacement.FieldBudgetFailureLeavesNoActiveEquipmentReservation`
- `Catfishing.Unit.Environment.ChumPlacement.EquipmentFailureAbortsPendingField`
- `Catfishing.Unit.Environment.ChumPlacement.CommitConsumesQuantityAndActivatesExactlyOneField`
- `Catfishing.Unit.Environment.ChumPlacement.ReplayReturnsSameFieldWithoutSecondConsumption`
- `Catfishing.Unit.Environment.ChumPlacement.IdentityScopesRequestReplayAndFailureIsFirstWins`
- `Catfishing.Unit.Environment.ChumPlacement.SynchronousPublishReentrySeesFrozenTerminalAndNoSecondSideEffect`

反射测试必须断言 PlaceChum 命令没有 `SessionId/CastAttemptId/ExpectedRevision/ACatWaterRegion*`。

- [ ] **Step 2: 构建并确认 RED**

Expected: missing service/RPC/result APIs cause compile failure.

- [ ] **Step 3: 实现服务器候选点验证**

服务从 owning PlayerState 派生 StableNetId，从 `GetPawnViewLocation()` 和 server `ControlRotation` 得到权威视角。调用 `ResolveCandidatePointToWater(Command.ClientCandidateWorldPoint, ExpectedHandle)`，再验证：

```cpp
Distance(ViewOrigin, CorrectedCenter) <= MaxPlacementRangeCentimeters;
Dot(ServerViewDirection, Normalize(CorrectedCenter - ViewOrigin))
    >= cos(MaxAimDeviationDegrees);
LineTraceSingleByChannel(ViewOrigin, CorrectedCenter,
    PlacementLineOfSightChannel, ignoring ControlledCharacter) has no blocking hit;
```

Expected Handle 过期、岛屿/Exclude、明显越界、超距、角度或 LOS 失败均在 Equipment 前拒绝。

- [ ] **Step 4: 实现严格事务顺序**

1. 查 Field Subsystem terminal replay。
2. 验证命令/身份/定义/Quantity/落点。
3. `BeginRunConsumableUse(RequestId, DefinitionId, Quantity, ExpectedEquipmentRevision)`。
4. `PrepareField(PrepareRequest)`；失败则 Release Equipment。
5. `CommitRunConsumableUseDeferred(RequestId)`；失败则 Abort Field。
6. `ActivatePreparedFieldDeferred(Token, committed equipment revision)`；valid token 路径不得失败。
7. 构造成功 Result 并 Store terminal；只有 ledger 已冻结后，才依次 `PublishDeferredRunConsumableUse`、`PublishActivatedField`，最后由 CommandComponent 投递 owner result。

一旦服务器已派生出非空 StableNetId 且 RequestId 合法，以上任一成功或失败分支都必须走同一个 `FinalizeFirstResult`：仅首次写入 `(StableNetId, RequestId)` terminal ledger 并返回该结果。无效身份/无效 RequestId 无法形成 ledger key，直接 fail-closed；客户端若要在 RevisionConflict 后重试必须生成新 RequestId。

Equipment Snapshot、Field activated delegate 与后续 FastArray 都是同步可观察写口，禁止在 terminal 落账前广播。重入测试分别从 Equipment `OnSnapshotChanged` 与 Field `OnFieldActivated` 回调再次提交相同 RequestId，必须只读到首次成功 terminal，且 Quantity、Field count、Revision 与 owner mailbox 都只变化一次。

- [ ] **Step 5: 删除旧玩家 Chum RPC**

从 Controller 删除 `ServerContributeChum`、`PlayerChumTerminalCache`、`MakeInvalidPlayerChumResult` 与旧测试；从 CommandTypes 删除 `FCatContributeChumCommand/Result`，把 enum 值替换为 `PlaceChum`；从 Equipment Definition 删除 deprecated `ChumContribution`。给 `UCatFishingCommandComponent` 加最小 friend 访问 `CanForwardGameplayCommand()`；不移动或格式化任何输入方法。

- [ ] **Step 6: 验证、审查 Controller 窄 diff 并提交**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Environment.ChumPlacement' -RunName 'phase-a1-chum-placement'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Fishing.CommandComponent' -RunName 'phase-a1-place-chum-results'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Fishing.Contracts' -RunName 'phase-a1-place-chum-contracts'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Framework' -RunName 'phase-a1-place-chum-framework'
git diff -- Source/Catfishing/Framework/Game/CatGameplayTypes.h Source/Catfishing/Framework/Game/CatGameplayTypes.cpp
git add -- Source/Catfishing/Environment/CatChumPlacementService.h Source/Catfishing/Environment/CatChumPlacementService.cpp Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.h Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.cpp Source/Catfishing/Fishing/Integration/CatFishingCommandComponent.h Source/Catfishing/Fishing/Integration/CatFishingCommandComponent.cpp Source/Catfishing/Equipment/CatEquipmentDefinition.h Source/Catfishing/Equipment/CatEquipmentDefinition.cpp Source/Catfishing/Framework/Game/CatGameplayTypes.h Source/Catfishing/Framework/Game/CatGameplayTypes.cpp Source/Catfishing/Fishing/Tests/CatFishingContractTests.cpp Source/Catfishing/Fishing/Tests/CatFishingCommandComponentTests.cpp Source/Catfishing/Environment/Tests/CatChumPlacementServiceTests.cpp Source/Catfishing/Framework/Game/Tests/CatGameplayTypesTests.cpp
git diff --cached --check
git commit -m "Place spatial chum through atomic authority commands"
```

---

### Task 10: Fish 局部 Chum、Bait 与饱和选择器

**Files:**

- Create: `Source/Catfishing/Data/CatFishSelectionTypes.h`
- Modify: `Source/Catfishing/Data/CatFishDefinition.h`
- Modify: `Source/Catfishing/Data/CatFishDefinition.cpp`
- Modify: `Source/Catfishing/Data/CatFishCatalogSettings.h`
- Modify: `Source/Catfishing/Data/CatFishCatalogSettings.cpp`
- Modify: `Source/Catfishing/Data/Tests/CatFishDefinitionTests.cpp`
- Modify: `Source/Catfishing/Data/Tests/CatFishCatalogSettingsTests.cpp`

**Interfaces:**

- Consumes: `FCatChumSample`、Bait definition ID、Environment/协作/seed。
- Produces:

```cpp
USTRUCT(BlueprintType)
struct FCatBaitWeightMultiplier
{
    GENERATED_BODY()
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName BaitDefinitionId = NAME_None;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) double Multiplier = 0.0;
};

USTRUCT()
struct FCatFishSelectionContext
{
    GENERATED_BODY()
    FCatWaterRegionHandle WaterRegion;
    FCatChumSample ChumSample;
    ECatEnvironmentTimeOfDay TimeOfDay = ECatEnvironmentTimeOfDay::Unknown;
    ECatEnvironmentWeather Weather = ECatEnvironmentWeather::Unknown;
    FName BaitDefinitionId = NAME_None;
    int32 ActivePlayerCount = 0;
    double CombinedFishingStrength = 0.0;
    double CombinedFightStamina = 0.0;
    int32 RandomSeed = 0;
};

USTRUCT()
struct FCatFishSelectionResult
{
    GENERATED_BODY()
    bool bSelected = false;
    FName FishDefinitionId = NAME_None;
    double WeightKilograms = 0.0;
    double SelectedFinalWeight = 0.0;
};

FCatFishSelectionResult SelectRuntimeDefinition(const FCatFishSelectionContext& Context) const;
```

`UCatFishDefinition` 用 `FCatChumVector ChumPreference` 和 `TArray<FCatBaitWeightMultiplier> BaitWeightMultipliers` 替换 `PreferredSpecialBaitIds`。未列出的合法 Bait multiplier 为 1.0；重复 ID、非正/非有限 multiplier fail-closed。

本任务新增 context overload，但暂留现有 raw-parameter selector overload 为 deprecated compile bridge，因为当前 `CatFishingService` 要到 Task 11 才迁移。旧 overload 保持原 Gate/neutral chum 行为，不调用新 context 伪造 Geometry Handle；Task 11 更新唯一生产调用后同步删除旧声明/实现。

Catalog settings 增加：

```cpp
TSoftObjectPtr<UCurveFloat> ChumSaturationCurve;
double ChumAffinityHalfSaturation = 0.0;
double MaximumChumModifier = 0.0;
```

`NormalizedAffinity = Raw / (Raw + HalfSaturation)`；HalfSaturation 必须正，MaximumChumModifier 必须有限且 `>=1`。曲线在 `[0,1]` 上必须单调不减、输出有限、`Curve(0)==1`、上限不超过 `MaximumChumModifier`。

- [ ] **Step 1: 写 selector RED 测试**

- `Catfishing.Unit.Data.FishDefinition.ChumPreferenceAndBaitMultipliersMustBeValid`
- `Catfishing.Unit.Data.FishCatalog.ZeroLocalChumAndUnlistedBaitAreNeutral`
- `Catfishing.Unit.Data.FishCatalog.OverlappingChumAndBaitComposeDeterministically`
- `Catfishing.Unit.Data.FishCatalog.SaturationIsMonotonicBoundedAndCannotGuaranteeRareFish`
- `Catfishing.Unit.Data.FishCatalog.SameContextAndSeedReturnSameFishAndWeight`
- `Catfishing.Unit.Data.FishCatalog.StaleChumGeometryFailsClosed`

- [ ] **Step 2: 构建并确认 RED**

Expected: compile fails on missing context/result/preferences.

- [ ] **Step 3: 实现纯选择公式**

先做现有 Region/时段/天气/协作 Gate，再计算：

```cpp
RawAffinity = Dot(Context.ChumSample.EffectiveChumVector, Fish.ChumPreference);
Normalized = RawAffinity <= 0.0 ? 0.0 : RawAffinity / (RawAffinity + HalfSaturation);
ChumModifier = Clamp(SaturationCurve->GetFloatValue(Normalized), 0.0, MaximumChumModifier);
BaitModifier = Fish.FindBaitMultiplierOrNeutral(Context.BaitDefinitionId);
FinalWeight = Fish.SpawnWeight * ChumModifier * BaitModifier;
```

按稳定 FishDefinitionId 排序后用确定性随机流抽取；Result 返回选中项的 FinalWeight 和重量。不得把局部 Chum 写回鱼定义或 Session。

- [ ] **Step 4: 验证并提交**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Data.Fish' -RunName 'phase-a1-fish-selection'
git add -- Source/Catfishing/Data/CatFishSelectionTypes.h Source/Catfishing/Data/CatFishDefinition.h Source/Catfishing/Data/CatFishDefinition.cpp Source/Catfishing/Data/CatFishCatalogSettings.h Source/Catfishing/Data/CatFishCatalogSettings.cpp Source/Catfishing/Data/Tests/CatFishDefinitionTests.cpp Source/Catfishing/Data/Tests/CatFishCatalogSettingsTests.cpp
git diff --cached --check
git commit -m "Select fish from local chum and bait context"
```

---

### Task 11: 正式 BeginCast 落水点与真咬时冻结选鱼

**Files:**

- Modify: `Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.h`
- Modify: `Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.cpp`
- Modify: `Source/Catfishing/Fishing/Integration/CatFishingCommandComponent.h`
- Modify: `Source/Catfishing/Fishing/Integration/CatFishingCommandComponent.cpp`
- Modify: `Source/Catfishing/Fishing/CatFishingTypes.h`
- Modify: `Source/Catfishing/Fishing/CatFishingService.h`
- Modify: `Source/Catfishing/Fishing/CatFishingService.cpp`
- Modify: `Source/Catfishing/Fishing/CatFishingSession.h`
- Modify: `Source/Catfishing/Fishing/CatFishingSession.cpp`
- Modify: `Source/Catfishing/Fishing/CatFishingSettings.h`
- Modify: `Source/Catfishing/Fishing/CatFishingSettings.cpp`
- Modify: `Source/Catfishing/Fishing/CatFishingStateTreeNodes.h`
- Modify: `Source/Catfishing/Fishing/CatFishingStateTreeNodes.cpp`
- Modify: `Source/Catfishing/Fishing/Actors/CatFishingHookActor.h`
- Modify: `Source/Catfishing/Fishing/Actors/CatFishingHookActor.cpp`
- Modify: `Source/Catfishing/Fishing/Actors/CatFishEncounterActor.h`
- Modify: `Source/Catfishing/Fishing/Actors/CatFishEncounterActor.cpp`
- Modify: `Source/Catfishing/Equipment/CatEquipmentComponent.h`
- Modify: `Source/Catfishing/Equipment/CatEquipmentComponent.cpp`
- Modify: `Source/Catfishing/Data/CatFishCatalogSettings.h`
- Modify: `Source/Catfishing/Data/CatFishCatalogSettings.cpp`
- Modify narrowly: `Source/Catfishing/Framework/Game/CatGameplayTypes.h`
- Modify narrowly: `Source/Catfishing/Framework/Game/CatGameplayTypes.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingContractTests.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingActorContractTests.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingCommandComponentTests.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingSettingsTests.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingServiceTests.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingSessionTests.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingStateTreeNodesTests.cpp`
- Modify: `Source/Catfishing/Equipment/Tests/CatEquipmentFishingUseTests.cpp`

**Interfaces:**

- Consumes: deployed Rod registry、canonical RodTip、Equipment FishingUse、Water candidate resolver、Run public Environment、Chum sample、Fish selector。
- Produces:

```cpp
// FCatBeginCastCommand adds:
UPROPERTY(BlueprintReadWrite) FCatWaterRegionHandle ExpectedWaterRegionHandle;

USTRUCT(BlueprintType)
struct FCatBeginCastResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FCatFishingCommandResult Command;
    UPROPERTY(BlueprintReadOnly) FCatWaterRegionHandle WaterRegion;
    UPROPERTY(BlueprintReadOnly) FVector ServerCorrectedLandingWorldPoint = FVector::ZeroVector;
};

FCatBeginCastResult UCatFishingService::BeginCast(
    AController* FisherController, const FCatBeginCastCommand& Command);

enum class ECatTrueBiteResolutionStatus : uint8
{
    InProgress, Selected, TerminalNoEligibleFish, Failed
};
struct FCatTrueBiteResolutionResult
{
    ECatTrueBiteResolutionStatus Status = ECatTrueBiteResolutionStatus::Failed;
    ECatDomainCommandError Error = ECatDomainCommandError::DependencyUnavailable;
    FCatFishSelectionResult Selection;
};
FCatTrueBiteResolutionResult UCatFishingService::ResolveTrueBiteSelection(
    FGuid FishingSessionId, double ServerTime);

void UCatFishingCommandComponent::SubmitBeginCast(const FCatBeginCastCommand& Command);
void DeliverBeginCastResultFromAuthority(const FCatBeginCastResult& Result);
bool TryGetBeginCastResult(FGuid RequestId, FCatBeginCastResult& OutResult) const;

UFUNCTION(Server, Reliable)
void ServerSubmitBeginCast(const FCatBeginCastCommand& Command);

UFUNCTION(Client, Reliable)
void ClientReceiveBeginCastResult(const FCatBeginCastResult& Result);
```

`FCatFishingAttemptSnapshot` 最终只存一个 `FCatWaterRegionHandle WaterRegion`，删除独立 `WaterRegionId/GeometryRevision`。Session 改成有 publication barrier 的两阶段初始化：准备与 StateTree 同步启动可以失败，但不产生本地 delegate/网络快照；Service terminal 与 active identity 可见后才统一 flush。初始 `FishDefinitionId=None`：

```cpp
struct FCatPreparedFishingSessionToken { FGuid Value; bool IsValid() const; };
bool PrepareSessionFromAuthority(const FCatFishingAttemptSnapshot& Attempt,
    AController* FisherController, ACatCharacter* FisherCharacter,
    ACatFishingHookActor* HookActor, FGuid FisherGuardContainerId,
    const FCatWaterRegionSnapshot& LegacyWaterRegionSnapshot,
    FCatPreparedFishingSessionToken& OutToken);
bool StartPreparedSessionLogic(FCatPreparedFishingSessionToken Token);
void PublishPreparedSession(FCatPreparedFishingSessionToken Token);
void AbortPreparedSession(FCatPreparedFishingSessionToken Token);
bool IsBeginCastPublicationBlocked() const;

bool PrepareFishSelectionFromAuthority(const FCatFishSelectionResult& Selection,
    const FCatChumSample& FrozenChumSample, UCatFishDefinition* Definition,
    ACatFishEncounterActor* PreparedFishActor, FGuid& OutCommitToken);
void AbortPreparedFishSelection(FGuid CommitToken);
bool ActivatePreparedFishSelectionDeferred(FGuid CommitToken); // valid token path cannot fail
void PublishPreparedFishSelection(FGuid CommitToken);

FCatFishingUseOperationResult UCatEquipmentComponent::CommitFishingBaitDeferred(
    FGuid FishingSessionId);
void UCatEquipmentComponent::PublishDeferredFishingBait(FGuid FishingSessionId);

bool ACatFishingHookActor::DeferPresentationPublicationFromAuthority();
void ACatFishingHookActor::PublishDeferredPresentationFromAuthority();
bool ACatFishEncounterActor::DeferPresentationPublicationFromAuthority();
void ACatFishEncounterActor::PublishDeferredPresentationFromAuthority();
```

`CommitFishingBait()` 保留为“deferred commit + publish”的兼容封装；deferred 版本扣除冻结的特殊饵并推进一次 Equipment Revision，但不 `OnSnapshotChanged`/`ForceNetUpdate`，publish 恰好一次。Hook/Fish Actor 在 `SpawnActorDeferred` 后、`FinishSpawning` 前开启表现屏障；屏障期间 identity 写入只更新私有 replicated state，既不 `ForceNetUpdate` 也不调用 Blueprint event，`BeginPlay` 不能穿透屏障，publish 后才一次派发已经合并的首次状态并请求网络更新。Session barrier 期间 `PublishSnapshot` 仍按 mutation 规则推进内存版本，但合并通知并抑制 `ForceNetUpdate`；`PublishPreparedSession` 只发一个本地 signal/网络更新，不再次推进版本。

Barrier 期间只允许 StateTree 的 phase-intent 更新 Session 内存；Fight exchange、failure budget、Items capture、Equipment commit/release、Collection/Profile 写入、Social prompt、Scoop 和任何终态清理副作用都必须在写前拒绝。若启动树试图进入终态或触发上述写口，`StartPreparedSessionLogic` 返回 false，Service 走无发布 Abort；不能靠“通常第一帧不会调用”维持原子性。

StateTree 增加 `FCatFishingResolveTrueBiteSelectionTask`，只调用 Session/Service 的上述有界入口；不自带鱼种、Chum 或世界坐标参数。`InProgress` 映射为 Task `Running`，`Selected` 映射为 `Succeeded`，`TerminalNoEligibleFish` 表示 Session 已同步进入既有失败终态，`Failed` 只表示依赖/契约错误并走明确失败分支；禁止用 `Selection.bSelected=false` 同时冒充这三种语义。若 Session 的 BeginCast publication barrier 尚未打开，Task 保持 `Running` 且无副作用，不能提前选择鱼或把启动失败写成业务终态。

完整 `FCatFrozenFishSelectionFacts`（ChumSample/Bait/Environment/seed/selector weight）只保存在 authority Session 私有状态，不放进 replicated public Snapshot；公开 Attempt 在选择后只发布 FishDefinitionId、Fish Actor 引用及既有必要读模型，不能泄露 contributing FieldIds 或三轴原值。

- [ ] **Step 1: 写 BeginCast 与 delayed-selection RED 测试**

- `Catfishing.Unit.Fishing.Contracts.BeginCastCarriesExactWaterHandleAndCandidatePoint`
- `Catfishing.Unit.Fishing.CommandComponent.BeginCastResultIsOwnerScopedFirstWinsAndBounded`
- `Catfishing.Unit.Fishing.Service.BeginCastUsesServerCorrectedLandingNotCharacterFeet`
- `Catfishing.Unit.Fishing.Service.BeginCastRejectsStaleHandleRodRevisionRangeAngleAndLOS`
- `Catfishing.Unit.Fishing.Service.BeginCastInjectsMatchingLegacyWaterSnapshotUntilNearShoreCutover`
- `Catfishing.Unit.Fishing.Service.BeginCastReservesEquipmentBeforePublishingSession`
- `Catfishing.Unit.Fishing.Service.BeginCastStartupReentrySeesPendingIdentityAndCreatesOnce`
- `Catfishing.Unit.Fishing.Service.BeginCastFailurePublishesNoSessionHookOrReservation`
- `Catfishing.Unit.Fishing.Session.InitializeDoesNotSelectOrSpawnFish`
- `Catfishing.Unit.Fishing.Session.PreparedStartupSuppressesSnapshotUntilTerminalFlush`
- `Catfishing.Unit.Fishing.Session.PreparedStartupRejectsAllCrossDomainSideEffects`
- `Catfishing.Unit.Fishing.Actors.AttemptReplicatesExactWaterHandleWithoutLegacyFields`
- `Catfishing.Unit.Fishing.Actors.DeferredHookAndFishPresentationCannotEscapeBeforePublish`
- `Catfishing.Unit.Fishing.Selection.TrueBiteSamplesHookPointAndFreezesChumBaitEnvironmentSeed`
- `Catfishing.Unit.Fishing.Selection.ResolutionStatusSeparatesInProgressNoFishAndFailure`
- `Catfishing.Unit.Fishing.Selection.NoEligibleFishFinalizesTerminatedEmptyHookWithoutEventDelivery`
- `Catfishing.Unit.Fishing.Selection.FieldChangesAfterSelectionDoNotRerollFish`
- `Catfishing.Unit.Fishing.Selection.SpecialBaitCommitsOnceBeforeFishPublication`
- `Catfishing.Unit.Fishing.Selection.SynchronousEquipmentActorAndSessionReentrySeesFrozenSelection`
- `Catfishing.Unit.Fishing.Selection.BaitFailureAbortsPendingFishWithoutRerollingContext`
- `Catfishing.Unit.Equipment.FishingUse.DeferredBaitCommitIsInvisibleUntilIdempotentPublish`
- `Catfishing.Unit.Fishing.StateTreeNodes.TrueBiteSelectionExposesNoDataOverrides`

当前仓库没有可运行的 `ST_FishingSession` 资产，且本计划禁止伪造 runtime fallback。因此本任务的真实 `BeginCast` public entry 只做 fail-closed 与清理路径测试；成功事务由只授予测试类的 private friend 分别驱动真实 Prepare/Preparing-registry/Finalize/publish helper，不能把它标成完整 StateTree 端到端。不得增加“跳过 StateTree”的 shipping/test flag、UFUNCTION 或 Blueprint 后门。Task 16 Runbook 和 Phase B 必须在用户创建正式 StateTree 后补一条真实 public BeginCast→StateTree 网络验收，才可以称为可玩抛竿。

- [ ] **Step 2: 构建并确认 RED**

Expected: tests fail on missing Handle field/new service signatures; old Character-foot test must be removed only after new RED exists.

- [ ] **Step 3: 实现 BeginCast authority sequence**

1. Validate controller gate/identity and frozen terminal replay；第一次合法请求立即把 `(StableNetId, RequestId)` 放入 private in-progress set，后续同步重入只返回非终态 `RequestInProgress`，不进入 CommandComponent mailbox，也不执行副作用。
2. Resolve registered Rod by PlayerState; exact RodActorId/Revision/EquipmentRevision。
3. Resolve candidate against expected Water Handle; validate range/angle/LOS from canonical RodTip。
4. Generate `FishingSessionId` and `CastAttemptId` on server。
5. `BeginFishingUse(SessionId, frozen Rod/Bait/Float IDs, ExpectedEquipmentRevision)`；该 reservation 不发布 Snapshot。
6. 用已验证的 server-corrected landing 调 Task 5 的临时 `QueryWaterRegion` wrapper 取得与 exact Handle 一致的 `FCatWaterRegionSnapshot`，只作为 Task 12 前旧 NearShore/capture 条件的兼容输入；Handle、RegionId、GeometryRevision 任一不一致都在 spawn 前 fail-closed。
7. 用 `SpawnActorDeferred` 创建 Hook，先开启 actor presentation barrier，再初始化 identity 并 `FinishSpawning`；Finish 后重新验证 Actor 未 PendingKill、identity/transform 未被 Blueprint construction 改写。随后创建 Session 并调用 `PrepareSessionFromAuthority`，此时 Session publication barrier 已开启。
8. 把 Session/钓手 identity 以 private `Preparing` 状态预登记到 Service 三个索引，保留单活跃槽位；public `FindSession`/`TryGetActiveSessionForController` 继续排除 Preparing，但 Session StateTree 的内部入口可以解析自身。
9. `StartPreparedSessionLogic(Token)`；启动栈内的 Snapshot 写入只合并到 deferred snapshot，外部命令 fail-closed，新增 true-bite Task 在 barrier 打开前保持 Running。失败时按 Session→Hook→Equipment 的逆序 Abort/Destroy/Release，精确撤销 Preparing 索引，再冻结失败 terminal；整个失败路径没有 delegate、Actor Blueprint event 或网络 publication。
10. 成功时先构造并 Store BeginCast terminal，把 registry 状态原子改为 Active 并清 in-progress；随后 `PublishDeferredPresentationFromAuthority()`、`PublishPreparedSession(Token)`，最后返回结果。此后的同步回调/重放只能看到同一成功 terminal 和已登记 Session。

`UCatFishingCommandComponent::SubmitBeginCast` owns the Server RPC、32-entry first-result-wins owner mailbox and result delivery；收到专用结果后同步生成通用 `FCatFishingCommandResult{CommandType=BeginCast}`。`ECatFishingCommandError` 新增 `RequestInProgress`，但它只允许 Service 内同步重入诊断返回，CommandComponent 不缓存/广播它。Service 的 terminal 仍以 `(StableNetId, RequestId)` 首次终态冻结；Preparing 索引不是第二份 Session 真相。Remove legacy `ServerStartFishingSession(FGuid)`、`UCatFishingService::StartFishingSession(AController*, FGuid)`、`FCatFishingStartResult` 和 Task 10 暂留的 raw-parameter Fish selector overload；do not bind an input action in this task。`LegacyWaterRegionSnapshot` 只允许保存在 Session private compatibility state，不复制、不得成为新逻辑真相，并由 Task 12 连同初始化参数一起删除。鱼尚未冻结时，Fight/Failure/Scoop 等依赖鱼定义的 Session API 必须返回 DependencyUnavailable，不得读取 null definition。

所有失败、Cancel、`CloseCommandsAndTerminateAll` 与 `Deinitialize` 都必须精确清理 in-progress key 和 Preparing registry；只清匹配的 SessionId/StableNetId，迟到 Abort 不能删除已替代的 active entry。

- [ ] **Step 4: 实现 true-bite selection transaction**

读取 Session Hook Actor 当前服务器 Transform，采样 Chum，读取 GameState `FCatRunPublicState.Environment`，构造 SelectionContext。Service 第一次调用即把 Session/PhaseEpoch 标为 selection-in-progress 并冻结 context/seed/selection result；同步重入只返回 in-progress，无副作用。无合格鱼时先缓存失败选择事实，再由 Session 在同一调用栈直接 `FinalizeSession(Phase=Terminated, Outcome=EmptyHook)`；`Cat.Fishing.Event.FishSelectionFailed` 只作 StateTree/表现观察通知，终态不依赖事件送达。`TerminalNoEligibleFish` 的唯一可重放结果必须对应该精确 Phase/Outcome，不在同一 Attempt 重抽。

选择成功时用 `SpawnActorDeferred` 创建 FishEncounter，先开启 presentation barrier、初始化 identity、再 `FinishSpawning`；BeginPlay 仍不得派发显式 BP presentation event。Finish 后重新验证 Actor 存活、identity 与服务器 Transform 未被 construction 改写，任何异常都发生在扣饵前。Session 校验全部 identity/phase/definition/actor 后创建不可见 pending selection token。调用 `CommitFishingBaitDeferred`；失败则 Abort token、destroy prepared Fish、保持未选择。成功后 `ActivatePreparedFishSelectionDeferred` 只做不可失败的同步状态切换，把冻结事实、Fish Actor 引用和首次 selection terminal 一起写入 Session private state但不广播。只有 terminal 已可重放后，才按 Equipment Snapshot → Fish actor presentation → Session Snapshot 的顺序调用三个 publish；来自任一同步回调的重入必须返回同一冻结 selection，不能再扣饵、spawn 或激活。普通 bait 也走同一顺序，只是 Equipment publish 为幂等 no-op。

FishingSettings 增 `HookActorClass`、`FishEncounterActorClass`、`MaximumCastRangeCentimeters`、`MaximumCastAimDeviationDegrees`、`CastLineOfSightChannel`；全部 Unset 时 fail-closed。Blueprint subclasses only supply meshes/skins/animations through existing actor hooks。

- [ ] **Step 5: 验证、审查并提交**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Fishing' -RunName 'phase-a1-begin-cast-selection'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Equipment.FishingUse' -RunName 'phase-a1-begin-cast-bait-publication'
git diff -- Source/Catfishing/Framework/Game/CatGameplayTypes.h Source/Catfishing/Framework/Game/CatGameplayTypes.cpp
git add -- Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.h Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.cpp Source/Catfishing/Fishing/Integration/CatFishingCommandComponent.h Source/Catfishing/Fishing/Integration/CatFishingCommandComponent.cpp Source/Catfishing/Fishing/CatFishingTypes.h Source/Catfishing/Fishing/CatFishingService.h Source/Catfishing/Fishing/CatFishingService.cpp Source/Catfishing/Fishing/CatFishingSession.h Source/Catfishing/Fishing/CatFishingSession.cpp Source/Catfishing/Fishing/CatFishingSettings.h Source/Catfishing/Fishing/CatFishingSettings.cpp Source/Catfishing/Fishing/CatFishingStateTreeNodes.h Source/Catfishing/Fishing/CatFishingStateTreeNodes.cpp Source/Catfishing/Fishing/Actors/CatFishingHookActor.h Source/Catfishing/Fishing/Actors/CatFishingHookActor.cpp Source/Catfishing/Fishing/Actors/CatFishEncounterActor.h Source/Catfishing/Fishing/Actors/CatFishEncounterActor.cpp Source/Catfishing/Equipment/CatEquipmentComponent.h Source/Catfishing/Equipment/CatEquipmentComponent.cpp Source/Catfishing/Data/CatFishCatalogSettings.h Source/Catfishing/Data/CatFishCatalogSettings.cpp Source/Catfishing/Framework/Game/CatGameplayTypes.h Source/Catfishing/Framework/Game/CatGameplayTypes.cpp Source/Catfishing/Fishing/Tests/CatFishingContractTests.cpp Source/Catfishing/Fishing/Tests/CatFishingActorContractTests.cpp Source/Catfishing/Fishing/Tests/CatFishingCommandComponentTests.cpp Source/Catfishing/Fishing/Tests/CatFishingSettingsTests.cpp Source/Catfishing/Fishing/Tests/CatFishingServiceTests.cpp Source/Catfishing/Fishing/Tests/CatFishingSessionTests.cpp Source/Catfishing/Fishing/Tests/CatFishingStateTreeNodesTests.cpp Source/Catfishing/Equipment/Tests/CatEquipmentFishingUseTests.cpp
git diff --cached --check
git commit -m "Start fishing from authoritative water landings"
```

---

### Task 12: Fish Transform NearShore、猫岸外 Scoop 与 StateTree 去坐标化

**Files:**

- Modify: `Source/Catfishing/Fishing/CatFishingSettings.h`
- Modify: `Source/Catfishing/Fishing/CatFishingSettings.cpp`
- Modify: `Source/Catfishing/Fishing/CatFishingSession.h`
- Modify: `Source/Catfishing/Fishing/CatFishingSession.cpp`
- Modify: `Source/Catfishing/Fishing/CatFishingService.cpp`
- Modify: `Source/Catfishing/Fishing/CatFishingStateTreeNodes.h`
- Modify: `Source/Catfishing/Fishing/CatFishingStateTreeNodes.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingSettingsTests.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingServiceTests.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingSessionTests.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingStateTreeNodesTests.cpp`

**Interfaces:**

- Consumes: Session exact Water Handle、FishEncounter server transform、QuerySubsystem、Character capsule/ground、reach/LOS settings。
- Produces:

```cpp
FCatFishingPhaseResult EnterPhaseFromStateTree(ECatFishingPhase NewPhase);

ECatDomainCommandError ValidateFishNearShore(
    FCatWaterSpatialResult& OutFishRelation,
    bool& bOutShouldInvalidateSession) const;

ECatDomainCommandError ValidateScoopGeometry(
    const ACatCharacter* ScoopingCharacter,
    FCatWaterSpatialResult& OutFishRelation,
    FCatWaterSpatialResult& OutScooperRelation,
    bool& bOutShouldInvalidateSession) const;
```

Fishing settings exact fields:

```cpp
double NearShoreWidthCentimeters = 0.0;
double BankInteractionWidthCentimeters = 0.0;
double ScoopReachCentimeters = 0.0;
double ShoreGroundTraceHalfHeightCentimeters = 0.0;
double MaximumBankSlopeDegrees = 0.0;
TEnumAsByte<ECollisionChannel> ShoreGroundTraceChannel = ECC_Visibility;
TEnumAsByte<ECollisionChannel> ScoopLineOfSightChannel = ECC_Visibility;
```

删除旧 `bEnableNearShoreValidation`；上述正阈值、合法 trace channel 与总 Fishing runtime gate 共同构成 fail-closed readiness。

- [ ] **Step 1: 写岸距与绕过防线 RED 测试**

- `Catfishing.Unit.Fishing.StateTreeNodes.EnterPhaseExposesOnlyPhaseIntent`
- `Catfishing.Unit.Fishing.Session.NearShoreReadsFishTransformAndExactWaterHandle`
- `Catfishing.Unit.Fishing.Session.NearShoreCutoverRemovesLegacyWaterSnapshotState`
- `Catfishing.Unit.Fishing.Session.FarInsideFishDoesNotEnterNearShore`
- `Catfishing.Unit.Fishing.Session.MissingFishStaleGeometryOrCrossRegionInvalidates`
- `Catfishing.Unit.Fishing.Session.ScoopRequiresFishInsideAndCatOutsideMatchingShoreBands`
- `Catfishing.Unit.Fishing.Session.ScoopRequeriesMovedFishAndNeverTrustsClientWorldPoint`
- `Catfishing.Unit.Fishing.Session.ScoopRequiresReachGroundSlopeAndLineOfSight`
- `Catfishing.Unit.Fishing.Session.IslandBankDirectionSupportsValidOutsideCat`

- [ ] **Step 2: 构建并确认 RED**

Expected: tests fail because EnterPhase still exposes bool/FVector and Session still caches AABB target。

- [ ] **Step 3: 删除 StateTree 世界坐标和 Session target cache**

`FCatFishingEnterPhaseTaskInstanceData` 只剩 `ECatFishingPhase Phase`。删除 `AuthoritativeNearShoreTarget`、`bHasAuthoritativeNearShoreTarget`、`NearShoreTargetWorldLocation`、`bHasNearShoreTarget`。同时从 `PrepareSessionFromAuthority` 删除 Task 11 的临时 `FCatWaterRegionSnapshot` 参数与 Session private compatibility state；`UCatFishingService::BeginCast` 调用改为只传 Attempt/Hook/钓手事实，不再请求或注入 legacy snapshot；Task 11 的兼容 Service 测试替换为 `NearShoreCutoverNoLongerQueriesLegacyWaterSnapshot`。capture condition 的 RegionId 改读 `Attempt.WaterRegion.RegionId`。进入 NearShore 时每次读取 Fish Actor location，要求 `Inside && 0 < SignedDistance <= NearShoreWidth`。

合法水内但离岸太远只拒绝 Phase；Fish actor 丢失、Handle 过期、Region 不存在或鱼进入另一 Region 直接 `TerminateSession(Invalidated)`。

- [ ] **Step 4: 实现 Scoop 全量服务器复核**

Items 提交前重新读取 Fish transform；猫用 `QueryShoreRelation` 要求 exact Handle、`-BankWidth <= signed < 0`。随后验证 Character-to-Fish Reach、从 capsule 周围向下的 ground trace、`ImpactNormal.Z >= cos(MaxSlope)`，以及 Pawn view location 到 Fish 的 LOS。`Command.ScoopWorldLocation` 仅写诊断日志，不能替换任一服务器点。

- [ ] **Step 5: 验证并提交**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Fishing.Session' -RunName 'phase-a1-near-shore-session'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Fishing.StateTreeNodes' -RunName 'phase-a1-near-shore-statetree'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Fishing.Settings' -RunName 'phase-a1-near-shore-settings'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Fishing.Service' -RunName 'phase-a1-near-shore-service'
git add -- Source/Catfishing/Fishing/CatFishingSettings.h Source/Catfishing/Fishing/CatFishingSettings.cpp Source/Catfishing/Fishing/CatFishingSession.h Source/Catfishing/Fishing/CatFishingSession.cpp Source/Catfishing/Fishing/CatFishingService.cpp Source/Catfishing/Fishing/CatFishingStateTreeNodes.h Source/Catfishing/Fishing/CatFishingStateTreeNodes.cpp Source/Catfishing/Fishing/Tests/CatFishingSettingsTests.cpp Source/Catfishing/Fishing/Tests/CatFishingServiceTests.cpp Source/Catfishing/Fishing/Tests/CatFishingSessionTests.cpp Source/Catfishing/Fishing/Tests/CatFishingStateTreeNodesTests.cpp
git diff --cached --check
git commit -m "Validate near-shore fishing from actor geometry"
```

---

### Task 13: GameState FastArray 与 Blueprint 表现入口

**Files:**

- Create: `Source/Catfishing/Environment/CatChumFieldReplicationComponent.h`
- Create: `Source/Catfishing/Environment/CatChumFieldReplicationComponent.cpp`
- Create: `Source/Catfishing/Environment/Presentation/CatChumFieldPresentationActor.h`
- Create: `Source/Catfishing/Environment/Presentation/CatChumFieldPresentationActor.cpp`
- Create: `Source/Catfishing/Environment/Presentation/CatChumFieldPresentationSubsystem.h`
- Create: `Source/Catfishing/Environment/Presentation/CatChumFieldPresentationSubsystem.cpp`
- Modify: `Source/Catfishing/Environment/CatChumFieldTypes.h`
- Modify: `Source/Catfishing/Environment/CatChumFieldTypes.cpp`
- Modify: `Source/Catfishing/Equipment/CatEquipmentDefinition.cpp`
- Modify: `Source/Catfishing/Equipment/Tests/CatEquipmentDefinitionTests.cpp`
- Modify: `Source/Catfishing/Environment/CatChumFieldSubsystem.h`
- Modify: `Source/Catfishing/Environment/CatChumFieldSubsystem.cpp`
- Modify narrowly: `Source/Catfishing/Framework/Game/CatGameplayTypes.h`
- Modify narrowly: `Source/Catfishing/Framework/Game/CatGameplayTypes.cpp`
- Create: `Source/Catfishing/Environment/Tests/CatChumFieldReplicationTests.cpp`
- Create: `Source/Catfishing/Environment/Tests/CatChumFieldPresentationTests.cpp`
- Modify: `Source/Catfishing/Framework/Game/Tests/CatGameplayTypesTests.cpp`

**Interfaces:**

- Consumes: Active Field state and formal `ACatfishingGameState`。
- Produces:

```cpp
USTRUCT(BlueprintType)
struct FCatChumFieldPublicItem : public FFastArraySerializerItem
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FGuid FieldId;
    UPROPERTY(BlueprintReadOnly) FName ChumDefinitionId;
    UPROPERTY(BlueprintReadOnly) FName PresentationId;
    UPROPERTY(BlueprintReadOnly) FVector Center = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) double RadiusCentimeters = 0.0;
    UPROPERTY(BlueprintReadOnly) double StartServerTime = 0.0;
    UPROPERTY(BlueprintReadOnly) double ExpireServerTime = 0.0;
    UPROPERTY(BlueprintReadOnly) uint8 PublicIntensityTier = 0;
};

USTRUCT()
struct FCatChumFieldPublicArray : public FFastArraySerializer
{
    GENERATED_BODY()
    UPROPERTY() TArray<FCatChumFieldPublicItem> Items;
    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams);
};

template<>
struct TStructOpsTypeTraits<FCatChumFieldPublicArray>
    : TStructOpsTypeTraitsBase2<FCatChumFieldPublicArray>
{
    enum { WithNetDeltaSerializer = true };
};

enum class ECatChumFieldPublicChange : uint8 { Added, Changed, Removed };
DECLARE_MULTICAST_DELEGATE_TwoParams(
    FCatChumFieldPublicChanged, ECatChumFieldPublicChange, const FCatChumFieldPublicItem&);

UCLASS(ClassGroup=(Catfishing))
class CATFISHING_API UCatChumFieldReplicationComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    UCatChumFieldReplicationComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    void ApplyFieldFromAuthority(const FCatChumFieldPublicItem& Item);
    void RemoveFieldFromAuthority(FGuid FieldId);
    const TArray<FCatChumFieldPublicItem>& GetPublicFields() const;
    FCatChumFieldPublicChanged OnPublicFieldChanged;
};

UCLASS()
class CATFISHING_API UCatChumFieldPresentationSubsystem final : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    void SetLocalInfluencePreviewVisible(bool bVisible);
};
```

GameState 增加 native default subobject、constructor、BlueprintPure getter。`Apply/Remove` authority 本地广播一次并 ForceNetUpdate；FastArray add/change/remove callback 在客户端广播一次。

Replication component 以 `UPROPERTY(Replicated) FCatChumFieldPublicArray PublicFields` 保存镜像，`GetLifetimeReplicatedProps` 显式注册；每次 add/change 后 `MarkItemDirty`，remove 后 `MarkArrayDirty`。Array 保存非序列化 owner-component back pointer，严格实现 UE 5.8 的 `PostReplicatedAdd`、`PostReplicatedChange` 与 `PreReplicatedRemove`：删除通知必须在 item 从数组移除前复制并广播其 public DTO；不存在也不得臆造 `PostReplicatedRemove`。back pointer 不写入网络。

`ACatChumFieldPresentationActor` 暴露：

```cpp
UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic)
void BP_ApplyChumFieldPresentation(const FCatChumFieldPublicItem& Item);
UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic)
void BP_SetInfluenceRadiusVisible(bool bVisible);
UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic)
void BP_PlayFieldActivated();
UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic)
void BP_PlayFieldExpired();
```

本任务创建原生父类后，把 Task 6 的 `TSoftClassPtr<AActor> PresentationClass` 收窄为 `TSoftClassPtr<ACatChumFieldPresentationActor>`，并让 Chum Definition readiness/Data Validation 拒绝无法解析或父类错误的非空路径；这是资产类型约束，不给 Blueprint 任何 Field 写权限。

- [ ] **Step 1: 写复制与表现 RED 测试**

- `Catfishing.Unit.Environment.ChumReplication.FastArrayPublishesOnlyPublicFields`
- `Catfishing.Unit.Environment.ChumReplication.AuthorityAndRepCallbacksNotifyExactlyOnce`
- `Catfishing.Unit.Environment.ChumReplication.AddChangeRemoveAreIdempotent`
- `Catfishing.Unit.Environment.ChumReplication.AuthorityCleanupTimerRemovesExpiredPublicItemOnce`
- `Catfishing.Unit.Environment.ChumPresentation.BlueprintBaseIsCosmeticCollisionFreeAndNonAuthoritative`
- `Catfishing.Unit.Environment.ChumPresentation.PreviewVisibilityDefaultsOff`
- `Catfishing.Unit.Equipment.Definition.ChumPresentationClassMustUseNativeCosmeticBase`
- `Catfishing.Unit.Framework.GameState.OwnsNativeChumReplicationComponent`

- [ ] **Step 2: 构建并确认 RED**

Expected: missing replication/presentation types and GameState getter.

- [ ] **Step 3: 实现 FastArray 和 Subsystem 发布**

Replication component 在 authority GameState BeginPlay 订阅 Field Subsystem 的 `OnFieldActivated/OnFieldRemoved`，随后通过仅授予该 component 的 friend helper 复制当前 Active states 并做幂等 reconcile；EndPlay 精确解绑。这样 seamless GameState 重建也不会漏掉既有 Field。只有 Task 9 已落 terminal 后触发的 activated delegate 才调用 `ApplyFieldFromAuthority`；Abort/Pending 永不公开。过期在采样上立即归零；authority Timer 的 Cleanup 真正删除记录时由 removed delegate 调 `RemoveFieldFromAuthority`。DTO 不含 OwnerStableNetId、EquipmentRevision、错误、预算或三轴原值。`ChumDefinitionId` 只用于客户端从已配置 Equipment catalog 精确解析表现类，不能用于客户端重建 Influence。`PublicIntensityTier` 在激活时按 Field 的 L1 基础贡献占 `MaxRawContributionPerRegion` 的比例量化为 1/2/3（`<=1/3`、`<=2/3`、其余），不随时间曲线 Tick 更新。

- [ ] **Step 4: 实现本地表现 Subsystem**

Subsystem 订阅当前 GameState component，按 FieldId 维护本地 `TWeakObjectPtr<ACatChumFieldPresentationActor>`。它用 `ChumDefinitionId` 从 Equipment catalog 找到唯一 ready definition，校验其 `ChumInfluence.PresentationId` 与 DTO 一致，再加载 optional `PresentationClass`；定义歧义、ID 不一致或缺类时只跳过表现，不改 DTO/Field。`SetLocalInfluencePreviewVisible(bool)` 统一切换圆形范围，默认 false；Phase B 的 Aim/Chum Ability 只调用此本地入口。

- [ ] **Step 5: 验证、审查窄 diff 并提交**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Environment.ChumReplication' -RunName 'phase-a1-chum-replication'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Environment.ChumPresentation' -RunName 'phase-a1-chum-presentation'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Equipment.Definition' -RunName 'phase-a1-chum-presentation-definition'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Framework' -RunName 'phase-a1-chum-game-state-framework'
git add -- Source/Catfishing/Environment/CatChumFieldReplicationComponent.h Source/Catfishing/Environment/CatChumFieldReplicationComponent.cpp Source/Catfishing/Environment/Presentation/CatChumFieldPresentationActor.h Source/Catfishing/Environment/Presentation/CatChumFieldPresentationActor.cpp Source/Catfishing/Environment/Presentation/CatChumFieldPresentationSubsystem.h Source/Catfishing/Environment/Presentation/CatChumFieldPresentationSubsystem.cpp Source/Catfishing/Environment/CatChumFieldTypes.h Source/Catfishing/Environment/CatChumFieldTypes.cpp Source/Catfishing/Environment/CatChumFieldSubsystem.h Source/Catfishing/Environment/CatChumFieldSubsystem.cpp Source/Catfishing/Equipment/CatEquipmentDefinition.cpp Source/Catfishing/Equipment/Tests/CatEquipmentDefinitionTests.cpp Source/Catfishing/Framework/Game/CatGameplayTypes.h Source/Catfishing/Framework/Game/CatGameplayTypes.cpp Source/Catfishing/Environment/Tests/CatChumFieldReplicationTests.cpp Source/Catfishing/Environment/Tests/CatChumFieldPresentationTests.cpp Source/Catfishing/Framework/Game/Tests/CatGameplayTypesTests.cpp
git diff --cached --check
git commit -m "Replicate chum presentation fields through game state"
```

---

### Task 14: 自然空间 Field 与旧 AABB/共享 ChumPool 完整删除

**Files:**

- Modify: `Source/Catfishing/Environment/CatEnvironmentSettings.h`
- Modify: `Source/Catfishing/Environment/CatEnvironmentSettings.cpp`
- Modify: `Source/Catfishing/Environment/CatChumFieldAnchor.h`
- Modify: `Source/Catfishing/Environment/CatChumFieldAnchor.cpp`
- Modify: `Source/Catfishing/Environment/CatChumFieldSubsystem.h`
- Modify: `Source/Catfishing/Environment/CatChumFieldSubsystem.cpp`
- Modify: `Source/Catfishing/Environment/CatWaterTypes.h`
- Modify: `Source/Catfishing/Environment/CatWaterRegion.h`
- Modify: `Source/Catfishing/Environment/CatWaterRegion.cpp`
- Modify: `Source/Catfishing/Environment/CatWaterQuerySubsystem.h`
- Modify: `Source/Catfishing/Environment/CatWaterQuerySubsystem.cpp`
- Modify narrowly: `Source/Catfishing/Framework/Game/CatGameplayTypes.h`
- Modify narrowly: `Source/Catfishing/Framework/Game/CatGameplayTypes.cpp`
- Modify: `Source/Catfishing/Environment/Tests/CatEnvironmentSettingsTests.cpp`
- Modify: `Source/Catfishing/Environment/Tests/CatWaterRegionTests.cpp`
- Modify: `Source/Catfishing/Environment/Tests/CatWaterQuerySubsystemTests.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingContractTests.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingServiceTests.cpp`
- Modify: `Source/Catfishing/Fishing/Tests/CatFishingSessionTests.cpp`
- Modify: `Source/Catfishing/Framework/Game/Tests/CatGameplayTypesTests.cpp`

**Interfaces:**

- Consumes: registered Anchor、ActiveEventId、Run identity、same Field prepare/activate path。
- Produces:

```cpp
bool UCatEnvironmentSettings::TryGetNaturalChumField(
    FName& OutAnchorId, FName& OutChumDefinitionId) const;

FCatPlaceChumResult UCatChumFieldSubsystem::CreateNaturalField(
    const FString& StableEventIdentity, FGuid RequestId,
    const FCatWaterRegionHandle& ExpectedHandle,
    const FVector& CandidateCenter,
    FName ChumDefinitionId,
    const FCatChumInfluenceSpec& Influence,
    double ServerTime);
```

Settings fields为 `NaturalChumAnchorId` 和 `NaturalChumDefinitionId`。GameMode 必须通过 `UCatEquipmentSettings::FindRuntimeDefinition` 解析唯一 ready Chum definition，并把其 DefinitionId 与 Influence 一起传给 Field Subsystem；自然事件不复制第二份 Influence 配置。Anchor 在 BeginPlay/EndPlay 向 Field Subsystem 精确注册/注销；重复 AnchorId fail-closed。

- [ ] **Step 1: 在删除前扫描序列化引用并保存证据**

```powershell
rg -a -n 'bEnablePrototypeBounds|LocalCenterOffset|HalfExtent|AggregationRevision|ChumPool|ServerContributeChum|FCatAggregation|FCatWaterRegionSnapshot|NaturalAggregation' Content Config
```

Expected: 无正式资产/Config 命中。若出现命中，停止删除并列出精确资产路径；不得静默让旧序列化字段丢失。

- [ ] **Step 2: 写 natural-field 与 no-legacy RED 测试**

- `Catfishing.Unit.Environment.Settings.NaturalChumRequiresAnchorAndReadyChumDefinition`
- `Catfishing.Unit.Environment.ChumField.NaturalAndPlayerSourcesUseSameGeometryAndBudgetRules`
- `Catfishing.Unit.Environment.ChumField.NaturalSourcePublishesResolvablePresentationDefinition`
- `Catfishing.Unit.Framework.Run.NaturalChumStableEventSpawnsExactlyOneField`
- `Catfishing.Unit.Framework.Run.NaturalChumRetryReusesRequestId`
- `Catfishing.Unit.Environment.Water.Contracts.NoAabbOrSharedAggregationFieldsRemain`

- [ ] **Step 3: 实现自然 Field**

GameMode 对 `(RunId, DayIndex, ActiveEventId, AnchorId)` 生成稳定字符串并调用 `FGuid::NewDeterministicGuid(Key, 0x4341544348554D31ull)` 构造固定 RequestId；失败重试复用它。解析唯一 Anchor 和唯一 ready Chum Definition，按 exact Handle 调 Water query，再调用 `CreateNaturalField(Source=NaturalEvent, DefinitionId, Definition.ChumInfluence)`；不经过 Equipment。CreateNaturalField 仍遵守 Prepare → deferred activate → terminal ledger → publish delegate 的顺序，Field 回调重入只能读到首次终态。缺 Anchor、定义缺失/歧义、过期 Handle 或非法 Influence 都拒绝，不退回整 Region。

- [ ] **Step 4: 删除所有 legacy 类型/字段/方法**

本提交删除：

- `FCatAggregationCommand/Result`、`ECatAggregationSource`。
- `FCatWaterRegionSnapshot`、`FCatWaterQuery`、`FCatWaterQueryResult`。
- `AggregationRevision`、`AggregationBudget`、`ChumPool`、aggregation cache/validate/contribute。
- `bEnablePrototypeBounds`、`LocalCenterOffset`、`HalfExtent` 和 AABB fallback。
- `ContainsWorldPoint()` 与 legacy `QueryWaterRegion()`。
- `NaturalAggregationRegionId/Contribution`、`SubmittedNaturalAggregationKeys` 和旧 GameMode 写池方法。
- 以上 legacy Water DTO 的最后非 Session 兼容引用；Session 存储和初始化参数已在 Task 12 删除。

- [ ] **Step 5: 源码零命中、构建、全域回归并提交**

```powershell
rg -n 'bEnablePrototypeBounds|LocalCenterOffset|\bHalfExtent\b|AggregationRevision|ChumPool|ChumContribution|FCatAggregation|FCatContributeChum|FCatWaterRegionSnapshot|\bFCatWaterQuery\b|ServerContributeChum|NaturalAggregation|QueryWaterRegion\(|ContainsWorldPoint\(|PreferredSpecialBaitIds|FCatFishingStartResult|StartFishingSession\(' Source
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' Catfishing Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Environment' -RunName 'phase-a1-environment-cutover'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Fishing' -RunName 'phase-a1-fishing-cutover'
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Unit.Framework' -RunName 'phase-a1-framework-cutover'
git add -- Source/Catfishing/Environment/CatEnvironmentSettings.h Source/Catfishing/Environment/CatEnvironmentSettings.cpp Source/Catfishing/Environment/CatChumFieldAnchor.h Source/Catfishing/Environment/CatChumFieldAnchor.cpp Source/Catfishing/Environment/CatChumFieldSubsystem.h Source/Catfishing/Environment/CatChumFieldSubsystem.cpp Source/Catfishing/Environment/CatWaterTypes.h Source/Catfishing/Environment/CatWaterRegion.h Source/Catfishing/Environment/CatWaterRegion.cpp Source/Catfishing/Environment/CatWaterQuerySubsystem.h Source/Catfishing/Environment/CatWaterQuerySubsystem.cpp Source/Catfishing/Framework/Game/CatGameplayTypes.h Source/Catfishing/Framework/Game/CatGameplayTypes.cpp Source/Catfishing/Environment/Tests/CatEnvironmentSettingsTests.cpp Source/Catfishing/Environment/Tests/CatWaterRegionTests.cpp Source/Catfishing/Environment/Tests/CatWaterQuerySubsystemTests.cpp Source/Catfishing/Fishing/Tests/CatFishingContractTests.cpp Source/Catfishing/Fishing/Tests/CatFishingServiceTests.cpp Source/Catfishing/Fishing/Tests/CatFishingSessionTests.cpp Source/Catfishing/Framework/Game/Tests/CatGameplayTypesTests.cpp
git diff --cached --check
git commit -m "Remove shared river chum and AABB water fallbacks"
```

Expected: `rg` returns exit code 1/no matches. Review cached paths before commit；if any unrelated file appears, unstage that exact path only，never restore the worktree。

---

### Task 15: Dedicated/Listen 两客户端 PIE 网络自动化

**Files:**

- Modify: `Catfishing.uproject`
- Create: `Source/CatfishingEditorTests/CatfishingEditorTests.Build.cs`
- Create: `Source/CatfishingEditorTests/CatfishingEditorTests.cpp`
- Create: `Source/CatfishingEditorTests/PhaseA1/CatPhaseA1NetworkTestGameMode.h`
- Create: `Source/CatfishingEditorTests/PhaseA1/CatPhaseA1NetworkTestGameMode.cpp`
- Create: `Source/CatfishingEditorTests/PhaseA1/CatPhaseA1NetworkTests.cpp`

**Interfaces:**

- Consumes: `FStartPIEForAutomationCommand`、`FEndPlayMapCommand`、real `ACatfishingPlayerController` command component、real GameState FastArray。
- Produces: Editor-only `Catfishing.Network.PhaseA1.*` tests；Game target 不加载测试模块。

- [ ] **Step 1: 添加 Editor-only module 声明**

先创建当前不存在的精确目录，已存在时停止并审查而不是覆盖：

```powershell
New-Item -ItemType Directory -Path 'D:\develop\Catfishing\Source\CatfishingEditorTests'
New-Item -ItemType Directory -Path 'D:\develop\Catfishing\Source\CatfishingEditorTests\PhaseA1'
```

`.uproject` 增加：

```json
{
  "Name": "CatfishingEditorTests",
  "Type": "Editor",
  "LoadingPhase": "PostEngineInit"
}
```

Build.cs exact dependencies：

```csharp
PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Catfishing" });
PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "LevelEditor" });
```

- [ ] **Step 2: 写最小 no-op test GameMode 与三个失败的 latent PIE tests**

- `Catfishing.Network.PhaseA1.DedicatedOwnerOnlyPlaceChumAndFieldFastArray`
- `Catfishing.Network.PhaseA1.ListenServerLocalAndRemoteUseSameFieldDto`
- `Catfishing.Network.PhaseA1.NaturalAndPlayerFieldsShareReplicatedPresentationSchema`

先实现只设置 `GameStateClass=ACatfishingGameState`、`PlayerControllerClass=ACatfishingPlayerController` 的 no-op test GameMode；它不投递 Result/DTO，因此测试能编译并产生真实 runtime RED。测试先 `FAutomationEditorCommonUtils::CreateNewMap()`，放两个 `APlayerStart`，用 `FRequestPlaySessionParams.GameModeOverride=ACatPhaseA1NetworkTestGameMode::StaticClass()`。Dedicated 参数：

```cpp
Settings->SetPlayNetMode(PIE_Client);
Settings->SetRunUnderOneProcess(true);
Settings->SetPlayNumberOfClients(2);
```

Listen 参数：`PIE_ListenServer` + 2 clients + one process。每个测试结束必须 `ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand())`。

- [ ] **Step 3: 构建并确认 RED**

Expected: tests run but fail because test GameMode has not yet delivered a result/Field DTO;不能只用 reflection flags 作为 GREEN。

- [ ] **Step 4: 实现最小真实网络 probe**

Test GameMode 使用 `ACatfishingGameState`、`ACatfishingPlayerController`。两个玩家登录后，服务器对第一 Controller 的 CommandComponent 调 `DeliverPlaceChumResultFromAuthority`，并向 GameState replication component Apply 一个固定 Field DTO。Latent verifier 枚举 PIE WorldContexts 并断言：

- owning client 按固定 RequestId 收到完整 PlaceChum Result exactly once。
- non-owning client 的专用与通用邮箱都没有该结果。
- 前两个测试中，两个客户端都只看到一个相同 Field DTO。
- natural schema 测试中，player/natural 两个 DTO 都携带各自可解析的 ChumDefinitionId/PresentationId，远端顺序不改变 FieldId 对应关系。
- Dedicated server 自己没有 client mailbox。
- Listen host 本地结果/Field delegate 各触发一次；remote client 使用相同 DTO。
- Remove 后每个客户端各收到一次 removal，不重复生成表现记录。

- [ ] **Step 5: 验证 Editor test 与 Game target isolation**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing.Network.PhaseA1' -RunName 'phase-a1-network-pie'
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' Catfishing Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
git add -- Catfishing.uproject Source/CatfishingEditorTests/CatfishingEditorTests.Build.cs Source/CatfishingEditorTests/CatfishingEditorTests.cpp Source/CatfishingEditorTests/PhaseA1/CatPhaseA1NetworkTestGameMode.h Source/CatfishingEditorTests/PhaseA1/CatPhaseA1NetworkTestGameMode.cpp Source/CatfishingEditorTests/PhaseA1/CatPhaseA1NetworkTests.cpp
git diff --cached --check
git commit -m "Test fishing field privacy in multiplayer PIE"
```

---

### Task 16: 最终代码门禁、用户编辑器 Runbook 与 Phase B 交接

**Files:**

- Create: `Docs/SplineWaterAndChumEditorRunbook_zh-CN.md`
- Do not modify: `Docs/FishingActorBlueprintHooks_zh-CN.md`
- User-created after code gate: exact assets listed below under `/Game/Catfishing`

**Interfaces:**

- Consumes: 全部 Phase A.1 C++、Automation、network tests 和 Task 0 baseline。
- Produces: 可执行 Editor 操作手册、保护审计和 Phase B 明确输入；不声称 GAS/正式 StateTree 已完成。

- [ ] **Step 1: 编写 Runbook 的精确资产清单**

Runbook 必须要求用户创建：

```text
/Game/Catfishing/Environment/Curves/C_ChumDistance_Test
/Game/Catfishing/Environment/Curves/C_ChumTime_Test
/Game/Catfishing/Data/Curves/C_ChumSaturation_Test
/Game/Catfishing/Data/Fish/DA_Fish_Test
/Game/Catfishing/Data/Equipment/DA_Rod_Test
/Game/Catfishing/Data/Equipment/DA_Bait_Test
/Game/Catfishing/Data/Equipment/DA_Float_Test
/Game/Catfishing/Data/Equipment/DA_Chum_Test
/Game/Catfishing/Presentation/BP_WaterRegionPresentation
/Game/Catfishing/Presentation/BP_ChumFieldPresentation
/Game/Catfishing/Fishing/Actors/BP_FishingRod
/Game/Catfishing/Fishing/Actors/BP_FishingHook
/Game/Catfishing/Fishing/Actors/BP_FishEncounter
```

并明确复用、不重命名：

```text
/Game/Catfishing/Maps/TestMap
/Game/Game/BP_CatFishingGamemode
/Game/Player/BP_CatFishingController
/Game/Character/BP_CatCharacter
```

- [ ] **Step 2: 写 Water authoring 操作**

1. 在 TestMap 放一个 native `ACatWaterRegion`，配置唯一 RegionId、水面 Z、容差和采样值。
2. 放一个或多个 `ACatWaterBoundarySplineActor`；至少一个 Include；岛/禁钓洞用 Exclude。
3. 每条 spline 勾 Closed Loop，设置唯一 BoundaryId/OwningRegion，并加到 Region `BoundaryActors`。
4. 点击 `Bake Geometry`；Data Validation 必须无错误且 GeometryRevision 非零。
5. 明显水位差必须拆成另一个 Region；不得把 Pitch/Roll/Scale 用来拟合坡水面。
6. Presentation BP 只实现 VisualRoot/Niagara/材质与两个 cosmetic event；所有组件 NoCollision/不影响 Nav。

- [ ] **Step 3: 写 Chum/Data 配置操作**

Rod/Bait/Float/Chum 都用现有 `UCatEquipmentDefinition`；Fish 用 `UCatFishDefinition`。`DA_Chum_Test` 必须：Kind=Chum、RunConsumable=true、半径/持续时间/MaximumQuantity 正值、三轴至少一轴正、两条曲线已引用、稳定 PresentationId，并把 PresentationClass 指向 `BP_ChumFieldPresentation`。`DA_Fish_Test` 填 ChumPreference 与 BaitMultiplier；Catalog settings 引用饱和曲线、正 HalfSaturation 和有界 MaxModifier。Rod/Hook/Fish BP 继承 Phase A 原生 Actor，只加 Mesh/材质/AnimBP/动画事件；RodSkinDefinition DataAsset/换肤选择命令尚属 Phase B，本 checkpoint 只验证既有稳定 SkinId 与 `BP_ApplyRodSkin` 接口，不臆造新资产类型。

Runbook 还要列出 Project Settings/`DefaultGame.ini` 的人工装配：把上述 Equipment/Fish 资产加入各自显式 Definitions，填写 ChumFieldSettings 的正预算/射程/角度/LOS/cleanup 并开启其 gate，给 FishingSettings 指定 Hook/Fish Actor class 与正几何阈值。正式 Fishing gate 在 `ST_RunFlow/ST_FishingSession` 尚未创建前继续保持 fail-closed；不得为了演示绕过 StateTree readiness。

若本轮启用自然聚鱼，TestMap 还要放唯一 `ACatChumFieldAnchor`，填写 AnchorId/exact Water Handle，并让 Environment settings 的 `NaturalChumDefinitionId` 指向同一 catalog 中的 ready Chum Definition；不得在 settings 再手填一份 Influence。以后每次重新 Bake 导致 GeometryRevision 变化，都必须把 Anchor 的 Expected Handle 更新为新值，否则自然 Field 按设计拒绝。

- [ ] **Step 4: 运行最终 build、全量 tests 和旧语义扫描**

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' Catfishing Win64 Development 'D:\develop\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReloadFromIDE
& 'D:\develop\Catfishing\Build\Automation\RunCatAutomation.ps1' -Filter 'Catfishing' -RunName 'phase-a1-all'
rg -n 'bEnablePrototypeBounds|LocalCenterOffset|\bHalfExtent\b|AggregationRevision|ChumPool|ChumContribution|FCatAggregation|FCatContributeChum|FCatWaterRegionSnapshot|\bFCatWaterQuery\b|ServerContributeChum|QueryWaterRegion\(|ContainsWorldPoint\(|PreferredSpecialBaitIds|FCatFishingStartResult|StartFishingSession\(|AuthoritativeNearShoreTarget|NearShoreTargetWorldLocation' Source
```

Expected: both builds succeeded；全量 JSON `failed/notRun/inProcess=0`；legacy scan 无命中。Warnings 必须逐条分类，不能把新 gameplay warning 当预期。

- [ ] **Step 5: 比较不可变 baseline**

重新生成当前 protected hashes 与 Content manifest，和 Task 0 run 目录 `Compare-Object`。允许差异只有本计划显式 Source、Build、`.uproject` 与新 Runbook；`Config/DefaultEditor.ini`、原脏 Fishing actor 文档、Task 0 已有 1523 个 Content 文件内容和 stash OID 必须不变。Controller no-index diff 只允许 Tasks 9/11/13 中列出的 Chum/BeginCast/GameState 接缝。

- [ ] **Step 6: 提交 Runbook**

```powershell
git add -- Docs/SplineWaterAndChumEditorRunbook_zh-CN.md
git diff --cached --check
git commit -m "Document spline water and chum authoring workflow"
```

- [ ] **Step 7: 用户完成 Editor checkpoint 后做只读验收**

用户回报 Bake/Data Validation 结果与新资产路径后，执行 Reference Viewer/Asset Audit，确认：

- Region/Boundary 不引用 GFur 或 Demo C++ 类。
- Chum/Water presentation 不含 collision/nav authority。
- Hook/Fish BP 父类是正式 Catfishing native actor。
- 没有编辑或重父级现有 `BP_TestGamemode`。
- 不把本 checkpoint 称为完整可玩钓鱼；正式输入 AbilitySet、GAS、完整 StateTree、Montage 和 HUD 仍是 Phase B 验收项。

---

## Phase A.1 Completion Gate

Phase A.1 只有在以下条件全部成立时才可标记完成：

- Phase A authority Snapshot 本地/远端通知一致。
- 样条烘焙、凸/凹/洞/岸距/Revision/歧义测试通过。
- Runtime 查询不遍历 World、不退回 AABB。
- 每个窝料 Field 独立、圆形、重叠稳定累加，陆地/Exclude 采样为零。
- Quantity 消耗与 Field activation 原子、重放不重复。
- BeginCast 冻结服务器修正落点与 exact Water Handle，不在创建 Session 时选鱼。
- 真咬选择冻结局部 Chum+Bait+Environment+seed，后续 Field 变化不重抽。
- NearShore/Scoop 读取 Fish Actor Transform；StateTree 无世界坐标绕过入口。
- GameState FastArray 只公开表现 DTO；owner-only Placement Result 通过 Dedicated/Listen 两客户端 PIE 门禁。
- 旧 AABB、共享 ChumPool、AggregationRevision、legacy Chum RPC 与静态 NearShore target 在 Source 中零命中。
- Editor/Game builds 和全量 `Catfishing` Automation 通过，保护 baseline 无漂移。

Phase B 的起点是：上述门禁通过、用户完成 Region/Chum/Hook/Fish 资产 checkpoint 后，再实施 GAS Ability/InputConfig、Rod Place/Operate/Pack、Cast/Waiting/Probe/TrueBite/Fight Runner、正式 `ST_FishingSession`、Montage/Cue 和 Fishing HUD。
