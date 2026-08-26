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
$EvidenceRoot = Join-Path $ProjectRoot "Saved\Automation\ItemsTankSacrificeCamp"

function Assert-ToolFile {
    <#
    验证单个外部工具或项目文件真实存在。
    调用方在启动 UBT、Editor 或地图探针前使用它收口前置条件；缺失时立即非零退出，避免后续步骤沿用旧日志伪造本轮证据。
    #>
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description not found: $Path"
    }
}

function Invoke-ModuleStaticCheck {
    <#
    执行第三模块的静态合同检查。
    流程先解析 Harness JSON 并逐项确认第三模块、单原子模式和 A1-A4 完成规则，再用 rg 核对网络入口声明；它只证明合同结构和代码入口存在，不冒充构建、Automation 或 Runtime 接线。
    #>
    $Ripgrep = Get-Command rg -ErrorAction SilentlyContinue
    if (-not $Ripgrep) {
        throw "rg is required for static verification"
    }
    $HarnessPath = Join-Path $ProjectRoot ".harness/harness.json"
    $Harness = Get-Content -LiteralPath $HarnessPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not ($Harness.module_delivery.PSObject.Properties.Name -contains "ItemsTankSacrificeCamp")) {
        throw "Static contract missing: ItemsTankSacrificeCamp module"
    }
    $Module = $Harness.module_delivery.ItemsTankSacrificeCamp
    if ($Module.delivery_mode -ne "single_atomic_module") {
        throw "Static contract mismatch: ItemsTankSacrificeCamp delivery_mode=$($Module.delivery_mode)"
    }
    foreach ($CriterionId in @("A1", "A2", "A3", "A4")) {
        if ($Module.completion_rule -notmatch $CriterionId) {
            throw "Static contract missing: ItemsTankSacrificeCamp completion_rule lacks $CriterionId"
        }
    }
    $Checks = @(
        @{ Pattern = 'UFUNCTION\(NetMulticast, Reliable\)'; Path = 'Source/Catfishing/Camp/CatCampHubActor.h' },
        @{ Pattern = 'MulticastCampfirePlaybackRequested'; Path = 'Source/Catfishing/Camp/CatCampHubActor.h' },
        @{ Pattern = 'ClientReceiveSacrificeResult'; Path = 'Source/Catfishing/Framework/Game/CatGameplayTypes.h' },
        @{ Pattern = 'ClientReceiveCampCommandResult'; Path = 'Source/Catfishing/Framework/Game/CatGameplayTypes.h' },
        @{ Pattern = 'UFUNCTION\(Client, Reliable\)'; Path = 'Source/Catfishing/Framework/Game/CatGameplayTypes.h' }
    )
    foreach ($Check in $Checks) {
        & $Ripgrep.Source -n $Check.Pattern (Join-Path $ProjectRoot $Check.Path)
        if ($LASTEXITCODE -ne 0) {
            throw "Static contract missing: pattern=$($Check.Pattern) path=$($Check.Path)"
        }
    }
}

function Invoke-ModuleBuild {
    <#
    构建当前项目的 CatfishingEditor 目标。
    项目根始终由脚本位置推导，先检查 uproject 和 UBT 入口，再把非零构建结果原样抛出，作为第三模块交付前的硬失败证据。
    #>
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $BuildTool "Unreal Build tool"
    & $BuildTool CatfishingEditor Win64 Development $ProjectFile -WaitMutex -NoHotReload -NoUBA
    if ($LASTEXITCODE -ne 0) {
        throw "CatfishingEditor build failed with exit code $LASTEXITCODE"
    }
}

