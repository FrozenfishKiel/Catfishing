param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Static", "Build", "Automation")]
    [string]$Mode
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ProjectFile = Join-Path $ProjectRoot "Catfishing.uproject"
$EngineRoot = if ($env:UE_ROOT) { $env:UE_ROOT } else { "D:\UE_5.8" }
$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$BuildTool = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
$EvidenceRoot = Join-Path $ProjectRoot "Saved\Automation\DataWorldProfileAlbum"
$TranscriptStarted = $false
$TranscriptLogFile = $null

if ($Mode -eq "Static" -or $Mode -eq "Build") {
    $TranscriptRoot = Join-Path $EvidenceRoot ("{0}-{1}" -f $Mode, (Get-Date -Format "yyyyMMdd-HHmmss"))
    $TranscriptLogFile = Join-Path $TranscriptRoot ("{0}.log" -f $Mode)
    New-Item -ItemType Directory -Path $TranscriptRoot -Force | Out-Null
    Start-Transcript -Path $TranscriptLogFile -Force | Out-Null
    $TranscriptStarted = $true
}

function Assert-ToolFile {
    <#
    验证项目文件或外部工具真实存在。
    各模式启动 UBT、Editor 或读取合同前统一调用它；缺失时立即失败，避免旧日志被误当成本轮 DataWorldProfileAlbum 证据。
    #>
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw ("{0} not found: {1}" -f $Description, $Path)
    }
}

function Assert-TextPattern {
    <#
    对项目文件执行稳定文本合同检查。
    Static 模式只用它证明整模块合同和关键入口仍在同一条链路上，不把文本命中冒充构建或自动化成功。
    #>
    param([string]$Pattern, [string]$RelativePath, [string]$Description)
    $Ripgrep = Get-Command rg -ErrorAction SilentlyContinue
    if (-not $Ripgrep) {
        throw "rg is required for DataWorldProfileAlbum static verification"
    }
    & $Ripgrep.Source -n $Pattern (Join-Path $ProjectRoot $RelativePath)
    if ($LASTEXITCODE -ne 0) {
        throw ("DataWorldProfileAlbum static contract missing: {0} pattern={1} path={2}" -f $Description, $Pattern, $RelativePath)
    }
}

