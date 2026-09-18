# 皮鞭：Blender 骨骼样稿

按本次确认的短握柄、细长软皮鞭概念制作。此目录是独立美术源资产，没有建立 UE 物品、商店占位、能力、碰撞或伤害行为。

## 查看与文件

- [Whip.blend](Whip.blend)：可编辑网格、程序材质、骨架、两段 Action 及预览灯光。默认选中 `RIG_Whip`；时间轴按空格播放抽击。
- [动画预览](Previews/Whip_Attack.gif)：由 Blender 实际渲染的 25 帧编码，循环播放约 1.63 秒。
- [模型预览](Previews/Whip_Hero.png)，正交视图：[正面](Previews/Whip_Front.png)、[俯视](Previews/Whip_Top.png)、[右侧端面](Previews/Whip_Right.png)。
- `SK_Whip.fbx`：直展参考姿态，包含网格和骨架。
- `AN_Whip_Idle.fbx`、`AN_Whip_Attack.fbx`：相同骨架与网格，分别烘焙一段动画。导入引擎时可选择仅导入动画并使用目标骨架；尚未实测 UE 导入。
- `whip_manifest.json`：源资产统计与每帧鞭梢测量。

## 规格与编辑

Blender 5.2.2 LTS；米制；握柄约 0.2 m、鞭身约 1 m，总长 1.2 m。握柄中部是对象原点，参考姿态沿 +X 伸展，+Z 向上。尺寸是概念值，未与正式猫爪或握持 Socket 对齐。

单网格 `SK_Whip`，3,026 顶点、6,012 三角面；木柄、缠绕接缝、铜色箍环、猫爪印与软皮鞭组成。骨架为 `root` 加连续 `lash_01`—`lash_16`，握柄刚性绑定 root，鞭身每顶点最多两根骨骼影响，权重归一。使用普通线性蒙皮；不依赖骨骼缩放、布料、刚体或外部插件。握柄细节为独立闭合网格岛，同属一个网格对象。

在 Dope Sheet → Action Editor 中选择：

| Action | 帧范围 | 30 fps 下首末关键帧间隔 | 内容 |
| --- | --- | --- | --- |
| `AN_Whip_Idle` | 1–61 | 2 秒 | 下垂、轻微摆动，首尾姿态一致 |
| `AN_Whip_Attack` | 1–49 | 1.6 秒 | 待机→蓄势→甩出→回落，首尾姿态一致 |

时间轴 21 帧 `Strike_Visual_Only` 只是造型演示标记，不定义游戏命中帧。攻击每帧烘焙，鞭身通过关节弯曲展开。动画尚未配合角色手臂，鞭身仍有轻微分段感，后续可在这套骨架上润色节奏及弧线。

源材质使用 Blender 程序噪声表现皮革微表面，无外部贴图。FBX 只作为几何、权重和动画交换文件；程序材质不会完整移植到 UE，正式材质、UV 整理、贴图烘焙、LOD 和 PhysicsAsset 尚未制作。不要把当前各网格岛的局部 UV 当成已打包烘焙 UV。

## 重建与检查

从工程根，在独立后台进程运行；Store 版可将 `blender` 换成 `blender-launcher.exe`：

```text
blender --background --factory-startup --python Scripts/Art/create_whip.py
blender --background --factory-startup --python Scripts/Art/inspect_whip.py
python Scripts/Art/encode_whip_preview.py
```

已有 `Whip.blend` 时生成脚本会拒绝覆盖。确认已另存手工修改后，可在生成命令末尾加 `-- --replace`。脚本自行推导工程根，不修改用户首选项文件。检查脚本输出到 `Saved/Art/Whip/inspection.json`，预览帧到 `Saved/Art/Whip/Frames/`。编码脚本需要安装 Pillow 的 Python，将这 25 帧按约 15 fps 编码，不改变模型或渲染内容。

## 本轮影响与验证结果

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 美术源模型与生成入口 | 原有 `SourceArt/Characters/CreamCat/` 是其他独立样稿；本皮鞭此前只有对话概念图 | 新增米制骨骼源资产和两段动画，不定义攻击数值 | 新增本目录与 `Scripts/Art/create_whip.py`、`inspect_whip.py`、`encode_whip_preview.py` | 网格→蒙皮→动画→保存→重开→FBX 回读→渲染→GIF 编码 | 尺寸、权重、闭合几何、动画首尾及逐帧坐标 | 已生成 .blend、3 个 FBX、8 张实渲图片与 25 帧 GIF；110 帧检查通过，FBX 三文件回读通过 |
| 游戏消费者与交付边界 | `Docs/Architecture/物品使用与策划配置.md` 未列皮鞭接入；本目录不在 Content 内 | 本轮仅独立样稿，不接游戏运行链 | 沿用现有入口；不涉及配置、BP/WBP/DataAsset、网络权威、玩法持久化、UI、退出清理、Cook/打包和开发运行日志 | 后续导入时在 UE 编辑器核查资产引用和握持绑定 | 本轮无 UE 消费者衔接验收；二进制历史引用未确认，没有据此删除资产 | UE 相关工作不在本次源模型交付范围；本轮没有清理或替换既有实现 |

- `contract`：保存重开成功；17 根骨骼；权重归一；最多两影响；网格非流形边 0；3 个 FBX 回读成功，参考模型总长约 1.2 m。
- `runtime_behavior`：Blender 内两段动画共 110 个整数帧坐标有效、骨骼缩放恒为 1、首末鞭梢一致；回读动画确实产生运动。未覆盖所有子帧、自交和正式角色协同，不代表 UE 运行验收。
- `presentation_delivery`：已检查实际渲染的造型、待机与挥出姿态，修正待机鞭梢裁切；提供完整抽击预览。属于可编辑样稿，正式角色持握、游戏命中表现、多人和打包未验证。

制作前工作区已有祭坛 BP、抓握 C++ 和一份未跟踪裁决文档变更，均未纳入本次修改。未使用 Harness，不改变业务模块完成状态。
