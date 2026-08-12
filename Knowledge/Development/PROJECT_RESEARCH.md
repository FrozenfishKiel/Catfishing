# 项目调研基线

**更新时间：** 2026-08-10  
**文档状态：** 已核实的开发基线；随工程、构建环境或已裁决规则变化更新。  
**范围：** 当前 UE 工程、可用开发环境、已确认的开发口径，以及进入技术方案讨论前仍未裁决的边界。

## 权威与使用边界

- 当前工程事实以 `Catfishing.uproject`、`Source/`、`Config/`、`Content/` 和真实构建证据为准，优先于 GDD 对“已实现状态”的描述。
- 用户在本轮确认的开发口径覆盖旧 GDD 分册的冲突说法；其中不覆盖最终技术方案，也不替代后续玩法平衡裁决。
- 本文不声明任何尚未落地的玩法、资产、网络方案或持久化方案已经实现；也不作为最终技术方案。

## 当前结论

开发环境已具备开工和构建条件；工程本身仍是一个可编译的 UE 空模板。首个可玩版本所需的玩法代码、项目地图、占位资产、输入资产与联机接线均尚未建立。

## 已验证环境与构建证据

- Unreal Engine 根目录：`D:\UE_5.8`；版本：UE 5.8.1，CL 56057345。
- 编译工具链：Visual Studio Community 2026 18.8.2；MSVC `cl` 19.51.36252 x64；Windows SDK 22621 与 26100；.NET SDK 10.0.302。
- 构建入口：`D:\UE_5.8\Engine\Build\BatchFiles\Build.bat`。
- 2026-08-10 21:20:29 已执行并成功：

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\UnreaProjects\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReload
```

  Exit code 为 `0`；UBT 输出 `Target is up to date`、`Result: Succeeded`，总耗时 2.43 秒。本轮复跑同一目标同样成功，耗时 0.65 秒。现场日志为 `%LOCALAPPDATA%\UnrealBuildTool\Log.txt`，Trace 为 `%LOCALAPPDATA%\UnrealBuildTool\Trace.uba`；它们会被后续构建覆盖，不能作为长期不可变证据。
- 此证据仅证明当前空模板基线的 Editor Target 可构建；尚未验证新增代码后的冷编译，也未验证 Editor 启动、Cook、Package 或多人 PIE。

## 工程基线

- `Catfishing.uproject` 的 `EngineAssociation` 为 `5.8`；仅声明 `Catfishing` Runtime 模块。
- `Source/Catfishing.Target.cs` 与 `Source/CatfishingEditor.Target.cs` 仅提供 Game 和 Editor Targets。`Source/Catfishing/Catfishing.Build.cs` 的模块依赖仅为 `Core`、`CoreUObject`、`Engine`、`InputCore`。
- `Source/Catfishing/` 只有默认模块入口 `IMPLEMENT_PRIMARY_GAME_MODULE` 和无玩法内容的 `MyClass`；不存在 Pawn、Character、Controller、GameMode、GameState、GameInstance、Subsystem、RPC 或复制实现。
- `Content/` 为 0 文件、0 地图。`Config/DefaultEngine.ini` 的 `GameDefaultMap` 指向引擎模板路径 `/Engine/Maps/Templates/OpenWorld`，不是项目地图。
- `Config/DefaultInput.ini` 仅把默认输入类设为 Enhanced Input 的类；没有项目 Action Mapping、Input Action 或 Mapping Context 资产，`Catfishing.Build.cs` 也未接入 `EnhancedInput` 模块。
- `Config/DefaultGame.ini` 除通用 `ProjectID` 外，只有 CommonUI 相关配置痕迹；没有 CommonUI 模块依赖、业务 UI 代码或 UI 资产。Enhanced Input 与 CommonUI 都只是配置痕迹，不能视为已完成接线。
- 未发现 OnlineSubsystem、会话、NetDriver、复制、Steam 或 EOS 的配置与项目实现。
- `Plugins/Developer/RiderLink/` 是 Rider IDE 的 EditorNoCommandlet 开发插件；`.uproject` 中的 `ModelingToolsEditorMode` 也只允许 Editor Target。二者均非游戏业务插件。

## 已确认的开发口径

- 团队当前职责是把项目开发落地；策划中尚未交付的美术不得阻塞开发。缺少资源时可使用白盒、白模或简单占位表现。
- 鱼归第一个成功抄中的玩家；网络权威与并发判序留待技术方案确定。
- 数值、节奏、状态压力、成长收益及其他非程序裁决不阻塞原型。实现时使用可调参数或明确的原型默认值，试玩后再确定最终平衡。

## GDD 的稳定开发约束

以下是进入技术方案时应保留的产品边界，不表示已有工程实现：

- 游戏是 1–8 人局制，并且单人可玩；共享局内状态需要权威裁决。
- 实物鱼与图鉴/印记记录是不同对象；局内状态在局末清空，图鉴和印记跨局保留。
- 大鱼合力发生在搏斗阶段；鱼近岸后进入抢抄，合法抢抄成功者取得鱼。
- 装备与外观的“解锁”跟人走；待定的是解锁的具体形态、收益与实现方式。此为产品规则，不表示已有工程实现。

来源：`Knowledge/GDD/GDD 系统分册.md`、`Knowledge/GDD/局与进程.md`、`Knowledge/GDD/钓鱼系统.md`、`Knowledge/GDD/联机社交.md`、`Knowledge/GDD/鱼类图鉴.md`、`Knowledge/GDD/印记图鉴.md`，以及本轮用户确认。

## 可延后且不构成原型阻塞的事项

美术资源、完整鱼表、最终平衡、C4/C5/C7、种花等内容可以后置。后置不等于废弃：当其进入实现范围时，应先补齐对应规则和验收方式。

## 下一阶段技术方案待讨论项

以下按对首个可玩原型的阻塞顺序排列，均未拍板：

1. 首个可玩闭环及其验收标准。
2. 目标平台与联机承载方式。
3. 局内状态的所有权与生命周期。
4. 服务器钓鱼与抢抄状态机。
5. 身份实体与配置数据模型。
6. C++、蓝图与占位表现之间的职责切缝。
7. 跨局存储。
8. 印记系统的技术路线。
9. 多人验证矩阵。

## 已知文档冲突

`Knowledge/GDD/联机社交.md` 中的“双人抄网”是旧说法。当前以 `Knowledge/GDD/钓鱼系统.md` 的近岸抢抄描述和本轮用户确认的“谁拿到归谁”为准：大鱼可以在搏斗阶段合力，近岸鱼由首个合法抢抄成功者取得。

## 事实来源

- 工程描述与模块：`Catfishing.uproject`、`Source/Catfishing.Target.cs`、`Source/CatfishingEditor.Target.cs`、`Source/Catfishing/Catfishing.Build.cs`、`Source/Catfishing/Catfishing.cpp`、`Source/Catfishing/MyClass.*`。
- 配置与资产：`Config/DefaultEngine.ini`、`Config/DefaultGame.ini`、`Config/DefaultInput.ini`、`Content/`（2026-08-10 清点为空）。
- 插件：`Plugins/Developer/RiderLink/RiderLink.uplugin`、`Catfishing.uproject` 的插件声明。
- 构建与环境：本节列出的 2026-08-10 UBT 命令及其已观察结果；`%LOCALAPPDATA%\UnrealBuildTool\Log.txt` 与 `Trace.uba` 是可被后续构建覆盖的现场文件。
- 产品约束：`Knowledge/GDD/GDD 系统分册.md`、`Knowledge/GDD/局与进程.md`、`Knowledge/GDD/钓鱼系统.md`、`Knowledge/GDD/联机社交.md`、`Knowledge/GDD/鱼类图鉴.md`、`Knowledge/GDD/印记图鉴.md`，以及本轮用户确认。用户确认只覆盖其明确裁决的冲突，不推导为其他未裁决设计。
