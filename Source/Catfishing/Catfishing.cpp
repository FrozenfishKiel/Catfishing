#include "Catfishing.h"
#include "CatLog.h"
#include "Modules/ModuleManager.h"

// 模块加载流程：注册各深模块稳定分类；不增加日志包装层，调用点直接保留真实生命周期、请求和事务字段。
DEFINE_LOG_CATEGORY(LogCatfishing);
DEFINE_LOG_CATEGORY(LogCatOnline);
DEFINE_LOG_CATEGORY(LogCatRun);
DEFINE_LOG_CATEGORY(LogCatEnvironment);
DEFINE_LOG_CATEGORY(LogCatFishing);
DEFINE_LOG_CATEGORY(LogCatItems);
DEFINE_LOG_CATEGORY(LogCatProfile);
DEFINE_LOG_CATEGORY(LogCatUI);
DEFINE_LOG_CATEGORY(LogCatSocial);
DEFINE_LOG_CATEGORY(LogCatCharacter);
IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, Catfishing, "Catfishing" );
