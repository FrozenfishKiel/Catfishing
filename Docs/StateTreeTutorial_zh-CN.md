# StateTree 从零到能用（Catfishing 项目版）

面向没接触过 StateTree 的人。前半部分讲清概念，后半部分解释本项目的三棵树：整局流程、钓鱼会话，以及单条鱼的搏斗行为。

---

## 目录

- [第一部分：概念](#第一部分概念)
  - [1. StateTree 是什么](#1-statetree-是什么)
  - [2. 五个核心概念](#2-五个核心概念)
  - [3. Task 的生命周期和返回值](#3-task-的生命周期和返回值)
  - [4. ⚠️ State 什么时候算"完成"——最大的坑](#4-️-state-什么时候算完成最大的坑)
  - [5. Transition（过渡）](#5-transition过渡)
  - [6. Selection Behavior（选择行为）](#6-selection-behavior选择行为)
- [第二部分：编辑器界面](#第二部分编辑器界面)
- [第三部分：实操建 ST_RunFlow](#第三部分实操建-st_runflow)
- [第四部分：实操建 ST_FishingSession](#第四部分实操建-st_fishingsession)
- [第五部分：调试](#第五部分调试)

---

# 第一部分：概念

## 1. StateTree 是什么

一句话：**带层级的状态机 + 行为树的选择能力**。

对比你可能熟悉的东西：

| | 传统状态机 (FSM) | 行为树 (BT) | StateTree |
|---|---|---|---|
| 结构 | 平铺的状态 + 转移 | 树，每帧从根往下跑 | 树形状态 + 转移 |
| 谁决定下一步 | 转移条件 | 每帧重新遍历选节点 | 转移（事件/完成/条件） |
| 适合 | 阶段明确的流程 | 需要频繁重新决策的 AI | **阶段明确 + 需要层级复用** |

**本项目为什么用它**：钓鱼是一条阶段明确的长流程（等口 → 试探 → 搏斗 → 近岸），阶段拓扑要能让策划改，但数值和权威写入必须留在 C++。StateTree 正好——资产决定"什么时候进哪个阶段"，C++ 节点决定"进这个阶段要执行什么写入"。

**很重要的一点**：本项目的 StateTree **不做任何数值计算**。所有节点都只是调用 C++ 里已经写好的权威写口，返回成功/失败。你在树里配置的只有「顺序」和「什么事件触发什么跳转」。

---

## 2. 五个核心概念

### State（状态）

树上的一个节点，代表"当前处于哪个阶段"。可以嵌套（父状态 → 子状态）。

同一时刻，从 Root 到某个叶子的**整条路径上的状态都是"激活"的**。比如 `Root → Combat → Attacking`，那么 `Combat` 和 `Attacking` 的 Task 都在跑。

State 有几种类型（`Type` 属性）：

| 类型 | 含义 | 我们用不用 |
|---|---|---|
| **State** | 普通状态，能放 Task，也能有子状态 | ✅ 全部用这个 |
| Group | 只放子状态，自己不放 Task | 用不上 |
| Linked | 跳到树里另一个状态 | 用不上 |
| Linked Asset | 跳到另一个 StateTree 资产 | 用不上 |
| Subtree | 可被 Linked 引用的子树 | 用不上 |

### Task（任务）

挂在 State 上的**执行单元**。State 被激活时，它的所有 Task 依次执行；State 退出时，Task 被清理。

本项目的 Task 全部是 C++ 写好的，你只能挑选和配参数，不能写逻辑。

### Condition（条件）

布尔判断，用在两个地方：
- **Enter Conditions**：挂在 State 上，决定这个 State 能不能被选中
- **Transition Conditions**：挂在转移上，决定这条转移能不能走

本项目只有一个 Condition：`Cat Fishing Phase Is`（判断当前 Session 阶段），MVP 用不到。

### Transition（过渡）

"从这个 State 跳到那个 State"的规则。挂在 State 上，包含：
- **Trigger**（什么时候尝试跳）
- **Target**（跳到哪）
- 可选的 Condition 和 Priority

### Schema（模式）

决定这棵树**能拿到什么数据**、**能用哪些节点**。

本项目的树都以 **`StateTree Component Schema`** 为基础，它的关键属性是 **Context Actor Class** —— 指定这棵树挂在哪种 Actor 上。鱼行为树使用项目自定义的 `CatFishBehaviorStateTreeSchema`，本质仍是 Component Schema，但把 Context 限定为 `ACatFishEncounterActor`，避免把鱼 Task 错挂到别的 Actor。

> **这个必须设对。** 所有 C++ 节点内部都是 `Cast<ACatFishingSession>(Context.GetOwner())` 这种写法。Context Actor Class 设错，Cast 返回 null，节点直接返回 Failed，整棵树跑不起来，而且**不会有明显报错**。

---

## 3. Task 的生命周期和返回值

每个 Task 有三个时机：

```
EnterState()   ← State 被激活时调用一次
   ↓
Tick()         ← 每帧调用（如果 Task 开启了 tick）
   ↓
ExitState()    ← State 退出时调用一次
```

`EnterState()` 和 `Tick()` 都返回一个**运行状态**：

| 返回值 | 含义 |
|---|---|
| **Running** | 还在进行中，别动我 |
| **Succeeded** | 我做完了，成功 |
| **Failed** | 我失败了 |

### 本项目 Task 的返回值速查

| Task | EnterState 返回 | 会 Tick 吗 |
|---|---|---|
| `Cat Fishing Schedule Waiting Probe` | 调用成功→Succeeded，失败→Failed | ❌ |
| `Cat Fishing Enter Phase` | 阶段写入成功→Succeeded，被拒→Failed | ❌ |
| `Cat Fishing Open True Bite Window` | 成功打开响应窗→Succeeded，否则→Failed | ❌ |
| `Cat Fishing Start Fight Runner` | Runner 已跑或启动成功→Succeeded，否则→Failed | ❌ |
| `Cat Fishing Wait For Fight Runner` | Runner 在跑→Running，否则→Succeeded | ✅ 每帧检查 |
| **`Cat Fishing Wait`** | **永远 Running** | ❌ |
| `Cat Run Enter Phase` | 阶段写入成功→Succeeded，被拒→Failed | ❌ |
| **`Cat Run Wait For Event`** | **永远 Running** | ❌ |

注意最后两个加粗的 —— **`Wait` 类 Task 永远返回 Running，永远不会完成**。这是它们存在的唯一目的：**把 State 钉住，让树不结束**。

---

## 4. ⚠️ State 什么时候算"完成"——最大的坑

这是整篇文档最重要的一节。

### 规则

每个 State 有一个属性叫 **`Tasks Completion`**，两个取值：

| 取值 | 含义 |
|---|---|
| **`Any`**（**引擎默认值**） | **任何一个** Task 完成，State 就完成 |
| **`All`** | **所有** Task 都完成，State 才完成 |

任何一个 Task 返回 **Failed**，无论哪种设置，State 都**立刻判定为失败**。

### 为什么这是个坑

看我们要建的 `Probe` 状态，它有三个 Task：

```
Probe
  Task 1: Cat Fishing Enter Phase (Phase=Probe)        → Succeeded
  Task 2: Cat Fishing Open True Bite Window             → Succeeded
  Task 3: Cat Fishing Wait                             → Running（永远）
```

**用默认的 `Any`**：Task 1 一返回 Succeeded，State 立刻完成退出 —— Task 2、3 白搭，树直接跑飞。

**改成 `All`**：要三个都完成才算完成，而 Task 3 永远 Running，所以 State 永远保持激活，安静等事件。✅ 这才是我们要的。

### 所以：**凡是放了多个 Task 的 State，都要把 `Tasks Completion` 改成 `All`**

只有一个 Task 的 State（比如只放 `Cat Fishing Wait` 的 `NearShore`）两种设置都一样，不用管。

### 为什么不用另一种改法

Task 上还有个 **`Considered For Completion`** 勾选框，取消勾选也能让某个 Task 不参与完成判定。**但不要用这个方案** —— 我查过引擎源码，被排除的 Task **连 Failed 都不会传播**（`HasAnyFailed()` 会跳过它）。那样 `Enter Phase` 失败时你不会看到任何异常，State 会继续傻等，极难排查。

用 `Tasks Completion = All`，失败照样能传播出来。

---

## 5. Transition（过渡）

### Trigger 类型（引擎全部选项）

| Trigger | 什么时候尝试跳转 |
|---|---|
| **On State Completed** | State 成功**或**失败时 |
| **On State Succeeded** | State 成功时 |
| **On State Failed** | State 失败时 |
| **On Tick** | 每帧都尝试（配合 Condition 用） |
| **On Event** | 收到指定 Gameplay Tag 事件时 |
| On Delegate | 收到委托时（本项目用不到） |

本项目主要用 **On Event** 和 **On State Succeeded**。

### Target 类型

| Target | 含义 |
|---|---|
| **指定某个 State** | 跳到你选的那个状态 |
| Next State | 跳到同级的下一个兄弟状态 |
| Next Selectable State | 跳到下一个 Enter Condition 通过的兄弟状态 |
| Tree Succeeded | **整棵树结束**（成功） |
| Tree Failed | **整棵树结束**（失败） |
| None | 不跳，什么也不做 |

> ⚠️ **本项目千万别用 `Tree Succeeded` / `Tree Failed`。** 树一旦结束，`StateTreeComponent->IsRunning()` 变 false，而 C++ 里所有权威写口（`EnterPhaseFromStateTree` 等）都要求树在运行中，之后所有阶段写入都会被拒。

### On Event 怎么配

选 `On Event` 后会多出一个 **Event Tag** 字段，填 Gameplay Tag。C++ 那边用 `SendStateTreeEvent(Tag)` 发事件，Tag 对上了就触发。

**本项目 C++ 实际会发的事件（只有这些，别等其他的）**：

`ST_FishingSession` 能收到：
```
Cat.Fishing.Event.ProbeTriggered    ← 咬钩计时器到期
Cat.Fishing.Event.HookAccepted      ← 真咬窗口内成功提竿
Cat.Fishing.Event.WindowExpired     ← 真咬窗口超时（Probe 接回 Waiting）
Cat.Fishing.Event.EarlyHook         ← 过早提竿（发完就停树，收不到）
Cat.Fishing.Event.Interrupted       ← 主动取消（发完就停树，收不到）
```

`ST_RunFlow` 能收到：
```
Cat.Run.QuotaReached
Cat.Run.QuotaFailed
Cat.Run.AllEligibleReady
Cat.Run.SettlementComplete
```

> 头文件里声明了 16 个 fishing 事件，但**只有上面 5 个有发送点**，其余 11 个（`CastLanded`、`ScoopCommitted`、`RodBroken`…）在代码里没有任何地方发送。别在资产里等它们。

> `WindowExpired` 是例外：它不再调用 `FinalizeSession()`，必须由 Probe 状态接回 Waiting。只有 `EarlyHook` 和 `Interrupted` 发出后会立即由 C++ 收口终态并停树。

---

## 6. Selection Behavior（选择行为）

State 的另一个属性，决定"这个 State 被考虑时，是自己上还是让子状态上"。

| 取值 | 含义 |
|---|---|
| **Try Select Children In Order**（**默认**） | 按顺序尝试选第一个子状态；没有子状态就自己上 |
| Try Enter | 即使有子状态，也是自己上 |
| Try Follow Transitions | 不进入，直接走转移 |
| Try Select Children At Random / Utility 系列 | 随机/按效用选子状态（AI 用，我们用不上） |

当前三棵树都是**扁平的**（Root 下面一排平级状态，没有嵌套），所以**保持默认就行**，不用动。

树启动时，从 Root 开始按默认行为选择 —— 会选中 **Root 的第一个子状态**。所以**子状态的排列顺序决定了哪个是起始状态**。

---

# 第二部分：编辑器界面

打开一个 StateTree 资产，界面大致分这几块：

```
┌─────────────────────────────────────────────────────────┐
│  工具栏： [Compile] [Save] ...                           │
├──────────────────────┬──────────────────────────────────┤
│                      │                                  │
│   StateTree 主视图    │        Details 面板              │
│   （状态层级列表）     │   （选中什么就显示什么的属性）      │
│                      │                                  │
│   Root               │   选中 State 时显示：              │
│    ├── DayActive     │     - Type                       │
│    ├── NormalNight   │     - Selection Behavior         │
│    └── FailureNight  │     - Tasks Completion  ←重要     │
│                      │     - Enter Conditions           │
│                      │     - Tasks         ←在这加任务    │
│                      │     - Transitions   ←在这加过渡    │
├──────────────────────┴──────────────────────────────────┤
│  Compiler Results / 输出                                 │
└─────────────────────────────────────────────────────────┘
```

**常用操作**：

| 想做什么 | 怎么做 |
|---|---|
| 加一个状态 | 主视图空白处右键 → `Add State`，或选中 Root 右键 → `Add Child State` |
| 重命名状态 | 双击状态名 |
| 加 Task | 选中状态 → Details 面板 `Tasks` 分类 → 点 `+` → 从下拉里搜节点名 |
| 加 Transition | 选中状态 → Details 面板 `Transitions` 分类 → 点 `+` |
| 改 Tasks Completion | 选中状态 → Details 面板 `State` 分类里找 |
| 调整状态顺序 | 主视图里拖拽 |
| **编译** | 工具栏 `Compile`（**改完必须编译，否则运行时用的还是旧的**） |

> Schema 设置在哪：主视图里**选中 Root**（或者点空白处），Details 面板顶部就有 `Schema` 相关设置，包括 **Context Actor Class**。不同小版本位置略有差异，找不到就在 Details 面板搜索框里输 `Context`。

---

# 第三部分：实操建 ST_RunFlow

先建这棵，它更简单，适合练手。而且**不建这棵，钓鱼根本开不了工** —— `BeginCast` 第一道校验就是 `GameMode->CanAcceptGameplayCommand()`，而这个开关只有 Run 进入 `DayActive` 阶段才会打开。

## 3.1 创建资产

1. Content Browser → 进入或新建 `/Game/Data/StateTrees`
2. 右键 → **Artificial Intelligence → State Tree**
3. 弹出 Schema 选择框 → 选 **`StateTree Component Schema`**
4. 命名 **`ST_RunFlow`**

## 3.2 设 Context Actor Class

打开资产 → 选中 Root → Details 面板找到 **Context Actor Class** → 设为 **`CatfishingGameModeBase`**

## 3.3 建三个状态

在 Root 下建三个子状态（右键 Root → Add Child State），依次命名：

```
Root
 ├── DayActive        ← 第一个 = 起始状态
 ├── NormalNight
 └── FailureNight
```

**顺序很重要**：`DayActive` 必须是第一个，因为树启动时会选中 Root 的第一个子状态。

## 3.4 配置 DayActive

选中 `DayActive`：

**State 分类**
- `Tasks Completion` → 改成 **`All`** ⚠️

**Tasks 分类**（点 `+` 加，注意顺序）

| # | Task | 参数 |
|---|---|---|
| 1 | `Cat Run Enter Phase` | `Phase` = `DayActive`，`Reason` = `None` |
| 2 | `Cat Run Wait For Event` | 无参数 |

**Transitions 分类**（加两条）

| # | Trigger | Event Tag | Target |
|---|---|---|---|
| 1 | `On Event` | `Cat.Run.QuotaReached` | `NormalNight` |
| 2 | `On Event` | `Cat.Run.QuotaFailed` | `FailureNight` |

## 3.5 配置 NormalNight

- `Tasks Completion` → **`All`**
- Tasks：
  1. `Cat Run Enter Phase`，`Phase` = `NormalNight`，`Reason` = `QuotaReached`
  2. `Cat Run Wait For Event`
- Transitions：
  1. `On Event`，Tag = `Cat.Run.AllEligibleReady`，Target = `DayActive`

## 3.6 配置 FailureNight

- `Tasks Completion` → **`All`**
- Tasks：
  1. `Cat Run Enter Phase`，`Phase` = `FailureSettlementNight`，`Reason` = `QuotaFailed`
  2. `Cat Run Wait For Event`
- Transitions：
  1. `On Event`，Tag = `Cat.Run.SettlementComplete`，Target = `DayActive`

## 3.7 编译保存

点工具栏 **Compile**，看 Compiler Results 没报错，然后 Save。

> **不要接 `SuccessSettlementNight`**：`EnterRunPhaseFromStateTree` 里有硬校验，`SuccessSettlementPolicy != Enabled` 时进这个阶段直接返回 `PolicyUndecided`（当前 ini 没启用这条策略）。

> **嫌白天太短**：`DayActive` 会启动一个 `DayLengthSeconds`（ini 里配的 600 秒）倒计时，到期额度不够就发 `QuotaFailed` 跳夜晚。测试期间可以把 ini 里的值调大，改完重启编辑器。

---

# 第四部分：实操建 ST_FishingSession

## 4.1 先理解这棵树的分工

**这棵树没有你想象中那么"主导"。** 大部分阶段其实是 C++ 自己写的，树只负责起头和等事件：

| 阶段 | 谁写进去的 |
|---|---|
| Waiting | `Schedule Waiting Probe` 这个 Task **内部自己**调 EnterPhase |
| Probe | 树的 `Cat Fishing Enter Phase` Task |
| TrueBiteWindow | `Open True Bite Window` 打开通用窗口；只播放浮漂下沉，不创建鱼 |
| HookedFight | 真咬窗内收到左键后，`RequestHook` 才选鱼、生成 Actor、扣饵并 EnterPhase |
| NearShore | 搏斗 Runner 跑完后 **C++ 内部** EnterPhase |
| Resolved / Terminated | `FinalizeSession()`，**树禁止进入** |

所以这棵树只有 4 个状态，逻辑非常薄。

## 4.2 创建资产

同样 **Artificial Intelligence → State Tree** → Schema 选 **`StateTree Component Schema`** → 命名 **`ST_FishingSession`**

**Context Actor Class 设为 `CatFishingSession`**

## 4.3 建四个状态

```
Root
 ├── Waiting          ← 起始状态，树启动就进这里
 ├── Probe
 ├── HookedFight
 └── NearShore
```

## 4.4 配置 Waiting

- `Tasks Completion` → **`All`** ⚠️
- Tasks：
  1. `Cat Fishing Schedule Waiting Probe`（无参数）
  2. `Cat Fishing Wait`（无参数）
- Transitions：
  1. `On Event`，Tag = `Cat.Fishing.Event.ProbeTriggered`，Target = `Probe`

**这个状态在干嘛**：树启动（`BeginCast` 里调 `StartLogic()`）就进这里。Task 1 内部把阶段写成 `Waiting`，并按泊松分布算出一个咬钩延迟、起一个计时器。然后 Task 2 把状态钉住。计时器到期时 C++ 发 `ProbeTriggered`，跳 `Probe`。

## 4.5 配置 Probe

- `Tasks Completion` → **`All`** ⚠️
- Tasks（**顺序不能错**）：
  1. `Cat Fishing Enter Phase`，`Phase` = **`Probe`**
  2. `Cat Fishing Open True Bite Window`（无参数）
  3. `Cat Fishing Wait`（无参数）
- Transitions：
  1. `On Event`，Tag = `Cat.Fishing.Event.HookAccepted`，Target = `HookedFight`
  2. `On Event`，Tag = `Cat.Fishing.Event.WindowExpired`，Target = `Waiting`

**顺序为什么不能错**：Task 2 内部第一件事就是检查 `Snapshot.Phase == Probe`，这个前提由 Task 1 建立。Task 1 没先跑，Task 2 直接失败。

**这个状态在干嘛**：Task 2 只把阶段切到 `TrueBiteWindow`、让浮漂猛沉并启动通用响应计时器。玩家在窗内按左键后，`RequestHook` 才冻结选鱼上下文（水域、窝料、时段、天气、饵料、参战能力和本轮种子）、抽鱼、生成鱼 Actor并真正扣饵。窗口超时则沿 `WindowExpired → Waiting` 回到慢浮等待，不结束架杆会话；下一轮会派生新的服务器随机种子。

## 4.6 配置 HookedFight

- `Tasks Completion` → **`All`** ⚠️
- Tasks：
  1. `Cat Fishing Start Fight Runner`（无参数）
  2. `Cat Fishing Wait For Fight Runner`（无参数）
- Transitions：
  1. **`On State Succeeded`**，Target = `NearShore`

**注意这条转移和前面的不一样** —— 不是 On Event，是 **On State Succeeded**。

**原理**：Task 2 每帧检查搏斗 Runner 还在不在跑。鱼被拉到近岸时，C++ 会 `Runner->Stop()` 并把阶段写成 `NearShore`。Task 2 下一帧发现 Runner 停了，返回 Succeeded。此时 Task 1 早就 Succeeded 了，两个都完成 → State 完成（Succeeded）→ 触发转移。

> 这里正好体现 `Tasks Completion = All` 的必要性：如果是默认的 `Any`，Task 1 一 Succeeded 就立刻跳走了，搏斗根本没机会跑。

## 4.7 配置 NearShore

- Tasks：
  1. `Cat Fishing Wait`（无参数）
- Transitions：**不配**
- `Tasks Completion`：只有一个 Task，不用管

**这个状态在干嘛**：什么也不干，就是把树钉住别结束。玩家按抢抄键后，`RequestScoop` 走完捕获事务，`FinalizeSession()` 会停掉整棵树。

> **不要在这里放 `Cat Fishing Enter Phase (NearShore)`** —— C++ 已经写过了，重复写只会白白递增 Revision。

## 4.8 编译保存

**Compile** → 检查 Compiler Results → **Save**。

## 4.9 五条铁律（贴墙上）

1. **树永远不能自然结束。** 每个叶子状态都要有一个 `Cat Fishing Wait` 钉住。转移 Target 绝不能选 `Tree Succeeded` / `Tree Failed`。
2. **多 Task 的 State 必须设 `Tasks Completion = All`。**
3. **`Cat Fishing Enter Phase` 的 Phase 不能选 `Resolved` 或 `Terminated`。** 选了会返回 `AlreadyResolved` → Failed。
4. **`NearShore` 不要再放 Enter Phase。**
5. **不用为超时/空竿/取消配转移。** C++ 发完事件立刻停树，那些事件收不到。

## 4.10 用不上的三个节点

搜节点时你会看到这几个，MVP 别碰：

| 节点 | 为什么不用 |
|---|---|
| `Cat Fishing Fight Exchange` | 实现开头就检查 `FightRunner->IsRunning()`，Runner 在跑时直接拒绝。搏斗数值全在 Runner 里 |
| `Cat Fishing Commit Failure Budget` | 失败惩罚（丢饵/伤竿）当前由 C++ 路径处理 |
| `Cat Fishing Resolve Retry Exhausted Escape` | 剪影图鉴终态，MVP 范围外 |

---

# 第五部分：单条鱼行为树 ST_FishFight

这棵树挂在 `ACatFishEncounterActor` 的 `FishBehaviorStateTree` 组件上，并且**只在服务器启动**。默认拓扑很小：

```text
Hooked Fish Behavior
 ├─ Struggling Outward       [Cat Fish Behavior State: StrugglingOutward]
 │        └─ On State Completed → Calm Direction Selection
 └─ Calm Direction Selection [Cat Fish Behavior State: CalmOrInward]
          └─ On State Completed → Struggling Outward
```

每个 Task 进入时只做两件事：把 `MotionIntent` 交给 FightRunner，并从鱼性格 DA 的时长区间抽出本状态持续时间；倒计时结束后 Task 成功，让树切到另一个状态。位置、转向、鱼线、力量、体力和鱼竿磨损全部仍由服务器固定步模拟器处理。

使用自定义 `CatFishBehaviorStateTreeSchema` 的好处是编辑器会把 Context Actor 限定为鱼 Encounter，鱼专用 Task 不会误挂到 Session 或 GameState。编译好 Editor 模块后，可用命令行编辑器稳定生成/重建默认资产：

```text
D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe D:/develop/Catfishing/Catfishing.uproject -ExecutePythonScript=D:/develop/Catfishing/Scripts/create_fish_behavior_state_tree.py -unattended -nop4 -NullRHI
```

未来添加“低体力蓄力冲刺”时，推荐新增一个 StateTree 状态和一个新的 `MotionIntent`，条件只负责决定何时进入；冲刺速度、体力门槛与网络结果仍写在纯 C++ 模拟层并加单元测试。这样 StateTree 是可视化编排，不会变成无法验证的第二套战斗逻辑。

---

# 第六部分：调试

## 编译期

改完**一定要点 Compile**。Compiler Results 里的常见报错：

| 报错大意 | 原因 |
|---|---|
| 节点找不到 / 无效 | Context Actor Class 没设或设错，节点不兼容当前 Schema |
| Transition target 无效 | 目标状态被删了或改名了 |

## 运行期

**看日志**。Output Log 过滤这些关键字：

```
LogCatRun        ← Run 阶段变化
LogCatFishing    ← 钓鱼阶段变化
```

**期望看到的日志序列**（一次完整流程）：

```
Event=run_started ...
Event=run_phase_entered ... Phase=DayActive         ← ST_RunFlow 工作正常
...（抛竿后）
Event=fishing_phase_entered ... Phase=Waiting        ← ST_FishingSession 启动
Event=fishing_phase_entered ... Phase=Probe          ← ProbeTriggered 转移成功
Event=fishing_phase_entered ... Phase=TrueBiteWindow ← 选鱼成功（C++ 直接写的）
Event=fishing_phase_entered ... Phase=HookedFight    ← 提竿成功
Event=fishing_phase_entered ... Phase=NearShore      ← 搏斗结束
Event=fishing_session_resolved ...                   ← 抄鱼成功
```

**哪一步没出现，问题就在那一步的转移或 Task 上。**

## StateTree Debugger

UE 自带可视化调试器：菜单 **Tools → Debug → StateTree Debugger**（不同版本位置可能在 Window 菜单下）。PIE 运行时能看到当前激活的状态、Task 状态、事件流。

比看日志直观，但需要先在项目设置里开启 Trace。如果一时开不起来，看日志足够定位问题。

## 症状对照表

| 症状 | 大概率原因 |
|---|---|
| 树完全不启动，没有任何 `fishing_phase_entered` | ini 里 `FishingSessionStateTree` 没填，或路径写错 |
| 只有 `Phase=Waiting` 就再也不动了 | `ProbeTriggered` 转移没配，或 Event Tag 拼错 |
| `Phase=Waiting` 都没有 | Context Actor Class 设错，或 `Schedule Waiting Probe` 返回了 Failed（检查 ini 里 `BaseBiteRatePerSecond` 等三个咬钩参数是否都 > 0） |
| 进了 Probe 但没有 TrueBiteWindow | 鱼表没匹配上 —— 检查 `DA_Fish_Test01` 的 `RegionIds` 是否等于关卡里 Region 的 `RegionId` |
| 阶段跳得飞快，几帧就跑完 | **`Tasks Completion` 忘了改成 `All`** |
| 提竿后没进 HookedFight | 提竿时机不在 TrueBiteWindow 窗口内（默认 3 秒） |
| 搏斗一进去就跳 NearShore | 同样是 `Tasks Completion` 的问题 |

---

## 附：本项目节点速查

**ST_FishingSession 可用节点**（Context = `CatFishingSession`）

| 节点 | 类型 | 参数 | 返回 |
|---|---|---|---|
| `Cat Fishing Schedule Waiting Probe` | Task | — | Succeeded / Failed |
| `Cat Fishing Enter Phase` | Task | `Phase` | Succeeded / Failed |
| `Cat Fishing Open True Bite Window` | Task | — | Succeeded / Failed |
| `Cat Fishing Start Fight Runner` | Task | — | Succeeded / Failed |
| `Cat Fishing Wait For Fight Runner` | Task | — | Running → Succeeded |
| `Cat Fishing Wait` | Task | — | 永远 Running |
| `Cat Fishing Phase Is` | Condition | `ExpectedPhase` | bool |

**ST_RunFlow 可用节点**（Context = `CatfishingGameModeBase`）

| 节点 | 类型 | 参数 | 返回 |
|---|---|---|---|
| `Cat Run Enter Phase` | Task | `Phase`, `Reason` | Succeeded / Failed |
| `Cat Run Wait For Event` | Task | — | 永远 Running |
| `Cat Run Result Reason` | Condition | `ExpectedReason` | bool |

**枚举取值**

```
ECatFishingPhase:
  Created, Probe, TrueBiteWindow, HookedFight, NearShore,
  Resolved(禁), Terminated(禁), CastFlight, Waiting, AutoHauling

ECatRunPhase:
  NotStarted, DayActive, NormalNight, FailureSettlementNight,
  SuccessSettlementNight(禁), Ending, Ended

ECatRunTransitionReason:
  None, QuotaReached, QuotaFailed, AllEligibleReady,
  SettlementComplete, HostExit, NaturalEnd
```

---

**建完三棵树别忘了**：把路径写进 `Config/DefaultGame.ini`，然后**重启编辑器**。

```ini
[/Script/Catfishing.CatRunSettings]
RunFlowStateTree=/Game/Data/StateTrees/ST_RunFlow.ST_RunFlow

[/Script/Catfishing.CatFishingSettings]
FishingSessionStateTree=/Game/Data/StateTrees/ST_FishingSession.ST_FishingSession

FishBehaviorStateTree=/Game/Data/StateTrees/ST_FishFight.ST_FishFight
```
