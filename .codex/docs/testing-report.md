# Catfishing A–G 开发测试报告

> **文档状态：存在测试缺口**  
> **事实截止时间：2026-08-12 15:20 CST（Asia/Shanghai）**
> **结论口径：** 当前 C++ 工程在目录整理、编码修复和测试补齐后可编译；UE Automation 已扩展到 43 个测试文件 / 64 条测试，其中 63 条 `Catfishing.Unit.*` 与 1 条 `Catfishing.Slice.*` 在同一无窗口批次中全部 Success，warning/error 均为 0。阶段 A 的无窗口 Editor 正常旅行与缺失 Lake 失败补偿证据仍来自同一功能逻辑回归；可见 PIE、Steam 双账号、跨进程复制、真实 UMG 按钮/焦点及 B–G 产品玩法尚未验证。

## 事实来源

- 目标与边界：`Docs/Development/AI开发交接.md`，仅用于确认阶段 A–G 原始目标；其中“空模板”等初始状态已过时，不作为当前实现事实。
- 当前代码与配置：`Source/Catfishing/` 144 个源码/构建文件（101 个非测试文件 + 43 个 Automation 测试 cpp）、2 个 Target 文件、`Config/` 4 个文件、`Scripts/` 4 个文件，以及 `Content/Catfishing/Maps/Frontend.umap`、`Lake.umap` 两个地图资产。
- 最新目录整理基线：本地提交 `25136c8 Organize Catfishing source directories`；`AGENTS.md` 记录“按类型自身所属系统归档，而不是按服务对象归档”的文件分类规则。
- 目录整理与测试补齐后补充验证：严格 UTF-8 扫描通过；`comment_quality_check.py` 最新扫描 `Source/Catfishing` 144 文件 / 2044 条注释 / 0 error；`git diff --check` 通过；新增/修正测试后 `CatfishingEditor` 与 `Catfishing` Game Target 均重新构建通过。
- 最终验证证据根：`Saved/Verification/BG_StaticBuild_20260812_011156/`。
- 最终同源构建证据：`build_CatfishingEditor_final_header_167_179_206.log`、`build_Catfishing_final_header_167_179_206.log`、`final_header_167_179_206_static_summary.log`。
- 最终运行证据：`runtime_safe_null_travel_after_wait_end_pie_fix.log`、`runtime_safe_null_missing_lake_failure_after_wait_end_pie_fix.log`。
- 覆盖边界与资产缺口：`runtime_bg_gate_matrix.md`、`runtime_wait_end_pie_fix_baseline_hashes.csv`。
- 辅助证据：`static_python_ast.log`、`static_git_diff_check.log`、`runtime_main_asset_query_process.log`、`runtime_main_game_frontend_process.log`、`postbuild_process_check.log`。
- 自动化测试方案与模块级实现：`Docs/Development/自动化测试方案.md`、`Source/Catfishing/*/Tests/*Tests.cpp`。
- Items 四项批量证据：`Saved/Automation/20260812-items-commit-capture-suite-01/Build-UBT.log`、`Game-Build-UBT.log`、`Automation.log`、`Report/index.json` 与 `index.html`；逐条证据另见 `20260812-items-replay-tracer-01`、`20260812-items-stale-revision-01`、`20260812-items-wrong-owner-01`、`20260812-items-full-container-01`。
- 最终 64 条完整批量证据：`Saved/Automation/20260812-full-automation-04/Automation.log`、`Report/index.json` 与 `index.html`；最终结果为 succeeded=64、succeededWithWarnings=0、failed=0、notRun=0、inProcess=0、warnings/errors 合计 0。该批次包含 63 条 `Catfishing.Unit.*` 与 1 条 `Catfishing.Slice.*`。
- 独立审查交接：完整 Source/Config/Scripts/资产元数据代码审查最终 `pass`，F01–F15 与 CMT01–CMT22 已闭环；其中 CMT16–CMT22 是 6 处 header 与 1 处 cpp 的注释准确性修正。该结论不替代运行验收。

## 1. 测试概览

