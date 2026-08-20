# Catfishing 持续检查入口
#
# 这是本机与 CI 共用的唯一检查入口：本地改完代码跑它，CI（自托管 Runner）也跑它，两边口径完全一致。
# 阶段与顺序（默认全跑）：
#   build       构建 CatfishingEditor（零 error 零 warning 才算过）
#   catalog     数据目录校验 commandlet（鱼表 + 装备表作为一个内容包整体校验）
#   statetree   StateTree 资产生成器的 -verify（从磁盘加载并复检两棵树的形状）
#   automation  全量自动化（failed=0 且 notRun=0 才算过；条数下限防"测试悄悄消失"）
#   smoke       双进程真实联机冒烟（见 run_multiplayer_smoke.ps1 头注释）
#
# 用法：
#   powershell -File Tools\ci_check.ps1                     # 全部阶段
#   powershell -File Tools\ci_check.ps1 -Stage automation   # 只跑某一阶段
# 退出码：0=全部通过；非 0=第一个失败阶段的序号
#
# 已知环境风险：本机 Smart App Control 处于强制模式时，新链接出的 DLL 可能被按哈希拒绝加载
# （日志特征 GetLastError=4551，事件查看器 CodeIntegrity 3118）。这不是代码问题，处置见
# .harness/CODING_LESSONS.md 的 Smart App Control 一节；本脚本遇到时会明确标注而不是报成测试失败。

param(
    [ValidateSet('all', 'build', 'catalog', 'statetree', 'automation', 'smoke')]
    [string]$Stage = 'all',
    # 自动化条数下限：低于它说明有测试被整文件丢掉了（编译失败的测试文件不会进入运行集，不会报 failed）。
    # 新增测试后应上调；下调必须在提交说明里写明哪些测试为什么消失。
    [int]$MinimumTestCount = 205
)

$ErrorActionPreference = 'Stop'
$Root = 'D:\UnreaProjects\Catfishing'
$Engine = 'D:\UE_5.8\Engine'
$Project = "$Root\Catfishing.uproject"
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$OutDir = "$Root\Saved\CI\$Stamp"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Test-SacBlocked {
    param([string]$LogPath)
    return (Test-Path $LogPath) -and (Select-String -Path $LogPath -Pattern 'GetLastError=4551' -Quiet)
}

