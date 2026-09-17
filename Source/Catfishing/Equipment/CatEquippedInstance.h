#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Config/CatAbilitySet.h"
#include "CatEquippedInstance.generated.h"

class UCatEquippedDefinition;
class UCatInventoryItemInstance;

/** 一次装备生效期间的运行对象；关联原物品并持有来源授予，耐久等持久事实仍写回物品实例。 */
UCLASS(BlueprintType)
class CATFISHING_API UCatEquippedInstance : public UObject
{
	GENERATED_BODY()
public:
	/** 装备对象由宿主登记为复制子对象，GAS 的 SourceObject 可据此在客户端解析。 */
	virtual bool IsSupportedForNetworking() const override { return true; }
	/** 只复制来源和定义；能力句柄的增删由各自 ASC 复制，避免第二份能力状态。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 权威建立一次装备授予；任一集合失败时撤销已授予部分，禁止复用仍在生效的实例。 */
	bool Equip(UCatAbilitySystemComponent* AbilitySystem, UCatInventoryItemInstance* Item, const UCatEquippedDefinition* Definition);
	/** 从最初的 ASC 回收本装备全部授予；调用方再撤销复制登记并释放实例，重复回收没有副作用。 */
	void Unequip();
	/** 装备绑定的原始物品；能力需要身份或耐久时读取它，不改用当前快捷栏。 */
	UCatInventoryItemInstance* GetSourceItem() const { return SourceItem; }
	/** 最初接受授予的能力系统；宿主用于识别换 Pawn，卸下前不改绑到新身体。 */
	UCatAbilitySystemComponent* GetAbilitySystem() const { return GrantedAbilitySystem.Get(); }
private:
	/** 原物品实例引用；服务器装备时写入，随宿主的子对象复制供客户端解析来源，引用可用性仍取决于原物品自身的复制。 */
	UPROPERTY(Replicated) TObjectPtr<UCatInventoryItemInstance> SourceItem;
	/** 本次装备所用的配置资产引用；Equip 写入并复制给客户端，卸下不清空，资产内容并未在此复制成独立快照。 */
	UPROPERTY(Replicated) TObjectPtr<const UCatEquippedDefinition> EquipmentDefinition;
	/** 最初接受授予的 ASC；换 Pawn 后回收仍指向旧身体，不能误删新身体能力。 */
	TWeakObjectPtr<UCatAbilitySystemComponent> GrantedAbilitySystem;
	/** 这次装备独占的能力和效果句柄；装备写入，卸下时移出并回收。 */
	UPROPERTY(Transient) FCatGrantedAbilitySetHandles GrantedHandles;
};
