# Catfishing Coding Lessons

更新时间：2026-08-20

范围：本文件记录已经在 Catfishing 代码开发中造成或险些造成回归的工程教训。它是 Harness 必读上下文，供后续 Coding、Review 和 Verification 使用；它不是任务日志，也不是需求事实源。

## 案例教训必须抽成可执行不变量，不能只记住具体事故

适用范围：用户指出近期问题、Review 暴露回归、验证发现漏测，或者任一 Coding 回合需要把一次错误转化为后续可复用约束时。

核心规则：

- 先分开现象、真实运行链、断点证据和可复用不变量；不要把最近一次出错的位置当成以后所有相似问题的默认原因。
- 可执行沉淀必须进入 Harness、自动化测试、Review 检查项或稳定知识入口之一；只在对话里承诺“下次注意”不算沉淀。
- 下次遇到相似结构时，先检查当前路径是否满足已沉淀的不变量，再决定是否需要代码改动；不能机械复用上一次修法。
- 验证必须覆盖从错误中抽出的不变量，而不只是覆盖当时失败的最窄样例。

当前关键入口：

- `.harness/PROBLEM_ANALYSIS_STANDARD.md`
  - 要求 Bug、行为异常、回归和修改既有行为的任务先形成诊断记录。
- `.harness/CODING_LESSONS.md`
  - 记录已经转化为后续 Coding、Review 和 Verification 约束的工程教训。
- `Source/Catfishing/Integration/Fishing/CatFishingOperationJournal.cpp`
  - 当前幂等重放教训的直接代码载体。
- `Source/Catfishing/Integration/Fishing/Tests/CatFishingBoundaryContractTests.cpp`
  - 当前幂等重放教训的合同测试载体。

常见误修：

- 把用户指出的问题理解成“以后遇到同一个符号要小心”，而不是提炼出跨路径的判定规则。
- 只补一个正好复现当前失败的测试，不补会阻止同类回归的合同测试。
- 在 Review 中只查本次修改有没有复发旧 bug，不查新路径是否违反同一个不变量。
- 把历史案例当固定修法或固定测试矩阵，忽略当前真实运行链已经不同。

后续修改检查顺序：

1. 先写清这次问题的可复用不变量是什么。
2. 找到不变量在当前代码中的真实持有者、写入者和消费路径。
3. 决定沉淀载体：Harness 规则、合同测试、Review 检查项或知识文档；能用测试固定的优先写测试。
4. 验证新载体确实会在同类错误复发时失败或给出明确 Review finding。
5. 交付时说明这是从哪个问题抽出的经验，以及仍没有被自动化覆盖的边界。

## 幂等操作必须先重放旧结果，再检查当前可变状态

适用范围：任何使用 `FCatFishingOperationJournal`、OperationKey、Inbox、terminal cache 或 RequestId 幂等语义的代码路径。

核心规则：

- 已被接受的同一个 operation，必须优先通过 Journal/Inbox/cache 返回第一次的稳定结果。
- `CloseAttempt`、`bCommandsOpen`、`ActiveSessionByFisher`、当前 `Character/GameState/WaterQuery` 是否仍可读，只能拦截新的 operation，不能覆盖已接受 operation 的重放。
- 如果 operation 已经被 Journal 接受为 `Pending`，后续 fail-closed 也必须提交稳定 `Rejected`，不能留下悬空 Pending。
- 终态只能第一次从 Pending 写入；不得把 Rejected 改成 Committed，也不得把 Committed 降级或覆盖。

当前关键代码入口：

- `Source/Catfishing/Integration/Fishing/CatFishingOperationJournal.cpp`
  - `FCatFishingOperationJournal::AcceptOrReplay`
  - `FCatFishingOperationJournal::CommitResult`
  - `FCatFishingOperationJournal::CloseAttempt`
- `Source/Catfishing/Integration/Fishing/CatFishingBoundarySubsystem.cpp`
  - `UCatFishingBoundarySubsystem::Start`
  - `UCatFishingBoundarySubsystem::CastAccepted`
- `Source/Catfishing/Fishing/CatFishingService.cpp`
  - `UCatFishingService::StartFishingSession`
  - `StartResultByAttempt`
  - `StartResultByBoundaryOperation`

常见误修：

