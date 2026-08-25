param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Static", "Build", "Automation")]
    [string]$Mode
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ProjectFile = Join-Path $ProjectRoot "Catfishing.uproject"
$Editor = "D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$BuildTool = "D:\UE_5.8\Engine\Build\BatchFiles\Build.bat"
$EvidenceRoot = Join-Path $ProjectRoot "Saved\Automation\FishingPlayerEntry"

function Assert-ContainsText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,
        [Parameter(Mandatory = $true)]
        [string]$Needle,
        [Parameter(Mandatory = $true)]
        [string]$ErrorMessage
    )
    <# 静态核验的基础断言：只判断关键合同文本是否存在，不解析业务语义，避免把易变实现细节写成硬门禁。 #>
    if (-not $Text.Contains($Needle)) {
        throw $ErrorMessage
    }
}

function Assert-FishingPlayerEntryStatic {
    <# 静态核验流程：同时检查 Harness 单原子规则、配置入口、地图探针和 FullLoop 测试锚点；它不替代 Build/Automation，只防止模块被拆碎或入口合同悄悄漂移。 #>
    $HarnessPath = Join-Path $ProjectRoot ".harness\harness.json"
    if (-not (Test-Path -LiteralPath $HarnessPath)) {
        throw "Missing harness.json for FishingPlayerEntry"
    }
    $HarnessText = Get-Content -LiteralPath $HarnessPath -Raw -Encoding UTF8
    $Harness = $HarnessText | ConvertFrom-Json
    $Module = $Harness.module_delivery.FishingPlayerEntry
    if (-not $Module) {
        throw "Harness missing FishingPlayerEntry module_delivery"
    }
    if ($Module.delivery_mode -ne "single_atomic_module") {
        throw "FishingPlayerEntry delivery_mode must be single_atomic_module"
    }
    $Facets = @($Module.facets)
    if (($Facets -join "|") -ne "Map|Input|Interaction") {
        throw "FishingPlayerEntry facets must stay Map/Input/Interaction as one module"
    }
    Assert-ContainsText $Module.tracking_rule "模块级缺口" "FishingPlayerEntry tracking_rule must keep new questions under module-level gaps"
    Assert-ContainsText $Module.tracking_rule "不能散成" "FishingPlayerEntry tracking_rule must reject fragmented task tracking"
    foreach ($Needle in @("正式 Lake", "唯一输入配置", "完整 Fishing 玩家命令链", "个人鱼获", "构建和运行证据")) {
        Assert-ContainsText $Module.completion_rule $Needle "FishingPlayerEntry completion_rule missing required endpoint: $Needle"
    }
    foreach ($Key in @("fishing_player_entry_static_check", "fishing_player_entry_build", "fishing_player_entry_automation")) {
        if (-not $Harness.verification.PSObject.Properties.Name.Contains($Key)) {
            throw "Harness verification missing $Key"
        }
    }

    $DefaultGame = Get-Content -LiteralPath (Join-Path $ProjectRoot "Config\DefaultGame.ini") -Raw -Encoding UTF8
    $DefaultInput = Get-Content -LiteralPath (Join-Path $ProjectRoot "Config\DefaultInput.ini") -Raw -Encoding UTF8
    Assert-ContainsText $DefaultGame "GameplayMap=/Game/Catfishing/Maps/Lake.Lake" "GameplayMap must point to formal Lake"
    Assert-ContainsText $DefaultGame "AbilityInputConfig=/Game/Data/Abilities/DA_CatAbilityInputConfig.DA_CatAbilityInputConfig" "AbilityInputConfig must point to the formal input asset"
    Assert-ContainsText $DefaultInput "DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput" "DefaultPlayerInputClass must use Enhanced Input"
    Assert-ContainsText $DefaultInput "DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent" "DefaultInputComponentClass must use Enhanced Input"

    $MapVerifier = Get-Content -LiteralPath (Join-Path $ProjectRoot "Scripts\verify_stage_a_map.py") -Raw -Encoding UTF8
    Assert-ContainsText $MapVerifier "FISHING_PLAYER_ENTRY_MAP_PASS" "Map verifier must emit the FishingPlayerEntry pass marker"
    Assert-ContainsText $MapVerifier "EXPECTED_FISHING_TAGS" "Map verifier must keep the Fishing input tag set explicit"

    $FullLoopTest = Get-Content -LiteralPath (Join-Path $ProjectRoot "Source\Catfishing\Fishing\Tests\CatFishingPlayerEntryTests.cpp") -Raw -Encoding UTF8
    Assert-ContainsText $FullLoopTest "Catfishing.PlayerEntry.FullLoop" "FishingPlayerEntry must keep the FullLoop automation test"
    Assert-ContainsText $FullLoopTest "SubmitScoop" "FullLoop must enter through the player command component"
    Assert-ContainsText $FullLoopTest "FishGuard" "FullLoop must verify personal fish guard capture"

    Write-Host "FishingPlayerEntry static check passed"
}

function Invoke-FishingPlayerEntryBuild {
    <# 以项目真实 Editor Target 构建当前实现；外部进程非零立即抛错，不把旧二进制当成本轮证据。 #>
    # 验证进程禁用共享 UBA Server，避免另一工作树或 Editor 残留的本机加速端口把构建结果变成环境竞争。
    & $BuildTool CatfishingEditor Win64 Development $ProjectFile -WaitMutex -NoHotReload -NoUBA
    if ($LASTEXITCODE -ne 0) {
        throw "CatfishingEditor build failed with exit code $LASTEXITCODE"
    }
}