| 编号 | 验证层级 | 实际目标 | 结果 | 强度与边界 |
|---|---|---|---|---|
| T-01 | 静态检查 | Python 语法、补丁空白、运行副本未污染、残留进程 | 通过 | 仅证明静态可解析与验证过程卫生 |
| T-02 | C++ 构建 | `CatfishingEditor` Win64 Development | 通过；目录整理后 28/28 actions，18.87 s，exit 0 | UE 5.8 编译/UHT/链接证据；旧 runtime 同源构建另见原日志 |
| T-03 | C++ 构建 | `Catfishing` Game Win64 Development | 通过；目录整理后 35/35 actions，40.42 s，exit 0 | 非 Editor Target 编译/链接证据；旧 runtime 同源构建另见原日志 |
| T-04 | 无窗口 Editor 真实运行 | 主项目资产查询与 Frontend 启动 | 通过 | `UnrealEditor-Cmd`、无窗口、非人工 PIE |
| T-05 | 无窗口 Editor 真实运行 | 安全 Null OSS 副本，两轮 Host→Lake→Leave→Frontend | 通过，50.704 s，PASS 1 / FAIL 0 | 覆盖阶段 A 四事实状态与重复 pending；不是 Steam |
| T-06 | 无窗口 Editor 真实运行 | 安全副本移除 Lake 后的旅行失败、Destroy 补偿与新请求重试 | 通过，43.181 s，PASS 1 / FAIL 0 | 覆盖受控负路径；未直接注入真正迟到 OSS 回调 |
| T-07 | 配置/资产门禁扫描 | B–G 正式资产、产品数值、权限与持久化策略 | 存在阻断缺口 | 证明系统 fail-closed；没有运行 B–G 玩法 |
| T-08 | 可见 PIE / 人工体验 | 键位、UI、角色、钓鱼、营地、社交完整体验 | 未执行 | 必须由人类在正式资产装配后执行 |
| T-09 | Steam 双账号 | Create/Find/Join/Invite/掉线重连/Host 退出 | 未执行 | 缺正式 Online 策略及双账号环境 |
| T-10 | 目录/编码回归 | 源码按领域目录归档后，确认 UTF-8、include、注释和双 Target 构建 | 通过 | 只覆盖目录整理本身；没有重跑 UE runtime |
| T-11 | L1 Automation 合同测试 | Items 捕获重放、过期 Revision、错误 owner、容量已满四条合同 | 通过；Found 4，Success 4，exit 0 | 真实 Game World、真实 WorldSubsystem 与公开 Snapshot；不覆盖跨进程复制或其他操作族 |
| T-12 | L1/L2/L3 Automation 完整批次 | 43 个测试文件、63 条 `Catfishing.Unit.*` 与 1 条 `Catfishing.Slice.*` | 通过；Found 64，Success 64，warning/error 0，exit 0 | 真实 UE Automation 与若干真实 Game World；覆盖第一条 Items→Collection 切片；不覆盖 Steam、可见 PIE、真实 UMG 按钮/焦点或正式资产 |
| T-13 | L3 Automation 纵向切片 | `Items→Collection` committed capture 到 `FishRecorded` Grant 的最小桥接 | 通过；已纳入完整批次，Success 1，exit 0 | 真实 UE Automation、真实 Game World 和两个真实 WorldSubsystem；不覆盖 Fishing 起点、Profile durable ACK、跨进程复制或可见 PIE |

## 2. 逐项测试记录

### T-01 静态与验证卫生

- **验收项映射：** 工程脚本可加载；验证不修改主工作区；退出后无残留 UE/构建进程。
- **来源：** Graph/Harness 的证据优先约束，以及阶段 A 自动化脚本。
- **目标：** 排除脚本语法错误、空白错误、验证副本污染和后台进程残留。
- **方法与入口：** 对 `Scripts/*.py` 做 AST 解析；执行 `git diff --check`；在两次运行前后比较 114 个主文件哈希；扫描相关进程。
- **前置条件：** 当前冻结代码面；验证工作在 `Saved/Verification/...` 的安全副本中运行。
- **实际观察：** 4/4 Python 脚本 AST 解析通过；`git diff --check` exit 0；114 个基线文件 hash delta 0；最终残留进程数 0。
- **证据：** `static_python_ast.log`、`static_git_diff_check.log`、`runtime_wait_end_pie_fix_baseline_hashes.csv`、`postbuild_process_check.log`。
- **已覆盖：** Python 语法、补丁空白、主文件未被运行测试改写、进程清理。
- **未覆盖：** Python 行为正确性、二进制资产语义、用户工作树归属。
- **证据强度：** 中；静态和过程卫生证据，不是玩法证据。
- **判断：** 通过。

### T-10 目录整理与编码修复回归

