# 基线迁移

基线：Docs/gap-analysis/2026-09-11

匹配顺序：完全同键 → 同文档同 h8 唯一锚点漂移 → 同文档同锚点措辞改写 → 新增／消失。
改写与锚点漂移独立计数，可同时计入状态变化；只有一级同键同状态计未变。

## ui与交互

未变 30／退步 13／修复 4／其他变化 23／改写 4／锚点漂移 0／新增 23／消失 38

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r3#58147926 | ESC 菜单，含设置、音频、操作等基础功能（Sheet1.csv 第 3 行 备注；Sheet2.csv 第 30 行 当前作用） | ✅ Implemented → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r4#8d6698fa | 左上角猫爪印图标常驻，点击打开鱼图鉴（Sheet1.csv 第 4 行 UI内容；Sheet1.csv 第 4 行 交互方式） | ❌ Missing → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r9#ee26145d | 提竿成功反馈，右下角短暂出现（Sheet1.csv 第 9 行 UI位置/场景；Sheet1.csv 第 9 行 出现条件） | ✅ Implemented → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r14#0ca88a6c | 首次解锁新鱼种时在钓点附近自动展示鱼种特写（名称、品种介绍、重量，Space 继续）（Sheet1.csv 第 14 行 出现条件；Sheet2.csv 第 2 | ❌ Missing → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r18#11a6fd1c | 左上角白天常驻显示当日任务点数与缸内可献点数（Sheet1.csv 第 18 行 UI内容；Sheet1.csv 第 18 行 出现条件） | 🔄 Divergent → ✅ Implemented | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r19#d436270e | 上方显示白天倒计时（Sheet1.csv 第 19 行 UI内容；Sheet1.csv 第 19 行 出现条件） | ❌ Missing → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r20#a1032e48 | 升级时屏幕中央三选一弹窗，每项带描述、按出现次序刷出（Sheet1.csv 第 20 行 UI内容；Sheet1.csv 第 20 行 备注） | ❌ Missing → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r21#42642913 | 准星态：有合法目标够着时显示，多个合法目标同时够着时提示当前对谁出手（Sheet1.csv 第 21 行 出现条件；Sheet1.csv 第 21 行 备注） | ⚠️ Partial → 🔄 Divergent | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r22#16dd3bb5 | 搏斗中长按收竿键时屏幕中央显示放弃进度条（Sheet1.csv 第 22 行 UI内容；Sheet1.csv 第 22 行 出现条件） | ❌ Missing → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r23#cab1c2d9 | 主钓手体力低于 5% 时的换人濒死强提示（Sheet1.csv 第 23 行 出现条件；Sheet1.csv 第 23 行 UI内容） | ❌ Missing → 🔄 Divergent | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#527a79d3 | 抛竿：鼠标左键点哪落哪、无蓄力（Sheet2.csv 第 2 行 当前作用） | ✅ Implemented → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r3#48f51f41 | 鱼儿咬钩时鼠标左键提竿（Sheet2.csv 第 3 行 当前作用） | ✅ Implemented → ❓ Unverifiable | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r4#34577fd5 | 遛鱼状态：按住鼠标左键收线、按住鼠标右键放线（Sheet2.csv 第 4 行 当前作用；Sheet2.csv 第 7 行 当前作用） | ✅ Implemented → ❓ Unverifiable | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r5#2449ff0c | 鼠标左键从鱼护取鱼、从鱼缸取鱼（Sheet2.csv 第 5 行 当前作用；Sheet2.csv 第 6 行 当前作用） | 🔄 Divergent → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r8#557e216a | 遛鱼状态 W 前移靠水、S 后退离水，钓鱼中不切相机反转（Sheet2.csv 第 8 行 当前作用；Sheet2.csv 第 9 行 当前作用） | ✅ Implemented → ❓ Unverifiable | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r11#35b348dd | 抄鱼键 F（Sheet2.csv 第 11 行 按键） | ✅ Implemented → ❓ Unverifiable | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r15#20f5d99e | E 靠近鱼护查看鱼护（Sheet2.csv 第 15 行 当前作用） | ✅ Implemented → ❓ Unverifiable | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r17#c89d92f7 | 长按 E 吃掉嘴里叼着的鱼（Sheet2.csv 第 17 行 当前作用） | ❌ Missing → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r20#cd72038e | E 商人对话确认选择（Sheet2.csv 第 20 行 当前作用） | ✅ Implemented → ❓ Unverifiable | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r21#6a5a892f | F 鱼落地后拾取（Sheet2.csv 第 21 行 当前作用） | 🔄 Divergent → ❓ Unverifiable | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r25#a85253e6 | T 查询当前鱼窝／查看鱼窝范围（Sheet2.csv 第 25 行 当前作用） | ❌ Missing → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r29#ae717058 | 鱼缸放鱼界面：Tab 进入「选择放入」、Enter 全部放入／确认当前选择、Esc 取消（Sheet2.csv 第 29 行 当前作用；Sheet2.csv 第 | ❌ Missing → ⚠️ Partial | ① |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互神像献祭#966dbfbb | 祭坛是石像前的一块地不是容器：把鱼一趟趟叼过去扔在祭坛上就算摆好，随时可叼回，直到石像被按下那一刻（交互.md:34；交互.md:35；营地.md:30） | ✅ Implemented → 🔄 Divergent | ① |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互神像献祭#1791ccff | 两个圈：摆鱼圈 2 米、到场圈 8 米（交互.md:36；数值模拟与参数记录.md:65） | ✅ Implemented → ❓ Unverifiable | ① |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互神像献祭#5e78aa23 | 世界进度平时隐藏，靠近神像或打开界面时查看（交互.md:42；Sheet1.csv 第 18 行 备注） | ⚠️ Partial → ✅ Implemented | ① |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互篝火#301d0651 | 篝火：夜晚靠近出现柔和光效提示，显示【F】坐下，坐下进入松弛状态、别的猫凑过来叠成一堆（交互.md:50；交互.md:56；营地.md:89） | ❌ Missing → ⚠️ Partial | ① |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#玩家靠近湖面#754d7ed3 | 手持鱼竿即钓鱼待机，左键点水面抛竿，不设「F 钓鱼」模式入口（交互.md:67；钓鱼规则.md:25） | ✅ Implemented → ⚠️ Partial | ① |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互换饵与窝料#27abf6a9 | 搏斗中不能掏窝料；其他人可以补窝（交互.md:90；钓鱼规则.md:45） | 🔄 Divergent → ✅ Implemented | ① |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互抛竿与投窝料的落点#725f695a | 抛竿与投窝料的落点：点哪落哪，无蓄力（交互.md:95；钓鱼规则.md:43；钓鱼规则.md:121） | 🔄 Divergent → ⚠️ Partial | ① |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互抛竿与投窝料的落点#f8677842 | 窝料落岸成掉落物，可捡回（交互.md:97；钓鱼规则.md:43） | ❌ Missing → 🔄 Divergent | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r5#5a17c5d0 | 鱼可抄时屏幕中央爪印图标＋「快抄！（按 F）」，以收鱼资格判定为准（提示文案.csv 第 5 行 提示或表现） | ❌ Missing → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r6#a7f30189 | 猫体力偏低时体力条变红闪烁＋「猫喘气了！」与喘气表现（提示文案.csv 第 6 行 提示或表现） | ❌ Missing → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r7#e25caba1 | 落水危险时爪扒地划痕＋「要滑下去了！」（提示文案.csv 第 7 行 提示或表现） | ❌ Missing → ❓ Unverifiable | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r8#57cbbe52 | 完美中鱼：金色水花＋瞳孔放大＋清脆一声「叮」＋「完美！」（提示文案.csv 第 8 行 提示或表现） | ❌ Missing → ❓ Unverifiable | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r9#0e9e6950 | 断竿：咔嚓声、爪里只剩半截竿、猫愣住（提示文案.csv 第 9 行 提示或表现） | ⚠️ Partial → ❓ Unverifiable | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r10#311319cc | 鱼逃：耳朵耷拉、水面恢复平静（提示文案.csv 第 10 行 提示或表现） | ❌ Missing → ❓ Unverifiable | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r13#9296bfd0 | 碾压：鱼落身后草地、猫翘尾巴，仍需拾取（提示文案.csv 第 13 行 提示或表现；设计修改记录.md:323） | ❌ Missing → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r12#9d6d65ab | E 换人：主钓手按 E 发起请求、再按 E 取消；替补按 E 接手，不设体力准入（Sheet2.csv 第 12 行 当前作用；设计修改记录.md:396） | ❌ Missing → 🔄 Divergent | ② 措辞改写 |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r17#6c97f3df | 钓鱼时常驻显示鱼竿耐久，手持鱼竿时显示（Sheet1.csv 第 17 行 出现条件；Sheet1.csv 第 17 行 UI内容） | 🔄 Divergent → ✅ Implemented | ② 措辞改写 |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r11#1867f6e8 | 遛鱼状态显示玩家体力（Sheet1.csv 第 11 行 UI内容；Sheet1.csv 第 11 行 出现条件） | ✅ Implemented → ⚠️ Partial | ② 措辞改写 |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互神像献祭#e5378593 | 与石像互动＝结算＋翻天：全员到齐，倒地豁免、已离开不计；短倒计时中离圈中断，完成后鱼一起消失并进入翻天过场（交互.md:35；营地.md:141） | ✅ Implemented → ✅ Implemented | ② 措辞改写 |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r27#410a4cd8 | 退出游戏二次确认弹窗，Esc 取消／Enter 确认退出（Sheet2.csv 第 27 行 当前作用；Sheet2.csv 第 32 行 当前作用） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#685f0cf4 | 房间人数上限 4（设计修改记录.md:255） | — → ✅ Implemented | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r28#ec994152 | 设置页底部快捷键：R 恢复默认、Enter 应用、Esc 返回（Sheet2.csv 第 28 行 当前作用；Sheet2.csv 第 33 行 当前作用；Sh | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r12#63d9ed04 | 主动放弃时给出放生反馈，文案按鱼饵损失、浮漂不消耗的现行口径（提示文案.csv 第 12 行 提示或表现；设计修改记录.md:287） | — → ❓ Unverifiable | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r30#41a6e5c4 | 派对菜单项：钓鱼图鉴入口（Sheet2.csv 第 30 行 当前作用） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r3#f180a01d | ESC 菜单内显示第 N 天（Sheet1.csv 第 3 行 UI内容） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#49d1a08b | 正式 HUD 布局：左上天数与任务/缸内点数、左下旅行包、右下提竿反馈与其下环形倒计时、竿旁或下方耐久（Sheet1.csv 第 2 行 UI位置/场景；She | — → ❓ Unverifiable | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r17#a0e479ec | 竿耐久采用三档 40／70／120，上限与当前值分明，归零即断竿（Sheet1.csv 第 17 行 备注） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r19#bdf0fe73 | 窝点鱼量不进 HUD：全队可见猫爪标记读 Total 浓／中／淡三档，聚鱼变亮加密，不显示存量或倒计时（Sheet1.csv 第 19 行 备注；钓鱼规则.md | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r39#ae044023 | Shift + Tab 在 Steam 房间／组队管理打开 Steam 界面（Sheet2.csv 第 39 行 当前作用） | — → ❓ Unverifiable | ③ |
| Sisrw4FzWiL2AekXetFc7Vxunqg#1#e5a152ae | 三选一 11 档均能当场生效，显示描述对应实际效果，按出现次序入池（升级效果.md:19；升级效果.md:25；设计修改记录.md:389） | — → ⚠️ Partial | ③ |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#4#4cb17ea4 | 黄色体力在条末端显示并在搏斗绿段耗尽后可花；绿空黄不空时不清零力量（数值成长.md:50；设计修改记录.md:379） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#a1dcf588 | 无人值守的竿可以被接管续钓，不设体力准入（设计修改记录.md:400） | — → 🔄 Divergent | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#5c9dc959 | 祭坛倒计时 3 秒，给反悔留窗口并作为翻天演出起手式（设计修改记录.md:327；数值模拟与参数记录.md:44） | — → ⚠️ Partial | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互篝火#e628cf78 | 篝火入夜即自动点亮；玩家主动去，坐不坐都不影响翻天（交互.md:58；交互.md:59；营地.md:124） | — → ✅ Implemented | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#归交互层的待定项#cedd232f | 三选一选择演出：三条小鱼干选叼哪条（交互.md:113；升级效果.md:21） | — → ❓ Unverifiable | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#归交互层的待定项#81b3232a | 吃鱼瞬时浮层显示体力含黄段增量、经验槽与 +N，认识的鱼显示效果、未认识显示「？？？」（交互.md:110；数值成长.md:63） | — → ⚠️ Partial | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#归交互层的待定项#b0e2ceb8 | 公款余额对全队常时可见（交互.md:114；商店.md:133） | — → ✅ Implemented | ③ |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#6#85f6cd26 | 主动查看面板承载黄色体力精确存量、已叠 build 构成、各 buff 剩余时间（数值成长.md:65） | — → ⚠️ Partial | ③ |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#6#7c90d2ef | 翻鱼护／鱼缸看每条鱼时，同屏显示经验、供奉、售价三个原始值，不给换算结论（数值成长.md:66） | — → ⚠️ Partial | ③ |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#6#6b39c5cf | 三选一卡面显示每项已叠次数（数值成长.md:64） | — → ❓ Unverifiable | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#归交互层的待定项#a2e7a883 | 线长余量给世界内表现：线轮上还剩几圈；力量差靠前滑、爪扒地和线绷紧读，不给数字（交互.md:119；钓鱼规则.md:376） | — → ❓ Unverifiable | ③ |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#2#16109396 | 三选一面板可挂起且无超时，开着仍可行动，不冻结世界、不阻挡其他玩家，连满时逐个呈现（数值成长.md:36） | — → ⚠️ Partial | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#5f3a0871 | 前端主菜单四项入口：开始游戏／加入队伍／设置／退出游戏（主界面.md:33，revision_id 353） | ✅ Implemented → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#410a4cd8 | 退出游戏二次确认弹窗，Esc 取消／Enter 确认退出（主界面.md:17；Sheet2.csv 第27行 当前作用、第32行 当前作用） | ⚠️ Partial → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#6f8f9c6d | 「加入游戏」页：好友房间列表＋输入邀请码两种入口（主界面.md:21） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#c6ea619d | 房间页：房间名、房间 ID（邀请码）、成员列表、空位、离开房间、开始游戏（主界面.md:25,45） | ✅ Implemented → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#685f0cf4 | 房间人数上限 4（主界面.md:49「房间状态显示为4/4」；09-08 裁决「单局上限锁死 4 人」） | ✅ Implemented → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#c09de38f | 房间内玩家「已准备」状态，全员准备后才能开始游戏（主界面.md:25,45） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#eb8823f4 | 邀请好友：Steam 好友列表＋在线／游戏中／离线三态＋邀请按钮（主界面.md:47） | ✅ Implemented → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#265f2bdd | 复制房间 ID／邀请码（主界面.md:45,49） | ✅ Implemented → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#e94e354d | 房间设置：编辑房间名称、设置房间密码、改人数上限、开启语音聊天、保存设置（主界面.md:49） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#b785caad | 房间底部「语音聊天」入口（主界面.md:25,45） | ⚠️ Partial → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#54dda21a | 房间底部「查看玩家信息」入口（主界面.md:25） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#7c5c439a | 选择存档页用左右键切换存档（主界面.md:37） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#d187d278 | 存档条目显示第 N 天、地点、游戏时长（主界面.md:41） | ⚠️ Partial → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#d7ab72e1 | 设置页四个选项卡：游戏／画面／声音／控制（主界面.md:61,63,65,89） | ✅ Implemented → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#6d2788d4 | 「辅助功能」选项卡及六项：界面缩放、文字大小、高对比度界面、色觉模式、减少镜头晃动、减少闪光效果（主界面.md:91） | ⚠️ Partial → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#972d19f1 | 游戏设置项：游戏语言、语音聊天、语音输入模式、麦克风校准、震动（主界面.md:61） | ✅ Implemented → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#7fc7993b | 画面设置项：显示模式、分辨率、画质预设、垂直同步、亮度（主界面.md:63,85） | ✅ Implemented → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#b51e81a6 | 画面设置项：帧率上限、抗锯齿、动态模糊（主界面.md:63,85） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#21833784 | 声音设置项：总音量、音乐、音效、环境声、语音音量、输出设备、后台静音（主界面.md:65,87） | ✅ Implemented → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#3a17d66f | 控制设置项：鼠标灵敏度、镜头灵敏度、反转 Y 轴、按键设置（主界面.md:89） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#ec994152 | 设置页底部快捷键：R 恢复默认、Enter 应用、Esc 返回（主界面.md:85,91；Sheet2.csv 第28行、第33行、第37行 当前作用） | ⚠️ Partial → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#8cb063e6 | 游戏加载界面：进度条＋阶段提示文字（主界面.md:69） | ✅ Implemented → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#bfdac119 | 加载界面显示「第 n 天」与本局进度轴（09-08 已把「献祭进度」作废，改「世界进度」，另给当日任务点数与缸内可献点数）（主界面.md:69,71） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#f4ffdc9b | 他人解锁新鱼种时给同房其他玩家提示，且提示期间仍可移动、交互、继续钓鱼（主界面.md:129） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#512ce1d3 | 背包装备栏四格：鱼竿、鱼饵、冰壶（窝料）、鱼篓（鱼护）（主界面.md:97） | ❌ Missing → — | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r30#332cf411 | Esc 在游戏中打开或关闭派对菜单（Sheet2.csv 第30行） | ✅ Implemented → — | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互鱼缸鱼护取鱼#3392da6f | 小猫靠近鱼缸／鱼护进入交互范围时，镜头轻微转向目标（交互.md:17） | ❌ Missing → — | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互鱼缸鱼护取鱼#27952e02 | 可交互物体出现淡淡描边高亮效果（交互.md:18） | ⚠️ Partial → — | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互鱼缸鱼护取鱼#fc9cf3b5 | 靠近鱼缸／鱼护显示提示「【F】取出鱼」（交互.md:19-21） | ⚠️ Partial → — | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互鱼缸鱼护取鱼#a33f7366 | 按下取鱼键后播放取鱼动作、镜头推进到鱼的特写（交互.md:23） | ❌ Missing → — | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互鱼缸鱼护取鱼#436348b6 | 取鱼后左上角显示鱼的信息（名称、品种、重量）（交互.md:24） | 🔄 Divergent → — | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#玩家靠近湖面#c82c3404 | 玩家靠近湖面时：附近有窝则屏幕右下角显示当前区域窝点信息，附近无窝则不显示任何提示（交互.md:70-77） | ❌ Missing → — | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r12#a087e061 | 主动放弃：「放生了鱼…鱼饵和浮漂被带走了」（提示文案.csv 第12行） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#9e908b03 | 派对菜单项：派对设置（主界面.md:79） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#a47cf01a | 派对菜单项：组队管理（队伍 x/4、等待队列、Steam 好友邀请、仅好友／仅邀请、复制邀请链接）（主界面.md:79,81） | ⚠️ Partial → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#c24742b8 | 派对菜单项：申请暂停，中途暂停需经房主同意（主界面.md:77,79） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#41a6e5c4 | 派对菜单项：钓鱼图鉴入口（主界面.md:79；Sheet2.csv 第30行「图鉴入口之一，见图鉴册」） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#d837d35e | 鱼缸／鱼护界面的「放掉（放生）」操作（主界面.md:163「放掉／取鱼／放入鱼缸」三个选项） | ❌ Missing → — | ③ |

### Code-only mechanics

