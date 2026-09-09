# Knowledge/Feishu —— 飞书知识库「小猫钓鱼」的本地镜像

设计真值在飞书（space `7670823117626870757`，租户 `qcniqd0mjfwg.feishu.cn`）。这里是它的
**导出镜像，只读**：改内容去飞书，改完重跑导出脚本；不要手改这里的文件——下次导出会整体替换。

## 里面有什么

| 类型 | 落点 | 来源格式 |
|---|---|---|
| docx → Markdown | 按 wiki 路径分层；有子节点的页 = `目录/同名.md` | `docs +fetch --doc-format markdown` |
| sheet 节点 → CSV | 表所在节点的目录下 `<子表名>.csv` | `sheets +csv-get`，只取 `current_region` |
| docx 内嵌的表 → CSV | `<文档名>.embedded/<sheet_id>.csv`（内嵌表没有人起的名字，表头记在清单里） | 同上，按正文里的 `<sheet token sheet-id>` 标签 |
| bitable（Ideas / 任务进度板 / 团队周报） | **不导出**——不是设计文档 | — |

CSV 统一 UTF-8 BOM、首行表头、**行号 = 表格行号**；`[row=N]` 前缀已剥掉。

每个 Markdown 开头有 front matter 七个键：`source`（wiki 链接）、`node_token`、`obj_token`、
`revision_id`、`fetched_at`、`wiki_path`、`generated: true`。

`_manifest.json`：`tree`（全部节点）、`docs`、`sheets`、`embedded_sheets`（含 `in_doc`、`header`）、
`skipped`、`warnings`（内嵌表拿不到之类的非致命问题）、`exported_at`。数量以清单为准，本文不写死。

## 重要节点

- `GDD 系统分册/` —— 九册与子页。**「钓鱼系统/钓鱼规则.md」是唯一实现规格书**（2026-09-08 起 v1.2：
  附录撤销，缺口逐条拍完退役，裁决出处以「裁决同步/设计修改记录.md」为准）。
- `GDD 系统分册/道具/` —— 道具总表 + 鱼竿 / 鱼漂 / 窝料 / 鱼饵 四张表（内容层已经是表格）。
- `GDD 系统分册/鱼/鱼表格/` —— 「第一版.csv」为现行；「旧版-已废弃-勿引（饥饿口径）.csv」按名字对待。
- `裁决同步/` —— 三份属主页 + 设计修改记录（按日期的裁决账本）。
- `数值模拟与参数记录.md` —— 参数页。

`Knowledge/GDD/` 是 2026-08 的旧快照，三册已改名（装备与道具→道具、鱼类图鉴→鱼、印记图鉴→印记）；
以本目录为准。

## 重新导出

```bash
# 需要 lark-cli 已用公司账号登录（lark-cli auth status --json --verify）
python Knowledge/Feishu/_export/export_feishu.py 7670823117626870757 Knowledge/Feishu
```

脚本只读飞书、只写本目录。它先导到 `Feishu.building/`，**全部成功才替换**本目录并清掉上次
遗留（改名/删节点的旧文件）；任何节点失败就退出 1、本目录原样不动、半成品留在 `.building/`
供检查。飞书文档只要被保存 `revision_id` 就会推进，正文可能一字未变——比对内容时忽略
`revision_id` / `fetched_at` 两行。
