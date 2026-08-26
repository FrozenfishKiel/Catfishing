param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Static", "Build", "Automation", "Runtime", "BehaviorEvidence")]
    [string]$Mode
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ProjectFile = Join-Path $ProjectRoot "Catfishing.uproject"
$EngineRoot = if ($env:UE_ROOT) { $env:UE_ROOT } else { "D:\UE_5.8" }
$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$BuildTool = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
$EvidenceRoot = Join-Path $ProjectRoot "Saved\Automation\RunEnvironmentSocial"
$RuntimeProbe = Join-Path $ProjectRoot "Scripts\verify_run_environment_social_runtime.py"
$BehaviorEvidenceRelativePath = ".codex\state\run-environment-social-behavior-evidence.json"
$BehaviorEvidenceRecorderRelativePath = "Scripts/record_run_environment_social_behavior_evidence.ps1"
$TranscriptRoot = Join-Path $EvidenceRoot ("{0}-{1}" -f $Mode, (Get-Date -Format "yyyyMMdd-HHmmss"))
$TranscriptLogFile = Join-Path $TranscriptRoot ("{0}.log" -f $Mode)

New-Item -ItemType Directory -Path $TranscriptRoot -Force | Out-Null
Start-Transcript -Path $TranscriptLogFile -Force | Out-Null

function Assert-ToolFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )
    <# 工具校验把项目文件、UBT 和 Editor 入口固定到本轮证据；缺失时直接失败，避免沿用旧构建或旧自动化日志。 #>
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw ("RunEnvironmentSocial missing {0}: {1}" -f $Description, $Path)
    }
}

function Assert-FileExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )
    <# 本模块 Static 只接受当前工作树里的真实文件作为入口证据，防止把技术方案中的拟定类型误当成已经接入的代码。 #>
    $Path = Join-Path $ProjectRoot $RelativePath
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw ("RunEnvironmentSocial missing {0}: {1}" -f $Description, $RelativePath)
    }
}

function Assert-DirectoryExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )
    <# 目录校验用于确认 Run、Environment、Social 三个实现域真实存在；它不把目录存在冒充模块完成。 #>
    $Path = Join-Path $ProjectRoot $RelativePath
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw ("RunEnvironmentSocial missing {0}: {1}" -f $Description, $RelativePath)
    }
}

function Assert-DirectoryTextContains {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativeDirectory,
        [Parameter(Mandatory = $true)]
        [string]$Needle,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )
    <# 文档锚点校验只确认同一个原子模块入口仍可被人类和机器找到，不把局部文字命中当成功能验收。 #>
    $Directory = Join-Path $ProjectRoot $RelativeDirectory
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        throw ("RunEnvironmentSocial missing directory for {0}: {1}" -f $Description, $RelativeDirectory)
    }
    $Matched = $false
    foreach ($File in Get-ChildItem -LiteralPath $Directory -Filter "*.md" -File) {
        $Text = Get-Content -LiteralPath $File.FullName -Raw -Encoding UTF8
        if ($Text.Contains($Needle)) {
            $Matched = $true
            break
        }
    }
    if (-not $Matched) {
        throw ("RunEnvironmentSocial directory text contract missing {0}: {1} in {2}" -f $Description, $Needle, $RelativeDirectory)
    }
}

