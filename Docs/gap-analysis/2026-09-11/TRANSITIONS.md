# 基线迁移

基线：Docs\gap-analysis\2026-09-09-1321

改写是二级匹配的独立计数，可同时计入退步／修复／其他变化；同状态改写不计未变。

## ui与交互

未变 44／退步 1／修复 1／其他变化 6／改写 3／新增 54／消失 22

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#6f8f9c6d | 「加入游戏」页：好友房间列表＋输入邀请码两种入口（主界面.md:21） | ⚠️ Partial → ❌ Missing | ① |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#bfdac119 | 加载界面显示「第 n 天」与本局进度轴（09-08 已把「献祭进度」作废，改「世界进度」，另给当日任务点数与缸内可献点数）（主界面.md:69,71） | ⚠️ Partial → ❌ Missing | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r6#5b9702d9 | 按 B 弹出背包、再按 B 关闭（Sheet1.csv 第6行 交互方式；主界面.md:95；Sheet2.csv 第13行 按键） | 🔄 Divergent → ❓ Unverifiable | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r15#c017a00b | 营地装备与仓库：回营地打开，出行装备与仓库浏览（Sheet1.csv 第15行） | ✅ Implemented → ⚠️ Partial | ① |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#512ce1d3 | 背包装备栏四格：鱼竿、鱼饵、冰壶（窝料）、鱼篓（鱼护）（主界面.md:97） | ⚠️ Partial → ❌ Missing | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r4#34577fd5 | 遛鱼状态：按住鼠标左键收线、按住鼠标右键放线（Sheet2.csv 第4行、第7行） | ❓ Unverifiable → ✅ Implemented | ① |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互神像献祭#429a2084 | 献祭呈现要区分三个量：当日任务点数、缸内可献点数、世界进度（交互.md:42） | ❌ Missing → ✅ Implemented | ① |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互神像献祭#5e78aa23 | 世界进度平时隐藏，靠近神像或打开界面时查看（交互.md:42） | ❌ Missing → ⚠️ Partial | ① |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r7#f793932d | 屏幕右下角提竿提示「鱼儿咬钩啦！」左键提竿，鱼漂明显下沉时出现（Sheet1.csv 第7行） | ⚠️ Partial → ⚠️ Partial | ② |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r16#b75addcd | 图鉴页面：按 M 打开，鱼种解锁、剪影鱼种（Sheet1.csv 第16行） | ⚠️ Partial → ⚠️ Partial | ② |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#a47cf01a | 派对菜单项：组队管理（队伍 x/4、等待队列、Steam 好友邀请、仅好友／仅邀请、复制邀请链接）（主界面.md:79,81） | ⚠️ Partial → ⚠️ Partial | ② |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r26#0f149607 | 存档页快捷键：Enter 载入、N 新建、Esc 返回主菜单（Sheet2.csv 第26行、第31行、第36行 当前作用） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r17#96acd292 | 钓鱼时常驻显示鱼竿耐久（Sheet1.csv 第17行「竿旁／屏幕下方，手持鱼竿时，自动显示」；主界面.md:119 图注「左下角是『鱼下耐久』相关的内容」） | — → 🔄 Divergent | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r18#11a6fd1c | 左上角白天常驻显示当日任务点数与缸内可献点数（Sheet1.csv 第18行） | — → 🔄 Divergent | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r19#d436270e | 上方显示白天倒计时（Sheet1.csv 第19行） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r20#a1032e48 | 升级时屏幕中央三选一弹窗，每项带描述、按出现次序刷出（Sheet1.csv 第20行） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r21#42642913 | 准星态：有合法目标够着时显示，多个合法目标同时够着时提示当前对谁出手（Sheet1.csv 第21行） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r22#16dd3bb5 | 搏斗中长按收竿键时屏幕中央显示放弃进度条（Sheet1.csv 第22行） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r23#cab1c2d9 | 主钓手体力低于 5% 时的换人濒死强提示（Sheet1.csv 第23行） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#527a79d3 | 抛竿：鼠标左键点哪落哪、无蓄力（Sheet2.csv 第2行 按键；主界面.md:105） | — → ✅ Implemented | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#e0d44b81 | 超射程或猫与点击点之间有遮挡都抛不出，提示「够不到那边」（Sheet2.csv 第2行 当前作用；提示文案.csv 第14行 提示或表现；交互.md:96） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r3#48f51f41 | 鱼儿咬钩时鼠标左键提竿（Sheet2.csv 第3行） | — → ✅ Implemented | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r5#2449ff0c | 鼠标左键从鱼护取鱼、从鱼缸取鱼（Sheet2.csv 第5行、第6行） | — → 🔄 Divergent | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r8#557e216a | 遛鱼状态 W 前移靠水、S 后退离水，钓鱼中不切相机反转（Sheet2.csv 第8行、第9行） | — → ✅ Implemented | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r10#81ad1085 | 长按收竿键 1.5 秒放弃、松手取消（Sheet2.csv 第10行；Sheet1.csv 第22行 备注） | — → 🔄 Divergent | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r11#f1853f22 | 抄鱼资格：抄手朝向射线碰到鱼身上的可捞圆即够着，圆半径逐鱼配置，视线遮挡与脚下坡度为拒绝条件（Sheet2.csv 第11行 当前作用） | — → ✅ Implemented | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r11#35b348dd | 抄鱼键 F（Sheet2.csv 第11行 按键，09-07 裁定 F、Space 不用） | — → ✅ Implemented | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r12#3c174ace | E 换人：主钓手按 E 发起请求、再按 E 取消；替补按 E 接手（Sheet2.csv 第12行） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r14#8922a068 | 使用键：背包装备栏选中道具后使用——鱼竿＝朝对应位置抛漂、鱼护＝在脚下放下鱼护容器（Sheet2.csv 第14行；交互.md:67,91,98） | — → 🔄 Divergent | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r15#20f5d99e | E 靠近鱼护查看鱼护（Sheet2.csv 第15行） | — → ✅ Implemented | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r16#72d0a977 | F 拾起放下的鱼护：变回道具、鱼随护走（Sheet2.csv 第16行；主界面.md:139,155；09-09 八问⑧） | — → 🔄 Divergent | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r17#c89d92f7 | 长按 E 吃掉嘴里叼着的鱼（Sheet2.csv 第17行） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r18#b9591e2a | E 靠近营地鱼缸，把鱼护中的鱼放入鱼缸（Sheet2.csv 第18行） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r19#0bbec601 | E 鱼缸交互完成后，把取出的鱼放回鱼护（Sheet2.csv 第19行） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r20#cd72038e | E 商人对话确认选择（Sheet2.csv 第20行） | — → ✅ Implemented | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r21#6a5a892f | F 鱼落地后拾取（Sheet2.csv 第21行） | — → 🔄 Divergent | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r22#47693e67 | F 放下嘴里叼着的鱼（Sheet2.csv 第22行） | — → ❓ Unverifiable | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r23#fa54339f | 与石像互动显示提示「【F】献给圣猫」（Sheet2.csv 第23行；交互.md:38-39） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r25#a85253e6 | T 查询当前鱼窝／查看鱼窝范围（Sheet2.csv 第25行；主界面.md:99） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r29#ae717058 | 鱼缸放鱼界面：Tab 进入「选择放入」、Enter 全部放入／确认当前选择、Esc 取消（Sheet2.csv 第29行、第34行、第38行） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r30#332cf411 | Esc 在游戏中打开或关闭派对菜单（Sheet2.csv 第30行） | — → ✅ Implemented | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r33#60de1c40 | Esc 在商店对话、背包、设置页返回上一层（Sheet2.csv 第33行、第35行） | — → ✅ Implemented | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互神像献祭#966dbfbb | 祭坛是石像前的一块地不是容器：把鱼一趟趟叼过去扔在祭坛上就算摆好，随时可叼回，直到石像被按下那一刻（交互.md:34-35） | — → ✅ Implemented | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互神像献祭#c0cc6f54 | 与石像互动＝结算＋翻天：全员到齐才能按下，倒地的猫豁免、已离开的玩家不计入；按下后祭坛上的鱼一起消失，播过场，天亮（交互.md:35） | — → ✅ Implemented | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互神像献祭#1791ccff | 两个圈：摆鱼圈 2 米、到场圈 8 米（交互.md:36） | — → ✅ Implemented | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互神像献祭#db6ce98a | 祭坛空着也能按，结算为当日任务没交齐，世界进度按降幅下跌（交互.md:41） | — → ✅ Implemented | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#玩家靠近湖面#754d7ed3 | 手持鱼竿即钓鱼待机，左键点水面抛竿，不设「F 钓鱼」模式入口（交互.md:67） | — → ✅ Implemented | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互换饵与窝料#a8cbcc9c | 换饵与窝料：岸边只能切换已带出的物品，不能凭空换（交互.md:89） | — → ✅ Implemented | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互换饵与窝料#27abf6a9 | 搏斗中不能掏窝料；其他人可以补窝（交互.md:90） | — → 🔄 Divergent | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互抛竿与投窝料的落点#725f695a | 抛竿与投窝料的落点：点哪落哪，无蓄力（交互.md:95） | — → 🔄 Divergent | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互抛竿与投窝料的落点#f8677842 | 窝料落岸成掉落物，可捡回（交互.md:97） | — → ❌ Missing | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互求助与震动#ea71d23f | 手动求助＝急促猫叫加头顶图标；只有巨物给全体提示（交互.md:102） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#780ab036 | 抛竿表现：左键点水面，猫甩竿（提示文案.csv 第2行 提示或表现） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r3#45f814c1 | 鱼向岸休息时状态文字变绿「鱼累了！按住左键拖回来！」、鱼向外发力时变红「鱼在发力！放线喘口气，拖线就是硬拼！」，配猫耳朵尾巴表现（提示文案.csv 第3行、第4 | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r5#5a17c5d0 | 鱼可抄时屏幕中央爪印图标＋「快抄！（按 F）」，以收鱼资格判定为准（提示文案.csv 第5行） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r6#a7f30189 | 猫体力偏低时体力条变红闪烁＋「猫喘气了！」与喘气表现（提示文案.csv 第6行） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r7#e25caba1 | 落水危险时爪扒地划痕＋「要滑下去了！」（提示文案.csv 第7行） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r8#57cbbe52 | 完美中鱼：金色水花＋瞳孔放大＋清脆一声「叮」＋「完美！」（提示文案.csv 第8行） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r9#0e9e6950 | 断竿：咔嚓声、爪里只剩半截竿、猫愣住（提示文案.csv 第9行） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r10#311319cc | 鱼逃：耳朵耷拉、水面恢复平静（提示文案.csv 第10行） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r11#28b9c410 | 挥空：猫拍空或被绊、沙漏图标＋「爪子麻了！」（提示文案.csv 第11行） | — → ⚠️ Partial | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r12#a087e061 | 主动放弃：「放生了鱼…鱼饵和浮漂被带走了」（提示文案.csv 第12行） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r13#9296bfd0 | 碾压：鱼落身后草地、猫翘尾巴，仍需拾取（提示文案.csv 第13行） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r15#78a026a6 | 「没够着」：抢抄被拒的四种几何原因统一提示（提示文案.csv 第15行） | — → ⚠️ Partial | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#d837d35e | 鱼缸／鱼护界面的「放掉（放生）」操作（主界面.md:163「放掉／取鱼／放入鱼缸」三个选项） | — → ❌ Missing | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r24#0f149607 | 存档页快捷键：Enter 载入、N 新建、Esc 返回主菜单（Sheet2.csv 第24行、第29行、第34行 当前作用） | ⚠️ Partial → — | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r2#0cff5eaa | 湖边／抛竿界面，鼠标左键抛竿（Sheet2.csv 第2行 按键；主界面.md:105；钓鱼规则.md:119） | ❓ Unverifiable → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#a3bdc915 | 抛竿后可放置鱼竿，按 E 确认位置、放置后进入等待钓获（主界面.md:107,109；Sheet2.csv 第9行） | ❓ Unverifiable → — | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r20#44e412e2 | 抄鱼键（Sheet2.csv 第20行写 Space；以钓鱼规则.md:246、353 为准＝F） | ❓ Unverifiable → — | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r15#9f1e47fd | 鱼落地后按 F 拾取（Sheet2.csv 第15行；钓鱼规则.md:256） | 🔄 Divergent → — | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r11#cc8dc2bf | 嘴里叼着鱼时：F 放下鱼、长按 E 吃掉鱼（Sheet2.csv 第11行、第16行） | ❌ Missing → — | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r17#23741596 | F 叼起鱼护、携带鱼护时 F 放下鱼护（Sheet2.csv 第17行、第18行；主界面.md:155,157） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#64051329 | 按 T 查看鱼窝范围／查询当前鱼窝（主界面.md:99；Sheet2.csv 第22行） | ❌ Missing → — | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r23#b2c3532d | 按 T 在鱼图鉴里追踪鱼类（Sheet2.csv 第23行） | ❌ Missing → — | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#交互神像献祭#fa54339f | 与石像互动显示提示「【F】献给圣猫」（交互.md:36-37） | ❌ Missing → — | ③ |
| PjD4w3XrJi4UT4k3fthcTCOrnhf#玩家靠近湖面#e76c2935 | 玩家靠近湖面时按 F 钓鱼（交互.md:66） | 🔄 Divergent → — | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r10#e38eca4a | E 键：靠近鱼护查看鱼护、靠近营地鱼缸放入鱼、鱼缸交互完成后放回鱼护、商人对话确认选择（Sheet2.csv 第10行、第12行、第13行、第14行） | ✅ Implemented → — | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r28#332cf411 | Esc 在游戏中打开或关闭派对菜单（Sheet2.csv 第28行） | ✅ Implemented → — | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r31#60de1c40 | Esc 在商店对话、背包、设置页返回上一层（Sheet2.csv 第31行、第33行；背包页同理） | ✅ Implemented → — | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r27#c5e9f4ef | 鱼缸放鱼界面：Tab 进入「选择放入」、Enter 全部放入／确认当前选择（Sheet2.csv 第27行、第36行） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#a1be7a56 | 鱼护界面操作：收、放、丢弃、取鱼（主界面.md:149） | ⚠️ Partial → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#6ada1e51 | 鱼缸界面操作：放生、取鱼、放入鱼护（主界面.md:163） | ⚠️ Partial → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#af8081b6 | 鱼缸展示鱼的重量与数量 x/容量（主界面.md:161） | ⚠️ Partial → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#2f129034 | 左上角同时显示当前时段（清晨／白天／夜晚）（主界面.md:75） | ❌ Missing → — | ③ |
| OEqkw02J2iHso9keQ1Yc7Wsdnpe#主界面#b7c669a4 | 钓鱼界面左下角常驻显示鱼竿耐久（主界面.md:119「左下角标注鱼竿耐久780」，与左上角体力同屏） | ⚠️ Partial → — | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r15#bf6e5353 | 营地装备与仓库界面附带「追踪鱼轻提示」（Sheet1.csv 第15行 备注） | ❌ Missing → — | ③ |
| UZ3ywmA0WiN8MKkSyOrcvuvknDb#r7#72ab7e28 | 提竿按键（Sheet1.csv 第7行写「F提竿」；Sheet2.csv 第3行写「鼠标左键，鱼儿咬钩，提竿」；钓鱼规则.md:350 只给了「抛竿＝左键点水面 | 🔄 Divergent → — | ③ |

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 印记与图鉴