function Invoke-AutomationBatch {
    <#
    为单个 Automation 过滤器启动独立 Editor 进程并保存新报告与日志。
    每批只负责自己的过滤器；流程会检查报告、日志、成功数、失败/未运行终态和必需用例成功状态，缺报告、零成功、任一失败或目标用例缺失都会非零退出。
    #>
    param([string]$Filter, [string]$BatchName, [string]$RunRoot, [string[]]$RequiredTests = @())
    $BatchRoot = Join-Path $RunRoot $BatchName
    $ReportRoot = Join-Path $BatchRoot "Report"
    $LogFile = Join-Path $BatchRoot "Automation.log"
    New-Item -ItemType Directory -Path $BatchRoot -Force | Out-Null
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
        "-ExecCmds=Automation RunTests $Filter;Quit" `
        "-TestExit=Automation Test Queue Empty" `
        "-ReportExportPath=$ReportRoot" `
        "-abslog=$LogFile"
    $EditorExitCode = $LASTEXITCODE
    $IndexFile = Join-Path $ReportRoot "index.json"
    if (-not (Test-Path -LiteralPath $IndexFile) -or -not (Test-Path -LiteralPath $LogFile)) {
        throw "$BatchName automation did not produce a fresh report and log: editorExit=$EditorExitCode"
    }
    $LogText = Get-Content -LiteralPath $LogFile -Raw
    $SuccessCount = ([regex]::Matches($LogText, "Test Completed\. Result=\{Success\}")).Count
    $FailureCount = ([regex]::Matches($LogText, "Test Completed\. Result=\{(Fail|NotRun)\}")).Count
    $MissingRequiredTests = @()
    foreach ($RequiredTest in $RequiredTests) {
        $RequiredPattern = "Test Completed\. Result=\{Success\}.*Path=\{$([regex]::Escape($RequiredTest))\}"
        if ($LogText -notmatch $RequiredPattern) {
            $MissingRequiredTests += $RequiredTest
        }
    }
    if ($EditorExitCode -ne 0 -or $SuccessCount -lt 1 -or $FailureCount -ne 0 -or $MissingRequiredTests.Count -gt 0) {
        $MissingText = if ($MissingRequiredTests.Count -gt 0) {
            " missingRequired=$($MissingRequiredTests -join ',')"
        } else {
            ""
        }
        throw "$BatchName automation is not green: editorExit=$EditorExitCode successes=$SuccessCount failures=$FailureCount$MissingText"
    }
}

function Invoke-ModuleAutomation {
    <#
    编排第三模块相关的 Items、Run、Camp 和 PlayerController Automation 批次。
    每批独立留证，防止宽过滤器掩盖缺测试；即使前序批次失败也继续启动后续批次，让新增 Camp、PlayerController 和 A3 恢复合同始终留下本轮 report/log，最后再用汇总失败保持非零退出。
    #>
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $Editor "Unreal Editor commandlet"
    $RunRoot = Join-Path $EvidenceRoot (Get-Date -Format "yyyyMMdd-HHmmss")
    $Batches = @(
        @{ Filter = 'Catfishing.Unit.Items'; Name = 'Items'; RequiredTests = @('Catfishing.Unit.Items.TransferOwnedFish.SuccessReplayAndStaleTargetAreAtomic') },
        @{ Filter = 'Catfishing.Unit.Run'; Name = 'Run'; RequiredTests = @('Catfishing.Unit.Run.SacrificeCoordinator.ItemsCommittedRecoveryOnlyRetriesRun') },
        @{ Filter = 'Catfishing.Unit.Camp'; Name = 'Camp'; RequiredTests = @('Catfishing.Unit.Camp.HubActor.CampfirePlaybackUsesReliableMulticastAndLocalDelegate') },
        @{ Filter = 'Catfishing.Unit.Framework.PlayerController'; Name = 'PlayerController'; RequiredTests = @('Catfishing.Unit.Framework.PlayerController.CampAndSacrificeResultsUseReliableOwningClientRpcs') }
    )
    $FailedBatches = @()
    foreach ($Batch in $Batches) {
        try {
            Invoke-AutomationBatch $Batch.Filter $Batch.Name $RunRoot $Batch.RequiredTests
        }
        catch {
            $FailedBatches += "$($Batch.Name): $($_.Exception.Message)"
            Write-Warning "$($Batch.Name) automation batch failed; continuing so later batches still produce evidence. $($_.Exception.Message)"
        }
    }
    if ($FailedBatches.Count -gt 0) {
        throw "Automation batches failed:`n$($FailedBatches -join "`n")"
    }
}

function Invoke-ModuleRuntime {
    <#
    运行第三模块的 Lake 地图只读 Runtime 探针。
    流程先确认项目、Editor 和 Python 探针存在，再让 Unreal Python 读取 Camp/Tank 装配事实；控制台 GetAll 在本机 Online 初始化噪声中不能稳定退出，所以这里用唯一 PASS 标记和固定事实字段共同证明 Hub、Tank、SharedFishTank、位置和 400cm 半径。
    #>
    Assert-ToolFile $ProjectFile "Catfishing project"
    Assert-ToolFile $Editor "Unreal Editor commandlet"
    $MapVerifier = Join-Path $ProjectRoot "Scripts\verify_items_tank_sacrifice_camp_map.py"
    Assert-ToolFile $MapVerifier "ItemsTankSacrificeCamp map verifier"
    $RuntimeRoot = Join-Path $EvidenceRoot ("Runtime-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
    $LogFile = Join-Path $RuntimeRoot "Runtime.log"
    New-Item -ItemType Directory -Path $RuntimeRoot -Force | Out-Null
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
        "-ExecutePythonScript=$MapVerifier" `
        "-abslog=$LogFile"
    if ($LASTEXITCODE -ne 0) {
        throw "ItemsTankSacrificeCamp runtime probe failed with exit code $LASTEXITCODE"
    }
    if (-not (Test-Path -LiteralPath $LogFile)) {
        throw "ItemsTankSacrificeCamp runtime probe did not produce a log"
    }
    $LogText = Get-Content -LiteralPath $LogFile -Raw
    $PassCount = ([regex]::Matches($LogText, "ITEMS_TANK_SACRIFICE_CAMP_MAP_PASS")).Count
    $ExpectedPass = "ITEMS_TANK_SACRIFICE_CAMP_MAP_PASS CampHub=StageC_CampHub HubLocation=\(-750\.0,350\.0,0\.0\) FishTank=StageC_FishTank TankLocation=\(-575\.0,350\.0,0\.0\) Radius=400\.0"
    if ($PassCount -ne 1 -or $LogText -match "LogPython: Error" -or $LogText -notmatch $ExpectedPass) {
        throw "BLOCKED A4: Lake Camp/Tank map facts are incomplete. PassMarkers=$PassCount Log=$LogFile"
    }
}

switch ($Mode) {
    "Static" { Invoke-ModuleStaticCheck }
    "Build" { Invoke-ModuleBuild }
    "Automation" { Invoke-ModuleAutomation }
    "Runtime" { Invoke-ModuleRuntime }
    default { throw "Unknown verification mode: $Mode" }
}
