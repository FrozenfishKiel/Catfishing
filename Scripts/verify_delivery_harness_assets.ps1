param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Static", "AssetReadiness")]
    [string]$Mode
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$EvidenceRoot = Join-Path $ProjectRoot "Saved\Automation\DeliveryHarnessAssets"
$TranscriptRoot = Join-Path $EvidenceRoot ("{0}-{1}" -f $Mode, (Get-Date -Format "yyyyMMdd-HHmmss"))
$TranscriptLogFile = Join-Path $TranscriptRoot ("{0}.log" -f $Mode)

New-Item -ItemType Directory -Path $TranscriptRoot -Force | Out-Null
Start-Transcript -Path $TranscriptLogFile -Force | Out-Null

function Assert-FileExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )
    <# 交付清单只接受当前工作树里的真实文件；缺文件立即失败，避免把历史报告或口头记忆当成本轮证据入口。 #>
    $Path = Join-Path $ProjectRoot $RelativePath
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw ("DeliveryHarnessAssets missing {0}: {1}" -f $Description, $RelativePath)
    }
}

function Assert-TextContains {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath,
        [Parameter(Mandatory = $true)]
        [string]$Needle,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )
    <# 文档和配置核验只检查稳定合同锚点；它证明缺口被集中记录，不把文字命中冒充正式资产已经就绪。 #>
    $Path = Join-Path $ProjectRoot $RelativePath
    Assert-FileExists $RelativePath $Description
    $Text = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    if (-not $Text.Contains($Needle)) {
        throw ("DeliveryHarnessAssets text contract missing {0}: {1} in {2}" -f $Description, $Needle, $RelativePath)
    }
}

function Assert-TextNotContains {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath,
        [Parameter(Mandatory = $true)]
        [string]$Needle,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )
    <# 负向配置核验只用于历史测试资产名这类稳定禁用锚点；命中即失败，避免旧白盒资产在正式配置中悄悄回流。 #>
    $Path = Join-Path $ProjectRoot $RelativePath
    Assert-FileExists $RelativePath $Description
    $Text = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    if ($Text.Contains($Needle)) {
        throw ("DeliveryHarnessAssets forbidden text still present {0}: {1} in {2}" -f $Description, $Needle, $RelativePath)
    }
}

function Assert-DirectoryTextContains {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativeDirectory,
        [Parameter(Mandatory = $true)]
        [string]$Needle,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )
    <# 面向中文文件名文档的核验入口：按目录和内容查找，不把具体中文文件名写进脚本，避免 Windows PowerShell 编码差异破坏路径。 #>
    $Directory = Join-Path $ProjectRoot $RelativeDirectory
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        throw ("DeliveryHarnessAssets missing directory for {0}: {1}" -f $Description, $RelativeDirectory)
    }
    $Matched = $false
    foreach ($File in Get-ChildItem -LiteralPath $Directory -Filter "*.md" -File) {
        $Text = Get-Content -LiteralPath $File.FullName -Raw -Encoding UTF8
        if ($Text.Contains($Needle)) {
            $Matched = $true
            break
        }
    }
    if (-not $Matched) {
        throw ("DeliveryHarnessAssets directory text contract missing {0}: {1} in {2}" -f $Description, $Needle, $RelativeDirectory)
    }
}

function Assert-HarnessVerification {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Harness,
        [Parameter(Mandatory = $true)]
        [string]$Key,
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$ExpectedScript
    )
    <# Harness verification 入口必须同时存在于机器合同和真实脚本文件；这样换 Agent 时不会只拿到一个看似完成的模块标题。 #>
    if (-not $Harness.verification.PSObject.Properties.Name.Contains($Key)) {
        throw ("DeliveryHarnessAssets harness verification missing key: {0}" -f $Key)
    }
    if ($ExpectedScript) {
        Assert-FileExists $ExpectedScript ("script for {0}" -f $Key)
    }
}

function Assert-FormalFishAssetInputPackage {
    <# 正式鱼资产输入包只作为 DeliveryHarnessAssets 的一个集中交接证据读取；status 必须由 blocking_gaps 是否为空推导，不能把当前缺口清单写成永远必须存在的小任务。 #>
    $PackagePath = Join-Path $ProjectRoot ".harness\formal-fish-asset-input-package.json"
    Assert-FileExists ".harness\formal-fish-asset-input-package.json" "formal fish asset input package"
    $Package = Get-Content -LiteralPath $PackagePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($Package.package_id -ne "FormalFishAssetInputPackage") {
        throw ("FormalFishAssetInputPackage id mismatch: {0}" -f $Package.package_id)
    }
    if ($Package.delivery_module -ne "DeliveryHarnessAssets") {
        throw ("FormalFishAssetInputPackage delivery module mismatch: {0}" -f $Package.delivery_module)
    }
    if (@("blocked", "ready") -notcontains $Package.status) {
        throw ("FormalFishAssetInputPackage unsupported status: {0}" -f $Package.status)
    }
    foreach ($SourceKey in @("fish_table", "fish_book", "fish_owner_decision_sync", "fish_behavior_doc", "bait_table", "eating_effect_doc", "art_resource_sheet")) {
        if (-not $Package.sources.PSObject.Properties.Name.Contains($SourceKey)) {
            throw ("FormalFishAssetInputPackage missing source: {0}" -f $SourceKey)
        }
    }
    foreach ($FieldName in @("BodyClass", "RarityTierId", "BitePersonalityId", "FightPersonalityId", "BaitWeightMultipliers", "bEnableRuntimeDefinition")) {
        if (@($Package.ue_required_fields) -notcontains $FieldName) {
            throw ("FormalFishAssetInputPackage missing UE required field: {0}" -f $FieldName)
        }
    }
    $BlockingGaps = @($Package.blocking_gaps)
    foreach ($GapName in $BlockingGaps) {
        if ([string]::IsNullOrWhiteSpace($GapName)) {
            throw "FormalFishAssetInputPackage has empty blocking gap"
        }
    }
    $ExpectedStatus = if ($BlockingGaps.Count -eq 0) { "ready" } else { "blocked" }
    if ($Package.status -ne $ExpectedStatus) {
        throw ("FormalFishAssetInputPackage status drift: status={0} expected={1}" -f $Package.status, $ExpectedStatus)
    }
}

function Assert-FormalNonFishContentInputPackage {
    <# 非鱼正式内容输入包把表现边界、Unlock、印记、成长和黄色体力缺口收成一个 Delivery 输入面；包级状态和包级 gap 必须从内部 content group 推导，避免它们在 handoff 里散成可独立关闭的小任务。 #>
    $PackagePath = Join-Path $ProjectRoot ".harness\formal-non-fish-content-input-package.json"
    Assert-FileExists ".harness\formal-non-fish-content-input-package.json" "formal non-fish content input package"
    $Package = Get-Content -LiteralPath $PackagePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($Package.package_id -ne "FormalNonFishContentInputPackage") {
        throw ("FormalNonFishContentInputPackage id mismatch: {0}" -f $Package.package_id)
    }
    if ($Package.delivery_module -ne "DeliveryHarnessAssets") {
        throw ("FormalNonFishContentInputPackage delivery module mismatch: {0}" -f $Package.delivery_module)
    }
    if ($Package.delivery_mode -ne "single_atomic_module") {
        throw ("FormalNonFishContentInputPackage delivery_mode mismatch: {0}" -f $Package.delivery_mode)
    }
    if (@("blocked", "ready") -notcontains $Package.status) {
        throw ("FormalNonFishContentInputPackage unsupported status: {0}" -f $Package.status)
    }

    foreach ($SourceKey in @("gap_list", "technical_plan", "release_handoff", "harness_contract")) {
        if (-not $Package.sources.PSObject.Properties.Name.Contains($SourceKey)) {
            throw ("FormalNonFishContentInputPackage missing source: {0}" -f $SourceKey)
        }
        $Source = $Package.sources.$SourceKey
        if ([string]::IsNullOrWhiteSpace($Source.path)) {
            throw ("FormalNonFishContentInputPackage source path is empty: {0}" -f $SourceKey)
        }
        Assert-FileExists $Source.path ("FormalNonFishContentInputPackage source {0}" -f $SourceKey)
    }

    $RequiredGroups = @{
        presentation_boundary = "FormalPresentationBoundaryFreeze"
        progress_growth_content = "FormalProgressGrowthContentFreeze"
    }
    $PackageGapByGroup = @{
        presentation_boundary = "FormalPresentationBoundaryOpen"
        progress_growth_content = "FormalProgressGrowthContentOpen"
    }
    $ExpectedPackageGaps = @()
    foreach ($GroupName in $RequiredGroups.Keys) {
        $GroupProperty = $Package.content_groups.PSObject.Properties | Where-Object { $_.Name -eq $GroupName } | Select-Object -First 1
        if (-not $GroupProperty) {
            throw ("FormalNonFishContentInputPackage missing content group: {0}" -f $GroupName)
        }
        $Group = $GroupProperty.Value
        if ($Group.input_id -ne $RequiredGroups[$GroupName]) {
            throw ("FormalNonFishContentInputPackage group input_id mismatch: {0} input_id={1}" -f $GroupName, $Group.input_id)
        }
        if (@("blocked", "satisfied") -notcontains $Group.current_status) {
            throw ("FormalNonFishContentInputPackage group unsupported status: {0} status={1}" -f $GroupName, $Group.current_status)
        }
        if (@($Group.unblocks_requirements).Count -eq 0 -or @($Group.required_facts).Count -eq 0 -or @($Group.do_not_do).Count -eq 0) {
            throw ("FormalNonFishContentInputPackage group missing required contract fields: {0}" -f $GroupName)
        }
        $GroupBlockingGaps = @($Group.blocking_gaps)
        if ($Group.current_status -eq "blocked" -and $GroupBlockingGaps.Count -eq 0) {
            throw ("FormalNonFishContentInputPackage blocked group has no blocking_gaps: {0}" -f $GroupName)
        }
        if ($Group.current_status -eq "satisfied" -and $GroupBlockingGaps.Count -gt 0) {
            throw ("FormalNonFishContentInputPackage satisfied group still has blocking_gaps: {0}" -f $GroupName)
        }
        if ($Group.current_status -eq "blocked") {
            $ExpectedPackageGaps += $PackageGapByGroup[$GroupName]
        }
        if ($Package.status -eq "ready" -and $Group.current_status -ne "satisfied") {
            throw ("FormalNonFishContentInputPackage cannot be ready while group is not satisfied: {0} status={1}" -f $GroupName, $Group.current_status)
        }
    }

    $PackageBlockingGaps = @($Package.package_blocking_gaps)
    foreach ($GapName in $PackageBlockingGaps) {
        if ($ExpectedPackageGaps -notcontains $GapName) {
            throw ("FormalNonFishContentInputPackage has stale or unknown package gap: {0}" -f $GapName)
        }
    }
    foreach ($ExpectedGap in $ExpectedPackageGaps) {
        if ($PackageBlockingGaps -notcontains $ExpectedGap) {
            throw ("FormalNonFishContentInputPackage missing package gap mirrored from group: {0}" -f $ExpectedGap)
        }
    }
    $ExpectedPackageStatus = if ($ExpectedPackageGaps.Count -eq 0 -and $PackageBlockingGaps.Count -eq 0) { "ready" } else { "blocked" }
    if ($Package.status -ne $ExpectedPackageStatus) {
        throw ("FormalNonFishContentInputPackage status drift: status={0} expected={1}" -f $Package.status, $ExpectedPackageStatus)
    }
}

