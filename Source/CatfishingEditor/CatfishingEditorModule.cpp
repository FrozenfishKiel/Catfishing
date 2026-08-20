#include "Modules/ModuleManager.h"

// 模块入口流程：Editor 工具模块没有启动期接线，Commandlet 由 UCLASS 反射注册即可被 -run= 找到，因此直接使用引擎默认模块实现。
IMPLEMENT_MODULE(FDefaultModuleImpl, CatfishingEditor);