描述差异、待核机制变化：以下仅对比文字，由复核／综合核源码与属主裁决后确认。
| 描述来源 | 机制描述 | code_ref |
|---|---|---|
| 本轮独有描述 | 首钓揭示 8 秒自动关闭，并接受 Enter 关闭 | Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp:53；Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp:382；Source/Catfishing/UI/Collection/CatFishRevealWidget.cpp:74 |
| 本轮独有描述 | 外部容器复用通用库存：右键 Use 吃鱼、单击选择后点 Carry、独立 Place/Drop | Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.cpp:112；Source/Catfishing/UI/Inventory/CatInventoryWidget.cpp:241 |
| 本轮独有描述 | 商店将交互键与背包键也当关闭键 | Source/Catfishing/UI/Shop/CatShopWidget.cpp:786 |
| 本轮独有描述 | 辅助功能/鼠标灵敏度/相机灵敏度/Y 反转偏好可保存，保存被视为生效 | Source/Catfishing/UI/Frontend/CatFrontendSettingsModel.cpp:739；Source/Catfishing/Settings/CatGameUserSettings.cpp:408；Source/Catfishing/Settings/CatGameUserSettings.cpp:421；Source/Catfishing/Framework/Game/CatfishingPlayerController.cpp:540 |
| 本轮独有描述 | 手动求助有独立 6 秒冷却 | Source/Catfishing/Social/CatSocialService.cpp:210；Config/DefaultGame.ini:391 |
| 本轮独有描述 | 无人值守竿可由附近玩家切线，未限制为原持竿者/竿主人 | Source/Catfishing/Fishing/Integration/CatFishingCommandComponent.cpp:1122；Source/Catfishing/Fishing/CatFishingService.cpp:1594 |
| 本轮独有描述 | 前端仍有“加入派对”菜单项，但请求直接提示尚未开放并返回菜单 | Source/Catfishing/UI/Frontend/CatFrontendRootWidget.cpp:439；Source/Catfishing/UI/Frontend/CatFrontendPageController.cpp:95 |
| 基线独有描述 | 世界信息牌框架：任意 Actor 挂 `UCatWorldInfoComponent` 即可公开只读信息行，三档策略（NearbyFull／NearbySummary／FocusOnly）＋距离＋遮挡＋优先级，本地 Controller 统一创建正式 WBP | Source/Catfishing/UI/WorldInfo/CatWorldInfoComponent.h:12-63；CatWorldInfoController.cpp:81-140；CatWorldInfoTypes.h:26-63 |
| 基线独有描述 | 物品悬停提示框：背包格鼠标悬停时由 LocalPlayer 唯一 TooltipController 显示名称／描述，鱼加「重量：x kg」、鱼竿加「耐久：x / y（已断裂）」 | Source/Catfishing/UI/ItemTooltip/CatItemTooltipModel.cpp:11-49；CatItemTooltipController.h:12-46；Source/Catfishing/UI/CatUISettings.h:46-47 |
| 基线独有描述 | 翻天遮罩与献祭结果界面：黑幕三段时间轴＋「第 {n} 天」标题＋「献祭 {x}/{y} 点 · 达标/未达标 / 世界进度 {a}% → {b}%」结算摘要，锁定期吞键鼠并接管焦点 | Source/Catfishing/UI/Run/CatDayTransitionWidget.cpp:14-79；Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp:269-308；Config/DefaultGame.ini:16 |
| 基线独有描述 | 物理抓握 HUD 提示：`bShowPhysicalControls`／左右爪伸手与抓握状态／主控身份，文案「主控 · 左键抛竿 / 收线 · 右键放线 · R 放竿」与「按住左 / 右键抓人或抓竿 · WASD 拉动 · 松键释放」 | Source/Catfishing/UI/HUD/CatHUDModel.cpp:143-164；Source/Catfishing/UI/HUD/CatHUDWidget.h:78-86,259-262 |
| 基线独有描述 | 鱼竿上竿／离竿操作位（RodInteract 一次性输入 → PlaceRod／OperateRod） | Source/Catfishing/AbilitySystem/Tags/CatFishingAbilityTags.cpp:5,11；Source/Catfishing/AbilitySystem/Fishing/InputAbilities/CatFishingRodInteractAbility.h:7-19；Source/Catfishing/Fishing/Integration/CatFishingCommandComponent.cpp:958-976 |
| 基线独有描述 | 背包 Drop／Place 两种离库动作＋堆叠数量确认面板（同页 SpinBox，冻结实例 ID 后再提交） | Source/Catfishing/UI/Inventory/CatInventoryWidget.h:49-55,129-147,197-226；Source/Catfishing/Inventory/CatInventoryStatics.h:16-21 |
| 基线独有描述 | 鱼护页的单条／全部售鱼入口与预估价显示（按附近买家 0.1 秒节流刷新，地面鱼护才显示） | Source/Catfishing/UI/Inventory/CatFishGuardInventoryWidget.h:46-76；Source/Catfishing/ShopEconomy/CatFishBuyerActor.h:25 |
| 基线独有描述 | 长按通用交互键 0.5 秒＝拾起鱼护，短按＝开护（同一输入两种动作，由 `GuardHoldSeconds` 分流） | Source/Catfishing/Interaction/CatInteractionTargetingComponent.h:55-63；CatInteractionTargetingComponent.cpp:149-185 |

## 印记与图鉴

未变 35／退步 2／修复 6／其他变化 10／改写 0／锚点漂移 0／新增 10／消失 4

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| EPi4wjG5IigRTwkH8stcGWoUnUh#1#87a3873f | 各册抛出的「印记事件」有统一投递口，本册只接收、成像、入册，不定义玩法（印记.md:69、印记.md:169） | ✅ Implemented → ⚠️ Partial | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#8#d47b900b | 首钓新鱼种抛印记（举鱼定格＋图鉴新页角标）（图鉴.md:149、印记.md:75） | 🔄 Divergent → ⚠️ Partial | ① |
| EPi4wjG5IigRTwkH8stcGWoUnUh#3#d7273b28 | 局内相册按时间线排本局印记，等待期随手可翻（印记.md:112、印记.md:56） | ❌ Missing → ⚠️ Partial | ① |
| EPi4wjG5IigRTwkH8stcGWoUnUh#4#ccb85ceb | 单人局照样成立：一只猫守着篝火翻自己的相纸（印记.md:124） | ✅ Implemented → ⚠️ Partial | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#2#e9e221e7 | 局内图鉴＝营地里的公共板子：全队钓到自动上页、盖登记归属爪印、只增不减、不外流、局末随营地清空（图鉴.md:68） | ❌ Missing → ⚠️ Partial | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#3#f7d907b3 | 鱼的三层解锁：线索层／收集层／知识层（图鉴.md:86） | 🔄 Divergent → ✅ Implemented | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#3#cb11407e | 线索层触发：上钩成立就揭剪影，之后跑掉／断竿／放弃都不回滚（图鉴.md:98、设计修改记录.md:249、设计修改记录.md:259） | ❌ Missing → 🔄 Divergent | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#3#8e3ebc46 | 线索层触发：空钓（用了饵没钓上来）揭开当时咬钩失败的那条（图鉴.md:99） | ❌ Missing → ✅ Implemented | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#5#de1ce3e2 | 首次遇上的条件回显（首次记录时冻结，后续破纪录不覆盖）（图鉴.md:132） | ⚠️ Partial → ✅ Implemented | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#3#6247a77a | 知识层：自己吃过才解锁，给吃鱼效果（限时 buff 与经验值）（图鉴.md:92） | ❌ Missing → ⚠️ Partial | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#4#280b918c | 知识层不共享：谁吃谁记，被偷走吃掉记进小偷的图鉴（图鉴.md:124） | ❌ Missing → ✅ Implemented | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#4#8602681e | 各层字段按鱼配置：没有的信息不存在、不留「待解锁」空位；不可食用的咸鱼与湖心巨影没有知识层（图鉴.md:122、设计修改记录.md:269） | ❌ Missing → ⚠️ Partial | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#4#09ffffd0 | 收集层归上钩者一人（成功收鱼时写入上钩者的图鉴）（图鉴.md:111、设计修改记录.md:259） | 🔄 Divergent → ⚠️ Partial | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#3#89dcd492 | 未解锁＝纯黑影，开局满图是影，玩家知道湖里有多少种（图鉴.md:94、图鉴.md:130） | ❌ Missing → ⚠️ Partial | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#3#dcadea86 | 已开页里没到条件的字段一律写「待解锁」，不显示灰掉的假数值（图鉴.md:84、图鉴.md:41、图鉴.md:171） | ❌ Missing → ✅ Implemented | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#3#95939dee | 分布线索呈现：剪影一揭开就摊开窝料与鱼饵偏好、出现条件、水域与时段（图鉴.md:90、图鉴.md:137） | ❌ Missing → ⚠️ Partial | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#3#57d3bf7d | 「当时看不清」是线索层成立的前提：水里的鱼影只给存在感不给答案，看不出是哪一种（图鉴.md:107） | ⚠️ Partial → ❓ Unverifiable | ① |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#35e5a13d | 跟人走三样（个人图鉴含剪影、印记相册、外观解锁清单）的存档：旧档版本不符按空档重建（设计修改记录.md:287） | 🔄 Divergent → ✅ Implemented | ① |
| EPi4wjG5IigRTwkH8stcGWoUnUh#1#12d53616 | 已拍定必进：叼着鱼的猫被另一只猫扑倒、鱼落地，双方入册（印记.md:74、印记.md:84） | — → ❌ Missing | ③ |
| EPi4wjG5IigRTwkH8stcGWoUnUh#4#465ac5db | 篝火每晚轮播：本册持轮播内容，营地提供自主停留的篝火场景（印记.md:124、营地.md:85、营地.md:152） | — → ⚠️ Partial | ③ |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#1#edb387dc | 个人图鉴跨局跟人走，与印记相册、外观解锁同列三个持久载体（图鉴.md:21、图鉴.md:166） | — → ✅ Implemented | ③ |
| EPi4wjG5IigRTwkH8stcGWoUnUh#3#c5d40dbc | 翻阅相册本身不自产印记，避免拍“看照片”递归（印记.md:118、印记.md:154） | — → ✅ Implemented | ③ |
| EPi4wjG5IigRTwkH8stcGWoUnUh#2#20258880 | 相纸质感贯穿成像与翻阅，入口和篝火回看重猫演出，长相册允许轻量网格（印记.md:100、印记.md:116、印记.md:158） | — → ❓ Unverifiable | ③ |
| EPi4wjG5IigRTwkH8stcGWoUnUh#6#2a6ffae4 | 越老的局，相纸越卷边泛旧，时间感不依赖显示日期（印记.md:152） | — → ❓ Unverifiable | ③ |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#10#8c8a0f18 | RC 图鉴开局能容纳拍定的 20 种鱼黑影（图鉴.md:229） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#caf83de4 | 个人档案写失败不影响本局，下局开局重试（设计修改记录.md:287） | — → 🔄 Divergent | ③ |
| EPi4wjG5IigRTwkH8stcGWoUnUh#1#447ba396 | 印记不展示数量／完成度，图鉴不是收集进度条（印记.md:45、图鉴.md:19） | — → 🔄 Divergent | ③ |
| EPi4wjG5IigRTwkH8stcGWoUnUh#6#1834689a | 跨局个人相册在局内、局外都能翻，局外入口具体形式待交互定（印记.md:148、印记.md:245） | — → ❌ Missing | ③ |
| EPi4wjG5IigRTwkH8stcGWoUnUh#1#1a6a6a6b | 已拍定必进：偷鱼被抓，双方入册（印记.md:74、印记.md:85） | ✅ Implemented → — | ③ |
| EPi4wjG5IigRTwkH8stcGWoUnUh#4#3c8e88b5 | 篝火每晚轮播：营地持仪式流程，本册持轮播内容（印记.md:122、印记.md:124） | ⚠️ Partial → — | ③ |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#1#064c8323 | 个人图鉴跨局跟人走，是仅有的两个跨局持久载体之一（图鉴.md:21、图鉴.md:166） | ⚠️ Partial → — | ③ |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#3#1a0b8db3 | 线索层触发：错饵被拒（鱼影绕一圈摆尾走）揭剪影（图鉴.md:100） | ✅ Implemented → — | ③ |

### Code-only mechanics

描述差异、待核机制变化：以下仅对比文字，由复核／综合核源码与属主裁决后确认。
| 描述来源 | 机制描述 | code_ref |
|---|---|---|
| 本轮独有描述 | 结算 RPC 内要求封面计划＋计划终态＋全部 Grant ACK；实际 Host 退出链另要求全部 Grant ACK | Source/Catfishing/Collection/CatRunImprintService.cpp:574、Source/Catfishing/Framework/Game/CatfishingPlayerController.cpp:671、Source/Catfishing/Online/CatOnlineSubsystem.cpp:1842、Source/Catfishing/Framework/Game/CatfishingGameModeBase.cpp:2236 |
| 本轮独有描述 | 逐鱼 CaptureImprintEventId 仍驱动首钓候选 | Source/Catfishing/Data/CatFishDefinition.h:204、Source/Catfishing/Items/Fish/CatFishPickupActor.cpp:1152、Source/Catfishing/Items/Fish/CatFishPickupActor.cpp:1162 |
| 本轮独有描述 | EncounterCount 在剪影与捕获各累加，并公开显示“交手 N” | Source/Catfishing/Framework/Core/CatProfileContracts.h:182、Source/Catfishing/Profile/CatProfileSubsystem.cpp:288、Source/Catfishing/Profile/CatProfileSubsystem.cpp:304、Source/Catfishing/UI/Collection/CatCollectionModel.cpp:112 |
| 本轮独有描述 | Host 媒体 Manifest、分块 hash、授权版本与 cursor 续传，4 MiB／64 KiB／128 块与 MIME 限制 | Source/Catfishing/Collection/CatImprintMediaTransportService.cpp:46、Source/Catfishing/Collection/CatImprintMediaTransportService.cpp:258、Source/Catfishing/Collection/CatImprintMediaSettings.h:24、Config/DefaultGame.ini:183 |
| 本轮独有描述 | 已不在当前鱼目录中的个人鱼记录仍保留为页面 | Source/Catfishing/UI/Collection/CatCollectionModel.cpp:136 |
| 本轮独有描述 | 以 ControllerId 选个人槽、Schema v2 重建覆盖同槽，仍保存功能装备选择 | Source/Catfishing/Profile/CatProfileSubsystem.cpp:21、Source/Catfishing/Profile/CatProfileSubsystem.cpp:35、Source/Catfishing/Profile/CatProfileSaveGame.h:50 |
| 本轮独有描述 | 公开图鉴摘要最多 512 条，超限整批拒收并保留旧摘要 | Source/Catfishing/Framework/Game/CatfishingPlayerState.cpp:47、Source/Catfishing/Framework/Game/CatfishingPlayerState.cpp:63、Source/Catfishing/Framework/Game/CatfishingPlayerController.cpp:705 |
| 基线独有描述 | 结算完成被印记链路阻塞：`IsSettlementArchiveReady` 要求本局必须已有篝火封面计划、所有 CapturePlan 进终态、所有 Grant 拿到 durable ACK，否则 `CompleteSettlementFromServerRequest` 直接返回 TeardownFailed | Source/Catfishing/Collection/CatRunImprintService.cpp:444-475、Source/Catfishing/Framework/Game/CatfishingPlayerController.cpp:660-673 |
| 基线独有描述 | 逐鱼印记事件字段 `CaptureImprintEventId`：鱼资产逐条配置成像事件 ID，None 就跳过 CapturePlan | Source/Catfishing/Data/CatFishDefinition.h:107、Source/Catfishing/Items/Fish/CatFishPickupActor.cpp:1090、:1100 |
| 基线独有描述 | 图鉴记录的「交手次数」`EncounterCount`：剪影与捕获 Grant 都累加，并在图鉴行文本里直接显示 | Source/Catfishing/Framework/Core/CatProfileContracts.h:168-170、Source/Catfishing/Profile/CatProfileSubsystem.cpp:268、Source/Catfishing/UI/Collection/CatCollectionModel.cpp:51-55（行号更正：原写 :174-176，那里已是 `FCatLocalImprintRecord`） |
| 基线独有描述 | 印记媒体传输服务：Host 单次传输 + Manifest + 分块 hash + 按收件人授权纪元与 cursor 断点续传，Config 已开 `bEnableImprintMediaTransport=True` 并限定 MIME 与容量 | Source/Catfishing/Collection/CatImprintMediaTransportService.{h,cpp} 全文、Source/Catfishing/Collection/CatImprintMediaSettings.h、Config/DefaultGame.ini:132-138 |

## 商店

未变 27／退步 2／修复 1／其他变化 8／改写 1／锚点漂移 0／新增 16／消失 8

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| U0JtwqFJ8ijKxakb1egcRXhvnKg#2#9b03260c | 鱼缸容量升级＝唯一设施类商品：不进团队装备库、直接作用在营地那口缸上、按档位买、随局清空；初始 10 条，两档 20／30，价 300／700 金币（商店.md | ❌ Missing → 🔄 Divergent | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.2#6c496649 | 消耗品单次携带上限以「钓鱼系统」2.7 为准（商店.md:71） | ⚠️ Partial → ✅ Implemented | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.2#7e4582e3 | 水面设空气墙，物品不能丢入湖中（商店.md:77） | ❓ Unverifiable → ⚠️ Partial | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.1#4d8ab60b | 收购价、吃鱼成长收益与献祭分量使用一套可对照的等价换算表（商店.md:65；商店.md:125） | ❌ Missing → ⚠️ Partial | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.3#4b0d44a5 | 每局设启动金，具体金额在数值阶段确定（商店.md:83；商店.md:129） | ⚠️ Partial → 🔄 Divergent | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.4#dae889b6 | 商人猫是世界里第一个 NPC，猫猫在他这里进行买卖（商店.md:87） | 🔄 Divergent → ❓ Unverifiable | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#2#8cde8f62 | 成功局＝把世界进度顶到 100% 的那次上供当场转成结算夜（不翻天），商人猫在此收摊（商店.md:54） | ✅ Implemented → ⚠️ Partial | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#2#03234dba | 收摊后把剩余公款／装备换成小鱼干，给猫猫们在篝火旁娱乐（商店.md:54；商店.md:87） | ❌ Missing → ⚠️ Partial | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#7#f9003750 | 购买广播作为全场事件，至少显示购买者、商品和支出（商店.md:134） | ❌ Missing → ⚠️ Partial | ① |
| DeiSFL#r2#f77e0692 | 其余三项 Playtest 记录指标：卖／吃／献的鱼数与价值占比、高级竿连续占用时长与重复争抢次数、断竿后重新出发时间与免费 1 级竿使用率（DeiSFL.cs | ❌ Missing → ⚠️ Partial | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#2#daec1341 | 购买以一车为单位：一次可选多件，提交时服务器按当前价重算总价、整车一起成交，任一件缺货或公款不够就整车不成交且公款不动（商店.md:52；商店.md:71） | ✅ Implemented → ⚠️ Partial | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#2#de1bcb30 | 商人猫收摊＝买卖冻结：停止新的购物车支付、售鱼入账与每日进货（商店.md:54；商店.md:87） | ✅ Implemented → ✅ Implemented | ② 措辞改写 |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#a15a4c9b | 落地后至翻天仍无人拾取的物品，在翻天时消失；当天内扔下＝赠送不变（设计修改记录.md:231） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#97e7bf5a | 收购价以鱼体重为主要锚点，售价＝鱼表格金钱系数（金币／kg）×实际重量（kg）（设计修改记录.md:291） | — → ✅ Implemented | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.3#ebe86de5 | 金币跟局走，局末不带入新局；终局存档保留并标记已完结、不再提供继续（商店.md:81；设计修改记录.md:307） | — → ⚠️ Partial | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#2#0560d1e4 | 失败局资源不继承到新局，已解锁图鉴保持不变；失败槽保留并封存（商店.md:55；设计修改记录.md:307） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#b3c11edd | Demo 不做装备解锁：三档竿、三种漂、抄网、鱼护开局就在货架，解锁只管表达类（设计修改记录.md:287） | — → ⚠️ Partial | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.5#ddd15080 | 解锁清单跟玩家走；装备和公款只跟当前局走，同一局的装备、公款及营地状态进入局中断点（商店.md:93；设计修改记录.md:287；设计修改记录.md:307） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#9c73b2b2 | 商店只负责收购端，不判断拿鱼动机或归属；拿鱼规则与玩家间追逐归联机社交（设计修改记录.md:305） | — → ✅ Implemented | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#30c1f38d | 商品价格以鱼竿／鱼漂／鱼饵／窝料四张专表为准（设计修改记录.md:287） | — → ❓ Unverifiable | ③ |
| DKhnweGaEiPTJZkO8OScQOJenqg#r2#e677b1e4 | 收鱼表按 fish_id 对齐全部 16 种鱼的逐公斤金钱系数（Knowledge/Design/GDD 系统分册/鱼/鱼表格/第一版.csv 第 2 行 金钱 | — → ❓ Unverifiable | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.3#a59bf42a | 货币实体形态、材质与世界观表达由内容与美术层交付（商店.md:83；GaHeFr.csv 第 8 行 待对齐内容） | — → ❓ Unverifiable | ③ |
| DeiSFL#r2#7ca7bb6f | 首轮三路取舍：卖／吃／献均被主动选择，任一路径价值占比不长期高于 70%（DeiSFL.csv 第 2 行 首轮通过条件） | — → ❓ Unverifiable | ③ |
| DeiSFL#r3#499ee979 | 首轮公款自治：多数购买广播带来沟通或笑闹，无持续破坏一局体验的消费冲突（DeiSFL.csv 第 3 行 首轮通过条件） | — → ❓ Unverifiable | ③ |
| DeiSFL#r4#91e41a5e | 首轮公共装备分配：好装备存在协商或轮换，无单人长期垄断导致其他玩家失去参与感（DeiSFL.csv 第 4 行 首轮通过条件） | — → ❓ Unverifiable | ③ |
| DeiSFL#r5#7228752a | 首轮软竞争：玩家拿鱼出售能形成追逐与故事，不稳定导致团队经济崩溃或团灭（DeiSFL.csv 第 5 行 首轮通过条件；设计修改记录.md:305） | — → ❓ Unverifiable | ③ |
| DeiSFL#r6#f7d887de | 首轮经济弧线：前中后期均有有效购买，结算有余兴且不出现大量无处可花的资源（DeiSFL.csv 第 6 行 首轮通过条件） | — → ❓ Unverifiable | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.2#75f8db77 | 备装领取要承接成长后的携带能力：背包格数每次 +1，后勤扩容令鱼饵／窝料上限各 +2（商店.md:71；Knowledge/Design/GDD 系统分册/猫咪 | — → ⚠️ Partial | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.2#626929e9 | 落地后至次日清晨仍无人拾取的物品，自动回收到营地（商店.md:77） | ❌ Missing → — | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.1#f79169d5 | 收购价以鱼体重为主要锚点（商店.md:63、128） | ✅ Implemented → — | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.1#a439bdd3 | 窝料轴可作为收购价修正系数（商店.md:63） | ❌ Missing → — | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.3#60de6fd6 | 金币跟局走，局末通过结算流程归零（商店.md:81） | ⚠️ Partial → — | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#2#583193a8 | 失败局无论剩余多少，本局金币与装备全部清零；已解锁的图鉴保持不变（商店.md:55） | ⚠️ Partial → — | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.5#902e00ba | 解锁＝商店上新货：解锁的品类进入货架，具体件仍需使用公款购买（商店.md:91） | ⚠️ Partial → — | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.5#b7f42ffb | 解锁清单跟玩家走，使下一局初始货架包含已解锁品类；具体装备和公款只跟当前局走（商店.md:93） | ⚠️ Partial → — | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.1#1c7fa6a2 | 偷卖后的追逐、追回与抓捕规则归联机社交，商店只负责收购端（商店.md:67、109） | ✅ Implemented → — | ③ |

### Code-only mechanics