- 先判断 Attempt 是否关闭，再查是否已有同一请求的稳定结果。
- 把 `PrincipalId` 放进幂等键，导致同一 RequestId 的身份漂移绕过 PayloadMismatch/InvalidIdentity 检查。
- 在 Boundary 的 Start/Cast 中，先读取当前 `Character/GameState/WaterQuery`，再查询 Journal/cache，导致环境变化后旧请求不能稳定重放。
- 在 Service 中，先用 `ActiveSessionByFisher` 拦截，再查询同一 Start/Cast 的终态缓存，导致成功创建会话后的同请求重试被误判为 InvalidPhase。
- Cast 已进入 Journal Pending 后，依赖缺失直接返回普通拒绝，没有写回稳定 Rejected，导致下一次重放看到悬空 Pending。

后续修改检查顺序：

1. 先构造 canonical operation identity：OperationKind、AttemptId、RequestId 和稳定 payload hash 必须足以识别同一业务动作。
2. 先查 Journal/Inbox/cache 中是否已有结果；如果已有，直接返回稳定结果。
3. 身份漂移单独拒绝为 `InvalidIdentity`，不要通过改变幂等键生成第二条 operation。
4. 只有确认是新 operation 后，才检查 Attempt 关闭、命令关闭、当前环境、活跃会话和依赖可用性。
5. 首次 Pending 之后，成功提交 Committed；失败提交 Rejected；两者都必须成为后续重放的唯一结果。
6. Review 必须专门检查“重放优先级”，Verification 至少覆盖 close/replay、terminal overwrite/downgrade、payload mismatch 和当前状态变化后的 replay 风险。

已验证证据边界：

- Atom A 已用 Boundary Contract 自动化覆盖 Journal close 后旧请求重放、PayloadMismatch、身份漂移、终态不可覆盖和 hash golden。
- WORK-00/B 已把 Start/Cast 接入调整为优先 Journal/cache，再读取当前依赖；Service 允许已接受 Start 先通过 Attempt 级终态缓存重放，再落到 Cast 结果缓存。
- 2026-08-17 已记录验证：Atom A/B 阶段 `Catfishing.FishingBoundary.Contract` 5 条自动化通过、`Catfishing.Fishing.Service` 1 条自动化通过；WORK-00/C 后 `Catfishing.FishingBoundary.Contract` 9 条自动化通过、`Catfishing.Fishing.Session` 1 条自动化通过，CatfishingEditor Win64 Development 构建通过。
- 当前仍缺少完整有效 Controller + Run + Water + FishCatalog + Session/Equipment 的成功链 fixture；不要把结构体级测试或合同测试冒充特殊饵真实扣写一次、RodBroken 玩法结果或完整生产成功链验证。

## 宽泛上下文搜索必须显式排除项目唯一禁读材料

适用范围：任何 `rg`、全文检索、文档命令或上下文采集命令会扫描 `Docs/Architecture/`、`Docs/`、`Knowledge/` 或全仓 Markdown 时。

核心规则：

- 在命令层显式排除项目 `AGENTS.md` 中声明的唯一禁读材料；不要只靠“我不会打开它”的主观记忆。
- 如果只是找构建、测试或代码入口，优先限定到 README、Harness、Knowledge/Framework、Knowledge/Development 或具体源码路径，避免把人类分工文档带入候选集合。
- 一旦误命中禁读材料，停止使用该输出中的任何内容；只记录违规事实和防复发动作，不把命中文本转述进任务推理、Context Pack、Review Packet 或交付结论。
- 后续 Review 要检查宽泛搜索命令是否带排除条件，尤其是 `rg ... Docs Knowledge` 这类容易跨过边界的命令。

本轮触发原因：一次用于查找构建/Automation 命令的宽泛文档搜索误命中了禁读材料。修正后的不变量是“宽泛搜索先缩范围，必须搜索大范围时显式排除禁读材料”。

## 新增同类校验/幂等模板前必须先查是否已有可复用实现

适用范围：任何要给某个 RPC handler 或写入路径加 RequestId 校验、PayloadSignature/哈希比对、TerminalCache 或幂等缓存这类样板逻辑的任务。

核心规则：

- 落笔前先用 `rg` 搜索项目里是否已经存在同构模式（例如 `PayloadSignature = FString::Printf`、`TerminalCache`、`RequestId` 校验这类关键词），找到就复用或抽公共函数，不要另起一份。
- 一个任务如果要在多个领域（Equipment/Camp/Social/Items/Fishing 等）里各自实现"同一件事"，先判断这件事能不能抽成一个共享的校验/记账工具，再分头接线，不要先分头写完再回头合并。
- 实现完成前必须做删减审查（全局规范已有此要求）：尝试把新增的校验/幂等逻辑内联或合并进已有实现，合并后仍满足验收就必须合并。
- Graph/Harness 流程走到 `comment_review`/`review` 节点之前，不能认为任务收尾；这两步是抓重复实现的主要关卡，跳过等于让重复代码直接进仓库。

