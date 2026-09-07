#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "UObject/Object.h"
#include "CatInventoryItemInstance.generated.h"

class APawn;
class AController;
class UCatInventoryComponent;
class UCatInventoryItemDefinition;
struct FCatInventoryEntry;

/** 库存物品 Use 的服务器上下文；服务器命令链传递这份事实，具体物品实例只读取它来决定自己的领域效果和回包。 */
struct FCatInventoryItemUseContext
{
	/** 本次 Use 意图的稳定请求 ID；服务器和下游领域命令用它做幂等和 UI 回包关联。 */
	FGuid RequestId;

	/** 发起 Use 的玩家控制器；领域 gate 已在协调器检查，下游只把它作为身份或日志来源。 */
	AController* RequestingController = nullptr;

	/** 实际使用物品的 Pawn；装备、消耗品或未来工具效果通过它寻找自己的目标组件。 */
	APawn* UserPawn = nullptr;

	/** 持有被点击槽位的正式库存组件；实例如需扣量或查询当前版本，只能回到这份事实源。 */
	UCatInventoryComponent* SourceInventory = nullptr;

	/** UI 观察到的正式库存版本；库存组件先用它阻止陈旧 Use 覆盖较新的背包事实。 */
	int64 ExpectedInventoryRevision = 0;

	/** 被使用物品所在的正式库存槽位；库存组件按它重读条目，实例不能信任 UI 传来的定义或类别。 */
	int32 InventorySlotIndex = INDEX_NONE;

	/** 是否要求下游 Equipment 版本也必须匹配；只给旧钓具选择 RPC 保留，新通用 Use 不暴露这个前提。 */
	bool bRequireEquipmentRevision = false;

	/** 旧钓具选择 RPC 观察到的 Equipment 版本；只有 bRequireEquipmentRevision 为 true 时才参与校验。 */
	int64 ExpectedEquipmentRevision = 0;
};

/** 运行期的一份物品身份；库存格保存数量，实例保存这件物品跨移动、使用和复制时不该丢的身份与行为入口。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryItemInstance : public UObject
{
	GENERATED_BODY()

public:
	/** 构造一份空物品实例；定义资产会在正式入库前由库存组件写入。 */
	UCatInventoryItemInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 复制声明流程：同步定义资产和运行宿主，客户端据此还原展示和只读语义。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 实例需要支持网络引用；库存组件把它登记为子对象，避免客户端 FastArray 只拿到空指针。 */
	virtual bool IsSupportedForNetworking() const override;

	/** 定义资产一旦绑定就驱动片段初始化；这样实例状态从静态配置派生，避免显示名或标签反推身份。 */
	void SetItemDefinition(UCatInventoryItemDefinition* InDefinition);

	/** 读取这份实例的静态定义资产；存档、日志或适配层需要稳定目录入口时使用它。 */
	UCatInventoryItemDefinition* GetItemDefinition() const;

	/** 读取这份实例的稳定定义 ID；日志、旧快照投影和商店回执用它对齐同一种物品。 */
	FName GetItemDefinitionId() const;

	/** 读取这份运行实例自己的稳定 ID；堆叠格共享一个实例 ID，非堆叠物每件各自拥有一个。 */
	FGuid GetItemInstanceId() const;

	/** authority 恢复这份实例的稳定 ID；存档和旧快照迁移用它保留原物品身份，客户端不能伪造。 */
	void SetItemInstanceIdFromAuthority(FGuid InItemInstanceId);

	/** 运行宿主记录当前拥有者；跨库存移动会刷新它，避免实例行为继续认为自己属于旧 Actor。 */
	void SetRuntimeOwnerActor(AActor* InRuntimeOwnerActor);

	/** 读取当前运行宿主；没有显式宿主时回退到 Outer Actor，方便刚创建的实例立即可用。 */
	AActor* GetRuntimeOwnerActor() const;

	/** Use 能力由具体实例子类声明；通用实例默认拒绝，避免任意物品被库存层扣掉。 */
	virtual bool CanUseFromInventory(const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const;

	/** 执行这份物品的库存使用裁决；成功必须代表实例已经完成真实效果裁决，OutConsumeCount 可以为 0 表示不扣库存数量。 */
	virtual bool TryUseFromInventory(FCatInventoryEntry& InventoryEntry, APawn* UserPawn, int32& OutConsumeCount);

	/** 执行结构化库存 Use；服务器命令入口用它拿到 RequestId、Revision 和错误码，避免把装备、草药或窝料规则写回协调器。 */
	virtual FCatDomainCommandResult UseFromInventorySlotFromAuthority(
		const FCatInventoryEntry& InventoryEntry, const FCatInventoryItemUseContext& UseContext);

protected:
	/** 定义绑定后的实例状态扩展点；父类片段已完成初始化后调用它，子类只能补齐自己拥有的运行状态。 */
	virtual void HandleItemDefinitionAssigned();

	/** 这份运行物品的稳定实例 ID；服务器创建时写入，客户端和旧快照投影只读取它。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Inventory")
	FGuid ItemInstanceId;

	/** 这份实例对应的静态定义资产；库存只保存物品身份和配置引用，不持有 GAS、Fishing 或 InputAbilities 的运行状态。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UCatInventoryItemDefinition> ItemDefinition;

	/** 当前运行宿主 Actor；服务器移动实例后刷新它，客户端只用它判断本地归属和调试来源。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<AActor> RuntimeOwnerActor = nullptr;
};
