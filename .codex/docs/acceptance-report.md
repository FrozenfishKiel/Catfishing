# Catfishing A–G 开发人工验收报告

> **文档状态：待人工验收**  
> **事实截止时间：2026-08-12 02:21 CST（Asia/Shanghai）**  
> **当前阻断：** 阶段 B–G 的正式产品设置、输入资产、两棵 StateTree、Fish/Equipment DataAsset、水域与营地场景装配尚未提供。当前代码会按设计 fail-closed，因此可以验收“没有伪成功”，但无法完成正式产品体验验收。

## 事实来源

- 原始范围与产品边界：`Docs/Development/AI开发交接.md`。
- 当前实现事实：`Source/`、`Config/`、`Scripts/` 和 `Content/Catfishing/Maps/Frontend.umap`、`Lake.umap`。
- 产品门禁清单：`Saved/Verification/BG_StaticBuild_20260812_011156/runtime_bg_gate_matrix.md`。
- 已完成的工程验证：同目录下两个最终 build 日志、两个最终 runtime 日志及 `runtime_wait_end_pie_fix_baseline_hashes.csv`。
- 测试边界：`.codex/docs/testing-report.md`。
- 独立代码审查：当前完整 A–G 代码面最终 `pass`，无未闭环 code finding；这不等于人工验收通过。

## 1. 这次交付了什么

当前代码在一个 Runtime 模块内落地了阶段 A–G 的程序合同与权威执行骨架：

- 阶段 A：Frontend/Lake 地图基线、Online 四事实状态、Session/Travel 生命周期、原生旅行诊断 UI 和两条回归脚本。
- 阶段 B：Steam OSS 接线、身份/准入、重连和 Host 退出交付/ACK 的程序路径。
- 阶段 C：Character-owned ASC、属性、输入桥、状态与装备组件、原生生存状态视图。
- 阶段 D：Run phase/命令/公开复制合同、StateTree Task/Condition、环境 Provider、水域聚合与 teardown。
- 阶段 E：钓鱼会话、巨鱼 HookedFight 协作、NearShore 首个合法抄中者得鱼、容器 FastArray、捕获与献祭事务。
- 阶段 F：鱼目录、本轮印记候选、全参与者 CapturePlan、durable Grant 与本地 Profile/SaveGame 合同。
- 阶段 G：装备、状态恢复、营地共享缸/篝火/救援、偷鱼/恶作剧/求助/保护牌等权威服务。

正式数值、键位和美术/玩法资产没有被 AI 擅自猜测。缺失时相关入口会拒绝命令或记录 startup failure，而不是伪造一个“能玩”的默认版本。

## 2. 当前能看到的效果

### 已有工程效果

- 打开项目默认进入 `Frontend`。
- Frontend 原生 Online 面板包含 `Host Session`、`Find Sessions`、`Join First Result`、`Join Accepted Invite`、`Leave Session`，并展示 World/Session/Role/Transport/Operation/结果/邀请/错误。
- 在验证专用 Null OSS 安全副本中，Host→Lake→Leave→Frontend 已连续两轮完成；缺失 Lake 时会补偿销毁 Session 并允许新请求重试。
- Lake 在缺少正式 Run/Environment 配置时会明确 fail-closed，不会静默启动一套假的日循环。

### 当前不能验收的产品效果

- 主项目使用 Steam，但 `CatOnlineSettings` 的 SessionAccess、重连、隐私和 Host ACK timeout 仍未定，不能把 Null OSS 回归等价成 Steam 通过。
- 没有正式输入资产和 Character/GAS 数值，不能验收操作手感与状态反馈。
- 没有 `ST_RunFlow`、`ST_FishingSession`、鱼/装备 DataAsset、水域/营地 Actor，不能验收完整 Run、钓鱼、献祭、营地和社交玩法。
- Profile 持久化与外部印记桥默认关闭，不能验收跨局存档和真实照片文件。

## 3. 冷启动人工验收前置清单

