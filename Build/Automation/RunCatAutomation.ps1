param(
  [Parameter(Mandatory=$true)][string]$Filter,
  [Parameter(Mandatory=$true)][string]$RunName,
  # 引擎装在哪台机器上都不一样；默认读 $env:UE_ROOT，协作仓库不写死绝对路径。
  [string]$EngineRoot = $env:UE_ROOT
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $EngineRoot) {
    throw '请先设置 $env:UE_ROOT 指向本机 UE 5.8 安装目录，或用 -EngineRoot 传入；这是协作仓库，脚本里不写死绝对路径。'
}
# 工程根按脚本自身位置推导（本文件在 <工程根>/Build/Automation/ 下）。
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$run = Join-Path $root "Saved\Automation\$RunName"
$report = Join-Path $run 'Report'
if (Test-Path $run) { throw "Automation run already exists: $run" }
New-Item -ItemType Directory -Path $report -Force | Out-Null
& (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') "$root\Catfishing.uproject" `
  -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache `
  "-ExecCmds=Automation RunTests $Filter;Quit" `
  '-TestExit=Automation Test Queue Empty' `
  "-ReportExportPath=$report" "-abslog=$run\Automation.log"
$editorExit = $LASTEXITCODE
if (-not (Test-Path "$report\index.json")) { throw "Missing Automation report for $Filter" }
$index = Get-Content -Raw -Encoding UTF8 "$report\index.json" | ConvertFrom-Json
if ($index.tests.Count -le 0) { throw "No tests ran for $Filter" }
if ($index.failed -ne 0 -or $index.notRun -ne 0 -or $index.inProcess -ne 0) {
  throw "Automation failed: failed=$($index.failed) notRun=$($index.notRun) inProcess=$($index.inProcess)"
}
if ($editorExit -ne 0) { throw "UnrealEditor-Cmd exited $editorExit for $Filter" }
if (Select-String -Path "$run\Automation.log" -Pattern 'Fatal error:|Assertion failed:|Unhandled Exception:' -Quiet) {
  throw "Severe engine failure found in Automation.log for $Filter"
}
$index | Select-Object succeeded,succeededWithWarnings,failed,notRun,inProcess,totalDuration