function Assert-HarnessEntry {
    <# Harness 校验保证 Run、Environment、Social 被作为一个原子模块追踪；缺任一规则都说明后续容易退回碎片化交接。 #>
    $HarnessPath = Join-Path $ProjectRoot ".harness\harness.json"
    Assert-FileExists ".harness\harness.json" "project harness"
    $Harness = Get-Content -LiteralPath $HarnessPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $Harness.module_delivery.PSObject.Properties.Name.Contains("RunEnvironmentSocial")) {
        throw "RunEnvironmentSocial module is missing from harness"
    }
    $Module = $Harness.module_delivery.RunEnvironmentSocial
    if ($Module.delivery_mode -ne "single_atomic_module") {
        throw ("RunEnvironmentSocial delivery_mode mismatch: {0}" -f $Module.delivery_mode)
    }
    foreach ($Facet in @("RunFlow", "EnvironmentSnapshot", "WaterQueryAndChum", "SocialProtection", "ReconnectBoundary")) {
        if (@($Module.facets) -notcontains $Facet) {
            throw ("RunEnvironmentSocial facet missing from atomic module contract: {0}" -f $Facet)
        }
    }
    if ([string]::IsNullOrWhiteSpace($Module.tracking_rule) -or [string]::IsNullOrWhiteSpace($Module.completion_rule) -or [string]::IsNullOrWhiteSpace($Module.handoff_rule)) {
        throw "RunEnvironmentSocial atomic contract has empty tracking/completion/handoff rule"
    }
    if (-not $Harness.verification.PSObject.Properties.Name.Contains("run_environment_social_static_check")) {
        throw "RunEnvironmentSocial static verification key is missing from harness"
    }
    if (-not $Harness.verification.PSObject.Properties.Name.Contains("run_environment_social_build")) {
        throw "RunEnvironmentSocial build verification key is missing from harness"
    }
    if (-not $Harness.verification.PSObject.Properties.Name.Contains("run_environment_social_automation")) {
        throw "RunEnvironmentSocial automation verification key is missing from harness"
    }
    if (-not $Harness.verification.PSObject.Properties.Name.Contains("run_environment_social_runtime")) {
        throw "RunEnvironmentSocial runtime verification key is missing from harness"
    }
    if (-not $Harness.verification.PSObject.Properties.Name.Contains("run_environment_social_behavior_evidence")) {
        throw "RunEnvironmentSocial behavior evidence verification key is missing from harness"
    }
    if (-not $Harness.verification.PSObject.Properties.Name.Contains("run_environment_social_record_behavior_evidence")) {
        throw "RunEnvironmentSocial behavior evidence recorder key is missing from harness"
    }
}

function Assert-ModuleStatusFile {
    <# 状态文件校验把“还缺一场整体人工或多客户端行为证据”固定成模块级缺口，避免后续把天气、窝料、求助、偷取和重连拆成散单。 #>
    $StatusRelativePath = ".codex\state\run-environment-social-harness.json"
    $StatusPath = Join-Path $ProjectRoot $StatusRelativePath
    Assert-FileExists $StatusRelativePath "RunEnvironmentSocial module status"
    $Status = Get-Content -LiteralPath $StatusPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($Status.module_key -ne "RunEnvironmentSocial") {
        throw ("RunEnvironmentSocial status module_key mismatch: {0}" -f $Status.module_key)
    }
    if ($Status.delivery_mode -ne "single_atomic_module") {
        throw ("RunEnvironmentSocial status delivery_mode mismatch: {0}" -f $Status.delivery_mode)
    }
    if ($Status.closure_status -ne "runtime_evidence_passed") {
        throw ("RunEnvironmentSocial status closure_status mismatch: {0}" -f $Status.closure_status)
    }
    if ($Status.open_module_acceptance.acceptance_id -ne "RunEnvironmentSocial.ModuleLevelHumanOrMultiClientBehaviorEvidence") {
        throw "RunEnvironmentSocial status missing module-level human or multi-client acceptance"
    }
    if (([string]::IsNullOrWhiteSpace($Status.open_module_acceptance.recording_command)) -or ($Status.open_module_acceptance.recording_command -notmatch "record_run_environment_social_behavior_evidence\.ps1")) {
        throw "RunEnvironmentSocial status must point to the single behavior evidence recorder"
    }
    if ($Status.open_module_acceptance.status -ne "blocked") {
        throw ("RunEnvironmentSocial status must stay blocked until module-level behavior evidence exists: {0}" -f $Status.open_module_acceptance.status)
    }
    if (@($Status.open_module_acceptance.must_cover).Count -lt 5 -or @($Status.open_module_acceptance.do_not_split_into).Count -lt 5) {
        throw "RunEnvironmentSocial status must keep one complete acceptance scope and anti-splitting guard"
    }
}

