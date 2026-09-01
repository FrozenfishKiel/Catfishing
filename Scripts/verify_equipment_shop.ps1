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
$EvidenceRoot = Join-Path $ProjectRoot "Saved\Automation\EquipmentShop"
$RuntimeProbe = Join-Path $ProjectRoot "Scripts\verify_equipment_shop_runtime.py"
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
    Assert-TextPattern "EquipmentShop" ".harness/harness.json" "module harness"
    Assert-TextPattern "equipment_shop_static_check" ".harness/harness.json" "static verification entry"
    Assert-TextPattern "equipment_shop_build" ".harness/harness.json" "build verification entry"
    Assert-TextPattern "equipment_shop_automation" ".harness/harness.json" "automation verification entry"
    Assert-TextPattern "equipment_shop_runtime" ".harness/harness.json" "runtime verification entry"
    Assert-TextPattern "Equipment / Shop" "Docs/Development" "human progress entry"
    Assert-TextPattern "bAutoConfigureStarterLoadout=False" "Config/DefaultGame.ini" "developer starter auto loadout disabled"
    Assert-TextPattern "bAutoGrantStarterScoopNet=True" "Config/DefaultGame.ini" "temporary starter scoop grant is explicitly enabled"
    Assert-TextPattern "bAutoGrantStarterScoopNet" "Docs/Development" "temporary starter scoop grant is tracked in the human progress entry"
    Assert-TextPattern "Equip_Rod_StarterT1" "Config/DefaultGame.ini" "formal starter rod asset entry"
    Assert-TextPattern "Equip_Rod_ShopT2" "Config/DefaultGame.ini" "formal glass fiber rod asset entry"
    Assert-TextPattern 'DISPLAY_NAME = "玻璃纤维竿"' "Scripts/configure_glass_fiber_rod.py" "ShopRodT2 is authored as the glass fiber rod"
    Assert-TextPattern "GlassRod=ShopRodT2" "Scripts/verify_equipment_shop_runtime.py" "runtime verifies the glass fiber rod identity"
    Assert-TextPattern "Equip_ScoopNet_Starter" "Config/DefaultGame.ini" "formal starter scoop asset entry"
    Assert-TextPattern "Equip_Bait_Bug" "Config/DefaultGame.ini" "formal bait asset entry"
    Assert-TextPattern "Equip_Chum_Bug" "Config/DefaultGame.ini" "formal chum asset entry"
    Assert-NoTextPattern "/Game/Data/Equipment/DA_.*_Basic|Rod_Basic|Bait_Basic|Chum_Basic|FakeBait_Giant" "Config/DefaultGame.ini" "legacy Basic equipment or fake bait id in runtime config"
    Assert-NoBinaryTextPattern "FakeBait_|/Game/Data/Equipment/DA_.*_Basic|Rod_Basic|Bait_Basic|Chum_Basic" "Content/Catfishing/Data/Equipment" "legacy Basic equipment or fake bait id in formal equipment assets"
    Assert-NoTextPattern "CatTeamEquipment|TeamLibrary" "Source/Catfishing" "removed legacy team equipment library source semantics"
    Assert-NoTextPattern "EquipmentTeamLibraryShop|equipment_teamlibrary_shop|verify_equipment_teamlibrary_shop|EQUIPMENT_TEAMLIBRARY_SHOP" ".harness" "removed team equipment library harness semantics"
    Assert-ToolFile (Join-Path $ProjectRoot "Content\Catfishing\Data\Shop\DT_ShopCatalog_Default.uasset") "formal shop catalog DataTable"
    Assert-TextPattern "DefaultShopCatalogTable=/Game/Catfishing/Data/Shop/DT_ShopCatalog_Default" "Config/DefaultGame.ini" "formal shop catalog DataTable setting"
    Assert-TextPattern "Definition \? Definition->Thumbnail" "Source/Catfishing/UI/Shop/CatShopModel.cpp" "shop icon falls back to the equipment definition thumbnail"
    Assert-TextPattern "Event=ui_shop_icon_missing" "Source/Catfishing/UI/Shop/CatShopModel.cpp" "missing shop icons remain diagnosable in Development logs"
    Assert-NoTextPattern "UCatShopCatalogDefinition|FixedSaleEntries|RandomSaleEntries" "Source/Catfishing/ShopEconomy" "removed legacy shop catalog sources"
    Assert-NoTextPattern "ServerGrantRunConsumable|DirectClientGrantDisabled" "Source/Catfishing/Framework/Game" "direct client quantity grant RPC removed"
    Assert-TextPattern "EQUIPMENT_SHOP_RUNTIME_PASS" "Scripts/verify_equipment_shop_runtime.py" "runtime pass marker"
    Assert-TextPattern "GetInventoryItemQuantity\(BaitDefinitionId\) <= GetPendingReservedFishingBaitCount" "Source/Catfishing/Equipment/CatEquipmentComponent.cpp" "fishing bait begin requires an unreserved inventory item"
    Assert-TextPattern "RemoveInventoryItemQuantity\(Record->BaitDefinitionId, 1\)" "Source/Catfishing/Equipment/CatEquipmentComponent.cpp" "fishing bait commit consumes exactly one inventory item"
    Assert-TextPattern "SubmitFishSale" "Source/Catfishing/ShopEconomy/CatShopOrderCoordinator.cpp" "fish sale coordinator"
    Assert-TextPattern "ConsumeFish" "Source/Catfishing/ShopEconomy/CatShopOrderCoordinator.cpp" "fish sale consumes Items first"
    Assert-TextPattern "ValidateFishSale" "Source/Catfishing/ShopEconomy/CatShopOrderCoordinator.cpp" "fish sale wallet precheck"
    Assert-TextPattern "ApplyFishSale" "Source/Catfishing/ShopEconomy/CatShopOrderCoordinator.cpp" "fish sale wallet apply"
    Assert-TextPattern "StolenEscrow" "Source/Catfishing/ShopEconomy/CatShopOrderCoordinator.cpp" "stolen escrow fail closed"
    Assert-TextPattern "ValidateAddItemsFromAuthority" "Source/Catfishing/ShopEconomy/CatShopOrderCoordinator.cpp" "cart prechecks camp inventory before payment"
    Assert-TextPattern "AddItemsFromAuthority" "Source/Catfishing/ShopEconomy/CatShopOrderCoordinator.cpp" "cart delivers into camp inventory"
    Assert-TextPattern "ResolvePublicInventoryForShopOrder" "Source/Catfishing/Framework/Game/CatGameplayTypes.cpp" "server resolves camp public inventory for shop orders"
    Assert-TextPattern "WithdrawToEquipmentFromAuthority" "Source/Catfishing/Camp/CatCampInventoryActor.cpp" "players withdraw camp items into personal equipment"
    Assert-TextPattern "UseActorClass" "Source/Catfishing/Fishing/CatFishingService.cpp" "deployed rods use the purchased item definition actor class"
    Assert-TextPattern "ServerPublishEquipmentUnlocks" "Source/Catfishing/Framework/Game/CatGameplayTypes.h" "profile unlock publish RPC"
    Assert-TextPattern "DOREPLIFETIME\(ThisClass, AuthorizedEquipmentUnlockIds\)" "Source/Catfishing/Framework/Game/CatGameplayTypes.cpp" "authorized unlock replication"
    Assert-TextPattern "RecordCommittedUnlock" "Source/Catfishing/Collection/CatRunImprintService.h" "unlock grant delivery entry"
    Assert-TextPattern "GetEquipmentUnlockSnapshot" "Source/Catfishing/Profile/CatProfileSubsystem.h" "profile unlock snapshot"
    Write-Host "EQUIPMENT_SHOP_STATIC_PASS FormalConfig=True GlassRod=ShopRodT2 ShopCatalogDataTable=True CampInventoryDelivery=True RejectsLegacyCatalog=True NormalBaitInventoryGate=True"
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
    Write-Host ("EQUIPMENT_SHOP_BUILD_PASS UBTLog={0} ExitCode=0" -f $BuildOutputLog)
}

