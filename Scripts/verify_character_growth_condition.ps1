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
    <# 静态验证流程：先核对 Harness 原子模块合同，再检查 ASC、Condition、Growth、鱼定义、UI 和配置的核心接线；同时确认 Poison 通过正式 GE/ASC 入口提交、BodyAction 网关覆盖非 Fishing 身体动作、Stage C 诊断入口已离开 Character 正式路径、草药距离和 Downed gate 仍在服务端写口处自守，并扫描废弃 Hunger/Fatigue 运行时入口。这一层只证明 contract 接线，不关闭正式资产、正式 UI 或整场交付验收。 #>
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
    Assert-TextNotContains $AbilitySettings "InitialHunger|InitialFatigue|DiagnosticHunger|DiagnosticFatigue|TryGetDiagnosticPoisonDelta|bEnableDiagnosticAbility|DiagnosticPoisonDelta|DiagnosticInputAction|DiagnosticMappingContext" "Ability settings still contains removed Hunger/Fatigue or diagnostic configuration"

    $AbilitySystemComponent = Read-ProjectFileText "Source\Catfishing\AbilitySystem\CatAbilitySystemComponent.h"
    Assert-TextContains $AbilitySystemComponent "ApplyPoisonDelta" "Project ASC must expose a formal Poison delta command"
    Assert-TextContains $AbilitySystemComponent "IsPoisonAtLeast" "Project ASC must expose a formal Poison threshold reader for Condition"
    $PoisonEffect = Read-ProjectFileText "Source\Catfishing\AbilitySystem\CatPoisonEffect.h"
    Assert-TextContains $PoisonEffect "UCatGE_PoisonDelta" "Poison changes must have a dedicated GameplayEffect"
    $BodyActionAbility = Read-ProjectFileText "Source\Catfishing\AbilitySystem\CatBodyActionAbility.h"
    Assert-TextContains $BodyActionAbility "UCatGA_BodyActionCommand" "Non-Fishing body actions must have a formal GameplayAbility gateway"
    Assert-TextContains $BodyActionAbility "UCatBodyActionPayload" "Body action RPC routing must carry typed payloads instead of untyped tags"
    Assert-TextContains $BodyActionAbility "BodyActionCommitWindowSeconds" "BodyAction ability must expose a cancellable commit window instead of immediately forwarding to domains"
    Assert-TextContains $BodyActionAbility "ActivePresentationEventTag" "BodyAction ability must freeze a presentation tag so cancel clears the same long-action presentation it started"
    Assert-TextContains $BodyActionAbility "GetBodyActionLeadInSecondsForAutomation" "BodyAction ability tests must prove action-level presentation lead-in is the active wait source"
    Assert-TextContains $BodyActionAbility "ClearBodyActionCommitWindowOverrideForAutomation" "BodyAction automation override must have an explicit clear path so tests return to settings-driven lead-in"
    Assert-TextContains $BodyActionAbility "CommitBodyActionAfterWindow" "BodyAction ability must submit domains after the GAS lifecycle window"
    $BodyActionPresentationSettings = Read-ProjectFileText "Source\Catfishing\AbilitySystem\CatBodyActionPresentationSettings.h"
    Assert-TextContains $BodyActionPresentationSettings "UCatBodyActionPresentationSettings" "BodyAction long-action presentation must have one formal settings surface"
    Assert-TextContains $BodyActionPresentationSettings "ActionPresentationConfigs" "BodyAction presentation settings must keep action-level lead-in and optional Montage config together"
    Assert-TextContains $BodyActionPresentationSettings "TSoftObjectPtr<UAnimMontage>" "BodyAction presentation settings must expose optional Montage assets without hard-coding them in domains"
    $BodyActionPresentationAssetTest = Read-ProjectFileText "Source\Catfishing\AbilitySystem\Tests\CatBodyActionPresentationAssetTests.cpp"
    Assert-TextContains $BodyActionPresentationAssetTest "Catfishing\.Editor\.AbilitySystem\.CreateBodyActionMontageAssets" "BodyAction formal Montage assets must have an editor automation creation/verification entry"
    Assert-TextContains $BodyActionPresentationAssetTest "UAnimMontageFactory" "BodyAction formal Montage asset automation must create Montage assets from existing animation sequences"
    Assert-TextContains $BodyActionPresentationAssetTest "CREATE_BODY_ACTION_MONTAGE_ASSETS_PASS" "BodyAction formal Montage asset automation must emit a stable pass marker"
    $BodyActionPresentationAssetScript = Read-ProjectFileText "Scripts\create_body_action_montages.py"
    Assert-TextContains $BodyActionPresentationAssetScript "unreal\.AnimMontageFactory" "BodyAction formal Montage fallback script must use Unreal's Montage factory"
    Assert-TextContains $BodyActionPresentationAssetScript "CREATE_BODY_ACTION_MONTAGE_ASSETS_PASS" "BodyAction formal Montage fallback script must emit the same stable pass marker"
    $BodyActionAbilityCpp = Read-ProjectFileText "Source\Catfishing\AbilitySystem\CatBodyActionAbility.cpp"
    Assert-TextContains $BodyActionAbilityCpp "UAbilityTask_WaitDelay" "BodyAction ability must use an AbilityTask wait window before domain submission"
    Assert-TextContains $BodyActionAbilityCpp "ActivePayload" "BodyAction ability must hold its payload through the cancellable Ability lifecycle"
    Assert-TextContains $BodyActionAbilityCpp "Multicast_PlayBodyActionPresentation" "BodyAction ability must start long-action presentation through Character, not through domain services"
    Assert-TextContains $BodyActionAbilityCpp "Multicast_StopBodyActionPresentation" "BodyAction cancel must stop the presentation it started"
    Assert-TextContains $BodyActionAbilityCpp "GBodyActionCommitWindowOverrideSeconds = -1.0f" "BodyAction automation override clear must restore the unset sentinel instead of pinning the default window"
    Assert-TextContains $AbilitySystemComponent "CancelBodyActionAbilitiesFromAuthority" "Project ASC must expose a narrow BodyAction cancel entry for the unified cancel input"
    $FishingCommandComponent = Read-ProjectFileText "Source\Catfishing\Fishing\Integration\CatFishingCommandComponent.cpp"
    Assert-TextContains $FishingCommandComponent "CancelBodyActionAbilitiesFromAuthority" "Fishing Cancel authority path must cancel pending BodyAction abilities before continuing Fishing cancel semantics"
    $AbilitySet = Read-ProjectFileText "Source\Catfishing\AbilitySystem\CatAbilitySet.cpp"
    Assert-TextContains $AbilitySet "UCatGA_BodyActionCommand::StaticClass" "Default AbilitySet readiness must require the BodyAction gateway ability"
    Assert-TextContains $AbilitySet "GrantedAbilities\.Num\(\) < 7" "Default AbilitySet must require Fishing six inputs plus BodyAction gateway"

    $CharacterHeader = Read-ProjectFileText "Source\Catfishing\Character\CatCharacter.h"
    $CharacterCpp = Read-ProjectFileText "Source\Catfishing\Character\CatCharacter.cpp"
    Assert-TextContains $CharacterHeader "Multicast_PlayBodyActionPresentation" "Character must expose a BodyAction-specific multicast that does not reuse Fishing local prediction skipping"
    Assert-TextContains $CharacterHeader "BP_StopBodyActionPresentation" "Character Blueprint must have a stop hook for cancellable BodyAction presentation"
    Assert-TextContains $CharacterHeader "PlayBodyActionMontageFromPresentation" "Character must expose optional BodyAction Montage playback from presentation settings"
    Assert-TextContains $CharacterHeader "UFUNCTION\(NetMulticast, Reliable\)\s*void Multicast_StopBodyActionPresentation" "BodyAction stop presentation must be reliable so cancel can clear looping presentation on clients"
    Assert-TextContains $CharacterCpp "UCatBodyActionPresentationSettings" "Character BodyAction presentation must read the shared settings surface"
    Assert-TextContains $CharacterCpp "StopAnimMontage" "Character must be able to stop configured BodyAction Montage on cancel"
    Assert-TextNotContains $CharacterHeader "CatStageCTestAbility|StageCTest|DiagnosticInputAction|DiagnosticMappingContext|HandleDiagnosticAbilityInput|GrantStageCTestAbility|bEnableDiagnosticAbility" "Character header still exposes diagnostic Ability/Input path"
    Assert-TextNotContains $CharacterCpp "CatStageCTestAbility|StageCTest|DiagnosticInputAction|DiagnosticMappingContext|HandleDiagnosticAbilityInput|GrantStageCTestAbility|bEnableDiagnosticAbility" "Character implementation still grants or binds diagnostic Ability/Input path"
    Assert-TextNotContains $CharacterCpp "UCatGA_BodyActionCommand::StaticClass" "BodyAction ability must be granted by DefaultAbilitySet, not hard-coded in Character lifecycle"
    foreach ($RemovedDiagnosticPath in @(
        "Source\Catfishing\AbilitySystem\CatStageCTestAbility.h",
        "Source\Catfishing\AbilitySystem\CatStageCTestAbility.cpp",
        "Source\Catfishing\AbilitySystem\Tests\CatStageCTestAbilityTests.cpp"
    )) {
        if (Test-Path -LiteralPath (Join-Path $ProjectRoot $RemovedDiagnosticPath)) {
            throw "Removed diagnostic source still exists: $RemovedDiagnosticPath"
        }
    }

    $ConditionComponent = Read-ProjectFileText "Source\Catfishing\Condition\CatConditionComponent.cpp"
    Assert-TextContains $ConditionComponent "ApplyCommittedFish" "Condition component must hand off committed fish to Growth"
    Assert-TextContains $ConditionComponent "ApplyPoisonDelta" "Condition component must route Poison mutations through the project ASC"
    Assert-TextContains $ConditionComponent "IsPoisonAtLeast" "Condition component must ask the project ASC for Poison threshold checks"
    Assert-TextContains $ConditionComponent "HerbUseRangeCentimeters" "Condition component must self-guard herb recovery with configured authority range"
    Assert-TextContains $ConditionComponent 'if \(!Snapshot\.bDowned\)' "CarryToCamp must reject targets that are not currently Downed"
    Assert-TextNotContains $ConditionComponent "SetNumericAttributeBase[\s\S]{0,160}GetPoisonAttribute|ApplyModToAttribute[\s\S]{0,160}GetPoisonAttribute" "Condition component must not directly write the Poison Attribute"
    Assert-TextNotContains $ConditionComponent "GetHungerAttribute|GetFatigueAttribute|HungerRelief|FatigueRelief" "Condition component still mutates removed Hunger/Fatigue state"

    $GameplayTypes = Read-ProjectFileText "Source\Catfishing\Framework\Game\CatGameplayTypes.cpp"
    Assert-TextContains $GameplayTypes "CanAcceptFishingCommand" "GameMode must expose the Fishing/Chum command gate"
    Assert-TextContains $GameplayTypes "GetConditionComponent" "Fishing/Chum gate must read the current Character Condition component"
    Assert-TextContains $GameplayTypes "!Conditions->GetSnapshot\(\)\.bDowned" "Fishing/Chum gate must close when the current Character is Downed"
    Assert-TextContains $GameplayTypes "SubmitBodyActionThroughAbility" "PlayerController non-Fishing action RPCs must route through BodyAction GameplayAbility"
    Assert-TextContains $GameplayTypes "ExecuteBodyActionAbilityPayload" "BodyAction GameplayAbility must have one explicit Controller callback seam"
    foreach ($BodyActionRpc in @(
        "ServerRequestSacrifice_Implementation",
        "ServerRequestCampRest_Implementation",
        "ServerRequestCampfirePlayback_Implementation",
        "ServerTransferFishToTank_Implementation",
        "ServerRescueCharacterToCamp_Implementation",
        "ServerRepairRodAtCamp_Implementation",
        "ServerUseHerbOnCharacter_Implementation",
        "ServerConsumeFish_Implementation",
        "ServerBeginTheft_Implementation",
        "ServerCatchTheft_Implementation",
        "ServerRequestManualHelp_Implementation",
        "ServerRequestMischief_Implementation",
        "ServerPlaceProtectionSign_Implementation",
        "ServerCompleteShakeDry_Implementation"
    )) {
        Assert-TextContains $GameplayTypes "$BodyActionRpc[\s\S]{0,700}SubmitBodyActionThroughAbility" "$BodyActionRpc must hand off to BodyAction GameplayAbility before domain execution"
    }

    $FishingService = Read-ProjectFileText "Source\Catfishing\Fishing\CatFishingService.cpp"
    Assert-TextContains $FishingService "CanControllerStartFishingAction" "Fishing service must keep a service-level Downed guard for direct write calls"
    Assert-TextContains $FishingService "CanAcceptFishingCommand" "Fishing service must use the formal Fishing gate, not only the wide gameplay gate"

    $ChumPlacement = Read-ProjectFileText "Source\Catfishing\Environment\CatChumPlacementService.cpp"
    Assert-TextContains $ChumPlacement "CanAcceptFishingCommand" "Player chum placement must use the formal Fishing gate"
    Assert-TextContains $ChumPlacement "Conditions->GetSnapshot\(\)\.bDowned" "Player chum placement must reject Downed characters at the service write boundary"

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
    Assert-TextContains $LocalPlayerUI "LoadLakeReachWidgetClass" "LocalPlayer UI must create LakeReach from the configured formal WBP class"
    Assert-TextContains $LocalPlayerUI "UCatLakeReachModel" "LocalPlayer UI must assemble a LakeReach Model instead of aggregating gameplay state itself"
    Assert-TextContains $LocalPlayerUI "UCatLakeReachPageController" "LocalPlayer UI must assemble a LakeReach PageController for View intent routing"
    Assert-TextNotContains $LocalPlayerUI "GetHungerAttribute|GetFatigueAttribute|ViewState\.Hunger|ViewState\.Fatigue" "LocalPlayer UI still reads removed Hunger/Fatigue state"

    $LakeReachModel = Read-ProjectFileText "Source\Catfishing\UI\CatLakeReachModel.cpp"
    Assert-TextContains $LakeReachModel "GetGrowthComponent" "LakeReach Model must bind the Character Growth component"
    Assert-TextContains $LakeReachModel "NewState\.Growth = Growth->GetSnapshot\(\)" "LakeReach Model must copy Growth into the read-only view state"
    Assert-TextContains $LakeReachModel "GetConditionComponent" "LakeReach Model must bind the Character Condition component"
    Assert-TextContains $LakeReachModel "NewState\.Condition = Conditions->GetSnapshot\(\)" "LakeReach Model must copy Condition into the read-only view state"
    Assert-TextNotContains $LakeReachModel "GetHungerAttribute|GetFatigueAttribute|ViewState\.Hunger|ViewState\.Fatigue" "LakeReach Model still reads removed Hunger/Fatigue state"

    $LakeReachRender = Read-ProjectFileText "Source\Catfishing\UI\CatLakeReachWidget.cpp"
    Assert-TextContains $LakeReachRender "BlueprintState\.Growth = ViewState\.Growth" "Lake reach View must carry the Growth snapshot into the blueprint-safe DTO"
    Assert-TextNotContains $LakeReachRender "Hunger|Fatigue" "Lake reach debug view still renders removed Hunger/Fatigue labels"

    $DefaultGame = Read-ProjectFileText "Config\DefaultGame.ini"
    Assert-TextContains $DefaultGame "\[/Script/Catfishing\.CatConditionSettings\]" "DefaultGame must configure Condition runtime"
    Assert-TextContains $DefaultGame "\[/Script/Catfishing\.CatGrowthSettings\]" "DefaultGame must configure Growth runtime"
    Assert-TextContains $DefaultGame "\[/Script/Catfishing\.CatBodyActionPresentationSettings\]" "DefaultGame must configure BodyAction formal Montage presentation settings"
    foreach ($BodyActionMontageAsset in @(
        "AM_BodyAction_RequestSacrifice",
        "AM_BodyAction_CampRest",
        "AM_BodyAction_CampfirePlayback",
        "AM_BodyAction_TransferFishToTank",
        "AM_BodyAction_RescueCharacterToCamp",
        "AM_BodyAction_RepairRodAtCamp",
        "AM_BodyAction_UseHerbOnCharacter",
        "AM_BodyAction_ConsumeFish",
        "AM_BodyAction_BeginTheft",
        "AM_BodyAction_CatchTheft",
        "AM_BodyAction_RequestManualHelp",
        "AM_BodyAction_RequestMischief",
        "AM_BodyAction_PlaceProtectionSign",
        "AM_BodyAction_CompleteShakeDry"
    )) {
        Assert-TextContains $DefaultGame $BodyActionMontageAsset "DefaultGame must reference every BodyAction formal Montage asset: $BodyActionMontageAsset"
    }
    Assert-TextNotContains $DefaultGame "InitialHunger|InitialFatigue|HungerRelief|FatigueRelief" "DefaultGame still contains removed Hunger/Fatigue runtime configuration"
}

function Invoke-CharacterGrowthConditionBuild {
    <# 构建流程：调用项目真实 Editor Target，并显式关闭 IDE 热重载入口；非零退出码直接失败，避免旧 DLL 或只读扫描被误当成可构建证据。 #>
    & $BuildTool CatfishingEditor Win64 Development $ProjectFile -WaitMutex -NoHotReload -NoHotReloadFromIDE -NoUBA
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
    Invoke-AutomationFilter "Catfishing.Editor.AbilitySystem.CreateBodyActionMontageAssets" "BodyActionMontageAssets"
    Invoke-AutomationFilter "Catfishing.Unit.AbilitySystem" "AbilitySystem"
    Invoke-AutomationFilter "Catfishing.Unit.Character" "Character"
    Invoke-AutomationFilter "Catfishing.Unit.Condition" "Condition"
    Invoke-AutomationFilter "Catfishing.Unit.Framework.GameMode.CommandIntentPhaseGatesKeepSocialReadyAndSettlementOpen" "FrameworkCommandGate"
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
