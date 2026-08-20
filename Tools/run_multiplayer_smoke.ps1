# 双进程真实联机冒烟测试
#
# 为什么存在：项目所有自动化都在单进程单 World 里跑，复制（Replication）路径一条都没验证过——
# 快照能不能序列化过网线、远端客户端能不能通过真实准入、Pawn 会不会在客户端生成，这些只有真开两个进程才知道。
# 本脚本起一个监听服务器进程 + 一个客户端进程（同机回环，NULL 在线子系统），用双方日志里的结构化事件断言：
#
#   服务器侧必须出现：
#     identity_reserved            —— 远端客户端通过真实 PreLogin 准入（不是 PIE 后门）
#     lake_postlogin_complete ×2   —— 本机主机玩家 + 远端客户端都完成入局并拿到 CatCharacter
#     run_started                  —— 一局真的开起来（ST_RunFlow）
#   客户端侧必须出现：
#     run_snapshot_received        —— Run 公开快照真的复制到了客户端（含全部嵌套 DTO 的网络序列化）
#     ui_survival_view_created     —— 客户端 LocalPlayer 管线成立（占有 Pawn + 状态 View 装配）
#   双方都不允许出现：
#     identity_prelogin_rejected / Fatal error / GetLastError=4551（模块被系统拦截）
#
# 已知边界（如实声明，不是疏漏）：
#   - 用 -nosteam 走 NULL 在线子系统，验证的是"复制与准入"这一层；Steam 真实会话、邀请、双账号仍需人工验收。
#   - 冒烟只验证"加入 + 复制 + 生成"，不从客户端驱动玩法命令（客户端进程没有可脚本化的输入通道）。
#
# 用法：powershell -File Tools\run_multiplayer_smoke.ps1 [-TimeoutSeconds 240]
# 退出码：0=通过，1=失败（缺必需标记或出现禁止标记），2=环境问题（进程没起来/被系统拦截）

param(
    [int]$TimeoutSeconds = 240
)

$ErrorActionPreference = 'Stop'
$Engine = 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$Project = 'D:\UnreaProjects\Catfishing\Catfishing.uproject'
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$OutDir = "D:\UnreaProjects\Catfishing\Saved\MultiplayerSmoke\$Stamp"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$ServerLog = "$OutDir\server.log"
$ClientLog = "$OutDir\client.log"

# 双方都开 Verbose：客户端要看 run_snapshot_received（Verbose 级），服务器顺便留完整现场。
$LogCmds = 'LogCatRun Verbose, LogCatOnline Verbose, LogCatfishing Verbose, LogCatUI Verbose'

Write-Host "[smoke] 输出目录: $OutDir"

# —— 起监听服务器：直接进 Lake 并挂 ?listen；-nosteam 让在线子系统退回 NULL，回环联机不依赖 Steam 进程 ——
$ServerArgs = @(
    "`"$Project`"", '/Game/Catfishing/Maps/Lake?listen',
    '-game', '-nullrhi', '-unattended', '-nosplash', '-nosteam', '-ForceLogFlush', '-DDC-ForceMemoryCache',
    "-LogCmds=`"$LogCmds`"", "-abslog=`"$ServerLog`""
)
$Server = Start-Process -FilePath $Engine -ArgumentList $ServerArgs -PassThru -WindowStyle Hidden
Write-Host "[smoke] 服务器进程 PID=$($Server.Id)"

function Wait-ForPattern {
    param([string]$Path, [string]$Pattern, [int]$Seconds, [System.Diagnostics.Process[]]$MustLive)
    $Deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $Deadline) {
        foreach ($P in $MustLive) {
            if ($P.HasExited) { return $false }
        }
        if ((Test-Path $Path) -and (Select-String -Path $Path -Pattern $Pattern -Quiet)) { return $true }
        Start-Sleep -Milliseconds 1500
    }
    return $false
}

function Stop-Procs {
    param([System.Diagnostics.Process[]]$Procs)
    foreach ($P in $Procs) {
        if ($P -and -not $P.HasExited) { Stop-Process -Id $P.Id -Force -ErrorAction SilentlyContinue }
    }
}

# 服务器就绪判据：一局真的开起来。这一步失败通常是环境问题（模块被拦/地图加载失败），单独给退出码 2。
if (-not (Wait-ForPattern -Path $ServerLog -Pattern 'Event=run_started' -Seconds $TimeoutSeconds -MustLive @($Server))) {
    $Blocked = (Test-Path $ServerLog) -and (Select-String -Path $ServerLog -Pattern 'GetLastError=4551' -Quiet)
    Write-Host "[smoke] 失败：服务器在 $TimeoutSeconds 秒内没有开局$(if ($Blocked) { '（模块被 Smart App Control 拦截）' })"
    Stop-Procs @($Server)
    exit 2
}
Write-Host '[smoke] 服务器已开局，启动客户端…'