未变 46／退步 2／修复 1／其他变化 5／改写 0／新增 3／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| EPi4wjG5IigRTwkH8stcGWoUnUh#1#54f68d19 | 合捕／巨型鱼合影＝共同印记，自动分发给所有参与者（印记.md:73、印记.md:136） | ✅ Implemented → ⚠️ Partial | ① |
| EPi4wjG5IigRTwkH8stcGWoUnUh#6#10fd8561 | 照片忠实记录拍摄当时的猫外观与装备（印记.md:148） | ❓ Unverifiable → ❌ Missing | ① |
| EPi4wjG5IigRTwkH8stcGWoUnUh#1#64752577 | 抓拍只用轻快门声＋相纸飘入，不弹窗、不暂停、不抢镜头（印记.md:47、印记.md:199） | ❓ Unverifiable → ⚠️ Partial | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#3#cb11407e | 线索层触发：上钩成立就揭剪影，之后跑掉／断竿／放弃都不回滚（图鉴.md:98；设计修改记录.md:249、:259） | ⚠️ Partial → ❌ Missing | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#3#1a0b8db3 | 线索层触发：错饵被拒（鱼影绕一圈摆尾走）揭剪影（图鉴.md:100） | ❌ Missing → ✅ Implemented | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#4#31084d4b | 合力拉竿的猫与抄网命中者只进演出贡献名单（合影／头衔），不登记、不刷新个人最佳重量（图鉴.md:116-117；设计修改记录.md:267） | ✅ Implemented → ⚠️ Partial | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#3#57d3bf7d | 「当时看不清」是线索层成立的前提：水里的鱼影只给存在感不给答案，看不出是哪一种（图鉴.md:107） | 🔄 Divergent → ⚠️ Partial | ① |
| EPi4wjG5IigRTwkH8stcGWoUnUh#2#eaa19002 | 构图与拍摄：自动构图追动作高潮帧、出洋相构图、全员合影自动站位（印记.md:100、印记.md:106） | ❓ Unverifiable → ❌ Missing | ① |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#7#3041effb | 共用大鱼缸是活的图鉴：缸内按实际鱼种游动，看缸等于翻一遍（图鉴.md:195） | — → ❓ Unverifiable | ③ |
| ZNEpwV95CiU8eVkXoN4c0M1Sn8c#2026#35e5a13d | 跟人走三样（个人图鉴含剪影、印记相册、外观解锁清单）的存档：旧档版本不符按空档重建（设计修改记录.md:287 ③） | — → 🔄 Divergent | ③ |
| Kiz5wpRKRiIFF6kfjhOcOmwrncQ#8#4736feef | 破个人纪录／本局全队最佳抛印记（趣味展示、不做排行）（图鉴.md:150、印记.md:75） | — → ❌ Missing | ③ |

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 商店