描述差异、待核机制变化：以下仅对比文字，由复核／综合核源码与属主裁决后确认。
| 描述来源 | 机制描述 | code_ref |
|---|---|---|
| 本轮独有描述 | 逐鱼整数舍入与 GAS 精确整数上限 16,777,216 | Source/Catfishing/AbilitySystem/Executions/CatShopEconomyTransactionExecutionCalculation.cpp:38；Source/Catfishing/AbilitySystem/Executions/CatShopEconomyTransactionExecutionCalculation.cpp:84 |
| 本轮独有描述 | 购物车输入最多 64 原始行，同商品聚合最多 999 次 | Source/Catfishing/ShopEconomy/Trading/CatShopTradingTypes.h:229；Source/Catfishing/ShopEconomy/CatShopCartCommandUtils.cpp:20 |
| 本轮独有描述 | HUD 首秒只建立历史基线、显示 6 秒、保留 8 条，仅将最后一条投到文本 | Source/Catfishing/UI/HUD/CatHUDModel.cpp:45；Source/Catfishing/UI/HUD/CatHUDModel.cpp:283；Source/Catfishing/UI/HUD/CatHUDModel.cpp:863；Source/Catfishing/UI/HUD/CatHUDWidget.h:159 |
| 本轮独有描述 | 未配置公共架／公库角色时回退到同一个营地库存 | Source/Catfishing/ShopEconomy/Trading/CatShopTradeController.cpp:133；Source/Catfishing/ShopEconomy/Trading/CatShopTradeController.cpp:146 |
| 本轮独有描述 | 小鱼干兑换按金币整除，支持封顶，零头和超过兑换上限的余额留在本局 | Source/Catfishing/ShopEconomy/CatShopEconomyService.cpp:731；Source/Catfishing/ShopEconomy/CatShopEconomyService.cpp:784；Source/Catfishing/ShopEconomy/CatShopEconomySettings.h:54 |
| 本轮独有描述 | Profile 选槽仍以功能装备 RequiredUnlockId 作许可条件 | Source/Catfishing/Profile/CatProfileSubsystem.cpp:127；Source/Catfishing/Profile/CatProfileSubsystem.cpp:135；Source/Catfishing/Equipment/CatEquipmentComponent.cpp:274 |
| 基线独有描述 | 下单时的摊位距离证明：服务端在整车提交前复核玩家仍在摊位 `InteractionRadiusCentimeters`（默认 300cm）内，超距即 `DependencyUnavailable` | `Source/Catfishing/ShopEconomy/CatShopKioskActor.cpp:87-102`、`Source/Catfishing/ShopEconomy/Trading/CatShopTradeController.cpp:115-119` |
| 基线独有描述 | 公款版本校验：整车命令带 `ExpectedRevision`，与服务器 `WalletRevision` 不符即 `RevisionConflict` 拒绝整车 | `Source/Catfishing/ShopEconomy/CatShopEconomyService.cpp:223-227`、`Source/Catfishing/ShopEconomy/Trading/CatShopTradingTypes.h:241` |
| 基线独有描述 | 两段式交付：购买账本先写 `bDeliveryPending`，再由 `ConfirmTransactionDelivery` 用下游回执推进到 `bDeliveryConfirmed`，公开 DTO 也带这两个标位 | `Source/Catfishing/ShopEconomy/CatShopEconomyService.cpp:577-667`、`Source/Catfishing/ShopEconomy/Trading/CatShopTradingTypes.h:81-91` |
| 基线独有描述 | 商店分类页：目录行带 `DisplayCategoryId` 与分类显示名覆盖，UI 归纳出分类按钮并本地过滤 | `Source/Catfishing/ShopEconomy/Catalog/CatShopCatalogTypes.h:23-29、112-118`、`Source/Catfishing/UI/Shop/CatShopTypes.h:90-111` |
| 基线独有描述 | 售鱼金额的工程边界：逐鱼四舍五入后再求和，单鱼与整批都夹在 16777216 以内，越界整单拒绝而不给部分金额 | `Source/Catfishing/AbilitySystem/Executions/CatShopEconomyTransactionExecutionCalculation.cpp:32-46`、`Source/Catfishing/ShopEconomy/CatShopEconomyService.cpp:472-476` |
| 基线独有描述 | 地面鱼护页的批量售鱼入口：鱼护页自带「卖选中／卖全部」按钮，按附近买家与双方距离显隐，一次提交多条鱼 | `Source/Catfishing/UI/Inventory/CatFishGuardInventoryWidget.cpp:22-29、67-80`、`Source/Catfishing/ShopEconomy/Trading/CatShopTradeController.cpp:216-241` |
| 基线独有描述 | 功能装备的用侧解锁闸门：竿／饵／漂／抄网四件装备定义各带 `RequiredUnlockId`，装配时逐件要求 PlayerState 持有服务器授权的解锁证明，Profile 选槽也另查一次 `UnlockIds`（复核补） | `Source/Catfishing/Equipment/CatEquipmentDefinition.h:79-81`、`Source/Catfishing/Equipment/CatEquipmentComponent.cpp:274-277`、`Source/Catfishing/Profile/CatProfileSubsystem.cpp:132`、`Source/Catfishing/Framework/Game/CatfishingPlayerState.cpp:95-99` |

## 猫咪与状态

未变 27／退步 0／修复 3／其他变化 27／改写 1／锚点漂移 0／新增 18／消失 4

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| BIeHwP14vigUEBkI3UxciEionxf#L0#ed62f1b5 | 选猫界面用非数值示意力气（鱼骨 icon ×1～×4 ＋性格文案），不显示数字（猫咪与状态.md:15） | ❌ Missing → ❓ Unverifiable | ① |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#2#f99b50bd | 经验槽 250 点（数值成长.md:35；升级效果.md:13） | 🔄 Divergent → ✅ Implemented | ① |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#2#148720f2 | 槽满触发三选一：随机抽三项、同次互异、当场生效、本局永久、同项可跨次叠加、按出现次序解锁（数值成长.md:36；升级效果.md:19） | ❌ Missing → ⚠️ Partial | ① |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#2#e5c92c36 | 三选一面板不冻结世界、个人选择不阻挡他人、可挂起、无超时（数值成长.md:36） | ❌ Missing → ❓ Unverifiable | ① |
| Sisrw4FzWiL2AekXetFc7Vxunqg#2#bf64540c | 选项池 11 项的效果定义、每次数值、叠加上限、出现次序（升级效果.md:27） | ❌ Missing → ⚠️ Partial | ① |
| BIeHwP14vigUEBkI3UxciEionxf#2#32700fc2 | 吃鱼给限时 buff，按鱼种配置、时长 60～600 秒（猫咪与状态.md:80；数值成长.md:26；数值成长.md:44） | ❌ Missing → ⚠️ Partial | ① |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#3#d3d4a7c2 | buff 叠加规则「类＝鱼种」：同种刷新时长、异种并存（数值成长.md:45） | ❌ Missing → ⚠️ Partial | ① |
| O753wiu7yiuBtMkburWcicZnnre#3#24c4d280 | 社交演出族 buff 四项：变声器、满嘴泥巴、头上长小花、请勿靠近（吃鱼效果.md:39） | ❌ Missing → ⚠️ Partial | ① |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#4#bfa89c93 | 黄色体力：体力条末端一次性护盾段，绿先扣、不自然回复、可叠加无上限、过夜清空，来源＝特定鱼种与祝福（数值成长.md:50；数值成长.md:52） | ❌ Missing → ⚠️ Partial | ① |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#5#7a402925 | 搏斗内放线本身不回体，回体速率基础为 0、只靠三选一「放线回体速度」加（数值成长.md:56；升级效果.md:29） | 🔄 Divergent → ⚠️ Partial | ① |
| O753wiu7yiuBtMkburWcicZnnre#5#f64d2ee7 | 咸鱼不可食用、湖心巨影无法食用（吃鱼效果.md:58；吃鱼效果.md:62） | ❌ Missing → ❓ Unverifiable | ① |
| BIeHwP14vigUEBkI3UxciEionxf#4#9f9e8dc4 | 中毒按鱼各配、无渐进升级，最重一档＝吃下即倒地（猫咪与状态.md:104） | 🔄 Divergent → ⚠️ Partial | ① |
| BIeHwP14vigUEBkI3UxciEionxf#4#5db5f710 | 认识一种鱼分两层：钓到＝记外观与名字，首次吃过＝图鉴补食用效果（猫咪与状态.md:104；猫咪与状态.md:224） | ❌ Missing → ⚠️ Partial | ① |
| BIeHwP14vigUEBkI3UxciEionxf#5#277c1586 | 倒地者可缓慢爬行（猫咪与状态.md:116） | 🔄 Divergent → ✅ Implemented | ① |
| BIeHwP14vigUEBkI3UxciEionxf#5#3ab6ecad | 倒地满一段时间（占位 60 秒）后自己站起来（猫咪与状态.md:116） | ❌ Missing → ✅ Implemented | ① |
| BIeHwP14vigUEBkI3UxciEionxf#5#4c18d43a | 翻天时仍在倒地的自动救起，清晨在营地醒来（猫咪与状态.md:116） | ❌ Missing → ⚠️ Partial | ① |
| BIeHwP14vigUEBkI3UxciEionxf#5#6e6ac713 | 倒地者视角不黑屏，镜头贴地看得见围过来的猫脸（猫咪与状态.md:116） | ❌ Missing → ❓ Unverifiable | ① |
| BIeHwP14vigUEBkI3UxciEionxf#5#f0d54b1f | 旁观者互动：凑近嗅嗅、爪子戳肚皮、舔脸、旁边躺平陪倒（猫咪与状态.md:120） | ❌ Missing → ❓ Unverifiable | ① |
| BIeHwP14vigUEBkI3UxciEionxf#4#3521bfb8 | 中毒失态演出：走路画龙、瞳孔漩涡、打嗝冒泡、幻觉、翻肚皮晕倒、头顶转圈小鱼（猫咪与状态.md:104；猫咪与状态.md:106；猫咪与状态.md:190） | ⚠️ Partial → ❓ Unverifiable | ① |
| BIeHwP14vigUEBkI3UxciEionxf#6#d70586ee | 雨天猫下意识往树下、叶子下钻（猫咪与状态.md:134） | ❌ Missing → ❓ Unverifiable | ① |
| BIeHwP14vigUEBkI3UxciEionxf#3#f0de8d2d | 疲惫三档纯演出：轻→打哈欠、中→走路拖沓、重→坐下瞌睡点头，无数值条无惩罚（猫咪与状态.md:92） | ❌ Missing → ⚠️ Partial | ① |
| BIeHwP14vigUEBkI3UxciEionxf#1#1e5dc607 | 猫行为库素材：舔毛、甩尾、打哈欠、打滚、伸懒腰、休息、睡觉（猫咪与状态.md:68；猫咪与状态.md:188） | ⚠️ Partial → ❓ Unverifiable | ① |
| BIeHwP14vigUEBkI3UxciEionxf#1#e6299c03 | 行为与状态联动：累了瞌睡点头、看见别人的鱼护舔嘴、无聊时追尾巴拍水面（猫咪与状态.md:68） | ❌ Missing → ❓ Unverifiable | ① |
| BIeHwP14vigUEBkI3UxciEionxf#1#33a08426 | 群体行为：打哈欠传染、营地睡觉叠罗汉（猫咪与状态.md:70） | ❌ Missing → ❓ Unverifiable | ① |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#6#a9d9ca50 | 吃鱼瞬时浮层：体力快照（含黄段与本次增量）、经验槽小环与 +N、已认识的鱼显示效果一行、没吃过显示「？？？」（数值成长.md:63） | ❌ Missing → ⚠️ Partial | ① |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#6#6b39c5cf | 三选一卡面显示每项已叠次数（数值成长.md:64） | ❌ Missing → ⚠️ Partial | ① |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#6#12a257b6 | 主动查看面板：黄色体力存量、已叠 build 构成、各 buff 剩余时间（数值成长.md:65） | ❌ Missing → ⚠️ Partial | ① |
| BIeHwP14vigUEBkI3UxciEionxf#2#9129542b | 吃鱼演出：叼住甩头吞下、吃完舔爪抹脸满足眯眼；经验满时伸大懒腰抖毛（猫咪与状态.md:82） | ❌ Missing → ❓ Unverifiable | ① |
| O753wiu7yiuBtMkburWcicZnnre#6#09816fc8 | 咸鱼可卖但**不可献**，献祭时被猫神拨开（吃鱼效果.md:62） | ❌ Missing → 🔄 Divergent | ① |
| O753wiu7yiuBtMkburWcicZnnre#3#41bedcfa | 「请勿靠近」屏蔽帮助与恶作剧（搬运被屏蔽、通用猫抓猫扑倒不被屏蔽）（吃鱼效果.md:44；设计修改记录.md:305） | ❌ Missing → ⚠️ Partial | ② 措辞改写 |
| BIeHwP14vigUEBkI3UxciEionxf#10#e21b727e | 拿来的鱼吃掉同样获得成长（猫咪与状态.md:219；数值成长.md:19；设计修改记录.md:305） | — → ✅ Implemented | ③ |
| BIeHwP14vigUEBkI3UxciEionxf#5#0e0f901e | 钓鱼中倒地＝中断本人的钓鱼操作、释放抓握，状态机按剩余现场打断回退（猫咪与状态.md:118；猫咪与状态.md:217；设计修改记录.md:303） | — → ✅ Implemented | ③ |
| BIeHwP14vigUEBkI3UxciEionxf#版本历史#27e6585b | 草药机制已删除；倒地通过自起、休息、伙伴营地救援或翻天救起解除（猫咪与状态.md:249；猫咪与状态.md:116；设计修改记录.md:285） | — → ✅ Implemented | ③ |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#2#956045a9 | 正式逐鱼经验系数由鱼表持有，与力量系数独立，并用于实际食用经验（数值成长.md:35；第一版.csv 第 2 行 经验系数（KG×系数=经验)） | — → ❓ Unverifiable | ③ |
| O753wiu7yiuBtMkburWcicZnnre#2#03017f88 | 小彩鱼、黑鱼、风铃鱼各授予 20 点黄色体力（吃鱼效果.md:27；吃鱼效果.md:66） | — → ❓ Unverifiable | ③ |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#4#f01cd8dd | 绿空黄不空时仍保持力量输出，不能仅因绿段为零判无力（数值成长.md:50；设计修改记录.md:379） | — → ⚠️ Partial | ③ |
| BIeHwP14vigUEBkI3UxciEionxf#6#5a197ca2 | 淋湿只用于表现，不造成任何数值惩罚（猫咪与状态.md:140） | — → ✅ Implemented | ③ |
| Sisrw4FzWiL2AekXetFc7Vxunqg#1#b9846450 | 三选一是纯被动的局内提升、不增加搏斗内主动操作，选项池最多 16 项（升级效果.md:19；升级效果.md:20） | — → ✅ Implemented | ③ |
| O753wiu7yiuBtMkburWcicZnnre#2#5d87d053 | 吃鱼成长不提供常规即时体力回复、不回补鱼竿耐久，入搏斗不补满体力（吃鱼效果.md:31；数值成长.md:31；设计修改记录.md:309） | — → ✅ Implemented | ③ |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#6#ad1cb1c9 | 同种 buff 刷新时，旧演出熄灭、新演出重燃（数值成长.md:63） | — → ❓ Unverifiable | ③ |
| BIeHwP14vigUEBkI3UxciEionxf#10#3691e7f5 | 倒地救援可发求助广播（猫咪与状态.md:219） | — → ⚠️ Partial | ③ |
| BIeHwP14vigUEBkI3UxciEionxf#3#44a36944 | 回营或休息后清掉疲惫演出档（猫咪与状态.md:92） | — → ❌ Missing | ③ |
| BIeHwP14vigUEBkI3UxciEionxf#1#ed95fc8f | 不引入复杂生存、宠物等级培养、复杂医疗或烹饪系统，鱼直接食用（猫咪与状态.md:45；猫咪与状态.md:155；猫咪与状态.md:156） | — → ✅ Implemented | ③ |
| O753wiu7yiuBtMkburWcicZnnre#6#e82544bf | 河纹鱼与河口鲈只提供经验、不提供 buff（吃鱼效果.md:62） | — → ❓ Unverifiable | ③ |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#2#bab382b6 | 吃倾向路线的三选一触发率至少 1.5 次/白天，经济参数变动后重跑模拟（数值成长.md:37；数值成长.md:74） | — → ❓ Unverifiable | ③ |
| Sisrw4FzWiL2AekXetFc7Vxunqg#1#7a382f63 | 三选一以三条小鱼干供猫选择叼取（升级效果.md:21；猫咪与状态.md:82） | — → ❓ Unverifiable | ③ |
| BIeHwP14vigUEBkI3UxciEionxf#6#0864ee20 | 抖水会溅到旁猫，形成连锁抖水演出（猫咪与状态.md:136） | — → ❓ Unverifiable | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#bc8f5733 | 吃鱼动作按实际体重档分两套：小档不足1kg，中档及以上至少1kg；巨影不可食用无吃鱼动作（设计修改记录.md:273；动作表现集.md:89；动作表现集.md: | — → ❓ Unverifiable | ③ |
| BIeHwP14vigUEBkI3UxciEionxf#1#ab0f6e59 | 偷来的鱼吃掉同样获得成长（猫咪与状态.md:41；数值成长.md:19） | ✅ Implemented → — | ③ |
| Sisrw4FzWiL2AekXetFc7Vxunqg#3#67668c40 | 第 10 天结算生成「毕业照」印记（本局 build 快照 ＋猫味外号）（升级效果.md:44） | ❌ Missing → — | ③ |
| BIeHwP14vigUEBkI3UxciEionxf#5#a1518cc0 | 钓鱼中倒地＝中断钓鱼、交回剩余成员（猫咪与状态.md:118） | ✅ Implemented → — | ③ |
| BIeHwP14vigUEBkI3UxciEionxf#版本历史#68be4e0c | 草药机制已删除，倒地解除只剩救援与休息（猫咪与状态.md:249 v1.3） | 🔄 Divergent → — | ③ |

### Code-only mechanics

描述差异、待核机制变化：以下仅对比文字，由复核／综合核源码与属主裁决后确认。
| 描述来源 | 机制描述 | code_ref |
|---|---|---|
| 本轮独有描述 | 野外自救 RPC 一次调用立即起身 | Source/Catfishing/Framework/Game/CatfishingPlayerController.cpp:1212；Source/Catfishing/Condition/CatConditionComponent.cpp:305；Source/Catfishing/Condition/CatConditionComponent.cpp:422 |
| 本轮独有描述 | 自然恢复额外闸门与20%恢复出力阈值 | Source/Catfishing/AbilitySystem/Physics/CatPhysicalEffortComponent.cpp:77；Source/Catfishing/AbilitySystem/Physics/CatPhysicalEffortComponent.cpp:93；Source/Catfishing/AbilitySystem/Config/CatPhysicalEffortSettings.h:23 |
| 本轮独有描述 | 近身救援可直接传送目标回营地 | Source/Catfishing/Camp/CatCampHubActor.cpp:208；Source/Catfishing/Camp/CatCampHubActor.cpp:215 |
| 本轮独有描述 | 倒地自动松开嘴叼鱼/鱼护 | Source/Catfishing/Character/CatCharacter.cpp:478 |
| 本轮独有描述 | 爬行倍率0.25与禁跳作为工程参数 | Config/DefaultGame.ini:216；Source/Catfishing/Character/CatCharacter.cpp:496；Source/Catfishing/Character/Physics/CatPhysicalBodyComponent.cpp:765 |
| 本轮独有描述 | 删除草药后仍有旧动作资产与过期恢复说明 | Source/Catfishing/Condition/CatConditionTypes.h:13；Config/DefaultGame.ini:204 |
| 本轮独有描述 | 叠加到上限的选项从后续候选中剔除 | Source/Catfishing/Growth/CatGrowthComponent.cpp:182；Source/Catfishing/Growth/CatGrowthComponent.cpp:189 |
| 本轮独有描述 | 倒地额外禁止部分物品、交易和社交交互 | Source/Catfishing/Inventory/CatInventoryComponent.cpp:2278；Source/Catfishing/Framework/Game/CatfishingPlayerController.cpp:886；Source/Catfishing/ShopEconomy/Trading/CatShopTradeController.cpp:333；Source/Catfishing/Social/CatSocialService.cpp:76；Source/Catfishing/Camp/CatCampfireActor.cpp:94 |
| 基线独有描述 | 倒地时服务器强制释放嘴里叼着的鱼或鱼护，就地落地 | `Source/Catfishing/Character/CatCharacter.cpp:466`–`470` |
| 基线独有描述 | 倒地＝全面交互禁用：钓鱼、打窝、商店交易、库存使用、容器取鱼、鱼护拾取、社交求助／恶作剧、祭坛确认，再加通用命令门、地面鱼叼起与投放、以及一处 Controller 侧 gate——**复核重数共 12 处**（原文写「八处」漏了后四处） | `Source/Catfishing/Fishing/CatFishingService.cpp:1511`、`Source/Catfishing/Fishing/CatFishingService.cpp:1529`、`Source/Catfishing/Environment/CatChumPlacementService.cpp:135`、`Source/Catfishing/ShopEconomy/Trading/CatShopTradeController.cpp:201`、`Source/Catfishing/Inventory/CatInventoryComponent.cpp:2227`、`Source/Catfishing/FishContainers/CatFishContainerService.cpp:469`、`Source/Catfishing/FishContainers/CatFishGuardActor.cpp:131`、`Source/Catfishing/Social/CatSocialService.cpp:634`、`Source/Catfishing/Camp/CatAltarActor.cpp:76`、`Source/Catfishing/Framework/Game/CatfishingGameModeBase.cpp:798`、`Source/Catfishing/Framework/Game/CatfishingPlayerController.cpp:1097`、`Source/Catfishing/Items/Fish/CatFishPickupActor.cpp:610`、`Source/Catfishing/Items/Fish/CatFishPickupActor.cpp:1055` |
| 基线独有描述 | 每次进入搏斗把钓手体力补满，且多条钓鱼终局路径再次 `RequestFishingStaminaReset` | `Source/Catfishing/AbilitySystem/Core/CatAbilitySystemComponent.cpp:309`–`330`、`Source/Catfishing/AbilitySystem/Core/CatAbilitySystemComponent.cpp:332`–`345`；`Source/Catfishing/Fishing/CatFishingSession.cpp:130`、`Source/Catfishing/Fishing/CatFishingSession.cpp:1067`（入场补满两个入口）；`Source/Catfishing/Fishing/CatFishingSession.cpp:1154`、`:1197`、`:1238`、`:1255`、`:2052`、`:2134`（**复核补齐**：六条钓鱼终局路径再挂一次 `RequestFishingStaminaReset`，原文只写了「多条」没给行号） |
| 基线独有描述 | 出力体力恢复的两个前置条件：必须先付过出力费（`bRecoveryPending`），且连续静止满 2 秒（`RecoveryDelaySeconds = 2.0`）才起算 | `Source/Catfishing/AbilitySystem/Config/CatPhysicalEffortSettings.h:18`–`21`；`Source/Catfishing/AbilitySystem/Physics/CatPhysicalEffortComponent.cpp:70`、`Source/Catfishing/AbilitySystem/Physics/CatPhysicalEffortComponent.cpp:76`–`82` |
| 基线独有描述 | 臭臭鱼作为供品每条给整批世界进度增益打 25% 折扣（上限归零） | `Source/Catfishing/AbilitySystem/Executions/CatRunSettleOfferingExecutionCalculation.cpp:109`–`110`；`Source/Catfishing/Camp/CatAltarActor.cpp:237`；`Config/DefaultGame.ini:249` |