function Invoke-ModuleStaticCheck {
    <#
    执行 Data/World/Profile/Album 原子模块静态合同检查。
    流程先解析 Harness 中唯一模块入口，再核对 Data、Profile、Collection、Album、Imprint 的关键代码和测试证据都挂在同一闭环下。
    #>
    $HarnessPath = Join-Path $ProjectRoot ".harness/harness.json"
    $Harness = Get-Content -LiteralPath $HarnessPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not ($Harness.module_delivery.PSObject.Properties.Name -contains "DataWorldProfileAlbum")) {
        throw "DataWorldProfileAlbum module is missing from harness"
    }
    $Module = $Harness.module_delivery.DataWorldProfileAlbum
    if ($Module.delivery_mode -ne "single_atomic_module") {
        throw ("DataWorldProfileAlbum delivery_mode mismatch: {0}" -f $Module.delivery_mode)
    }
    foreach ($Facet in @("DataCatalog", "WorldProgress", "ProfileGrant", "Album", "Imprint")) {
        if (@($Module.facets) -notcontains $Facet) {
            throw ("DataWorldProfileAlbum facet missing from atomic module contract: {0}" -f $Facet)
        }
    }
    if ([string]::IsNullOrWhiteSpace($Module.tracking_rule) -or [string]::IsNullOrWhiteSpace($Module.completion_rule)) {
        throw "DataWorldProfileAlbum atomic tracking/completion rule is incomplete"
    }

    Assert-TextPattern "DataWorldProfileAlbum" ".harness/harness.json" "module harness entry"
    Assert-TextPattern "single_atomic_module" ".harness/harness.json" "atomic delivery mode"
    Assert-TextPattern "data_world_profile_album_static_check" ".harness/harness.json" "static verification entry"
    Assert-TextPattern "data_world_profile_album_build" ".harness/harness.json" "build verification entry"
    Assert-TextPattern "data_world_profile_album_automation" ".harness/harness.json" "automation verification entry"
    Assert-TextPattern "Data / World / Profile / Album" "Docs/Development" "human module row"
    Assert-TextPattern "UnlockId" "Docs/Development" "unlock milestone decision remains visible"
    Assert-TextPattern "Fish_RiverPattern" "Config/DefaultGame.ini" "formal fish catalog seed is explicit"
    Assert-TextPattern "Fish_Puffer" "Config/DefaultGame.ini" "formal toxic fish catalog seed is explicit"
    Assert-TextPattern "Bite_Cautious" "Config/DefaultGame.ini" "formal bite personality seed is explicit"
    Assert-TextPattern "Fight_GiantHeavy" "Config/DefaultGame.ini" "formal fight personality seed is explicit"
    $DefaultGame = Get-Content -LiteralPath (Join-Path $ProjectRoot "Config/DefaultGame.ini") -Raw -Encoding UTF8
    foreach ($TestAssetName in @("DA_Fish_Test01", "DA_Bite_Test01", "DA_Fight_Test01")) {
        if ($DefaultGame.Contains($TestAssetName)) {
            throw ("DataWorldProfileAlbum static contract still references tracked test asset: {0}" -f $TestAssetName)
        }
    }
    Assert-TextPattern "FCatProfileGrant" "Source/Catfishing/Framework/Core/CatProfileContracts.h" "profile grant contract"
    Assert-TextPattern "ApplyGrant" "Source/Catfishing/Profile/CatProfileSubsystem.h" "durable grant entry"
    Assert-TextPattern "GetFishCollectionSnapshot" "Source/Catfishing/Profile/CatProfileSubsystem.h" "public collection snapshot"
    Assert-TextPattern "SetImprintHidden" "Source/Catfishing/Profile/CatProfileSubsystem.h" "local album privacy"
    Assert-TextPattern "RecordCommittedCapture" "Source/Catfishing/Collection/CatRunImprintService.h" "capture to fish grant entry"
    Assert-TextPattern "ReportCaptureResult" "Source/Catfishing/Collection/CatRunImprintService.h" "capture plan result entry"
    Assert-TextPattern "RecordCommittedUnlock" "Source/Catfishing/Collection/CatRunImprintService.h" "unlock grant entry"
    Assert-TextPattern "FCatImprintCaptureDeliveryRecord" "Source/Catfishing/Collection/CatImprintTypes.h" "capture delivery record"
    Assert-TextPattern "FCatGrantDeliveryRecord" "Source/Catfishing/Collection/CatImprintTypes.h" "grant delivery record"
    Assert-TextPattern "GrantJournalFishAlbumAndUnlocksMergeDurably" "Source/Catfishing/Profile/Tests/CatProfileSubsystemTests.cpp" "profile durable grant automation"
    Assert-TextPattern "CaptureResultCreatesImprintGrantAfterSuccess" "Source/Catfishing/Collection/Tests/CatRunImprintServiceTests.cpp" "imprint grant automation"
    Assert-TextPattern "CommitCaptureRecordsSingleFishGrant" "Source/Catfishing/Collection/Tests/CatItemsCollectionSliceTests.cpp" "items to collection slice automation"
    Assert-TextPattern "ManifestChunksHashCursorAndRecipientAuth" "Source/Catfishing/Collection/Tests/CatImprintMediaTransportServiceTests.cpp" "imprint media automation"
    Assert-TextPattern "DuplicateRuntimeDefinitionsFailClosed" "Source/Catfishing/Data/Tests/CatFishCatalogSettingsTests.cpp" "data catalog fail closed automation"
    Assert-TextPattern "ShowcaseRiverSelectsFormalFishCatalog" "Source/Catfishing/Data/Tests/CatFishCatalogSettingsTests.cpp" "Showcase formal fish catalog automation"
    Assert-TextPattern "SHOWCASE_FORMAL_FISH_REGION_VERIFY_PASS" "Scripts/verify_showcase_formal_fish_region.py" "Showcase River map verification entry"
}