未变 34／退步 0／修复 4／其他变化 6／改写 1／新增 3／消失 1

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| U0JtwqFJ8ijKxakb1egcRXhvnKg#2#a8397ca3 | 任何玩家出售任何来源的鱼——自己的鱼、缸里的鱼和偷来的鱼——所得统一进入公款（商店.md:51、67） | ⚠️ Partial → ✅ Implemented | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.1#0f14f028 | 白天不可献祭，夜晚由全队统一开启献祭（商店.md:67） | 🔄 Divergent → ✅ Implemented | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.2#6c496649 | 消耗品单次携带上限以「钓鱼系统」2.7 为准（商店.md:71） | 🔄 Divergent → ⚠️ Partial | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.2#851e14f4 | 鱼竿断裂后该鱼竿直接消失，系统不自动返还、不自动替换、不回退为 1 级竿（商店.md:75；DeiSFL.csv 第 7 行 首轮通过条件） | ⚠️ Partial → 🔄 Divergent | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.2#dafba75a | 赠送与丢弃统一为「扔下」行为（商店.md:77） | ❌ Missing → ✅ Implemented | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.1#f79169d5 | 收购价以鱼体重为主要锚点（商店.md:63、128） | ⚠️ Partial → ✅ Implemented | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.4#dae889b6 | 商人猫是世界里第一个 NPC，猫猫在他这里进行买卖（商店.md:87） | ❓ Unverifiable → 🔄 Divergent | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#2#587d2d7b | 失败局不进入结算夜，直接执行失败结算（商店.md:55） | 🔄 Divergent → ⚠️ Partial | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#7#a8e5722f | 商人猫收摊需同时通过动作、音效与商店不可交互状态传达（商店.md:136） | ❓ Unverifiable → ⚠️ Partial | ① |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#2#633e09bc | 每车成交向全队广播一条，列购买者与所购商品，并记录购买者、商品、价格和购买时刻供 Playtest 回看（商店.md:52、97） | ⚠️ Partial → 🔄 Divergent | ② |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#2#9b03260c | 鱼缸容量升级＝唯一设施类商品：不进团队装备库、直接作用在营地那口缸上、按档位买、随局清空；初始 10 条，两档 20／30，价 300／700 金币（商店.md | — → ❌ Missing | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#2#daec1341 | 购买以一车为单位：一次可选多件，提交时服务器按当前价重算总价、整车一起成交，任一件缺货或公款不够就整车不成交且公款不动（商店.md:52、71） | — → ✅ Implemented | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#分册范围与职责#75b811bc | 硬性边界：物品本体（清单、属性、解锁表达）归「道具」分册，本册只管交易与经济（商店.md:17、106） | — → ✅ Implemented | ③ |
| U0JtwqFJ8ijKxakb1egcRXhvnKg#3.1.2#9b03260c | 鱼缸容量升级＝唯一设施类商品：不进团队装备库、直接作用在营地那口缸上、按档位买、随局清空；初始 10 条，两档 20／30，价 300／700 金币（商店.md | ❌ Missing → — | ③ |

### Code-only mechanics