function Invoke-FishingPlayerEntryMapVerification {
    <# 地图只读验证流程：删除本轮专用旧日志后在真实 Editor 中执行既有 Lake 验证脚本；同时要求进程成功、唯一 PASS 标记存在且 Python 没有报错，避免旧日志或 Editor 的宽松退出码伪造通过。 #>
    $MapVerifier = Join-Path $ProjectRoot "Scripts\verify_stage_a_map.py"
    $MapLog = Join-Path $EvidenceRoot "MapVerification.log"
    New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
    if (Test-Path -LiteralPath $MapLog) {
        Remove-Item -LiteralPath $MapLog -Force
    }
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
        "-ExecutePythonScript=$MapVerifier" `
        "-abslog=$MapLog"
    if ($LASTEXITCODE -ne 0) {
        throw "FishingPlayerEntry map verification failed with exit code $LASTEXITCODE"
    }
    if (-not (Test-Path -LiteralPath $MapLog)) {
        throw "FishingPlayerEntry map verification did not produce a fresh log"
    }
    $MapLogText = Get-Content -LiteralPath $MapLog -Raw
    $PassCount = ([regex]::Matches($MapLogText, "FISHING_PLAYER_ENTRY_MAP_PASS")).Count
    if ($PassCount -ne 1 -or $MapLogText -match "LogPython: Error") {
        throw "FishingPlayerEntry map verification is not green: passMarkers=$PassCount"
    }
}

function Assert-FishingPlayerEntryAutomationReport {
    param(
        [Parameter(Mandatory = $true)]
        [string]$IndexFile,
        [Parameter(Mandatory = $true)]
        [string]$LogFile
    )
    <# 报告核验流程：从结构化报告精确定位唯一 FullLoop 并要求 Success/零错误，再从日志核对同一路径确实开始且以 Success 完成；测试未发现、未执行、重名、失败或只留下旧报告都拒绝。 #>
    $Report = Get-Content -LiteralPath $IndexFile -Raw | ConvertFrom-Json
    $Matches = @($Report.tests | Where-Object { $_.fullTestPath -eq "Catfishing.PlayerEntry.FullLoop" })
    if ($Matches.Count -ne 1 -or @($Report.tests).Count -ne 1) {
        throw "FishingPlayerEntry report did not discover exactly one FullLoop: matches=$($Matches.Count) total=$(@($Report.tests).Count)"
    }
    $FullLoop = $Matches[0]
    if ($FullLoop.state -ne "Success" -or [int]$FullLoop.errors -ne 0) {
        throw "FishingPlayerEntry FullLoop did not succeed: state=$($FullLoop.state) errors=$($FullLoop.errors)"
    }
    $LogText = Get-Content -LiteralPath $LogFile -Raw
    $Started = $LogText -match "Test Started\..*Path=\{Catfishing\.PlayerEntry\.FullLoop\}"
    $Completed = $LogText -match "Test Completed\. Result=\{Success\}.*Path=\{Catfishing\.PlayerEntry\.FullLoop\}"
    if (-not $Started -or -not $Completed) {
        throw "FishingPlayerEntry log does not prove FullLoop start and success: started=$Started completed=$Completed"
    }
}

function Invoke-FishingPlayerEntryAutomation {
    <# 自动化流程：先完成 Lake 只读事实验证，再清除本轮专用旧报告并运行唯一 PlayerEntry 过滤器；最后把结构化报告与日志交给精确核验，任一证据缺失都 fail-closed。 #>
    Invoke-FishingPlayerEntryMapVerification
    $ReportRoot = Join-Path $EvidenceRoot "Report"
    $LogFile = Join-Path $EvidenceRoot "Automation.log"
    New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
    if (Test-Path -LiteralPath $ReportRoot) {
        Remove-Item -LiteralPath $ReportRoot -Recurse -Force
    }
    if (Test-Path -LiteralPath $LogFile) {
        Remove-Item -LiteralPath $LogFile -Force
    }
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
        "-ExecCmds=Automation RunTests Catfishing.PlayerEntry;Quit" `
        "-TestExit=Automation Test Queue Empty" `
        "-ReportExportPath=$ReportRoot" `
        "-abslog=$LogFile"
    if ($LASTEXITCODE -ne 0) {
        throw "FishingPlayerEntry automation editor failed with exit code $LASTEXITCODE"
    }
    $IndexFile = Join-Path $ReportRoot "index.json"
    if (-not (Test-Path -LiteralPath $IndexFile) -or -not (Test-Path -LiteralPath $LogFile)) {
        throw "FishingPlayerEntry automation did not produce a fresh report and log"
    }
    Assert-FishingPlayerEntryAutomationReport -IndexFile $IndexFile -LogFile $LogFile
}

if ($Mode -eq "Static") {
    Assert-FishingPlayerEntryStatic
} elseif ($Mode -eq "Build") {
    Invoke-FishingPlayerEntryBuild
} else {
    Invoke-FishingPlayerEntryAutomation
}