function Assert-BehaviorEvidenceFile {
    param(
        [switch]$RequirePass
    )
    <# BehaviorEvidence 是 RunEnvironmentSocial 唯一的模块级行为证据入口：它要求同一场人工或多客户端过程同时证明 RunFlow、环境、水域/窝料、社交保护、偷取和重连边界。
       这些行为只有在同一局 Lake Run 里连起来才说明玩家入口闭环成立，所以缺证据时保持 blocked，不能拆成天气/窝料/求助/保护牌/偷取/重连小任务分别认领。 #>
    $EvidencePath = Join-Path $ProjectRoot $BehaviorEvidenceRelativePath
    Assert-FileExists $BehaviorEvidenceRelativePath "RunEnvironmentSocial behavior evidence intake"
    $Evidence = Get-Content -LiteralPath $EvidencePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($Evidence.module_key -ne "RunEnvironmentSocial") {
        throw ("RunEnvironmentSocial behavior evidence module_key mismatch: {0}" -f $Evidence.module_key)
    }
    if ($Evidence.evidence_id -ne "RunEnvironmentSocial.ModuleLevelHumanOrMultiClientBehaviorEvidence") {
        throw ("RunEnvironmentSocial behavior evidence id mismatch: {0}" -f $Evidence.evidence_id)
    }
    if ($Evidence.delivery_mode -ne "single_atomic_module" -or $Evidence.evidence_mode -ne "human_or_multi_client") {
        throw "RunEnvironmentSocial behavior evidence must stay a single human_or_multi_client module record"
    }
    if (([string]::IsNullOrWhiteSpace($Evidence.recording_script)) -or ($Evidence.recording_script -ne $BehaviorEvidenceRecorderRelativePath)) {
        throw "RunEnvironmentSocial behavior evidence must point to the single behavior evidence recorder"
    }
    if (@("blocked", "pass") -notcontains $Evidence.status) {
        throw ("RunEnvironmentSocial behavior evidence unsupported status: {0}" -f $Evidence.status)
    }
    if ([string]::IsNullOrWhiteSpace($Evidence.anti_fragmentation_rule) -or $Evidence.anti_fragmentation_rule -notmatch "Do not split") {
        throw "RunEnvironmentSocial behavior evidence must keep the anti-fragmentation rule"
    }

    $ExpectedCoverageIds = @(
        "SameLakeRunTwoPlayersThroughFrontendOnline",
        "RunFlowAndEnvironmentVisibleInActiveRun",
        "WaterQueryAndChumUseSingleAuthoritySurface",
        "SocialAndTheftAreServerAuthoritative",
        "ReconnectFailClosedBoundaryRemainsClosed"
    )
    $Coverage = @($Evidence.coverage_requirements)
    foreach ($CoverageId in $ExpectedCoverageIds) {
        $Matches = @($Coverage | Where-Object { $_.coverage_id -eq $CoverageId })
        if ($Matches.Count -ne 1) {
            throw ("RunEnvironmentSocial behavior evidence coverage mismatch: {0} matches={1}" -f $CoverageId, $Matches.Count)
        }
        if ([string]::IsNullOrWhiteSpace($Matches[0].proof_required)) {
            throw ("RunEnvironmentSocial behavior evidence coverage missing proof text: {0}" -f $CoverageId)
        }
    }
    foreach ($Entry in $Coverage) {
        if ($ExpectedCoverageIds -notcontains $Entry.coverage_id) {
            throw ("RunEnvironmentSocial behavior evidence has unknown coverage id: {0}" -f $Entry.coverage_id)
        }
        if (@("missing", "pass") -notcontains $Entry.status) {
            throw ("RunEnvironmentSocial behavior evidence coverage status invalid: {0} status={1}" -f $Entry.coverage_id, $Entry.status)
        }
    }

    if (-not $RequirePass) {
        return $Evidence
    }

    if ($Evidence.status -ne "pass") {
        $Missing = @($Coverage | Where-Object { $_.status -ne "pass" } | ForEach-Object { $_.coverage_id })
        Write-Host ("RUN_ENVIRONMENT_SOCIAL_BEHAVIOR_EVIDENCE_BLOCKED EvidenceFile={0} Missing={1}" -f $EvidencePath, ($Missing -join ","))
        throw "RunEnvironmentSocial behavior evidence is not passed yet"
    }
    if (@($Evidence.accepted_source_kinds) -notcontains $Evidence.evidence_record.source_kind) {
        throw ("RunEnvironmentSocial behavior evidence source_kind is not accepted: {0}" -f $Evidence.evidence_record.source_kind)
    }
    if ([int]$Evidence.evidence_record.participants_count -lt 2) {
        throw ("RunEnvironmentSocial behavior evidence requires at least two participants: {0}" -f $Evidence.evidence_record.participants_count)
    }
    if ([string]::IsNullOrWhiteSpace($Evidence.evidence_record.recorded_on) -or [string]::IsNullOrWhiteSpace($Evidence.evidence_record.summary)) {
        throw "RunEnvironmentSocial behavior evidence pass requires recorded_on and summary"
    }
    if (@($Evidence.evidence_record.evidence_refs).Count -lt 1) {
        throw "RunEnvironmentSocial behavior evidence pass requires at least one evidence reference"
    }
    foreach ($Entry in $Coverage) {
        if ($Entry.status -ne "pass") {
            throw ("RunEnvironmentSocial behavior evidence cannot pass while coverage is missing: {0}" -f $Entry.coverage_id)
        }
    }
    Write-Host ("RUN_ENVIRONMENT_SOCIAL_BEHAVIOR_EVIDENCE_PASS EvidenceFile={0}" -f $EvidencePath)
}