| 变化 | 机制 |
|---|---|
| 新增 | 下单时的摊位距离证明：服务端在整车提交前复核玩家仍在摊位 `InteractionRadiusCentimeters`（默认 300cm）内，超距即 `DependencyUnavailable` |
| 新增 | 两段式交付：购买账本先写 `bDeliveryPending`，再由 `ConfirmTransactionDelivery` 用下游回执推进到 `bDeliveryConfirmed`，公开 DTO 也带这两个标位 |
| 新增 | 公款版本校验：整车命令带 `ExpectedRevision`，与服务器 `WalletRevision` 不符即 `RevisionConflict` 拒绝整车 |
| 新增 | 功能装备的用侧解锁闸门：竿／饵／漂／抄网四件装备定义各带 `RequiredUnlockId`，装配时逐件要求 PlayerState 持有服务器授权的解锁证明，Profile 选槽也另查一次 `UnlockIds`（复核补） |
| 新增 | 售鱼金额的工程边界：逐鱼四舍五入后再求和，单鱼与整批都夹在 16777216 以内，越界整单拒绝而不给部分金额 |
| 新增 | 商店分类页：目录行带 `DisplayCategoryId` 与分类显示名覆盖，UI 归纳出分类按钮并本地过滤 |
| 新增 | 地面鱼护页的批量售鱼入口：鱼护页自带「卖选中／卖全部」按钮，按附近买家与双方距离显隐，一次提交多条鱼 |
| 消失 | 两段式交付：购买账本先记 `bDeliveryPending`，营地公共仓库回执后才 `ConfirmTransactionDelivery` 置 `bDeliveryConfirmed` |
| 消失 | 公款乐观并发：提交购物车或售鱼必须带 `ExpectedWalletRevision`，与服务器当前公款版本不符即整单拒 `RevisionConflict`（复核补录） |
| 消失 | 售鱼最小金额门槛 `MinimumFishSaleValue=1`：估值低于该值的售鱼命令按「估价证据不足」拒绝 |
| 消失 | 商店商品分类页：`DisplayCategoryId` ＋分类按钮，UI 本地按分类过滤 |
| 消失 | 服务端下单距离证明：提交购物车时服务器复核玩家 Pawn 仍在摊位 300cm 内，否则整单拒 |
| 消失 | 购物车整车结算：多行选购、服务器重算总价、整车原子提交，任一行库存不足或总价溢出即整车拒绝 |

## 猫咪与状态

未变 54／退步 0／修复 1／其他变化 6／改写 0／新增 0／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| Na20wnqTXiwM7TkKf4kcnaQ0npf#5#a7d25ad2 | 搏斗外体力自然回复 5 点/秒、无场景乘数（数值成长.md:56） | ❌ Missing → ⚠️ Partial | ① |
| BIeHwP14vigUEBkI3UxciEionxf#5#277c1586 | 倒地者可缓慢爬行（猫咪与状态.md:116） | ❌ Missing → 🔄 Divergent | ① |
| BIeHwP14vigUEBkI3UxciEionxf#5#d6b1b9d0 | 倒地者不计入「全员到齐」的翻天判定（猫咪与状态.md:116） | ❌ Missing → ✅ Implemented | ① |
| BIeHwP14vigUEBkI3UxciEionxf#5#010efcf4 | 倒地时不能自己献祭，队友可代献他鱼护里的鱼（猫咪与状态.md:116） | ❌ Missing → ⚠️ Partial | ① |
| BIeHwP14vigUEBkI3UxciEionxf#4#3521bfb8 | 中毒失态演出：走路画龙、瞳孔漩涡、打嗝冒泡、幻觉、翻肚皮晕倒、头顶转圈小鱼（猫咪与状态.md:104、106、190） | ❓ Unverifiable → ⚠️ Partial | ① |
| BIeHwP14vigUEBkI3UxciEionxf#3.2#02b5eb7c | 通用动画规范：爪抓握自动贴合、双脚随坡度、尾巴耳朵程序化摆动、紧张炸毛、入水毛塌（猫咪与状态.md:149） | ❓ Unverifiable → ⚠️ Partial | ① |
| Na20wnqTXiwM7TkKf4kcnaQ0npf#6#2e17d7df | 渔获信息三值同屏：翻鱼护/鱼缸时显示每条鱼的经验、供奉、售价原始值（数值成长.md:66） | ❌ Missing → ⚠️ Partial | ① |

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 环境与氛围

未变 25／退步 1／修复 0／其他变化 1／改写 1／新增 6／消失 1

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| unknown#L0#36f35137 | 双区域探索结构：河流（日常水域）＋森林湖（要走一段路、鱼种与事件偏稀有）（:89） | ❌ Missing → ❓ Unverifiable | ① |
| unknown#L0#2206a296 | 自然事件按局内时钟与天气条件出场，条件不成立就没有事件（不造占位事件；:101） | ✅ Implemented → ⚠️ Partial | ① |
| unknown#L0#410998bb | 所有环境状态（时间、天气、事件进度）属于这一局，局末即弃、无跨局存档（:55、:147、:155） | ✅ Implemented → ✅ Implemented | ② |
| unknown#L0#c1fb523b | 时钟源＝局内时间流：开局启动、随局推进、局末即弃，任何机制不读现实时钟（环境与氛围.md rev106:65、:153、:46） | — → ✅ Implemented | ③ |
| unknown#L0#f767c955 | 夜晚不钓鱼、不产鱼情；入夜边界＝不再有新咬钩，进行中的搏斗允许打完（:65、:71；裁决同步/设计修改记录.md:58） | — → ✅ Implemented | ③ |
| unknown#L0#3bb60c2b | 两区域同属「湖畔」主场景（不是两张分开的关卡）（:89） | — → ✅ Implemented | ③ |
| unknown#L0#8640dc09 | 两区域的差异要落在「能遇到什么」上，森林湖的事件偏稀有（:89、:95） | — → ❌ Missing | ③ |
| unknown#L0#3a9aaddf | 每种天气至少绑一个专属演出或专属事件，天气不能只剩装饰（:83） | — → ⚠️ Partial | ③ |
| unknown#L0#c4097186 | 天气变化要同时改画面和声音（晴/雨/雾切换时画面与声音跟着变，:77、:185-186） | — → ⚠️ Partial | ③ |
| Bg2jwBdkViHRgRkOtx2crCgAnHh#1#c1fb523b | 时钟源＝局内时间流：开局启动、随局推进、局末即弃，任何机制不读现实时钟（环境与氛围.md:65、:153、:46） | ✅ Implemented → — | ③ |

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 联机社交