在第一次人工验收前，由产品/设计/程序负责人逐项确认。任一“必须”项缺失时，不继续宣称对应阶段通过。

| 前置项 | 当前事实 | 解除方式 | 严重度 |
|---|---|---|---|
| UE 版本 | `.uproject` 指向 5.8 | 使用 UE 5.8 打开，不允许自动降级工程格式 | 必须 |
| Online 产品策略 | 主项目 Steam 已启用；SessionAccess 等未决 | 在正式 `CatOnlineSettings` 中给出会话可见性、重连白名单/TTL、隐私和 Host ACK timeout | 必须（B） |
| Steam 环境 | 未提供双账号/双客户端证据 | 准备两个有效 Steam 账号、相同构建、可互相联机的机器或隔离客户端 | 必须（B） |
| Character/GAS | runtime gate 关闭，属性和输入资产未设置 | 决定复制策略，填五项初始属性，绑定 InputAction/MappingContext，启用 Lake 状态 View | 必须（C） |
| Run/Environment | `ST_RunFlow` 缺失；Run/Environment gate 关闭 | 提供正式 StateTree、日/额度/人数/结算规则、天气与事件配置 | 必须（D） |
| Fishing/Items | `ST_FishingSession`、FishDefinition 缺失；窗口/距离/容量为 0 | 提供 StateTree、鱼表、重量/环境筛选、响应窗口、reach、容器容量 | 必须（E） |
| Profile/Imprint | Catalog 空，持久化与外部桥关闭 | 决定 SaveSlot、照片格式/容量/原子写盘/损坏恢复，并接真实成像桥 | 必须（F） |
| Equipment/Camp/Social | definitions 为空；Camp/Social gate 关闭；Lake 无对应 Actor | 提供 EquipmentDefinition、Camp/WaterRegion/共享缸/保护牌场景装配及权限/范围/冷却 | 必须（G） |

## 4. 冷启动步骤

### 4.1 第一次打开工程

| 步骤 | 人工操作 | 预期结果 | 通过标准 | 失败与留证 |
|---|---|---|---|---|
| COLD-01 | 记录当前 commit/工作树状态，确认不把 `Saved/Verification/.../runtime_safe_null*` 当主工程 | 主项目仍使用 `Config/DefaultEngine.ini` 的 Steam 配置 | 主工程未被测试副本覆盖 | 截图工作区路径与配置；停止后续操作 |
| COLD-02 | 用 UE 5.8 打开 `Catfishing.uproject` | 工程加载，默认打开 `/Game/Catfishing/Maps/Frontend` | 无模块缺失、地图缺失或致命弹窗 | 保存 Editor 日志和弹窗截图 |
| COLD-03 | 在 Content Browser 搜索 `Frontend`、`Lake` | 两张地图均存在 | 两个资产可打开，World Settings 指向对应 GameMode | 记录资产路径与 World Settings 截图 |
| COLD-04 | 运行一次可见 PIE，但先不点击 Online 按钮 | Frontend 出现且只出现一个原生 Online 面板 | 五个按钮、四事实状态和错误栏可读；无重复 Widget | 截图全屏、记录 Widget 数量和 Output Log |
| COLD-05 | 结束 PIE，再次 PIE | 旧 Widget 被移除，新会话只创建一个 | 第二次仍只有一个面板，无 stale delegate/崩溃 | 保存两次 PIE 的日志片段 |

### 4.2 当前未配置主项目的预期门禁

当前只有 GATE-01 和 GATE-02 可立即执行，用来确认 Online 与 Run/Environment 的 fail-closed；它们不是产品联机或玩法验收。GATE-03 当前是验收断点：C–G 缺正式输入、资产和场景入口，人类没有合法操作路径，必须先补齐前置项再执行。