function Invoke-ModuleBuild {
    <#
    构建当前 CatfishingEditor 目标。
    这是 DataWorldProfileAlbum 的新鲜二进制证据；UBT 非零时直接失败，不沿用其他模块或旧构建结果。
    #>
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $BuildTool "Unreal Build tool"
    $BuildArguments = @("CatfishingEditor", "Win64", "Development", $ProjectFile, "-WaitMutex", "-NoHotReload", "-NoUBA")
    $BuildOutputLog = $null
    if ($TranscriptLogFile) {
        $BuildOutputLog = Join-Path (Split-Path -Parent $TranscriptLogFile) "UBT.log"
        & $BuildTool @BuildArguments 2>&1 | Tee-Object -FilePath $BuildOutputLog
    }
    else {
        & $BuildTool @BuildArguments
    }
    $BuildExitCode = $LASTEXITCODE
    if ($BuildExitCode -ne 0) {
        throw ("CatfishingEditor build failed with exit code {0}" -f $BuildExitCode)
    }
    Write-Host ("DATA_WORLD_PROFILE_ALBUM_BUILD_PASS UBTLog={0} ExitCode=0" -f $BuildOutputLog)
}

function Invoke-AutomationBatch {
    <#
    为单个 Automation 过滤器启动独立 Editor 进程并保存新报告与日志。
    每批必须精确包含本模块的必需用例；失败、未运行、缺报告或缺目标用例都会让 Automation 模式失败。
    #>
    param([string]$Filter, [string]$BatchName, [string]$RunRoot, [string[]]$ExpectedTests)
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $Editor "Unreal Editor commandlet"
    $BatchRoot = Join-Path $RunRoot $BatchName
    $ReportRoot = Join-Path $BatchRoot "Report"
    $LogFile = Join-Path $BatchRoot "Automation.log"
    New-Item -ItemType Directory -Path $BatchRoot -Force | Out-Null
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
    $Report = Get-Content -LiteralPath $IndexFile -Raw | ConvertFrom-Json
    $Tests = @($Report.tests)
    $NonSuccess = @($Tests | Where-Object { $_.state -ne "Success" -or [int]$_.errors -ne 0 })
    if ($NonSuccess.Count -gt 0) {
        throw ("{0} automation has non-success tests: {1}" -f $BatchName, ($NonSuccess.fullTestPath -join ", "))
    }
    foreach ($ExpectedTest in $ExpectedTests) {
        $Matches = @($Tests | Where-Object { $_.fullTestPath -eq $ExpectedTest })
        if ($Matches.Count -ne 1) {
            throw ("{0} automation missing expected test {1}: matches={2} total={3}" -f $BatchName, $ExpectedTest, $Matches.Count, $Tests.Count)
        }
    }
    $LogText = Get-Content -LiteralPath $LogFile -Raw
    foreach ($ExpectedTest in $ExpectedTests) {
        if ($LogText -notmatch [regex]::Escape($ExpectedTest)) {
            throw ("{0} automation log does not mention expected test {1}" -f $BatchName, $ExpectedTest)
        }
    }
}