未变 40／退步 6／修复 3／其他变化 0／改写 2／新增 2／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#1#2b0783ff | 局制红线：局末世界清空（营地、渔获、祭坛进度归零），物资跟局走（联机社交.md:84、:178） | ✅ Implemented → ⚠️ Partial | ① |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#5#074b9a64 | 偷的对象＝他人鱼护与营地共用大鱼缸两类容器（联机社交.md:130） | ✅ Implemented → 🔄 Divergent | ① |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#5#3e535ff3 | 偷了就吃：进食时间即追回窗口，吃完不可逆；偷吃获得成长（buff＋经验）（联机社交.md:130、:210） | ✅ Implemented → ⚠️ Partial | ① |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#5#f43b3805 | 被抓现场：物归原主（联机社交.md:130） | ✅ Implemented → ⚠️ Partial | ① |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#5#86577d5e | 被抓现场双方进印记（「偷鱼未遂」名场面）（联机社交.md:130、:134） | ✅ Implemented → ⚠️ Partial | ① |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#2#e0ee9226 | 白天攒缸、白天不可献；夜里全队到石像前一次性上供，上供当场结算（联机社交.md:73、:140） | 🔄 Divergent → ✅ Implemented | ① |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#6#06da3d7f | 交齐＝世界进度上涨、未交齐＝下跌，进度归零才是这一局失败（联机社交.md:140，v3.2 改制） | 🔄 Divergent → ✅ Implemented | ① |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#2#5ee86427 | 扔物传递（赠送＝把身上的东西扔下给别人拾取，无交易界面）（联机社交.md:71） | ⚠️ Partial → ✅ Implemented | ① |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#1#c424b4bc | 当日任务清晨按当时人数定死、不重算；有人中途退出留下的人扛完（联机社交.md:84） | ✅ Implemented → ⚠️ Partial | ② |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#1#95e0eb94 | 房主离开时自动移交给最早加入者；房主离开只换房主、不收口，本局接着打（联机社交.md:84，v3.7 于 2026-09-09 补入） | 🔄 Divergent → 🔄 Divergent | ② |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#2#2ce552f7 | 共同目标：夜里全队到石像前上供＝翻天开关，鱼从营地拿到祭坛一次性交清（联机社交.md:73、:140） | — → ✅ Implemented | ③ |
| EH0LwpHihiFR8Ukh6s6cXTqUnXc#1#9772cf2d | 负面互动的权限前提：偷取有权限开关（联机社交.md:63） | — → ✅ Implemented | ③ |

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 营地与局进程

未变 31／退步 1／修复 19／其他变化 4／改写 0／新增 2／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| EE9vwzWE2iHVXGkduzGcEx4bnjh#2#a8ae4f3e | 鱼缸容量首版：初始 10 条、两档升级 20／30 条、价 300／700 金币、用公款在商人猫处升级、升级随局清空（营地.md:61；局与进程.md:111） | ❌ Missing → 🔄 Divergent | ① |
| EE9vwzWE2iHVXGkduzGcEx4bnjh#2#504a5139 | 鱼缸跟局走、跨天留存、局末随局清空（营地.md:61,103；局与进程.md:103） | ✅ Implemented → ⚠️ Partial | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#a2a8f7fa | 天循环五步：清晨定任务 → 白天限时 → 到点入夜 → 夜晚无限 → 石像互动结算翻天（局与进程.md:40-44） | 🔄 Divergent → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#1#f8bd186f | 到点即入夜、入夜不判定任务（局与进程.md:54） | 🔄 Divergent → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#c269c180 | 进行中的搏斗允许打完，压哨鱼进夜后照常可献（局与进程.md:42,54；营地.md:41,148） | 🔄 Divergent → ✅ Implemented | ① |
| EE9vwzWE2iHVXGkduzGcEx4bnjh#2#c43fc32e | 白天只攒不献，献祭窗口只在夜晚（营地.md:39,61；局与进程.md:23,41,60） | 🔄 Divergent → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#d2e81024 | 祭坛是石像前的一块地不是容器：鱼一趟趟叼过去扔上就算摆好，互动前随时可叼回（局与进程.md:60,205；营地.md:42,61） | ❌ Missing → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#31483528 | 当前仍在局里的玩家到齐后与石像互动一次，祭坛上的鱼一起消失、当场结算、天亮（局与进程.md:44,60,95；营地.md:42,61） | 🔄 Divergent → ⚠️ Partial | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#e460002e | 祭坛两个圈：摆鱼范围 2 米、判定到场范围 8 米（局与进程.md:60） | ❌ Missing → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#85ab020c | 祭坛空着也能互动，结算为没交齐（局与进程.md:60,205；营地.md:42） | ❌ Missing → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#79932bec | 翻天没有单独的确认按钮，献祭本身就是翻天开关（局与进程.md:44,95,203） | 🔄 Divergent → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#5#4cebd0f8 | 倒地的猫豁免、不计入到场判定，队友仍可代献其鱼护里的鱼（局与进程.md:95；营地.md:42,77,150） | ❌ Missing → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#b873ed43 | 任务逐日变大（局与进程.md:58,81,139） | ⚠️ Partial → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#9f410a40 | 供奉点数按实际体重档取：小 1／中 2／大 4／巨影档 10（局与进程.md:66-71） | 🔄 Divergent → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#1599e061 | 多交超出当日任务没有额外收益、也不滚存到明天（局与进程.md:73） | 🔄 Divergent → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#2#ba24188f | 世界进度：0~100% 一根条、开局 10%、跟局走（局与进程.md:46,79,103,138） | ❌ Missing → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#3#53132395 | 翻天结算时交齐则进度涨、没交齐则跌；涨幅跟任务大小走，降幅走独立曲线且开局压低（局与进程.md:79,81,83,140） | ❌ Missing → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#3#5877248b | 臭臭鱼计入当日任务点数但削弱当日涨幅，每条 −25%、线性叠加、削后不低于原地踏步（局与进程.md:85,141） | ❌ Missing → ⚠️ Partial | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#6#ad5207e5 | 唯一的胜利条件是世界进度到 100%，天数没有上限（局与进程.md:99） | 🔄 Divergent → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#4#64530c75 | 毕业：把进度顶到 100% 的那一次上供不翻天，当场转成结算夜，玩家自己决定何时离开（局与进程.md:91） | ⚠️ Partial → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#4#82b805f0 | 失败＝世界进度归零，这一局立刻结束，不进结算夜（局与进程.md:91,120） | 🔄 Divergent → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#8#4a3300f5 | 单人＝自己走到祭坛与石像互动即可结算，空祭坛也不卡流程（局与进程.md:107；营地.md:142） | ⚠️ Partial → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#7#60aa27f2 | 白天全队可见「缸内可献点数 vs 今日任务」（局与进程.md:146） | ❌ Missing → ✅ Implemented | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#4#a9c1c3b8 | 进度涨跌看得见：夜晚祭坛即进度条、森林随进度复苏或枯萎（局与进程.md:125,146） | ❌ Missing → ⚠️ Partial | ① |
| Ca49ww71ai2IAlklmsZcqF7Tnzb#7#8c5473a9 | 局中断点可存可续：一局没打完退出时，世界进度、天数、营地与鱼缸、装备与公款存下来，下次开局接着打；旧档版本不符按空档重建，写失败不影响本局（局与进程.md:10 | — → ⚠️ Partial | ③ |
| EE9vwzWE2iHVXGkduzGcEx4bnjh#4#f5b6d781 | 篝火是营地五件固定设施之一，摆在场景里（营地.md:103） | — → ❌ Missing | ③ |

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 道具

