param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Static", "Build", "WBPCreate", "Automation", "Runtime")]
    [string]$Mode
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ProjectFile = Join-Path $ProjectRoot "Catfishing.uproject"
$EngineRoot = if ($env:UE_ROOT) { $env:UE_ROOT } else { "D:\UE_5.8" }
$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$DotNet = Join-Path $EngineRoot "Engine\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe"
$UnrealBuildTool = Join-Path $EngineRoot "Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll"
$EvidenceRoot = Join-Path $ProjectRoot "Saved\Automation\UIReach"
$RuntimeProbe = Join-Path $ProjectRoot "Scripts\verify_ui_reach_runtime.py"

function Assert-ToolFile {
    <#
    验证项目文件或外部工具真实存在。
    各模式在启动构建、Editor 或 Runtime 探针前统一调用它；缺失时立即失败，避免旧日志被误当成本轮 UIReach 证据。
    #>
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw ("{0} not found: {1}" -f $Description, $Path)
    }
}

function Assert-NoToolFile {
    <#
    验证旧源码文件已经从项目中移除。
    Static 模式用它防止已拆掉的一体化 UI 再次进入构建，而不是只检查旧函数名不出现。
    #>
    param([string]$Path, [string]$Description)
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        throw ("removed file still exists: {0} path={1}" -f $Description, $Path)
    }
}

function Assert-TextPattern {
    <#
    对一个项目文件执行稳定文本合同检查。
    Static 模式只用它证明 UIReach 的原子模块合同和关键代码入口仍在同一条链路上，不把文本命中冒充构建或运行成功。
    #>
    param([string]$Pattern, [string]$RelativePath, [string]$Description)
    $Ripgrep = Get-Command rg -ErrorAction SilentlyContinue
    if (-not $Ripgrep) {
        throw "rg is required for UIReach static verification"
    }
    & $Ripgrep.Source -n $Pattern (Join-Path $ProjectRoot $RelativePath)
    if ($LASTEXITCODE -ne 0) {
        throw ("UIReach static contract missing: {0} pattern={1} path={2}" -f $Description, $Pattern, $RelativePath)
    }
}

function Assert-NoTextPattern {
    <#
    对一个项目文件执行禁止模式检查。
    Static 模式用它防止 UIReach 重新退回 C++ 构造玩家白盒界面或在 WBP 缺失时创建原生替身。
    #>
    param([string]$Pattern, [string]$RelativePath, [string]$Description)
    $Ripgrep = Get-Command rg -ErrorAction SilentlyContinue
    if (-not $Ripgrep) {
        throw "rg is required for UIReach static verification"
    }
    & $Ripgrep.Source -n $Pattern (Join-Path $ProjectRoot $RelativePath)
    if ($LASTEXITCODE -eq 0) {
        throw ("UIReach forbidden static contract found: {0} pattern={1} path={2}" -f $Description, $Pattern, $RelativePath)
    }
    if ($LASTEXITCODE -ne 1) {
        throw ("UIReach forbidden static contract check failed: {0} pattern={1} path={2}" -f $Description, $Pattern, $RelativePath)
    }
}

function Report-KnownLegacyAsset {
    <#
    报告仍需通过编辑器清理的旧二进制资产。
    Static 模式不直接删除 UAsset；这里把风险显式写入输出，避免旧 LakeReach 源码已清而资产边界被误读成全清。
    #>
    param([string]$Path, [string]$Description)
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        Write-Warning ("legacy binary asset still requires editor-side cleanup: {0} path={1}" -f $Description, $Path)
    }
}

