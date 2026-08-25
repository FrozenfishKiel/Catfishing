param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Static", "Build", "Automation", "Runtime")]
    [string]$Mode
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ProjectFile = Join-Path $ProjectRoot "Catfishing.uproject"
$Editor = "D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$BuildTool = "D:\UE_5.8\Engine\Build\BatchFiles\Build.bat"
$EvidenceRoot = Join-Path $ProjectRoot "Saved\Automation\CharacterGrowthCondition"

function Assert-TextContains {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,
        [Parameter(Mandatory = $true)]
        [string]$Pattern,
        [Parameter(Mandatory = $true)]
        [string]$Message
    )
    <# 断言流程：用正则确认关键文本存在；缺失时立即抛错，让静态验证不把缺少接线的文件当作通过。 #>
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

function Assert-TextNotContains {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,
        [Parameter(Mandatory = $true)]
        [string]$Pattern,
        [Parameter(Mandatory = $true)]
        [string]$Message
    )
    <# 断言流程：用正则确认废弃符号不存在；命中时立即抛错，避免饥饿或疲惫数值悄悄回到运行时代码。 #>
    if ($Text -match $Pattern) {
        throw $Message
    }
}

function Read-ProjectFileText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )
    <# 读取流程：把项目相对路径固定解析到仓库内；文件缺失直接失败，防止静态检查在空输入上误判通过。 #>
    $Path = Join-Path $ProjectRoot $RelativePath
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Required file is missing: $RelativePath"
    }
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8
}

function Invoke-CharacterGrowthConditionStatic {
    <# 静态验证流程：先核对 Harness 原子模块合同，再检查 ASC、Condition、Growth、鱼定义、UI 和配置的核心接线；同时扫描废弃 Hunger/Fatigue 运行时入口。 #>
    $Harness = Get-Content -LiteralPath (Join-Path $ProjectRoot ".harness\harness.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    $Module = $Harness.module_delivery.CharacterGrowthCondition
    if (-not $Module) {
        throw "Harness is missing CharacterGrowthCondition module"
    }
    if ($Module.delivery_mode -ne "single_atomic_module") {
        throw "CharacterGrowthCondition must remain a single_atomic_module"
    }
    foreach ($Facet in @("CharacterBody", "Condition", "Growth", "FishingImpact", "Recovery")) {
        if ($Module.facets -notcontains $Facet) {
            throw "CharacterGrowthCondition harness facet is missing: $Facet"
        }
    }

    $SurvivalSet = Read-ProjectFileText "Source\Catfishing\AbilitySystem\CatSurvivalAttributeSet.h"
    Assert-TextContains $SurvivalSet "Poison" "Survival AttributeSet must expose Poison"
    Assert-TextContains $SurvivalSet "FishingStrength" "Survival AttributeSet must expose FishingStrength"
    Assert-TextContains $SurvivalSet "FightStamina" "Survival AttributeSet must expose FightStamina"
    Assert-TextNotContains $SurvivalSet "GetHungerAttribute|GetFatigueAttribute|InitialHunger|InitialFatigue" "Survival AttributeSet still exposes removed Hunger/Fatigue runtime symbols"

    $AbilitySettings = Read-ProjectFileText "Source\Catfishing\AbilitySystem\CatAbilitySettings.h"
    Assert-TextContains $AbilitySettings "TryGetInitialAttributes\(float& OutPoison, float& OutFishingStrength, float& OutFightStamina\)" "Ability settings must initialize the three current survival attributes"
    Assert-TextNotContains $AbilitySettings "InitialHunger|InitialFatigue|DiagnosticHunger|DiagnosticFatigue" "Ability settings still contains removed Hunger/Fatigue configuration"

    $ConditionComponent = Read-ProjectFileText "Source\Catfishing\Condition\CatConditionComponent.cpp"
    Assert-TextContains $ConditionComponent "ApplyCommittedFish" "Condition component must hand off committed fish to Growth"
    Assert-TextContains $ConditionComponent "GetPoisonAttribute" "Condition component must mutate Poison for toxic fish"
    Assert-TextNotContains $ConditionComponent "GetHungerAttribute|GetFatigueAttribute|HungerRelief|FatigueRelief" "Condition component still mutates removed Hunger/Fatigue state"

    $GrowthComponent = Read-ProjectFileText "Source\Catfishing\Growth\CatGrowthComponent.cpp"
    Assert-TextContains $GrowthComponent "PendingChoiceCount" "Growth component must track queued three-choice opportunities"
    Assert-TextContains $GrowthComponent "ExperiencePerChoiceSlot" "Growth component must use the configured experience slot length"
    Assert-TextContains $GrowthComponent "TerminalCache" "Growth component must keep committed fish requests idempotent"

    $FishDefinition = Read-ProjectFileText "Source\Catfishing\Data\CatFishDefinition.h"
    Assert-TextContains $FishDefinition "EatingExperience" "Fish definition must expose eating experience"
    Assert-TextNotContains $FishDefinition "HungerRelief" "Fish definition still exposes removed hunger relief"

    $LakeReachWidget = Read-ProjectFileText "Source\Catfishing\UI\CatLakeReachWidget.h"
    Assert-TextContains $LakeReachWidget "FCatGrowthSnapshot Growth" "Lake reach view state must expose Growth snapshot"
    Assert-TextNotContains $LakeReachWidget "Hunger|Fatigue" "Lake reach view state still exposes removed Hunger/Fatigue display fields"

    $LocalPlayerUI = Read-ProjectFileText "Source\Catfishing\UI\CatLocalPlayerUISubsystem.cpp"
    Assert-TextContains $LocalPlayerUI "GetGrowthComponent" "LocalPlayer UI must bind the Character Growth component"
    Assert-TextContains $LocalPlayerUI "ViewState\.Growth = Growth->GetSnapshot\(\)" "LocalPlayer UI must copy Growth into the read-only view state"
    Assert-TextNotContains $LocalPlayerUI "GetHungerAttribute|GetFatigueAttribute|ViewState\.Hunger|ViewState\.Fatigue" "LocalPlayer UI still reads removed Hunger/Fatigue state"

    $LakeReachRender = Read-ProjectFileText "Source\Catfishing\UI\CatLakeReachWidget.cpp"
    Assert-TextContains $LakeReachRender "Growth XP" "Lake reach debug view must render the Growth snapshot when explicitly enabled"
    Assert-TextNotContains $LakeReachRender "Hunger|Fatigue" "Lake reach debug view still renders removed Hunger/Fatigue labels"

    $DefaultGame = Read-ProjectFileText "Config\DefaultGame.ini"
    Assert-TextContains $DefaultGame "\[/Script/Catfishing\.CatConditionSettings\]" "DefaultGame must configure Condition runtime"
    Assert-TextContains $DefaultGame "\[/Script/Catfishing\.CatGrowthSettings\]" "DefaultGame must configure Growth runtime"
    Assert-TextNotContains $DefaultGame "InitialHunger|InitialFatigue|HungerRelief|FatigueRelief" "DefaultGame still contains removed Hunger/Fatigue runtime configuration"
}

function Invoke-CharacterGrowthConditionBuild {
    <# 构建流程：调用项目真实 Editor Target；非零退出码直接失败，避免旧 DLL 或只读扫描被误当成可构建证据。 #>
    & $BuildTool CatfishingEditor Win64 Development $ProjectFile -WaitMutex -NoHotReload -NoUBA
    if ($LASTEXITCODE -ne 0) {
        throw "CatfishingEditor build failed with exit code $LASTEXITCODE"
    }
}

function Invoke-AutomationFilter {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Filter,
        [Parameter(Mandatory = $true)]
        [string]$EvidenceName
    )
    <# 自动化流程：每个过滤器写入本模块专属证据目录，运行前只清理该过滤器目录；报告必须存在、无失败、无警告且无错误。 #>
    $RunRoot = Join-Path $EvidenceRoot $EvidenceName
    $ReportRoot = Join-Path $RunRoot "Report"
    $LogFile = Join-Path $RunRoot "Automation.log"
    New-Item -ItemType Directory -Path $RunRoot -Force | Out-Null
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
        throw "Automation filter failed with exit code ${LASTEXITCODE}: $Filter"
    }
    $IndexFile = Join-Path $ReportRoot "index.json"
    if (-not (Test-Path -LiteralPath $IndexFile)) {
        throw "Automation filter did not produce index.json: $Filter"
    }
    $Report = Get-Content -LiteralPath $IndexFile -Raw -Encoding UTF8 | ConvertFrom-Json
    if ([int]$Report.failed -ne 0 -or [int]$Report.succeededWithWarnings -ne 0 -or [int]$Report.notRun -ne 0 -or [int]$Report.inProcess -ne 0) {
        throw "Automation filter is not clean: $Filter succeeded=$($Report.succeeded) warnings=$($Report.succeededWithWarnings) failed=$($Report.failed) notRun=$($Report.notRun) inProcess=$($Report.inProcess)"
    }
    foreach ($Test in @($Report.tests)) {
        if ($Test.state -ne "Success" -or [int]$Test.warnings -ne 0 -or [int]$Test.errors -ne 0) {
            throw "Automation test is not clean: $($Test.fullTestPath) state=$($Test.state) warnings=$($Test.warnings) errors=$($Test.errors)"
        }
    }
}