function Assert-DeliveryHarnessAssetsMachineStatus {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )
    <# DeliveryHarnessAssets 机器状态源是给后续接手者看的单一状态快照；Static 必须确认它存在、指向模块级输入，并且快照状态来自权威包/证据而不是第二份手写结论。 #>
    Assert-FileExists $RelativePath "DeliveryHarnessAssets machine status source"
    $StatePath = Join-Path $ProjectRoot $RelativePath
    $State = Get-Content -LiteralPath $StatePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($State.module_key -ne "DeliveryHarnessAssets") {
        throw ("DeliveryHarnessAssets machine status module_key mismatch: {0}" -f $State.module_key)
    }
    if ($State.delivery_mode -ne "single_atomic_module") {
        throw ("DeliveryHarnessAssets machine status delivery_mode mismatch: {0}" -f $State.delivery_mode)
    }
    if (@("blocked", "complete") -notcontains $State.closure_status) {
        throw ("DeliveryHarnessAssets machine status unsupported closure_status: {0}" -f $State.closure_status)
    }

    $InputPackages = $State.input_packages
    $ExpectedInputPackages = @{
        formal_fish_asset_input_package = ".harness/formal-fish-asset-input-package.json"
        formal_non_fish_content_input_package = ".harness/formal-non-fish-content-input-package.json"
        run_environment_social_behavior_evidence = ".codex/state/run-environment-social-behavior-evidence.json"
        release_handoff_package = ".harness/delivery-release-handoff-package.json"
    }
    foreach ($InputName in $ExpectedInputPackages.Keys) {
        $InputProperty = $InputPackages.PSObject.Properties | Where-Object { $_.Name -eq $InputName } | Select-Object -First 1
        if (-not $InputProperty) {
            throw ("DeliveryHarnessAssets machine status missing input package: {0}" -f $InputName)
        }
        $Input = $InputProperty.Value
        if ($Input.path -ne $ExpectedInputPackages[$InputName]) {
            throw ("DeliveryHarnessAssets machine status input path mismatch: {0} path={1}" -f $InputName, $Input.path)
        }
        Assert-FileExists $Input.path ("DeliveryHarnessAssets machine status input {0}" -f $InputName)
    }

    <# 输入状态镜像流程：逐个读取权威文件，再要求机器快照与权威状态一致；这样未来某个包改为 ready/pass 后，旧快照不能继续假装 blocked。 #>
    $FormalPackage = Get-Content -LiteralPath (Join-Path $ProjectRoot $ExpectedInputPackages.formal_fish_asset_input_package) -Raw -Encoding UTF8 | ConvertFrom-Json
    $NonFishPackage = Get-Content -LiteralPath (Join-Path $ProjectRoot $ExpectedInputPackages.formal_non_fish_content_input_package) -Raw -Encoding UTF8 | ConvertFrom-Json
    $BehaviorEvidence = Get-Content -LiteralPath (Join-Path $ProjectRoot $ExpectedInputPackages.run_environment_social_behavior_evidence) -Raw -Encoding UTF8 | ConvertFrom-Json
    $ReleaseHandoff = Get-Content -LiteralPath (Join-Path $ProjectRoot $ExpectedInputPackages.release_handoff_package) -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($InputPackages.formal_fish_asset_input_package.status -ne $FormalPackage.status) {
        throw ("DeliveryHarnessAssets machine status fish package drift: state={0} package={1}" -f $InputPackages.formal_fish_asset_input_package.status, $FormalPackage.status)
    }
    if ($InputPackages.formal_non_fish_content_input_package.status -ne $NonFishPackage.status) {
        throw ("DeliveryHarnessAssets machine status non-fish package drift: state={0} package={1}" -f $InputPackages.formal_non_fish_content_input_package.status, $NonFishPackage.status)
    }
    if ($InputPackages.run_environment_social_behavior_evidence.status -ne $BehaviorEvidence.status) {
        throw ("DeliveryHarnessAssets machine status behavior evidence drift: state={0} evidence={1}" -f $InputPackages.run_environment_social_behavior_evidence.status, $BehaviorEvidence.status)
    }
    if ($InputPackages.release_handoff_package.status -ne $ReleaseHandoff.package_status -or $InputPackages.release_handoff_package.delivery_module_status -ne $ReleaseHandoff.delivery_module_status) {
        throw ("DeliveryHarnessAssets machine status release handoff drift: statePackage={0} package={1} stateDelivery={2} delivery={3}" -f $InputPackages.release_handoff_package.status, $ReleaseHandoff.package_status, $InputPackages.release_handoff_package.delivery_module_status, $ReleaseHandoff.delivery_module_status)
    }
    if ($State.closure_status -ne $ReleaseHandoff.delivery_module_status) {
        throw ("DeliveryHarnessAssets machine closure status drift: state={0} release={1}" -f $State.closure_status, $ReleaseHandoff.delivery_module_status)
    }
    if ($FormalPackage.status -eq "blocked" -or $NonFishPackage.status -eq "blocked" -or $BehaviorEvidence.status -eq "blocked" -or $ReleaseHandoff.delivery_module_status -eq "blocked") {
        if ($State.closure_status -ne "blocked") {
            throw ("DeliveryHarnessAssets machine status must remain blocked while mirrored inputs are open: {0}" -f $State.closure_status)
        }
    }

    $ExpectedTrackedRequirements = @("FormalFishAssetInputPackageReady", "RuntimeConfigUsesFormalFishAssets", "FormalPresentationBoundaryClosed", "FormalProgressGrowthContentClosed", "RunEnvironmentSocialClosed")
    foreach ($RequirementId in $ExpectedTrackedRequirements) {
        if (@($State.tracked_requirements) -notcontains $RequirementId) {
            throw ("DeliveryHarnessAssets machine status missing tracked requirement: {0}" -f $RequirementId)
        }
    }
    <# open_requirements 只允许列仍未满足的关闭条件；全量门槛另放 tracked_requirements，避免接手者把已满足的鱼资产和运行配置条件误读成还没收。 #>
    $ReleaseRequirements = @($ReleaseHandoff.closure_requirements)
    foreach ($RequirementId in $ExpectedTrackedRequirements) {
        $ReleaseRequirement = $ReleaseRequirements | Where-Object { $_.requirement_id -eq $RequirementId } | Select-Object -First 1
        if (-not $ReleaseRequirement) {
            throw ("DeliveryHarnessAssets machine status cannot find release requirement: {0}" -f $RequirementId)
        }
        $IsOpen = $ReleaseRequirement.current_status -ne "satisfied"
        $IsListedOpen = @($State.open_requirements) -contains $RequirementId
        if ($IsOpen -and -not $IsListedOpen) {
            throw ("DeliveryHarnessAssets machine status missing open requirement: {0}" -f $RequirementId)
        }
        if (-not $IsOpen -and $IsListedOpen) {
            throw ("DeliveryHarnessAssets machine status lists satisfied requirement as open: {0}" -f $RequirementId)
        }
    }
    foreach ($ForbiddenSplit in @("AssetReadiness task", "Buff task", "UnlockId task", "Imprint task", "presentation task", "weather task", "chum task", "help task", "protection sign task", "theft task", "reconnect task")) {
        if (@($State.do_not_split_into) -notcontains $ForbiddenSplit) {
            throw ("DeliveryHarnessAssets machine status missing do-not-split guard: {0}" -f $ForbiddenSplit)
        }
    }

    foreach ($EvidenceProperty in $State.machine_evidence.PSObject.Properties) {
        $Evidence = $EvidenceProperty.Value
        if ([string]::IsNullOrWhiteSpace($Evidence.path) -or [string]::IsNullOrWhiteSpace($Evidence.expected_text)) {
            throw ("DeliveryHarnessAssets machine status evidence entry incomplete: {0}" -f $EvidenceProperty.Name)
        }
        Assert-TextContains $Evidence.path $Evidence.expected_text ("DeliveryHarnessAssets machine status evidence {0}" -f $EvidenceProperty.Name)
    }
}