function Invoke-Stage {
    param([string]$Name, [scriptblock]$Body)
    Write-Host "`n===== [$Name] =====" -ForegroundColor Cyan
    $Sw = [System.Diagnostics.Stopwatch]::StartNew()
    $Ok = & $Body
    $Sw.Stop()
    Write-Host ("[{0}] {1}（{2:n1} 秒）" -f $Name, $(if ($Ok) { '通过' } else { '失败' }), $Sw.Elapsed.TotalSeconds) `
        -ForegroundColor $(if ($Ok) { 'Green' } else { 'Red' })
    return $Ok
}

$StageResults = [ordered]@{}

# —— build ——
if ($Stage -in @('all', 'build')) {
    $StageResults['build'] = Invoke-Stage 'build' {
        $Log = "$OutDir\build.log"
        & "$Engine\Build\BatchFiles\Build.bat" CatfishingEditor Win64 Development "$Project" -WaitMutex -NoHotReload 2>&1 |
            Out-File -Encoding utf8 $Log
        if ($LASTEXITCODE -ne 0) { Write-Host "构建 exit=$LASTEXITCODE"; return $false }
        $Errors = (Select-String -Path $Log -Pattern 'error C|: error ' | Measure-Object).Count
        $Warnings = (Select-String -Path $Log -Pattern 'warning C' | Measure-Object).Count
        Write-Host "errors=$Errors warnings=$Warnings"
        return ($Errors -eq 0 -and $Warnings -eq 0)
    }
}

# —— catalog ——
if ($Stage -in @('all', 'catalog')) {
    $StageResults['catalog'] = Invoke-Stage 'catalog' {
        $Log = "$OutDir\catalog.log"
        & "$Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Project" -run=CatDataCatalogValidation -Unattended -NullRHI -abslog="$Log" 2>&1 | Out-Null
        if (Test-SacBlocked $Log) { Write-Host '⚠ 模块被 Smart App Control 拦截（环境问题，非代码失败）'; return $false }
        if ($LASTEXITCODE -ne 0) { Write-Host "commandlet exit=$LASTEXITCODE"; return $false }
        return (Select-String -Path $Log -Pattern 'data_catalog_validation_succeeded' -Quiet)
    }
}

# —— statetree ——
if ($Stage -in @('all', 'statetree')) {
    $StageResults['statetree'] = Invoke-Stage 'statetree' {
        $Log = "$OutDir\statetree.log"
        & "$Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Project" -run=CatBuildStateTreeAssets -verify -Unattended -NullRHI -abslog="$Log" 2>&1 | Out-Null
        if (Test-SacBlocked $Log) { Write-Host '⚠ 模块被 Smart App Control 拦截（环境问题，非代码失败）'; return $false }
        if ($LASTEXITCODE -ne 0) { Write-Host "commandlet exit=$LASTEXITCODE"; return $false }
        return (Select-String -Path $Log -Pattern 'statetree_verify_finished Result=Succeeded' -Quiet)
    }
}

# —— automation ——
if ($Stage -in @('all', 'automation')) {
    $StageResults['automation'] = Invoke-Stage 'automation' {
        $ReportDir = "$OutDir\Automation"
        New-Item -ItemType Directory -Force -Path "$ReportDir\Report" | Out-Null
        $Log = "$ReportDir\Automation.log"
        & "$Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Project" -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
            '-ExecCmds=Automation RunTests Catfishing;Quit' '-TestExit=Automation Test Queue Empty' `
            "-ReportExportPath=$ReportDir\Report" "-abslog=$Log" 2>&1 | Out-Null
        if (Test-SacBlocked $Log) { Write-Host '⚠ 模块被 Smart App Control 拦截（环境问题，非代码失败）'; return $false }
        $Index = "$ReportDir\Report\index.json"
        if (-not (Test-Path $Index)) { Write-Host '没有生成测试报告'; return $false }
        $Report = Get-Content -Raw $Index | ConvertFrom-Json
        $Total = $Report.tests.Count
        Write-Host ("succeeded={0} warn={1} failed={2} notRun={3} total={4}" -f `
            $Report.succeeded, $Report.succeededWithWarnings, $Report.failed, $Report.notRun, $Total)
        if ($Report.failed -gt 0 -or $Report.notRun -gt 0) {
            $Report.tests | Where-Object { $_.state -ne 'Success' } | Select-Object -First 10 |
                ForEach-Object { Write-Host "  非通过: $($_.fullTestPath)" }
            return $false
        }
        if ($Total -lt $MinimumTestCount) { Write-Host "测试条数 $Total 低于下限 $MinimumTestCount——有测试整文件消失了"; return $false }
        return $true
    }
}

# —— smoke ——
if ($Stage -in @('all', 'smoke')) {
    $StageResults['smoke'] = Invoke-Stage 'smoke' {
        powershell -ExecutionPolicy Bypass -File "$Root\Tools\run_multiplayer_smoke.ps1" | ForEach-Object { Write-Host $_ }
        return ($LASTEXITCODE -eq 0)
    }
}

# —— 汇总 ——
Write-Host "`n===== 汇总（$OutDir） =====" -ForegroundColor Cyan
$ExitCode = 0
$Index = 0
foreach ($Entry in $StageResults.GetEnumerator()) {
    $Index++
    Write-Host ("  {0,-12} {1}" -f $Entry.Key, $(if ($Entry.Value) { '✓ 通过' } else { '✗ 失败' }))
    if (-not $Entry.Value -and $ExitCode -eq 0) { $ExitCode = $Index }
}
exit $ExitCode