function Invoke-UIReachStaticCheck {
    <#
    执行拆分 UI 模块静态合同检查。
    该模式只核对源码边界和资产生成入口：HUD、Inventory、InventorySlot、Shop、Interaction、Collection 必须独立，Shop 不由 LocalPlayer 预建，输入仍来自项目既有 InputContext。
    #>
    Assert-ToolFile (Join-Path $ProjectRoot "Content\Input\InputAction\IA_LakeMenu.uasset") "UIReach menu Input Action asset"
    Assert-ToolFile (Join-Path $ProjectRoot "Content\Input\InputAction\IA_Interact.uasset") "Interaction confirm Input Action asset"
    Assert-ToolFile (Join-Path $ProjectRoot "Content\Input\InputContext\IMC_InputContext.uasset") "project InputContext asset"
    Assert-TextPattern "UCatHUDWidget" "Source/Catfishing/UI/HUD/CatHUDWidget.h" "HUD Widget class"
    Assert-TextPattern "UCatHUDModel" "Source/Catfishing/UI/HUD/CatHUDModel.h" "HUD Model class"
    Assert-TextPattern "CatStatusTextBlock" "Source/Catfishing/UI/HUD/CatHUDWidget.h" "HUD text block wiring"
    Assert-TextPattern "UCatInventoryWidget" "Source/Catfishing/UI/Inventory/CatInventoryWidget.h" "Inventory Widget class"
    Assert-TextPattern "UCatInventoryModel" "Source/Catfishing/UI/Inventory/CatInventoryModel.h" "Inventory Model class"
    Assert-TextPattern "UCatInventoryPageController" "Source/Catfishing/UI/Inventory/CatInventoryPageController.h" "Inventory PageController class"
    Assert-TextPattern "InventorySlotWrapBox" "Source/Catfishing/UI/Inventory/CatInventoryWidget.h" "Inventory WrapBox slot container"
    Assert-TextPattern "CreateWidget<UCatInventorySlotWidget>" "Source/Catfishing/UI/Inventory/CatInventoryWidget.cpp" "Inventory creates one WBP per slot"
    Assert-TextPattern "UCatInventorySlotWidget" "Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.h" "Inventory Slot Widget class"
    Assert-TextPattern "NativeOnMouseButtonDown" "Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.h" "slot mouse override"
    Assert-TextPattern "NativeOnDragDetected" "Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.h" "slot drag override"
    Assert-TextPattern "NativeOnDrop" "Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.h" "slot drop override"
    Assert-TextPattern "UCatInventoryDragDropOperation" "Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.h" "slot drag payload is stable DTO"
    Assert-TextPattern "OnSlotDropRequested" "Source/Catfishing/UI/Inventory/CatInventoryWidget.h" "Inventory forwards slot drop intent"
    Assert-TextPattern "MoveObjectBetweenContainers" "Source/Catfishing/UI/Inventory/CatInventoryTypes.h" "Inventory has real slot-based object move action"
    Assert-TextPattern "MoveInventoryItem" "Source/Catfishing/UI/Inventory/CatInventoryTypes.h" "Inventory has real slot-based personal inventory move action"
    Assert-TextPattern "InventoryObject" "Source/Catfishing/UI/Inventory/CatInventoryTypes.h" "Inventory projects personal inventory slots as first-class slots"
    Assert-TextPattern "InventorySlots" "Source/Catfishing/UI/Inventory/CatInventoryModel.cpp" "Inventory builds visible slots from the unified inventory array"
    Assert-TextPattern "MakeInventorySlotView" "Source/Catfishing/UI/Inventory/CatInventoryModel.cpp" "Inventory builds visible personal inventory slot views from Equipment"
    Assert-TextPattern "ServerMoveInventorySlot" "Source/Catfishing/Framework/Game/CatGameplayTypes.h" "personal inventory slot move reaches PlayerController RPC"
    Assert-TextPattern "InventoryObject" "Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.cpp" "Inventory drag/drop supports personal inventory slots separately from container slots"
    Assert-TextPattern "Containers" "Source/Catfishing/UI/Inventory/CatInventoryTypes.h" "Inventory projects a list of containers instead of a single fish guard"
    Assert-TextPattern "SetExternalContainerContexts" "Source/Catfishing/UI/Inventory/CatInventoryModel.h" "Inventory accepts extensible external container context"
    Assert-TextPattern "SourceContainerSlotIndex" "Source/Catfishing/Framework/Game/CatGameplayTypes.h" "slot move RPC carries source container slot"
    Assert-TextPattern "ServerTransferObjectBetweenContainers" "Source/Catfishing/Framework/Game/CatGameplayTypes.h" "slot move reaches PlayerController RPC"
    Assert-TextPattern "TransferContainedObject" "Source/Catfishing/Items/CatItemsService.cpp" "slot move uses Items object transfer seam"
    Assert-NoToolFile (Join-Path $ProjectRoot "Source\Catfishing\UI\CatLakeReachWidget.h") "removed LakeReach widget header"
    Assert-NoToolFile (Join-Path $ProjectRoot "Source\Catfishing\UI\CatLakeReachWidget.cpp") "removed LakeReach widget source"
    Assert-NoToolFile (Join-Path $ProjectRoot "Source\Catfishing\UI\CatLakeReachModel.h") "removed LakeReach model header"
    Assert-NoToolFile (Join-Path $ProjectRoot "Source\Catfishing\UI\CatLakeReachModel.cpp") "removed LakeReach model source"
    Assert-NoToolFile (Join-Path $ProjectRoot "Source\Catfishing\UI\CatLakeReachPageController.h") "removed LakeReach page controller header"
    Assert-NoToolFile (Join-Path $ProjectRoot "Source\Catfishing\UI\CatLakeReachPageController.cpp") "removed LakeReach page controller source"
    Assert-NoToolFile (Join-Path $ProjectRoot "Source\Catfishing\UI\Tests\CatLakeReachWidgetAssetTests.cpp") "removed LakeReach WBP asset automation"
    Report-KnownLegacyAsset (Join-Path $ProjectRoot "Content\UI\WBP_CatLakeReach.uasset") "old LakeReach WBP asset"
    Assert-NoTextPattern "CatLakeReach|WBP_CatLakeReach|LakeReachWidgetClass|bEnableLakeReachView|LoadLakeReach|ToggleLakeMenu|IsLakeMenuOpen|AttachLakePawn|DetachLakePawn" "Source/Catfishing" "source must not keep the removed one-page LakeReach UI entry"
    Assert-NoTextPattern "TransferSelectedFishToTank" "Source/Catfishing/UI/Inventory/CatInventoryTypes.h" "Inventory must not expose old one-off fish tank action"
    Assert-NoTextPattern "TransferFishToTankButton" "Source/Catfishing/UI/Inventory/CatInventoryWidget.h" "Inventory must not keep old one-off fish tank button"
    Assert-NoTextPattern "TransferFishToTankButton" "Source/Catfishing/UI/Tests/CatUIModuleWidgetAssetTests.cpp" "split Inventory WBP create must not regenerate old one-off fish tank button"
    Assert-NoTextPattern "ServerTransferFishToTank" "Source/Catfishing/Framework/Game/CatGameplayTypes.h" "PlayerController must not expose old one-off fish tank RPC"
    Assert-NoTextPattern "ServerTransferFishBetweenContainers" "Source/Catfishing/Framework/Game/CatGameplayTypes.h" "PlayerController cross-container RPC must be object-based, not fish-only"
    Assert-NoTextPattern "UButton" "Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.h" "Inventory slot must not be a Button"
    Assert-TextPattern "UCatShopWidget" "Source/Catfishing/UI/Shop/CatShopWidget.h" "Shop Widget class"
    Assert-TextPattern "UCatShopModel" "Source/Catfishing/UI/Shop/CatShopModel.h" "Shop Model class"
    Assert-TextPattern "UCatShopPageController" "Source/Catfishing/UI/Shop/CatShopPageController.h" "Shop PageController class"
    Assert-TextPattern "UCatShopInteractionComponent" "Source/Catfishing/UI/Shop/CatShopInteractionComponent.h" "Shop opened by interactable object component"
    Assert-TextPattern "ACatShopKioskActor" "Source/Catfishing/ShopEconomy/CatShopKioskActor.h" "placeable shop interaction actor"
    Assert-TextPattern "CreateWidget<UCatShopWidget>" "Source/Catfishing/UI/Shop/CatShopInteractionComponent.cpp" "Shop Widget created by interaction object"
    Assert-NoTextPattern "CreateWidget<UCatShopWidget>" "Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp" "LocalPlayer must not precreate Shop"
    Assert-TextPattern "ServerSubmitShopPurchase" "Source/Catfishing/UI/Shop/CatShopPageController.cpp" "shop purchase reaches PlayerController RPC"
    Assert-TextPattern "ServerClaimFreeShopEntry" "Source/Catfishing/UI/Shop/CatShopPageController.cpp" "shop free claim reaches PlayerController RPC"
    Assert-TextPattern "UCatInteractionPromptWidget" "Source/Catfishing/UI/Interaction/CatInteractionPromptWidget.h" "Interaction prompt Widget class"
    Assert-TextPattern "UCatInteractable" "Source/Catfishing/Interaction/CatInteractable.h" "generic actor interaction interface"
    Assert-TextPattern "public ICatInteractable" "Source/Catfishing/ShopEconomy/CatShopKioskActor.h" "shop actor implements generic interaction interface"
    Assert-TextPattern "public ICatInteractable" "Source/Catfishing/Items/CatFishTankActor.h" "container actor implements generic interaction interface"
    Assert-TextPattern "public ICatInteractable" "Source/Catfishing/Items/CatFishGuardActor.h" "ground fish guard actor implements generic interaction interface"
    Assert-TextPattern "public ICatInteractable" "Source/Catfishing/Items/World/CatFishPickupActor.h" "dead fish actor implements generic interaction interface"
    Assert-TextPattern "ECatFishPickupState::Carried" "Source/Catfishing/Items/World/CatFishPickupActor.cpp" "dead fish E interaction enters mouth-carried world state"
    Assert-TextPattern "MouthCarrySocketName" "Source/Catfishing/Items/World/CatFishPickupActor.cpp" "mouth carry uses configured cat mesh socket"
    Assert-TextPattern "MouthCarrySocketName=RigTongue3" "Config/DefaultGame.ini" "mouth carry uses the formal tongue bone without actor offset"
    Assert-TextPattern "ApplyCarriedVisualTransform" "Source/Catfishing/Items/World/CatFishPickupActor.cpp" "mouth carry clears landed-only mesh placement"
    Assert-TextPattern "ApplyLandedVisualTransform" "Source/Catfishing/Items/World/CatFishPickupActor.cpp" "available fish restores landed-only mesh placement"
    Assert-TextPattern "StoreInFishGuardFromAuthority" "Source/Catfishing/Items/CatFishGuardActor.cpp" "interacted ground fish guard stores the carried fish into itself"
    Assert-NoTextPattern "PersonalFishGuard" "Source/Catfishing/UI/Inventory" "inventory must not project a character-owned fish guard"
    Assert-NoTextPattern "CatInteractionTargetComponent|CatInteractionWidget|CatFishGuardInteractionComponent" "Source/Catfishing" "removed component/widget interaction routes must not return"
    Assert-TextPattern "UCatInteractionPageController" "Source/Catfishing/UI/Interaction/CatInteractionPageController.h" "interaction prompt controller"
    Assert-TextPattern "LoadInteractionConfirmAction" "Source/Catfishing/UI/CatUISettings.cpp" "interaction confirm action loader"
    Assert-TextPattern "ResolveInteractionConfirmKeyName" "Source/Catfishing/UI/CatUISettings.cpp" "interaction key resolved from existing IMC"
    Assert-TextPattern "InteractWithFocusedTarget" "Source/Catfishing/UI/Interaction/CatInteractionPageController.cpp" "confirm key reaches focused target"
    Assert-NoTextPattern "BindAction" "Source/Catfishing/UI/Interaction/CatInteractionPageController.cpp" "interaction prompt controller must not bind E a second time"
    Assert-TextPattern "Execute_GetInteractionRadius" "Source/Catfishing/Framework/Game/CatGameplayTypes.cpp" "server reach gate reuses actor interaction radius when available"
    Assert-TextPattern "UCatCollectionWidget" "Source/Catfishing/UI/Collection/CatCollectionWidget.h" "Collection Widget class"
    Assert-TextPattern "UCatCollectionModel" "Source/Catfishing/UI/Collection/CatCollectionModel.h" "Collection Model class"
    Assert-TextPattern "LoadInventoryToggleAction" "Source/Catfishing/UI/CatUISettings.cpp" "Inventory input action loader"
    Assert-TextPattern "LoadGameplayInputMappingContext" "Source/Catfishing/UI/CatUISettings.cpp" "existing InputContext loader"
    Assert-TextPattern "ResolveInventoryToggleKeyName" "Source/Catfishing/UI/CatUISettings.cpp" "inventory key resolved from existing IMC"
    Assert-NoTextPattern "IMC_LakeMenu" "Source/Catfishing/UI/CatUISettings.cpp" "UI Settings must use existing InputContext instead of a duplicate menu IMC"
    Assert-NoTextPattern "IMC_LakeMenu" "Source/Catfishing/UI/Tests/CatUIModuleWidgetAssetTests.cpp" "WBP create must not generate a duplicate menu IMC"
    Assert-NoTextPattern "NewObject<UInputAction>" "Source/Catfishing/UI/Inventory/CatInventoryPageController.cpp" "Inventory PageController must not create runtime InputAction"
    Assert-NoTextPattern "MapKey\(" "Source/Catfishing/UI/Inventory/CatInventoryPageController.cpp" "Inventory PageController must not hard-code key mappings"
    Assert-TextPattern "CreateWidget<UCatHUDWidget>" "Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp" "LocalPlayer creates HUD only"
    Assert-TextPattern "CreateWidget<UCatInventoryWidget>" "Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp" "LocalPlayer creates inventory shell"
    Assert-TextPattern "UCatInteractionPageController" "Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp" "LocalPlayer owns generic interaction controller"
    Assert-TextPattern "ShopPrecreated=false" "Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp" "LocalPlayer explicitly does not precreate shop"
    Assert-TextPattern "FCatContainerSnapshot" "Source/Catfishing/UI/Inventory/CatInventoryTypes.h" "Inventory view comes from container snapshot"
    Assert-TextPattern "Capacity" "Source/Catfishing/Items/CatItemTypes.h" "container snapshot exposes backend capacity"
    Assert-TextPattern "Catfishing.Editor.UIModules.CreateFormalWBPAssets" "Source/Catfishing/UI/Tests/CatUIModuleWidgetAssetTests.cpp" "split WBP asset generation automation"
    Assert-TextPattern "CREATE_UI_MODULE_WBPS_PASS" "Source/Catfishing/UI/Tests/CatUIModuleWidgetAssetTests.cpp" "split WBP asset pass marker"
    Assert-TextPattern "IA_Interact" "Source/Catfishing/UI/Tests/CatUIModuleWidgetAssetTests.cpp" "interaction input action generated into existing IMC"
}