function Assert-AutomationReportContract {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath,
        [Parameter(Mandatory = $true)]
        [object]$Contract
    )
    <# 自动化报告合同验证一份 report index：它只接受无失败、无跳过、无仍在运行的报告；允许 warning 必须由 handoff 包显式声明。 #>
    $ReportPath = Join-Path $ProjectRoot $RelativePath
    Assert-FileExists $RelativePath ("release handoff automation report {0}" -f $Contract.contract_id)
    $Report = Get-Content -LiteralPath $ReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ([int]$Report.failed -ne 0 -or [int]$Report.notRun -ne 0 -or [int]$Report.inProcess -ne 0) {
        throw ("DeliveryReleaseHandoffPackage automation report is not green: {0} failed={1} notRun={2} inProcess={3}" -f $Contract.contract_id, $Report.failed, $Report.notRun, $Report.inProcess)
    }

    $AllowsWarnings = $false
    if ($Contract.PSObject.Properties.Name.Contains("allow_warnings")) {
        $AllowsWarnings = [bool]$Contract.allow_warnings
    }
    if (-not $AllowsWarnings -and [int]$Report.succeededWithWarnings -ne 0) {
        throw ("DeliveryReleaseHandoffPackage automation report has warnings without explicit allowance: {0} warnings={1}" -f $Contract.contract_id, $Report.succeededWithWarnings)
    }

    $Tests = @($Report.tests)
    if ($Tests.Count -eq 0) {
        throw ("DeliveryReleaseHandoffPackage automation report has no tests: {0}" -f $Contract.contract_id)
    }
    foreach ($Test in $Tests) {
        if ($Test.state -ne "Success") {
            throw ("DeliveryReleaseHandoffPackage automation test is not Success: {0} test={1} state={2}" -f $Contract.contract_id, $Test.fullTestPath, $Test.state)
        }
        if (-not $AllowsWarnings -and ([int]$Test.warnings -ne 0 -or [int]$Test.errors -ne 0)) {
            throw ("DeliveryReleaseHandoffPackage automation test has warnings or errors: {0} test={1} warnings={2} errors={3}" -f $Contract.contract_id, $Test.fullTestPath, $Test.warnings, $Test.errors)
        }
    }

    $ExpectedTests = @()
    if ($Contract.PSObject.Properties.Name.Contains("expected_tests")) {
        $ExpectedTests = @($Contract.expected_tests)
    }
    foreach ($ExpectedTest in $ExpectedTests) {
        if ([string]::IsNullOrWhiteSpace($ExpectedTest)) {
            throw ("DeliveryReleaseHandoffPackage has empty expected test in contract: {0}" -f $Contract.contract_id)
        }
        $Matches = @($Tests | Where-Object { $_.fullTestPath -eq $ExpectedTest })
        if ($Matches.Count -ne 1) {
            throw ("DeliveryReleaseHandoffPackage expected test count mismatch: {0} test={1} matches={2}" -f $Contract.contract_id, $ExpectedTest, $Matches.Count)
        }
        if ($Matches[0].state -ne "Success") {
            throw ("DeliveryReleaseHandoffPackage expected test is not Success: {0} test={1} state={2}" -f $Contract.contract_id, $ExpectedTest, $Matches[0].state)
        }
    }
}

function Assert-ReleaseHandoffEvidenceContracts {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Package
    )
    <# 交付证据合同把每个模块的关键 PASS 标记收进同一个 release handoff 包；后续只维护这一个界面，不再散落人工清单。 #>
    if (-not $Package.PSObject.Properties.Name.Contains("evidence_contracts")) {
        throw "DeliveryReleaseHandoffPackage missing evidence_contracts"
    }
    $Contracts = @($Package.evidence_contracts)
    if ($Contracts.Count -lt 8) {
        throw ("DeliveryReleaseHandoffPackage evidence contract count is too small: {0}" -f $Contracts.Count)
    }

    foreach ($Contract in $Contracts) {
        if ([string]::IsNullOrWhiteSpace($Contract.contract_id) -or [string]::IsNullOrWhiteSpace($Contract.module_key) -or [string]::IsNullOrWhiteSpace($Contract.check_kind) -or [string]::IsNullOrWhiteSpace($Contract.path)) {
            throw "DeliveryReleaseHandoffPackage has an incomplete evidence contract"
        }
        if (-not $Package.module_statuses.PSObject.Properties.Name.Contains($Contract.module_key)) {
            throw ("DeliveryReleaseHandoffPackage evidence contract references unknown module: {0}" -f $Contract.module_key)
        }
        switch ($Contract.check_kind) {
            "file_contains" {
                if ([string]::IsNullOrWhiteSpace($Contract.expected_text)) {
                    throw ("DeliveryReleaseHandoffPackage file_contains contract missing expected_text: {0}" -f $Contract.contract_id)
                }
                Assert-TextContains $Contract.path $Contract.expected_text ("release handoff evidence contract {0}" -f $Contract.contract_id)
            }
            "automation_report" {
                Assert-AutomationReportContract $Contract.path $Contract
            }
            default {
                throw ("DeliveryReleaseHandoffPackage unsupported evidence contract kind: {0} kind={1}" -f $Contract.contract_id, $Contract.check_kind)
            }
        }
    }
}

