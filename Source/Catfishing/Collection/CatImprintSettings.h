#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatImprintSettings.generated.h"

/**
 * 印记册自己维护的触发准入与单局频控边界。
 * 飞书印记图鉴册把「触发总清单」的所有权放在本册（各来源册往清单里加条目，本册有权砍），
 * 所以清单在工程侧就落成一份显式配置，而不是散在各来源册里各自决定。
 * 两个字段都默认 fail-closed：空清单表示总清单还没被正式登记，未配置的上限表示数值阶段还没校准。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Imprint"))
class CATFISHING_API UCatImprintSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 判断一个事件名是否在总清单内；空名字和空清单一律拒绝，不存在“未配置即放行”的分支。 */
	bool IsImprintEventAllowed(FName EventId) const;

	/**
	 * 允许进入印记链路的触发事件总清单。
	 * 条目是各来源册（环境、装备、社交、营地、钓鱼等）抛出的稳定事件名，由本册统一收口。
	 * 空数组表示总清单尚未登记，此时任何候选都进不来——这是「宁缺毋滥」的默认态，不是配置遗漏后的静默放行。
	 * 读它的唯一地方是 UCatRunImprintService 的候选准入。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Admission")
	TArray<FName> AllowedImprintEventIds;

	/**
	 * 单局允许成立的印记候选条数上限，即飞书 §6 锚定项「单局印记条数上限」。
	 * 它约束的是「本局产生了几个成像瞬间」，不是「每个玩家收到几张图」——同一个候选会给全部参与者各发一份计划，
	 * 但只算一条。这样调它才对应飞书要防的「每条鱼都咔嚓一下」，而不是随人数漂移。
	 * 默认 0 表示数值阶段还没给出校准值，此时新候选一条都不放行；由 UCatRunImprintService 在受理候选时读取。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Admission", meta = (ClampMin = "0"))
	int32 MaxRunImprintCandidates = 0;
};