function Invoke-ModuleAutomation {
    throw 'EquipmentShop Automation 暂停：旧个人直发测试套件已经删除，必须为“购物车支付→营地公共仓库→取入随身 Equipment”重建端到端测试后才能恢复本模式。'
}

function Invoke-ModuleRuntime {
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $Editor "Unreal Editor commandlet"
    Assert-ToolFile $RuntimeProbe "EquipmentShop runtime probe"
    $RuntimeRoot = Join-Path $EvidenceRoot ("Runtime-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
    $LogFile = Join-Path $RuntimeRoot "Runtime.log"
    New-Item -ItemType Directory -Path $RuntimeRoot -Force | Out-Null
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
        "-ExecutePythonScript=$RuntimeProbe" `
        "-abslog=$LogFile"
    if ($LASTEXITCODE -ne 0) {
        throw ("EquipmentShop runtime probe failed with exit code {0}" -f $LASTEXITCODE)
    }
    if (-not (Test-Path -LiteralPath $LogFile)) {
        throw "EquipmentShop runtime probe did not produce a fresh log"
    }
    $LogText = Get-Content -LiteralPath $LogFile -Raw
    $PassCount = ([regex]::Matches($LogText, "EQUIPMENT_SHOP_RUNTIME_PASS")).Count
    if ($PassCount -ne 1 -or $LogText -match "LogPython: Error") {
        throw ("EquipmentShop runtime probe is not green: passMarkers={0} log={1}" -f $PassCount, $LogFile)
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