| 步骤 | 人工操作 | 当前预期 | 通过标准 | 失败标准 |
|---|---|---|---|---|
| GATE-01 | 在未补 `CatOnlineSettings` 的主项目点击 `Host Session` | 请求被明确拒绝，并显示策略未决/无效配置类错误；不应旅行到 Lake | 无 Session 残留、Operation 回 Idle、错误可读 | 反而成功开房、卡 pending、崩溃或状态互相矛盾 |
| GATE-02 | 直接打开 Lake 并 PIE | Environment/Run startup 明确失败，玩法命令不接受 | 日志出现可定位的 fail-closed 原因，无伪天气/伪 FSM | 使用默认 0 值仍进入“正常 Run” |
| GATE-03（当前断点） | 当前不执行；记录 C–G 缺正式输入、StateTree、DataAsset 与场景 Actor，因而没有合法人工入口 | 该项保持 blocked，不用控制台、临时代码或默认 0 值旁路门禁 | 缺口与所需资产记录完整，补齐后转入 4.4–4.8 | 在没有正式入口时仍声称已人工验证 C–G，或为执行验收而临时伪造产品事实 |

### 4.3 正式 Steam 双账号验收（补齐 Online 设置后）

| 步骤 | 人工操作 | 预期结果 | 通过标准 | 失败与留证 |
|---|---|---|---|---|
| B-01 | 账号 A 在 Frontend 点击 `Host Session` | 创建 Listen Session 并旅行 Lake | A 显示 `Host / Lake / Connected / None` | 记录四事实、RequestId、epoch、Steam 日志 |
| B-02 | 账号 B 点击 `Find Sessions`，再点 `Join First Result` | 找到兼容 Session 并进入同一 Lake | B 显示 `Client / Lake / Connected / None`；A 能看到 B | 保存两端日志与画面 |
| B-03 | A 接受/发送邀请，B 走 `Join Accepted Invite` | invite handle 只消费一次 | 重复接受不重复 Join；错误明确 | 记录邀请 ID/handle 与两端终态 |
| B-04 | B 在 Session 已运行后加入 | 通过 PreLogin/PostLogin 准入并同步公开状态 | StableNetId 稳定，Run/装备/容器公开快照一致 | 留两端 StableNetId 与 revision 日志 |
| B-05 | 人为断开 B 后在 TTL 内重连，再在 TTL 外重连 | 白名单/TTL 按正式策略生效 | TTL 内恢复合法身份；TTL 外明确拒绝 | 记录断线时间、重连时间和拒绝原因 |
| B-06 | B 点击 Leave | B 回 Frontend；A Session 保持 | B 为 `NoSession / Frontend / Idle`，A 不被错误销毁 | 保存两端四事实 |
| B-07 | A 在多人局点击 Leave | 先关玩法命令、交付/等待 ACK，再 Destroy Session | A/B 最终均回 Frontend；超时分支符合产品策略 | 保存 teardown、Grant ACK、timeout、Destroy 日志 |
| B-08 | 重复点击 Host/Leave/Join | active request 期间只返回 pending 拒绝 | 不出现第二条 OSS 操作或旧回调回写 | 保存 RequestId/epoch 序列 |

### 4.4 C：角色、GAS、状态和输入

| 步骤 | 人工操作 | 预期结果 | 通过标准 | 失败标准 |
|---|---|---|---|---|
| C-01 | Host 与 Client 分别生成/占有角色 | ASC actor info 在服务器和拥有客户端正确初始化 | 属性初值一致，非拥有客户端只看到允许的公开状态 | 重复 grant、属性为默认 0、权限泄露 |
| C-02 | 触发正式输入动作与诊断能力 | Enhanced Input 只绑定一次，能力按策略执行 | 重新占有/重生后无重复绑定或残留句柄 | 一次按键触发多次、旧 Pawn 仍响应 |
| C-03 | 制造湿身、低体力、倒地、草药/进食/营地恢复 | 状态转换由服务器权威，UI 随复制更新 | 距离、身份、修订号和幂等校验均生效 | 客户端可直接改状态；远距离救援成功 |

### 4.5 D：Run 与 Environment