- **验收项映射：** 文件结构按领域归属整理后，不引入编码破坏、旧 include、旧路径引用或构建回退。
- **来源：** 用户要求“按类自身隶属类型归档”、`AGENTS.md` 的目录分类规则、本地提交 `25136c8`。
- **目标：** 证明 `AbilitySystem/`、`Character/`、`Condition/`、`Framework/Game/`、`Logging/`、`Online/`、`UI/` 等目录整理后，源码仍是严格 UTF-8，UE 反射和链接仍可通过。
- **方法与入口：** 对 `AGENTS.md`、`Catfishing.uproject`、`Config/`、`Source/Catfishing/`、`Scripts/`、`.codex/docs/` 做 strict UTF-8 扫描；搜索旧根路径 include；运行 `git diff --check`；运行注释机械检查；分别构建 `CatfishingEditor` 与 `Catfishing`。
- **实际观察：** strict UTF-8 扫描 114 个文件通过；旧根 include/旧源路径未命中；`comment_quality_check.py` 为 101 文件、1884 条注释、0 error、pass；Editor 构建 28/28 actions、18.87 s、exit 0；Game 构建 35/35 actions、40.42 s、exit 0。
- **未覆盖：** 没有在目录整理后重跑无窗口 UE runtime、可见 PIE、Steam 或 B–G 产品玩法；这些仍按原测试缺口和人工验收计划处理。
- **证据强度：** 强于纯静态检查，能证明目录整理后的 C++/UHT/链接可用；不能替代运行验收。
- **判断：** 通过。

### T-11 Items CommitCapture 四项合同

- **验收项映射：** Items 捕获事务必须形成稳定终态；重复请求、过期 Revision、错误身份和容量不足不得复制实物鱼、留下部分 Committed 事实或错误推进容器 Revision。
- **来源：** `Docs/Development/自动化测试方案.md` 第 7 节与 `UCatItemsService` 的公开合同。
- **目标：** 证明合法捕获可提交且重放不复制鱼；Revision 冲突返回最新版本且不写入；非 owner 被拒绝且不写入；正容量已占满时返回容量错误且不写入。
- **方法与入口：** 每条都由 `FTestWorldWrapper` 创建独立真实 Game World；取得真实 `UCatItemsService`；在 authority Actor 上注册真实 `UCatContainerReplicationComponent`；只调用 `RegisterContainer`、`CommitCapture`、`TryGetContainerSnapshot`。失败场景在调用前保存公开 Snapshot，调用后逐字段比较容器和既有鱼。
- **实际观察：** 批量过滤精确发现 4 条，四个完整路径均 Success。重放返回 `AlreadyResolved` 且只有一条鱼；陈旧版本返回 `RevisionConflict / Revision 2`；错误身份返回 `PermissionDenied / Revision 1`；满容量返回 `CapacityExceeded / Revision 2`。三个拒绝结果均未提交、Committed DTO 保持默认，失败前后公开 Snapshot 不变。Automation 报告 succeeded=4、succeededWithWarnings=0、failed=0、notRun=0、warnings/errors 合计 0，进程 44.3 s 自然 exit 0。Game Target 最终实际执行 4 actions，明确编译 `CatItemsServiceTests.cpp` 并链接 `Catfishing.exe`，21.12 s、exit 0。
- **证据：** 总回归为 `Saved/Automation/20260812-items-commit-capture-suite-01/`，其中 `Game-Build-UBT.log` 保存最终 Game 构建；逐条目录为 `20260812-items-replay-tracer-01`、`20260812-items-stale-revision-01`、`20260812-items-wrong-owner-01`、`20260812-items-full-container-01`。
- **副作用检查：** 四条测试分别注销容器并销毁 Test World，WorldContext 均恢复；批量运行前后 Source/Config/Content 指纹同为 `A992EED680FEB45B8B75D9EDA4CEBBF7E37FE12AB1F3CDEB63BD1A0C553347EE`；无残留 UE/UBT/Shader/LiveCoding/UBA 进程。
- **未覆盖：** FastArray 跨进程复制、Steam、真实地图、多客户端、UI、StateTree、Capacity=0 的未决策略、Transfer/Consume/Reserve/Theft 等其他 Items 操作和其他模块测试族。
- **证据强度：** 强；真实 UE Automation、真实 Game World 与公开生产接口，但仍是单进程 L1 合同测试。
- **判断：** 通过。这 4 条都是既有合同的 characterization GREEN，没有为了展示 TDD 人为制造 RED。

### T-12 完整 `Catfishing` Automation 套件

