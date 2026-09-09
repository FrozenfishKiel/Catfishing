param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Prepare', 'BuildEditor', 'BuildGame', 'CreateMap', 'Automation')]
    [string]$Mode,
    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$RunName = 'PhysicsGrabPrototype-20260909',
    [string]$EngineRoot = 'D:\UE_5.8',
    [string]$Filter = 'Catfishing.PhysicsGrabPrototype',
    [switch]$Render
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$ValidationRoot = Join-Path $ProjectRoot "Saved\Validation\$RunName"
$EvidenceRoot = Join-Path $ProjectRoot "Saved\Automation\$RunName"
$ProjectFile = Join-Path $ValidationRoot 'Catfishing.uproject'
$BuildTool = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$Editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

function Assert-WorkspacePath([string]$Path) {
    $Resolved = [IO.Path]::GetFullPath($Path)
    if (-not $Resolved.StartsWith($ProjectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Validation output escapes project workspace: $Resolved"
    }
}

function Sync-ValidationSource {
    # 独立 Source/Config/Binaries/Intermediate，Content 只共享项目资产；不关闭正在使用正式 DLL 的编辑器。
    Assert-WorkspacePath $ValidationRoot
    Assert-WorkspacePath $EvidenceRoot
    New-Item -ItemType Directory -Path $ValidationRoot, $EvidenceRoot -Force | Out-Null
    foreach ($Directory in @('Source', 'Config')) {
        $Destination = Join-Path $ValidationRoot $Directory
        New-Item -ItemType Directory -Path $Destination -Force | Out-Null
        Copy-Item -Path (Join-Path $ProjectRoot "$Directory\*") -Destination $Destination -Recurse -Force
    }
    Copy-Item -LiteralPath (Join-Path $ProjectRoot 'Catfishing.uproject') -Destination $ProjectFile -Force
    $Content = Join-Path $ValidationRoot 'Content'
    $ExpectedContent = Join-Path $ProjectRoot 'Content'
    if (Test-Path -LiteralPath $Content) {
        $Item = Get-Item -LiteralPath $Content
        if ($Item.LinkType -ne 'Junction' -or [IO.Path]::GetFullPath($Item.Target) -ne $ExpectedContent) {
            throw "Validation Content must be a junction to the project Content directory: $Content"
        }
    } else {
        New-Item -ItemType Junction -Path $Content -Target $ExpectedContent | Out-Null
    }
    $Manifest = foreach ($File in Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'Source'), (Join-Path $ProjectRoot 'Config') -File -Recurse) {
        $Relative = [IO.Path]::GetRelativePath($ProjectRoot, $File.FullName)
        $SourceHash = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash
        $CopyHash = (Get-FileHash -LiteralPath (Join-Path $ValidationRoot $Relative) -Algorithm SHA256).Hash
        if ($SourceHash -ne $CopyHash) { throw "Validation source mismatch: $Relative" }
        [pscustomobject]@{ Path = $Relative; SHA256 = $SourceHash }
    }
    $Manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'SourceManifest.json') -Encoding utf8
    Write-Host "PHYSICS_GRAB_PREPARE_PASS Files=$($Manifest.Count) Project=$ProjectFile"
}

function Get-FreshEvidencePath([string]$Name, [string]$Extension) {
    $Stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    return Join-Path $EvidenceRoot "$Name-$Stamp.$Extension"
}

if ($Mode -eq 'Prepare') {
    Sync-ValidationSource
    exit 0
}

if (-not (Test-Path -LiteralPath $ProjectFile)) { throw 'Run -Mode Prepare first.' }
if ($Mode -eq 'BuildEditor' -or $Mode -eq 'BuildGame') {
    Sync-ValidationSource
    $Target = if ($Mode -eq 'BuildEditor') { 'CatfishingEditor' } else { 'Catfishing' }
    $Log = Get-FreshEvidencePath $Mode 'log'
    # Source may be copied while another task completes a header edit; regenerate UHT line macros for this snapshot.
    & $BuildTool $Target Win64 Development $ProjectFile -WaitMutex -NoHotReload -NoUBA -ForceHeaderGeneration "-Log=$Log"
    if ($LASTEXITCODE -ne 0) { throw "$Target failed: exit=$LASTEXITCODE log=$Log" }
    Write-Host "PHYSICS_GRAB_BUILD_PASS Target=$Target Log=$Log"
    exit 0
}

if ($Mode -eq 'CreateMap') {
    $Script = Join-Path $ProjectRoot 'Scripts\create_physics_grab_prototype_map.py'
    $Log = Get-FreshEvidencePath 'CreateMap' 'log'
    & $Editor $ProjectFile -unattended -nop4 -nosplash -nullrhi -DDC=InstalledNoZenLocalFallback -DDC-ForceMemoryCache "-ExecutePythonScript=$Script" "-abslog=$Log"
    $EditorExit = $LASTEXITCODE
    $Text = Get-Content -LiteralPath $Log -Raw
    if ($EditorExit -ne 0 -or $Text -match 'LogPython: Error|Fatal error:|Assertion failed:' -or
        ([regex]::Matches($Text, 'PHYSICS_GRAB_MAP_PASS')).Count -ne 1) {
        throw "Prototype map generation failed: exit=$EditorExit log=$Log"
    }
    Write-Host "PHYSICS_GRAB_MAP_VERIFICATION_PASS Log=$Log"
    exit 0
}

$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$Report = Join-Path $EvidenceRoot "Report-$Stamp"
$Log = Join-Path $EvidenceRoot "Automation-$Stamp.log"
New-Item -ItemType Directory -Path $Report -Force | Out-Null
$RenderingOption = if ($Render) { '-RenderOffscreen' } else { '-nullrhi' }
& $Editor $ProjectFile -unattended -nop4 -nosplash $RenderingOption -DDC=InstalledNoZenLocalFallback -DDC-ForceMemoryCache `
    "-ExecCmds=Automation RunTests $Filter;Quit" '-TestExit=Automation Test Queue Empty' `
    "-ReportExportPath=$Report" "-abslog=$Log"
$EditorExit = $LASTEXITCODE
$IndexPath = Join-Path $Report 'index.json'
if (-not (Test-Path -LiteralPath $IndexPath)) { throw "Missing fresh Automation report: $IndexPath" }
$Index = Get-Content -LiteralPath $IndexPath -Raw -Encoding utf8 | ConvertFrom-Json
if ($Index.tests.Count -le 0 -or $Index.failed -ne 0 -or $Index.notRun -ne 0 -or $Index.inProcess -ne 0 -or $EditorExit -ne 0) {
    throw "Prototype Automation failed: count=$($Index.tests.Count) failed=$($Index.failed) notRun=$($Index.notRun) inProcess=$($Index.inProcess) exit=$EditorExit report=$IndexPath"
}
if (Select-String -LiteralPath $Log -Pattern 'Fatal error:|Assertion failed:|Unhandled Exception:' -Quiet) {
    throw "Severe engine failure in $Log"
}
Write-Host "PHYSICS_GRAB_AUTOMATION_PASS Report=$IndexPath Log=$Log"
$Index | Select-Object succeeded, succeededWithWarnings, failed, notRun, inProcess, totalDuration
