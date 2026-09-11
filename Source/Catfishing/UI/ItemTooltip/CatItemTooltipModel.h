#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CatItemTooltipModel.generated.h"

class UCatInventoryItemInstance;
class UTexture2D;

/** 悬停框的一次只读投影；它不持有库存位置、权限或可写玩法状态。 */
USTRUCT(BlueprintType)
struct FCatItemTooltipViewData
{
	GENERATED_BODY()

	/** 物品定义提供的玩家名称；Model 写入，View 显示。 */
	UPROPERTY(BlueprintReadOnly)
	FText Name;
	/** 物品定义提供的说明正文；Model 写入，保留原换行供 View 显示。 */
	UPROPERTY(BlueprintReadOnly)
	FText Description;
	/** 当前实例的补充文本；只有鱼重量或鱼竿耐久，空值令 View 收起该区域。 */
	UPROPERTY(BlueprintReadOnly)
	FText InstanceDetails;
	/** 当前提示应展示的缩略图资源，代表物品定义软引用在本次投影中的解析结果；View 只消费它来显示或收起图片区，不缓存上一件物品图片。 */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UTexture2D> Icon = nullptr;
};

/** 悬停信息 MVC 的只读 Model；把物品定义与实例投影为文本，不订阅全库存或改写物品。 */
UCLASS()
class CATFISHING_API UCatItemTooltipModel : public UObject
{
	GENERATED_BODY()
public:
	/** 从当前实例生成显示数据；无定义返回 false 并清空输出，不猜测尚未同步的物品身份。 */
	bool BuildViewData(const UCatInventoryItemInstance* Instance, FCatItemTooltipViewData& OutData) const;
};