- **验收项映射：** 用户要求“所有模块测试一次性写完”，并按模块目录内 `Tests/` 存放测试文件；当前阶段先固定各模块公开合同、fail-closed gate、局部生命周期和事务边界，为下一步跨模块纵向切片提供基线。
- **来源：** `Docs/Development/自动化测试方案.md` V3、项目目录分类规则、各模块公开接口与 UE Automation 报告。
- **目标：** 证明当前 43 个测试文件能被 UE Automation 发现并执行，覆盖 AbilitySystem、Camp、Character、Collection、Condition、Data、Environment、Equipment、Fishing、Framework/Game、Items、Online、Profile、Run、Social、UI 的基础合同，并纳入 Items→Collection 第一条纵向切片。
- **方法与入口：** 构建 `CatfishingEditor` 与 `Catfishing` 两个 Development Target；随后使用无窗口 `UnrealEditor-Cmd` 执行 `Automation RunTests Catfishing`，输出到 `Saved/Automation/20260812-full-automation-04/`。
- **前置条件：** 不启动可见 UE；不使用 Steam 双账号；测试只读/瞬态构造配置对象，或用 `FTestWorldWrapper` 建真实 `EWorldType::Game` 测试 World。
- **实际观察：** 报告 `tests=64`、`succeeded=64`、`succeededWithWarnings=0`、`failed=0`、`notRun=0`、`inProcess=0`、测试 warnings/errors 合计 0；完整路径首条为 `Catfishing.Slice.ItemsCollection.CommitCaptureRecordsSingleFishGrant`，末条为 `Catfishing.Unit.UI.TravelWidget.ClassAndOpaqueHandlesRemainViewOnly`；`LogAutomationController: Error`、Fatal、Ensure 均为 0。
- **证据：** `Saved/Automation/20260812-full-automation-04/Report/index.json`、`Saved/Automation/20260812-full-automation-04/Automation.log`。本轮构建证据来自终端输出：Editor 5 actions / exit 0，Game 4 actions / exit 0，均明确编译最终改动的测试 cpp 并链接成功。
- **覆盖范围：** 覆盖 Settings/Definition readiness、Character 默认组件、AbilitySystem 诊断 Ability、Fishing Service/Session fail-closed、StateTree node 默认合同、Condition wet/营地搬运、Items 捕获/转移/消费/预留/关门事务、Collection 印记计划、Environment 水域聚合与查询、Equipment/Camp/Social/UI/Online/Run/Profile 的基础合同，以及 Items→Collection 最小切片。
- **未覆盖范围：** 未覆盖完整 Fishing→Items→Collection→Profile、Run→Sacrifice→Items、Character→AbilitySystem→Condition→UI 等正向纵向链；未覆盖 Steam、跨进程复制、真实 StateTree 资产拓扑、正式 DataAsset/地图摆放、输入、真实 UMG 按钮/焦点和可见 UI。
- **证据强度：** 强于静态检查和构建，属于真实 UE Automation；低于完整集成/PIE/Steam 验收。
- **判断：** 通过，可作为进入跨模块纵向切片前的模块级回归基线。

### T-13 `Items→Collection` 跨模块纵向切片

- **验收项映射：** 用户要求“没问题就进入跨模块纵向切片”；当前先选择不需要新增生产 seam 的最小链路，验证 Items 的 committed capture 事实能被 Collection 作为正式输入消费。
- **来源：** `Docs/Development/自动化测试方案.md` 的 L3 切片要求、`Source/Catfishing/Items/CatItemsService.*` 的 `CommitCapture` 公共结果，以及 `Source/Catfishing/Collection/CatRunImprintService.*` 的 `RecordCommittedCapture` 公共入口。
- **目标：** 证明一条通过 Items 写入的实物鱼，可以不读私有状态、不加测试后门，转化为 Collection 的单个 `FishRecorded` Grant；同一 `CaptureRequestId` 重放不会产生第二个 Grant 或增加 pending ACK。
- **方法与入口：** 新增 `Source/Catfishing/Collection/Tests/CatItemsCollectionSliceTests.cpp`。测试用 `FTestWorldWrapper` 创建真实 `EWorldType::Game`，取得真实 `UCatItemsService` 与 `UCatRunImprintService`；在 authority Actor 上注册真实 `UCatContainerReplicationComponent`；先调用 `CommitCapture`，再把公开 `CaptureResult.Committed` 传给 `RecordCommittedCapture`。
- **前置条件：** 不启动可见 UE；不使用 Steam、真实地图、StateTree、Profile SaveGame 或外部成像桥；测试只在单进程 Game World 内验证两个真实模块的组合行为。
- **实际观察：** Automation 精确过滤发现 1 条 `Catfishing.Slice.ItemsCollection.CommitCaptureRecordsSingleFishGrant`，报告 `succeeded=1`、`failed=0`、`notRun=0`、`inProcess=0`、warnings/errors 合计 0；日志显示测试 `Success`，并出现 Items 的 `items_capture_terminal ... Committed=true Error=None ContainerRevision=2` 结构化日志。
- **证据：** 该切片已纳入 `Saved/Automation/20260812-full-automation-04/Report/index.json`、`Saved/Automation/20260812-full-automation-04/Automation.log`；早期单独切片证据仍保留在 `Saved/Automation/20260812-slice-items-collection-01/`。
- **覆盖范围：** 覆盖 Items 成功捕获事实、Collection 记录 committed capture、同一 capture 重放幂等、pending Grant ACK 数量不重复、teardown 前仍能暴露 pending durable ACK。
- **未覆盖范围：** 未覆盖 FishingService/StateTree 产生捕获事实、Profile 接收 durable Grant ACK、SaveGame 磁盘恢复、FastArray 跨进程复制、Steam、可见 UI 或多人协作。
- **证据强度：** 中强；真实 UE Automation 与真实 WorldSubsystem 组合测试，强于单模块合同；但它是第一条最小切片，不代表完整玩法链路。
- **判断：** 通过，可作为后续 Fishing→Items→Collection→Profile 完整切片的第一颗“铆钉”。

