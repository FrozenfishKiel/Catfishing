#include "Profile/CatProfileSettings.h"

// 持久化 gate 流程：同时要求显式启用和非空槽位基础名；任何未配置状态都禁止创建或覆盖 SaveGame。
bool UCatProfileSettings::IsPersistenceReady() const
{
	return bEnableProfilePersistence && !SaveSlotBaseName.TrimStartAndEnd().IsEmpty();
}

// 成像 gate 流程：外部桥只有在永久档案也可用时才成立，避免成功图片没有 durable Grant 落点。
bool UCatProfileSettings::IsExternalImprintBridgeReady() const
{
	return IsPersistenceReady() && bEnableExternalImprintCaptureBridge;
}
