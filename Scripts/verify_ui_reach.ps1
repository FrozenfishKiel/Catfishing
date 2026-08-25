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

function Invoke-UIReachStaticCheck {
    <#
    执行 UIReach 原子模块静态合同检查。
    流程先解析 Harness 中的 UIReach 模块，再核对正式 WBP 前端、Model/PageController/View 基类、fail-closed 装配和统一鱼护/图鉴投影都没有被拆成独立交付入口。
    #>
    $HarnessPath = Join-Path $ProjectRoot ".harness/harness.json"
    $Harness = Get-Content -LiteralPath $HarnessPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not ($Harness.module_delivery.PSObject.Properties.Name -contains "UIReach")) {
        throw "UIReach module is missing from harness"
    }
    $Module = $Harness.module_delivery.UIReach
    if ($Module.delivery_mode -ne "single_atomic_module") {
        throw ("UIReach delivery_mode mismatch: {0}" -f $Module.delivery_mode)
    }
    foreach ($Facet in @("HUD", "Menu", "InventoryAndFishGuard", "Album")) {
        if (@($Module.facets) -notcontains $Facet) {
            throw ("UIReach facet missing from atomic module contract: {0}" -f $Facet)
        }
    }
    if ([string]::IsNullOrWhiteSpace($Module.tracking_rule) -or [string]::IsNullOrWhiteSpace($Module.completion_rule)) {
        throw "UIReach atomic tracking/completion rule is incomplete"
    }

    # Static 模式只证明“正式前端入口、只读绑定源和按钮意图链路还在”，不把文本命中说成玩家已经能点击成功。
    # 真正的点击到后端变化由 Automation 模式的鱼护交互用例负责兜底。
    Assert-TextPattern "UIReach" ".harness/harness.json" "UIReach harness entry"
    Assert-TextPattern "single_atomic_module" ".harness/harness.json" "atomic delivery mode"
    Assert-ToolFile (Join-Path $ProjectRoot "Content\UI\WBP_CatLakeReach.uasset") "UIReach formal WBP asset"
    Assert-TextPattern "UCatLakeReachModel" "Source/Catfishing/UI/CatLakeReachModel.h" "UIReach Model class"
    Assert-TextPattern "UCatLakeReachPageController" "Source/Catfishing/UI/CatLakeReachPageController.h" "UIReach PageController class"
    Assert-TextPattern "BP_RenderViewState" "Source/Catfishing/UI/CatLakeReachWidget.h" "WBP render event"
    Assert-TextPattern "BlueprintPoisonValue" "Source/Catfishing/UI/CatLakeReachWidget.h" "WBP Designer property-binding source values"
    Assert-TextPattern "WidgetBlueprint->Bindings" "Source/Catfishing/UI/Tests/CatLakeReachWidgetAssetTests.cpp" "formal WBP designer property bindings"
    Assert-TextPattern "LakeReachRootFrame" "Source/Catfishing/UI/Tests/CatLakeReachWidgetAssetTests.cpp" "formal WBP frontend frame"
    Assert-TextPattern "FishGuardActionPanel" "Source/Catfishing/UI/Tests/CatLakeReachWidgetAssetTests.cpp" "formal WBP fish guard action panel"
    Assert-TextPattern "BlueprintFishGuardActionVisibility" "Source/Catfishing/UI/Tests/CatLakeReachWidgetAssetTests.cpp" "formal WBP action-panel visibility binding"
    Assert-TextPattern "FCatUIReachFishCollectionEntry" "Source/Catfishing/UI/CatLakeReachWidget.h" "Blueprint-safe collection DTO"
    Assert-TextPattern "FishCollectionEntries" "Source/Catfishing/UI/CatLakeReachWidget.h" "Blueprint collection projection"
    Assert-TextPattern "ECatUIReachFishGuardAction" "Source/Catfishing/UI/CatLakeReachWidget.h" "fish guard action intent enum"
    Assert-TextPattern "OnFishGuardActionRequested" "Source/Catfishing/UI/CatLakeReachWidget.h" "fish guard action intent delegate"
    Assert-TextPattern "RequestConsumeSelectedFish" "Source/Catfishing/UI/CatLakeReachWidget.h" "consume fish UI intent"
    Assert-TextPattern "HandleViewFishGuardActionRequested" "Source/Catfishing/UI/CatLakeReachPageController.cpp" "PageController translates fish guard UI intent"
    Assert-TextPattern "ServerConsumeFish" "Source/Catfishing/UI/CatLakeReachPageController.cpp" "fish guard consume action reaches PlayerController server command"
    Assert-TextPattern "ServerTransferFishToTank" "Source/Catfishing/UI/CatLakeReachPageController.cpp" "fish guard transfer action reaches PlayerController server command"
    Assert-TextPattern "ServerRequestSacrifice" "Source/Catfishing/UI/CatLakeReachPageController.cpp" "fish guard sacrifice action reaches PlayerController server command"
    Assert-TextPattern "ClientReceiveFishConsumeResult" "Source/Catfishing/Framework/Game/CatGameplayTypes.h" "consume fish result returns to owning client"
    Assert-TextPattern "OnFishConsumeResultReceived" "Source/Catfishing/Framework/Game/CatGameplayTypes.h" "consume fish result can refresh UI Model"
    Assert-TextPattern "TryGetSharedFishTankSnapshot" "Source/Catfishing/Camp/CatCampHubActor.h" "UI can read shared tank revision without write access"
    Assert-TextPattern "Catfishing.Unit.UI.Reach.FishGuardWidgetEmitsPureSelectionAndActionIntents" "Source/Catfishing/UI/Tests/CatLakeReachWidgetTests.cpp" "fish guard pure intent automation"
    Assert-TextPattern "Catfishing.Unit.UI.Reach.FishGuardConsumeClickReachesBackendAndUpdatesGuard" "Source/Catfishing/UI/Tests/CatLakeReachFishGuardInteractionTests.cpp" "fish guard consume backend automation"
    Assert-TextPattern "LoadLakeReachWidgetClass" "Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp" "configured WBP class load"
    Assert-TextPattern "ui_reach_view_class_missing" "Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp" "missing WBP fail-closed log"
    Assert-TextPattern "CreateWidget<UCatLakeReachWidget>\(Controller, ViewClass\)" "Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp" "View created from configured WBP class"
    Assert-TextPattern "BoundPersonalFishGuard" "Source/Catfishing/UI/CatLakeReachModel.h" "personal fish guard read source belongs to Model"
    Assert-TextPattern "BoundProfile" "Source/Catfishing/UI/CatLakeReachModel.h" "durable collection read source belongs to Model"
    Assert-TextPattern "CanRequestOnlineLeaveFromLake" "Source/Catfishing/UI/CatLakeReachModel.cpp" "Lake menu leave gate belongs to Model"
    Assert-NoTextPattern "WidgetTree->ConstructWidget" "Source/Catfishing/UI/CatLakeReachWidget.cpp" "LakeReach View must not construct native player layout"
    Assert-NoTextPattern "SetText\(" "Source/Catfishing/UI/CatLakeReachWidget.cpp" "LakeReach C++ View must not render WBP text"
    Assert-NoTextPattern "SetVisibility\(" "Source/Catfishing/UI/CatLakeReachWidget.cpp" "LakeReach C++ View must not render WBP panel visibility"
    Assert-NoTextPattern "SetIsEnabled\(" "Source/Catfishing/UI/CatLakeReachWidget.cpp" "LakeReach C++ View must not render WBP button enablement"
    Assert-NoTextPattern "CreateWidget<UCatLakeReachWidget>\([^\r\n]*UCatLakeReachWidget::StaticClass\(\)" "Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp" "LakeReach must not fall back to native View class"
    Assert-TextPattern "Catfishing.Unit.UI.LocalPlayerUISubsystem.AttachesSingleLakeReachRootForPossessedCat" "Source/Catfishing/UI/Tests/CatLocalPlayerUISubsystemTests.cpp" "LocalPlayer viewport attach automation"
    Assert-TextPattern "Catfishing.Unit.UI.Settings.LakeReachUsesConfiguredWidgetClass" "Source/Catfishing/UI/Tests/CatUISettingsTests.cpp" "configured WBP automation"
    Assert-TextPattern "Catfishing.Unit.UI.Reach.BlueprintViewCarriesHudFishingGuardAndCollectionFacts" "Source/Catfishing/UI/Tests/CatLakeReachWidgetTests.cpp" "Blueprint View DTO automation"
    Assert-TextPattern "UI_REACH_RUNTIME_PASS" "Scripts/verify_ui_reach_runtime.py" "Runtime pass marker"
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
    创建或刷新正式 WBP_CatLakeReach 前端资产。
    该模式只运行 UIReach 的 Editor 资产自动化，并要求报告证明 WBP 继承正式 View 基类、拥有 Designer 属性绑定且保存成功。
    #>
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $Editor "Unreal Editor commandlet"
    $RunRoot = Join-Path $EvidenceRoot ("CreateWBP-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
    $ReportRoot = Join-Path $RunRoot "Report"
    $LogFile = Join-Path $RunRoot "CreateWBP.log"
    New-Item -ItemType Directory -Path $RunRoot -Force | Out-Null
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
        "-ExecCmds=Automation RunTests Catfishing.Editor.UIReach.CreateFormalWBPAsset;Quit" `
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
        "Catfishing.Editor.UIReach.CreateFormalWBPAsset"
    )
    # BindingCount 和面板名是生成脚本与正式 WBP 之间的最小握手信号。
    # 这里不检查美术细节，只防止动作区或 Designer 绑定缺失时仍保存出看似成功的资产。
    $LogText = Get-Content -LiteralPath $LogFile -Raw
    if ($LogText -notmatch "CREATE_UI_REACH_WBP_PASS" -or $LogText -notmatch "WidgetCount=39" -or $LogText -notmatch "BindingCount=16" -or $LogText -notmatch "FrontendPanel=LakeReachRootFrame" -or $LogText -notmatch "ActionPanel=FishGuardActionPanel" -or $LogText -match "EnsureFailed|LogPython: Error") {
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
    # 最后两个用例分开守住两层边界：
    # Widget 用例证明按钮只发 UI 意图，交互用例证明“吃鱼”意图能走到后端并把结果带回 Model。
    $AllowedWarningMessagesByTest = @{
        "Catfishing.Unit.UI.Reach.FishGuardConsumeClickReachesBackendAndUpdatesGuard" = @("ui_reach_menu_input_unavailable")
    }
    Assert-AutomationReport -IndexFile $IndexFile -LogFile $LogFile -ExpectedTests @(
        "Catfishing.Unit.UI.Settings.LakeReachUsesConfiguredWidgetClass",
        "Catfishing.Unit.UI.TravelWidget.ClassAndOpaqueHandlesRemainViewOnly",
        "Catfishing.Unit.UI.LocalPlayerUISubsystem.IsLocalPlayerScopedAndNotGlobalSingleton",
        "Catfishing.Unit.UI.LocalPlayerUISubsystem.ClearsFishingSessionWhenObservedActorEnds",
        "Catfishing.Unit.UI.LocalPlayerUISubsystem.AttachesSingleLakeReachRootForPossessedCat",
        "Catfishing.Unit.UI.LocalPlayerUISubsystem.OnlineTravelWidgetStaysOutOfLake",
        "Catfishing.Unit.UI.FishingViewState.ProjectsReplicatedFactsWithoutGameplayObjects",
        "Catfishing.Unit.UI.Reach.BlueprintViewCarriesHudFishingGuardAndCollectionFacts",
        "Catfishing.Unit.UI.Reach.FishGuardWidgetEmitsPureSelectionAndActionIntents",
        "Catfishing.Unit.UI.Reach.FishGuardConsumeClickReachesBackendAndUpdatesGuard"
    ) -AllowedWarningMessagesByTest $AllowedWarningMessagesByTest
}

function Invoke-UIReachRuntimeAttachAutomation {
    <#
    运行 UIReach Runtime 的 LocalPlayer 装配用例。
    该用例在命令行 Editor 内构造带 GameViewportClient 的 Game World，先证明关闭 gate 不装配半套 MVC，再显式开启 gate 触发 AttachLakePawn，并要求日志出现唯一 ui_reach_attached/RootCount=1/RootClass=WBP_CatLakeReach_C。
    #>
    param([string]$RuntimeRoot)
    $AttachRoot = Join-Path $RuntimeRoot "AttachAutomation"
    $ReportRoot = Join-Path $AttachRoot "Report"
    $LogFile = Join-Path $AttachRoot "Automation.log"
    New-Item -ItemType Directory -Path $AttachRoot -Force | Out-Null
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
        "-ExecCmds=Automation RunTests Catfishing.Unit.UI.LocalPlayerUISubsystem.AttachesSingleLakeReachRootForPossessedCat;Quit" `
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
        "Catfishing.Unit.UI.LocalPlayerUISubsystem.AttachesSingleLakeReachRootForPossessedCat"
    )
    $LogText = Get-Content -LiteralPath $LogFile -Raw
    $AttachCount = ([regex]::Matches($LogText, "Event=ui_reach_attached")).Count
    if ($AttachCount -ne 1 -or $LogText -notmatch "RootCount=1" -or $LogText -notmatch "RootClass=WBP_CatLakeReach_C") {
        throw ("UIReach runtime attach log is not green: attachMarkers={0} log={1}" -f $AttachCount, $LogFile)
    }
}

function Invoke-UIReachRuntime {
    <#
    在真实 Editor 命令行中执行 Lake 冷启动验证。
    流程先用 Python 只读探针检查正式 Lake、GameMode/Controller、UI Settings、UIReach C++ 基类和正式 WBP 类；再运行 LocalPlayer 装配用例要求 ui_reach_attached/RootCount=1/RootClass=WBP_CatLakeReach_C。
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