### T-02 Editor Target 构建

- **验收项映射：** UE 5.8 下 UHT、C++ 编译与 Editor 链接可完成。
- **来源：** 当前 `.uproject`、单 Runtime 模块与全部公共反射合同。
- **目标：** 捕获 UHT 签名、模块依赖、Include 根、反射类型和链接问题。
- **方法与入口：** 全量构建 `CatfishingEditor` Win64 Development。
- **前置条件：** UE 5.8 工具链可用；不启动图形化 Editor。
- **实际观察：** 原 A–G runtime 同源构建为 7/7 real actions、11.218 s、exit 0；目录整理和编码修复后重新构建为 28/28 actions、18.87 s、exit 0。
- **证据：** 原同源证据为 `build_CatfishingEditor_final_header_167_179_206.log`、`final_header_167_179_206_static_summary.log`；目录整理后的构建记录来自本轮终端证据。
- **已覆盖：** Editor Target 的 UHT、编译和链接。
- **未覆盖：** 目录整理后未重跑可见 Editor 交互、蓝图编译、Cook/Package、Shipping 或 UE runtime。
- **证据强度：** 强，真实构建。
- **判断：** 通过。

### T-03 Game Target 构建

- **验收项映射：** Runtime 模块未依赖 Editor-only API，Game Target 可链接。
- **来源：** `Catfishing.uproject` 与 `Source/Catfishing/Catfishing.Build.cs`。
- **目标：** 证明机械修复没有只让 Editor Target 偶然通过。
- **方法与入口：** 全量构建 `Catfishing` Win64 Development。
- **前置条件：** UE 5.8 工具链可用。
- **实际观察：** 原 A–G runtime 同源构建为 6/6 real actions、15.313 s、exit 0；目录整理和编码修复后重新构建为 35/35 actions、40.42 s、exit 0。
- **证据：** 原同源证据为 `build_Catfishing_final_header_167_179_206.log`、`final_header_167_179_206_static_summary.log`；目录整理后的构建记录来自本轮终端证据。
- **已覆盖：** Game Target 的编译和链接、Runtime/Editor 依赖边界。
- **未覆盖：** 目录整理后未重跑打包、Cook、Steam 发布构建、Shipping 或 UE runtime。
- **证据强度：** 强，真实构建。
- **判断：** 通过。

### T-04 主项目无窗口启动与资产查询

- **验收项映射：** Frontend/Lake 两张地图可由当前项目识别；Frontend 可在真实引擎进程中启动并正常退出。
- **来源：** 阶段 A 地图基线和 GameMode 接线。
- **目标：** 证明二进制地图不是只有文件名存在，主项目也不是仅能编译。
- **方法与入口：** 使用 UE 5.8 `UnrealEditor-Cmd` 在主项目上执行资产查询与 Frontend 无窗口运行。
- **前置条件：** 不进入可见 PIE；使用主项目正式配置。
- **实际观察：** 查询到 Frontend/Lake 两个资产；Frontend 加载、Online teardown 与退出完成；致命扫描无命中。
- **证据：** `runtime_main_asset_query_process.log`、`runtime_main_game_frontend_process.log`、`runtime_marker_counts.log`。
- **已覆盖：** 主项目的地图可发现性、Frontend 冷启动和基础 teardown。
- **未覆盖：** Steam 会话、Lake 产品体验、B–G 玩法。
- **证据强度：** 中强，真实引擎进程但无窗口、无人交互。
- **判断：** 通过。

### T-05 阶段 A 正常旅行回归