## 环境与氛围

未变 0／退步 0／修复 0／其他变化 0／改写 0／锚点漂移 0／新增 38／消失 34

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| Bg2jwBdkViHRgRkOtx2crCgAnHh#1#c1fb523b | 时钟源＝局内时间流：开局启动、随局推进、局末即弃，任何机制不读现实时钟（环境与氛围.md:65） | — → ✅ Implemented | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#1#b0d786eb | 白天时段轴＝清晨/白天/黄昏三段，夜晚不入时段轴（环境与氛围.md:65） | — → ⚠️ Partial | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#1#e9bc641a | 一局＝多天，白天限时推进、夜晚无时限停摆、全员同意后开始新的一天（环境与氛围.md:65） | — → ✅ Implemented | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#1#93df395c | 昼夜推进要真的改变语义：时段分界到点重新发布环境（事件出场时段的前提）（环境与氛围.md:71） | — → ✅ Implemented | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#1#a271ff1f | 昼夜改光照（画面随一天走完一条弧线）（环境与氛围.md:65） | — → ⚠️ Partial | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#7#952bf4d3 | 昼夜改环境音；夜晚虫鸣是「入夜」的听觉信号（环境与氛围.md:186） | — → ❓ Unverifiable | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#2#1440495e | 天气按局内时钟在晴/雨/雾之间切换，每一局有不同的脸色（环境与氛围.md:77） | — → 🔄 Divergent | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#2#f6e0dc4b | 天气轴枚举＝晴/雨/雾（轴定义权归本册）（环境与氛围.md:77） | — → ✅ Implemented | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#4#07246011 | 区域轴枚举由本册定义（有哪些区域），鱼落在轴上哪一格归鱼册（环境与氛围.md:156） | — → ✅ Implemented | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#3#36f35137 | 双区域探索结构：河流（日常水域）＋森林湖（要走一段路、鱼种与事件偏稀有）（环境与氛围.md:89） | — → ❓ Unverifiable | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#3#d80c6db3 | 钓点＝玩家概念而非机制点位，任意水边可钓，打窝即自发成为钓点（环境与氛围.md:89） | — → ✅ Implemented | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#2#45ea7ab8 | 时段与天气对鱼情做机会向修正（修正入口归本册，鱼表归鱼册）（环境与氛围.md:77） | — → ⚠️ Partial | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#4#5cbbc316 | 设计禁区：环境只给机会不做惩罚——无天气惩罚、无生存压力、无复杂生态模拟（环境与氛围.md:154） | — → ✅ Implemented | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#2#1bbd9dd3 | 雨天淋湿由本册触发（表现规范归猫册，本册只触发）（环境与氛围.md:77） | — → ⚠️ Partial | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#4#3efa169a | 自然事件按局内时钟与天气条件出场，条件不成立就没有事件（不造占位事件）（环境与氛围.md:101） | — → ⚠️ Partial | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#4#dddfc023 | 彩虹：雨后放晴出场（环境与氛围.md:105） | — → 🔄 Divergent | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#4#60923793 | 事件必须隔着距离也能察觉（声音＋光＋猫身体语言），否则聚不起人（环境与氛围.md:101） | — → ⚠️ Partial | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#3.2#e90650f0 | 系统按局内时钟排程周期事件，随机只做小抖动（环境与氛围.md:145） | — → ❌ Missing | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#5#813219a8 | 聚鱼时刻：Demo 只有玩家触发，多人向同一水域叠加窝料；自然触发归完整版（环境与氛围.md:120） | — → ⚠️ Partial | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#5#71f78466 | 聚鱼时刻演出与钓鱼收益是同一套：Demo 爆发窗口使咬钩加速、鱼群密度演出增强（环境与氛围.md:123） | — → ⚠️ Partial | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#3.2#410998bb | 所有环境状态（时间、天气、事件进度）属于这一局，局末即弃、无跨局存档（环境与氛围.md:147） | — → ✅ Implemented | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#5#3d6274b2 | 多人同步按局内广播，事件对同局全员同时可见、不存在离线错过（环境与氛围.md:168） | — → ✅ Implemented | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#3#e8107ddc | 森林湖沿途与湖区的特殊看点、风景点、隐藏观察点分布（环境与氛围.md:89） | — → ❓ Unverifiable | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#1#9598275f | 猫味演出：入夜瞳孔放大、清晨伸懒腰、正午找树荫、雨天炸毛甩水、雨停集体抖毛、看鸟咔咔、扑萤等（环境与氛围.md:67） | — → ❓ Unverifiable | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#7#596426b6 | §7 视觉反馈通道：湖面波纹、鱼跃、鸟群聚集、植物随时段的细节变化（环境与氛围.md:185） | — → ❓ Unverifiable | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#1#f767c955 | 夜晚不钓鱼、不产鱼情；入夜边界＝不再有新咬钩，进行中的搏斗允许打完（环境与氛围.md:71） | — → ✅ Implemented | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#3#3bb60c2b | 两区域同属「湖畔」主场景（不是两张分开的关卡）（环境与氛围.md:89） | — → ✅ Implemented | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#3#8640dc09 | 两区域的差异要落在「能遇到什么」上，森林湖的事件偏稀有（环境与氛围.md:95） | — → ⚠️ Partial | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#2#3a9aaddf | 每种天气至少绑一个专属演出或专属事件，天气不能只剩装饰（环境与氛围.md:83） | — → ⚠️ Partial | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#2#79f407e3 | 天气变化要同时改画面和声音（晴/雨/雾切换时画面与声音跟着变）（环境与氛围.md:77） | — → ⚠️ Partial | ③ |
| Xg1EwylUuiUeVckeLwtcMXMjnAg#当前拍定参数#f57f7b08 | 白天时长为 10 分钟，夜晚仍无时限（数值模拟与参数记录.md:46） | — → 🔄 Divergent | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#1#926982ae | 湖畔是固定舞台，不做大规模地形改造或大地图环境编辑（环境与氛围.md:44） | — → ✅ Implemented | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#4#c70f8ed8 | 萤火虫：夜晚、草木边出场（环境与氛围.md:106） | — → ❓ Unverifiable | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#4#c79f3a85 | 晚霞：黄昏出场（环境与氛围.md:107） | — → ⚠️ Partial | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#4#de0dd73e | 月光湖面：夜晚、晴天出场（环境与氛围.md:108） | — → ⚠️ Partial | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#4#2ec22b46 | 鸟群 / 蝴蝶：白天出场（环境与氛围.md:109） | — → ❓ Unverifiable | ③ |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#7#19097be8 | 森林随世界进度复苏或枯萎，让进度涨跌可见；环境规格归本册（局与进程.md:146） | — → ⚠️ Partial | ③ |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#7#1149118f | 鱼缸存量告急时提供全场氛围信号，环境册承接该反馈（局与进程.md:146） | — → ⚠️ Partial | ③ |
| unknown#L0#c1fb523b | 时钟源＝局内时间流：开局启动、随局推进、局末即弃，任何机制不读现实时钟（环境与氛围.md rev106:65、:153、:46） | ✅ Implemented → — | ③ |
| unknown#L0#b0d786eb | 白天时段轴＝清晨/白天/黄昏三段，夜晚不入时段轴（:65、:156 轴定义权归本册） | ✅ Implemented → — | ③ |
| unknown#L0#e9bc641a | 一局＝多天，白天限时推进、夜晚无时限停摆、全员同意后开始新的一天（:65，天结构归局与进程册） | ✅ Implemented → — | ③ |
| unknown#L0#8dad3acb | 昼夜推进要真的改变语义：时段分界到点重新发布环境（事件出场时段的前提，:71） | ✅ Implemented → — | ③ |
| unknown#L0#614e3031 | 昼夜改光照（画面随一天走完一条弧线，:65、:185） | ✅ Implemented → — | ③ |
| unknown#L0#952bf4d3 | 昼夜改环境音；夜晚虫鸣是「入夜」的听觉信号（:65、:186） | ❓ Unverifiable → — | ③ |
| unknown#L0#1440495e | 天气按局内时钟在晴/雨/雾之间切换，每一局有不同的脸色（:77） | 🔄 Divergent → — | ③ |
| unknown#L0#5411df5a | 天气轴枚举＝晴/雨/雾（轴定义权归本册，:77、:156） | ✅ Implemented → — | ③ |
| unknown#L0#07246011 | 区域轴枚举由本册定义（有哪些区域），鱼落在轴上哪一格归鱼册（:156） | ⚠️ Partial → — | ③ |
| unknown#L0#36f35137 | 双区域探索结构：河流（日常水域）＋森林湖（要走一段路、鱼种与事件偏稀有）（:89） | ❓ Unverifiable → — | ③ |
| unknown#L0#d80c6db3 | 钓点＝玩家概念而非机制点位，任意水边可钓，打窝即自发成为钓点（:89） | ✅ Implemented → — | ③ |
| unknown#L0#c7356a9d | 时段与天气对鱼情做机会向修正（修正入口归本册，鱼表归鱼册；:65、:77、:213） | ⚠️ Partial → — | ③ |
| unknown#L0#5cbbc316 | 设计禁区：环境只给机会不做惩罚——无天气惩罚、无生存压力、无复杂生态模拟（:42-45、:154、:170） | ✅ Implemented → — | ③ |
| unknown#L0#ad8d2370 | 雨天淋湿由本册触发（表现规范归猫册，本册只触发；:77） | ❌ Missing → — | ③ |
| unknown#L0#2206a296 | 自然事件按局内时钟与天气条件出场，条件不成立就没有事件（不造占位事件；:101） | ⚠️ Partial → — | ③ |
| unknown#L0#510e7f85 | 六类自然事件及各自出场条件成表：彩虹（雨后放晴）/萤火虫（夜·草木边）/晚霞（黄昏）/月光湖面（夜·晴）/鸟群蝴蝶（白天）/森林湖鱼群（:103-110） | 🔄 Divergent → — | ③ |
| unknown#L0#76de7605 | 每个自然事件显式标注是否抛印记，环境印记供给印记相册「风景页」（:99、:103-110、:169） | ❌ Missing → — | ③ |
| unknown#L0#60923793 | 事件必须隔着距离也能察觉（声音＋光＋猫身体语言），否则聚不起人（:101、:187） | ⚠️ Partial → — | ③ |
| unknown#L0#e90650f0 | 系统按局内时钟排程周期事件，随机只做小抖动（:145） | ❌ Missing → — | ③ |
| unknown#L0#d3263c52 | 聚鱼时刻的「自然触发」源：森林湖按局内时钟/天气自发涌现鱼群（本册 §5，:114-123） | 🔄 Divergent → — | ③ |
| unknown#L0#dfea7033 | 两种触发进入同一套演出与钓鱼收益，不写两套逻辑（:123、:129） | ✅ Implemented → — | ③ |
| unknown#L0#410998bb | 所有环境状态（时间、天气、事件进度）属于这一局，局末即弃、无跨局存档（:55、:147、:155） | ✅ Implemented → — | ③ |
| unknown#L0#3d6274b2 | 多人同步按局内广播，事件对同局全员同时可见、不存在离线错过（:147、:168） | ✅ Implemented → — | ③ |
| unknown#L0#45c079e7 | 种花：固定花坛点位、当局开花、局末随局清空、不存档不成长（:135） | ❌ Missing → — | ③ |
| unknown#L0#e8107ddc | 森林湖沿途与湖区的特殊看点、风景点、隐藏观察点分布（:89、:181） | ❓ Unverifiable → — | ③ |
| unknown#L0#9598275f | 猫味演出：入夜瞳孔放大、清晨伸懒腰、正午找树荫、雨天炸毛甩水、雨停集体抖毛、看鸟咔咔、扑萤等（:67、:79、:91、:106-110、:125） | ❓ Unverifiable → — | ③ |
| unknown#L0#596426b6 | §7 视觉反馈通道：湖面波纹、鱼跃、鸟群聚集、植物随时段的细节变化（:185） | ❓ Unverifiable → — | ③ |
| unknown#L0#af0f8f16 | 天气与区域各自的抛印记事件④：全员挤在一处躲雨的团子同框（:81）、首次抵达森林湖穿出树林的瞬间（:93） | ❌ Missing → — | ③ |
| unknown#L0#45f5dce2 | 两种触发共用一个稀缺预算，按一个机制记一本账（:123） | ⚠️ Partial → — | ③ |
| unknown#L0#f767c955 | 夜晚不钓鱼、不产鱼情；入夜边界＝不再有新咬钩，进行中的搏斗允许打完（:65、:71；裁决同步/设计修改记录.md:58） | ✅ Implemented → — | ③ |
| unknown#L0#3bb60c2b | 两区域同属「湖畔」主场景（不是两张分开的关卡）（:89） | ✅ Implemented → — | ③ |
| unknown#L0#8640dc09 | 两区域的差异要落在「能遇到什么」上，森林湖的事件偏稀有（:89、:95） | ❌ Missing → — | ③ |
| unknown#L0#3a9aaddf | 每种天气至少绑一个专属演出或专属事件，天气不能只剩装饰（:83） | ⚠️ Partial → — | ③ |
| unknown#L0#c4097186 | 天气变化要同时改画面和声音（晴/雨/雾切换时画面与声音跟着变，:77、:185-186） | ⚠️ Partial → — | ③ |

### Code-only mechanics

描述差异、待核机制变化：以下仅对比文字，由复核／综合核源码与属主裁决后确认。
| 描述来源 | 机制描述 | code_ref |
|---|---|---|
| 本轮独有描述 | 自然聚鱼保留写口：配置事件＋锚点构造系统投料请求、Quantity=1 的 NaturalEvent 场，按 RunId\|DayIndex\|EventId\|AnchorId 去重，走玩家同一提交服务 | Source/Catfishing/Framework/Game/CatfishingGameModeBase.cpp:1965；Source/Catfishing/Framework/Game/CatfishingGameModeBase.cpp:2014；Source/Catfishing/Framework/Game/CatfishingGameModeBase.cpp:2020 |
| 本轮独有描述 | 结算夜使用独立于普通夜晚的月光、天空光、雾和曝光目标；Ended 沿用结算夜目标 | Source/Catfishing/Environment/Presentation/CatEnvironmentPresentationActor.h:18；Source/Catfishing/Environment/Presentation/CatEnvironmentPresentationActor.cpp:244；Source/Catfishing/Environment/Presentation/CatEnvironmentPresentationActor.cpp:518 |
| 本轮独有描述 | 环境总开关与失败输出：未启用、天气 Unknown 或晨昏参数非法时拒绝求值，发布同 Revision 的空环境 | Source/Catfishing/Environment/CatEnvironmentSettings.h:35；Source/Catfishing/Environment/CatEnvironmentSettings.cpp:4；Source/Catfishing/Environment/CatConfiguredEnvironmentProvider.cpp:17；Source/Catfishing/Framework/Game/CatfishingGameModeBase.cpp:1863 |
| 本轮独有描述 | 水域同 ID 多实例或重叠归属拒绝查询，错误为 AmbiguousRegion；不选择最近的一个水域替代 | Source/Catfishing/Environment/CatWaterQuerySubsystem.cpp:62；Source/Catfishing/Environment/CatWaterQuerySubsystem.cpp:112；Source/Catfishing/Environment/CatWaterQuerySubsystem.cpp:206 |
| 本轮独有描述 | 按水域限制活跃＋预占窝料场数量与原始三轴贡献总量，超过上限拒绝投料 | Source/Catfishing/Environment/CatChumFieldSubsystem.cpp:184；Source/Catfishing/Environment/CatChumPlacementService.cpp:197；Config/DefaultGame.ini:356；Config/DefaultGame.ini:357 |
| 基线独有描述 | 画面表现阶段比设计多出「结算夜」与「局末」两档：`ECatEnvironmentPresentationPhase` 有 SettlementNight／Ended，各自有独立的灯光雾色目标 | `Source/Catfishing/Environment/Presentation/CatEnvironmentPresentationActor.h:18-34`；`.cpp:505-524`、`:244-248` |
| 基线独有描述 | 环境可整体关闭并 fail-closed：`bEnableEnvironmentRuntime` 默认 false，天气未配置或晨昏分界非法时 provider 拒绝出快照，Run 发同 Revision 的空环境而不是造一个中性晴天 | `Source/Catfishing/Environment/CatEnvironmentSettings.cpp:4-9`；`Source/Catfishing/Environment/CatConfiguredEnvironmentProvider.cpp:17-21`、`:32-39`；`Source/Catfishing/Framework/Game/CatfishingGameModeBase.cpp:1660-1671`；`Config/DefaultGame.ini:48` |
| 基线独有描述 | 水域是显式几何对象，同 ID 多实例或跨水域重叠一律拒绝（`AmbiguousRegion`），不做「就近归一个」的兜底 | `Source/Catfishing/Environment/CatWaterQuerySubsystem.cpp:62-84`、`:112-141`、`:217-220`；`Source/Catfishing/Environment/CatWaterTypes.h:58-79` |

## 联机社交

