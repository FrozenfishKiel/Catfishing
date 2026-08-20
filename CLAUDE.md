# Catfishing 项目约定（Claude 入口）

本项目的项目级规则与 Codex 同源，正文在同目录 `AGENTS.md`。Claude 必须把
`AGENTS.md` 当作项目本地约定的事实源完整遵守，本文件只补充 Claude 侧的入口和
执行口径，不重述、不改写、不弱化 `AGENTS.md` 的任何一条。

@AGENTS.md

## 必读入口

- 项目本地约定：`AGENTS.md`
- 项目 Harness 机器入口：`.harness/harness.json`（`required_context` 必须加载）
- 问题分析标准：`.harness/PROBLEM_ANALYSIS_STANDARD.md`
- 历史教训：`.harness/CODING_LESSONS.md`
- 需求事实源地图：`Knowledge/Requirements/PROJECT_MAP.md`（指向飞书「小猫钓鱼」）
- 长期接线规则：`Knowledge/Development/FRAMEWORK_WIRING.md`

## 禁读项

`Docs/Architecture/项目完整蓝图与开发分工.md` 是唯一禁读材料。除非用户明确要求
维护或审查它，不得读取、检索、引用、总结，也不得把它加入 Context Pack、Review
Packet、任务 prompt 或 handoff。

## 共享工程状态

本项目的 Harness、Graph 和 Context 状态与 Codex 共用同一组文件，Claude 和 Codex
随时可能交替接手：

- `.codex/state/current-harness.json`
- `.codex/state/current-graph.json`
- `.codex/state/current-context.json`
- `.codex/state/context-ledger.json`
- `.codex/docs/`（测试、验收、Review 三类文档产物）

接手前先 `check`，确认状态属于本轮任务再继续；属于上一轮任务或上一个会话时重新
`start`，不要沿用旧目标和旧边界。不得删除或重写 Codex 留下的状态与文档产物。

## 工作区保护

当前工作区带有未提交的真实开发成果（分支 `codex/current-tree-snapshot`）。接手时
先运行 `git status --short`，不要重置、恢复、删除、移动或覆盖既有改动，也不要在
用户没有明确要求时执行 `git add`、commit 或清理。

## 构建与验证

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\UnreaProjects\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReload
```

自动化测试按 `Docs/Development/自动化测试方案.md` 执行。声称完成前必须有本轮新鲜
的构建或自动化证据，不能用历史结果代替。