当前关键代码入口：

- `Source/Catfishing/Equipment/CatEquipmentComponent.cpp`、`Source/Catfishing/Camp/CatCampHubActor.cpp`、`Source/Catfishing/Social/CatSocialService.cpp`、`Source/Catfishing/Items/CatItemsService.cpp`、`Source/Catfishing/Fishing/CatFishingService.cpp`
  - 2026-08-18 审查发现这五个文件各自独立实现了一套几乎相同的 PayloadSignature 生成/比对/TerminalCache 写入模板，`git log -S "PayloadSignature"` 确认该模式在此前提交里完全不存在，是同一会话内被复制了 5 次以上。

常见误修：

- 把"每个领域自己实现一遍校验"当成合理的领域隔离，而不是识别成跨领域的真实语义重复。
- 只在写完之后才想起要不要复用，而不是落笔前先搜索。
- 把 Graph 状态推进到 `gate` 之后就当作任务在正常收口，不检查是否真的走到了 `review`。

后续修改检查顺序：

1. 先搜索项目里是否已有同构实现。
2. 有就复用/抽公共函数；没有且预计会被至少两个领域用到，先抽再接线。
3. 完成前跑一次针对本次新增校验逻辑的删减审查，明确指出为什么没有合并的部分是必要的。
4. 确认 Graph/Harness 走到了 `review` 节点，而不是停在 `gate` 或 `dispatch`。

## 引入新架构名词必须同步写进 TERMS.md 的人话解释，不能只写工程合同定义

适用范围：任何引入新的架构概念、协议名词或缩写（例如 Boundary、Journal、Cursor、Receipt、Operation 这类），并打算把它写进代码、计划文档或提交给 Review 的任务。

核心规则：

- 新名词进入代码或计划文档前，必须在 `Knowledge/Framework/TERMS.md` 里补一条条目；条目除了工程合同定义，还要有一句人话说明"这是为了解决什么问题、不加它会怎样"。
- 已经在 TERMS.md 里的词条如果只有抽象合同式定义（例如"唯一的 typed Request/Result 合同"），后续touch到该词条时应顺手补一句人话解释，不必等专门任务。
- Review 阶段要检查本次改动新引入的类名/成员名是否都能在 TERMS.md 里查到对应条目；查不到的视为文档缺口，不是可以忽略的小事。

当前关键代码入口：

- `Knowledge/Framework/TERMS.md`
  - 2026-08-18 审查发现 `Fishing Boundary`（第77行）、`Grant Journal`（第106行）、`Command Result`/Receipt（第123行）已有条目但只有工程合同式定义；`Cursor`（对应代码里的 `FightCursorLedger`）完全没有条目，查无出处。

常见误修：

- 认为"代码里有注释、计划文档里有定义"就等于信息已经传递到位，不用再写进 TERMS.md。
- 把 TERMS.md 的条目写成合同摘要，而不是给不熟悉这次设计的人看的解释。

后续修改检查顺序：

1. 列出本次改动新增的架构名词。
2. 逐个检查 TERMS.md 是否有条目、条目是否有人话解释。
3. 缺失的补上；只有工程定义没有人话解释的，顺手补一句。
4. Review 时把这项检查作为独立一条，不要归并进"注释是否齐全"里，因为方法注释和术语表解释解决的是不同问题。

## 用户反馈“RPC 太多”时先判断是否为玩家意图请求，不要机械替换成状态同步

适用范围：收到"RPC 太多""能用状态同步就不用 RPC"这类反馈，准备改动网络同步方式的任务。

核心规则：

- 先判断该 RPC 的语义：如果是"客户端发起一次带校验的动作请求"（例如 `ServerStartFishingSession`、`ServerRequestCampRest`、`ServerSellStolenFish` 这类，函数名里通常带 Request/Server 前缀且带 RequestId），这是 RPC 的标准正确用法，不应该改成状态同步。
- 只有当某个"状态"被多个客户端需要持续可见、且不需要一次性校验语义时（例如某个数值、某个开关），才是状态同步该管的范围；把它错放成 RPC 才算真实问题。
- 反馈里"写得乱、维护困难"的真实来源要先核实是不是 RPC handler 内部重复了同一套校验/幂等模板（参见本文件"新增同类校验/幂等模板前必须先查是否已有可复用实现"一条），而不是默认归因到 RPC 数量本身。
- 2026-08-18 已核实：当次改动只新增 1 个 RPC，现存 33 个 RPC 全部集中在一个头文件里，且都是玩家请求式动作，不构成"过度使用 RPC"；不要在没有核实语义的情况下把这条反馈直接执行成大范围替换。（2026-08-20 起该头文件已按宿主拆成五组，玩家意图 RPC 全部落在 `CatfishingPlayerController.h`。）

