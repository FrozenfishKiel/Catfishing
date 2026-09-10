# 基线迁移

基线：D:\LocalGameProject\Unreal\Catfishing\Docs\gap-analysis\2026-09-09

改写是二级匹配的独立计数，可同时计入退步／修复／其他变化；同状态改写不计未变。

## ui与交互

未变 77／退步 0／修复 0／其他变化 0／改写 0／新增 0／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 印记与图鉴

未变 54／退步 0／修复 0／其他变化 0／改写 0／新增 0／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 商店

未变 43／退步 0／修复 0／其他变化 1／改写 0／新增 1／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.2#b0e2ceb8 | 公款余额对全队常时可见（商店.md:100、133；GaHeFr.csv 第9行 待对齐内容列） | ❌ Missing → ⚠️ Partial | ① |
| DeiSFL#r2#f77e0692 | 其余三项 Playtest 记录指标：卖／吃／献的鱼数与价值占比、高级竿连续占用时长与重复争抢次数、断竿后重新出发时间与免费 1 级竿使用率（DeiSFL.cs | — → ❌ Missing | ③ |

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 猫咪与状态

未变 61／退步 0／修复 0／其他变化 0／改写 0／新增 0／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 环境与氛围

未变 29／退步 0／修复 0／其他变化 0／改写 0／新增 0／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 联机社交

未变 50／退步 0／修复 0／其他变化 0／改写 0／新增 0／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 营地与局进程

未变 55／退步 0／修复 0／其他变化 0／改写 0／新增 0／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 道具

未变 64／退步 0／修复 0／其他变化 0／改写 0／新增 0／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 钓鱼系统-打窝聚鱼与多人

未变 74／退步 0／修复 0／其他变化 0／改写 0／新增 0／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 钓鱼系统-核心

未变 98／退步 0／修复 0／其他变化 0／改写 0／新增 0／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 鱼

未变 38／退步 0／修复 0／其他变化 0／改写 0／新增 3／消失 1

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| DKhnweGaEiPTJZkO8OScQOJenqg#r10#a8fa640c | 存在「不可食用」的鱼：咸鱼、湖心巨影（第一版.csv 第10行、第7行 吃鱼效果列；设计修改记录.md 09-08 晚⑨） | — → 🔄 Divergent | ③ |
| DKhnweGaEiPTJZkO8OScQOJenqg#r1#bc855632 | 鱼表格加 fish_id 列＝工程 FishDefinitionId，程序按此列对鱼、不按名字；改名不影响对应（第一版.csv 第1行 fish_id 列；设计 | — → ✅ Implemented | ③ |
| DKhnweGaEiPTJZkO8OScQOJenqg#r10#3c9acbd3 | 特殊鱼的投掷效果按鱼种配：咸鱼投掷击退炸毛、臭臭鱼投掷驱散并短时屏蔽靠近（第一版.csv 第10行、第11行 吃鱼效果列；鱼.md:65 稀有度行） | — → ❌ Missing | ③ |
| DKhnweGaEiPTJZkO8OScQOJenqg#r13#a8fa640c | 存在「不可食用」的鱼：咸鱼、湖心巨影（第一版.csv 第13行、第19行 吃鱼效果列；设计修改记录.md 09-08 晚⑨） | 🔄 Divergent → — | ③ |

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 沿用基线的系统

- ui与交互：D:\LocalGameProject\Unreal\Catfishing\Docs\gap-analysis\2026-09-09
- 印记与图鉴：D:\LocalGameProject\Unreal\Catfishing\Docs\gap-analysis\2026-09-09
- 猫咪与状态：D:\LocalGameProject\Unreal\Catfishing\Docs\gap-analysis\2026-09-09
- 环境与氛围：D:\LocalGameProject\Unreal\Catfishing\Docs\gap-analysis\2026-09-09
- 联机社交：D:\LocalGameProject\Unreal\Catfishing\Docs\gap-analysis\2026-09-09
- 营地与局进程：D:\LocalGameProject\Unreal\Catfishing\Docs\gap-analysis\2026-09-09
- 道具：D:\LocalGameProject\Unreal\Catfishing\Docs\gap-analysis\2026-09-09
- 钓鱼系统-打窝聚鱼与多人：D:\LocalGameProject\Unreal\Catfishing\Docs\gap-analysis\2026-09-09
- 钓鱼系统-核心：D:\LocalGameProject\Unreal\Catfishing\Docs\gap-analysis\2026-09-09

合计：未变 643／退步 0／修复 0／其他变化 1／改写 0／新增 4／消失 1；稳定率 99.8%（一级 644 ÷ 基线 645）；二级 0，新增 4，消失 1
