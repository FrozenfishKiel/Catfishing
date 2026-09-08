# 奶油钓鱼猫：独立 Blender 样稿

本目录保存小猫的造型与基础动画。没有导入 UE，也没有替换游戏模型、骨架、动画、Blueprint 或配置。

## 查看

- `CreamCat_Live.blend`：当前交互建模文件；在时间轴按空格播放待机。
- `CreamCat.blend`：同一阶段的保存副本，供检查和展示脚本读取。
- `Animation_Showcase.blend`：三猫分别播放待机、带位移步行、钓鱼等待姿态。
- `Previews/CreamCat_Animation_Showcase.mp4`：1280×720、30 fps、192 帧，6.4 秒。
- `Previews/CreamCat_Hero.png`、`CreamCat_Front.png`、`CreamCat_Side.png`、`CreamCat_Back.png`：Blender 实际渲染。
- `Previews/CreamCat_Blink.png`、`CreamCat_Fishing.png`：闭眼与握竿姿态检查。

单角色文件在 Outliner 中显示并选中 `RIG_CreamCat`，切换 Dope Sheet → Action Editor 选择动作：

| Action | 帧范围 | 循环时长 |
| --- | --- | --- |
| AN_CreamCat_Idle | 1–121 | 4 秒 |
| AN_CreamCat_Walk | 1–33 | 32/30 秒 |
| AN_CreamCat_FishingWait | 1–151 | 5 秒 |

末帧重复首帧，曲线有循环修饰器。单角色动作在原地；展示中的独立 Walk 副本放慢到 48 帧周期，并根据支撑脚添加前进位移。整个视频不是循环视频。Idle 和 FishingWait 有呼吸、眨眼与摆尾。

## 造型约定

胸腹及臀部使用连续梨形轮廓，头身和尾根合为单个 Cat_Body。短前肢为连续曲面，圆爪没有拇指；耳朵、五官和肉垫保持独立。旧的分层胸腹椭圆体、独立 Cat_Head 和脸颊横向凸纹已移除。围巾为默认隐藏的可选配件。

- Blender 5.2.1 LTS，米制，正面 -Y、上方 +Z，站立约 1.27 米；没有匹配正式角色尺寸。
- 30 根骨骼，含躯干、四肢、耳眼、5 段尾骨及 Mouth/grip 定位骨。
- 10 个眼部组件以 Blink 形状键响应眼骨的 blink 属性，眼骨缩放保持 1。
- 毛色使用 Color 顶点色，不依赖外部贴图。
- 身体使用保持体积蒙皮与关节平滑修正。实际网格统计见 cream_cat_manifest.json；此为造型源资产，游戏拓扑、LOD、修正变形与形状键的引擎导出尚未验证。

## 鱼竿

Reference/SM_Rod_Reference.fbx 是现有 /Game/Catfishing/Fishing/Presentation/SM_Rod_BendSource 的只读导出，保留原尺寸、默认隐藏。导入曾提示部分层数据缺失，不能视为材质完整还原。

独立 PROP_* 竹竿、卷线轮和浮漂用于钓鱼第一帧姿态展示，默认隐藏。它们是静态美术道具，尚未绑定双爪跟随或接入游戏鱼线。三猫动画视频不显示鱼竿。

## 生成与实时编辑

在独立后台 Blender 进程运行：

```text
blender --background --factory-startup --python Scripts/Art/create_cream_cat.py
blender --background SourceArt/Characters/CreamCat/CreamCat.blend --python Scripts/Art/inspect_cream_cat.py
blender --background --factory-startup --python Scripts/Art/preview_cream_cat.py
```

Store 安装可使用 blender-launcher.exe。后台脚本不需要第三方插件，不修改用户首选项。重建会更新本目录由脚本生成的同名文件，手工修改前请另存为其他文件名。

实时制作通过 Blender MCP 调用 refine_cream_cat_live.py 的 begin、body、face、silhouette、rig、animations、studio、save 阶段。begin 仅允许处理 CreamCat_Live.blend，并在 Saved/Art/CreamCat/Before_Refine_*.blend 备份非空场景。两条工作流共用造型、骨架和动画实现。

## 验证范围

- contract：实际生成后的网格连通/流形、权重、骨架与循环、形状键及驱动检查。
- runtime_behavior：已写关键帧的有限坐标、眨眼、体积、拉伸与脚底高度；未覆盖所有插值时刻或所有自交。
- presentation_delivery：本目录实际渲染与视频供造型审查；此阶段为样稿，未做 UE 导入、角色替换、联机或打包验收。

Saved/Art/CreamCat/inspection.json 保存几何测量；showcase_report.json 保存本次渲染结果、源文件前后哈希和位移采样。数值通过不能证明动作自然，旧失败报告也不能作为新视频的完成证据。

本次实测：身体为单连通闭合网格，40 个蒙皮网格和 10 个 Blink 驱动检查无失败；154 个已写关键帧求值没有非有限坐标。待机/行走最大局部边拉伸约 1.72/1.75 倍，最低脚底净空约 0.47 mm。钓鱼抬爪局部仍有约 2.37 倍边拉伸，保留为姿态样稿，不能当作正式动作变形验收。展示源文件哈希保持一致，步行演示前进约 0.919 米；MP4 容器实测为 192 帧、30 fps、6.4 秒、1280×720 H.264。

## 本轮实现对照

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 身体、短肢、肉垫 | cream_cat_body.py/build_body，由 make_model 和 live.body 调用 | 分层椭圆轮廓改连续梨形和短弯肢 | 同入口替换，保留 AUTO_BODY | 曲面先于合网与蒙皮 | 接缝、肉垫错投；正侧背和静止/姿态对照 | 新源文件与实际渲染；原分层入口移除 |
| 头身及五官 | build_face → fuse_head_and_body | 独立头改融合体，圆冠圆颏 | Cat_Body 唯一主网格；五官独立 | 投影后合网，再统一比例和蒙皮 | 眼睑穿插、旧头消费者；Blink及表面检查 | Cat_Head 消费者已迁移；实际渲染审形 |
| 骨架与动作 | create_cream_cat.py/make_rig/skin/make_actions | 短肢及姿态同步、减轻腹胯拉扯 | 顶点、骨架、目标共用米制变换 | 静止形体、蒙皮、动作顺序生成 | 循环、拉伸、穿地；关键帧测量 | 30 骨和三动作保存；具体指标见本次报告 |
| 展示及检查 | preview_cream_cat.py、inspect_cream_cat.py、README.md | 旧头和缩放眨眼口径迁移 | 保留源隐藏配件和形状键驱动 | 保存源后生成独立展示 | 源文件被改、中文节点、视频写出 | 节点按类型定位；哈希与视频结果写入报告 |
| 游戏运行链 | Source/、Content/、Config/及 UE 资产 | 本轮无游戏行为变更 | 不涉及入口、状态/复制、持久化、UI、Cook/打包或运行日志 | 不涉及 | 保留其他任务并行修改 | 美术只写 Scripts/Art、SourceArt 和忽略的 Saved/Art |