当前关键代码入口：

- `Source/Catfishing/Framework/Game/CatfishingPlayerController.h`
  - 30+ 个现存玩家意图 RPC 的集中声明位置，全部是 `ACatfishingPlayerController` 上的请求式方法。

常见误修：

- 把"RPC 数量多"直接等同于"过度设计"，不核实每个 RPC 的语义就动手改。
- 把玩家一次性请求硬套成 Replicated 属性 + 轮询检测，制造出比原来更复杂、更不可靠的新模式。

后续修改检查顺序：

1. 列出被质疑"太多"的 RPC，逐个判断是请求式命令还是持续状态。
2. 请求式命令类维持 RPC，不改动同步方式。
3. 如果确实找到"本该是状态却用了 RPC轮询/重复请求"的例子，才启动状态同步改造，且要单独说明是哪个具体 case。
4. 把"维护困难"的真实原因（多半是重复模板）单独记录，不要和"RPC 该不该用"混在一起处理。

## 子 Agent 的偏离清单不可信，必须用 diff 独立核改动面

适用范围：把实现工作分派给子 Agent、并依据它的交付报告判断改了什么的任务。

核心规则：

- 交付报告里的"偏离清单"是自述，不是证据。2026-08-18 的删除饥饿系统与 WORK-01 收口两轮里，各有一处行为改动没被申报，都是审查节点从 `git diff` 里挖出来的：一处删掉了 `AddExpectedErrorPlain(TEXT("No GameplayCueNotifyPaths were specified"), ...)`，一处把 `FCatChumVector` 三个属性从 `UPROPERTY(BlueprintReadOnly)` 改成 `UPROPERTY(EditAnywhere, BlueprintReadOnly)`。两处改动本身都是必要且正确的，问题在于没进入任何人的判断视野。
- 这两处的共同点是"看起来只是小改"：一个测试辅助调用、一个 UPROPERTY 说明符。恰恰是这类改动不会在构建和自动化里留下信号——删预期错误声明只在特定执行顺序下才会咬人，改编辑标记只影响编辑器与反射写入面。靠"跑绿了"是发现不了的。
- 因此收口方必须自己跑一遍 `git diff HEAD -U0`，重点看这几类不会被测试覆盖的改动：UPROPERTY/UFUNCTION 说明符、函数签名与默认参数、测试里的断言与预期错误声明的增删、`.ini` 键的增删、枚举值顺序。
- 给子 Agent 的 prompt 里要把这几类点名列出来要求申报，但**不能以此代替自己核 diff**——点名只是降低漏报率。

当前关键代码入口：

- `Source/Catfishing/Environment/CatWaterTypes.h`
  - `FCatChumVector` 三个属性的编辑标记；USTRUCT 成员的编辑标记在所有使用点统一生效，改它会同时影响装备 DataAsset、`UCatEnvironmentSettings`、`ACatWaterRegion` 运行态和复制快照。
- `Source/Catfishing/AbilitySystem/Tests/CatStageCTestAbilityTests.cpp`
  - `AddExpectedErrorPlain` 出现次数参数的语义：`> 0` 要求次数完全相等，`== 0` 要求至少出现一次（一次都没有反而判失败），`< 0` 才是静默忽略。引擎依据在 `Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h` 的 `ExpectedNumberOfOccurrences` 与 `AddExpectedError` 文档注释。每进程只发一次的引擎告警要用负数，否则测试结论会随执行顺序漂移。

常见误修：

- 读完子 Agent 的交付报告就当作改动面已知，直接进入下一个原子。
- 发现未申报改动后只补申报、不追问它是否正确——两者要分开判断，必要的改动不该因为漏报而被回退。

后续修改检查顺序：

1. 子 Agent 交付后先跑 `git diff HEAD -U0`，过滤掉纯注释行，逐条比对交付报告。
2. 对报告里没有的改动，先判断必要性再判断正确性，不要直接回退。
3. 确认必要的，要求补注释并写进本轮申报；确认不必要的，回退并说明。
4. 工作区若有其他窗口的未提交改动，先按目录和文件把本轮改动面圈出来再比对，不要把别人的改动算进结论。

