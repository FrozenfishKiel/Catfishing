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

/** 库存物品 Use 的服务器上下文；服务器命令链传递这份事实，具体物品实例只读取它来决定自己的领域效果和回包。 */
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

	/** 仅服务器异步执行使用；领域完成后由库存缓存终态再通知请求方，禁止裸 UObject 捕获。 */
	TFunction<void(const FCatDomainCommandResult&)> OnCompleted;
};

/** 运行期的一份物品身份；库存格保存数量，实例保存这件物品跨移动、使用和复制时不该丢的身份与行为入口。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryItemInstance : public UObject
{
	GENERATED_BODY()

public:
	/** 本地采样入口；默认物品无需目标，各领域实例负责自己的目标解析。 */
	virtual FCatInventoryUseTarget CaptureUseTarget(APlayerController* Controller) const { return {}; }
	/** 查询定义声明的操作当前是否可用；客户端只读生成置灰原因，服务器执行前用同一规则复核。 */
	virtual bool CanExecuteInventoryAction(const FGameplayTag& Action, const FCatInventoryEntry& Entry,
		APawn* UserPawn, FText& OutReason) const;
	/** 已解析的服务器库存请求进入此虚函数；基类分发通用动作，特殊实例可扩展标识而无需修改菜单或RPC。 */
	virtual FCatDomainCommandResult ExecuteInventoryActionFromAuthority(const FGameplayTag& Action,
		const FCatInventoryEntry& Entry, const FCatInventoryItemUseContext& Context, int32 Quantity);
	/** 通用丢弃实现；复用库存世界事务，子类只有丢弃语义不同才需重写。 */
	virtual FCatDomainCommandResult DropFromInventoryFromAuthority(const FCatInventoryEntry& Entry,
		const FCatInventoryItemUseContext& Context, int32 Quantity);
	/** 通用放置实现；继续使用已有地面求解和库存提交，避免每种物品复制离库代码。 */
	virtual FCatDomainCommandResult PlaceFromInventoryFromAuthority(const FCatInventoryEntry& Entry,
		const FCatInventoryItemUseContext& Context, int32 Quantity);
	/** 叼起扩展点；基础物品不支持，由具备相应语义的实例重写。 */
	virtual FCatDomainCommandResult CarryFromInventoryFromAuthority(const FCatInventoryEntry& Entry,
		const FCatInventoryItemUseContext& Context);
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
	FName GetItemDefinitionId() const;

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

	/** Use 预检只读地声明实例是否可进入正式提交；基础实例要求有效消耗片段，未配置的物品明确不可用。 */
	virtual bool CanUseFromInventory(const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const;

	/** 正式库存 Use 的唯一实例扩展面；服务器命令入口用它传递 RequestId、槽位上下文和错误码，具体物品效果只通过结构化回包提交。 */
	virtual FCatDomainCommandResult UseFromInventorySlotFromAuthority(
		const FCatInventoryEntry& InventoryEntry, const FCatInventoryItemUseContext& UseContext);

	/** 说明本实例的 Use 是否必须等待同一次输入结束；Controller 据此固定实例和请求 ID，避免松开时改用新选中物品。 */
	virtual bool UsesContinuousInput() const;

	/** 报告本次使用成功的真实扣量要求；基础实例读取消耗片段，未配置返回零。 */
	virtual int32 GetInventoryUseQuantity() const;
	/** 仅提交本实例效果；由通用库存事务在暂扣后调用，失败由事务还原数量。 */
	virtual FCatDomainCommandResult ApplyUseEffectsFromAuthority(const FCatInventoryItemUseContext& UseContext);

	/** 本地连续 Use 的表现边沿；Controller 对 Begin、Release、取消和拒绝都通知同一实例，基类不保存状态也不产生玩法效果。 */
	virtual void SetUseInputActiveLocally(APlayerController* RequestingController, bool bActive);

	/** 同一次持续 Use 的结束或取消入口；调用方只能传回 Begin 已固定的上下文，基类明确拒绝没有持续语义的物品。 */
	virtual FCatDomainCommandResult EndUseFromInventorySlotFromAuthority(
		const FCatInventoryItemUseContext& UseContext, bool bCancelled);

protected:
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