未变 17／退步 6／修复 2／其他变化 6／改写 8／锚点漂移 7／新增 19／消失 16

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#1#3be6d1e5 | 房主可踢人——一切社交僵局的兜底，被踢者跟人走资产无损（Knowledge/Design/GDD 系统分册/联机社交.md:84） | ❌ Missing → ⚠️ Partial | ① |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#3#c195bdd8 | 围观互动（舔脸／戳肚皮等）的权限口径归本册（Knowledge/Design/GDD 系统分册/联机社交.md:106） | ❌ Missing → ❓ Unverifiable | ① |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#4#2bc54223 | 被整反制／被抓现场＝抓住肇事猫可扑倒滚成一团（Knowledge/Design/GDD 系统分册/联机社交.md:118） | ❌ Missing → ⚠️ Partial | ① |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#10#6ffc7d29 | 局内图鉴互看默认开放、不做排名（Knowledge/Design/GDD 系统分册/联机社交.md:251） | ✅ Implemented → ⚠️ Partial | ① |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#1#a1fb4e2b | 跨局跟人走＝图鉴、印记相册、外观解锁；装备不跟人走（Knowledge/Design/GDD 系统分册/联机社交.md:84） | 🔄 Divergent → ⚠️ Partial | 锚点漂移（EH0LwpHihiFR8Ukh6s6cXTqUnXc#L0#a1fb4e2b → EH0LwpHihiFR8Ukh6s6cXTqUnXc#1#a1fb4e2b） |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#4#67867764 | 组局基准＝熟人邀请制，不为生人局做额外设计（Knowledge/Design/GDD 系统分册/联机社交.md:186） | 🔄 Divergent → ✅ Implemented | 锚点漂移（EH0LwpHihiFR8Ukh6s6cXTqUnXc#1#67867764 → EH0LwpHihiFR8Ukh6s6cXTqUnXc#4#67867764） |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#7#ee4d1b72 | 仅巨物保留系统全体提示（Knowledge/Design/GDD 系统分册/联机社交.md:219） | ⚠️ Partial → ⚠️ Partial | 锚点漂移（EH0LwpHihiFR8Ukh6s6cXTqUnXc#3#ee4d1b72 → EH0LwpHihiFR8Ukh6s6cXTqUnXc#7#ee4d1b72） |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#10.1#21336cb5 | 倒地救援：搬运回营（Knowledge/Design/GDD 系统分册/联机社交.md:264） | ✅ Implemented → ⚠️ Partial | 锚点漂移（EH0LwpHihiFR8Ukh6s6cXTqUnXc#3#21336cb5 → EH0LwpHihiFR8Ukh6s6cXTqUnXc#10.1#21336cb5） |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#10.1#48a51401 | 巨型鱼多人合力（Knowledge/Design/GDD 系统分册/联机社交.md:265） | ✅ Implemented → ⚠️ Partial | 锚点漂移（EH0LwpHihiFR8Ukh6s6cXTqUnXc#3#48a51401 → EH0LwpHihiFR8Ukh6s6cXTqUnXc#10.1#48a51401） |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#6#e0ee9226 | 白天攒缸、白天不可献；夜里全队到石像前一次性上供，上供当场结算（Knowledge/Design/GDD 系统分册/联机社交.md:140） | ✅ Implemented → ✅ Implemented | 锚点漂移（EH0LwpHihiFR8Ukh6s6cXTqUnXc#2#e0ee9226 → EH0LwpHihiFR8Ukh6s6cXTqUnXc#6#e0ee9226） |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#10.1#bb7255e8 | 多人口子“抢抄”归本册：抄网挥空只剩“没够着”与“被抢先”两种（Knowledge/Design/GDD 系统分册/联机社交.md:266） | ✅ Implemented → ✅ Implemented | 锚点漂移（EH0LwpHihiFR8Ukh6s6cXTqUnXc#1#bb7255e8 → EH0LwpHihiFR8Ukh6s6cXTqUnXc#10.1#bb7255e8） |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#4#1a8efd0e | 拒绝做成演出：想整蛊立牌者会强制进入小动画“有『玩家名』钓鱼真好，我就不添乱了”（Knowledge/Design/GDD 系统分册/联机社交.md:118） | ⚠️ Partial → ⚠️ Partial | ② 措辞改写 |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#5#5708745b | 图鉴在钓到瞬间已记录，被拿走不丢收集（Knowledge/Design/GDD 系统分册/联机社交.md:130） | ✅ Implemented → ⚠️ Partial | ② 措辞改写 |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#1#9e9e28dc | 负面互动上限＝单条鱼被拿走：不可拿他人装备、不可破坏他人图鉴与相册（Knowledge/Design/GDD 系统分册/联机社交.md:63） | ✅ Implemented → 🔄 Divergent | ② 措辞改写 |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#4#0b76137b | 臭臭鱼“请勿靠近”：吃下后 90 秒周身臭气，无法被恶作剧选中、无法获得帮助和交互；搬运也屏蔽，猫之间扑倒不属恶作剧、不屏蔽（Knowledge/Design/ | ❌ Missing → 🔄 Divergent | ② 措辞改写 |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#3.2#f834610d | 社交行为的同步判定（拿鱼归属、恶作剧命中、猫之间扑倒）服务器权威（Knowledge/Design/GDD 系统分册/联机社交.md:169） | ✅ Implemented → ⚠️ Partial | ② 措辞改写 |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#7#ed6f116a | 拿鱼可见性：叼鱼逃跑的猫必须全场显眼，嘴里始终叼着一条明晃晃的鱼（Knowledge/Design/GDD 系统分册/联机社交.md:220） | ❌ Missing → ⚠️ Partial | ② 措辞改写 |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#5#ec041b8a | 拿了就吃：进食花时间，吃完不可逆；获得成长（buff＋经验），不设追回窗口（Knowledge/Design/GDD 系统分册/联机社交.md:130） | ⚠️ Partial → ⚠️ Partial | ② 措辞改写 |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#5#7a451974 | 拿了就卖：叼鱼奔向商店卖掉、钱进公款，跑路提供追逐时间，不设追回窗口（Knowledge/Design/GDD 系统分册/联机社交.md:130） | ❌ Missing → ✅ Implemented | ② 措辞改写 |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#4#e146c37b | 局制红线：局末世界清空，物资跟局走；终局槽标记已完结、不再继续，开新局另建槽，不自动删档（Knowledge/Design/GDD 系统分册/联机社交.md:1 | — → ✅ Implemented | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#4#80ee4f90 | 单人局不刷需要多人的鱼；面向多人的门槛内容不算单人义务（Knowledge/Design/GDD 系统分册/联机社交.md:179） | — → ⚠️ Partial | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#3#c68fd147 | 求助全手动：玩家自己喊，普通求助只在附近可感知、不自动分配任务（Knowledge/Design/GDD 系统分册/联机社交.md:106） | — → ⚠️ Partial | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#3#a67cf0ff | 旁观位不做——围观是自由行为，不设专属机制（Knowledge/Design/GDD 系统分册/联机社交.md:106） | — → ✅ Implemented | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#10.1#81748516 | 拽尾巴救援：钓手被拖拽时的介入动作（Knowledge/Design/GDD 系统分册/联机社交.md:265） | — → ❌ Missing | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#4#7da6f89a | 不设系统级频率上限与时机限制（熟人自治）（Knowledge/Design/GDD 系统分册/联机社交.md:118） | — → ✅ Implemented | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#4#66e108f0 | 防骚扰牌子：玩家在钓点立牌后，保护范围内不能被整蛊（Knowledge/Design/GDD 系统分册/联机社交.md:118） | — → ⚠️ Partial | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#5#bcbbc2f8 | 立牌只挡恶作剧，不保护鱼；立牌者的鱼照样可被拿（Knowledge/Design/GDD 系统分册/联机社交.md:130） | — → ✅ Implemented | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#5#7f6c2f9d | 拿鱼对象＝他人落地鱼护与营地共用大鱼缸；够得着即可取，不作犯罪判定（Knowledge/Design/GDD 系统分册/联机社交.md:130） | — → ⚠️ Partial | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#5#c7a7b25d | 不设自动追回窗口、强制物归原主或偷鱼专属反制；只按客观猫动作处理（Knowledge/Design/GDD 系统分册/联机社交.md:130） | — → ✅ Implemented | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#313821a3 | 翻天＝全员到场视为同意，任一人发起，3 秒倒计时内合格玩家离圈即中断，不逐人按准备（Knowledge/Design/设计修改记录.md:327） | — → ✅ Implemented | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#7#dd649389 | 归属可见：鱼带主人色环（Knowledge/Design/GDD 系统分册/联机社交.md:218） | — → ⚠️ Partial | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#5#4b5a6632 | 从他人鱼护或共享缸拿鱼，一嘴只能叼一条（Knowledge/Design/GDD 系统分册/联机社交.md:130） | — → ⚠️ Partial | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#4#fcbc18ec | 抓、拖、推队友可直接影响位移，关键搏斗期也不豁免；被推入水仍按基础落水规则（Knowledge/Design/GDD 系统分册/联机社交.md:186） | — → ✅ Implemented | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#7#4472aac2 | 窝料带施作者爪印标识，全场看得见谁的窝（Knowledge/Design/GDD 系统分册/联机社交.md:218） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#487c62af | 换人不设体力准入；无人值守竿可被任何猫接管且同样不设体力门槛（Knowledge/Design/设计修改记录.md:396） | — → 🔄 Divergent | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#4#f42ca5e1 | 社交感知优先靠猫的肢体语言和声音传达，少用文本浮窗（Knowledge/Design/GDD 系统分册/联机社交.md:187） | — → ❓ Unverifiable | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#6#2ea60705 | 石像位于湖岸、面朝湖心，形成全队上供的场景中心（Knowledge/Design/GDD 系统分册/联机社交.md:140） | — → ❓ Unverifiable | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#7#5c1b4438 | 咬钩信号远距离可感知，声音与猫身体语言让其他玩家察觉（Knowledge/Design/GDD 系统分册/联机社交.md:219） | — → ⚠️ Partial | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#1#2b0783ff | 局制红线：局末世界清空（营地、渔获、祭坛进度归零），物资跟局走（联机社交.md:84、:178） | ⚠️ Partial → — | ③ |
| A6，联机社交#L0#80ee4f90 | 单人局不刷需要多人的鱼；面向多人的门槛内容不算单人义务（A6，联机社交.md:179） | ✅ Implemented → — | ③ |
| B6，联机社交#L0#c68fd147 | 求助全手动：玩家自己喊，普通求助只在附近可感知、不自动分配任务（B6，联机社交.md:106、:219） | ✅ Implemented → — | ③ |
| B7 已消题，联机社交#L0#a67cf0ff | 旁观位不做——围观是自由行为，不设专属机制（B7 已消题，联机社交.md:106） | ✅ Implemented → — | ③ |
| 钓手被拖拽时的介入动作，联机社交#L0#05bdbeca | 拽尾巴救援（钓手被拖拽时的介入动作，联机社交.md:106、:265 标为「已接通」） | ❌ Missing → — | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#4#1f61df89 | 恶作剧社交规则＝权限开关（谁能整谁、什么时候能整），熟人局不做生人局额外权限（联机社交.md:118、:186） | ✅ Implemented → — | ③ |
| unknown#L0#753ae4f8 | 不设系统级频率上限与时机限制（熟人自治；B4 关账，联机社交.md:118、:209） | 🔄 Divergent → — | ③ |
| B4，联机社交#L0#66e108f0 | 防骚扰牌子：玩家在钓点立牌后，保护范围内不能被整蛊（B4，联机社交.md:118） | ✅ Implemented → — | ③ |
| 2026-08-16 拍定，联机社交#L0#0874fb23 | 立牌＝完整免打扰，同时豁免偷窃（2026-08-16 拍定，联机社交.md:118、:130） | 🔄 Divergent → — | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#5#074b9a64 | 偷的对象＝他人鱼护与营地共用大鱼缸两类容器（联机社交.md:130） | 🔄 Divergent → — | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#5#f43b3805 | 被抓现场：物归原主（联机社交.md:130） | ⚠️ Partial → — | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#5#86577d5e | 被抓现场双方进印记（「偷鱼未遂」名场面）（联机社交.md:130、:134） | ⚠️ Partial → — | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#2#4c4bbd7e | 翻天＝全队同意的开关，个人不能单独翻天（联机社交.md:73，2026-09-04 连带三） | ✅ Implemented → — | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#7#879ff3a2 | 共同印记分发：多人入镜的印记照片进每个入镜者的相册（联机社交.md:152、:110、:144） | ✅ Implemented → — | ③ |
| 已拍定，联机社交#L0#dd649389 | 归属可见：鱼带主人色环（已拍定，联机社交.md:218） | ❌ Missing → — | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#1#9772cf2d | 负面互动的权限前提：偷取有权限开关（联机社交.md:63） | ✅ Implemented → — | ③ |

### Code-only mechanics

描述差异、待核机制变化：以下仅对比文字，由复核／综合核源码与属主裁决后确认。
| 描述来源 | 机制描述 | code_ref |
|---|---|---|
| 本轮独有描述 | 手动求助半径固定 500cm、6s 冷却，冷却未就绪会关闭求助入口 | Config/DefaultGame.ini:390；Config/DefaultGame.ini:391；Source/Catfishing/Social/CatSocialSettings.cpp:23；Source/Catfishing/Social/CatSocialService.cpp:210 |
| 本轮独有描述 | 立牌 200cm 半径、80cm 放置距离，恶作剧 120cm 触达 | Config/DefaultGame.ini:387；Config/DefaultGame.ini:388；Config/DefaultGame.ini:389 |
| 本轮独有描述 | Profile 持久化功能装备槽位选择与解锁资格门 | Source/Catfishing/Profile/CatProfileSaveGame.h:52；Source/Catfishing/Profile/CatProfileSubsystem.cpp:127 |
| 本轮独有描述 | 已废偷鱼协议留下的命名资产与协议墓碑注释 | Source/Catfishing/Framework/Game/CatfishingPlayerController.h:199；Source/Catfishing/Social/CatSocialTypes.h:56 |
| 本轮独有描述 | 一份 LastHelpSignal 覆盖所有玩家/事件求助 | Source/Catfishing/Framework/Game/CatfishingGameState.cpp:122；Source/Catfishing/Social/CatSocialService.cpp:223；Source/Catfishing/Social/CatSocialService.cpp:249 |
| 本轮独有描述 | 普通倒地状态统一拒绝双方恶作剧，搬运前直接传送到营地 | Source/Catfishing/Social/CatSocialService.cpp:78；Source/Catfishing/Social/CatSocialService.cpp:269；Source/Catfishing/Camp/CatCampHubActor.cpp:215 |
| 本轮独有描述 | 用漂讯稳定度阈值 1.0 代替铃铛漂类型，决定是否全场广播 | Config/DefaultGame.ini:282；Source/Catfishing/Fishing/CatFishingSession.cpp:2105 |
| 基线独有描述 | 猫与猫之间的物理抓握与互推：左右键在非主控状态下驱动 `UCatPhysicsGrabComponent`，抓握目标解析到对方 Actor 上的 `UCatPhysicalBodyComponent` 后直接施加牵引力，等于一只猫能抓住、拖动、推开另一只猫 | `Source/Catfishing/Interaction/Grab/CatPhysicsGrabComponent.cpp:587-635`；`Source/Catfishing/AbilitySystem/Input/CatAbilityInputBindingComponent.h:14`、`.cpp:99-141`；`Source/Catfishing/Character/CatCharacter.cpp:81`、`:326` |
| 基线独有描述 | 普通库存转移可绕过整套偷鱼协议：`ServerMoveInventoryItemBetweenHosts` 只查距离与「鱼护是否在地面」，不查鱼的归属，任何玩家都能把地面鱼护或共享大鱼缸里的鱼直接搬进自己背包——没有追回窗口、没有进食计时、没有物归原主、没有印记 | `Source/Catfishing/Framework/Game/CatfishingPlayerController.h:126`；`Source/Catfishing/Inventory/CatInventoryStatics.cpp:214-282`；`Source/Catfishing/Inventory/CatInventoryAccessRules.cpp:26-36`；对比 `Source/Catfishing/Social/CatSocialService.cpp:65-180` |
| 基线独有描述 | 局末不作废存档断点：Run 判出终局（`WorldProgressDepleted`／`Success`）后没有任何路径作废世界快照，同一存档槽再读会把已结束的局连同营地仓库、世界鱼容器、天数、世界进度一起恢复 | `Source/Catfishing/Framework/Game/CatfishingGameModeBase.cpp:1142-1151`、`:1160-1162`；`Source/Catfishing/Save/CatSaveSubsystem.cpp:938-990`、`:560`；`Source/Catfishing/Save/CatRunSaveGame.h:247`、`:263`、`:279`、`:283` |

## 营地与局进程

未变 39／退步 6／修复 1／其他变化 6／改写 0／锚点漂移 2／新增 14／消失 3

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| EE9vwzWE2iHVXGkduzGcEx4bnjh#3#d8332a9b | 局内图鉴是营地第二件公共陈列：全队钓到新鱼自动上页、盖登记归属爪印、跟局走局末清空（营地.md:69） | ❌ Missing → ⚠️ Partial | ① |
| EE9vwzWE2iHVXGkduzGcEx4bnjh#2#a8ae4f3e | 鱼缸容量首版：初始 10 条、两档升级 20／30 条、价 300／700 金币、用公款在商人猫处升级、升级随局清空（营地.md:61；局与进程.md:111） | 🔄 Divergent → ⚠️ Partial | ① |
| EE9vwzWE2iHVXGkduzGcEx4bnjh#4#48ed48f7 | 翻天后全员在营地醒来，未获救的倒地者自动救起（营地.md:77；局与进程.md:95） | ❌ Missing → ⚠️ Partial | ① |
| EE9vwzWE2iHVXGkduzGcEx4bnjh#5#42db7fd0 | 篝火：入夜即自动点亮、整夜都在、想去就去不阻塞流程，坐下松弛并可叠罗汉（营地.md:85） | 🔄 Divergent → ⚠️ Partial | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#a2a8f7fa | 天循环五步：清晨定任务 → 白天限时 → 到点入夜 → 夜晚无限 → 石像互动结算翻天（局与进程.md:40） | ✅ Implemented → ⚠️ Partial | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#d2e81024 | 祭坛是石像前的一块地不是容器：鱼一趟趟叼过去扔上就算摆好，互动前随时可叼回（局与进程.md:60；营地.md:42） | ✅ Implemented → ⚠️ Partial | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#5#4cebd0f8 | 倒地的猫豁免、不计入到场判定，队友仍可代献其鱼护里的鱼（局与进程.md:95；营地.md:42） | ✅ Implemented → ⚠️ Partial | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#f7131bf0 | 每日任务按清晨在场人数确定，当天中途有人加入或退出都不重算（局与进程.md:40） | ❌ Missing → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#b873ed43 | 任务逐日变大（局与进程.md:58） | ✅ Implemented → ⚠️ Partial | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#4#64530c75 | 毕业：把进度顶到 100% 的那一次上供不翻天，当场转成结算夜，玩家自己决定何时离开（局与进程.md:91） | ✅ Implemented → ⚠️ Partial | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#4#82b805f0 | 失败＝世界进度归零，这一局立刻结束，不进结算夜（局与进程.md:91） | ✅ Implemented → ⚠️ Partial | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#3#8ea2b678 | 局末结算用非排序趣味头衔加全员合影（局与进程.md:87） | ❌ Missing → ⚠️ Partial | ① |
| EE9vwzWE2iHVXGkduzGcEx4bnjh#4#f5b6d781 | 篝火是营地五件固定设施之一，摆在场景里（营地.md:103） | ❌ Missing → ❓ Unverifiable | ① |
| EE9vwzWE2iHVXGkduzGcEx4bnjh#10#78140d73 | 营地公共仓库（团队装备库实体，商店发货落点）（营地.md:149） | ✅ Implemented → ✅ Implemented | 锚点漂移（EE9vwzWE2iHVXGkduzGcEx4bnjh#1#78140d73 → EE9vwzWE2iHVXGkduzGcEx4bnjh#10#78140d73） |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#7#a9c1c3b8 | 进度涨跌看得见：夜晚祭坛即进度条、森林随进度复苏或枯萎（局与进程.md:146） | ⚠️ Partial → ⚠️ Partial | 锚点漂移（Ca49ww71ai2IAlklmsZcqF7Tnzb#4#a9c1c3b8 → Ca49ww71ai2IAlklmsZcqF7Tnzb#7#a9c1c3b8） |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#dd24efe5 | 鱼缸与地面鱼护里的鱼可被任何够得着的猫拿取，一嘴一条；不问归属，不设追回窗口或偷鱼专属协议（Knowledge/Design/设计修改记录.md:305） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#4e0cb6a8 | 掉地未拾取物品翻天时消失，当天内扔下仍可赠送（Knowledge/Design/设计修改记录.md:231） | — → ⚠️ Partial | ③ |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#9f3c1a55 | 当前仍在局里的玩家到齐后，任一人与石像互动发起可打断倒计时，结束后批量消耗祭坛鱼、结算并按进度选择翻天或终局（局与进程.md:60） | — → ⚠️ Partial | ③ |
| Xg1EwylUuiUeVckeLwtcMXMjnAg#当前拍定参数#315c67dd | 白天正式时长为 10 分钟（Knowledge/Design/数值模拟与参数记录.md:46） | — → 🔄 Divergent | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#b6d8949e | 祭坛倒计时为 3 秒，任一合格玩家离开到场圈就中断（Knowledge/Design/设计修改记录.md:327） | — → ✅ Implemented | ③ |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#7#9e2febe3 | 游戏不自动删档；终局槽标为已完结且不可继续，开新局另建新槽（局与进程.md:103） | — → ⚠️ Partial | ③ |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#090c56e3 | 互动时才锁定祭坛鱼，同刻拿鱼按服务器判序，互动前可搬进搬出（局与进程.md:60） | — → 🔄 Divergent | ③ |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#dfee8e69 | 正式玩法中五段天循环及毕业、失败的终点分支生效（局与进程.md:44） | — → ❓ Unverifiable | ③ |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#10420330 | 任务增长应由吃鱼成长与换竿带动产能，按约九成完成率校准（局与进程.md:75） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#e4cbcdd3 | 祭坛信息牌删已确认人数，显示倒计时条（Knowledge/Design/设计修改记录.md:313） | — → ✅ Implemented | ③ |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#8#40cc4898 | 单人与多人共用局结构，单人不刷新需要多人的巨型鱼（局与进程.md:107） | — → ⚠️ Partial | ③ |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#4#a6d4face | 毕业结算夜有商人收摊、剩余公款兑换小鱼干，保留庆祝与篝火合影（局与进程.md:91） | — → ⚠️ Partial | ③ |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#7#92575038 | 缸存量告急时向全场发出氛围信号（局与进程.md:146） | — → ⚠️ Partial | ③ |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#4#a6bcee43 | 翻天与失败都要有猫味演出，失败演出压在进度归零时（局与进程.md:124；局与进程.md:26） | — → ⚠️ Partial | ③ |
| EE9vwzWE2iHVXGkduzGcEx4bnjh#2#d5795574 | 鱼缸与鱼护里的鱼可被偷（营地.md:61,115） | ✅ Implemented → — | ③ |
| EE9vwzWE2iHVXGkduzGcEx4bnjh#1#ecdfaf16 | 地上未被拾取的物品次日清晨出现在营地（营地.md:54） | ❌ Missing → — | ③ |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#31483528 | 当前仍在局里的玩家到齐后与石像互动一次，祭坛上的鱼一起消失、当场结算、天亮（局与进程.md:44,60,95；营地.md:42,61） | ⚠️ Partial → — | ③ |

### Code-only mechanics

描述差异、待核机制变化：以下仅对比文字，由复核／综合核源码与属主裁决后确认。
| 描述来源 | 机制描述 | code_ref |
|---|---|---|
| 本轮独有描述 | 十天后永远复用最后一天任务、涨幅和降幅 | Source/Catfishing/Run/CatRunSettings.cpp:44 |
| 本轮独有描述 | 供奉进度按整数四舍五入 | Source/Catfishing/AbilitySystem/Executions/CatRunSettleOfferingExecutionCalculation.cpp:115 |
| 本轮独有描述 | 任务额外倍率/压力，世界进度涨跌倍率 | Source/Catfishing/Framework/Game/CatfishingGameModeBase.cpp:1044；Source/Catfishing/AbilitySystem/Executions/CatRunSettleOfferingExecutionCalculation.cpp:110 |
| 本轮独有描述 | 旧 CampfirePlayback 合影封面候选协议与新坐下篝火并存 | Source/Catfishing/Camp/CatCampHubActor.cpp:299；Source/Catfishing/AbilitySystem/BodyAction/Camp/CatCampBodyActionAbilities.cpp:261；Config/DefaultGame.ini:205 |
| 本轮独有描述 | 旧容器注册表持久化与正式鱼缸库存并存 | Source/Catfishing/FishContainers/CatFishContainerService.cpp:802；Source/Catfishing/FishContainers/CatFishTankActor.cpp:30；Source/Catfishing/Save/CatSaveSubsystem.cpp:759 |
| 本轮独有描述 | 失败收口也经过毕业余款兑换入口 | Source/Catfishing/Framework/Game/CatfishingGameModeBase.cpp:1210；Source/Catfishing/ShopEconomy/CatShopEconomyService.cpp:695 |
| 基线独有描述 | 翻天过场的三段时序与全员操作锁：淡出／黑屏停留／淡入三段秒数按祭坛实例配置并由服务器冻结发布，期间禁用移动、压入阻断输入、关背包与主菜单，失败时另给 2 秒错误提示后解锁 | Source/Catfishing/Camp/CatAltarActor.h:66-74；Source/Catfishing/Framework/Core/CatRunContracts.h:159-208；Source/Catfishing/Framework/Game/CatfishingGameModeBase.cpp:1205-1348；Source/Catfishing/Framework/Game/CatfishingPlayerController.cpp:120-190 |
| 基线独有描述 | 祭坛信息牌的五个数：世界进度、今日任务、缸内储备、本次地面待献点数（服务器 0.2 秒低频预览）、已确认 N/M；另有上一次结算凭据「x/y 点·达标」与「a% → b%」 | Source/Catfishing/Camp/CatAltarActor.cpp:245-268；Source/Catfishing/UI/WorldInfo/CatAltarWorldInfoComponent.cpp:46-97 |
| 基线独有描述 | Run 侧四个可调倍率属性：DailyOfferingTargetMultiplier、DailyPressure（乘当日任务目标）、WorldProgressGainMultiplier、WorldProgressLossMultiplier（乘涨跌幅），默认全 1.0、只能由 GE 资产写 | Source/Catfishing/AbilitySystem/Attributes/CatRunModifierAttributeSet.h:24-31；Source/Catfishing/AbilitySystem/Executions/CatRunStartDayExecutionCalculation.cpp:40-53；Source/Catfishing/AbilitySystem/Executions/CatRunSettleOfferingExecutionCalculation.cpp:105-125 |
| 基线独有描述 | 共享鱼缸的入缸准入闸门：只有鱼定义上标了 `bTankDisplayEligible` 的鱼才能进缸，未标的整条拒绝（PolicyUndecided）；鱼护无此限制 | Source/Catfishing/FishContainers/CatFishContainerService.cpp:113-135；Source/Catfishing/Data/CatFishDefinition.h:183 |
| 基线独有描述 | 多槽位世界存档：槽 ID、玩家自取的显示名、最后保存 UTC、累计游玩秒数、地点短名，以及 180 秒一次的自动检查点 | Source/Catfishing/Save/CatRunSaveGame.h:132-177,222-243；Source/Catfishing/Save/CatSaveSettings.h:14-18 |

