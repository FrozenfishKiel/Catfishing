#pragma once

#include "CoreMinimal.h"

// Catfishing 运行时装配与非 Online 领域共享的日志分类，避免玩法宿主依赖会话模块的诊断细节。
DECLARE_LOG_CATEGORY_EXTERN(LogCatfishing, Log, All);

// Online 会话、旅行、运输失败与 RequestId/epoch 共享的日志分类，便于独立核对阶段 B 异步链。
DECLARE_LOG_CATEGORY_EXTERN(LogCatOnline, Log, All);

// RunFlow、阶段、计时器、命令幂等、Revision 与 teardown 共享的日志分类，避免额度/StateTree 诊断混入 Online。
DECLARE_LOG_CATEGORY_EXTERN(LogCatRun, Log, All);

// Environment DTO 求值与公开环境快照使用的日志分类，便于证明环境实现没有反向写 Run。
DECLARE_LOG_CATEGORY_EXTERN(LogCatEnvironment, Log, All);

// Fishing 会话、StateTree 阶段、搏斗协作与首个合法抄网交接共享的日志分类，用于核对旧“双人抄网”没有回流。
DECLARE_LOG_CATEGORY_EXTERN(LogCatFishing, Log, All);

// Items 容器、捕获、转移、献祭预留、消费与偷鱼 escrow 共享的日志分类，用于追踪每条实物鱼的唯一事务边界。
DECLARE_LOG_CATEGORY_EXTERN(LogCatItems, Log, All);

// Profile Grant、Journal、图鉴与印记投递共享的日志分类，用于区分服务器投递、客户端 durable 与 ACK 三个事实。
DECLARE_LOG_CATEGORY_EXTERN(LogCatProfile, Log, All);

// LocalPlayer 页面、Widget 生命周期、输入意图和跨 World 重绑共享的日志分类，用于证明 View 没有持有领域写权。
DECLARE_LOG_CATEGORY_EXTERN(LogCatUI, Log, All);

// Social 求助、偷鱼、追回、恶作剧与防护牌共享的日志分类，用于核对权限、窗口和最大负面影响。
DECLARE_LOG_CATEGORY_EXTERN(LogCatSocial, Log, All);

// Character 身体、装备与失能恢复共享的日志分类，用于证明表现状态和生存数值没有写成第二份真相。
DECLARE_LOG_CATEGORY_EXTERN(LogCatCharacter, Log, All);
