# 知识库节点挂载现状（客观事实，space_id 7670823117626870757）

- GDD 系统分册（索引页）`C7C8wed3ti43DFkzpQicUoWNn3c`
  - **装备与道具**（docx，册正文＝gear-main.md）`VVHrw3FAui78wZkjuKZcLHBdnEc`
    - 窝料定位（docx，属主王甜甜＝wl-positioning.md）`KBaFwqpAuiiiKjkmsvecG5YtnNg`
      - 窝料/饵（sheet，4 子表：Sheet1［已清空留指路］／窝料／鱼饵／假饵（点子库·Demo不做）＝wl-table/bait-table/lure-table.txt）`TwI7w9wooi52q2k1Bi8cZ6cCnag`
    - 鱼竿耐久度规则（docx＝rod-page.md）`T2shwLMhViFWVakXwExcVXa7nvg`
    - 道具（待补充）（sheet，单表＝props-table.txt）`RBHPwKudEiwItbko3HvcFPCenom`

# 关联文档 node_token（按需自取）

- 钓鱼系统册 `ConXwh5TAi6OMVk91pJcumbBnfe`；钓鱼规则（其唯一子页/实现规格书）`BzUbwq0qRil89ykyFPNcNou5nYo`（＝fishing-rules.md）
- 鱼册 `SlFcwt3BdidWCRkz2S4cIplJnnV`；鱼表格（sheet，现行为「第一版」子表）`DKhnweGaEiPTJZkO8OScQOJenqg`
- 猫咪与状态册 `BIeHwP14vigUEBkI3UxciEionxf`；商店册 `U0JtwqFJ8ijKxakb1egcRXhvnKg`
- 世界观与美术基调页 `FsLmwaQypioUnPkA3ZUcsjNKnAd`；联机社交册 `EH0LwpHihiFR8Ukh6s6cXTqUnXc`；营地册 `EE9vwzWE2iHVXGkduzGcEx4bnjh`
- 参数与校准记录页 `Xg1EwylUuiUeVckeLwtcMXMjnAg`（＝registry.md）；裁决同步·装备与商店 `Ux79wEepvivMxKkNpvvcvbW4nfm`（＝sync-page.md）

# lark-cli 速记（只读；禁止任何写操作）

- 读文档：`lark-cli docs +fetch --doc <node_token> --doc-format markdown --as user`（输出 JSON，正文在 data.document.content）
- 列子节点：`lark-cli wiki +node-list --space-id 7670823117626870757 --parent-node-token <node_token> --as user`
- 读表格：先 `lark-cli wiki +node-get --node-token <node_token> --as user` 取 obj_token，再 `lark-cli sheets +workbook-info --spreadsheet-token <obj_token> --as user` 列子表，再 `lark-cli sheets +csv-get --spreadsheet-token <obj_token> --sheet-id <sid> --range A1:X30 --as user`（正文在 data.annotated_csv）