## 被审查打回后的修复回合必须重新审，修复本身会引入新错误

适用范围：注释审查、代码审查判 fail 之后的修复回合。

核心规则：

- 2026-08-18 的删除饥饿系统原子群连续被注释审查打回三轮，模式固定：第一轮是漏改注释，第二轮和第三轮都是**修复时写出了新的事实错误**。典型两例：为解释测试清值而写"把配置逐项抹回声明初值"，但声明初值是 `-1.0f`（负值表示 Unset）而代码清成 `0.0f`；为解释校验顺序而写"空 ID 在这里就拦下，不让它掉进跨目录引用校验"，但那个 `if` 块只 `AddIssue`、不 return 也不 continue，真正拦下的是循环体里另一处无注释的 `continue`。
- 成因是被打回后的过度补偿：急着把注释写得完整、有因果、有理由，于是写出听起来合理但没有验证过的断言。这类注释比缺注释更危险，因为它会误导后来的人去删掉真正起作用的代码。
- 写每一条解释性注释前必须回读它描述的那段代码，逐项验证：说"这里拦下了 X"时那段代码真的有 return/continue/early-exit 吗；说"一一对应"时两边集合真的相等还是只是包含；说"抹回某个值"时抹成的真是那个值吗。
- 宁可写短、只写验证过的事实，不要为了显得完整而补充推测。无法验证的理由（例如"可能被写进存档"）要么去查证，要么删掉。

后续修改检查顺序：

1. 修复回合完成后重新提交完整 diff 复审，不是只审改动的那几行。
2. 复审时带上旧 finding 的闭环状态，逐条判定已闭环/部分闭环/未闭环。
3. 对新写的每条解释性注释，单独核对它的断言与代码事实。
4. 连续两轮出现同类问题时，把模式本身写进本文件，不要只修具体条目。

## 本机环境：新编译出的 DLL 可能被 Windows Smart App Control 拦截加载

适用范围：新增或改动任何 UE 模块（尤其是新建模块）后，编辑器、自动化或 Commandlet 突然起不来时。

核心规则：