function Invoke-CharacterGrowthConditionAutomation {
    <# 模块自动化流程：按一个闭环顺序覆盖属性、角色、状态、成长、鱼数据、入口 UI、Fishing 契约和 PlayerEntry；任一过滤器失败即整个模块验证失败。 #>
    Invoke-AutomationFilter "Catfishing.Unit.AbilitySystem" "AbilitySystem"
    Invoke-AutomationFilter "Catfishing.Unit.Character" "Character"
    Invoke-AutomationFilter "Catfishing.Unit.Condition" "Condition"
    Invoke-AutomationFilter "Catfishing.Unit.Growth" "Growth"
    Invoke-AutomationFilter "Catfishing.Unit.Data" "Data"
    Invoke-AutomationFilter "Catfishing.Unit.UI.Reach" "UIReach"
    Invoke-AutomationFilter "Catfishing.Unit.UI.LocalPlayerUISubsystem" "UILocalPlayer"
    Invoke-AutomationFilter "Catfishing.Unit.Fishing" "Fishing"
    Invoke-AutomationFilter "Catfishing.PlayerEntry.FullLoop" "PlayerEntry"
}

function Invoke-CharacterGrowthConditionRuntime {
    <# 运行证据流程：复用 FishingPlayerEntry 的 Lake 地图只读核对和 FullLoop，让身体/成长改动至少通过正式玩家入口链路进入一次运行时证据。 #>
    $PlayerEntryVerifier = Join-Path $PSScriptRoot "verify_fishing_player_entry.ps1"
    & powershell -ExecutionPolicy Bypass -File $PlayerEntryVerifier -Mode Automation
    if ($LASTEXITCODE -ne 0) {
        throw "CharacterGrowthCondition runtime evidence failed through FishingPlayerEntry"
    }
}

if ($Mode -eq "Static") {
    Invoke-CharacterGrowthConditionStatic
} elseif ($Mode -eq "Build") {
    Invoke-CharacterGrowthConditionBuild
} elseif ($Mode -eq "Automation") {
    Invoke-CharacterGrowthConditionAutomation
} else {
    Invoke-CharacterGrowthConditionRuntime
}