- **验收项映射：** 四事实状态 `Session / World / Role / Transport` 收敛；同一时刻只允许一个 Online 操作；Widget 生命周期成对；Host 离开先 teardown 再销毁会话。
- **来源：** `UCatOnlineSubsystem`、`UCatLocalPlayerUISubsystem`、`Scripts/verify_stage_a_travel.py`。
- **目标：** 在不触碰主项目 Steam 配置的安全副本中，验证同步 Null OSS 回调、两轮 Create/Travel/Leave 和 EndPIE 清理。
- **方法与入口：** `UnrealEditor-Cmd.exe` 对安全 Null OSS 副本执行 `py Scripts/verify_stage_a_travel.py`，`-unattended -nullrhi -NoCompile`。
- **前置条件：** 副本临时使用 Null OSS，并把未决的 `SessionAccess` 设为 Public；主项目保持 Steam 且未被改写。
- **实际观察：** 50.704 s，exit 0，`STAGE_A_TRAVEL_PASS` 1、FAIL 0；Lake 2 次、Frontend 2 次；`Host / Lake / Connected` 两轮，`NoSession / Frontend / Idle` 两轮；重复 pending 拒绝 4 次；`teardown_ready` 2 次；Widget 检查 5/5；致命扫描 0。
- **证据：** `runtime_safe_null_travel_after_wait_end_pie_fix.log`。
- **已覆盖：** 同步回调重入、公开入口 pending 优先级、Host 两轮旅行、Frontend transport 复位、Host teardown、Widget 去重与 EndPIE 清理。
- **未覆盖：** Steam 异步时序、Find/Join/Invite、远端客户端、掉线重连、Host ACK timeout 的网络实战。
- **证据强度：** 强于静态检查，属于真实 UE Editor 运行；平台覆盖仍为 Null OSS。
- **判断：** 通过。

### T-06 缺失 Lake 的失败补偿回归

- **验收项映射：** TravelFailure 后清理 Session、使旧 epoch 失效，并允许新 RequestId 重试。
- **来源：** `UCatOnlineSubsystem::HandleTravelFailure`、Destroy 补偿路径与 `Scripts/verify_stage_a_travel_failure.py`。
- **目标：** 验证“会话已创建但地图旅行失败”的补偿和生命周期收口。
- **方法与入口：** 在独立安全副本移除 Lake 资产，使用 Null OSS 和无窗口 Editor 执行负路径脚本。
- **前置条件：** 主项目不变；副本故意缺少 Lake。
- **实际观察：** 43.181 s，exit 0，PASS 1 / FAIL 0；epoch 1 接受首个请求，失败后 epoch 2 收敛为 `NoSession / Frontend / Failed / TravelFailed`；新 RequestId 在 epoch 3 被接受，epoch 4 完成清理。
- **证据：** `runtime_safe_null_missing_lake_failure_after_wait_end_pie_fix.log`。
- **已覆盖：** TravelFailure 终态、Destroy 补偿、epoch 前进、新请求释放、EndPIE 收口。
- **未覆盖：** 真正的网络断线、平台回调晚到、DestroySession 本身失败/超时、远端 Host Grant ACK timeout。
- **证据强度：** 强于静态检查，属于受控负路径的真实引擎运行。
- **判断：** 通过。

### T-07 B–G 配置与资产门禁

- **验收项映射：** 未决产品配置不得被默认值伪装成成功，缺资产时必须 fail-closed。
- **来源：** 各 `UDeveloperSettings`、Catalog、StateTree 软引用和场景 Provider。
- **目标：** 明确哪些合同已经落地，哪些产品事实仍不存在。
- **方法与入口：** 读取主项目 Content/Config、代码默认值与 Lake 启动日志；形成逐阶段 gate matrix。
- **前置条件：** 不临时写入产品数值，不制造占位 StateTree/DataAsset。
- **实际观察：** Content 仅有 Frontend/Lake；缺正式 `ST_RunFlow`、`ST_FishingSession`、Fish/Equipment DataAsset、InputAction/MappingContext、WaterRegion/Camp 场景装配；Online、Run、Environment、Fishing、Items、Profile、Camp、Social 的关键产品策略均未决或关闭。Lake 日志明确出现 environment/run startup fail-closed，没有 C++ 伪 fallback。
- **证据：** `runtime_bg_gate_matrix.md`、`runtime_bg_asset_config_gate_scan.log`、`runtime_safe_null_travel_process.log`。
- **已覆盖：** 缺配置/资产时的门禁与事实暴露。
- **未覆盖：** B–G 正常玩法路径、数值正确性、多人一致性与长期存档。
- **证据强度：** 中；配置与失败路径证据。
- **判断：** 门禁行为符合预期，但产品运行验收被阻断。

## 3. 关键失败、修复与重跑记录