function Invoke-UIReachBuild {
    <#
    构建当前 CatfishingEditor 目标。
    这是 UIReach 的新鲜二进制证据；UBT 非零时直接失败，不沿用其他模块或旧构建结果。
    #>
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $DotNet "Unreal bundled dotnet"
    Assert-ToolFile $UnrealBuildTool "Unreal Build Tool"
    $BuildRoot = Join-Path $EvidenceRoot ("Build-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
    $LogFile = Join-Path $BuildRoot "Build.log"
    New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null
    "UIReach Build started at $(Get-Date -Format o)" | Set-Content -LiteralPath $LogFile -Encoding UTF8
    $BuildOutput = & $DotNet $UnrealBuildTool CatfishingEditor Win64 Development $ProjectFile -WaitMutex -NoHotReload -NoUBA 2>&1
    $BuildExitCode = $LASTEXITCODE
    $BuildText = (($BuildOutput | ForEach-Object { [string]$_ }) -join [Environment]::NewLine).Replace([string][char]0, "")
    $BuildText | Add-Content -LiteralPath $LogFile -Encoding UTF8
    Write-Output $BuildText
    if ($BuildExitCode -ne 0) {
        throw ("CatfishingEditor build failed with exit code {0}" -f $BuildExitCode)
    }
}

function Invoke-UIReachWBPCreate {
    <#
    创建或刷新拆分后的正式 UI WBP 资产。
    该模式只运行 UI 模块的 Editor 资产自动化，并要求报告证明 HUD、背包、格子、商店、交互提示和图鉴六个 WBP 保存成功。
    #>
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $Editor "Unreal Editor commandlet"
    $RunRoot = Join-Path $EvidenceRoot ("CreateWBP-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
    $ReportRoot = Join-Path $RunRoot "Report"
    $LogFile = Join-Path $RunRoot "CreateWBP.log"
    New-Item -ItemType Directory -Path $RunRoot -Force | Out-Null
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
        "-ExecCmds=Automation RunTests Catfishing.Editor.UIModules.CreateFormalWBPAssets;Quit" `
        "-TestExit=Automation Test Queue Empty" `
        "-ReportExportPath=$ReportRoot" `
        "-abslog=$LogFile"
    if ($LASTEXITCODE -ne 0) {
        throw ("UIReach WBP create automation failed with exit code {0}" -f $LASTEXITCODE)
    }
    $IndexFile = Join-Path $ReportRoot "index.json"
    if (-not (Test-Path -LiteralPath $IndexFile) -or -not (Test-Path -LiteralPath $LogFile)) {
        throw "UIReach WBP create did not produce a fresh report and log"
    }
    Assert-AutomationReport -IndexFile $IndexFile -LogFile $LogFile -ExpectedTests @(
        "Catfishing.Editor.UIModules.CreateFormalWBPAssets"
    )
    # 拆分模块和关键控件名是生成脚本与正式 WBP 之间的最小握手信号。
    # 这里不检查美术细节，只防止仍生成旧总入口或漏掉背包格子/商店/提示模块。
    $LogText = Get-Content -LiteralPath $LogFile -Raw
    if ($LogText -notmatch "CREATE_UI_MODULE_WBPS_PASS" -or $LogText -notmatch "InventorySlotRoot=UserWidgetNotButton" -or $LogText -notmatch "SlotContainer=InventorySlotWrapBox" -or $LogText -notmatch "InventoryEquipmentText=EquipmentTextBlock" -or $LogText -notmatch "InventoryItemsText=InventoryItemsTextBlock" -or $LogText -notmatch "ShopOwner=InteractionObject" -or $LogText -notmatch "ShopKiosk=/Game/ShopEconomy/BP_CatShopKiosk" -or $LogText -notmatch "InteractAction=/Game/Input/InputAction/IA_Interact" -or $LogText -notmatch "InteractContext=/Game/Input/InputContext/IMC_InputContext" -or $LogText -notmatch "InteractKey=E" -or $LogText -match "EnsureFailed|LogPython: Error") {
        throw ("UIReach WBP create log is not green: {0}" -f $LogFile)
    }
}

function Assert-AutomationReport {
    <#
    核验 UIReach 自动化报告的结构化结果。
    报告必须只包含成功或允许的已知 warning，并且每个必需用例都精确出现一次；缺测试、失败、未运行或新增 warning 都会让对应模式失败。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [string]$IndexFile,
        [Parameter(Mandatory = $true)]
        [string]$LogFile,
        [Parameter(Mandatory = $true)]
        [string[]]$ExpectedTests,
        [hashtable]$AllowedWarningMessagesByTest = @{}
    )
    $Report = Get-Content -LiteralPath $IndexFile -Raw | ConvertFrom-Json
    $Tests = @($Report.tests)
    $NonSuccess = @($Tests | Where-Object { ($_.state -ne "Success" -and $_.state -ne "SucceededWithWarnings") -or [int]$_.errors -ne 0 })
    if ($NonSuccess.Count -gt 0) {
        throw ("UIReach automation has non-success tests: {0}" -f ($NonSuccess.fullTestPath -join ", "))
    }
    $AllowedWarningTests = @()
    $UnexpectedWarnings = @()
    foreach ($Test in $Tests) {
        $TestPath = [string]$Test.fullTestPath
        $WarningEntries = @($Test.entries | Where-Object { $_.event.type -eq "Warning" })
        if ([int]$Test.warnings -le 0 -and $Test.state -ne "SucceededWithWarnings") {
            continue
        }
        $AllowedPatterns = @()
        if ($AllowedWarningMessagesByTest.ContainsKey($TestPath)) {
            $AllowedPatterns = @($AllowedWarningMessagesByTest[$TestPath])
        }
        if ($AllowedPatterns.Count -le 0 -or [int]$Test.warnings -gt $WarningEntries.Count) {
            $UnexpectedWarnings += ("{0} warnings={1}" -f $TestPath, [int]$Test.warnings)
            continue
        }
        foreach ($WarningEntry in $WarningEntries) {
            $WarningMessage = [string]$WarningEntry.event.message
            $MatchedAllowedWarning = $false
            foreach ($AllowedPattern in $AllowedPatterns) {
                if ($WarningMessage -match $AllowedPattern) {
                    $MatchedAllowedWarning = $true
                    break
                }
            }
            if (-not $MatchedAllowedWarning) {
                $UnexpectedWarnings += ("{0} warning={1}" -f $TestPath, $WarningMessage)
            }
        }
        if ($UnexpectedWarnings.Count -eq 0) {
            $AllowedWarningTests += $TestPath
        }
    }
    if ($UnexpectedWarnings.Count -gt 0) {
        throw ("UIReach automation has unexpected warnings: {0}" -f ($UnexpectedWarnings -join "; "))
    }
    if ([int]$Report.succeededWithWarnings -ne $AllowedWarningTests.Count) {
        throw ("UIReach automation warning summary mismatch: report={0} allowed={1}" -f [int]$Report.succeededWithWarnings, $AllowedWarningTests.Count)
    }
    foreach ($ExpectedTest in $ExpectedTests) {
        $Matches = @($Tests | Where-Object { $_.fullTestPath -eq $ExpectedTest })
        if ($Matches.Count -ne 1) {
            throw ("UIReach automation missing expected test {0}: matches={1} total={2}" -f $ExpectedTest, $Matches.Count, $Tests.Count)
        }
    }
    $LogText = Get-Content -LiteralPath $LogFile -Raw
    foreach ($ExpectedTest in $ExpectedTests) {
        if ($LogText -notmatch [regex]::Escape($ExpectedTest)) {
            throw ("UIReach automation log does not mention expected test {0}" -f $ExpectedTest)
        }
    }
}

function Invoke-UIReachAutomation {
    <#
    运行 UIReach 的同一批 UI 自动化。
    过滤器保持在 Catfishing.Unit.UI 下，防止把 HUD、菜单、鱼护和图鉴拆成多个可单独关闭的小批次；最终由报告同时证明全部关键用例成功。
    #>
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $Editor "Unreal Editor commandlet"
    $RunRoot = Join-Path $EvidenceRoot (Get-Date -Format "yyyyMMdd-HHmmss")
    $ReportRoot = Join-Path $RunRoot "Report"
    $LogFile = Join-Path $RunRoot "Automation.log"
    New-Item -ItemType Directory -Path $RunRoot -Force | Out-Null
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
        "-ExecCmds=Automation RunTests Catfishing.Unit.UI;Quit" `
        "-TestExit=Automation Test Queue Empty" `
        "-ReportExportPath=$ReportRoot" `
        "-abslog=$LogFile"
    if ($LASTEXITCODE -ne 0) {
        throw ("UIReach automation editor failed with exit code {0}" -f $LASTEXITCODE)
    }
    $IndexFile = Join-Path $ReportRoot "index.json"
    if (-not (Test-Path -LiteralPath $IndexFile) -or -not (Test-Path -LiteralPath $LogFile)) {
        throw "UIReach automation did not produce a fresh report and log"
    }
    # 这些用例分开守住两层边界：
    # Reach Widget 用例证明按钮只发 UI 意图；交互用例证明“吃鱼”意图能走到后端并把结果带回 Model。
    Assert-AutomationReport -IndexFile $IndexFile -LogFile $LogFile -ExpectedTests @(
        "Catfishing.Unit.UI.Settings.SplitPlayerModulesUseConfiguredWidgetClasses",
        "Catfishing.Unit.UI.TravelWidget.ClassAndOpaqueHandlesRemainViewOnly",
        "Catfishing.Unit.UI.LocalPlayerUISubsystem.IsLocalPlayerScopedAndNotGlobalSingleton",
        "Catfishing.Unit.UI.LocalPlayerUISubsystem.AttachesSplitPlayerModulesForPossessedCat",
        "Catfishing.Unit.UI.LocalPlayerUISubsystem.OnlineTravelWidgetStaysOutOfLake",
        "Catfishing.Unit.UI.FishingViewState.ProjectsReplicatedFactsWithoutGameplayObjects"
    )
}

function Invoke-UIReachRuntimeAttachAutomation {
    <#
    运行 UIReach Runtime 的 LocalPlayer 装配用例。
    该用例在命令行 Editor 内构造带 GameViewportClient 的 Game World，先证明关闭 gate 不装配半套模块，再显式开启 gate 触发拆分玩家 UI，并要求日志出现唯一 ui_player_modules_attached。
    #>
    param([string]$RuntimeRoot)
    $AttachRoot = Join-Path $RuntimeRoot "AttachAutomation"
    $ReportRoot = Join-Path $AttachRoot "Report"
    $LogFile = Join-Path $AttachRoot "Automation.log"
    New-Item -ItemType Directory -Path $AttachRoot -Force | Out-Null
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
        "-ExecCmds=Automation RunTests Catfishing.Unit.UI.LocalPlayerUISubsystem.AttachesSplitPlayerModulesForPossessedCat;Quit" `
        "-TestExit=Automation Test Queue Empty" `
        "-ReportExportPath=$ReportRoot" `
        "-abslog=$LogFile"
    if ($LASTEXITCODE -ne 0) {
        throw ("UIReach runtime attach automation failed with exit code {0}" -f $LASTEXITCODE)
    }
    $IndexFile = Join-Path $ReportRoot "index.json"
    if (-not (Test-Path -LiteralPath $IndexFile) -or -not (Test-Path -LiteralPath $LogFile)) {
        throw "UIReach runtime attach automation did not produce a fresh report and log"
    }
    Assert-AutomationReport -IndexFile $IndexFile -LogFile $LogFile -ExpectedTests @(
        "Catfishing.Unit.UI.LocalPlayerUISubsystem.AttachesSplitPlayerModulesForPossessedCat"
    )
    $LogText = Get-Content -LiteralPath $LogFile -Raw
    $AttachCount = ([regex]::Matches($LogText, "Event=ui_player_modules_attached")).Count
    if ($AttachCount -ne 1 -or $LogText -notmatch "HUD=WBP_CatHUD_C" -or $LogText -notmatch "Inventory=WBP_CatInventory_C" -or $LogText -notmatch "Interaction=WBP_CatInteractionPrompt_C") {
        throw ("UIReach runtime attach log is not green: attachMarkers={0} log={1}" -f $AttachCount, $LogFile)
    }
}

function Invoke-UIReachRuntime {
    <#
    在真实 Editor 命令行中执行 Lake 冷启动验证。
    流程先用 Python 只读探针检查正式 Lake、GameMode/Controller、UI Settings、拆分 UI C++ 基类和正式 WBP 类；再运行 LocalPlayer 装配用例要求 ui_player_modules_attached。
    #>
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $Editor "Unreal Editor commandlet"
    Assert-ToolFile $RuntimeProbe "UIReach runtime probe"
    $RuntimeRoot = Join-Path $EvidenceRoot ("Runtime-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
    $LogFile = Join-Path $RuntimeRoot "Runtime.log"
    New-Item -ItemType Directory -Path $RuntimeRoot -Force | Out-Null
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
        "-ExecutePythonScript=$RuntimeProbe" `
        "-abslog=$LogFile"
    if ($LASTEXITCODE -ne 0) {
        throw ("UIReach runtime probe failed with exit code {0}" -f $LASTEXITCODE)
    }
    if (-not (Test-Path -LiteralPath $LogFile)) {
        throw "UIReach runtime probe did not produce a fresh log"
    }
    $LogText = Get-Content -LiteralPath $LogFile -Raw
    $PassCount = ([regex]::Matches($LogText, "UI_REACH_RUNTIME_PASS")).Count
    if ($PassCount -ne 1 -or $LogText -match "LogPython: Error") {
        throw ("UIReach runtime probe is not green: passMarkers={0} log={1}" -f $PassCount, $LogFile)
    }
    Invoke-UIReachRuntimeAttachAutomation -RuntimeRoot $RuntimeRoot
}

switch ($Mode) {
    "Static" { Invoke-UIReachStaticCheck }
    "Build" { Invoke-UIReachBuild }
    "WBPCreate" { Invoke-UIReachWBPCreate }
    "Automation" { Invoke-UIReachAutomation }
    "Runtime" { Invoke-UIReachRuntime }
    default { throw ("Unknown verification mode: {0}" -f $Mode) }
}