function Assert-BehaviorEvidenceRecorder {
    <# 记录器校验保证人工/多客户端证据只能通过一个整体验收入口写入；脚本必须要求显式完整覆盖确认，并且五个覆盖项不能被拆成命令行参数逐项打勾。 #>
    $RecorderPath = Join-Path $ProjectRoot $BehaviorEvidenceRecorderRelativePath
    Assert-FileExists $BehaviorEvidenceRecorderRelativePath "RunEnvironmentSocial behavior evidence recorder"
    $RecorderText = Get-Content -LiteralPath $RecorderPath -Raw -Encoding UTF8
    foreach ($Needle in @(
        "ConfirmAllCoverage",
        "RUN_ENVIRONMENT_SOCIAL_BEHAVIOR_EVIDENCE_RECORDED",
        "Set-BehaviorEvidencePass",
        "human_playtest",
        "multi_client_runtime"
    )) {
        if (-not $RecorderText.Contains($Needle)) {
            throw ("RunEnvironmentSocial behavior evidence recorder missing contract text: {0}" -f $Needle)
        }
    }
    foreach ($CoverageId in @(
        "SameLakeRunTwoPlayersThroughFrontendOnline",
        "RunFlowAndEnvironmentVisibleInActiveRun",
        "WaterQueryAndChumUseSingleAuthoritySurface",
        "SocialAndTheftAreServerAuthoritative",
        "ReconnectFailClosedBoundaryRemainsClosed"
    )) {
        if (-not $RecorderText.Contains($CoverageId)) {
            throw ("RunEnvironmentSocial behavior evidence recorder missing coverage id: {0}" -f $CoverageId)
        }
    }
    if ($RecorderText -match "CoverageId\\]|PartialCoverage|coverage_status") {
        throw "RunEnvironmentSocial behavior evidence recorder must not expose partial coverage knobs"
    }
}