function Assert-ReleaseHandoffClosureRequirements {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Package,
        [Parameter(Mandatory = $true)]
        [object]$FormalPackage,
        [Parameter(Mandatory = $true)]
        [object]$NonFishPackage,
        [Parameter(Mandatory = $true)]
        [object]$RunEnvironmentSocialBehaviorEvidence
    )
    <# 关闭条件校验只检查 DeliveryHarnessAssets 的稳定模块门；正式鱼资产和非鱼内容都只能作为模块级输入包关闭，不能把字段、表现或成长内容拆成散落小任务。 #>
    if (-not $Package.PSObject.Properties.Name.Contains("closure_requirements")) {
        throw "DeliveryReleaseHandoffPackage missing closure_requirements"
    }
    $Requirements = @($Package.closure_requirements)
    $RequiredIds = @(
        "FormalFishAssetInputPackageReady",
        "RuntimeConfigUsesFormalFishAssets",
        "ReleaseEvidenceContractsGreen",
        "EquipmentShopFreshEvidence",
        "FormalPresentationBoundaryClosed",
        "FormalProgressGrowthContentClosed",
        "RunEnvironmentSocialClosed"
    )
    foreach ($RequiredId in $RequiredIds) {
        $Matches = @($Requirements | Where-Object { $_.requirement_id -eq $RequiredId })
        if ($Matches.Count -ne 1) {
            throw ("DeliveryReleaseHandoffPackage closure requirement count mismatch: {0} matches={1}" -f $RequiredId, $Matches.Count)
        }
    }

    $BlockerIds = @($Package.completion_blockers | ForEach-Object { $_.blocker_id })
    foreach ($Requirement in $Requirements) {
        if (@("blocked", "satisfied") -notcontains $Requirement.current_status) {
            throw ("DeliveryReleaseHandoffPackage closure requirement unsupported status: {0} status={1}" -f $Requirement.requirement_id, $Requirement.current_status)
        }
        if ([string]::IsNullOrWhiteSpace($Requirement.proof_when_satisfied)) {
            throw ("DeliveryReleaseHandoffPackage closure requirement missing proof_when_satisfied: {0}" -f $Requirement.requirement_id)
        }
        if ($Requirement.current_status -eq "blocked") {
            if ([string]::IsNullOrWhiteSpace($Requirement.blocked_by) -or $BlockerIds -notcontains $Requirement.blocked_by) {
                throw ("DeliveryReleaseHandoffPackage closure requirement blocked_by is invalid: {0} blocked_by={1}" -f $Requirement.requirement_id, $Requirement.blocked_by)
            }
        }
    }

    $FormalRequirement = ($Requirements | Where-Object { $_.requirement_id -eq "FormalFishAssetInputPackageReady" } | Select-Object -First 1)
    $FormalPackageReady = ($FormalPackage.status -eq "ready" -and @($FormalPackage.blocking_gaps).Count -eq 0)
    if ($FormalPackageReady -and $FormalRequirement.current_status -ne "satisfied") {
        throw "FormalFishAssetInputPackageReady must be satisfied when FormalFishAssetInputPackage is ready with no blocking gaps"
    }
    if (-not $FormalPackageReady -and $FormalRequirement.current_status -ne "blocked") {
        throw "FormalFishAssetInputPackageReady must remain blocked while FormalFishAssetInputPackage is not ready"
    }

    $RuntimeRequirement = ($Requirements | Where-Object { $_.requirement_id -eq "RuntimeConfigUsesFormalFishAssets" } | Select-Object -First 1)
    $DefaultGamePath = Join-Path $ProjectRoot "Config\DefaultGame.ini"
    Assert-FileExists "Config\DefaultGame.ini" "DefaultGame config for closure requirement"
    $DefaultGame = Get-Content -LiteralPath $DefaultGamePath -Raw -Encoding UTF8
    $RuntimeConfigHasTestAssets = $false
    foreach ($AssetName in @("DA_Fish_Test01", "DA_Bite_Test01", "DA_Fight_Test01")) {
        if ($DefaultGame.Contains($AssetName)) {
            $RuntimeConfigHasTestAssets = $true
            break
        }
    }
    if ($RuntimeConfigHasTestAssets -and $RuntimeRequirement.current_status -ne "blocked") {
        throw "RuntimeConfigUsesFormalFishAssets must remain blocked while DefaultGame still references tracked test assets"
    }
    if (-not $RuntimeConfigHasTestAssets -and $RuntimeRequirement.current_status -ne "satisfied") {
        throw "RuntimeConfigUsesFormalFishAssets must be satisfied once DefaultGame no longer references tracked test assets"
    }

    $EquipmentShopEntryForEvidence = ($Package.module_statuses.PSObject.Properties | Where-Object { $_.Name -eq "EquipmentShop" } | Select-Object -First 1).Value
    $EvidenceRequirement = ($Requirements | Where-Object { $_.requirement_id -eq "ReleaseEvidenceContractsGreen" } | Select-Object -First 1)
    if ($EquipmentShopEntryForEvidence.closure_status -eq "complete" -and $EvidenceRequirement.current_status -ne "satisfied") {
        throw "ReleaseEvidenceContractsGreen must be satisfied once EquipmentShop has fresh evidence"
    }
    if ($EquipmentShopEntryForEvidence.closure_status -ne "complete" -and $EvidenceRequirement.current_status -ne "blocked") {
        throw "ReleaseEvidenceContractsGreen must remain blocked while EquipmentShop is waiting for fresh evidence"
    }

    $EquipmentShopRequirement = ($Requirements | Where-Object { $_.requirement_id -eq "EquipmentShopFreshEvidence" } | Select-Object -First 1)
    if ($EquipmentShopEntryForEvidence.closure_status -eq "complete" -and $EquipmentShopRequirement.current_status -ne "satisfied") {
        throw "EquipmentShopFreshEvidence must be satisfied once EquipmentShop is marked complete"
    }
    if ($EquipmentShopEntryForEvidence.closure_status -ne "complete" -and $EquipmentShopRequirement.current_status -ne "blocked") {
        throw "EquipmentShopFreshEvidence must remain blocked until EquipmentShop is marked complete with fresh evidence"
    }

    $PresentationRequirement = ($Requirements | Where-Object { $_.requirement_id -eq "FormalPresentationBoundaryClosed" } | Select-Object -First 1)
    $PresentationGroup = $NonFishPackage.content_groups.presentation_boundary
    $PresentationReady = (
        $NonFishPackage.status -eq "ready" -and
        @($NonFishPackage.package_blocking_gaps).Count -eq 0 -and
        $PresentationGroup.current_status -eq "satisfied" -and
        @($PresentationGroup.blocking_gaps).Count -eq 0
    )
    if ($PresentationReady -and $PresentationRequirement.current_status -ne "satisfied") {
        throw "FormalPresentationBoundaryClosed must be satisfied when FormalNonFishContentInputPackage and presentation_boundary are ready with no blocking gaps"
    }
    if (-not $PresentationReady -and $PresentationRequirement.current_status -ne "blocked") {
        throw "FormalPresentationBoundaryClosed must remain blocked until FormalNonFishContentInputPackage and presentation_boundary are ready"
    }
    if ($Package.delivery_module_status -eq "blocked" -and $BlockerIds -contains "FormalPresentationBoundaryOpen" -and $PresentationRequirement.current_status -ne "blocked") {
        throw "FormalPresentationBoundaryClosed must remain blocked while FormalPresentationBoundaryOpen is listed as a completion blocker"
    }

    $ProgressGrowthRequirement = ($Requirements | Where-Object { $_.requirement_id -eq "FormalProgressGrowthContentClosed" } | Select-Object -First 1)
    $ProgressGrowthGroup = $NonFishPackage.content_groups.progress_growth_content
    $ProgressGrowthReady = (
        $NonFishPackage.status -eq "ready" -and
        @($NonFishPackage.package_blocking_gaps).Count -eq 0 -and
        $ProgressGrowthGroup.current_status -eq "satisfied" -and
        @($ProgressGrowthGroup.blocking_gaps).Count -eq 0
    )
    if ($ProgressGrowthReady -and $ProgressGrowthRequirement.current_status -ne "satisfied") {
        throw "FormalProgressGrowthContentClosed must be satisfied when FormalNonFishContentInputPackage and progress_growth_content are ready with no blocking gaps"
    }
    if (-not $ProgressGrowthReady -and $ProgressGrowthRequirement.current_status -ne "blocked") {
        throw "FormalProgressGrowthContentClosed must remain blocked until FormalNonFishContentInputPackage and progress_growth_content are ready"
    }
    if ($Package.delivery_module_status -eq "blocked" -and $BlockerIds -contains "FormalProgressGrowthContentOpen" -and $ProgressGrowthRequirement.current_status -ne "blocked") {
        throw "FormalProgressGrowthContentClosed must remain blocked while FormalProgressGrowthContentOpen is listed as a completion blocker"
    }

    <# RunEnvironmentSocialClosed 从唯一 BehaviorEvidence 文件推导：只有同一份记录达到 pass、五个覆盖项都通过、且人工/多客户端记录有可追踪来源时才允许 release 条件 satisfied。 #>
    $RunEnvironmentSocialRequirement = ($Requirements | Where-Object { $_.requirement_id -eq "RunEnvironmentSocialClosed" } | Select-Object -First 1)
    if ($RunEnvironmentSocialBehaviorEvidence.module_key -ne "RunEnvironmentSocial" -or $RunEnvironmentSocialBehaviorEvidence.evidence_id -ne "RunEnvironmentSocial.ModuleLevelHumanOrMultiClientBehaviorEvidence") {
        throw "RunEnvironmentSocial behavior evidence identity mismatch for release closure"
    }
    if (@("blocked", "pass") -notcontains $RunEnvironmentSocialBehaviorEvidence.status) {
        throw ("RunEnvironmentSocial behavior evidence unsupported status for release closure: {0}" -f $RunEnvironmentSocialBehaviorEvidence.status)
    }
    $RunEnvironmentSocialCoverage = @($RunEnvironmentSocialBehaviorEvidence.coverage_requirements)
    $RunEnvironmentSocialMissingCoverage = @($RunEnvironmentSocialCoverage | Where-Object { $_.status -ne "pass" })
    $RunEnvironmentSocialRecord = $RunEnvironmentSocialBehaviorEvidence.evidence_record
    $RunEnvironmentSocialBehaviorReady = (
        $RunEnvironmentSocialBehaviorEvidence.status -eq "pass" -and
        $RunEnvironmentSocialCoverage.Count -eq 5 -and
        $RunEnvironmentSocialMissingCoverage.Count -eq 0 -and
        @($RunEnvironmentSocialBehaviorEvidence.accepted_source_kinds) -contains $RunEnvironmentSocialRecord.source_kind -and
        $RunEnvironmentSocialRecord.participants_count -ge 2 -and
        -not [string]::IsNullOrWhiteSpace($RunEnvironmentSocialRecord.recorded_on) -and
        -not [string]::IsNullOrWhiteSpace($RunEnvironmentSocialRecord.summary) -and
        @($RunEnvironmentSocialRecord.evidence_refs).Count -gt 0
    )
    if ($RunEnvironmentSocialBehaviorReady -and $RunEnvironmentSocialRequirement.current_status -ne "satisfied") {
        throw "RunEnvironmentSocialClosed must be satisfied when the module-level BehaviorEvidence record is passed with complete coverage"
    }
    if (-not $RunEnvironmentSocialBehaviorReady -and $RunEnvironmentSocialRequirement.current_status -ne "blocked") {
        throw "RunEnvironmentSocialClosed must remain blocked until the module-level BehaviorEvidence record is passed with complete coverage"
    }
    if ($Package.delivery_module_status -eq "blocked" -and $BlockerIds -contains "RunEnvironmentSocialModuleOpen" -and $RunEnvironmentSocialRequirement.current_status -ne "blocked") {
        throw "RunEnvironmentSocialClosed must remain blocked while RunEnvironmentSocialModuleOpen is listed as a completion blocker"
    }
}

