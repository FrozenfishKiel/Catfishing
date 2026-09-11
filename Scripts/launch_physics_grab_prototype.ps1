param(
    [ValidateSet('Solo', 'Host', 'Client')][string]$Mode = 'Solo',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName = 'PhysicsGrabPrototype-20260909',
    [ValidatePattern('^[A-Za-z0-9.\-]+$')][string]$Address = '127.0.0.1',
    [ValidateRange(1024, 65535)][int]$Port = 7779,
    [string]$EngineRoot = 'D:\UE_5.8'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ValidationRoot = Join-Path $ProjectRoot "Saved\Validation\$RunName"
$ProjectFile = Join-Path $ValidationRoot 'Catfishing.uproject'
$Editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$Map = '/Game/Catfishing/Prototypes/PhysicsGrabPrototype'
foreach ($Required in @($Editor, $ProjectFile, (Join-Path $ValidationRoot 'Binaries\Win64\UnrealEditor-Catfishing.dll'),
    (Join-Path $ProjectRoot 'Content\Catfishing\Prototypes\PhysicsGrabPrototype.umap'))) {
    if (-not (Test-Path -LiteralPath $Required)) {
        throw "Missing prototype dependency: $Required. Run verify_physics_grab_prototype.ps1 Prepare, BuildEditor and CreateMap first."
    }
}

# These settings belong only to the disposable validation copy. The active project keeps its Steam configuration.
$PrivateEngineConfig = Join-Path $ValidationRoot 'Config\DefaultEngine.ini'
$BaseEngineConfig = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Config\DefaultEngine.ini') -Raw
$LocalTransport = @'

[/Script/Engine.GameEngine]
!NetDriverDefinitions=ClearArray
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/OnlineSubsystemUtils.IpNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")
+NetDriverDefinitions=(DefName="DemoNetDriver",DriverClassName="/Script/Engine.DemoNetDriver",DriverClassNameFallback="/Script/Engine.DemoNetDriver")

[/Script/OnlineSubsystemUtils.IpNetDriver]
NetServerMaxTickRate=60
'@
[IO.File]::WriteAllText($PrivateEngineConfig, $BaseEngineConfig + $LocalTransport, [Text.UTF8Encoding]::new($false))
$Destination = switch ($Mode) {
    'Host' { "$Map`?listen" }
    'Client' { "$Address`:$Port" }
    default { $Map }
}
$Arguments = @('"' + $ProjectFile + '"', $Destination, '-game', '-windowed', '-ResX=1280', '-ResY=720',
    '-nosteam', '-nosplash', "-port=$Port", '-DDC=InstalledNoZenLocalFallback')
# This entry point is an interactive trial requested by the user. Background validation uses the separate verify script.
$Process = Start-Process -FilePath $Editor -ArgumentList $Arguments -WorkingDirectory $ValidationRoot -PassThru
Write-Host "Physics grab prototype opened: Mode=$Mode PID=$($Process.Id)"
Write-Host "WASD move; mouse aim; hold LMB/RMB to reach and grip; release to let go; Space jump; R reset; Tab switch cat; F1 contact markers."
Write-Host "Default on-disk logs: $ValidationRoot\Saved\Logs (no -log console required)."