function Invoke-StaticInventory {
    <# Static 盘点只建立下一原子模块的可追踪入口：代码域、关键测试、文档锚点和 Harness 合同必须同时存在，功能完成留给后续 Build/Automation/Runtime 证据。 #>
    Assert-HarnessEntry
    Assert-ModuleStatusFile
    Assert-BehaviorEvidenceFile | Out-Null
    Assert-BehaviorEvidenceRecorder

    foreach ($Directory in @("Source\Catfishing\Run", "Source\Catfishing\Environment", "Source\Catfishing\Social")) {
        Assert-DirectoryExists $Directory ("source directory {0}" -f $Directory)
    }
    foreach ($File in @(
        "Source\Catfishing\Run\CatRunStateTreeNodes.h",
        "Source\Catfishing\Framework\Core\CatRunContracts.h",
        "Source\Catfishing\Environment\CatWaterQuerySubsystem.h",
        "Source\Catfishing\Environment\CatChumFieldSubsystem.h",
        "Source\Catfishing\Social\CatSocialService.h",
        "Source\Catfishing\Social\CatProtectionSignActor.h",
        "Source\Catfishing\Run\Tests\CatRunStateTreeNodesTests.cpp",
        "Source\Catfishing\Environment\Tests\CatWaterQuerySubsystemTests.cpp",
        "Source\Catfishing\Environment\Tests\CatChumFieldSubsystemTests.cpp",
        "Source\Catfishing\Social\Tests\CatSocialServiceTests.cpp",
        "Source\Catfishing\Social\Tests\CatProtectionSignActorTests.cpp",
        "Scripts\configure_run_environment_social_map.py",
        "Scripts\verify_run_environment_social_runtime.py"
    )) {
        Assert-FileExists $File ("module entry {0}" -f $File)
    }

    Assert-DirectoryTextContains "Docs\Development" "RUN_ENVIRONMENT_SOCIAL_NEXT_MODULE" "human next module marker"
    Assert-DirectoryTextContains "Docs\Architecture" "RUN_ENVIRONMENT_SOCIAL_NEXT_MODULE" "technical next module marker"
    Assert-DirectoryTextContains "Docs\Architecture" "RequestHelp" "technical social help contract"
    Assert-DirectoryTextContains "Docs\Architecture" "WaterQuery" "technical water query contract"
    Assert-DirectoryTextContains "Docs\Architecture" "RunFlow" "technical run flow contract"

    Write-Host "RUN_ENVIRONMENT_SOCIAL_STATIC_PASS Harness=Ready SourceEntries=Ready Documentation=Ready ModuleStatus=OpenWaitingForHumanOrMultiClientEvidence"
}

function Invoke-ModuleBuild {
    <# Build 验证流程：调用当前项目真实 CatfishingEditor Target；UBT 输出保存在模块证据目录，非零退出码不被解释成静态通过。 #>
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $BuildTool "Unreal Build tool"
    $BuildOutputLog = Join-Path $TranscriptRoot "UBT.log"
    $BuildArguments = @("CatfishingEditor", "Win64", "Development", $ProjectFile, "-WaitMutex", "-NoHotReload", "-NoUBA")
    & $BuildTool @BuildArguments 2>&1 | Tee-Object -FilePath $BuildOutputLog
    $BuildExitCode = $LASTEXITCODE
    if ($BuildExitCode -ne 0) {
        throw ("CatfishingEditor build failed with exit code {0}" -f $BuildExitCode)
    }
    Write-Host ("RUN_ENVIRONMENT_SOCIAL_BUILD_PASS UBTLog={0} ExitCode=0" -f $BuildOutputLog)
}