## 道具

未变 30／退步 7／修复 3／其他变化 7／改写 1／锚点漂移 2／新增 25／消失 15

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| VVHrw3FAui78wZkjuKZcLHBdnEc#1#c2221a84 | 携带是格数制：背包格数有限，竿/漂/抄网各占一格，饵与窝料按份计（Knowledge/Design/GDD 系统分册/道具/道具.md:62） | ✅ Implemented → ⚠️ Partial | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#1#4b648332 | 队友装备靠世界表现看，凑近闻是同一件事的猫式演出；备装动作全是猫的（扒拉竿、拍饵袋、叼竿甩背）（Knowledge/Design/GDD 系统分册/道具/道具. | ❌ Missing → ❓ Unverifiable | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#1#bfcf8f48 | 鱼护＝随身渔获容器、两态、占一格、非必带、鱼随护走、放下的全队可开可拾（Knowledge/Design/GDD 系统分册/道具/道具.md:64） | ✅ Implemented → ⚠️ Partial | ① |
| RBHPwKudEiwItbko3HvcFPCenom#r7#47a85570 | 鱼护最多可以装 6 条鱼（Knowledge/Design/GDD 系统分册/道具/道具总表/道具.csv 第 7 行 裁决备注（2026-08-19）） | 🔄 Divergent → ⚠️ Partial | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#1#f355afbf | 叼鱼态：嘴里只能叼一条（Knowledge/Design/GDD 系统分册/道具/道具.md:64） | ✅ Implemented → ⚠️ Partial | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#1#1c0f130d | 叼着鱼不能抛竿（Knowledge/Design/GDD 系统分册/道具/道具.md:64） | ❌ Missing → ✅ Implemented | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#1#78103cc0 | 叼着鱼不能再抄或拾第二条（Knowledge/Design/GDD 系统分册/道具/道具.md:64） | ✅ Implemented → ⚠️ Partial | ① |
| RBHPwKudEiwItbko3HvcFPCenom#r6#5a43ef10 | 抄网商店购买、20 金币（Knowledge/Design/GDD 系统分册/道具/道具总表/道具.csv 第 6 行 价格） | ⚠️ Partial → ❓ Unverifiable | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#2#326a767d | 窝料决定上钩的是哪一类鱼，鱼饵决定是哪一条（属性命中后按偏好权重再选一次）（Knowledge/Design/GDD 系统分册/道具/道具.md:83） | ✅ Implemented → 🔄 Divergent | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#2#7d73efc6 | 普通饵营地免费自取（保底三件之一）（Knowledge/Design/GDD 系统分册/道具/道具.md:85） | ⚠️ Partial → ❓ Unverifiable | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#2#eb543d4f | 普通饵携带上限 8 份（2026-08-19 由 5 改）（Knowledge/Design/GDD 系统分册/道具/道具.md:85） | 🔄 Divergent → ✅ Implemented | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#2#f8e5b5aa | 特殊饵商店购买、每日进货限量（Knowledge/Design/GDD 系统分册/道具/道具.md:85） | ⚠️ Partial → ❓ Unverifiable | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#7#945eccd8 | 每种窝料一组配方 `{腥, 香, 酵}`，三个非负整数，允许某一类为零（Knowledge/Design/GDD 系统分册/道具/道具.md:147） | ✅ Implemented → 🔄 Divergent | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#7#9a15326d | 窝料携带上限 5 份（Knowledge/Design/GDD 系统分册/道具/道具.md:147） | ⚠️ Partial → ✅ Implemented | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#4#4e39deb6 | 装备不可被偷：好友只可以偷鱼（Knowledge/Design/GDD 系统分册/道具/道具.md:204） | ✅ Implemented → 🔄 Divergent | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#4#bda10976 | 物品当天不灭失、跨天不保留（翻天时一并消失）；水面有空气墙，物品丢不进湖里（Knowledge/Design/GDD 系统分册/道具/道具.md:205） | ❌ Missing → ⚠️ Partial | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#3.3#2c384c47 | 耐久不可修复；竿耐久对道具「回补封闭」（Knowledge/Design/GDD 系统分册/道具/道具.md:187） | ✅ Implemented → ✅ Implemented | 锚点漂移（VVHrw3FAui78wZkjuKZcLHBdnEc#3.2#2c384c47 → VVHrw3FAui78wZkjuKZcLHBdnEc#3.3#2c384c47） |
| VVHrw3FAui78wZkjuKZcLHBdnEc#10#2c2e8a8c | 三选一成长项「窝料携带 +2」「鱼饵携带 +2」「背包格数 +1」作用于本册携带字段（Knowledge/Design/GDD 系统分册/道具/道具.md:26 | ❌ Missing → ⚠️ Partial | 锚点漂移（VVHrw3FAui78wZkjuKZcLHBdnEc#7#2c2e8a8c → VVHrw3FAui78wZkjuKZcLHBdnEc#10#2c2e8a8c） |
| VVHrw3FAui78wZkjuKZcLHBdnEc#1#f653e408 | 湖心巨影：任何抄网都抄不动；可经碾压或消耗战收鱼（Knowledge/Design/GDD 系统分册/道具/道具.md:69） | ❓ Unverifiable → ❓ Unverifiable | ② 措辞改写 |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#b3e9b0ad | 鱼饵表 5 行（虫虫／肉块／果实／花蜜／月光）× 6 列落进资产与运行目录（Knowledge/Design/GDD 系统分册/道具/鱼饵/Sheet1.csv | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#f2620c0a | 射程分 10／15／20 米档（Knowledge/Design/GDD 系统分册/道具/鱼漂/Sheet1.csv 第 2 行 射程（米）） | — → ❓ Unverifiable | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#36110aae | 精准度高／中／低＝落点精准（Knowledge/Design/GDD 系统分册/道具/鱼漂/Sheet1.csv 第 2 行 精准度） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r4#dc30d698 | 铃铛漂咬钩铃响、全场可闻（Knowledge/Design/GDD 系统分册/道具/鱼漂/Sheet1.csv 第 4 行 特效） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#225a03f4 | 羽毛漂 0 营地免费自取、毛线球 10／铃铛 15 公款购买（Knowledge/Design/GDD 系统分册/道具/鱼漂/Sheet1.csv 第 2 行  | — → ❓ Unverifiable | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#5ad975bb | 窝料表 4 行（虫虫窝／花果香窝／发酵谷物窝／巨鱼窝）× 9 列落进资产与运行目录（Knowledge/Design/GDD 系统分册/道具/窝料/Sheet1 | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#8f059308 | 三档竿：树枝竿／玩具竿／逗猫棒竿（Knowledge/Design/GDD 系统分册/道具/鱼竿/Sheet1.csv 第 2 行 鱼竿） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#be9ba0b1 | 竿另有「强度」参数参与搏斗判定（25／60／210）（Knowledge/Design/GDD 系统分册/道具/鱼竿/Sheet1.csv 第 2 行 强度） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#ededd94f | 三档竿耐久 40／70／120（Knowledge/Design/GDD 系统分册/道具/鱼竿/Sheet1.csv 第 2 行 耐久） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#34234101 | L_max 放线上限 60／80／100 米（Knowledge/Design/GDD 系统分册/道具/鱼竿/Sheet1.csv 第 2 行 L_max 放线 | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r6#5db9a26f | 耐久损耗分层案之一：每钓一条固定 -1 基础磨损（搏斗以渔获结束即扣，渔网产出不扣）（Knowledge/Design/GDD 系统分册/道具/鱼竿/Sheet | — → ✅ Implemented | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r6#26cc6c51 | 耐久损耗分层案之二：线绷紧的每一秒 -= 鱼力×0.05（Knowledge/Design/GDD 系统分册/道具/鱼竿/Sheet1.csv 第 6 行 鱼竿 | — → ⚠️ Partial | ③ |
| RBHPwKudEiwItbko3HvcFPCenom#r11#4ea3e2a9 | 社交演出三件：湿毛器／恶作剧假鱼／响响筒，零数值副作用（Knowledge/Design/GDD 系统分册/道具/道具总表/道具.csv 第 11 行 作用） | — → ❌ Missing | ③ |
| RBHPwKudEiwItbko3HvcFPCenom#r14#3fbba680 | 渔网特例：500 金币、按落点窝点概率 roll 5~8 条、稀有不加权、湖心巨影不入网、随身限 1、每队每局限购、用后 30 秒死水（Knowledge/De | — → ❌ Missing | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#5#1574ccf0 | 解锁清单跟人走，件与公款跟局走；装备里程碑解锁为完整版预留（Knowledge/Design/GDD 系统分册/道具/道具.md:123） | — → ⚠️ Partial | ③ |
| RBHPwKudEiwItbko3HvcFPCenom#r7#fb8ae890 | 鱼护商店购买，价格 5 金币（Knowledge/Design/GDD 系统分册/道具/道具总表/道具.csv 第 7 行 价格） | — → ❓ Unverifiable | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#2#eb637efb | 特殊饵消耗却未钓到目标鱼时，图鉴出现该鱼虚影以揭开线索层（Knowledge/Design/GDD 系统分册/道具/道具.md:87） | — → ⚠️ Partial | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#2#ae5bc5d8 | 连续落空渐进提高下次机会（Knowledge/Design/GDD 系统分册/道具/道具.md:87） | — → ❌ Missing | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#6#1cfb3833 | 用对饵能看到目标鱼影，用错饵时鱼影绕钩并嫌弃摆尾离开（Knowledge/Design/GDD 系统分册/道具/道具.md:135） | — → ⚠️ Partial | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#3#2a9e148c | 基础咬钩信号不装任何漂也成立；铃铛漂只增加全场公共化（Knowledge/Design/GDD 系统分册/道具/道具.md:101） | — → 🔄 Divergent | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#10#f0c1bc32 | 断竿演出按树枝竿／玩具竿／逗猫棒竿分支（Knowledge/Design/GDD 系统分册/道具/道具.md:271） | — → ⚠️ Partial | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#10#83c1ddd3 | 道具图鉴以五张表为数据本体，不设线索层，也没有吃这一层（Knowledge/Design/GDD 系统分册/道具/道具.md:269） | — → ❌ Missing | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#84298191 | 鱼漂不消耗（Knowledge/Design/设计修改记录.md:287） | — → ✅ Implemented | ③ |
| Sisrw4FzWiL2AekXetFc7Vxunqg#2#ae49d3b9 | 成长的竿耐久磨损减免：每次-10%、加算上限-50%、第5次选择起出现，同时作用于每条-1与绷线磨损，减耗不回补（Knowledge/Design/GDD 系统 | — → ⚠️ Partial | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#3#bf1fea9f | 鱼漂专属猫式演出：从背包翻出、新漂到手拍两下再系上、猫尾尖跟随漂起伏（Knowledge/Design/GDD 系统分册/道具/道具.md:99） | — → ❓ Unverifiable | ③ |
| unknown#L0#b3e9b0ad | 鱼饵表 5 行（虫虫／肉块／果实／花蜜／月光）× 6 列落进资产与运行目录（鱼饵/Sheet1.csv 第2-6行） | ⚠️ Partial → — | ③ |
| unknown#L0#f2620c0a | 射程分 10／15／20 米档（鱼漂/Sheet1.csv 第2-4行 射程列；2026-09-09 由 3／5／7 改表） | ❓ Unverifiable → — | ③ |
| unknown#L0#36110aae | 精准度高／中／低＝落点精准（鱼漂/Sheet1.csv 第2-4行 精准度列） | ⚠️ Partial → — | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#3#dc30d698 | 铃铛漂咬钩铃响、全场可闻（道具.md:97,101；鱼漂/Sheet1.csv 第4行 特效列） | ❌ Missing → — | ③ |
| unknown#L0#225a03f4 | 羽毛漂 0 营地免费自取、毛线球 10／铃铛 15 公款购买（鱼漂/Sheet1.csv 第2-4行 价格列／获取方式列） | ⚠️ Partial → — | ③ |
| unknown#L0#5ad975bb | 窝料表 4 行（虫虫窝／花果香窝／发酵谷物窝／巨鱼窝）× 9 列落进资产与运行目录（窝料/Sheet1.csv 第2-5行；第1行 表头） | ⚠️ Partial → — | ③ |
| unknown#L0#8f059308 | 三档竿：树枝竿／玩具竿／逗猫棒竿（鱼竿/Sheet1.csv 第2-4行 鱼竿列／档位别名列） | ⚠️ Partial → — | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#3.2#be9ba0b1 | 竿另有「强度」参数参与搏斗判定（25／60／210）（道具.md:161；鱼竿/Sheet1.csv 第2-4行 强度列，第7行） | 🔄 Divergent → — | ③ |
| unknown#L0#ededd94f | 三档竿耐久 40／70／120（鱼竿/Sheet1.csv 第2-4行 耐久列） | 🔄 Divergent → — | ③ |
| unknown#L0#34234101 | L_max 放线上限 60／80／100 米（鱼竿/Sheet1.csv 第2-4行 L_max 列） | ⚠️ Partial → — | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#3.2#5db9a26f | 耐久损耗分层案之一：每钓一条固定 -1 基础磨损（搏斗以渔获结束即扣，渔网产出不扣）（道具.md:161；鱼竿/Sheet1.csv 第6行） | ❌ Missing → — | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#3.2#26cc6c51 | 耐久损耗分层案之二：线绷紧的每一秒 -= 鱼力×0.05（道具.md:161；鱼竿/Sheet1.csv 第6行） | 🔄 Divergent → — | ③ |
| unknown#L0#4ea3e2a9 | 社交演出三件：湿毛器／恶作剧假鱼／响响筒，零数值副作用（道具.csv 第11-13行） | ❌ Missing → — | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#3.3#3fbba680 | 渔网特例：500 金币、按落点窝点概率 roll 5~8 条、稀有不加权、湖心巨影不入网、随身限 1、每队每局限购、用后 30 秒死水（道具.md:193；道具 | ❌ Missing → — | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#5#45b50da2 | 解锁＝商店上新货；解锁清单跟人走，件与公款跟局走（道具.md:123） | ✅ Implemented → — | ③ |

### Code-only mechanics

描述差异、待核机制变化：以下仅对比文字，由复核／综合核源码与属主裁决后确认。
| 描述来源 | 机制描述 | code_ref |
|---|---|---|
| 本轮独有描述 | 鱼竿皮肤的独立数据与应用入口 | Source/Catfishing/Fishing/Presentation/CatRodSkinDefinition.h:11；Source/Catfishing/Equipment/CatEquipmentComponent.cpp:374；Source/Catfishing/Fishing/Actors/CatFishingRodActor.cpp:699 |
| 本轮独有描述 | 独立Place精确摆放及空间护栏 | Source/Catfishing/Inventory/CatInventoryStatics.cpp:43；Source/Catfishing/UI/Inventory/CatInventoryWidget.cpp:141 |
| 本轮独有描述 | 单次投窝可提交多份，并有逐定义的单次上限 | Source/Catfishing/Environment/CatChumFieldTypes.h:62；Source/Catfishing/Environment/CatChumPlacementService.cpp:151 |
| 本轮独有描述 | 三款表外饵继续注册：Flashing／GiantLure／Sound | Config/DefaultGame.ini:80；Config/DefaultGame.ini:82；Config/DefaultGame.ini:86 |
| 本轮独有描述 | 玩家离场后，场上竿与预约／待退饵进入服务器私有托管 | Source/Catfishing/Equipment/CatFishingResourceCustodian.h:10；Source/Catfishing/Fishing/CatFishingService.cpp:1123；Source/Catfishing/Fishing/CatFishingService.cpp:1191 |
| 本轮独有描述 | 每人最多部署两根鱼竿 | Config/DefaultGame.ini:277；Source/Catfishing/Fishing/CatFishingService.cpp:1694 |
| 本轮独有描述 | 挑战度1.35硬筛仍先于窝料／鱼饵权重 | Source/Catfishing/Data/CatFishCatalogSettings.cpp:183；Config/DefaultGame.ini:122 |
| 本轮独有描述 | 鱼护入背包时还占嘴并附着嘴部插槽 | Source/Catfishing/FishContainers/CatFishGuardActor.cpp:134；Source/Catfishing/FishContainers/CatFishGuardActor.cpp:155；Source/Catfishing/FishContainers/CatFishGuardActor.cpp:239 |
| 本轮独有描述 | 鱼饵额外修正咬钩等待率与最短等待 | Source/Catfishing/Equipment/Fragments/CatEquipmentFragment_Bait.h:23；Source/Catfishing/Equipment/Fragments/CatEquipmentFragment_Bait.h:27；Source/Catfishing/Fishing/CatFishingSession.cpp:819；Source/Catfishing/Fishing/Simulation/CatFishingBiteTimingModel.cpp:61 |
| 基线独有描述 | 草药恢复链：库存物品片段 `CatHerbRecoveryItemFragment` + 服务器入口 `UseHerbOnCharacterFromAuthority`，按 250cm 触达对他人减 50 点 Poison | Source/Catfishing/Condition/CatHerbRecoveryItemFragment.h；Source/Catfishing/Condition/CatConditionComponent.cpp:171-237,426；Config/DefaultGame.ini:170,171 |
| 基线独有描述 | 鱼竿外观皮肤：纯外观数据资产 `UCatRodSkinDefinition`（网格/材质/VFX/SFX/动画集/锚点映射/兼容竿列表/`RequiredUnlockId`），经装配写入 `RodSkinDefinitionId`，部署竿调 `BP_ApplyRodSkin` | Source/Catfishing/Fishing/Presentation/CatRodSkinDefinition.h:11-29；Source/Catfishing/Equipment/CatEquipmentComponent.h:53-56；Source/Catfishing/Fishing/Actors/CatFishingRodActor.cpp:308-312,698 |
| 基线独有描述 | 库存落地三动作 `Drop / Place / Carry`：Place 是带坡度 30°、高差 30cm、视线与四角支撑校验的精确摆放；Carry 是从鱼护/鱼缸把一条鱼直接叼进嘴 | Source/Catfishing/Inventory/CatInventoryStatics.h:14-21；Source/Catfishing/Inventory/CatInventoryStatics.cpp:19-80；Source/Catfishing/Inventory/CatInventoryComponent.cpp:2233-2340；Config/DefaultGame.ini 无对应段（值在 CatInventorySettings.h:73-89 默认值） |
| 基线独有描述 | 单次投窝数量上限：窝料片段 `MaximumQuantityPerPlacement`，投放命令按它拒绝超量，即一次可投多份窝料 | Source/Catfishing/Environment/CatChumFieldTypes.h:62；Source/Catfishing/Environment/CatChumFieldTypes.cpp:57-58；Source/Catfishing/Environment/CatChumPlacementService.cpp:140 |
| 基线独有描述 | 三款表外鱼饵：`FlashingBait`／`GiantLureBait`／`SoundBait` 已进运行库存目录 | Config/DefaultGame.ini:69,71,75；Content/Catfishing/Data/Equipment/Equip_Bait_Flashing.uasset 等三个资产 |
| 基线独有描述 | 离场装备托管：玩家离场时，其场上鱼竿与已预约鱼饵被搬进一个服务器私有的 `ACatFishingResourceCustodian`（Transient、不复制、按原主 StableId 归属、生命周期止于本局），而不是随人一起消失或落地 | Source/Catfishing/Equipment/CatFishingResourceCustodian.h:10-26；Source/Catfishing/Equipment/CatEquipmentComponent.cpp:1698-1712；Source/Catfishing/Fishing/CatFishingService.cpp:968,1009-1011 |

## 钓鱼系统-打窝聚鱼与多人

