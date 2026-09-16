#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatProfileSettings.generated.h"

/**
 * 本地永久档案与外部成像桥的显式配置边界。
 * 相册索引、外观解锁和装备选择使用本地档；个人图鉴由 ProfileSubsystem 单独按账号保存。
 * 局中断点归世界槽。外部成像桥仍 fail-closed，避免开发机占位图片被误认为正式媒体。
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

	/** 永久档案持久化开关；ProfileSubsystem 读取后决定是否加载与写入本地档和账号图鉴，不控制世界槽。 */
	UPROPERTY(Config, EditAnywhere, Category = "Persistence")
	bool bEnableProfilePersistence = true;

	/** 本地档槽位基础名；追加 ControllerId 保存相册等本地内容，编辑器图鉴另用其哈希隔离测试档，正式 Steam 图鉴不用它命名。空值关闭档案持久化。 */
	UPROPERTY(Config, EditAnywhere, Category = "Persistence")
	FString SaveSlotBaseName = TEXT("CatProfile");

	/** 外部本地截图/编码/文件落盘桥已完成的显式 gate；默认关闭且本模块不提供占位图片。 */
	UPROPERTY(Config, EditAnywhere, Category = "Imprint")
	bool bEnableExternalImprintCaptureBridge = false;
};
