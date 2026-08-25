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

function Invoke-UIReachStaticCheck {
    <#
    执行 UIReach 原子模块静态合同检查。
    流程先解析 Harness 中的 UIReach 模块，再核对唯一 LocalPlayer 协调器、可显式开启的 LakeReach 根 View、只读 Fishing View、个人鱼护和图鉴投影都没有被拆成独立交付入口。
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

    Assert-TextPattern "UIReach" ".harness/harness.json" "UIReach harness entry"
    Assert-TextPattern "single_atomic_module" ".harness/harness.json" "atomic delivery mode"
    Assert-TextPattern "UCatLakeReachWidget" "Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp" "LocalPlayer can explicitly create the single LakeReach root"
    Assert-TextPattern "LakeReachWidget->Render" "Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp" "LocalPlayer is the only render caller"
    Assert-TextPattern "BoundPersonalFishGuard" "Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp" "personal fish guard read source"
    Assert-TextPattern "BoundProfile" "Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp" "durable collection read source"
    Assert-TextPattern "CanRequestOnlineLeaveFromLake" "Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp" "Lake menu leave gate"
    Assert-TextPattern "Personal fish guard" "Source/Catfishing/UI/CatLakeReachWidget.cpp" "fish guard menu text"
    Assert-TextPattern "Fish collection" "Source/Catfishing/UI/CatLakeReachWidget.cpp" "collection menu text"
    Assert-TextPattern "Last action" "Source/Catfishing/UI/CatLakeReachWidget.cpp" "Fishing command feedback text"
    Assert-TextPattern "Catfishing.Unit.UI.LocalPlayerUISubsystem.AttachesSingleLakeReachRootForPossessedCat" "Source/Catfishing/UI/Tests/CatLocalPlayerUISubsystemTests.cpp" "LocalPlayer viewport attach automation"
    Assert-TextPattern "Catfishing.Unit.UI.Reach.SingleRootCarriesHudFishingGuardAndCollectionFacts" "Source/Catfishing/UI/Tests/CatLakeReachWidgetTests.cpp" "single root UIReach automation"
    Assert-TextPattern "UI_REACH_RUNTIME_PASS" "Scripts/verify_ui_reach_runtime.py" "Runtime pass marker"
}

function Invoke-UIReachBuild {
    <#
    构建当前 CatfishingEditor 目标。
    这是 UIReach 的新鲜二进制证据；UBT 非零时直接失败，不沿用其他模块或旧构建结果。
    #>
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $BuildTool "Unreal Build tool"
    & $BuildTool CatfishingEditor Win64 Development $ProjectFile -WaitMutex -NoHotReload -NoUBA
    if ($LASTEXITCODE -ne 0) {
        throw ("CatfishingEditor build failed with exit code {0}" -f $LASTEXITCODE)
    }
}

function Assert-AutomationReport {
    <#
    核验 UIReach 自动化报告的结构化结果。
    报告必须只包含成功或零错误结果，并且每个必需用例都精确出现一次；缺测试、失败、未运行或旧报告都会让 Automation 模式失败。
    #>
    param(
        [Parameter(Mandatory = $true)]
        [string]$IndexFile,
        [Parameter(Mandatory = $true)]
        [string]$LogFile,
        [Parameter(Mandatory = $true)]
        [string[]]$ExpectedTests
    )
    $Report = Get-Content -LiteralPath $IndexFile -Raw | ConvertFrom-Json
    $Tests = @($Report.tests)
    $NonSuccess = @($Tests | Where-Object { $_.state -ne "Success" -or [int]$_.errors -ne 0 })
    if ($NonSuccess.Count -gt 0) {
        throw ("UIReach automation has non-success tests: {0}" -f ($NonSuccess.fullTestPath -join ", "))
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
    Assert-AutomationReport -IndexFile $IndexFile -LogFile $LogFile -ExpectedTests @(
        "Catfishing.Unit.UI.Settings.LakeStatusViewUsesOnlyExplicitGate",
        "Catfishing.Unit.UI.TravelWidget.ClassAndOpaqueHandlesRemainViewOnly",
        "Catfishing.Unit.UI.LocalPlayerUISubsystem.IsLocalPlayerScopedAndNotGlobalSingleton",
        "Catfishing.Unit.UI.LocalPlayerUISubsystem.ClearsFishingSessionWhenObservedActorEnds",
        "Catfishing.Unit.UI.LocalPlayerUISubsystem.AttachesSingleLakeReachRootForPossessedCat",
        "Catfishing.Unit.UI.LocalPlayerUISubsystem.OnlineTravelWidgetStaysOutOfLake",
        "Catfishing.Unit.UI.FishingViewState.ProjectsReplicatedFactsWithoutGameplayObjects",
        "Catfishing.Unit.UI.Reach.SingleRootCarriesHudFishingGuardAndCollectionFacts"
    )
}

function Invoke-UIReachRuntimeAttachAutomation {
    <#
    运行 UIReach Runtime 的 LocalPlayer 装配用例。
    该用例在命令行 Editor 内构造带 GameViewportClient 的 Game World，先证明默认玩家路径不装配白盒根，再显式开启 gate 触发 AttachLakePawn，并要求日志出现唯一 ui_reach_attached/RootCount=1。
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
    if ($AttachCount -ne 1 -or $LogText -notmatch "RootCount=1") {
        throw ("UIReach runtime attach log is not green: attachMarkers={0} log={1}" -f $AttachCount, $LogFile)
    }
}

function Invoke-UIReachRuntime {
    <#
    在真实 Editor 命令行中执行 Lake 冷启动验证。
    流程先用 Python 只读探针检查正式 Lake、GameMode/Controller、UI Settings、UIReach C++ 类和默认不可见口径；再运行 LocalPlayer 装配用例要求显式开启后 ui_reach_attached/RootCount=1。
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
    "Automation" { Invoke-UIReachAutomation }
    "Runtime" { Invoke-UIReachRuntime }
    default { throw ("Unknown verification mode: {0}" -f $Mode) }
}