未变 33／退步 6／修复 4／其他变化 8／改写 4／锚点漂移 1／新增 16／消失 25

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| BzUbwq0qRil89ykyFPNcNou5nYo#2.1#bdcd6733 | 几何归属唯一规则：近者优先（比圆心距离）、等距早者优先（创建序号小者）、边界算内（距离 ≤ 半径）（钓鱼规则.md:39） | ❌ Missing → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#2.1#da0ff236 | 叠加没有设计上限，代码只留正常游玩碰不到的工程安全夹（钓鱼规则.md:41） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#2.1#17f4d063 | 投料落水泛涟漪、出现鱼影（钓鱼规则.md:43） | ❓ Unverifiable → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#2.1#2db496b2 | 补窝受道具总闸门约束：正在搏斗的猫不能掏窝料；其他处于可用道具状态的玩家可以为同一个窝补料（钓鱼规则.md:45；多人钓鱼附篇.md:75） | 🔄 Divergent → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#2.3#f54e6453 | 某个浮漂计时到点，服务器执行一次原子事务：读该漂主人当前的饵 → 选类别 → 选鱼种 → 当场扣 1 条库存（钓鱼规则.md:68） | 🔄 Divergent → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#2.3#d8d93db3 | 饵的数量在真咬成立时扣当时挂着的那 1 份，试探期提竿不损饵（钓鱼规则.md:74） | ✅ Implemented → 🔄 Divergent | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#2.5#2d26748e | 聚鱼只有投料一条触发路径，范围＝触发它的那个窝点；环境事件触发归完整版、不在本版（钓鱼规则.md:103） | 🔄 Divergent → ❌ Missing | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.4#4829c16d | 体力不高于 5% 时 UI 给濒死强提示（多人钓鱼附篇.md:63） | ❌ Missing → ⚠️ Partial | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.4#4e8a5b8e | 接手瞬间完成：鱼与竿的状态完全继承，猫体力各是各的，接手者用自己的（多人钓鱼附篇.md:65） | 🔄 Divergent → ✅ Implemented | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.4#76bf72d2 | 图鉴归属不变，原始上钩者保留（多人钓鱼附篇.md:65） | 🔄 Divergent → ✅ Implemented | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#3.1#34a10299 | 多人搏斗到收鱼仍是两条路：可抄鱼贴岸抢抄，或翻肚落岸后抢拾；资格与并列判序见主文（多人钓鱼附篇.md:73） | ✅ Implemented → ⚠️ Partial | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#3.2#bcdf51ba | 贡献只用于演出（合影名单、头衔类），不进图鉴、不刷新个人最佳重量（多人钓鱼附篇.md:81） | ⚠️ Partial → ✅ Implemented | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#3.3#3a5adb32 | 求助由玩家手动发出：急促猫叫加头顶图标（多人钓鱼附篇.md:87） | ✅ Implemented → ⚠️ Partial | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.1#5f5361b5 | 多人不绕过当前可用协作能力的鱼候选过滤；加入、退出、换人与在局人数变化是能力变化的来源（多人钓鱼附篇.md:49） | ✅ Implemented → ⚠️ Partial | 锚点漂移（ABIKwtQeliv3aLkuGjrcFAi3nzk#1#5f5361b5 → ABIKwtQeliv3aLkuGjrcFAi3nzk#2.1#5f5361b5） |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.4#0581288f | 岸上替补自愿接手，先按先得、同刻并列按席位序；不设体力准入（多人钓鱼附篇.md:65；设计修改记录.md:396） | ❌ Missing → 🔄 Divergent | ② 措辞改写 |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#3.3#04624d35 | 主控与协作者反馈可区分，不做力量百分比条（多人钓鱼附篇.md:87；设计修改记录.md:303） | ✅ Implemented → ⚠️ Partial | ② 措辞改写 |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.4#7c02611c | 主钓手任意时刻可发起或取消换人请求；请求无时限挂起、本竿结束自然失效；E指主钓手交接，最终键位随装备栏重构定（多人钓鱼附篇.md:63；设计修改记录.md:30 | ❌ Missing → ⚠️ Partial | ② 措辞改写 |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#3.2#116d0281 | 操作贡献者＝上钩者＋本次搏斗摸过竿的猫＋抄中或拾取者；只认直接抓竿，打窝与围观不算（多人钓鱼附篇.md:81；设计修改记录.md:303；设计修改记录.md:3 | 🔄 Divergent → ⚠️ Partial | ② 措辞改写 |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.3#0dfc3a98 | 位置落水只判持竿者（多人钓鱼附篇.md:57） | — → ✅ Implemented | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#f2856d5f | 放线与主动放弃归主钓手；无人值守竿只有原持竿者或竿主人可回来切线（设计修改记录.md:408） | — → 🔄 Divergent | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#3.2#056e0faa | 举鱼展示、围观互动保留，演出由内容层补（多人钓鱼附篇.md:83） | — → ❓ Unverifiable | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#ee85d634 | 多人协作只有一个主控，普通抓握不取得主控；主钓手交接仍需双方同意（设计修改记录.md:303；设计修改记录.md:406） | — → ✅ Implemented | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#110dd137 | 任何猫可用左右手物理抓住鱼竿或队友，借物理约束参与牵引（设计修改记录.md:303） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#7431dacb | 协作猫按自身满力量牵引，不再使用辅助力量折扣、位置倍率或ρ分档（设计修改记录.md:303） | — → ✅ Implemented | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#ee50581f | 物理协作按各猫的「意图位移−实际位移」各扣自己的体力（设计修改记录.md:303） | — → ✅ Implemented | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#8eb8502a | 抓、推、爬与钓鱼搏斗使用同一条跨竿体力，进入搏斗不补满（设计修改记录.md:309） | — → ✅ Implemented | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#7c24b419 | 搏斗消耗优先扣绿色段，绿色不足时继续消费黄色体力（设计修改记录.md:379） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#d45cc32e | 绿色归零而黄色尚有余额时保留当前力量，总体力耗尽才进入力竭（设计修改记录.md:381） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#c44ff43b | 无人值守的竿允许任何猫走近接管续钓，不设体力准入（设计修改记录.md:400） | — → ⚠️ Partial | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#2.5#04990677 | 聚鱼窗口不加库存、衰减照常，仅加速消耗既有存量（钓鱼规则.md:92；钓鱼规则.md:103） | — → ❌ Missing | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#2.5#ec189704 | 聚鱼门槛不为单人下调；合格投料触发概率为固定基础值，不随窝料品质或总存量变化（钓鱼规则.md:92；钓鱼规则.md:103） | — → ❌ Missing | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.4#9b591642 | 换人请求挂起不增加特殊状态或消耗，也不替代本竿原有终结条件（多人钓鱼附篇.md:63） | — → ✅ Implemented | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#e69aa357 | 聚鱼进行中窝点标记变亮，不显示数字倒计时（设计修改记录.md:267） | — → ❌ Missing | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#6f72a0bc | 翻天时窝点及其聚鱼窗口、死水状态一并清空，不遗留到新一天（设计修改记录.md:269） | — → ❌ Missing | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.1#0f944dfc | 参与角色有主钓手、辅助手和岸上替补；合力发生在搏斗拉竿阶段（多人钓鱼附篇.md:35-41） | 🔄 Divergent → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.1#63c4d3b2 | 力量计入：钓鱼位 100%、辅助位 75%（多人钓鱼附篇.md:39-40,53） | ❌ Missing → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.1#32f406eb | 消耗倍率 K_pos：钓鱼位 1.0、辅助位 1.5（多人钓鱼附篇.md:39-40,53） | ❌ Missing → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.1#f1cdf6a9 | 入位：靠近搏斗中的主钓手 1.5 米内按 F；退出：再按 F 或走出半径，自由来去无惩罚（多人钓鱼附篇.md:45） | 🔄 Divergent → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.1#2675f56c | 辅助手体力归零就弹出辅助位、2 秒喘气演出、不落水，恢复到 20% 以上才能重入（多人钓鱼附篇.md:45） | ⚠️ Partial → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.1#3666bdd7 | 辅助人数不另设上限，受 4 人上限自然约束，主 1 加辅最多 3（多人钓鱼附篇.md:45） | ⚠️ Partial → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.1#2e58fd2c | F 目标选择：看着鱼按 F 是抓鱼、看着主钓手按 F 是入位，两者难分时先算抓鱼；准星态提示当前 F 目标（多人钓鱼附篇.md:47） | ❌ Missing → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.2#502619b6 | `总力量 = 主钓手当前力量 × 100% + Σ 辅助手当前力量 × 75%`，辅助力量不设上限；合力变化触发强度检查，提交 F_total 与各参与者 K_ | 🔄 Divergent → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.3#a1757341 | 位置落水只判持竿者；辅助手归零弹出辅助位、不落水（多人钓鱼附篇.md:57） | ✅ Implemented → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.3#75a06e16 | 主钓手放线时全员零消耗，辅助手不必退出（多人钓鱼附篇.md:57） | ⚠️ Partial → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.3#44d6b2ab | 辅助手没有放线权；只有主钓手能触发主动放弃（多人钓鱼附篇.md:57） | ✅ Implemented → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#3.2#b860d9bf | 举鱼展示、围观互动、巨物合力成功的慢镜头合影（多人钓鱼附篇.md:83） | ❓ Unverifiable → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4#91f786aa | WASD 控制移动；`F_net = Σ(猫力量 × 方向单位向量)`、`S = Σ猫力量`（按键者）、`ρ = \|F_net\| / S`；任意方向输入直接进合 | 🔄 Divergent → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4#1d2f2fc9 | 无人按键（S = 0）不进 ρ 计算，直接按静止处理，不存在除零（多人钓鱼附篇.md:93） | ⚠️ Partial → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4#3e579e9c | ρ 三档：齐步走 ρ≥高阈（0.8）正常移动、拖行居中走龟速、僵局 ρ≤低阈（0.2）（多人钓鱼附篇.md:95-99） | ❌ Missing → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4#3674dae4 | 僵局细分：两组共线反向就对顶——恰 2 人顶牛、4 人 2v2 拔河，其余组合自然推拉、无特定演出；方向散乱就打转（多人钓鱼附篇.md:99） | ❌ Missing → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4#1de9c69b | 打转持续超过 5 秒：恰 3 人随机甩飞一只，4 人连环摔倒，两者互斥（多人钓鱼附篇.md:101；09-08 裁：连环摔倒恰 4 人、不产生搏斗结局） | ❌ Missing → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4#8886b2d4 | 移动合力是全额力量相加，钓鱼合力才有辅助位 75% 折扣，两处口径独立（多人钓鱼附篇.md:101） | ⚠️ Partial → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4.1#276dd256 | 移动消耗表：正常移动 1、龟速被拖者 8／顺行者 2、原地打转 5、顶牛拔河 6（点/秒/人）（多人钓鱼附篇.md:111-114） | 🔄 Divergent → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4.1#f61a406d | 无人移动（静止）恢复 5 点/秒/人（多人钓鱼附篇.md:115） | ⚠️ Partial → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4.1#9f7f0107 | 移动中体力归零的猫走不动：不能主动移动、没有拖/背交互、原地恢复并随队被拖行、不阻塞编组（多人钓鱼附篇.md:107,123） | ⚠️ Partial → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4.2#565b3759 | 不按键者不进计票：不贡献力量向量、不拖慢队伍，随队被拖、自己照常恢复（多人钓鱼附篇.md:125） | ⚠️ Partial → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4.2#f81f05cf | 编组就是同房间全员一队、没有子编组，加入退出即入局离局（多人钓鱼附篇.md:125） | 🔄 Divergent → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4.3#b0344d1c | UI：屏幕上方按 W/A/S/D 图标显示每个队友当前方向；移动状态文字「齐步走／被拖行／原地打转／顶牛中」；每个角色头顶体力条、力竭变灰（多人钓鱼附篇.md: | ❌ Missing → — | ③ |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4.3#5ccbbd71 | 六个群体动作演出：顶牛、被拖着走、原地打转、连环摔倒、被甩飞、拔河拉长（多人钓鱼附篇.md:132-138） | ❌ Missing → — | ③ |

### Code-only mechanics

描述差异、待核机制变化：以下仅对比文字，由复核／综合核源码与属主裁决后确认。
| 描述来源 | 机制描述 | code_ref |
|---|---|---|
| 本轮独有描述 | 鱼饵额外改变等待速率及最短静默时间 | Source/Catfishing/Fishing/CatFishingSession.cpp:819；Source/Catfishing/Fishing/Simulation/CatFishingBiteTimingModel.cpp:61 |
| 本轮独有描述 | 投料额外限制视线夹角 | Source/Catfishing/Environment/CatChumPlacementService.cpp:175；Config/DefaultGame.ini:359 |
| 本轮独有描述 | 投料视线遮挡直接拒绝 | Source/Catfishing/Environment/CatChumPlacementService.cpp:181 |
| 本轮独有描述 | 恢复闸及力竭后20%才能再次抓握 | Source/Catfishing/AbilitySystem/Physics/CatPhysicalEffortComponent.cpp:93；Source/Catfishing/AbilitySystem/Physics/CatPhysicalEffortComponent.cpp:97；Source/Catfishing/AbilitySystem/Config/CatPhysicalEffortSettings.h:23 |
| 本轮独有描述 | 辅助力量旧字段仍在资产生成脚本 | Scripts/create_fishing_fight_balance_asset.py:28 |
| 本轮独有描述 | 换人中途失败会留下无人值守竿 | Source/Catfishing/Fishing/CatFishingService.cpp:745；Source/Catfishing/Fishing/CatFishingService.cpp:760 |
| 本轮独有描述 | 删三带后仍保留挑战度1.35安全上限 | Source/Catfishing/Data/CatFishCatalogSettings.cpp:176；Source/Catfishing/Data/CatFishCatalogSettings.cpp:183 |
| 本轮独有描述 | 单次投料数量与每次上限 | Source/Catfishing/Fishing/CatFishingSettings.h:185；Source/Catfishing/Fishing/Integration/CatFishingCommandComponent.cpp:1405；Source/Catfishing/Environment/CatChumPlacementService.cpp:151 |
| 本轮独有描述 | 已裁删除的逐鱼印记事件字段仍控制贡献演出候选 | Source/Catfishing/Data/CatFishDefinition.h:204；Source/Catfishing/Items/Fish/CatFishPickupActor.cpp:1152；Source/Catfishing/Items/Fish/CatFishPickupActor.cpp:1162 |
| 本轮独有描述 | 拽尾救援残留协议声明 | Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.h:12；Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.h:157 |
| 基线独有描述 | 持续物理抓握协作：任何猫可用左右手抓住鱼竿或队友身体，服务端建约束并施加等量反向牵引力（注释：`Force on target; the holder receives the equal and opposite force. No fishing membership or stamina sum.`），协助者按自身满力量出力、按「意图缺失位移」单独扣体，力竭即自动松手 | Source/Catfishing/Interaction/Grab/CatPhysicsGrabComponent.cpp:581-637；Source/Catfishing/AbilitySystem/Physics/CatPhysicalEffortComponent.cpp:19-108；Source/Catfishing/Fishing/CatFishingSession.cpp:181-196 |
| 基线独有描述 | 抽鱼第一步按「挑战档三带」（Comfort/Matched/Risky）roll，档内再算窝料/饵权重；档位阈值与权重在 ini | Source/Catfishing/Data/CatFishCatalogSettings.cpp:245-281；Config/DefaultGame.ini:91-98 |
| 基线独有描述 | 鱼饵直接改咬钩等待：`BiteRateMultiplier` 乘采样速率、`MinimumBiteDelayMultiplier` 乘最小静默时长 | Source/Catfishing/Fishing/CatFishingSession.cpp:694-701；Source/Catfishing/Equipment/Fragments/CatEquipmentFragment_Bait.h:21-27 |
| 基线独有描述 | 打窝落点的视角偏离拒绝：服务端要求视线方向与「视点→落点」夹角不超过 `MaxAimDeviationDegrees`（现值 60°），超出与超程共用 `PlacementOutOfRange` 错误码（复核补行） | Source/Catfishing/Environment/CatChumPlacementService.cpp:164-169；Config/DefaultGame.ini:260 |
| 基线独有描述 | 打窝落点的视线遮挡拒绝 `PlacementOccluded`（服务端从视点到落点打一条线） | Source/Catfishing/Environment/CatChumPlacementService.cpp:170-177 |
| 基线独有描述 | 单次投放份数：`ChumThrowQuantity` 与窝料资产 `MaximumQuantityPerPlacement` 两道闸 | Source/Catfishing/Fishing/CatFishingSettings.h:122；Source/Catfishing/Environment/CatChumFieldTypes.h:61-62；Source/Catfishing/Environment/CatChumPlacementService.cpp:135-143 |
| 基线独有描述 | `ECatFishingCommandType::TailRescue` 与 `FCatTailRescueCommand` 残留：只有枚举与空结构，无任何服务端处理链 | Source/Catfishing/Fishing/Integration/CatFishingCommandTypes.h:12,141 |

## 钓鱼系统-核心