| 步骤 | 人工操作 | 预期结果 | 通过标准 | 失败标准 |
|---|---|---|---|---|
| D-01 | 从新局开始，按正式 StateTree 走 Morning→Day→Night→Settlement | phase 只能由 StateTree 合法转换 | phase/revision 单调，非法跳转被拒 | C++ fallback FSM 与 StateTree 双写 |
| D-02 | 在 Day 提交配额，Night 逐人 Ready | 命令带 StableNetId/RequestId，重复请求回放同一终态 | 配额不重复累加；首个合法转换只发生一次 | 重复计数、客户端越权、夜晚计时器自行跳转 |
| D-03 | 改天气/水域聚合并跨客户端观察 | Provider 输出稳定 EnvironmentSnapshot | 相同 revision 下结果一致，无缺省伪天气 | 客户端各算各的或 0 值被当正常 |
| D-04 | 结束 Run/Host 退出 | Fishing/Items/Social/Imprint 关闭命令并等待交付 | teardown 有界、可重试、无未处理 reservation/grant | Session 先销毁导致交付丢失 |

### 4.6 E：Fishing、Items 与 Sacrifice

| 步骤 | 人工操作 | 预期结果 | 通过标准 | 失败标准 |
|---|---|---|---|---|
| E-01 | 开始普通鱼会话并完成 StateTree | 只有合法角色/位置可开局；会话终态释放 active 缓存 | 一次捕获只产生一个 fish instance | 同角色并发多会话或终态后仍锁死 |
| E-02 | 两人参与巨鱼 HookedFight | 助力只在 HookedFight 生效，并使用开始时能力快照 | 参与者计划完整，能力不会中途漂移 | 在 NearShore 要求“双人抄网”或漏参与者 |
| E-03 | NearShore 两人近同时抄网 | 首个合法抄中者得鱼，后续请求得到同一终态/明确失败 | 只提交一次 Capture，只有一个 owner | 双人都得鱼、旧“双人抄网”复活 |
| E-04 | 装满鱼袋/共享缸后再捕获或转移 | 容量、所有权、距离和 revision 全部校验 | 部分失败不产生半条鱼；FastArray 快照一致 | 容器超容、客户端伪造 owner/revision |
| E-05 | 对同一鱼重复献祭并模拟中途失败 | Reserve→Commit/Cancel 两阶段事务幂等 | 鱼最多消费一次，贡献最多记一次 | 鱼丢失但贡献未记，或贡献重复 |

### 4.7 F：Profile 与 Imprint

| 步骤 | 人工操作 | 预期结果 | 通过标准 | 失败标准 |
|---|---|---|---|---|
| F-01 | 捕获一条配置了印记事件的鱼 | 所有合法参与者先完整创建 CapturePlan，再执行单人交付 | batch 要么完整准备，要么无部分计划 | 只给部分参与者计划 |
| F-02 | 成像成功/失败/重复上报 | terminal 先落稳定记录，再派发 durable Grant | 重复上报/重启后不重复 grant | 图片失败仍伪装成功；grant 先于终态落盘 |
| F-03 | ACK 丢失、重连、退出再进 | pending journal 重投直到 ACK | 同一 GrantId 只合并一次，ACK 后清除 | 未 ACK 就丢；重连重复收藏 |
| F-04 | 重启 Editor/游戏、损坏存档、达到图片容量 | SaveGame schema、恢复和外部文件策略按产品规则执行 | 数据可恢复或明确拒绝，无静默清空 | 嵌套 SaveGame 丢字段、文件半写 |

### 4.8 G：Equipment、Camp 与 Social