未变 54／退步 1／修复 4／其他变化 4／改写 2／新增 1／消失 1

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| VVHrw3FAui78wZkjuKZcLHBdnEc#1#e79bd30b | 玩家自己打开装备栏时，鱼竿鱼饵等装备分开摆，不杂糅（道具.md:62） | ❓ Unverifiable → ❌ Missing | ① |
| unknown#L0#f2620c0a | 射程分 10／15／20 米档（鱼漂/Sheet1.csv 第2-4行 射程列；2026-09-09 由 3／5／7 改表） | 🔄 Divergent → ❓ Unverifiable | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#7#9a15326d | 窝料携带上限 5 份（道具.md:147） | ✅ Implemented → ⚠️ Partial | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#3.2#2c384c47 | 耐久不可修复；竿耐久对道具「回补封闭」（道具.md:161；道具.md:187 资源边界表「竿耐久」行） | 🔄 Divergent → ✅ Implemented | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#4#897f8f8d | 不引入世界观外元素：装备命名与形态一律猫世界化，无人类文化符号（道具.md:206） | 🔄 Divergent → ❓ Unverifiable | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#4#012423f0 | 主动分享道具＝把身上的东西扔下让人拾取，无交易界面（道具.md:204-205） | ❌ Missing → ✅ Implemented | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#7#b41c4a79 | 装备外观即信息：竿／饵／漂外观差异一眼可辨（道具.md:236-237） | ⚠️ Partial → ❓ Unverifiable | ① |
| VVHrw3FAui78wZkjuKZcLHBdnEc#1#ccec05d2 | 可抄条件：钩上或翻肚的鱼、抄取必中（道具.md:68） | 🔄 Divergent → ✅ Implemented | ② |
| VVHrw3FAui78wZkjuKZcLHBdnEc#1#bfcf8f48 | 鱼护＝随身渔获容器、两态、占一格、非必带、鱼随护走、放下的全队可开可拾（道具.md:64；道具.csv 第7行 作用列） | 🔄 Divergent → ✅ Implemented | ② |
| VVHrw3FAui78wZkjuKZcLHBdnEc#5#7d6ecc1c | Demo 不做图鉴里程碑解锁：三档竿／三种漂／抄网／鱼护开局就在货架，全靠商店买（道具.md:123，v1.36 由「里程碑附带解锁」改） | — → ⚠️ Partial | ③ |
| VVHrw3FAui78wZkjuKZcLHBdnEc#5#99a90d03 | 图鉴收集里程碑可附带装备解锁（道具.md:123） | ⚠️ Partial → — | ③ |

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 钓鱼系统-打窝聚鱼与多人

未变 57／退步 6／修复 2／其他变化 9／改写 0／新增 2／消失 0

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.1#0f944dfc | 参与角色有主钓手、辅助手和岸上替补；合力发生在搏斗拉竿阶段（多人钓鱼附篇.md:35-41） | ⚠️ Partial → 🔄 Divergent | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.1#63c4d3b2 | 力量计入：钓鱼位 100%、辅助位 75%（多人钓鱼附篇.md:39-40,53） | 🔄 Divergent → ❌ Missing | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.1#31bc0d47 | 岸上替补搏斗外恢复 5 点/秒（多人钓鱼附篇.md:41） | 🔄 Divergent → ⚠️ Partial | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.1#2675f56c | 辅助手体力归零就弹出辅助位、2 秒喘气演出、不落水，恢复到 20% 以上才能重入（多人钓鱼附篇.md:45） | ❌ Missing → ⚠️ Partial | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.1#3666bdd7 | 辅助人数不另设上限，受 4 人上限自然约束，主 1 加辅最多 3（多人钓鱼附篇.md:45） | ✅ Implemented → ⚠️ Partial | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.2#502619b6 | `总力量 = 主钓手当前力量 × 100% + Σ 辅助手当前力量 × 75%`，辅助力量不设上限；合力变化触发强度检查，提交 F_total 与各参与者 K_ | ⚠️ Partial → 🔄 Divergent | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.3#a1757341 | 位置落水只判持竿者；辅助手归零弹出辅助位、不落水（多人钓鱼附篇.md:57） | 🔄 Divergent → ✅ Implemented | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.3#75a06e16 | 主钓手放线时全员零消耗，辅助手不必退出（多人钓鱼附篇.md:57） | ✅ Implemented → ⚠️ Partial | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.4#82735e57 | 岸上替补自愿按 E 接手，先按先得、同刻并列按席位序；体力恢复到 50% 以上的替补才能接手（多人钓鱼附篇.md:65） | 🔄 Divergent → ❌ Missing | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#2.4#4e8a5b8e | 接手瞬间完成：鱼与竿的状态完全继承，猫体力各是各的，接手者用自己的（多人钓鱼附篇.md:65） | ✅ Implemented → 🔄 Divergent | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#3.3#f62abce2 | 只有巨物保留全体提示（多人钓鱼附篇.md:87） | ✅ Implemented → ⚠️ Partial | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#3.3#7de22a8c | 主辅位的反馈与角色区分不做力量百分比条（多人钓鱼附篇.md:87） | ⚠️ Partial → ✅ Implemented | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4#1d2f2fc9 | 无人按键（S = 0）不进 ρ 计算，直接按静止处理，不存在除零（多人钓鱼附篇.md:93） | ✅ Implemented → ⚠️ Partial | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4#8886b2d4 | 移动合力是全额力量相加，钓鱼合力才有辅助位 75% 折扣，两处口径独立（多人钓鱼附篇.md:101） | 🔄 Divergent → ⚠️ Partial | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4.1#f61a406d | 无人移动（静止）恢复 5 点/秒/人（多人钓鱼附篇.md:115） | ❌ Missing → ⚠️ Partial | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4.2#7315a0fc | 恢复速率由主文基础自然恢复提供：搏斗外一律 5 点/秒，静止、岸上替补、摸鱼与归零随队被拖都是这一个数、没有场景乘数（多人钓鱼附篇.md:123） | 🔄 Divergent → ⚠️ Partial | ① |
| ABIKwtQeliv3aLkuGjrcFAi3nzk#4.2#565b3759 | 不按键者不进计票：不贡献力量向量、不拖慢队伍，随队被拖、自己照常恢复（多人钓鱼附篇.md:125） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#2.3#227c65e4 | 抽鱼的「主人」由到点的那个浮漂确定：不为窝点先抽一条公共鱼再比各人的饵，也不在多个饵命中的玩家之间随机分钩（钓鱼规则.md:68） | — → ✅ Implemented | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#2.3#2406841e | 随机区间的端点算在区间内（钓鱼规则.md:70） | — → ✅ Implemented | ③ |

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 钓鱼系统-核心