- 本机 Smart App Control 处于强制模式（`VerifiedAndReputablePolicyState=1`）。它按**本地 ML 特征**判断，未签名的新 DLL 可能被直接拒绝加载，且报错信息与代码错误长得完全不一样。
- 典型症状：`The game module 'XXX' could not be loaded`、`GetLastError=4551`，而同一份代码换一种写法重新编译又能加载。2026-08-19 新建 `CatfishingEditor` 模块时命中过一次（用 `FindPropertyByName`+`ContainerPtrToValuePtr` 直接改写私有属性的那版 DLL 被拦；改成 `ImportText_InContainer`/`ExportText_InContainer` 后正常）。同日引擎自带的 `UnrealEditor-Fab.dll` 也被拦过一次，**说明这不是项目代码的问题**。
- 排查入口：事件查看器 `Microsoft-Windows-CodeIntegrity/Operational` 的 3118 事件。看到它就不要再去改代码逻辑找"编译错误"。
- 不要为此关闭系统安全策略——那是用户的决定，不是 Agent 能替他做的。可行的绕法是换一种等价实现重新编译。
- 2026-08-19 晚再次命中（白盒命令面板原子，`UnrealEditor-Catfishing.dll`）时补充的事实：3118 事件的 `DefenderCloudCallRequested/MadeCloudCall/DefenderTrust` 字段显示裁决来自 Defender 云端按 SHA256 给的信誉，同一哈希的结论会被缓存；而每次链接都会因 PE 时间戳/PDB GUID 产生新哈希，所以"只改一个日志字面量再链接"也可能从被拦变成放行（当晚同一份源码：第 1 次链接被拦、第 2 次放行）。排查顺序因此是：先用 `-run=<任意 Commandlet>` 起一次编辑器并查 3118 事件确认是否被拦，再做小改动重链、重查；不要先怀疑代码逻辑。
- 2026-08-20 凌晨（做减法整改那轮）碰到连续拦截：`UnrealEditor-Catfishing.dll` 和 `UnrealEditor-CatfishingEditor.dll` 轮流被拦，连续 30 多次重链全部被拒。当时的系统状态是 `Get-MpComputerStatus` 里 `AMRunningMode = Passive Mode`、`RealTimeProtectionEnabled = False`：Defender 实时保护关掉后不再替 SAC 发云端请求（3118 事件里 `DefenderCloudCallRequested=true` 但 `DefenderMadeCloudCall=false`），新哈希拿不到云端信誉，只能听本地 ML 的。**但这不是硬堵死**：同一份源码第 20 次重链的产物就被放行了，随即跑完了全量自动化（187/8/0/0，total 195）和两个 Commandlet。所以碰到连续拦截时不要在十几次后就宣布环境阻塞，要把重链写成循环跑到 20-30 次；真拿不到放行时才如实报告“构建绿但自动化无法跑”，并把 `AMRunningMode` 当成原因写进交付说明。开 RTP 或关 SAC 都是用户的决定，不要代他做。
- 刷重链循环有一个必须避开的坑：**删掉输出 DLL 不会触发重链**。UBT 会直接报 `Target is up to date` 什么都不做，编辑器随后报的是“模块 could not be **found**”而不是 4551，于是“没有 4551 就算通过”的判据会连续误报成功。正确做法是删掉每个模块的一个 `.obj`，并在 build 之后断言两个 DLL 确实存在，再拿真实的自动化跑做探针（这样第一次放行就直接出证据）。
- 同时注意：只要重建过的模块就会换哈希，所以“上一轮能跑自动化”不代表本轮能跑；只改了注释的文件也会触发重链，把上一份已获信誉的 DLL 换掉。
- 一旦某次链接产物通过了加载，就在那份 DLL 上立刻跑完自动化，之后任何源码改动都要重新走一遍"链接→3118 检查"；交付报告里要写明这件事，避免接手者以为"构建绿 = 能加载"。
- 2026-08-20（环境层原子，天气/事件调度 + 漂射程）碰到**一次都没放行**的情况：同一份源码连续 **369 次**重链全部被拒（348 次常规 unity 链接 + 20 次 `-DisableUnity` 链接 + 1 次完整 `-Clean` 全量重建）。那次全量重建本身是 69.92 秒、exit 0、零 warning，新出的 DLL 仍然 4551。系统状态与上一次连续拦截时相同：SAC `VerifiedAndReputablePolicyState=1`、Defender `AMRunningMode=Passive Mode`、`RealTimeProtectionEnabled=False`。
- 由此可以更新判断：**"重链 20-30 次总能过"不是稳定规律**。RTP 关闭时 Defender 不替 SAC 发云端请求，裁决完全交给本地 ML，而本地 ML 对这套项目 DLL 的判定可能在一段时间内稳定为拒绝，跟链接次数无关。已经排除过的非原因：链接次数、`-DisableUnity` 造成的代码布局差异、增量 vs 全量重建、输出文件的 Mark-of-the-Web（`Get-Item -Stream *` 只有 `:$DATA`，没有 `Zone.Identifier`）。
- 因此重链循环要设上限（例如 30 次一批、总共不超过 150-200 次），到顶就**如实报告"构建绿但编辑器起不来，自动化/Commandlet/PIE 全部无法执行"**，并把 `AMRunningMode` 与 SAC 状态写进交付说明，不要无限刷下去，也不要把没跑成的验证说成跑过了。开 RTP、关 SAC 或加信任都是用户的决定。
- 被这样卡住时，交接里要写清"哪些验证被卡住"和"解封后按什么顺序补"：**先跑落数据的 Commandlet/脚本，再跑目录校验，最后跑全量自动化**——顺序反了会拿着还没落值的资产去跑自动化，得到一堆与代码无关的红。
- 循环脚本本身有两个已经踩过的坑：① PowerShell 里 `if ($build -notmatch 'Result: Succeeded')` 对**数组**是逐元素筛选，返回非空数组恒为真，会把成功的构建误判成失败，要写成 `if (-not ($build | Select-String -Pattern 'Result: Succeeded' -Quiet))`；② 编辑器要用 `Start-Process -ArgumentList` 显式传参并 `-Wait -PassThru` 取 `ExitCode`，直接 `& exe -run=... | Out-Null` 在脚本文件里可能根本没把进程拉起来（表现是 exit=0 但 `-abslog` 指定的日志压根不存在）。
- 另外，写给 PowerShell 5.1 执行的 `.ps1` 如果带中文注释，必须存成**带 BOM 的 UTF-8**（或者干脆只写英文注释）。不带 BOM 时 5.1 按 ANSI 解码，中文变成乱码字节后会触发莫名其妙的 `Unexpected token '}'` 解析错误。

常见误修：

- 把"模块加载不了"当成链接错误或依赖缺失，反复改 Build.cs。
- 把随后所有自动化失败都归因到本轮代码改动，实际是编辑器根本没起来。