| 顺序 | 失败现象 | 根因 | 最小修复 | 重跑结果与证据 |
|---|---|---|---|---|
| F-01 | 最初两次构建停在 UBT 启动/WaitMutex | 受限环境中的工具启动与锁等待，不是已定位的源码错误 | 终止挂起进程，在允许的真实工具链环境重跑 | 后续进入真实 UHT；`build_CatfishingEditor_attempt1_waitmutex_terminated.log`、`...attempt2_sandbox_startup_terminated.log` |
| F-02 | Editor/Game 首次真实构建 UHT exit 6 | replicated `TArray` RPC 参数未按 const reference 声明 | 把相关 RPC 参数改为 `const TArray<...>&` | UHT 越过该错误；`build_CatfishingEditor_Win64_Development.log`、`build_Catfishing_Win64_Development.log` |
| F-03 | 随后 C++ 编译报跨目录 include 缺失、C4458、类型未声明与签名连锁错误 | 单 Runtime 模块的根 include 未登记，个别 cpp 缺直接 include，局部变量遮蔽反射成员 | `PublicIncludePaths.Add(ModuleDirectory)`；补直接 include；局部变量改名 | `...after_compile_fix2` 两个 Target 成功，最终两个 Target 又重建成功；`build_CatfishingEditor_after_rpc_fix_errors.txt`、`build_CatfishingEditor_Win64_Development_after_rpc_fix.log` |
| F-04 | 正常旅行脚本报 `create_submit_mismatch` | Python 对 UE `FGuid` wrapper 的身份/字符串表达比较不稳定 | 统一通过 UE 5.8 `to_string()` 生成规范 GUID 文本 | 首次改法仍失败，`to_string()` 版本越过 RequestId 比较；相关 `...after_guid_fix...` 与 `...after_guid_to_string...` 日志 |
| F-05 | 一次 `to_string()` 重跑挂起；再跑报 `create_pending_gate_mismatch` | 脚本的 EndPIE 等待顺序有问题；Online 六个公开入口先做状态校验，导致 active request 的重复命令返回 `InvalidState` 而不是统一 pending 错误 | 脚本先处理 `WAIT_END_PIE`；六个入口把 pending gate 提到业务状态校验之前 | pending gate 后续 4 次均按 `CommandAlreadyPending` 拒绝 |
| F-06 | 重跑报 `frontend_fact_mismatch` | 回到 Frontend 后 Transport 仍保留 `Connected` | `HandlePostLoadMap` 在 Frontend 明确把 transport 收敛为 `Idle` | 后续两轮均为 `NoSession / Frontend / Idle` |
| F-07 | 旅行事实已正确但脚本超时在 `WAIT_END_PIE` | EndPIE 后脚本先重新获取已销毁 Online，再检查结束状态 | 两个脚本都把 EndPIE 分支放到 Online 枚举之前 | 最终正常旅行 50.704 s、负路径 43.181 s，均 exit 0 |
| F-08 | 负路径最初报 `request_id_changed_before_first_terminal` | TravelFailure 的内部 Destroy 补偿曾开启新的操作身份，使原 Create 的 RequestId 在该用户请求到达失败终态前被替换 | Destroy 作为原 Create/Join 的内部补偿继续沿用该 RequestId/operation；用 `DeferredFailureAfterDestroy` 在 Destroy callback 后结束原请求，只有用户发起重试才生成新 RequestId | 最终观察首请求在 epoch 1 接受、epoch 2 失败终态仍可关联；用户重试在 epoch 3 获得新 RequestId，epoch 4 清理 |
| F-09 | 模块套件首次运行 `Condition.Component.CarryToCampRequiresValidRescueFactAndReplays` 失败 | 测试夹具直接 Spawn 抽象 `AController`，导致有效搬运 Controller 为空 | 改为实例化 `APlayerController` 并补直接 include | 最终 `20260812-full-automation-04` 中该测试 Success，完整批次 64/64 |
| F-10 | Items 事务测试编译和运行先后失败 | 测试文件残留 UE 5.8 不存在的 `FTestWorldWrapper(EWorldType::Game)` 构造、命名空间类型未限定、`TObjectPtr` 直接传给 `TestNotNull`，以及把容器初始 Revision=1 误写成 0 | 统一 `CreateTestWorld`；给 `FRegisteredContainer` 加命名空间限定；`TestNotNull` 使用 `.Get()`；按真实 Revision 链修正捕获/转移/消费/预留期望 | Editor/Game 目标重新构建成功；最终 `20260812-full-automation-04` 中 Items 事务相关测试全部 Success |
| F-11 | 第一条切片需要避免变成“读私有状态”的假集成 | Collection 当前不公开 Grant payload，直接验证 Profile ACK 会迫使测试读取私有队列或新增测试后门 | 切片先停在 `RecordCommittedCapture` 的公开返回值和 pending ACK 计数；不强行越界到 Profile | `20260812-full-automation-04` 证明 Items→Collection 最小桥接成功；Profile durable ACK 留给下一条切片 |