function Assert-ReleaseHandoffRemainingInputContracts {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Package
    )
    <# 剩余输入合同只记录解除 DeliveryHarnessAssets 阻塞所需的稳定输入来源；它不是拆给不同 Agent 的待办清单，状态必须镜像它解锁的关闭条件。 #>
    if (-not $Package.PSObject.Properties.Name.Contains("remaining_input_contracts")) {
        throw "DeliveryReleaseHandoffPackage missing remaining_input_contracts"
    }
    $Contracts = @($Package.remaining_input_contracts)
    $ExpectedContracts = @{
        FormalFishAssetFreeze = @{
            AuthoritySource = ".harness/formal-fish-asset-input-package.json"
            Requirements = @("FormalFishAssetInputPackageReady", "RuntimeConfigUsesFormalFishAssets")
        }
        FormalPresentationBoundaryFreeze = @{
            AuthoritySource = ".harness/formal-non-fish-content-input-package.json"
            Requirements = @("FormalPresentationBoundaryClosed")
        }
        FormalProgressGrowthContentFreeze = @{
            AuthoritySource = ".harness/formal-non-fish-content-input-package.json"
            Requirements = @("FormalProgressGrowthContentClosed")
        }
    }
    foreach ($RequiredId in $ExpectedContracts.Keys) {
        $Matches = @($Contracts | Where-Object { $_.input_id -eq $RequiredId })
        if ($Matches.Count -ne 1) {
            throw ("DeliveryReleaseHandoffPackage remaining input contract count mismatch: {0} matches={1}" -f $RequiredId, $Matches.Count)
        }
    }

    $ClosureRequirements = @($Package.closure_requirements)
    foreach ($Contract in $Contracts) {
        if ([string]::IsNullOrWhiteSpace($Contract.input_id) -or [string]::IsNullOrWhiteSpace($Contract.current_status) -or [string]::IsNullOrWhiteSpace($Contract.authority_source)) {
            throw "DeliveryReleaseHandoffPackage has an incomplete remaining input contract"
        }
        if (@("blocked", "satisfied") -notcontains $Contract.current_status) {
            throw ("DeliveryReleaseHandoffPackage remaining input contract unsupported status: {0} status={1}" -f $Contract.input_id, $Contract.current_status)
        }
        if (-not $ExpectedContracts.ContainsKey($Contract.input_id)) {
            throw ("DeliveryReleaseHandoffPackage has unknown remaining input contract: {0}" -f $Contract.input_id)
        }
        $ExpectedContract = $ExpectedContracts[$Contract.input_id]
        if ($Contract.authority_source -ne $ExpectedContract.AuthoritySource) {
            throw ("DeliveryReleaseHandoffPackage remaining input authority mismatch: {0} source={1} expected={2}" -f $Contract.input_id, $Contract.authority_source, $ExpectedContract.AuthoritySource)
        }
        Assert-FileExists $Contract.authority_source ("authority source for remaining input {0}" -f $Contract.input_id)

        $UnblockedRequirements = @($Contract.unblocks_requirements)
        $ExpectedRequirementIds = @($ExpectedContract.Requirements)
        if ($UnblockedRequirements.Count -ne $ExpectedRequirementIds.Count) {
            throw ("DeliveryReleaseHandoffPackage remaining input requirement count mismatch: {0} count={1} expected={2}" -f $Contract.input_id, $UnblockedRequirements.Count, $ExpectedRequirementIds.Count)
        }
        foreach ($ExpectedRequirementId in $ExpectedRequirementIds) {
            if ($UnblockedRequirements -notcontains $ExpectedRequirementId) {
                throw ("DeliveryReleaseHandoffPackage remaining input missing linked requirement: {0} requirement={1}" -f $Contract.input_id, $ExpectedRequirementId)
            }
        }
        $LinkedRequirements = @()
        foreach ($RequirementId in $UnblockedRequirements) {
            $Requirement = $ClosureRequirements | Where-Object { $_.requirement_id -eq $RequirementId } | Select-Object -First 1
            if (-not $Requirement) {
                throw ("DeliveryReleaseHandoffPackage remaining input contract references unknown closure requirement: {0} requirement={1}" -f $Contract.input_id, $RequirementId)
            }
            $LinkedRequirements += $Requirement
        }
        $BlockedLinkedRequirements = @($LinkedRequirements | Where-Object { $_.current_status -ne "satisfied" })
        $ExpectedStatus = if ($BlockedLinkedRequirements.Count -eq 0) { "satisfied" } else { "blocked" }
        if ($Contract.current_status -ne $ExpectedStatus) {
            throw ("DeliveryReleaseHandoffPackage remaining input contract status drift: {0} status={1} expected={2}" -f $Contract.input_id, $Contract.current_status, $ExpectedStatus)
        }

        if (@($Contract.required_facts).Count -eq 0 -or @($Contract.do_not_do).Count -eq 0) {
            throw ("DeliveryReleaseHandoffPackage remaining input contract must record required_facts and do_not_do: {0}" -f $Contract.input_id)
        }
        foreach ($RequiredFact in @($Contract.required_facts)) {
            if ([string]::IsNullOrWhiteSpace($RequiredFact)) {
                throw ("DeliveryReleaseHandoffPackage remaining input contract has empty required fact: {0}" -f $Contract.input_id)
            }
        }
        foreach ($ForbiddenAction in @($Contract.do_not_do)) {
            if ([string]::IsNullOrWhiteSpace($ForbiddenAction)) {
                throw ("DeliveryReleaseHandoffPackage remaining input contract has empty do_not_do entry: {0}" -f $Contract.input_id)
            }
        }
    }
}

function Assert-ReleaseHandoffLatestRecordedEvidence {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Package
    )
    <# 记录证据校验把“当前包引用哪份证据”和“本次复跑又生成了哪份 transcript”分开；Static 记录只保留结构化出口，AssetReadiness 仍必须投影对应 evidence_contract，避免自引用 PASS 日志把 Static 启动锁死。 #>
    if (-not $Package.PSObject.Properties.Name.Contains("latest_recorded_evidence")) {
        throw "DeliveryReleaseHandoffPackage missing latest_recorded_evidence"
    }
    $RecordedEvidence = $Package.latest_recorded_evidence
    $EvidenceContracts = @($Package.evidence_contracts)
    $ExpectedEvidenceMirrors = @{
        asset_readiness_log = "DeliveryHarnessAssets.AssetReadinessProtectedBlock"
    }
    foreach ($EntryName in @("static_log", "asset_readiness_log")) {
        $EntryProperty = $RecordedEvidence.PSObject.Properties | Where-Object { $_.Name -eq $EntryName } | Select-Object -First 1
        if (-not $EntryProperty) {
            throw ("DeliveryReleaseHandoffPackage latest recorded evidence missing entry: {0}" -f $EntryName)
        }
        $Entry = $EntryProperty.Value
        if ([string]::IsNullOrWhiteSpace($Entry.path) -or [string]::IsNullOrWhiteSpace($Entry.expected_text) -or [string]::IsNullOrWhiteSpace($Entry.scope)) {
            throw ("DeliveryReleaseHandoffPackage latest recorded evidence incomplete: {0}" -f $EntryName)
        }
        if (-not $ExpectedEvidenceMirrors.ContainsKey($EntryName)) {
            continue
        }

        $ContractId = $ExpectedEvidenceMirrors[$EntryName]
        $Contract = $EvidenceContracts | Where-Object { $_.contract_id -eq $ContractId } | Select-Object -First 1
        if (-not $Contract) {
            throw ("DeliveryReleaseHandoffPackage latest recorded evidence cannot find mirrored contract: {0} contract={1}" -f $EntryName, $ContractId)
        }
        if ($Contract.check_kind -ne "file_contains") {
            throw ("DeliveryReleaseHandoffPackage latest recorded evidence can only mirror file_contains contracts: {0} kind={1}" -f $EntryName, $Contract.check_kind)
        }
        if ($Entry.path -ne $Contract.path -or $Entry.expected_text -ne $Contract.expected_text) {
            throw ("DeliveryReleaseHandoffPackage latest recorded evidence drift: {0} path={1} contractPath={2}" -f $EntryName, $Entry.path, $Contract.path)
        }
        Assert-TextContains $Entry.path $Entry.expected_text ("latest recorded evidence {0}" -f $EntryName)
    }
    if ([string]::IsNullOrWhiteSpace($RecordedEvidence.update_policy)) {
        throw "DeliveryReleaseHandoffPackage latest recorded evidence missing update_policy"
    }
}

