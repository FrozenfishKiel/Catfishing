param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Static", "Build", "Automation", "Runtime")]
    [string]$Mode
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ProjectFile = Join-Path $ProjectRoot "Catfishing.uproject"
$EngineRoot = if ($env:UE_ROOT) { $env:UE_ROOT } else { "D:\UE_5.8" }
$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$BuildTool = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
$EvidenceRoot = Join-Path $ProjectRoot "Saved\Automation\EquipmentTeamLibraryShop"
$RuntimeProbe = Join-Path $ProjectRoot "Scripts\verify_equipment_teamlibrary_shop_runtime.py"
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
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw ("{0} not found: {1}" -f $Description, $Path)
    }
}

function Assert-TextPattern {
    param([string]$Pattern, [string]$RelativePath, [string]$Description)
    $Ripgrep = Get-Command rg -ErrorAction SilentlyContinue
    if (-not $Ripgrep) {
        throw "rg is required for static verification"
    }
    & $Ripgrep.Source -n $Pattern (Join-Path $ProjectRoot $RelativePath)
    if ($LASTEXITCODE -ne 0) {
        throw ("Static contract missing: {0} pattern={1} path={2}" -f $Description, $Pattern, $RelativePath)
    }
}

function Assert-NoTextPattern {
    param([string]$Pattern, [string]$RelativePath, [string]$Description)
    $Ripgrep = Get-Command rg -ErrorAction SilentlyContinue
    if (-not $Ripgrep) {
        throw "rg is required for static verification"
    }
    & $Ripgrep.Source -n $Pattern (Join-Path $ProjectRoot $RelativePath)
    if ($LASTEXITCODE -eq 0) {
        throw ("Static contract unexpectedly found: {0} pattern={1} path={2}" -f $Description, $Pattern, $RelativePath)
    }
    if ($LASTEXITCODE -ne 1) {
        throw ("Static contract scan failed: {0} pattern={1} path={2} exit={3}" -f $Description, $Pattern, $RelativePath, $LASTEXITCODE)
    }
}

function Assert-NoBinaryTextPattern {
    param([string]$Pattern, [string]$RelativePath, [string]$Description)
    $Ripgrep = Get-Command rg -ErrorAction SilentlyContinue
    if (-not $Ripgrep) {
        throw "rg is required for static verification"
    }
    & $Ripgrep.Source -a -n $Pattern (Join-Path $ProjectRoot $RelativePath)
    if ($LASTEXITCODE -eq 0) {
        throw ("Static binary contract unexpectedly found: {0} pattern={1} path={2}" -f $Description, $Pattern, $RelativePath)
    }
    if ($LASTEXITCODE -ne 1) {
        throw ("Static binary contract scan failed: {0} pattern={1} path={2} exit={3}" -f $Description, $Pattern, $RelativePath, $LASTEXITCODE)
    }
}

