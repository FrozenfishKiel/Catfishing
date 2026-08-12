#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatProfileSettings.generated.h"

/**
 * 本地永久档案与外部成像桥的显式配置边界。
 * 默认值全部 fail-closed，避免开发机占位存档或图片目录被误认为产品格式与平台策略。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Profile"))
class CATFISHING_API UCatProfileSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 判断本地永久档案是否具有可用槽位；Shipping 与开发构建都必须由正式配置显式启用。 */
	bool IsPersistenceReady() const;

	/** 判断外部本地成像实现是否已经接入；关闭时 CapturePlan 可以到达，但不能报告伪造成功。 */
	bool IsExternalImprintBridgeReady() const;

	/** 本地 SaveGame 持久化总开关；默认关闭，防止阶段代码写入未经裁决的玩家数据。 */
	UPROPERTY(Config, EditAnywhere, Category = "Persistence")
	bool bEnableProfilePersistence = false;

	/** 每个 LocalPlayer 存档槽位的基础名；实际槽位会追加 ControllerId，空值表示 Unset。 */
	UPROPERTY(Config, EditAnywhere, Category = "Persistence")
	FString SaveSlotBaseName;

	/** 外部本地截图/编码/文件落盘桥已完成的显式 gate；默认关闭且本模块不提供占位图片。 */
	UPROPERTY(Config, EditAnywhere, Category = "Imprint")
	bool bEnableExternalImprintCaptureBridge = false;
};