function Assert-ReleaseHandoffPackage {
    <# 交付 handoff 包是 DeliveryHarnessAssets 的单一交接口；它汇总模块状态，但不允许任何模块行被当成独立关闭事项。 #>
    $PackagePath = Join-Path $ProjectRoot ".harness\delivery-release-handoff-package.json"
    Assert-FileExists ".harness\delivery-release-handoff-package.json" "delivery release handoff package"
    $Package = Get-Content -LiteralPath $PackagePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($Package.package_id -ne "DeliveryReleaseHandoffPackage") {
        throw ("DeliveryReleaseHandoffPackage id mismatch: {0}" -f $Package.package_id)
    }
    if ($Package.delivery_module -ne "DeliveryHarnessAssets") {
        throw ("DeliveryReleaseHandoffPackage delivery module mismatch: {0}" -f $Package.delivery_module)
    }
    if ($Package.delivery_mode -ne "single_atomic_module") {
        throw ("DeliveryReleaseHandoffPackage delivery_mode mismatch: {0}" -f $Package.delivery_mode)
    }
    if ($Package.package_status -ne "ready") {
        throw ("DeliveryReleaseHandoffPackage package_status mismatch: {0}" -f $Package.package_status)
    }
    if ($Package.delivery_module_status -ne "blocked") {
        throw ("DeliveryReleaseHandoffPackage must keep DeliveryHarnessAssets blocked until asset readiness passes: {0}" -f $Package.delivery_module_status)
    }
    if ([string]::IsNullOrWhiteSpace($Package.release_handoff_rule) -or [string]::IsNullOrWhiteSpace($Package.close_rule)) {
        throw "DeliveryReleaseHandoffPackage has empty release_handoff_rule or close_rule"
    }

    $ExpectedStatuses = @{
        FishingPlayerEntry = "complete"
        UIReach = "complete"
        ItemsTankSacrificeCamp = "complete"
        EquipmentShop = "needs_verification"
        FrontendOnline = "complete"
        DataWorldProfileAlbum = "engineering_complete"
        CharacterGrowthCondition = "engineering_complete"
        RunEnvironmentSocial = "runtime_evidence_passed"
        DeliveryHarnessAssets = "blocked"
    }
    foreach ($ModuleKey in $ExpectedStatuses.Keys) {
        $ModuleProperty = $Package.module_statuses.PSObject.Properties | Where-Object { $_.Name -eq $ModuleKey } | Select-Object -First 1
        if (-not $ModuleProperty) {
            throw ("DeliveryReleaseHandoffPackage missing module status: {0}" -f $ModuleKey)
        }
        $ModuleEntry = $ModuleProperty.Value
        if ($ModuleEntry.closure_status -ne $ExpectedStatuses[$ModuleKey]) {
            throw ("DeliveryReleaseHandoffPackage module status mismatch for {0}: {1}" -f $ModuleKey, $ModuleEntry.closure_status)
        }
        if ([string]::IsNullOrWhiteSpace($ModuleEntry.status_basis)) {
            throw ("DeliveryReleaseHandoffPackage module status_basis is empty: {0}" -f $ModuleKey)
        }
        foreach ($EvidenceRoot in @($ModuleEntry.evidence_roots)) {
            if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
                throw ("DeliveryReleaseHandoffPackage has empty evidence root for {0}" -f $ModuleKey)
            }
            $EvidencePath = Join-Path $ProjectRoot $EvidenceRoot
            if (-not (Test-Path -LiteralPath $EvidencePath)) {
                throw ("DeliveryReleaseHandoffPackage evidence root missing for {0}: {1}" -f $ModuleKey, $EvidenceRoot)
            }
        }
    }

    $FormalPackagePath = Join-Path $ProjectRoot ".harness\formal-fish-asset-input-package.json"
    $FormalPackage = Get-Content -LiteralPath $FormalPackagePath -Raw -Encoding UTF8 | ConvertFrom-Json
    $NonFishPackagePath = Join-Path $ProjectRoot ".harness\formal-non-fish-content-input-package.json"
    $NonFishPackage = Get-Content -LiteralPath $NonFishPackagePath -Raw -Encoding UTF8 | ConvertFrom-Json
    $RunEnvironmentSocialBehaviorEvidencePath = Join-Path $ProjectRoot ".codex\state\run-environment-social-behavior-evidence.json"
    $RunEnvironmentSocialBehaviorEvidence = Get-Content -LiteralPath $RunEnvironmentSocialBehaviorEvidencePath -Raw -Encoding UTF8 | ConvertFrom-Json

    <# completion_blockers 是 release 状态的镜像，不是手写待办清单：每个 blocker 必须由对应权威输入仍未 ready/pass 推导出来，输入关闭后旧 blocker 必须同步移除。 #>
    $RuntimeConfigHasTestAssets = $false
    $DefaultGamePath = Join-Path $ProjectRoot "Config\DefaultGame.ini"
    Assert-FileExists "Config\DefaultGame.ini" "DefaultGame config for completion blocker mirror"
    $DefaultGame = Get-Content -LiteralPath $DefaultGamePath -Raw -Encoding UTF8
    foreach ($AssetName in @("DA_Fish_Test01", "DA_Bite_Test01", "DA_Fight_Test01")) {
        if ($DefaultGame.Contains($AssetName)) {
            $RuntimeConfigHasTestAssets = $true
            break
        }
    }

    $ExpectedBlockerSources = @{}
    $EquipmentShopEntry = ($Package.module_statuses.PSObject.Properties | Where-Object { $_.Name -eq "EquipmentShop" } | Select-Object -First 1).Value
    if ($EquipmentShopEntry.closure_status -ne "complete") {
        $ExpectedBlockerSources["EquipmentShopNeedsFreshEvidence"] = ".harness/delivery-release-handoff-package.json"
    }
    if (-not ($FormalPackage.status -eq "ready" -and @($FormalPackage.blocking_gaps).Count -eq 0)) {
        $ExpectedBlockerSources["FormalFishAssetInputPackageBlocked"] = ".harness/formal-fish-asset-input-package.json"
    }
    if ($RuntimeConfigHasTestAssets) {
        $ExpectedBlockerSources["TrackedTestAssetsStillConfigured"] = "Config/DefaultGame.ini"
    }
    $PresentationGroup = $NonFishPackage.content_groups.presentation_boundary
    if (-not ($NonFishPackage.status -eq "ready" -and @($NonFishPackage.package_blocking_gaps).Count -eq 0 -and $PresentationGroup.current_status -eq "satisfied" -and @($PresentationGroup.blocking_gaps).Count -eq 0)) {
        $ExpectedBlockerSources["FormalPresentationBoundaryOpen"] = ".harness/formal-non-fish-content-input-package.json"
    }
    $ProgressGrowthGroup = $NonFishPackage.content_groups.progress_growth_content
    if (-not ($NonFishPackage.status -eq "ready" -and @($NonFishPackage.package_blocking_gaps).Count -eq 0 -and $ProgressGrowthGroup.current_status -eq "satisfied" -and @($ProgressGrowthGroup.blocking_gaps).Count -eq 0)) {
        $ExpectedBlockerSources["FormalProgressGrowthContentOpen"] = ".harness/formal-non-fish-content-input-package.json"
    }
    $RunEnvironmentSocialCoverage = @($RunEnvironmentSocialBehaviorEvidence.coverage_requirements)
    $RunEnvironmentSocialMissingCoverage = @($RunEnvironmentSocialCoverage | Where-Object { $_.status -ne "pass" })
    $RunEnvironmentSocialRecord = $RunEnvironmentSocialBehaviorEvidence.evidence_record
    $RunEnvironmentSocialBehaviorReady = (
        $RunEnvironmentSocialBehaviorEvidence.status -eq "pass" -and
        $RunEnvironmentSocialCoverage.Count -eq 5 -and
        $RunEnvironmentSocialMissingCoverage.Count -eq 0 -and
        @($RunEnvironmentSocialBehaviorEvidence.accepted_source_kinds) -contains $RunEnvironmentSocialRecord.source_kind -and
        $RunEnvironmentSocialRecord.participants_count -ge 2 -and
        -not [string]::IsNullOrWhiteSpace($RunEnvironmentSocialRecord.recorded_on) -and
        -not [string]::IsNullOrWhiteSpace($RunEnvironmentSocialRecord.summary) -and
        @($RunEnvironmentSocialRecord.evidence_refs).Count -gt 0
    )
    if (-not $RunEnvironmentSocialBehaviorReady) {
        $ExpectedBlockerSources["RunEnvironmentSocialModuleOpen"] = ".codex/state/run-environment-social-behavior-evidence.json"
    }

    $SeenBlockers = @{}
    foreach ($Blocker in @($Package.completion_blockers)) {
        if ([string]::IsNullOrWhiteSpace($Blocker.blocker_id) -or [string]::IsNullOrWhiteSpace($Blocker.source) -or [string]::IsNullOrWhiteSpace($Blocker.summary)) {
            throw "DeliveryReleaseHandoffPackage has an incomplete completion blocker"
        }
        if ($SeenBlockers.ContainsKey($Blocker.blocker_id)) {
            throw ("DeliveryReleaseHandoffPackage duplicate completion blocker: {0}" -f $Blocker.blocker_id)
        }
        $SeenBlockers[$Blocker.blocker_id] = $true
        if (-not $ExpectedBlockerSources.ContainsKey($Blocker.blocker_id)) {
            throw ("DeliveryReleaseHandoffPackage has stale or unknown completion blocker: {0}" -f $Blocker.blocker_id)
        }
        if ($Blocker.source -ne $ExpectedBlockerSources[$Blocker.blocker_id]) {
            throw ("DeliveryReleaseHandoffPackage completion blocker source mismatch: {0} source={1} expected={2}" -f $Blocker.blocker_id, $Blocker.source, $ExpectedBlockerSources[$Blocker.blocker_id])
        }
    }
    foreach ($ExpectedBlockerId in $ExpectedBlockerSources.Keys) {
        if (-not $SeenBlockers.ContainsKey($ExpectedBlockerId)) {
            throw ("DeliveryReleaseHandoffPackage missing completion blocker: {0}" -f $ExpectedBlockerId)
        }
    }

    $DeliveryEntry = ($Package.module_statuses.PSObject.Properties | Where-Object { $_.Name -eq "DeliveryHarnessAssets" } | Select-Object -First 1).Value
    if ($FormalPackage.status -eq "blocked" -and $DeliveryEntry.closure_status -eq "complete") {
        throw "DeliveryReleaseHandoffPackage cannot mark DeliveryHarnessAssets complete while FormalFishAssetInputPackage is blocked"
    }
    if ($NonFishPackage.status -eq "blocked" -and $DeliveryEntry.closure_status -eq "complete") {
        throw "DeliveryReleaseHandoffPackage cannot mark DeliveryHarnessAssets complete while FormalNonFishContentInputPackage is blocked"
    }
    if ($RunEnvironmentSocialBehaviorEvidence.status -eq "blocked" -and $DeliveryEntry.closure_status -eq "complete") {
        throw "DeliveryReleaseHandoffPackage cannot mark DeliveryHarnessAssets complete while RunEnvironmentSocial BehaviorEvidence is blocked"
    }
    Assert-ReleaseHandoffEvidenceContracts $Package
    Assert-ReleaseHandoffClosureRequirements $Package $FormalPackage $NonFishPackage $RunEnvironmentSocialBehaviorEvidence
    Assert-ReleaseHandoffRemainingInputContracts $Package
    Assert-ReleaseHandoffLatestRecordedEvidence $Package
}