最终 runtime PASS 不是在源码机械修复之前取得的旧逻辑结果：Online transport/pending 修复后，Editor 与 Game 两个 Target 均已重建并运行。其后只发生注释准确性修正；当前 header 同源的最终重链再次通过，而且 Editor/Game PE `.text` 的 size 与 SHA-256 均和产生 runtime PASS 的副本完全一致，证明可执行逻辑没有变化。详见 `final_header_167_179_206_static_summary.log`。

## 4. 反向验证与副作用检查

- 重复 Create/Leave 不得开启第二条操作：最终日志观察到 4 次 pending 拒绝。
- Host 离开不得先销毁 Session 再丢失 Run 交付：两轮均观察到 `teardown_ready`。
- 回到 Frontend 不得残留连接事实：两轮均收敛 `NoSession / Frontend / Idle`。
- 旅行失败不得锁死后续命令：新 RequestId 和更高 epoch 被接受。
- EndPIE 不得遗留 View 或进程：Widget 生命周期 5/5，残留进程 0。
- 自动化不得改主项目：114 个主文件 hash delta 0。
- 缺 B–G 产品事实不得伪造成功：各域保持 unset/disabled/undecided 并 fail-closed。
- 旧“双人抄网”规则不得复活：代码审查确认巨鱼协作只发生于 HookedFight，NearShore 首个合法抄中者获得鱼；该规则尚未做多人运行实测。
- 自动化不得被错误过滤器伪装成通过：最新批次明确发现 64 条 `Catfishing.*`，不是 0 test；所有个案 Success 且 warnings/errors 合计 0。
- 纵向切片不得绕过模块公开接口：第一条 `Catfishing.Slice.*` 只使用 Items/Collection public seam，不读取私有 map/cache，也没有新增生产测试后门。

## 5. 测试缺口与严重度

### 严重：阻断产品完成声明

1. **B–G 正常 runtime 完全未跑。** 正式 Run/Fishing StateTree、Fish/Equipment DataAsset、输入资产、WaterRegion/Camp 场景 Actor 与产品设置缺失，无法形成可玩的正常路径。
2. **Steam 双账号未验证。** 没有真实 Create/Find/Join/Invite、中途加入、掉线重连、Host 退出、StableNetId 与 Grant ACK 的平台证据。
3. **人工可见 PIE 未验证。** 没有玩家视角的操作、画面、键位、焦点、UI 可读性、角色反馈和完整日循环证据。

### 中等：上线前必须补齐

1. Profile SaveGame 持久化、崩溃恢复、损坏存档、重复 Grant 与真实外部印记文件桥未运行。
2. 1/2/4/8 人网络矩阵、延迟/丢包/迟到 RPC、客户端恶意参数和跨域事务部分失败未做真实运行压力。
3. Cook/Package/Shipping、Steam 打包和发布配置未验证。
4. StateTree 拓扑唯一性目前靠代码合同与资产缺失门禁，尚无正式资产可审查。

### 较低：维护风险

1. 最终构建仍提示 Target 使用 `EngineIncludeOrderVersion.Unreal5_6` 兼容顺序；在 UE 5.8 可编译，但建议单独升级后重建，避免未来 include 顺序差异。
2. 当前自动化已覆盖一批 B–G 模块级合同，并落地第一条 Items→Collection 纵向切片；但完整 Fishing→Items→Collection→Profile、Run→Sacrifice→Items、Character→AbilitySystem→Condition→UI 等真实玩法 Functional Test 仍未落地。

## 6. 结论

当前证据足以支持以下窄结论：

- 当前冻结代码在 UE 5.8 的 Editor/Game Development Target 中编译、链接通过。
- 阶段 A 的 Null OSS 两轮旅行、四事实收敛、重复 pending gate、Widget/EndPIE 生命周期，以及缺失 Lake 的失败补偿与重试，在无窗口真实 Editor 中通过。
- 43 个测试文件的 63 条 `Catfishing.Unit.*` 与 1 条 `Catfishing.Slice.*` 已作为基础回归通过；其中 Items→Collection 最小链路能通过公开接口组合。
- 当前验证没有改写主项目，也没有留下 UE/构建进程。

当前证据**不支持**以下声明：

- 不支持“阶段 B–G 已运行通过”或“产品体验已完成”。
- 不支持“Steam 联机已通过”。
- 不支持“可见 PIE 与人工验收已通过”。
- 不支持“正式数值、键位、StateTree、DataAsset、水域、营地和持久化策略已经确定”。

因此本报告保持 **存在测试缺口**，下一步必须先由产品/设计补齐正式资产与设置，再按 `acceptance-report.md` 执行人工冷启动和 Steam 双账号验收。