function Invoke-ModuleAutomation {
    <#
    编排 DataWorldProfileAlbum 的 Data、Profile、Collection、Items→Collection 和 PlayerState 投影自动化。
    这些批次是同一模块的证据切面；脚本最后统一汇总，不允许某个切面单独标成完成。
    #>
    $RunRoot = Join-Path $EvidenceRoot (Get-Date -Format "yyyyMMdd-HHmmss")
    $Batches = @()
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Data"
        Name = "Data"
        ExpectedTests = @(
            "Catfishing.Unit.Data.FishDefinition.DefaultsAreNotRuntimeReady",
            "Catfishing.Unit.Data.FishDefinition.StandardFishReadyWithoutCaptureImprintEvent",
            "Catfishing.Unit.Data.FishDefinition.ToxicFishRequiresPositivePoisonIncrease",
            "Catfishing.Unit.Data.FishDefinition.ChumPreferenceAndBaitMultipliersMustBeValid",
            "Catfishing.Unit.Data.FishDefinition.BiteAndFightPersonalitiesFailClosed",
            "Catfishing.Unit.Data.FishCatalog.DuplicateRuntimeDefinitionsFailClosed",
            "Catfishing.Unit.Data.FishCatalog.ZeroLocalChumAndUnlistedBaitAreNeutral",
            "Catfishing.Unit.Data.FishCatalog.RiskyFishAllowedUntilChallengeSafetyCeiling",
            "Catfishing.Unit.Data.FishCatalog.AvailableChallengeBandsFollowConfiguredMix",
            "Catfishing.Unit.Data.FishCatalog.EnduranceOnlyFishDoesNotOccupyMatchedBand",
            "Catfishing.Unit.Data.FishCatalog.MissingWeightedBandFallsBackToAvailableFish",
            "Catfishing.Unit.Data.FishCatalog.TargetChallengeOutweighsDistantFishWithinBand",
            "Catfishing.Unit.Data.FishCatalog.ShowcaseRiverSelectsFormalFishCatalog",
            "Catfishing.Unit.Data.FishCatalog.ShowcaseRiverProducesVariedFightStrengths"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Profile"
        Name = "Profile"
        ExpectedTests = @(
            "Catfishing.Unit.Profile.Settings.PersistenceAndImprintBridgeRequireExplicitSlot",
            "Catfishing.Unit.Profile.Subsystem.CapturePlanBroadcastRequiresExplicitBridgeAndCompletePlan",
            "Catfishing.Unit.Profile.Subsystem.NonFishInputsDoNotPublishFishCollectionChanges",
            "Catfishing.Unit.Profile.Subsystem.GrantJournalFishAlbumAndUnlocksMergeDurably"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Collection"
        Name = "Collection"
        ExpectedTests = @(
            "Catfishing.Unit.Collection.RunImprintService.CandidatePreflightSubmitAndReplayContracts",
            "Catfishing.Unit.Collection.RunImprintService.CapturePlansDeduplicateRecipientsAndReplayStable",
            "Catfishing.Unit.Collection.RunImprintService.TeardownClosesNewCandidatesAndPlans",
            "Catfishing.Unit.Collection.RunImprintService.UnlockGrantAckAuthorizesEquipment",
            "Catfishing.Unit.Collection.RunImprintService.CaptureResultCreatesImprintGrantAfterSuccess",
            "Catfishing.Unit.Collection.ImprintMedia.SettingsGateFailsClosedByDefault",
            "Catfishing.Unit.Collection.ImprintMedia.ManifestChunksHashCursorAndRecipientAuth",
            "Catfishing.Unit.Collection.ImprintMediaSettings.ProjectDefaultsEnableBoundedImageTransport"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Slice.ItemsCollection"
        Name = "ItemsCollectionSlice"
        ExpectedTests = @(
            "Catfishing.Slice.ItemsCollection.CommitCaptureRecordsSingleFishGrant"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Framework.PlayerState.PublicCollectionValidationAndStarterUnlockContract"
        Name = "FrameworkPlayerStateProjection"
        ExpectedTests = @(
            "Catfishing.Unit.Framework.PlayerState.PublicCollectionValidationAndStarterUnlockContract"
        )
    }

    $FailedBatches = @()
    foreach ($Batch in $Batches) {
        try {
            Invoke-AutomationBatch $Batch.Filter $Batch.Name $RunRoot $Batch.ExpectedTests
        }
        catch {
            $FailedBatches += ("{0}: {1}" -f $Batch.Name, $_.Exception.Message)
            Write-Warning ("{0} automation batch failed; continuing so other DataWorldProfileAlbum batches still leave evidence. {1}" -f $Batch.Name, $_.Exception.Message)
        }
    }
    if ($FailedBatches.Count -gt 0) {
        throw ("Automation batches failed:`n{0}" -f ($FailedBatches -join "`n"))
    }
}

try {
    switch ($Mode) {
        "Static" { Invoke-ModuleStaticCheck }
        "Build" { Invoke-ModuleBuild }
        "Automation" { Invoke-ModuleAutomation }
        default { throw ("Unknown verification mode: {0}" -f $Mode) }
    }
}
finally {
    if ($TranscriptStarted) {
        Stop-Transcript | Out-Null
        Write-Output ("Evidence log: {0}" -f $TranscriptLogFile)
    }
}
