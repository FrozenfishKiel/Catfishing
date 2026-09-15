#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatProfileSettings.generated.h"

/**
 * 本地永久档案与外部成像桥的显式配置边界。
 * 跟人走的三样（个人图鉴含剪影、印记相册索引、外观解锁清单）常态落盘，正式取值见 DefaultGame.ini 同名段；
 * 局中断点归世界槽，不从这里配置。外部成像桥仍 fail-closed，避免开发机占位图片被误认为产品格式与平台策略。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Profile"))
class CATFISHING_API UCatProfileSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 判断本地永久档案是否具有可用槽位；总开关与非空槽位基础名同时成立才允许读写。 */
	bool IsPersistenceReady() const;

	/** 判断外部本地成像实现是否已经接入；关闭时 CapturePlan 可以到达，但不能报告伪造成功。 */
	bool IsExternalImprintBridgeReady() const;

	/** 本地 SaveGame 持久化总开关；跟人走的三样已由 2026-09-09 存档范围裁决定案，默认开启，关闭即整局不落盘。 */
	UPROPERTY(Config, EditAnywhere, Category = "Persistence")
	bool bEnableProfilePersistence = true;

	/** 每个 LocalPlayer 存档槽位的基础名；实际槽位会追加 ControllerId，空值表示 Unset 并关闭持久化。 */
	UPROPERTY(Config, EditAnywhere, Category = "Persistence")
	FString SaveSlotBaseName = TEXT("CatProfile");

	/** 外部本地截图/编码/文件落盘桥已完成的显式 gate；默认关闭且本模块不提供占位图片。 */
	UPROPERTY(Config, EditAnywhere, Category = "Imprint")
	bool bEnableExternalImprintCaptureBridge = false;
};
