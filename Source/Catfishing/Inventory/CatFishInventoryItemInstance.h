#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "CatFishInventoryItemInstance.generated.h"

class UCatFishDefinition;

/** 实物鱼库存实例；每条鱼把重量、来源会话和捕获者身份带在同一个 ItemInstance 上，鱼护和鱼缸只负责保存格子。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatFishInventoryItemInstance : public UCatInventoryItemInstance
{
	GENERATED_BODY()

public:
	/** 构造一条空鱼实例；捕获提交成功前不会写入鱼身份和重量。 */
	UCatFishInventoryItemInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 复制声明流程：客户端需要鱼来源会话和重量做展示，捕获者身份只留在服务器不复制。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** authority 从捕获结果初始化鱼实例；调用方已经确认目标库存可以接收，函数只写本实例拥有的鱼运行状态。 */
	bool InitializeFishFromAuthority(FGuid InFishingSessionId, FGuid InFishInstanceId,
		const FString& InOwnerStableNetId, double InWeightKilograms);

	/** 读取鱼定义资产；不是鱼定义时返回空，调用方按正式库存无效载荷处理。 */
	UCatFishDefinition* GetFishDefinition() const;

	/** 读取来源钓鱼会话；图鉴归档、保存和调试用它串联这条实物鱼的来源。 */
	FGuid GetSourceFishingSessionId() const;

	/** 读取捕获者稳定身份；服务器售鱼、偷取或后续权限链可以用它，不复制给客户端。 */
	const FString& GetFishOwnerStableNetId() const;

	/** 读取冻结真实重量，单位千克；商店估价和 UI 展示必须从同一实例取得。 */
	double GetFishWeightKilograms() const;

	/** 鱼 Use 裁决负责确认一格一条鱼且鱼专属字段完整；身体效果在 UseFromInventorySlotFromAuthority 中提交。 */
	virtual ECatDomainCommandError Use(const FCatInventoryEntry& Item, int32 Quantity) const override;

	/** 鱼使用后由库存扣掉这一条；它不会进入 held entry 或部署到世界。 */
	virtual bool ConsumesInventoryQuantityOnUse() const override;

	/** 只读检查鱼能否被当前角色吃掉；库存 mutation 之前用它询问 Condition。 */
	virtual bool CanUseFromInventory(const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const override;

	/** authority 从正式库存槽吃鱼；库存先扣掉真实鱼实例，再由 Condition 提交身体效果，失败时库存负责回滚。 */
	virtual FCatDomainCommandResult UseFromInventorySlotFromAuthority(
		const FCatInventoryEntry& InventoryEntry, const FCatInventoryItemUseContext& UseContext) override;

protected:
	/** 来源 FishingSession ID；服务器捕获时写入，客户端展示和后续保存读取，不作为当前会话恢复入口。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Catfishing|Fish")
	FGuid SourceFishingSessionId;

	/** 冻结真实重量，单位千克；鱼定义只给范围，单条鱼实例保存本次抽中的最终值。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Catfishing|Fish")
	double WeightKilograms = 0.0;

	/** 捕获者稳定身份；这是服务器权限和持久化字段，不复制给客户端 UI。 */
	UPROPERTY(Transient)
	FString OwnerStableNetId;
};