function Invoke-ModuleStaticCheck {
    Assert-TextPattern "EquipmentTeamLibraryShop" ".harness/harness.json" "module harness"
    Assert-TextPattern "equipment_teamlibrary_shop_static_check" ".harness/harness.json" "static verification entry"
    Assert-TextPattern "equipment_teamlibrary_shop_build" ".harness/harness.json" "build verification entry"
    Assert-TextPattern "equipment_teamlibrary_shop_automation" ".harness/harness.json" "automation verification entry"
    Assert-TextPattern "equipment_teamlibrary_shop_runtime" ".harness/harness.json" "runtime verification entry"
    Assert-TextPattern "EquipmentTeamLibraryShop" ".codex/state/equipment-teamlibrary-shop-harness.json" "module harness state"
    Assert-TextPattern "EquipmentTeamLibraryShop" ".codex/state/equipment-teamlibrary-shop-context.json" "module context state"
    Assert-TextPattern "Equipment / TeamLibrary / Shop" "Docs/Development" "human progress entry"
    Assert-TextPattern "bAutoConfigureStarterLoadout=False" "Config/DefaultGame.ini" "developer starter auto loadout disabled"
    Assert-TextPattern "Equip_Rod_StarterT1" "Config/DefaultGame.ini" "formal starter rod asset entry"
    Assert-TextPattern "Equip_Bait_Bug" "Config/DefaultGame.ini" "formal bait asset entry"
    Assert-TextPattern "Equip_Chum_Bug" "Config/DefaultGame.ini" "formal chum asset entry"
    Assert-NoTextPattern "/Game/Data/Equipment/DA_.*_Basic|Rod_Basic|Bait_Basic|Chum_Basic|FakeBait_Giant" "Config/DefaultGame.ini" "legacy Basic equipment or fake bait id in runtime config"
    Assert-NoBinaryTextPattern "FakeBait_|/Game/Data/Equipment/DA_.*_Basic|Rod_Basic|Bait_Basic|Chum_Basic" "Content/Catfishing/Data/Equipment" "legacy Basic equipment or fake bait id in formal equipment assets"
    Assert-TextPattern "FreeBugBaitClaim" "Config/DefaultGame.ini" "free formal bait shop entry"
    Assert-TextPattern "FreeBugBaitClaim" "Source/Catfishing/ShopEconomy/Tests/CatShopEconomySettingsTests.cpp" "shop settings free bait expectation"
    Assert-TextPattern "DirectClientGrantDisabled" "Source/Catfishing/Framework/Game/CatGameplayTypes.cpp" "direct client consumable grant disabled"
    Assert-TextPattern "EQUIPMENT_TEAMLIBRARY_SHOP_RUNTIME_PASS" "Scripts/verify_equipment_teamlibrary_shop_runtime.py" "runtime pass marker"
    Assert-TextPattern "Normal-bait Begin without inventory is rejected" "Source/Catfishing/Equipment/Tests/CatEquipmentFishingUseTests.cpp" "normal bait inventory gate test"
    Assert-TextPattern "Normal bait commit removes exactly one bait" "Source/Catfishing/Equipment/Tests/CatEquipmentFishingUseTests.cpp" "normal bait commit consumes inventory test"
    Assert-TextPattern "SubmitFishSale" "Source/Catfishing/ShopEconomy/CatShopOrderCoordinator.cpp" "fish sale coordinator"
    Assert-TextPattern "ConsumeFish" "Source/Catfishing/ShopEconomy/CatShopOrderCoordinator.cpp" "fish sale consumes Items first"
    Assert-TextPattern "ValidateFishSale" "Source/Catfishing/ShopEconomy/CatShopOrderCoordinator.cpp" "fish sale wallet precheck"
    Assert-TextPattern "ApplyFishSale" "Source/Catfishing/ShopEconomy/CatShopOrderCoordinator.cpp" "fish sale wallet apply"
    Assert-TextPattern "StolenEscrow" "Source/Catfishing/ShopEconomy/CatShopOrderCoordinator.cpp" "stolen escrow fail closed"
    Assert-TextPattern "ValidateTeamLibraryEquipFromAuthority" "Source/Catfishing/Equipment/CatEquipmentComponent.h" "team library equip precheck declaration"
    Assert-TextPattern "ValidateTeamLibraryEquipFromAuthority" "Source/Catfishing/Framework/Game/CatGameplayTypes.cpp" "team library equip precheck call"
    Assert-TextPattern "TakeInstance" "Source/Catfishing/Framework/Game/CatGameplayTypes.cpp" "team library take call"
    Assert-TextPattern "ServerPublishEquipmentUnlocks" "Source/Catfishing/Framework/Game/CatGameplayTypes.h" "profile unlock publish RPC"
    Assert-TextPattern "DOREPLIFETIME\(ThisClass, AuthorizedEquipmentUnlockIds\)" "Source/Catfishing/Framework/Game/CatGameplayTypes.cpp" "authorized unlock replication"
    Assert-TextPattern "RecordCommittedUnlock" "Source/Catfishing/Collection/CatRunImprintService.h" "unlock grant delivery entry"
    Assert-TextPattern "GetEquipmentUnlockSnapshot" "Source/Catfishing/Profile/CatProfileSubsystem.h" "profile unlock snapshot"
    Assert-TextPattern "FishSaleConsumesFishCreditsWalletAndReplays" "Source/Catfishing/ShopEconomy/Tests/CatShopOrderCoordinatorTests.cpp" "fish sale replay test"
    Assert-TextPattern "TakeFlowPrecheckProtectsLibraryBeforeEquip" "Source/Catfishing/Equipment/Tests/CatTeamEquipmentLibraryAdapterTests.cpp" "team library take order test"
    Assert-TextPattern "UnlockGrantAckAuthorizesEquipment" "Source/Catfishing/Collection/Tests/CatRunImprintServiceTests.cpp" "unlock grant ack test"
    Write-Host "EQUIPMENT_TEAMLIBRARY_SHOP_STATIC_PASS FormalConfig=True RejectsLegacyBasic=True RejectsLegacyAssetStrings=True NormalBaitInventoryGate=True"
}