未变 75／退步 1／修复 3／其他变化 5／改写 13／新增 11／消失 4

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| BzUbwq0qRil89ykyFPNcNou5nYo#3.3#c649ff05 | 竿耐久不能回补、竿不可维修（钓鱼规则.md:135,204） | 🔄 Divergent → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.1#5e96d470 | 鱼力量 F_fish 由鱼表按实际重量给出（实例力量＝重量×K）（钓鱼规则.md:159,295） | ✅ Implemented → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.6#c18706a6 | 剪影层在上钩成立时揭开、永不撤销，试探期空竿同揭（钓鱼规则.md:284） | 🔄 Divergent → ❌ Missing | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#基础自然恢复#7860df4f | 搏斗外自然恢复 5 点/秒，站立/岸上替补/摸鱼一律同速率、无场景乘数；搏斗中不恢复（钓鱼规则.md:310；v1.3 裁决） | 🔄 Divergent → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#6.4#ba564637 | 当天扔下等于赠送路径（谁都能捡），物品不能扔进湖里（钓鱼规则.md:324） | ❌ Missing → ⚠️ Partial | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#6.5#3d5dae71 | 入夜：不再产生新咬钩，还没咬钩的浮漂剩余计时清掉，进行中的搏斗允许打完（钓鱼规则.md:336） | 🔄 Divergent → ✅ Implemented | ① |
| BzUbwq0qRil89ykyFPNcNou5nYo#6.4#22d5f7fd | 转存接口要区分成功与拒收，满容器不得当作转存成功（钓鱼规则.md:328） | ✅ Implemented → ✅ Implemented | ② |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.1#0178de63 | 抄网非必带、单一款式，没有长度/体型上限/成功率属性，不按竿长、鱼重量或抄网等级判断（钓鱼规则.md:244） | ✅ Implemented → ✅ Implemented | ② |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.1#4b70863d | 射程由漂提供（点击点到猫站立点的距离 D_click 不超过浮漂射程）（钓鱼规则.md:121） | 🔄 Divergent → 🔄 Divergent | ② |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.4#9c76b445 | 消耗随固定步运动求解走，求解抖动会抖体力条，需要一层平滑（钓鱼规则.md:199） | ⚠️ Partial → ⚠️ Partial | ② |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.1#64169f9f | 湖心巨影的可捞圆为 0、不可抄，只能翻肚拖岸或碾压落岸后拾取（钓鱼规则.md:244） | ❌ Missing → ⚠️ Partial | ② |
| BzUbwq0qRil89ykyFPNcNou5nYo#F目标选择#1e1a7a56 | 多个合法目标同时够着时 F 作用于玩家视角中心所指的那个，UI 以准星态提示当前 F 目标（钓鱼规则.md:278） | ⚠️ Partial → ⚠️ Partial | ② |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.4#bb52c223 | 移动附加按秒计费的常数：W 前移 1.5 点/秒、S 后退 3 点/秒；力量差不进腿部消耗；W/S 在钓鱼中不翻转（钓鱼规则.md:201） | 🔄 Divergent → 🔄 Divergent | ② |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.3#132d7a28 | 水里翻肚的鱼超过苏醒时限（默认 30 秒）未上岸就苏醒逃跑，拖动中计时照走（钓鱼规则.md:260） | ❌ Missing → ❌ Missing | ② |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.1#572da2f0 | 超射程与遮挡都拒绝抛竿并提示「够不到那边」（钓鱼规则.md:121；v1.7 补遮挡） | ⚠️ Partial → ⚠️ Partial | ② |
| BzUbwq0qRil89ykyFPNcNou5nYo#6.5#4374c70a | 入夜瞬间处于试探期的竿按空竿收回、不损饵；真咬待响应的算进行中，允许提竿并打完，超时鱼逃；与入夜同刻到点的浮漂不再抽鱼（钓鱼规则.md:336） | ❌ Missing → ⚠️ Partial | ② |
| BzUbwq0qRil89ykyFPNcNou5nYo#6.1#576c359d | 「巨影档」是重量档名、湖心巨影是鱼种名，不能互代来确定可抄或可食用性（钓鱼规则.md:298） | ⚠️ Partial → ⚠️ Partial | ② |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.5#26f33c9d | 鱼的位置以服务器物理碰撞箱中心为准（钓鱼规则.md:272） | ✅ Implemented → ✅ Implemented | ② |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.1#869e9ef7 | 可抄资格：对象是钩上的鱼或翻肚鱼，抄手朝向射线碰到挂在鱼身上的可捞圆即够着、够着即必中；自由游动的鱼不可捞（钓鱼规则.md:244；v1.6 按代码转正） | 🔄 Divergent → ✅ Implemented | ② |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.4#7e0b8999 | 搏斗消耗按猫的出力计：收线按线长每米单价、转竿按弧度单价、顶住按负载平方每秒，转竿支撑与顶住只收较高一项；负载项＝空载系数＋当前负载×负载系数，放线时三样都不计 | — → ✅ Implemented | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.3#0ead289f | 翻肚后不放线时线长固定，猫往后走鱼就被拉得离岸更近，拖过岸线即按力竭处理、侧翻落在碰岸处；这一段无对抗即无消耗（钓鱼规则.md:258；v1.8 按代码改写） | — → ⚠️ Partial | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.5#e40a2443 | 抄网射程 2 米；落岸鱼可拾距离 1.5 米，是全游戏统一交互半径，与抄网射程是两个参数（钓鱼规则.md:272） | — → ⚠️ Partial | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.5#5180f01e | 够不够着看抄手的身体朝向：从抄手位置沿面朝方向水平发射线，转镜头不改变朝向（钓鱼规则.md:272；v1.6 按代码转正） | — → ✅ Implemented | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#3.4#d93e19f4 | 完美削减是三项削减：鱼力、鱼体力，再加初始线长按完美线长系数缩短（钓鱼规则.md:149,209；v1.7 按代码转正） | — → ✅ Implemented | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.6#df884a3c | 主位在搏斗中主动走开且无人接手时竿落地、会话不结场（无人值守放线）；原持竿者或竿主人走到竿边 2.5 米内可按切线，鱼逃（钓鱼规则.md:230；09-09 裁 | — → ✅ Implemented | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.5#0ee88ac7 | 抄网三条拒绝条件：抄手与鱼之间有遮挡、抄手脚下坡度超过 45 度、抄手与鱼高差超过 2.5 米（钓鱼规则.md:272；v1.6 按代码转正） | — → ✅ Implemented | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.5#b3f70288 | 抄手必须站在岸上，嘴里不能叼着鱼（钓鱼规则.md:272） | — → ✅ Implemented | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#6.5#18ae30f8 | 翻天完成时窝点清空，聚鱼窗口与死水随窝点一并清空（钓鱼规则.md:338） | — → ❌ Missing | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.5#431ad87d | 等待期移动不受线约束（钓鱼规则.md:209） | — → ✅ Implemented | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#7.2#fcd12210 | 队友的装备只靠世界表现与凑近闻来了解，不新增查看装备的菜单（钓鱼规则.md:371） | — → ✅ Implemented | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#4.4#d2e94ac4 | 搏斗消耗公式 `C_battle = ΔD_blocked × (1+V_fish/V_base) × (F_fish/F_base) × K_dir × K_ | 🔄 Divergent → — | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.3#9d83a1ef | 翻肚后鱼沿鱼→猫方向以固定速率（建议 2 米/秒）被拖动，触岸即在碰岸处落地（钓鱼规则.md:256） | 🔄 Divergent → — | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.5#a823259e | 挥网者到鱼 ≤1.5 米才算够着，1.5 米同时是落岸鱼的可拾距离，全游戏统一交互半径一处管全部（钓鱼规则.md:270） | 🔄 Divergent → — | ③ |
| BzUbwq0qRil89ykyFPNcNou5nYo#5.5#6d8e7f76 | 不加朝向或扇形判定，距离圈够用（钓鱼规则.md:270） | 🔄 Divergent → — | ③ |

### Code-only mechanics

基线报告没有该节，无法确认机制新增／消失。

## 鱼

未变 32／退步 2／修复 1／其他变化 2／改写 3／新增 8／消失 2

