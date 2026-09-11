#pragma once

#include "CoreMinimal.h"
#include "CatWorldInfoTypes.generated.h"

class UTexture2D;

/** 每个对象独立选择的信息展开方式；只控制本地阅读，不授予交互权限。 */
UENUM(BlueprintType)
enum class ECatWorldInfoPolicy : uint8
{
	/** 距离内直接展开完整信息。 */
	NearbyFull,
	/** 距离内先看摘要，准星命中本对象才展开详情。 */
	NearbySummary,
	/** 只有准星命中本对象时显示。 */
	FocusOnly
};

/** 本次观察应显示的内容层级；Hidden 同时用于对象自定义的拒绝显示。 */
UENUM(BlueprintType)
enum class ECatWorldInfoDetail : uint8 { Hidden, Summary, Full };

/** 一行只读信息；稳定键供自定义 WBP 定位字段，不向控件暴露可写玩法对象。 */
USTRUCT(BlueprintType)
struct FCatWorldInfoRow
{
	GENERATED_BODY()
	/** 字段的稳定语义标识；提供者填写，自定义 WBP 可按此选择自己的布局。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName Id;
	/** 数值代表的概念名称；提供者生成本地化文本，信息牌只显示。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText Label;
	/** 提供者写入、行 WBP 读取的格式化结果，包含必要单位；不可用须明确写出，不能用零伪装缺失。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText Value;
	/** 提供者选择的可选字段图标；为空时行 WBP 隐藏图像，正式模板仍保留固定宽度图标列以保持文字对齐。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UTexture2D> Icon = nullptr;
	/** 提供者写入的归一化进度；负数或非有限值表示不显示进度，行 WBP 将有效值限制在零到一。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Progress = -1.0f;
	/** 该行是否属于近处摘要；详情显示所有行，摘要只留下提供者选择的重点。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bSummary = true;
};

/** 一次观察的数据快照；不复制、不持有命令入口，所有业务状态仍归原系统。 */
USTRUCT(BlueprintType)
struct FCatWorldInfoViewData
{
	GENERATED_BODY()
	/** 对象的玩家可见名称；提供者写入，WBP 在标题区显示。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText Title;
	/** 提供者写入的当前阶段或不可操作原因；WBP 显示文本，空值时隐藏文本控件，不决定实际交互资格。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText Status;
	/** 本次对象公开的信息行；提供者决定内容和顺序，WBP 按详情层级筛选并生成列表载荷，不识别具体业务键。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FCatWorldInfoRow> Rows;
};