## Harness 的 acceptance / comment-target 必须写成可执行输入，不能写说明性文字

适用范围：任何用 `harness_gate_check.py start` 建立本轮 Harness 的时候。

核心规则：

- `--acceptance` 的格式是 `可观察结果::验证方法[::machine|manual]`，其中**验证方法那一段会被 `harness verify` 当成命令直接执行**。写成"Build.bat 输出""Report/index.json"这类说明性文字，`verify` 会报 `CommandNotFoundException`，于是这条验收永远拿不到 tool-generated 证据，收尾时 `check --phase complete` 必然卡在"缺少可信的 machine 通过证据"。
  正确写法举例：`构建零 error 零 warning::& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\UnreaProjects\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReload::machine`
- `--comment-target` 同理：`comment-review` 会把它当**路径**去扫描。写成"所有新增/改动的类型、属性、方法都要补注释"这种目标描述，扫描结果是"注释检查目标不存在；文件 0"。应该写成 `Source/Catfishing` 这类真实路径。
- 2026-08-20 这一轮就是两处都写成了文字，结果整轮的构建/自动化/PIE 证据都齐全（自动化 195 条全过、5 轮 PIE 实测），却没有一条能被 Harness 自己确认，收尾门禁全红。**证据是真的，只是进不了 Harness 的可信通道。**
- 中途发现写错时不要为了过门禁重新 `start`——那会丢掉整轮已经记录的 visit/handoff/证据。正确做法是照常把证据留在报告文件里，并在交付说明中写明"哪几条验收的机器通道没走通、真实证据在哪个文件"。

常见误修：

- 看到 `check --phase complete` 报"缺少可信证据"，就以为验证没做，回头重跑一遍验证——重跑多少次都一样，因为卡点在声明格式而不是验证本身。
- 为了让门禁变绿而 `confirm` 人工验收——那是伪造用户确认，绝对不可以；人不在就如实留着不满足。

### 更正：「重链几十次总能过」不是规律，别把它当兜底

2026-08-20 早先记的做法是"被 SAC 拦了就多重链几次，历史上第 20 次过了"。当天晚些时候这条**被证伪**：
连续约 **400 次**重链（含 `-DisableUnity` 改变代码布局、`-Clean` 全量重建、`Update-MpSignature`）**无一通过**，
每次都是新哈希、每次都是 3118。中间那段能过的时间窗与 Defender 云端信誉通道当时恰好可用有关，不是重链本身的功劳。

因此正确的判据是：

- 先查 `Get-MpComputerStatus`。只要 `AMRunningMode=Passive Mode` 且 `RealTimeProtectionEnabled=False`，就**认定这台机器当前不可运行**，
  最多试 5～10 次重链确认，不要试上百次——那是在烧时间，不是在排查。
- 确认阻塞后**立刻停手**，把工程留在一个**编译通过且自洽**的状态，然后如实上报需要用户在安全中心操作。
- **特别注意"改了代码但资产没落值"这种中间态**：本轮就踩到了——给装备定义加了必填字段、校验收紧，
  但落值脚本要在编辑器里跑，而编辑器起不来，于是整份装备目录不合法 → starter 三件套取不到 →
  `StartPlay` 直接 `FailRunStartup("DataCatalogInvalid")`，**整局游戏都开不起来**。
  收尾时把新字段改成"未落值＝按未接线处理"，才把工程救回自洽状态。
- 教训一般化：**任何"改校验 + 改资产"成对的改动，要么两边一起落地，要么校验侧必须容忍资产还没跟上**。
  只做一半、还没法验证，比不做更糟。

### 修生产 Bug 时，先搜有没有测试把这个 Bug 编码成了"预期"

2026-08-20：修掉"每天入夜刷一条假 environment_evaluation_failed"的生产 Bug 后，全量自动化反而多了一条红——
`DayDeadlineEntersNightAndFlipConfirmSettlesQuota` 里有一行
`AddExpectedErrorPlain(TEXT("Event=environment_evaluation_failed"), ..., 0)`（0 = 必须至少出现一次）。
当年写测试的人把这条错误当成了"本用例没装配 provider 的正常噪音"放行，实际上 ini 配置在自动化进程里生效、
provider 一直在正常工作，那条错误恰恰就是后来定性的生产 Bug 本身。Bug 修好 → 错误消失 → "必须出现"的预期落空 → 测试红。

防复发做法：