未变 53／退步 16／修复 14／其他变化 18／改写 2／锚点漂移 2／新增 9／消失 3

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| BzUbwq0qRil89ykyFPNcNou5nYo#1#00abdedf | 一竿流程七阶段：备装→可选打窝→抛竿→等待/试探→真咬/提竿→搏斗→收鱼/失败（钓鱼规则.md:23） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.1#f9c14001 | 实际落点按漂精准度在点击点周围均匀圆盘随机偏移（高/中/低 = 0.5/1/1.5 米）（钓鱼规则.md:121） | ❌ Missing → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.1#52896ef9 | 偏出水面（落岸）判空竿收回、不损饵、可重抛；超射程后偏移越界收敛到射程圆边界（钓鱼规则.md:121） | ❌ Missing → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.2#ff8715e8 | 浮漂归属在落水瞬间定死、不随漂移重算（钓鱼规则.md:127） | ✅ Implemented → 🔄 Divergent | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.3#af5e458e | 咬钩前主动收竿零损失；进入咬钩后无论上鱼/超时/放弃/断竿/落水都消耗 1 份饵（钓鱼规则.md:133） | 🔄 Divergent → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.3#aacfc4f9 | 携带 8 份饵＝8 次咬钩机会（钓鱼规则.md:133） | 🔄 Divergent → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.3#757a05b2 | 从咬钩成立到本竿结局落定禁止主动掏用道具，抄网除外（钓鱼规则.md:135） | ❌ Missing → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.4#216f13a5 | 演出时序：抽中瞬间鱼影出现（按真鱼体型）→浮漂轻点进入试探期→漂猛沉即真咬（钓鱼规则.md:141） | 🔄 Divergent → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.4#42c85f9c | 前 1 秒为完美窗，用服务器时间戳判定，恰好 1 秒算完美（闭区间）（钓鱼规则.md:141） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.4#22cee983 | 超时：响应窗结束仍未提竿则鱼吐钩逃跑、饵已消耗（钓鱼规则.md:147） | 🔄 Divergent → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.1#06d962c6 | 猫体力基础上限 100 点（钓鱼规则.md:157） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.1#79be0da0 | 鱼体力初始值＝鱼表「体力系数」× 实际重量（钓鱼规则.md:160） | 🔄 Divergent → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.1#fc4befb2 | 当前力量不随体力衰减，体力低不降力量（钓鱼规则.md:170） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.4#bb52c223 | 移动附加按秒计费的常数：W 前移 1.5 点/秒、S 后退 3 点/秒；力量差不进腿部消耗；W/S 在钓鱼中不翻转（钓鱼规则.md:201） | 🔄 Divergent → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.4#6fa4deef | 线绷紧的每一秒竿耐久另扣「鱼力 × 0.05」（钓鱼规则.md:203） | 🔄 Divergent → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.4#b6f023f3 | 搏斗以渔获结束时，竿另扣基础磨损 1 点（钓鱼规则.md:203） | ❌ Missing → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.5#4aaf22c4 | 真咬成立当刻已超 L_max 直接判脱钩鱼逃，饵已扣、不丢漂（钓鱼规则.md:209） | ⚠️ Partial → 🔄 Divergent | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.5#d955dc56 | 放线本身不恢复体力，搏斗中一律不恢复；放线回体基础为 0，只作猫册三选一成长项（钓鱼规则.md:213） | 🔄 Divergent → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.5#d7e3021a | 放线期间不判位置落水（钓鱼规则.md:213） | ⚠️ Partial → 🔄 Divergent | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.6#d1050930 | 三个资源归零出口：鱼体力≤0 翻肚、猫体力≤0 被拖下水鱼逃、竿耐久≤0 断竿鱼逃（钓鱼规则.md:221） | 🔄 Divergent → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.6#c10addf8 | 落水后果：鱼逃、饵已扣、嘴里原有的鱼保留，无其他损失（钓鱼规则.md:229） | 🔄 Divergent → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.7#923b1c6a | 放弃按鱼逃同口径：已揭剪影保留、收集层不写（钓鱼规则.md:235） | ⚠️ Partial → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.1#869e9ef7 | 可抄资格：对象是钩上的鱼或翻肚鱼，抄手朝向射线碰到挂在鱼身上的可捞圆即够着、够着即必中；自由游动的鱼不可捞（钓鱼规则.md:245） | ✅ Implemented → 🔄 Divergent | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.1#0178de63 | 抄网非必带、单一款式，没有长度/体型上限/成功率属性，不按竿长、鱼重量或抄网等级判断（钓鱼规则.md:245） | ✅ Implemented → 🔄 Divergent | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.2#02d2a4b6 | 嘴里只能叼一条鱼；叼着鱼不能抛竿、不能再抄或拾第二条（钓鱼规则.md:253） | ⚠️ Partial → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.3#5cd45cb7 | 落岸不等于入袋，仍要按 F 拾取、拾取必中；岸上的鱼谁都能拾、先到先得（钓鱼规则.md:259） | 🔄 Divergent → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.3#132d7a28 | 水里翻肚的鱼超过苏醒时限（默认 30 秒）未上岸就苏醒逃跑，拖动中计时照走（钓鱼规则.md:261） | ❌ Missing → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.4#746a2c9e | 时间戳完全并列时按席位序（加入房间先后）打破（钓鱼规则.md:269） | ⚠️ Partial → ❌ Missing | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.5#26f33c9d | 鱼的位置以服务器物理碰撞箱中心为准（钓鱼规则.md:273） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#F目标选择#1e1a7a56 | 多个合法目标同时够着时 F 作用于玩家视角中心所指的那个，UI 以准星态提示当前 F 目标（钓鱼规则.md:279） | ⚠️ Partial → 🔄 Divergent | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.6#c18706a6 | 剪影层在上钩成立时揭开、永不撤销，试探期空竿同揭（钓鱼规则.md:285） | ❌ Missing → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.6#dbcd5913 | 收集层在成功收鱼时写入，归上钩者；实物被队友抢走不取消登记（钓鱼规则.md:285） | 🔄 Divergent → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#6.1#6bda7a0f | 鱼册输入：每鱼×每饵权重、出现条件（区域/时段/天气）、在场协作能力条件（钓鱼规则.md:295） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#6.1#576c359d | 「巨影档」是重量档名、湖心巨影是鱼种名，不能互代来确定可抄或可食用性（钓鱼规则.md:299） | ⚠️ Partial → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#6.2#65147955 | 猫册输入：当前力量、当前/上限体力、倒地/可行动状态（钓鱼规则.md:306） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#6.5#4374c70a | 入夜瞬间处于试探期的竿按空竿收回、不损饵；真咬待响应的算进行中，允许提竿并打完，超时鱼逃；与入夜同刻到点的浮漂不再抽鱼（钓鱼规则.md:337） | ⚠️ Partial → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#6.5#8c5544a3 | 局结束停止钓鱼，释放窝、漂、搏斗等本系统瞬态；已输出的捕获事实不因失败撤销（钓鱼规则.md:341） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#7.1#85a0e2f4 | 基础咬钩信号要让漂主远距离感知，含声音、身体语言与震动通道；铃铛漂增强为全场可闻（钓鱼规则.md:366） | ❓ Unverifiable → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#7.2#8852fb3d | 鱼影按已抽定那条真鱼的体型演出，附近玩家都能看见（钓鱼规则.md:370） | 🔄 Divergent → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#7.2#87216a93 | 线的松紧、竿身弯曲表达张力（钓鱼规则.md:372） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.3#fa284b89 | 增效道具按「进入咬钩」计一场、咬钩前收竿不计场；备装预穿戴的被动效果正常结算；鱼体力不接受道具直接改动，猫体力道具只在搏斗外使用（钓鱼规则.md:135） | ❌ Missing → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#6.5#a55b7951 | 下一轮钓鱼开放：从新的准备与打窝流程开始，不恢复昨天的咬钩倒计时（钓鱼规则.md:340） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.4#d93e19f4 | 完美削减是三项削减：鱼力、鱼体力，再加初始线长按完美线长系数缩短（钓鱼规则.md:149） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.6#df884a3c | 主位在搏斗中主动走开且无人接手时竿落地、会话不结场（无人值守放线）；原持竿者或竿主人走到竿边 2.5 米内可按切线，鱼逃（钓鱼规则.md:231） | ✅ Implemented → 🔄 Divergent | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#7.2#fcd12210 | 队友的装备只靠世界表现与凑近闻来了解，不新增查看装备的菜单（钓鱼规则.md:372） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.4#9b167bca | 试探期提竿必空竿、不损饵，且空竿揭图鉴剪影（钓鱼规则.md:141） | ⚠️ Partial → ✅ Implemented | 锚点漂移（BzUbwq0qRil89ykyFPNcNou5nYo#3.3#9b167bca → BzUbwq0qRil89ykyFPNcNou5nYo#3.4#9b167bca） |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.5#342a028d | D 初始值＝浮漂实际落点到猫的距离，L₀＝D₀（钓鱼规则.md:209） | 🔄 Divergent → 🔄 Divergent | 锚点漂移（BzUbwq0qRil89ykyFPNcNou5nYo#4.1#342a028d → BzUbwq0qRil89ykyFPNcNou5nYo#4.5#342a028d） |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.2#a9d62c16 | 猫力达到鱼力 2 倍即碾压，沿鱼线方向把鱼甩到猫身后固定距离并进待拾取，遇水或墙回拉落点；检查序为竿强瞬断→碾压→常规搏斗（钓鱼规则.md:178） | ❌ Missing → ⚠️ Partial | ② 措辞改写 |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.4#908fc471 | 完美削减系数：普通系数为鱼力量×0.8、体力×0.85；最高档珍稀取×0.85/×0.9，湖心巨影归稀有并取普通系数（钓鱼规则.md:145） | 🔄 Divergent → ⚠️ Partial | ② 措辞改写 |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.6#33fb7c23 | 准备态可走动/切饵；搏斗中主动离竿且无人接手时进入无人值守放线，主动放弃另走终局（钓鱼规则.md:231） | — → ✅ Implemented | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.4#b85a44b8 | 试探期时长逐鱼配置；鱼表未填时才按参数页 2～4 秒随机兜底（钓鱼规则.md:141） | — → ⚠️ Partial | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.2#059e4170 | 竿强度是静态配置，三档 25/60/210；强度不超过总力量与鱼力较小者即当场瞬断并报废鱼竿，是巨影装备门槛；正式开放须先具备钩前鱼影（钓鱼规则.md:176） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#d07bdbf8 | 搏斗消耗先绿后黄；力竭判断使用绿＋黄，绿空黄不空时不得清零当前力量（设计修改记录.md:379） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#5ed39e8d | 体力是抓、推、爬与钓鱼共享的跨竿资源，进入、退出搏斗与换人均不得重置补满（设计修改记录.md:309） | — → ✅ Implemented | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#f5f7e089 | 换人与无人值守竿接管均不设体力准入；无人值守竿可由其他猫接管续钓（设计修改记录.md:396） | — → ⚠️ Partial | ③ |
| Sisrw4FzWiL2AekXetFc7Vxunqg#2#81932132 | 成长「完美窗加宽」每次增加 0.5 秒、最多增加 1 秒；提竿判定与预表现同步加宽（升级效果.md:33） | — → ⚠️ Partial | ③ |
| Sisrw4FzWiL2AekXetFc7Vxunqg#2#6d2a892a | 成长「竿耐久磨损减免」每次减免 10%、最多减免 50%，同时作用于每条鱼基础磨损和绷紧磨损（升级效果.md:37） | — → ⚠️ Partial | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.4#980aa080 | 抢收同一条可抄鱼或岸上鱼失败者各自进入 3 秒独立硬直，硬直中按 F 无效并提示「爪子还在麻」（钓鱼规则.md:267） | — → ⚠️ Partial | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#1#f3702981 | 准备态可走动/切饵；搏斗中不能直接退，只能走主动放弃（钓鱼规则.md:25） | 🔄 Divergent → — | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.4#3c39c2ad | 试探期时长 2～4 秒随机（钓鱼规则.md:141，占位快照） | ⚠️ Partial → — | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.1#42eab424 | 竿强度是静态配置，三档 25/60/210；强度不超过总力量与鱼力较小者即当场瞬断，是巨影装备门槛（钓鱼规则.md:162,176） | 🔄 Divergent → — | ③ |

### Code-only mechanics

描述差异、待核机制变化：以下仅对比文字，由复核／综合核源码与属主裁决后确认。
| 描述来源 | 机制描述 | code_ref |
|---|---|---|
| 本轮独有描述 | 每人最多部署两根鱼竿 | Config/DefaultGame.ini:277；Source/Catfishing/Fishing/CatFishingService.cpp:465 |
| 本轮独有描述 | 强对抗确认时间、角度阈值与僵持派生状态 | Source/Catfishing/Fishing/Simulation/CatFishingFightSimulator.cpp:656；Source/Catfishing/Fishing/CatFishingSession.cpp:1390；Source/Catfishing/UI/HUD/CatHUDModel.cpp:449 |
| 本轮独有描述 | 绿段耗尽后的加速拖猫过程 | Source/Catfishing/Fishing/Simulation/CatFishingFightSimulator.cpp:73；Source/Catfishing/Fishing/Simulation/CatFishingFightSimulator.cpp:209；Source/Catfishing/Fishing/Simulation/CatFishingFightRunner.cpp:407 |
| 本轮独有描述 | NearShore 近岸带状态与 300 cm 边界 | Source/Catfishing/Fishing/CatFishingTypes.h:24；Source/Catfishing/Fishing/CatFishingSession.cpp:78；Config/DefaultGame.ini:310 |
| 本轮独有描述 | 最大线长以外再容许 EscapeSlack | Source/Catfishing/Fishing/Simulation/CatFishingFightSimulator.cpp:448；Source/Catfishing/Fishing/CatFishingSession.cpp:1411；Scripts/create_fishing_fight_balance_asset.py:46 |
| 本轮独有描述 | LateralArc 横切段继续用测试模板时长 | Source/Catfishing/Fishing/Simulation/CatFishBehaviorProfile.cpp:37；Source/Catfishing/Fishing/Simulation/CatFishSteeringModel.cpp:163 |
| 本轮独有描述 | Tooltip 展示精确鱼重量与竿耐久 | Source/Catfishing/UI/ItemTooltip/CatItemTooltipModel.cpp:29；Source/Catfishing/UI/ItemTooltip/CatItemTooltipModel.cpp:42 |
| 本轮独有描述 | 搏斗外恢复需恢复标记、无负载与出力空闲累计 2 秒 | Source/Catfishing/AbilitySystem/Physics/CatPhysicalEffortComponent.cpp:83；Source/Catfishing/AbilitySystem/Config/CatPhysicalEffortSettings.h:23 |
| 本轮独有描述 | 鱼剩余正体力低于阈值时提前清零 | Source/Catfishing/Fishing/Simulation/CatFishingFightSimulator.cpp:650；Source/Catfishing/Fishing/CatFishingSession.cpp:1389；Scripts/create_fishing_fight_balance_asset.py:44 |
| 基线独有描述 | 猫体力归零后的「持续拖拽态」：不结场，鱼按 `ExhaustedCatTowAccelerationCentimetersPerSecondSquared` 继续把力竭猫往外拽，直到真实水深触发落水表现 | Source/Catfishing/Fishing/Simulation/CatFishingFightSimulator.cpp（`ShouldEscapeExhaustedCat`）；Source/Catfishing/Fishing/Simulation/CatFishingFightSimulator.h:7-14,52-55 |
| 基线独有描述 | 每人场上最多两根实体竿：一个人可以同时部署两根竿、各自跑独立会话 | Source/Catfishing/Fishing/CatFishingService.h:25-26（`MaximumDeployedRodsPerPlayer = 2`）；Source/Catfishing/Fishing/CatFishingService.cpp:452-481 |
| 基线独有描述 | 强对抗与僵局判定：`StrongConfrontationAlignmentThreshold` + `StrongConfrontationConfirmationSeconds` 确认后置 `bStrongConfrontation`/`bStalemate` 并复制给 UI | Source/Catfishing/Fishing/Simulation/CatFishingFightSimulator.cpp:650-662；Source/Catfishing/Fishing/CatFishingTypes.h（`bStrongConfrontation`） |
| 基线独有描述 | 通用身体出力体力账：非钓鱼场合（推挤、抓握、攀爬）按「意图位移 − 实际位移」扣同一条搏斗体力，归零进 `bExhausted` 并强制松开所有抓握 | Source/Catfishing/AbilitySystem/Physics/CatPhysicalEffortComponent.cpp:36-108；Source/Catfishing/AbilitySystem/Config/CatPhysicalEffortSettings.h:13-23 |
| 基线独有描述 | 鱼距超 `L_max + EscapeSlackCentimeters`（现值 100cm）判 `Escaped` 终局 | Source/Catfishing/Fishing/Simulation/CatFishingFightSimulator.cpp:448-451；Scripts/create_fishing_fight_balance_asset.py:46 |
| 基线独有描述 | 物品悬停提示在 C++ 里把鱼重量与鱼竿耐久写成明文数字（「重量：{n} kg」「耐久：{当前} / {上限}（已断裂）」） | Source/Catfishing/UI/ItemTooltip/CatItemTooltipModel.cpp:24-46；Source/Catfishing/UI/ItemTooltip/CatItemTooltipWidget.h:41 |
| 基线独有描述 | `NearShore` 兼容阶段与 `NearShoreWidthCentimeters=300` 近岸带校验仍在运行链上（复核更正 ini 行号 213→219） | Source/Catfishing/Fishing/CatFishingTypes.h:20-21；Source/Catfishing/Fishing/CatFishingSession.cpp:70-88,114-124；Config/DefaultGame.ini:219 |

## 鱼

未变 30／退步 3／修复 1／其他变化 12／改写 0／锚点漂移 0／新增 8／消失 1

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| DKhnweGaEiPTJZkO8OScQOJenqg#r1#9a7c9e97 | 力量系数 K 按鱼种配，鱼力量＝重量×K（巨影 K5，与竿强三档 25／60／210 配对）（第一版.csv 第 1 行 力量系数K；设计修改记录.md:283 | 🔄 Divergent → ⚠️ Partial | ① |
| DKhnweGaEiPTJZkO8OScQOJenqg#r1#79be0da0 | 鱼体力初始值＝鱼表「体力系数」× 实际重量（第一版.csv 第 1 行 体力系数；设计修改记录.md:275） | 🔄 Divergent → ✅ Implemented | ① |
| SlFcwt3BdidWCRkz2S4cIplJnnV#1#29862b08 | 吃鱼效果·经验值：局内成长制，经验＝鱼种经验系数 × 实际重量（鱼.md:67；鱼.md:89；第一版.csv 第 1 行 经验系数；数值模拟与参数记录.md: | 🔄 Divergent → ⚠️ Partial | ① |
| SlFcwt3BdidWCRkz2S4cIplJnnV#1#906ac91e | 吃鱼效果·限时 buff 按鱼种配置（含持续时间；负面与演出族同样按鱼种配）（鱼.md:67；鱼.md:89；第一版.csv 第 1 行 限时Buff；第一版. | ❌ Missing → ⚠️ Partial | ① |
| SlFcwt3BdidWCRkz2S4cIplJnnV#1#eff24969 | 吃鱼效果·负面（中毒等）按鱼种配（鱼.md:67；鱼.md:89；第一版.csv 第 1 行 吃鱼效果） | ✅ Implemented → ⚠️ Partial | ① |
| SlFcwt3BdidWCRkz2S4cIplJnnV#5#e57bec4a | 毒鱼经验按同体重档 1.5~2× 上浮（拼死吃河豚），保住倒地救援的内容供给（鱼.md:89） | ❌ Missing → ❓ Unverifiable | ① |
| DKhnweGaEiPTJZkO8OScQOJenqg#r10#a8fa640c | 存在「不可食用」的鱼：咸鱼、湖心巨影（第一版.csv 第 10 行 吃鱼效果；第一版.csv 第 7 行 吃鱼效果；设计修改记录.md:269） | 🔄 Divergent → ❓ Unverifiable | ① |
| SlFcwt3BdidWCRkz2S4cIplJnnV#2#aad6f434 | 鱼的去向·留缸观赏（标本资格按鱼种配）（鱼.md:48；鱼.md:143） | ✅ Implemented → ⚠️ Partial | ① |
| DKhnweGaEiPTJZkO8OScQOJenqg#r1#2461e93c | 食性（食肉／杂食／素食）按鱼种配，决定往外冲的基础倾向（第一版.csv 第 1 行 食性；设计修改记录.md:289） | 🔄 Divergent → ⚠️ Partial | ① |
| DKhnweGaEiPTJZkO8OScQOJenqg#r1#1da1802d | 发力段长／休息段长按鱼种配（快照 3~6 秒／2~5 秒）（第一版.csv 第 1 行 发力段长；第一版.csv 第 1 行 休息段长；设计修改记录.md:28 | 🔄 Divergent → ⚠️ Partial | ① |
| DKhnweGaEiPTJZkO8OScQOJenqg#r1#6bdd780e | 游速系数按鱼种取（不按体重档）（第一版.csv 第 1 行 游速系数；数值模拟与参数记录.md:57；设计修改记录.md:295） | 🔄 Divergent → ⚠️ Partial | ① |
| DKhnweGaEiPTJZkO8OScQOJenqg#r1#89a1cdcb | 与猫搏斗时鱼的动作按鱼种变化（横切／下潜／绕圈／贴底等）（第一版.csv 第 1 行 与猫搏斗时，鱼的动作变化） | ⚠️ Partial → ❓ Unverifiable | ① |
| SlFcwt3BdidWCRkz2S4cIplJnnV#7#c370c510 | 视觉反馈通道：体重档在上钩手感上读得出来，不靠数字告知（鱼.md:126） | ✅ Implemented → ❓ Unverifiable | ① |
| DKhnweGaEiPTJZkO8OScQOJenqg#r1#7fcb4475 | 鱼表格 fish_id 列＝工程 FishDefinitionId，程序按此列对鱼、不按名字；改名不影响对应（第一版.csv 第 1 行 fish_id；设计修 | 🔄 Divergent → ❓ Unverifiable | ① |
| DKhnweGaEiPTJZkO8OScQOJenqg#r10#3c9acbd3 | 特殊鱼的投掷效果按鱼种配：咸鱼投掷击退炸毛、臭臭鱼投掷驱散并短时屏蔽靠近（第一版.csv 第 10 行 吃鱼效果；第一版.csv 第 11 行 吃鱼效果；鱼.m | ❌ Missing → ⚠️ Partial | ① |
| SlFcwt3BdidWCRkz2S4cIplJnnV#1#71a45fba | 无窝料基础池的成员与概率：河纹鱼 0.4／小银鱼 0.3／泥鳅 0.2／臭臭鱼 0.1（占位）（鱼.md:70；鱼.md:85；基础池.csv 第 2 行 基础 | ❌ Missing → ⚠️ Partial | ① |
| SlFcwt3BdidWCRkz2S4cIplJnnV#1#3854f334 | 稀有度字段：四档价值判断（普通／少见／稀有／珍稀），删除「事件」档，湖心巨影归稀有，最高一档为珍稀（鱼.md:65；第一版.csv 第 7 行 稀有度；设计修改 | — → ❓ Unverifiable | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#b08b09da | 试探期时长逐鱼配置；该列未填时才以 2～4 秒兜底，不再由四套测试 Bite 模板决定（设计修改记录.md:375；设计修改记录.md:377；钓鱼规则.md: | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#af376f03 | 完美削减按最高稀有度档分组：四条珍稀鱼力量／体力乘 0.85／0.9，其余含湖心巨影乘 0.8／0.85（设计修改记录.md:424；设计修改记录.md:431 | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#730f8368 | 完美中鱼保留第三项削减：缩短初始线长（设计修改记录.md:285；钓鱼规则.md:149） | — → ⚠️ Partial | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#7b514f3e | 吃鱼获得的黄色体力可用于搏斗：先绿后黄，绿空而黄未空时不清零当前力量（设计修改记录.md:379；设计修改记录.md:383） | — → ⚠️ Partial | ③ |
| DKhnweGaEiPTJZkO8OScQOJenqg#r7#ab9cab26 | 湖心巨影献祭后的权威效果：全队获得 50 点黄色体力，体力消耗减少 50%，祝福持续 600 秒（第一版.csv 第 7 行 限时Buff；第一版.csv 第  | — → ❌ Missing | ③ |
| SlFcwt3BdidWCRkz2S4cIplJnnV#1#b10edabe | 当前 16 行鱼表及饵表的逐鱼数值有正式内容绑定：重量、K、体力／经验／金钱系数、分布、偏好饵与可捞圈等由对应资产承接（鱼.md:57；鱼.md:100；第一版 | — → ❓ Unverifiable | ③ |
| O753wiu7yiuBtMkburWcicZnnre#6#96e5f3c0 | 咸鱼不可献祭，祭坛不得消耗咸鱼或把其重量计入供奉点数（吃鱼效果.md:62） | — → 🔄 Divergent | ③ |
| SlFcwt3BdidWCRkz2S4cIplJnnV#1#63d8f436 | 稀有度字段：五档价值判断，值在鱼表格「稀有度」列（鱼.md:65；第一版.csv 第1行 稀有度列） | ✅ Implemented → — | ③ |

### Code-only mechanics

描述差异、待核机制变化：以下仅对比文字，由复核／综合核源码与属主裁决后确认。
| 描述来源 | 机制描述 | code_ref |
|---|---|---|
| 本轮独有描述 | 逐鱼额外 SpawnWeight 基础系数参与正常选鱼权重 | Source/Catfishing/Data/CatFishDefinition.h:228；Source/Catfishing/Data/CatFishCatalogSettings.cpp:242 |
| 本轮独有描述 | 个体重量立方根决定模型统一缩放，默认钳位 0.8～1.25 | Source/Catfishing/Fishing/Presentation/CatFishPresentationDefinition.cpp:37；Source/Catfishing/Fishing/Presentation/CatFishPresentationDefinition.cpp:43；Source/Catfishing/Fishing/Presentation/CatFishPresentationDefinition.h:89 |
| 本轮独有描述 | 每吃一条鱼就把连续经验向下取整，丢弃小数 | Source/Catfishing/Growth/CatGrowthComponent.cpp:62；Source/Catfishing/Data/CatFishDefinition.cpp:77 |
| 基线独有描述 | 逐鱼咬钩性格模板 `BitePersonalityId`：四套 Bite_* 资产，携带试探期时长、真咬窗、完美窗与完美中鱼的力量／体力／初始线长三个削减倍率 | Source/Catfishing/Data/CatFishDefinition.h:154-156；Data/CatFishPersonalityDefinition.h:9-21；Content/Catfishing/Data/Fish/Bite_{Aggressive,Cautious,Steady,Giant}.uasset |
| 基线独有描述 | 逐鱼基础出现权重 `SpawnWeight`：抽鱼最终权重的乘法基数（最终权重＝SpawnWeight × 窝料 × 鱼饵 × 挑战度），运行就绪要求它必须 >0 | Source/Catfishing/Data/CatFishDefinition.h:125-127；Data/CatFishDefinition.cpp:40；Data/CatFishCatalogSettings.cpp:308-309 |
| 基线独有描述 | 按个体重量的统一可视缩放：鱼在水里、落地和嘴叼时的 Mesh 缩放 ＝ clamp((实际重量 ÷ Mesh 参考重量)^(1/3), 最小缩放, 最大缩放)，三个参数逐鱼配在表现资产上 | Source/Catfishing/Fishing/Presentation/CatFishPresentationDefinition.cpp:34-42；Presentation/CatFishPresentationDefinition.h:49-56；Fishing/CatFishingSession.cpp:931-932,968,989；Items/Fish/CatFishPickupActor.cpp:850,871 |

## 沿用基线的系统

无。

合计：未变 321／退步 61／修复 39／其他变化 125／改写 21／锚点漂移 14／新增 196／消失 151；稳定率 73.8%（一级 523 ÷ 基线 709）；锚点漂移找回 14，二级改写 21，新增 196，消失 151