function Invoke-AutomationBatch {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Filter,
        [Parameter(Mandatory = $true)]
        [string]$BatchName,
        [Parameter(Mandatory = $true)]
        [string]$RunRoot,
        [Parameter(Mandatory = $true)]
        [string[]]$ExpectedTests
    )
    <# Automation 批次流程：每个切面独立落报告，但只由本脚本统一汇总；缺报告、失败、未运行或缺关键用例都会让整个原子模块验证失败。 #>
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $Editor "Unreal Editor commandlet"
    $BatchRoot = Join-Path $RunRoot $BatchName
    $ReportRoot = Join-Path $BatchRoot "Report"
    $LogFile = Join-Path $BatchRoot "Automation.log"
    New-Item -ItemType Directory -Path $BatchRoot -Force | Out-Null
    if (Test-Path -LiteralPath $ReportRoot) {
        Remove-Item -LiteralPath $ReportRoot -Recurse -Force
    }
    if (Test-Path -LiteralPath $LogFile) {
        Remove-Item -LiteralPath $LogFile -Force
    }
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
        "-ExecCmds=Automation RunTests $Filter;Quit" `
        "-TestExit=Automation Test Queue Empty" `
        "-ReportExportPath=$ReportRoot" `
        "-abslog=$LogFile"
    if ($LASTEXITCODE -ne 0) {
        throw ("{0} automation editor failed with exit code {1}" -f $BatchName, $LASTEXITCODE)
    }
    $IndexFile = Join-Path $ReportRoot "index.json"
    if (-not (Test-Path -LiteralPath $IndexFile) -or -not (Test-Path -LiteralPath $LogFile)) {
        throw ("{0} automation did not produce a fresh report and log" -f $BatchName)
    }
    $Report = Get-Content -LiteralPath $IndexFile -Raw -Encoding UTF8 | ConvertFrom-Json
    $Tests = @($Report.tests)
    $NonSuccess = @($Tests | Where-Object { $_.state -ne "Success" -or [int]$_.errors -ne 0 })
    if ($NonSuccess.Count -gt 0) {
        throw ("{0} automation has non-success tests: {1}" -f $BatchName, ($NonSuccess.fullTestPath -join ", "))
    }
    if ([int]$Report.failed -ne 0 -or [int]$Report.notRun -ne 0 -or [int]$Report.inProcess -ne 0) {
        throw ("{0} automation summary is not clean: succeeded={1} failed={2} notRun={3} inProcess={4}" -f
            $BatchName, $Report.succeeded, $Report.failed, $Report.notRun, $Report.inProcess)
    }
    foreach ($ExpectedTest in $ExpectedTests) {
        $Matches = @($Tests | Where-Object { $_.fullTestPath -eq $ExpectedTest })
        if ($Matches.Count -ne 1) {
            throw ("{0} automation missing expected test {1}: matches={2} total={3}" -f
                $BatchName, $ExpectedTest, $Matches.Count, $Tests.Count)
        }
        $LogText = Get-Content -LiteralPath $LogFile -Raw -Encoding UTF8
        if ($LogText -notmatch [regex]::Escape($ExpectedTest)) {
            throw ("{0} automation log does not mention expected test {1}" -f $BatchName, $ExpectedTest)
        }
    }
}

function Invoke-ModuleAutomation {
    <# 模块自动化流程：Run、Fishing 命令入口、Environment、Social 和 GameMode 边界作为一个原子证据包运行；这些批次不能被拆成独立完成项。 #>
    $RunRoot = Join-Path $EvidenceRoot (Get-Date -Format "yyyyMMdd-HHmmss")
    $Batches = @()
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Run"
        Name = "Run"
        ExpectedTests = @(
            "Catfishing.Unit.Run.Settings.RuntimeAdmissionAndSettlementPoliciesAreExplicit",
            "Catfishing.Unit.Run.StateTreeNodes.DefaultParametersDoNotStartHiddenRunFlow",
            "Catfishing.Unit.Run.SacrificeCoordinator.TeardownClosesNewSacrificeCommands",
            "Catfishing.Unit.Run.SacrificeCoordinator.ItemsCommittedRecoveryOnlyRetriesRun"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Framework.GameMode"
        Name = "FrameworkGameMode"
        ExpectedTests = @(
            "Catfishing.Unit.Framework.GameMode.RunCommandsFailClosedBeforeRuntimeStart",
            "Catfishing.Unit.Framework.GameMode.CommandIntentPhaseGatesKeepSocialReadyAndSettlementOpen",
            "Catfishing.Unit.Framework.GameMode.ReconnectAdmissionWhitelistIsFailClosedEndToEnd",
            "Catfishing.Unit.Framework.GameMode.RunEnvironmentSocialPlayerEntrypointsStayAtomic"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Fishing.CommandComponent"
        Name = "FishingCommandComponent"
        ExpectedTests = @(
            "Catfishing.Unit.Fishing.CommandComponent.DirectRodCommandsReturnCommandsClosedWhenFishingGateIsClosed"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Environment"
        Name = "Environment"
        ExpectedTests = @(
            "Catfishing.Unit.Environment.ConfiguredProvider.RequiresRuntimeSettingsAndReturnsReadOnlySnapshot",
            "Catfishing.Unit.Environment.Settings.TimeOfDayAndNaturalChumFieldRequireExplicitRuntime",
            "Catfishing.Unit.Environment.WaterQuery.RegistersAndUnregistersBakedRegions",
            "Catfishing.Unit.Environment.WaterQuery.CandidateIgnoresClientZAndProjectsToHorizontalPlane",
            "Catfishing.Unit.Environment.ChumField.BudgetReservationAbortAndActivationAreAtomic",
            "Catfishing.Unit.Environment.ChumField.SampleAddsOnlyOverlappingActiveFieldsInSameRegion",
            "Catfishing.Unit.Environment.ChumPlacement.CommitConsumesQuantityAndActivatesExactlyOneField",
            "Catfishing.Unit.Environment.ChumPlacement.IdentityScopesRequestReplayAndFailureIsFirstWins"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Social"
        Name = "Social"
        ExpectedTests = @(
            "Catfishing.Unit.Social.Settings.TheftMischiefAndManualHelpHaveIndependentGates",
            "Catfishing.Unit.Social.Service.EmptyTeardownAndMissingIdentityCommandsFailClosed",
            "Catfishing.Unit.Social.ProtectionSignActor.ProtectsOnlyConfiguredPlayerInsideRadius"
        )
    }

    $FailedBatches = @()
    foreach ($Batch in $Batches) {
        try {
            Invoke-AutomationBatch $Batch.Filter $Batch.Name $RunRoot $Batch.ExpectedTests
        }
        catch {
            $FailedBatches += ("{0}: {1}" -f $Batch.Name, $_.Exception.Message)
            Write-Warning ("{0} automation batch failed; continuing so other RunEnvironmentSocial batches still leave evidence. {1}" -f $Batch.Name, $_.Exception.Message)
        }
    }
    if ($FailedBatches.Count -gt 0) {
        throw ("RunEnvironmentSocial automation batches failed:`n{0}" -f ($FailedBatches -join "`n"))
    }
    Write-Host ("RUN_ENVIRONMENT_SOCIAL_AUTOMATION_PASS EvidenceRoot={0}" -f $RunRoot)
}

function Invoke-ModuleRuntime {
    <# Runtime 验证流程：在真实 Editor 里只读加载正式 Lake，并核对 Run/Environment/Social 的配置、地图锚点和正向 gate；它不保存资源，也不把 Runtime 绿灯拆成独立完成项。 #>
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $Editor "Unreal Editor commandlet"
    Assert-ToolFile $RuntimeProbe "RunEnvironmentSocial runtime probe"
    $RuntimeRoot = Join-Path $EvidenceRoot ("Runtime-{0}" -f (Get-Date -Format "yyyyMMdd-HHmmss"))
    $RuntimeLog = Join-Path $RuntimeRoot "EditorRuntime.log"
    New-Item -ItemType Directory -Path $RuntimeRoot -Force | Out-Null
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
        "-ExecutePythonScript=$RuntimeProbe" `
        "-abslog=$RuntimeLog"
    if ($LASTEXITCODE -ne 0) {
        throw ("RunEnvironmentSocial runtime editor failed with exit code {0}" -f $LASTEXITCODE)
    }
    if (-not (Test-Path -LiteralPath $RuntimeLog)) {
        throw "RunEnvironmentSocial runtime did not produce a fresh log"
    }
    $RuntimeLogText = Get-Content -LiteralPath $RuntimeLog -Raw -Encoding UTF8
    $PassCount = ([regex]::Matches($RuntimeLogText, "RUN_ENVIRONMENT_SOCIAL_RUNTIME_PASS")).Count
    if ($PassCount -ne 1 -or $RuntimeLogText -match "LogPython: Error") {
        throw ("RunEnvironmentSocial runtime evidence is not green: passMarkers={0}" -f $PassCount)
    }
    Write-Host ("RUN_ENVIRONMENT_SOCIAL_RUNTIME_PASS EvidenceRoot={0}" -f $RuntimeRoot)
}

function Invoke-BehaviorEvidence {
    <# BehaviorEvidence 模式只读取这一份模块级 JSON，不做局部补证；只有整场人工/多客户端记录全部覆盖并通过才输出 PASS。
       当前缺真人或多客户端证据时必须失败并暴露 blocked，让后续 Agent 知道这里是一个未闭合模块，而不是一串可拆散关闭的小项。 #>
    Assert-BehaviorEvidenceFile -RequirePass | Out-Null
}

try {
    switch ($Mode) {
        "Static" { Invoke-StaticInventory }
        "Build" { Invoke-ModuleBuild }
        "Automation" { Invoke-ModuleAutomation }
        "Runtime" { Invoke-ModuleRuntime }
        "BehaviorEvidence" { Invoke-BehaviorEvidence }
        default { throw ("Unknown verification mode: {0}" -f $Mode) }
    }
}
catch {
    Write-Host $_
    throw
}
finally {
    Stop-Transcript | Out-Null
    Write-Host ("Evidence log: {0}" -f $TranscriptLogFile)
}
