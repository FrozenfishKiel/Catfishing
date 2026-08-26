param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("human_playtest", "multi_client_runtime")]
    [string]$SourceKind,

    [Parameter(Mandatory = $false)]
    [string]$RecordedOn = (Get-Date -Format "yyyy-MM-dd"),

    [Parameter(Mandatory = $true)]
    [ValidateRange(2, 64)]
    [int]$ParticipantsCount,

    [Parameter(Mandatory = $true)]
    [string]$Summary,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string[]]$EvidenceRef,

    [Parameter(Mandatory = $false)]
    [string]$Notes = "",

    [Parameter(Mandatory = $false)]
    [switch]$ConfirmAllCoverage,

    [Parameter(Mandatory = $false)]
    [string]$EvidenceFile = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$DefaultEvidenceRelativePath = ".codex\state\run-environment-social-behavior-evidence.json"
$ExpectedCoverageIds = @(
    "SameLakeRunTwoPlayersThroughFrontendOnline",
    "RunFlowAndEnvironmentVisibleInActiveRun",
    "WaterQueryAndChumUseSingleAuthoritySurface",
    "SocialAndTheftAreServerAuthoritative",
    "ReconnectFailClosedBoundaryRemainsClosed"
)

function Resolve-BehaviorEvidencePath {
    <# 路径解析流程：优先使用调用方传入的测试/真实文件；未传时固定落到项目唯一 BehaviorEvidence 状态文件，避免产生第二份影子证据。 #>
    if ([string]::IsNullOrWhiteSpace($EvidenceFile)) {
        return Join-Path $ProjectRoot $DefaultEvidenceRelativePath
    }
    if ([System.IO.Path]::IsPathRooted($EvidenceFile)) {
        return $EvidenceFile
    }
    return Join-Path $ProjectRoot $EvidenceFile
}

function Assert-RecorderInput {
    <# 输入校验流程：先拒绝未确认完整覆盖的调用，再清洗摘要和引用；任何缺口都在写文件前失败，防止半场实测被记录成模块通过。 #>
    if (-not $ConfirmAllCoverage) {
        throw "RunEnvironmentSocial behavior evidence recording requires -ConfirmAllCoverage because partial coverage is not accepted."
    }
    if ([string]::IsNullOrWhiteSpace($RecordedOn)) {
        throw "RunEnvironmentSocial behavior evidence requires a recorded date."
    }
    if ([string]::IsNullOrWhiteSpace($Summary)) {
        throw "RunEnvironmentSocial behavior evidence requires a summary."
    }
    $CleanRefs = @($EvidenceRef | ForEach-Object { $_.Trim() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($CleanRefs.Count -lt 1) {
        throw "RunEnvironmentSocial behavior evidence requires at least one evidence reference."
    }
    return $CleanRefs
}

function Assert-BehaviorEvidenceSchema {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Evidence
    )
    <# 结构校验流程：确认当前文件仍是 RunEnvironmentSocial 的单原子证据入口，并且五个覆盖项完整存在；多余或缺失项都说明清单开始碎片化。 #>
    if ($Evidence.module_key -ne "RunEnvironmentSocial") {
        throw ("RunEnvironmentSocial behavior evidence module_key mismatch: {0}" -f $Evidence.module_key)
    }
    if ($Evidence.evidence_id -ne "RunEnvironmentSocial.ModuleLevelHumanOrMultiClientBehaviorEvidence") {
        throw ("RunEnvironmentSocial behavior evidence id mismatch: {0}" -f $Evidence.evidence_id)
    }
    if ($Evidence.delivery_mode -ne "single_atomic_module" -or $Evidence.evidence_mode -ne "human_or_multi_client") {
        throw "RunEnvironmentSocial behavior evidence must stay a single human_or_multi_client module record."
    }
    if (@($Evidence.accepted_source_kinds) -notcontains $SourceKind) {
        throw ("RunEnvironmentSocial behavior evidence source_kind is not accepted by the file contract: {0}" -f $SourceKind)
    }
    $Coverage = @($Evidence.coverage_requirements)
    if ($Coverage.Count -ne $ExpectedCoverageIds.Count) {
        throw ("RunEnvironmentSocial behavior evidence coverage count mismatch: {0}" -f $Coverage.Count)
    }
    foreach ($CoverageId in $ExpectedCoverageIds) {
        $Matches = @($Coverage | Where-Object { $_.coverage_id -eq $CoverageId })
        if ($Matches.Count -ne 1) {
            throw ("RunEnvironmentSocial behavior evidence coverage mismatch: {0} matches={1}" -f $CoverageId, $Matches.Count)
        }
        if ([string]::IsNullOrWhiteSpace($Matches[0].proof_required)) {
            throw ("RunEnvironmentSocial behavior evidence coverage missing proof text: {0}" -f $CoverageId)
        }
    }
    foreach ($Entry in $Coverage) {
        if ($ExpectedCoverageIds -notcontains $Entry.coverage_id) {
            throw ("RunEnvironmentSocial behavior evidence has an unknown coverage id: {0}" -f $Entry.coverage_id)
        }
    }
}

function Set-BehaviorEvidencePass {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Evidence,
        [Parameter(Mandatory = $true)]
        [string[]]$CleanEvidenceRefs
    )
    <# 写入流程：只在同一个 JSON 中整体切换状态；五个 coverage 一起变为 pass，并把实测来源、人数、摘要和引用作为同一条模块级记录保存。 #>
    $Evidence.status = "pass"
    foreach ($Entry in @($Evidence.coverage_requirements)) {
        $Entry.status = "pass"
    }
    $Evidence.evidence_record.source_kind = $SourceKind
    $Evidence.evidence_record.recorded_on = $RecordedOn
    $Evidence.evidence_record.participants_count = $ParticipantsCount
    $Evidence.evidence_record.summary = $Summary.Trim()
    $Evidence.evidence_record.evidence_refs = @($CleanEvidenceRefs)
    $Evidence.evidence_record.notes = $Notes.Trim()
}

function Save-BehaviorEvidence {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [object]$Evidence
    )
    <# 保存流程：确保目标目录存在后用足够 JSON 深度覆盖原文件；调用方已经完成结构校验，所以这里不再生成任何旁路状态。 #>
    $Directory = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        New-Item -ItemType Directory -Path $Directory -Force | Out-Null
    }
    $Evidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding UTF8
}

$BehaviorEvidencePath = Resolve-BehaviorEvidencePath
if (-not (Test-Path -LiteralPath $BehaviorEvidencePath -PathType Leaf)) {
    throw ("RunEnvironmentSocial behavior evidence file is missing: {0}" -f $BehaviorEvidencePath)
}

$CleanEvidenceRefs = Assert-RecorderInput
$Evidence = Get-Content -LiteralPath $BehaviorEvidencePath -Raw -Encoding UTF8 | ConvertFrom-Json
Assert-BehaviorEvidenceSchema -Evidence $Evidence
Set-BehaviorEvidencePass -Evidence $Evidence -CleanEvidenceRefs $CleanEvidenceRefs
Save-BehaviorEvidence -Path $BehaviorEvidencePath -Evidence $Evidence
Write-Host ("RUN_ENVIRONMENT_SOCIAL_BEHAVIOR_EVIDENCE_RECORDED EvidenceFile={0} SourceKind={1} Participants={2} Coverage={3}" -f
    $BehaviorEvidencePath, $SourceKind, $ParticipantsCount, ($ExpectedCoverageIds -join ","))
