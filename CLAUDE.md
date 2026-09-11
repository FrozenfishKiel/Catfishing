# Catfishing

UE 5.8 工程。项目本地约定见 [AGENTS.md](AGENTS.md)（Git 提交信息规范、源码目录分类等）。

## 项目环境

引擎、版本、工程根、技术栈、可检查程度、验证入口的声明在
**[game-toolkit.yaml](game-toolkit.yaml)** —— 那是格式化文件，人和程序都读它，
不要在本文件里另写一份。

改它用脚本，别手抄：

```bash
python <game-toolkit 插件>/skills/layer-contracts/scripts/project_env.py check .
```

## 设计文档（Design Documents）

设计真值在飞书知识库「小猫钓鱼」（space `7670823117626870757`）。它的导出镜像
**不在本仓库**——2026-09-10 移到设计项目 `C:/Users/lzcm7/OneDrive/GameProject/CatFishing/Feishu/`（本机路径）：
docx → Markdown（带 front matter 记 revision）、sheet → CSV，`_manifest.json` 记每个节点的
token / revision / 导出时间。**镜像只读**——改内容去飞书，改完重跑镜像目录下的
`_export/export_feishu.py <space_id> <镜像目录>`。

- 九册与子页：镜像的 `GDD 系统分册/`（含「钓鱼规则」＝唯一实现规格书）
- 内容表（道具四表 / 鱼表格 / UI 表）：同目录下的 `*.csv`
- 裁决与修改记录：镜像的 `裁决同步/`
- 本地 `Knowledge/GDD/` 旧快照已删除（三册也已改名：装备与道具→道具、鱼类图鉴→鱼、
  印记图鉴→印记），一律以上面那份飞书镜像为准。

## 一条要先知道的

分析本工程时，**按「检查动作」分档，别按扩展名一刀切**：

| 能做什么 | 结论强度 |
|---|---|
| 文件是否存在、路径与命名是否合约定 | 可直接下结论 |
| C++ / Config / `Scripts/` 下的脚本 | 可做静态文本检查 |
| 资产的属性、类、引用 | 要走引擎入口（`Scripts/` 下有一批 `verify_*`），按任务决定做不做 |
| Blueprint 图逻辑、运行时行为 | **文本搜索证明不了。不得据此判定「未实现」** |

最后一行是硬要求：`Content/` 下有一千多个 `.uasset`，它们能被 Glob 找到但内容读不懂。
把「查不了」报成「未实现」会直接引发重复实现。