function Invoke-StaticInventory {
    <# 静态交付盘点流程：把模块合同、验证入口、文档缺口和当前正式资产状态集中到 DeliveryHarnessAssets 一个模块下；它不替代正式资产 readiness。 #>
    $HarnessPath = Join-Path $ProjectRoot ".harness\harness.json"
    Assert-FileExists ".harness\harness.json" "project harness"
    $Harness = Get-Content -LiteralPath $HarnessPath -Raw -Encoding UTF8 | ConvertFrom-Json

    if (-not $Harness.module_delivery.PSObject.Properties.Name.Contains("DeliveryHarnessAssets")) {
        throw "DeliveryHarnessAssets module is missing from harness"
    }
    $Module = $Harness.module_delivery.DeliveryHarnessAssets
    Assert-DeliveryHarnessAssetsMachineStatus $Module.machine_status_source
    if ($Module.delivery_mode -ne "single_atomic_module") {
        throw ("DeliveryHarnessAssets delivery_mode mismatch: {0}" -f $Module.delivery_mode)
    }
    foreach ($Facet in @("HarnessInventory", "RegressionEvidence", "PlayerAcceptance", "AssetReadiness", "ReleaseHandoff")) {
        if (@($Module.facets) -notcontains $Facet) {
            throw ("DeliveryHarnessAssets facet missing from atomic module contract: {0}" -f $Facet)
        }
    }
    $HasEmptyAtomicRule =
        ([string]::IsNullOrWhiteSpace($Module.tracking_rule)) -or
        ([string]::IsNullOrWhiteSpace($Module.completion_rule)) -or
        ([string]::IsNullOrWhiteSpace($Module.handoff_rule))
    if ($HasEmptyAtomicRule) {
        throw "DeliveryHarnessAssets atomic contract has empty tracking/completion/handoff rule"
    }
    if ($Module.asset_input_package_source -ne ".harness/formal-fish-asset-input-package.json") {
        throw ("DeliveryHarnessAssets asset input package source mismatch: {0}" -f $Module.asset_input_package_source)
    }
    if ($Module.content_input_package_source -ne ".harness/formal-non-fish-content-input-package.json") {
        throw ("DeliveryHarnessAssets content input package source mismatch: {0}" -f $Module.content_input_package_source)
    }
    if ($Module.release_handoff_package_source -ne ".harness/delivery-release-handoff-package.json") {
        throw ("DeliveryHarnessAssets release handoff package source mismatch: {0}" -f $Module.release_handoff_package_source)
    }

    foreach ($ModuleKey in @(
        "FishingPlayerEntry",
        "UIReach",
        "ItemsTankSacrificeCamp",
        "EquipmentShop",
        "FrontendOnline",
        "DataWorldProfileAlbum",
        "CharacterGrowthCondition",
        "RunEnvironmentSocial",
        "DeliveryHarnessAssets"
    )) {
        if (-not $Harness.module_delivery.PSObject.Properties.Name.Contains($ModuleKey)) {
            throw ("DeliveryHarnessAssets module inventory missing module_delivery entry: {0}" -f $ModuleKey)
        }
    }

    $RequiredChecks = @(
        @{ Key = "fishing_player_entry_static_check"; Script = "Scripts\verify_fishing_player_entry.ps1" },
        @{ Key = "fishing_player_entry_build"; Script = "Scripts\verify_fishing_player_entry.ps1" },
        @{ Key = "fishing_player_entry_automation"; Script = "Scripts\verify_fishing_player_entry.ps1" },
        @{ Key = "ui_reach_static_check"; Script = "Scripts\verify_ui_reach.ps1" },
        @{ Key = "ui_reach_build"; Script = "Scripts\verify_ui_reach.ps1" },
        @{ Key = "ui_reach_automation"; Script = "Scripts\verify_ui_reach.ps1" },
        @{ Key = "ui_reach_runtime"; Script = "Scripts\verify_ui_reach.ps1" },
        @{ Key = "items_tank_sacrifice_camp_static_check"; Script = "Scripts\verify_items_tank_sacrifice_camp.ps1" },
        @{ Key = "items_tank_sacrifice_camp_build"; Script = "Scripts\verify_items_tank_sacrifice_camp.ps1" },
        @{ Key = "items_tank_sacrifice_camp_automation"; Script = "Scripts\verify_items_tank_sacrifice_camp.ps1" },
        @{ Key = "items_tank_sacrifice_camp_runtime"; Script = "Scripts\verify_items_tank_sacrifice_camp.ps1" },
        @{ Key = "equipment_shop_static_check"; Script = "Scripts\verify_equipment_shop.ps1" },
        @{ Key = "equipment_shop_build"; Script = "Scripts\verify_equipment_shop.ps1" },
        @{ Key = "equipment_shop_automation"; Script = "Scripts\verify_equipment_shop.ps1" },
        @{ Key = "equipment_shop_runtime"; Script = "Scripts\verify_equipment_shop.ps1" },
        @{ Key = "frontend_online_build"; Script = "" },
        @{ Key = "frontend_online_ui_automation"; Script = "" },
        @{ Key = "frontend_online_online_automation"; Script = "" },
        @{ Key = "data_world_profile_album_static_check"; Script = "Scripts\verify_data_world_profile_album.ps1" },
        @{ Key = "data_world_profile_album_build"; Script = "Scripts\verify_data_world_profile_album.ps1" },
        @{ Key = "data_world_profile_album_automation"; Script = "Scripts\verify_data_world_profile_album.ps1" },
        @{ Key = "character_growth_condition_static_check"; Script = "Scripts\verify_character_growth_condition.ps1" },
        @{ Key = "character_growth_condition_build"; Script = "Scripts\verify_character_growth_condition.ps1" },
        @{ Key = "character_growth_condition_automation"; Script = "Scripts\verify_character_growth_condition.ps1" },
        @{ Key = "character_growth_condition_runtime"; Script = "Scripts\verify_character_growth_condition.ps1" },
        @{ Key = "run_environment_social_static_check"; Script = "Scripts\verify_run_environment_social.ps1" },
        @{ Key = "run_environment_social_build"; Script = "Scripts\verify_run_environment_social.ps1" },
        @{ Key = "run_environment_social_automation"; Script = "Scripts\verify_run_environment_social.ps1" },
        @{ Key = "run_environment_social_runtime"; Script = "Scripts\verify_run_environment_social.ps1" },
        @{ Key = "run_environment_social_behavior_evidence"; Script = "Scripts\verify_run_environment_social.ps1" },
        @{ Key = "run_environment_social_record_behavior_evidence"; Script = "Scripts\record_run_environment_social_behavior_evidence.ps1" },
        @{ Key = "delivery_harness_assets_static_check"; Script = "Scripts\verify_delivery_harness_assets.ps1" },
        @{ Key = "delivery_harness_assets_asset_readiness"; Script = "Scripts\verify_delivery_harness_assets.ps1" }
    )
    foreach ($Check in $RequiredChecks) {
        Assert-HarnessVerification $Harness $Check.Key $Check.Script
    }

    Assert-DirectoryTextContains "Docs\Development" "Delivery / Harness / Assets" "human delivery module status"
    Assert-DirectoryTextContains "Docs\Development" "RUN_ENVIRONMENT_SOCIAL_NEXT_MODULE" "human next run environment social module marker"
    Assert-DirectoryTextContains "Docs\Development" "AssetReadiness" "formal asset blocker status"
    Assert-DirectoryTextContains "Docs\Development" "FeishuFishTableSource" "formal fish data source blocker status"
    Assert-DirectoryTextContains "Docs\Development" "FormalFishAssetInputPackage" "formal fish asset input package status"
    Assert-DirectoryTextContains "Docs\Development" "FormalNonFishContentInputPackage" "formal non-fish content input package status"
    Assert-DirectoryTextContains "Docs\Development" "DeliveryReleaseHandoffPackage" "delivery release handoff package status"
    Assert-DirectoryTextContains "Docs\Development" "FormalFishAssetFreeze" "human formal fish asset freeze input contract"
    Assert-DirectoryTextContains "Docs\Development" "FormalPresentationBoundaryFreeze" "human formal presentation boundary input contract"
    Assert-DirectoryTextContains "Docs\Development" "FormalProgressGrowthContentFreeze" "human formal progress and growth content input contract"
    Assert-DirectoryTextContains "Docs\Architecture" "DeliveryHarnessAssets" "technical delivery module contract"
    Assert-DirectoryTextContains "Docs\Architecture" "RUN_ENVIRONMENT_SOCIAL_NEXT_MODULE" "technical next run environment social module marker"
    Assert-DirectoryTextContains "Docs\Architecture" "FeishuFishTableSource" "technical fish data source blocker"
    Assert-DirectoryTextContains "Docs\Architecture" "FormalFishAssetInputPackage" "technical fish asset input package blocker"
    Assert-DirectoryTextContains "Docs\Architecture" "FormalNonFishContentInputPackage" "technical non-fish content input package blocker"
    Assert-DirectoryTextContains "Docs\Architecture" "DeliveryReleaseHandoffPackage" "technical delivery release handoff package"
    Assert-DirectoryTextContains "Docs\Architecture" "FormalFishAssetFreeze" "technical formal fish asset freeze input contract"
    Assert-DirectoryTextContains "Docs\Architecture" "FormalPresentationBoundaryFreeze" "technical formal presentation boundary input contract"
    Assert-DirectoryTextContains "Docs\Architecture" "FormalProgressGrowthContentFreeze" "technical formal progress and growth content input contract"
    Assert-DirectoryTextContains "Docs\Architecture" "R_MATRIX_IS_NOT_IMPLEMENTATION_STATUS" "technical matrix implementation-status boundary"
    Assert-TextContains "Config\DefaultGame.ini" "Fish_RiverPattern" "formal fish asset reference"
    Assert-TextContains "Config\DefaultGame.ini" "Fish_Puffer" "formal toxic fish asset reference"
    Assert-TextContains "Config\DefaultGame.ini" "Bite_Cautious" "formal bite asset reference"
    Assert-TextContains "Config\DefaultGame.ini" "Fight_GiantHeavy" "formal fight asset reference"
    Assert-TextNotContains "Config\DefaultGame.ini" "DA_Fish_Test01" "current fish test asset reference"
    Assert-TextNotContains "Config\DefaultGame.ini" "DA_Bite_Test01" "current bite test asset reference"
    Assert-TextNotContains "Config\DefaultGame.ini" "DA_Fight_Test01" "current fight test asset reference"
    Assert-FormalFishAssetInputPackage
    Assert-FormalNonFishContentInputPackage
    Assert-ReleaseHandoffPackage

    Write-Host "DELIVERY_HARNESS_ASSETS_STATIC_PASS DeliveryReleaseHandoffPackage=Ready FormalFishAssetInputPackage=Ready RuntimeConfigUsesFormalFishAssets=True FormalNonFishContentInputPackage=Blocked FormalAssetReadiness=BlockedByFormalNonFishContentAndBehaviorEvidence"
}

