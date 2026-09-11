# Catfishing 框架接线入口

文档状态：当前入口。早期接线细节已被当前源码和框架文档取代。

更新时间：2026-09-09

## 读取顺序

1. 先读 `Knowledge/Framework/PROJECT_MAP.md`，确认当前源码目录、运行时真相和最短阅读顺序。
2. 再读 `Knowledge/Framework/RULES.md`、`TERMS.md`、`DECISIONS.md` 和 `KNOWN_ISSUES.md`，统一边界、术语和常见误读。
3. 涉及具体产品规则时，读 `Knowledge/Design/`（设计真值，2026-09-11 起在本仓库；先看该目录 README 的三条数据流）。
4. 涉及属性、商店、存档、UI 或联机等专题时，读 `Docs/Architecture/` 下对应当前方案，并以真实 `Source/`、`Config/`、`Content/` 和最新验证证据为准。

## 当前硬边界

- 单 Runtime 模块仍是 `Catfishing`，业务系统按 `Source/Catfishing/` 下的领域目录划分。
- `FishContainers/` 只负责实物鱼和鱼容器事务，不是泛道具系统。
- `Inventory/` 负责正式道具实例、随身背包和营地公共仓库。
- Run 供品结算由 GameMode/Run ASC 写口收口，外部系统不能直接改世界进度。
- Save 只序列化各权威系统已经成立的事实，恢复时把快照交回对应领域，不从 UI 文本或界面缓存重建玩法状态。
- UI 采用 View / Model / PageController 分层：View 只渲染和提交意图，Model 只读权威快照，Controller 负责把意图转换为正式服务器入口。