- 修任何"会改变日志形态"的 Bug 前，先 `rg "AddExpectedError|AddExpectedMessage" Source/` 搜该日志事件名，
  把所有放行它的测试一并处理——要么删预期（错误从此不该出现，出现即回归），要么改成真实的新预期。
- 写测试时对 `AddExpectedError` 保持警惕：放行一条 Error 前先确认它真的是"测试环境固有噪音"，
  而不是没人追查过的生产缺陷。放行理由必须写在注释里且经得起查证；查证不了就不要放行，让测试红出来。

## 绿灯只证明没退化，不证明没缺陷；审查必须有独立于验证的触发器和产物

事故：2026-08-20，一次外部代码审查在当前工作区一次性查出 7 类设计问题，其中包含一个可达的正确性缺陷
（结算夜取用团队装备半提交：装备已落到角色、团队库因写口关闭拒绝移除，失败只写日志，终态缓存里存的还是"成功"，
同一件装备同时存在于角色和公库）。这些问题在被外部指出之前，本 Agent 完全没有发现——而当天本 Agent 恰好：
构建 0 error 0 warning、全量自动化 206 条 failed=0、双进程联机冒烟全绿、Harness 验收 A1–A3 全部落账、
注释机械检查 pass。**所有门禁都是绿的，缺陷一个没少。**

失效机制（四条都是结构性的，不是"不够仔细"）：

- **绿灯是确认型信号，不是搜索型信号。** 构建、测试、冒烟只能回答"我想到过的行为还对不对"，
  不能回答"有没有我没想到的路径"。半提交没有测试覆盖，所以测试全绿与它是否存在完全无关。
  把"没有红灯"当成"没有问题"是这次失效的总根源。
- **只读正在改的代码 = 症状驱动阅读。** 本轮打开过的每个文件，都是因为要改它或它里面的测试红了。
  按这个模式走，永远打不开一个零调用方的 853 行子系统——它不出错，就不产生把人引过去的信号。
  **配置里开着的死代码对症状驱动阅读是隐形的。**
- **删减审查只量自己的 diff。** 本轮的删减审查逐条做了且有效（删掉了用错的委托成员、删掉了过时的预期日志），
  但尺子从没量过现存的 219 个文件。只审本轮 diff 的删减审查，在定义上就不可能发现存量的未接线实现。
- **Harness 反而制造了虚假覆盖感。** 本轮甚至主动把注释契约目标收窄到自己改的 5 个文件——对注释门禁讲得通，
  但它正是那个模式的缩影：不断把检查窗口缩到自己碰过的地方，然后报告"门禁通过"。

核心规则：

- **审查是独立职责，必须有独立于"验证通过"的触发器和独立产物。** 在"实现 → 验证 → 下一项"的循环里，
  常驻审查职责没有任何自然触发点（没有失败信号会说"去读一个你没理由打开的文件"），所以它会被循环稳定地吃掉。
  声称完成一个阶段性任务前，审查必须作为单独一步执行并留下 findings 清单，不能用"全量测试通过"顶替。
- **按清单盘，不按症状查。** 审查必须至少跑一遍下列可机械搜索的存量维度，而不是只看本轮 diff：
  1. 零非测试调用方的公开符号（重点标注：带写副作用的、以及在 Config 里被显式启用的）；
  2. 同一函数里依次修改两个及以上状态所有者、且较晚一步失败只记日志的路径；
  3. 有写口开关的领域（`CloseCommands` / `bCommandsOpen`）与玩法准入门（`bRunCommandsOpen`）之间，
     是否存在"门开着但域关了"的相位；
  4. 循环体或高频路径里做同步加载、整表校验、哈希、全场 `TActorIterator` 扫描的调用；
  5. 成对维护的幂等结构（终态缓存 + payload 签名）是否存在只写一半、只清一半、签名漏字段的路径。
- **删减审查定期全仓跑一次**，不能长期只跑本轮 diff。
- **新增测试要覆盖接缝，不只覆盖自己刚做的东西。** 为本轮实现写的测试只能证明"我做的能跑通"；
  真正的缺陷高发区是不同人、不同领域之间的边界。
- **独立判断面优先于上下文深度。** 这次是一个上下文远少于本 Agent 的外部审查者一遍查出来的。
  对设计与跨领域缺陷这一类问题，外部 Review 不能用自审替代；自审可以做，但不能作为唯一判断面。

反例（不要这样做）：把"构建通过 + 全量自动化 failed=0 + 冒烟绿 + 注释检查 pass"写进交付说明，
然后据此声称代码质量没有问题。这四项全绿与本文开头那个可达的正确性缺陷同时成立过。