# —— 起客户端：连回环地址，走真实 PreLogin/Login/Travel ——
$ClientArgs = @(
    "`"$Project`"", '127.0.0.1',
    '-game', '-nullrhi', '-unattended', '-nosplash', '-nosteam', '-ForceLogFlush', '-DDC-ForceMemoryCache',
    "-LogCmds=`"$LogCmds`"", "-abslog=`"$ClientLog`""
)
$Client = Start-Process -FilePath $Engine -ArgumentList $ClientArgs -PassThru -WindowStyle Hidden
Write-Host "[smoke] 客户端进程 PID=$($Client.Id)"

# 客户端就绪判据：Run 快照真的复制过来了。
$ClientJoined = Wait-ForPattern -Path $ClientLog -Pattern 'Event=run_snapshot_received' -Seconds $TimeoutSeconds -MustLive @($Server, $Client)
# 状态 View 要等 Pawn 复制到客户端并触发占有通知，是最后一个到位的标记；必须在进程活着时轮询，
# 杀进程会把没刷盘的日志尾巴一起带走。
if ($ClientJoined) {
    Wait-ForPattern -Path $ClientLog -Pattern 'Event=ui_survival_view_created' -Seconds 90 -MustLive @($Server, $Client) | Out-Null
}
Stop-Procs @($Server, $Client)
Start-Sleep -Seconds 3

# —— 断言 ——
$Failures = New-Object System.Collections.Generic.List[string]

function Assert-Present {
    param([string]$Path, [string]$Pattern, [string]$What, [int]$MinCount = 1)
    $Count = 0
    if (Test-Path $Path) { $Count = (Select-String -Path $Path -Pattern $Pattern -AllMatches | Measure-Object).Count }
    if ($Count -lt $MinCount) { $Failures.Add("缺少标记: $What（要 $MinCount 个，实得 $Count 个）") }
    else { Write-Host "[smoke] OK  $What ×$Count" }
}

function Assert-Absent {
    param([string]$Path, [string]$Pattern, [string]$What)
    if ((Test-Path $Path) -and (Select-String -Path $Path -Pattern $Pattern -Quiet)) {
        $Line = (Select-String -Path $Path -Pattern $Pattern | Select-Object -First 1).Line
        $Failures.Add("出现禁止标记: $What → $($Line.Substring([Math]::Max(0, $Line.Length - 160)))")
    }
}

if (-not $ClientJoined) { $Failures.Add("客户端在 $TimeoutSeconds 秒内没有收到 Run 快照复制") }

Assert-Present -Path $ServerLog -Pattern 'Event=run_started'              -What '服务器: 一局开起来'
Assert-Present -Path $ServerLog -Pattern 'Event=identity_reserved'       -What '服务器: 真实 PreLogin 准入' -MinCount 2
Assert-Present -Path $ServerLog -Pattern 'Event=lake_postlogin_complete' -What '服务器: 入局完成（主机+远端）' -MinCount 2
Assert-Present -Path $ClientLog -Pattern 'Event=run_snapshot_received'   -What '客户端: 收到 Run 复制快照'
Assert-Present -Path $ClientLog -Pattern 'Event=ui_survival_view_created' -What '客户端: LocalPlayer 管线成立'

Assert-Absent -Path $ServerLog -Pattern 'identity_prelogin_rejected' -What '服务器: 准入被拒'
Assert-Absent -Path $ServerLog -Pattern 'GetLastError=4551'          -What '服务器: 模块被系统拦截'
Assert-Absent -Path $ClientLog -Pattern 'GetLastError=4551'          -What '客户端: 模块被系统拦截'
Assert-Absent -Path $ServerLog -Pattern 'Fatal error'                -What '服务器: 崩溃'
Assert-Absent -Path $ClientLog -Pattern 'Fatal error'                -What '客户端: 崩溃'

if ($Failures.Count -gt 0) {
    Write-Host "`n[smoke] 失败（$($Failures.Count) 条）："
    $Failures | ForEach-Object { Write-Host "  - $_" }
    Write-Host "[smoke] 完整日志: $OutDir"
    exit 1
}
Write-Host "`n[smoke] 通过：客户端经真实准入加入监听服务器，Run 快照复制成立。日志: $OutDir"
exit 0