| 键 | 要求 | 基线状态 → 本轮状态 | 匹配级 |
|---|---|---|---|
| SlFcwt3BdidWCRkz2S4cIplJnnV#1#7a062ee6 | 缸内游动表现：共用大鱼缸按实际鱼种游动，是「活的鱼册」（鱼.md:61、125、143） | ❌ Missing → ❓ Unverifiable | ① |
| SlFcwt3BdidWCRkz2S4cIplJnnV#4#727a4d98 | 窝料决定「哪一类」、鱼饵决定「哪一条」：属性命中后候选鱼按当前鱼饵偏好权重取（鱼.md:85；饵权重.csv 第18行 说明列「抽鱼两步：先按窝里腥／香／酵占比 | ✅ Implemented → ⚠️ Partial | ① |
| SlFcwt3BdidWCRkz2S4cIplJnnV#9#1c7330d6 | 供奉点数按体重档取：小 1／中 2／大 4／巨影 10（鱼.md:145；数值模拟与参数记录.md 第50行 供奉值行） | 🔄 Divergent → ✅ Implemented | ① |
| DKhnweGaEiPTJZkO8OScQOJenqg#r1#1da1802d | 发力段长／休息段长按鱼种配（快照 3~6 秒／2~5 秒）（第一版.csv 第1行 发力段长列、休息段长列；设计修改记录.md:289） | ⚠️ Partial → 🔄 Divergent | ① |
| DKhnweGaEiPTJZkO8OScQOJenqg#r1#7fcb4475 | 鱼表格 fish_id 列＝工程 FishDefinitionId，程序按此列对鱼、不按名字；改名不影响对应（第一版.csv 第1行 fish_id 列；设计修 | ✅ Implemented → 🔄 Divergent | ② |
| DKhnweGaEiPTJZkO8OScQOJenqg#r1#9a7c9e97 | 力量系数 K 按鱼种配，鱼力量＝重量×K（巨影 K5，与竿强三档 25／60／210 配对）（第一版.csv 第1行 力量系数K列；设计修改记录.md:283  | 🔄 Divergent → 🔄 Divergent | ② |
| DKhnweGaEiPTJZkO8OScQOJenqg#r1#6bdd780e | 游速系数按鱼种取（不按体重档）（第一版.csv 第1行 游速系数列；数值模拟与参数记录.md 第54行；设计修改记录.md:295 三问①） | 🔄 Divergent → 🔄 Divergent | ② |
| Xg1EwylUuiUeVckeLwtcMXMjnAg#当前拍定参数#0d45d0b1 | 体重档四档阈值：小 w<1／中 1≤w<5／大 5≤w<15／巨影 w≥15，端点半开区间；09-10 起消费链只剩供奉值一条（数值模拟与参数记录.md 第54 | — → ✅ Implemented | ③ |
| SlFcwt3BdidWCRkz2S4cIplJnnV#2#e7a2af19 | 鱼的去向·卖给商店，售价＝金钱系数 × 实际重量（逐鱼每公斤单价）（鱼.md:48、144；第一版.csv 第1行 金钱系数列；设计修改记录.md:291） | — → ✅ Implemented | ③ |
| SlFcwt3BdidWCRkz2S4cIplJnnV#1#c9d46f40 | 可捞圈半径逐鱼字段：抄手朝向射线碰到鱼身上的圆即可抄，小鱼小圈、大鱼大圈，湖心巨影 0＝不可抄（鱼.md:68、81；第一版.csv 第1行 可捞圈半径列；数值 | — → ✅ Implemented | ③ |
| SlFcwt3BdidWCRkz2S4cIplJnnV#1#2d4cc305 | 最近冲岸距离：活着时不进离岸多少米，0＝可到岸边；按鱼种／食性给、不按体重档（鱼.md:69；第一版.csv 第1行 最近冲岸距离列；数值模拟与参数记录.md  | — → ❌ Missing | ③ |
| SlFcwt3BdidWCRkz2S4cIplJnnV#1#45a6a446 | 饵权重子表：每鱼×每饵的抽鱼权重，目标饵权重高、非目标饵低但不为零（鱼.md:70、85；饵权重.csv 第1行 虫虫饵/肉块饵/果实饵/花蜜饵/月光饵列） | — → ✅ Implemented | ③ |
| SlFcwt3BdidWCRkz2S4cIplJnnV#1#71a45fba | 无窝料基础池的成员与概率：河纹鱼 0.4／小银鱼 0.3／泥鳅 0.2／臭臭鱼 0.1（占位）（鱼.md:70、85；基础池.csv 第2~5行 基础池概率（占 | — → ❌ Missing | ③ |
| SlFcwt3BdidWCRkz2S4cIplJnnV#2#72857545 | 抽鱼只由分布、窝料轴、鱼饵偏好决定，不存在按玩家当前强度分带的第四条轴（鱼.md:76；设计修改记录.md:289「删代码七条」含挑战档三带） | — → 🔄 Divergent | ③ |
| SlFcwt3BdidWCRkz2S4cIplJnnV#1#fa2ac5e6 | 鱼定义不持有逐鱼印记事件字段，印记准入清单归印记册（鱼.md:59-70 字段表无此项；设计修改记录.md:289「删代码七条」含逐鱼印记事件字段） | — → 🔄 Divergent | ③ |
| SlFcwt3BdidWCRkz2S4cIplJnnV#1#8575dcbe | 体重档四档阈值（小 w<1／中 1≤w<5／大 5≤w<15／巨影 w≥15，2026-09-06 重拍，端点半开区间）为供奉值、收购价、游速三条链共用（经验  | ⚠️ Partial → — | ③ |
| SlFcwt3BdidWCRkz2S4cIplJnnV#2#5f1f067d | 鱼的去向·卖给商店，收鱼定价按同尺换算（鱼.md:48、141） | ⚠️ Partial → — | ③ |

### Code-only mechanics

| 变化 | 机制 |
|---|---|
| 新增 | 按个体重量的统一可视缩放：鱼在水里、落地和嘴叼时的 Mesh 缩放 ＝ clamp((实际重量 ÷ Mesh 参考重量)^(1/3), 最小缩放, 最大缩放)，三个参数逐鱼配在表现资产上 |
| 新增 | 逐鱼咬钩性格模板 `BitePersonalityId`：四套 Bite_* 资产，携带试探期时长、真咬窗、完美窗与完美中鱼的力量／体力／初始线长三个削减倍率 |
| 新增 | 逐鱼基础出现权重 `SpawnWeight`：抽鱼最终权重的乘法基数（最终权重＝SpawnWeight × 窝料 × 鱼饵 × 挑战度），运行就绪要求它必须 >0 |
| 消失 | 挑战度分带选鱼：候选鱼先按 `max(S, 2ST/(S+T))` 算力量/体力挑战比，超 1.35 直接淘汰，再按轻松≤0.65／势均力敌≤1.05／高风险≤1.35 分三带、按 25%／60%／15% 抽带，带内才做窝料×鱼饵权重归一化 |
| 消失 | 逐鱼「可捞圈半径」`ScoopTargetRadiusCentimeters`：圆心随鱼权威位置移动，注释写明语义是「小鱼给小圈、巨鱼给大圈以降低多人抢抄难度」，0 即拒绝抢抄 |
| 消失 | 逐鱼咬钩性格模板 `BitePersonalityId`（四套 Bite_* 资产，携带完美中鱼的力量／体力／初始线长倍率与真咬窗） |
| 消失 | 逐鱼成像事件 `CaptureImprintEventId`：鱼定义上挂捕获后可选印记事件的语义 ID |

## 沿用基线的系统

无。

合计：未变 492／退步 21／修复 39／其他变化 48／改写 25／新增 92／消失 31；稳定率 91.4%（一级 592 ÷ 基线 648）；二级 25，新增 92，消失 31