function Invoke-AssetReadiness {
    <# 正式资产 readiness 严格核验流程：先同时要求鱼资产输入包与非鱼正式内容输入包 ready 且无阻塞字段，再要求运行配置不再引用测试鱼表/咬钩/搏斗资产，防止只补其中一半就误关交付模块。 #>
    Assert-FormalFishAssetInputPackage
    Assert-FormalNonFishContentInputPackage

    $PackagePath = Join-Path $ProjectRoot ".harness\formal-fish-asset-input-package.json"
    Assert-FileExists ".harness\formal-fish-asset-input-package.json" "formal fish asset input package"
    $Package = Get-Content -LiteralPath $PackagePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($Package.package_id -ne "FormalFishAssetInputPackage" -or $Package.delivery_module -ne "DeliveryHarnessAssets") {
        throw ("DELIVERY_HARNESS_ASSETS_ASSET_READINESS_BLOCKED FormalFishAssetInputPackageIdentityMismatch package_id={0} delivery_module={1}" -f $Package.package_id, $Package.delivery_module)
    }

    $NonFishPackagePath = Join-Path $ProjectRoot ".harness\formal-non-fish-content-input-package.json"
    Assert-FileExists ".harness\formal-non-fish-content-input-package.json" "formal non-fish content input package"
    $NonFishPackage = Get-Content -LiteralPath $NonFishPackagePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($NonFishPackage.package_id -ne "FormalNonFishContentInputPackage" -or $NonFishPackage.delivery_module -ne "DeliveryHarnessAssets") {
        throw ("DELIVERY_HARNESS_ASSETS_ASSET_READINESS_BLOCKED FormalNonFishContentInputPackageIdentityMismatch package_id={0} delivery_module={1}" -f $NonFishPackage.package_id, $NonFishPackage.delivery_module)
    }

    $FishBlockingGaps = @($Package.blocking_gaps)
    $NonFishBlockingGaps = @($NonFishPackage.package_blocking_gaps)
    $NonFishGroupStatuses = @()
    foreach ($GroupName in @("presentation_boundary", "progress_growth_content")) {
        $GroupProperty = $NonFishPackage.content_groups.PSObject.Properties | Where-Object { $_.Name -eq $GroupName } | Select-Object -First 1
        if (-not $GroupProperty) {
            throw ("DELIVERY_HARNESS_ASSETS_ASSET_READINESS_BLOCKED FormalNonFishContentInputPackageMissingGroup={0}" -f $GroupName)
        }
        $Group = $GroupProperty.Value
        $NonFishGroupStatuses += ("{0}:{1}" -f $GroupName, $Group.current_status)
        if ($Group.current_status -ne "satisfied") {
            $NonFishBlockingGaps += ("{0}NotSatisfied" -f $GroupName)
        }
        foreach ($Gap in @($Group.blocking_gaps)) {
            $NonFishBlockingGaps += $Gap
        }
    }
    $NonFishBlockingGaps = @($NonFishBlockingGaps | Select-Object -Unique)
    $PackageReadinessBlocked = ($Package.status -ne "ready") -or ($FishBlockingGaps.Count -gt 0) -or ($NonFishPackage.status -ne "ready") -or ($NonFishBlockingGaps.Count -gt 0)
    if ($PackageReadinessBlocked) {
        $BlockingGaps = @()
        foreach ($Gap in $FishBlockingGaps) {
            $BlockingGaps += $Gap
        }
        foreach ($Gap in $NonFishBlockingGaps) {
            $BlockingGaps += $Gap
        }
        throw ("DELIVERY_HARNESS_ASSETS_ASSET_READINESS_BLOCKED FormalFishAssetInputPackageStatus={0} FormalNonFishContentInputPackageStatus={1} FormalNonFishContentGroups={2} BlockingGaps={3}" -f $Package.status, $NonFishPackage.status, ($NonFishGroupStatuses -join ";"), ($BlockingGaps -join ","))
    }

    $DefaultGamePath = Join-Path $ProjectRoot "Config\DefaultGame.ini"
    Assert-FileExists "Config\DefaultGame.ini" "DefaultGame config"
    $DefaultGame = Get-Content -LiteralPath $DefaultGamePath -Raw -Encoding UTF8
    $BlockedAssets = @()
    foreach ($AssetName in @("DA_Fish_Test01", "DA_Bite_Test01", "DA_Fight_Test01")) {
        if ($DefaultGame.Contains($AssetName)) {
            $BlockedAssets += $AssetName
        }
    }
    if ($BlockedAssets.Count -gt 0) {
        throw ("DELIVERY_HARNESS_ASSETS_ASSET_READINESS_BLOCKED TestAssetsStillConfigured={0}" -f ($BlockedAssets -join ","))
    }
    Write-Host "DELIVERY_HARNESS_ASSETS_ASSET_READINESS_PASS"
}

try {
    switch ($Mode) {
        "Static" { Invoke-StaticInventory }
        "AssetReadiness" { Invoke-AssetReadiness }
        default { throw ("Unknown verification mode: {0}" -f $Mode) }
    }
}
finally {
    Stop-Transcript | Out-Null
    Write-Output ("Evidence log: {0}" -f $TranscriptLogFile)
}