| 步骤 | 人工操作 | 预期结果 | 通过标准 | 失败标准 |
|---|---|---|---|---|
| G-01 | 配装、消耗品使用、鱼竿耐久与营地维修 | 服务器验证 unlock、库存、revision 和营地距离 | 客户端无权解锁；重复请求幂等 | 未解锁装备可用、远程维修 |
| G-02 | 救援倒地队友、用草药、向共享缸转鱼 | Camp 范围和角色距离分别权威校验 | 远距离/跨营地拒绝，成功后快照一致 | 仅靠客户端指针通过距离 |
| G-03 | 触发营火回放 | 先为全部参与者准备 CapturePlan，再播放/交付 | batch 无部分成功；重复请求终态一致 | 一半玩家有计划、一半没有 |
| G-04 | 偷鱼并由受害者抓获 | TheftProtocolId 端到端返回，Catch RPC 绑定同一协议 | 距离/权限/窗口均权威；结果只终结一次 | 客户端猜 ID、远距离偷鱼、重复结算 |
| G-05 | 恶作剧、求助、保护牌 | 团队/能力/距离/冷却按统一 gate 生效 | teardown 时所有 active protocol 收口 | 非队友命中、冷却绕过、残留 timer |

## 5. 通过、失败与中断标准

### 可以标记通过

- 每个步骤都有可复查的画面或两端日志，而不是只凭“看起来正常”。
- 命令成功与失败都能对应 RequestId、StableNetId、revision/epoch 和明确终态。
- 多人结果以服务器权威为准，客户端不能通过 RPC 参数绕过距离、身份、解锁或团队规则。
- 旅行、捕获、献祭、Grant、偷鱼等重复请求不产生重复副作用。
- Host 退出、EndPIE、角色离开和 Session 销毁后没有残留 Widget、timer、delegate、reservation、protocol 或进程。

### 必须标记失败

- 任一正式入口在配置/资产缺失时仍返回成功。
- B–G 任一正常路径只能靠临时 C++ fallback、手改默认 0 值或跳过 StateTree/DataAsset 才能运行。
- 只验证 Host 单机，却把结果写成 Steam 双账号、多客户端或完整产品通过。
- 旧“双人抄网”规则出现；巨鱼协作扩散到 NearShore；首个合法抄中者不能唯一获得鱼。
- 发生重复鱼、重复贡献、重复 Grant、部分 CapturePlan、跨玩家 Profile 泄露或客户端越权。

### 必须中断验收

- 正式设置、资产或测试账号尚未提供。
- 使用的不是 UE 5.8 或不是同一冻结代码。
- 主项目被 Null OSS 安全副本覆盖，或验证过程开始改写主资产。
- 两端构建、配置或 Fish/Equipment Catalog 不一致。

## 6. 仍需补齐的验收证据

1. 一次完整可见 PIE 录像：Frontend→Lake→完整 Run→Fishing→Sacrifice→Settlement→Frontend。
2. 双账号 Steam 两端日志与画面，覆盖 Create/Find/Join/Invite/重连/Host 退出。
3. 1/2/4/8 人矩阵，至少包含普通鱼与巨鱼、NearShore 竞态、共享缸和营火 batch。
4. Profile 重启/损坏/重复 Grant/ACK 超时与真实图片桥证据。
5. Cook/Package 后的 Development/Shipping 冒烟证据。

## 7. 人工反馈记录模板

| 字段 | 填写内容 |
|---|---|
| 验收日期与人员 |  |
| commit / 工作树快照 |  |
| UE 版本与构建配置 |  |
| 账号/客户端数量 |  |
| 已装配的正式设置和资产版本 |  |
| 执行的步骤编号 |  |
| 通过项 |  |
| 失败项 |  |
| 首个失败时间点 |  |
| RequestId / StableNetId / epoch / revision |  |
| 两端日志路径 |  |
| 截图或录像路径 |  |
| 是否可稳定复现 |  |
| 复现步骤 |  |
| 期望结果与实际结果 |  |
| 是否存在存档/资产污染 |  |
| 处理决定 | 继续 / 阻断 / 回滚测试配置 / 提交程序修复 |

## 8. 当前验收结论

阶段 A 已有构建与无窗口真实运行证据，可作为后续人工验收的工程基线；当前主项目的 B–G 产品入口仍因正式事实缺失而 fail-closed。请先补齐第 3 节前置项，再按第 4 节执行。完成可见 PIE、双账号 Steam 和 B–G 正常路径之前，本报告状态保持 **待人工验收**。
