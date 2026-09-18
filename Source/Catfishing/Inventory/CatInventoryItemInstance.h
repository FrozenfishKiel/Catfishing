#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "GameplayTagContainer.h"
#include "Inventory/CatInventoryUseTarget.h"
#include "UObject/Object.h"
#include "CatInventoryItemInstance.generated.h"

class APawn;
class AController;
class UCatInventoryComponent;
class UCatInventoryItemDefinition;
struct FCatInventoryEntry;

/** 服务器物品操作上下文；能力或库存操作入口冻结来源后传给领域行为，不由实例编排使用效果。 */
struct FCatInventoryItemUseContext
{
	/** 本次 Use 意图的稳定请求 ID；服务器和下游领域命令用它做幂等和 UI 回包关联。 */
	FGuid RequestId;

	/** 发起 Use 的玩家控制器；RPC 入口已经完成命令 gate，下游只把它作为身份或日志来源。 */
	AController* RequestingController = nullptr;

	/** 实际使用物品的 Pawn；装备、消耗品或未来工具效果通过它寻找自己的目标组件。 */
	APawn* UserPawn = nullptr;

	/** 持有被点击槽位的正式库存组件；实例如需扣量或重读当前条目，只能回到这份事实源。 */
	UCatInventoryComponent* SourceInventory = nullptr;

	/** 被使用物品所在的正式库存槽位；库存组件按它重读条目，实例不能信任 UI 传来的定义或类别。 */
	int32 InventorySlotIndex = INDEX_NONE;

	/** 本次 Use 是否由按住输入发起；物品据此决定 Begin 后等待 End，右键等一次性入口保持 false 并由物品立即收束。 */
	bool bContinuousInput = false;

	/** 原输入采样；无目标需求的物品忽略，目标型物品在服务器重新验证。 */
	FCatInventoryUseTarget Target;

	/** 异步领域操作的终态接收者；使用能力绑定弱引用回调并按请求身份结束，不经过库存效果回调。 */
	TFunction<void(const FCatDomainCommandResult&)> OnCompleted;
};

/** 运行期的一份物品身份；库存格保存数量，实例保存这件物品跨移动、使用和复制时不该丢的身份与行为入口。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryItemInstance : public UObject
{
	GENERATED_BODY()

public:
	/** 读取此实物剩余的使用资源；负一表示没有独立资源，不从物品名称推断水量或次数。 */
	int32 GetRemainingResource() const { return RemainingResource; }
	/** 服务器恢复或结算本实例资源；拒绝缺配置与越界数值，成功后由子对象复制给当前持有者。 */
	bool SetRemainingResourceFromAuthority(int32 Value);
	/** 查询定义声明的操作当前是否可用；客户端只读生成置灰原因，服务器执行前用同一规则复核。 */
	virtual bool CanExecuteInventoryAction(const FGameplayTag& Action, const FCatInventoryEntry& Entry,
		APawn* UserPawn, FText& OutReason) const;
	/** 构造一份空物品实例；定义资产会在正式入库前由库存组件写入。 */
	UCatInventoryItemInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 复制声明流程：同步定义资产和运行宿主，客户端据此还原展示和只读语义。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 实例需要支持网络引用；库存组件把它登记为子对象，避免客户端 FastArray 只拿到空指针。 */
	virtual bool IsSupportedForNetworking() const override;

	/** 定义资产一旦绑定就驱动片段初始化；这样实例状态从静态配置派生，避免显示名或标签反推身份。 */
	void SetItemDefinition(UCatInventoryItemDefinition* InDefinition);

	/** 读取这份实例的静态定义资产；存档、日志或领域系统需要稳定目录入口时使用它。 */
	UCatInventoryItemDefinition* GetItemDefinition() const;

	/** 读取这份实例的稳定定义 ID；日志、读模型和商店回执用它对齐同一种物品。 */
	int32  GetItemId() const;

	/** 读取这份运行实例自己的稳定 ID；堆叠格共享一个实例 ID，非堆叠物每件各自拥有一个。 */
	FGuid GetItemInstanceId() const;

	/** 正式库存 Use 事务调用的实例侧裁决；条目和实例身份必须一致，返回值决定是否继续扣量或借出。 */
	virtual ECatDomainCommandError Use(const FCatInventoryEntry& Item, int32 Quantity) const;

	/** 正式库存 UnUse 事务调用的实例侧裁决；成功只说明这份活动 entry 可以归还可见库存。 */
	virtual ECatDomainCommandError UnUse(const FCatInventoryEntry& Item) const;

	/** 这份实例 Use 成功后是否由库存活动区暂存整份对象；部署物和长期占用物用它离开可见背包但不丢身份。 */
	virtual bool KeepsInventoryInstanceWhileUsed() const;

	/** 这份实例 Use 成功后是否直接扣库存数量；数量耗材通过它让 InventoryComponent 执行同一槽位扣减。 */
	virtual bool ConsumesInventoryQuantityOnUse() const;

	/** authority 恢复这份实例的稳定 ID；存档恢复用它保留原物品身份，客户端不能伪造。 */
	void SetItemInstanceIdFromAuthority(FGuid InItemInstanceId);

	/** 运行宿主记录当前拥有者；跨库存移动会刷新它，避免实例行为继续认为自己属于原 Actor。 */
	virtual void SetRuntimeOwnerActor(AActor* InRuntimeOwnerActor);

	/** 库存落地通过此入口取得原载体，保留拾取前的 Actor 状态和尺寸；空引用才走定义生成路径。 */
	virtual AActor* GetWorldActor() const;

	/** 拾取和落地时关联这份实例的原世界物；不改变数量、实例身份或存档。 */
	void SetWorldActor(AActor* InWorldActor);

	/** 读取当前运行宿主；没有显式宿主时回退到 Outer Actor，方便刚创建的实例立即可用。 */
	AActor* GetRuntimeOwnerActor() const;

	/** 菜单只读查询使用可用性；基础实例检查使用配置与数量，正式访问权限和提交条件由能力再次确认。 */
	virtual bool CanUseFromInventory(const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const;



	/** 读取使用配置声明的消耗件数；零表示不扣数量或配置不可用，不能仅凭此值判断可用性。 */
	virtual int32 GetInventoryUseQuantity() const;




protected:
	/** 本实物剩余水量或次数；定义绑定初始化，能力成本扣除、补给恢复、保存读取，跨玩家转交不重置。 */
	UPROPERTY(Replicated)
	int32 RemainingResource = -1;
	/** 这份物品在当前世界中的原 Actor；拾取保存、落地复用，只有一个引用，不按堆叠数量保存多份。 */
	UPROPERTY(Transient)
	TObjectPtr<AActor> WorldActor = nullptr;

	/** 定义绑定后的实例状态扩展点；父类片段已完成初始化后调用它，子类只能补齐自己拥有的运行状态。 */
	virtual void HandleItemDefinitionAssigned();

	/** 这份运行物品的稳定实例 ID；服务器创建时写入，客户端和读模型只读取它。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Inventory")
	FGuid ItemInstanceId;

	/** 这份实例对应的静态定义资产；库存只保存物品身份和配置引用，不持有 GAS、Fishing 或 InputAbilities 的运行状态。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UCatInventoryItemDefinition> ItemDefinition;

	/** 当前运行宿主 Actor；服务器移动实例后刷新它，客户端只用它判断本地归属和调试来源。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<AActor> RuntimeOwnerActor = nullptr;
};
