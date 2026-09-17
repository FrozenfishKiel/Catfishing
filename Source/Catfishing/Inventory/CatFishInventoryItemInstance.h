#pragma once

#include "CoreMinimal.h"
#include "Data/CatFishDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "CatFishInventoryItemInstance.generated.h"

class UCatFishDefinition;

/** 实物鱼库存实例；每条鱼把重量、来源会话和捕获者身份带在同一个 ItemInstance 上，鱼护和鱼缸只负责保存格子。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatFishInventoryItemInstance : public UCatInventoryItemInstance
{
	GENERATED_BODY()

public:
	/** 鱼补充容器叼起与单鱼出售条件；普通操作继续复用基类，只读查询不会冻结买家或售价。 */
	virtual bool CanExecuteInventoryAction(const FGameplayTag& Action, const FCatInventoryEntry& Entry,
		APawn* UserPawn, FText& OutReason) const override;
	/** 鱼单独分发出售，其余操作回到基类虚函数链，避免通用库存认识买家。 */
	virtual FCatDomainCommandResult ExecuteInventoryActionFromAuthority(const FGameplayTag& Action,
		const FCatInventoryEntry& Entry, const FCatInventoryItemUseContext& Context, int32 Quantity) override;
	/** 沿用现有鱼容器到嘴部事务，保留鱼身份、原载体和失败回滚。 */
	virtual FCatDomainCommandResult CarryFromInventoryFromAuthority(const FCatInventoryEntry& Entry,
		const FCatInventoryItemUseContext& Context) override;
	/** 重新寻找当前可服务买家，再调用已有单鱼交易，不接受客户端价格。 */
	virtual FCatDomainCommandResult SellFromInventoryFromAuthority(const FCatInventoryEntry& Entry,
		const FCatInventoryItemUseContext& Context);
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

	/** 读取捕获者稳定身份；服务器售鱼与图鉴登记可以用它，不复制给客户端；拿鱼不读它，机制层不问归属。 */
	const FString& GetFishOwnerStableNetId() const;

	/** 读取冻结真实重量，单位千克；商店估价和 UI 展示必须从同一实例取得。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fish")
	double GetFishWeightKilograms() const;

	/**
	 * 这条鱼投出去会不会产生对猫的后果（咸鱼击退炸毛、臭臭鱼驱散并短时屏蔽靠近）。
	 * 它只回答「这条鱼的投掷效果数据齐不齐」，不回答「现在能不能投」——投掷动作本身、距离与命中判定
	 * 归投掷规则那一侧（道具／联机册），本函数是鱼实例上的数据入口。
	 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fish")
	bool HasThrowEffect() const;

	/** 读取这条鱼的投掷效果数据；鱼定义缺失或未配置时返回 Kind=None 的空效果，不返回伪造值。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fish")
	FCatFishThrowEffect GetThrowEffect() const;

	/** 库存数量操作的只读裁决；要求一格一条、数量为一且捕获身份和重量完整，不施加进食效果。 */
	virtual ECatDomainCommandError Use(const FCatInventoryEntry& Item, int32 Quantity) const override;

	/** 鱼使用后由库存扣掉这一条；它不会进入 held entry 或部署到世界。 */
	virtual bool ConsumesInventoryQuantityOnUse() const override;

	/** 菜单只读检查鱼的可食用性、实例事实与动作配置；服务器额外检查成长条件，正式提交仍由进食能力负责。 */
	virtual bool CanUseFromInventory(const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const override;


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