function Invoke-ModuleBuild {
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
    if ($BuildOutputLog -and -not (Test-Path -LiteralPath $BuildOutputLog -PathType Leaf)) {
        throw ("CatfishingEditor build did not produce UBT log: {0}" -f $BuildOutputLog)
    }
    Write-Host ("EQUIPMENT_TEAMLIBRARY_SHOP_BUILD_PASS UBTLog={0} ExitCode=0" -f $BuildOutputLog)
}

function Invoke-AutomationBatch {
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
    $RunRoot = Join-Path $EvidenceRoot (Get-Date -Format "yyyyMMdd-HHmmss")
    $Batches = @()
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.ShopEconomy.OrderCoordinator"
        Name = "ShopOrderCoordinator"
        ExpectedTests = @(
            "Catfishing.Unit.ShopEconomy.OrderCoordinator.FishSaleConsumesFishCreditsWalletAndReplays",
            "Catfishing.Unit.ShopEconomy.OrderCoordinator.FishSalePrecheckFailureDoesNotConsumeFish"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.ShopEconomy.Service"
        Name = "ShopEconomyService"
        ExpectedTests = @(
            "Catfishing.Unit.ShopEconomy.Service.PurchaseStockWalletLedgerAndFreeBait",
            "Catfishing.Unit.ShopEconomy.Service.FishSaleCreditsWalletReplaysAndCloseRejectsNewCommands",
            "Catfishing.Unit.ShopEconomy.Service.RejectedRequestsReplayBeforeMutableState",
            "Catfishing.Unit.ShopEconomy.Service.ClosedShopRejectsEveryWriteEntryAndStillReplays"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.ShopEconomy.Settings.ProjectDefaultsExposeWalletCatalogAndFreeBait"
        Name = "ShopEconomySettings"
        ExpectedTests = @(
            "Catfishing.Unit.ShopEconomy.Settings.ProjectDefaultsExposeWalletCatalogAndFreeBait"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Equipment.TeamLibrary"
        Name = "EquipmentTeamLibrary"
        ExpectedTests = @(
            "Catfishing.Unit.Equipment.TeamLibrary.GrantTakeReplayAndClose",
            "Catfishing.Unit.Equipment.TeamLibrary.TakeFlowPrecheckProtectsLibraryBeforeEquip"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Equipment.FishingUse"
        Name = "EquipmentFishingUse"
        ExpectedTests = @(
            "Catfishing.Unit.Equipment.FishingUse.BeginIsAtomicExclusiveAndReplaySafe",
            "Catfishing.Unit.Equipment.FishingUse.BaitCommitAndReleaseAreIdempotent",
            "Catfishing.Unit.Equipment.FishingUse.WearSequenceIsAbsoluteMonotonicAndCommittedOnce",
            "Catfishing.Unit.Equipment.FishingUse.RodBreakOverridesWearAndCommitsZeroOnce",
            "Catfishing.Unit.Equipment.FishingUse.ActiveReservationBlocksLegacyMutationsAndProtectsReservedBait",
            "Catfishing.Unit.Equipment.FishingUse.DeferredBaitCommitPublishesExactlyOnce"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Equipment.ConsumableUse"
        Name = "EquipmentConsumableUse"
        ExpectedTests = @(
            "Catfishing.Unit.Equipment.ConsumableUse.BeginReservesExactQuantityWithoutPublishingSnapshot",
            "Catfishing.Unit.Equipment.ConsumableUse.CommitConsumesOnceAndReplayReturnsFrozenObservables",
            "Catfishing.Unit.Equipment.ConsumableUse.DeferredCommitIsInvisibleUntilIdempotentPublish",
            "Catfishing.Unit.Equipment.ConsumableUse.ReleasePreservesInventoryAndRejectsLateCommit",
            "Catfishing.Unit.Equipment.ConsumableUse.ActiveReservationBlocksConflictingMutations",
            "Catfishing.Unit.Equipment.ConsumableUse.FishingAndRunReservationsAreMutuallyExclusive",
            "Catfishing.Unit.Equipment.ConsumableUse.ReservedQuantityCannotBeDoubleSpentByLegacyConsume"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Equipment.Component.FishingFailureNoneIsIdempotentAndDoesNotPunish"
        Name = "EquipmentFailureBudget"
        ExpectedTests = @(
            "Catfishing.Unit.Equipment.Component.FishingFailureNoneIsIdempotentAndDoesNotPunish"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.PlayerEntry.FullLoop"
        Name = "FishingPlayerEntryFullLoop"
        ExpectedTests = @(
            "Catfishing.PlayerEntry.FullLoop"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Framework.ShopNetwork"
        Name = "FrameworkShopNetwork"
        ExpectedTests = @(
            "Catfishing.Unit.Framework.ShopNetwork.RpcsAreReliableAndSnapshotsReplicate"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Framework.PlayerState"
        Name = "FrameworkPlayerState"
        ExpectedTests = @(
            "Catfishing.Unit.Framework.PlayerState.PublicCollectionValidationAndStarterUnlockContract"
        )
    }
    $Batches += [pscustomobject]@{
        Filter = "Catfishing.Unit.Collection.RunImprintService.UnlockGrantAckAuthorizesEquipment"
        Name = "CollectionUnlockGrantAck"
        ExpectedTests = @(
            "Catfishing.Unit.Collection.RunImprintService.UnlockGrantAckAuthorizesEquipment"
        )
    }

    $FailedBatches = @()
    foreach ($Batch in $Batches) {
        try {
            Invoke-AutomationBatch $Batch.Filter $Batch.Name $RunRoot $Batch.ExpectedTests
        }
        catch {
            $FailedBatches += ("{0}: {1}" -f $Batch.Name, $_.Exception.Message)
            Write-Warning ("{0} automation batch failed; continuing so other fourth-module batches still leave evidence. {1}" -f $Batch.Name, $_.Exception.Message)
        }
    }
    if ($FailedBatches.Count -gt 0) {
        throw ("Automation batches failed:`n{0}" -f ($FailedBatches -join "`n"))
    }
}

function Invoke-ModuleRuntime {
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $Editor "Unreal Editor commandlet"
    Assert-ToolFile $RuntimeProbe "EquipmentTeamLibraryShop runtime probe"
    $RuntimeRoot = Join-Path $EvidenceRoot ("Runtime-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
    $LogFile = Join-Path $RuntimeRoot "Runtime.log"
    New-Item -ItemType Directory -Path $RuntimeRoot -Force | Out-Null
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
        "-ExecutePythonScript=$RuntimeProbe" `
        "-abslog=$LogFile"
    if ($LASTEXITCODE -ne 0) {
        throw ("EquipmentTeamLibraryShop runtime probe failed with exit code {0}" -f $LASTEXITCODE)
    }
    if (-not (Test-Path -LiteralPath $LogFile)) {
        throw "EquipmentTeamLibraryShop runtime probe did not produce a fresh log"
    }
    $LogText = Get-Content -LiteralPath $LogFile -Raw
    $PassCount = ([regex]::Matches($LogText, "EQUIPMENT_TEAMLIBRARY_SHOP_RUNTIME_PASS")).Count
    if ($PassCount -ne 1 -or $LogText -match "LogPython: Error") {
        throw ("EquipmentTeamLibraryShop runtime probe is not green: passMarkers={0} log={1}" -f $PassCount, $LogFile)
    }
}

try {
    switch ($Mode) {
        "Static" { Invoke-ModuleStaticCheck }
        "Build" { Invoke-ModuleBuild }
        "Automation" { Invoke-ModuleAutomation }
        "Runtime" { Invoke-ModuleRuntime }
        default { throw ("Unknown verification mode: {0}" -f $Mode) }
    }
}
finally {
    if ($TranscriptStarted) {
        Stop-Transcript | Out-Null
        Write-Output ("Evidence log: {0}" -f $TranscriptLogFile)
    }
}
