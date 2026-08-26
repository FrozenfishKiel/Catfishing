#pragma once

#include "CoreMinimal.h"
#include "Environment/CatWaterTypes.h"
#include "GameFramework/Actor.h"

#include "CatChumFieldAnchor.generated.h"

/** 自然事件聚鱼的关卡锚点；它把配置中的 AnchorId 映射到一个固定世界位置和明确 WaterRegion，不参与玩家交互或复制。 */
UCLASS(BlueprintType)
class CATFISHING_API ACatChumFieldAnchor : public AActor
{
	GENERATED_BODY()

public:
	/** 创建只读锚点 Actor；构造后它默认不 Tick、不复制且不碰撞，只作为 GameMode 扫描的静态事实。 */
	ACatChumFieldAnchor();

	/** 关卡内唯一锚点名字，表示 Environment 配置可引用的自然事件落点；数据人员写入，GameMode 按此字段查找唯一 Actor。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Chum")
	FName AnchorId = NAME_None;

	/** 锚点绑定的水域几何身份，表示自然事件窝点只能写入哪一个 WaterRegion；地图配置写入，GameMode 提交 ChumField 时读取并校验它有效。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Chum")
	FCatWaterRegionHandle ExpectedWaterRegionHandle;
};
