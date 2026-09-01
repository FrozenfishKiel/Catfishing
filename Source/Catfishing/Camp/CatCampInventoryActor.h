#pragma once

#include "CoreMinimal.h"
#include "Equipment/CatEquipmentTypes.h"
#include "GameFramework/Actor.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Interaction/CatInteractable.h"
#include "CatCampInventoryActor.generated.h"

class UCatEquipmentComponent;
class UCatEquipmentDefinition;
class UCatCampInventoryWidget;
class USceneComponent;
class USphereComponent;

/** 营地公共仓库的复制读模型；它只保存公共库存格和版本，不包含玩家当前钓鱼选择。 */
USTRUCT(BlueprintType)
struct FCatCampInventorySnapshot
{
	GENERATED_BODY()

	/** 公共仓库版本；商店发货、玩家取用或整理公共格子后递增，调用方用它做并发前提。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 公共仓库格子数组；每个格子沿用运行期物品实例，堆叠语义由装备定义和仓库配置共同决定。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatRunInventorySlot> InventorySlots;
};

/** 服务器权威来源请求放入公共仓库的一行物品；它只描述定义和数量，不指定目标格子。 */
USTRUCT(BlueprintType)
struct FCatCampInventoryAddItemRequest
{
	GENERATED_BODY()

	/** 要进入营地公共仓库的装备或耗材定义；实际堆叠规则由装备定义和仓库容量裁决。 */
	UPROPERTY(BlueprintReadWrite)
	FName DefinitionId = NAME_None;

	/** 本行要放入公共仓库的数量；装备型通常是 1，数量型可以大于 1。 */
	UPROPERTY(BlueprintReadWrite, meta = (ClampMin = "1"))
	int32 Quantity = 1;
};

/** 营地公共仓库快照变化通知；UI 或营地交互层收到后只能重读快照，不能直接写库存。 */
DECLARE_MULTICAST_DELEGATE(FCatCampInventorySnapshotChanged);

/** 营地公共仓库 Actor；商店购买物先进入这里，玩家再从公共仓库取到自己的随身库存。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API ACatCampInventoryActor : public AActor, public ICatInteractable
{
	GENERATED_BODY()

public:
	/** 公共仓库 Actor 的初始运行姿态：它是可复制、无 Tick 的关卡对象，并用默认容量作为未配置时的安全基线。 */
	ACatCampInventoryActor();

	/** 注册公共仓库快照复制；客户端只读 Slots，不在本地提交入库或取用。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 判断请求 Controller 是否能把本公共仓库作为交互目标；准星提示和本地打开都使用这条同一边界。 */
	virtual bool CanInteract_Implementation(AController* RequestingController) const override;

	/** 返回公共仓库交互提示；禁用交互时返回空文本，让提示层和执行层保持一致。 */
	virtual FText GetInteractionPrompt_Implementation() const override;

	/** 返回公共仓库允许交互的距离，单位厘米；服务器取用复核会复用这份距离声明。 */
	virtual double GetInteractionRadius_Implementation() const override;

	/** 执行公共仓库交互；本地玩家打开营地仓库自己的 WBP，库存写入仍只能由后续服务器请求完成。 */
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) override;

	/** 读取当前公共仓库快照；返回 const 引用防止外部绕过 Actor 写口修改格子。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|CampInventory")
	const FCatCampInventorySnapshot& GetSnapshot() const;

	/** 读取公共仓库对 UI 暴露的格子容量；空仓库也靠它显示稳定空格，不把空数组误认为没有仓库。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|CampInventory")
	int32 GetInventorySlotCapacityForView() const;

	/** 查询本仓库能否在指定版本接收一批物品；商店协调器用它在扣款前确认不会出现钱扣了但仓库塞不下。 */
	ECatDomainCommandError ValidateAddItemFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId, int32 Quantity) const;

	/** 从商店、奖励或其他服务器权威来源把物品放入公共仓库；成功后递增仓库版本并复制。 */
	FCatDomainCommandResult AddItemFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId, int32 Quantity);

	/** 查询本仓库能否在指定版本整批接收多种物品；服务器身份参与幂等作用域，避免不同玩家同 RequestId 串发货。 */
	ECatDomainCommandError ValidateAddItemsFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		const FString& StableNetId, const TArray<FCatCampInventoryAddItemRequest>& Items) const;

	/** 从商店购物车等服务器权威来源整批放入公共仓库；成功按服务器身份缓存终态，失败不写格子也不封死重试。 */
	FCatDomainCommandResult AddItemsFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		const FString& StableNetId, const TArray<FCatCampInventoryAddItemRequest>& Items);

	/** 查询玩家是否能从指定公共格子取物到自己的随身库存；正在 Use 或场上部署中的同一实例会被拒绝，本函数不修改两边库存。 */
	ECatDomainCommandError ValidateWithdrawToEquipment(FGuid RequestId, int32 SourceSlotIndex, int32 Quantity,
		UCatEquipmentComponent* TargetEquipment) const;

	/** 把公共仓库里的物品取到玩家随身库存；成功后公共仓库扣减，玩家随身库存通过自己的授予入口增加。 */
	FCatDomainCommandResult WithdrawToEquipmentFromAuthority(FGuid RequestId, int64 ExpectedCampRevision,
		int32 SourceSlotIndex, int32 Quantity, UCatEquipmentComponent* TargetEquipment,
		int64 ExpectedEquipmentRevision);

	/** 整理公共仓库内部两个格子；若源/目标格含 Use 或场上部署实例则拒绝，不把物品发到玩家随身库存。 */
	FCatDomainCommandResult MoveInventorySlotFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		int32 SourceSlotIndex, int32 TargetSlotIndex);

	/** 把玩家随身库存的一个格子拖入本公共仓库指定格；正在 Use 或场上部署中的同一实例不会借交换进入任一侧普通库存。 */
	FCatDomainCommandResult DepositFromEquipmentSlotFromAuthority(FGuid RequestId, int64 ExpectedCampRevision,
		int32 TargetCampSlotIndex, UCatEquipmentComponent* SourceEquipment, int64 ExpectedEquipmentRevision,
		int32 SourceEquipmentSlotIndex);

	/** 把本公共仓库的一个格子拖到玩家随身库存指定格；正在 Use 或场上部署中的同一实例不能被拖放或交换回背包。 */
	FCatDomainCommandResult WithdrawToEquipmentSlotFromAuthority(FGuid RequestId, int64 ExpectedCampRevision,
		int32 SourceCampSlotIndex, UCatEquipmentComponent* TargetEquipment, int64 ExpectedEquipmentRevision,
		int32 TargetEquipmentSlotIndex);

	/** 公共仓库快照变化通知；服务器提交和客户端复制回调都会广播。 */
	FCatCampInventorySnapshotChanged OnSnapshotChanged;

protected:
	/** 进入 World 后按项目交互设置对齐准星 Trace 通道；库存内容本身不在 BeginPlay 中写入。 */
	virtual void BeginPlay() override;

private:
	/** 客户端收到公共仓库复制快照后只广播读模型变化；不会自动把物品同步到玩家随身库存。 */
	UFUNCTION()
	void OnRep_Snapshot();

	/** 读取本公共仓库的格子容量；配置非法时返回 0，让入库 fail-closed。 */
	int32 GetConfiguredSlotCapacity() const;

	/** 读取定义在公共仓库单格中的最大堆叠数；未显式声明时复用项目数量型默认堆叠上限。 */
	int32 GetInventoryStackLimit(const UCatEquipmentDefinition& Definition) const;

	/** 只读判断指定数量能否完整放进公共仓库；不会为了预检扩容或写入空格。 */
	bool CanStoreItem(const UCatEquipmentDefinition& Definition, FName DefinitionId, int32 Quantity) const;

	/** 只读判断多行物品能否按当前仓库状态整批放完；模拟成功才允许购物车扣款。 */
	bool CanStoreItems(const TArray<FCatCampInventoryAddItemRequest>& Items) const;

	/** 解析公共仓库交互要打开的独立库存页类；路径失效时返回空，让交互明确失败而不是退回默认库存页。 */
	TSubclassOf<UCatCampInventoryWidget> LoadInventoryViewClass() const;

	/** 判断运行期实例是否已被 Use 或场上物品 Actor 占用；仓库取放和整理用它阻止跨玩家同 ID 回流。 */
	bool IsItemInstanceBlockedByActiveUse(const UCatEquipmentComponent* LocalEquipment, FGuid ItemInstanceId) const;

	/** 补齐公共仓库可见格子；只追加空格，不截断已有物品，避免容量调小吞掉库存。 */
	void EnsureInventorySlotArray();

	/** 向公共仓库格子写入一批物品；调用前必须已通过 CanStoreItem，成功后只改变 Snapshot.InventorySlots。 */
	bool AddItemQuantity(const UCatEquipmentDefinition& Definition, FName DefinitionId, int32 Quantity);

	/** 向指定格子数组模拟或写入一批物品；调用方决定传入临时数组还是正式 Snapshot。 */
	bool AddItemQuantityToSlots(TArray<FCatRunInventorySlot>& InventorySlots,
		const UCatEquipmentDefinition& Definition, FName DefinitionId, int32 Quantity) const;

	/** 整理整批入库请求并生成幂等载荷签名；重复 DefinitionId 会合并，行顺序不会影响同一请求的重放。 */
	bool BuildAddItemsPayloadSignature(const TArray<FCatCampInventoryAddItemRequest>& Items,
		FString& OutPayloadSignature, TArray<FCatCampInventoryAddItemRequest>& OutNormalizedItems) const;

	/** 把本次提交发布给复制和本机 UI；所有读者都从完整 Snapshot 重建表现。 */
	void PublishSnapshot();

	/** 组合操作名和 RequestId 的本 Actor 幂等键；缓存只活在当前仓库 Actor 生命周期内。 */
	static FString MakeTerminalKey(const TCHAR* Operation, FGuid RequestId);

	/** 组合操作名、服务器身份和 RequestId；跨玩家可共享同一个公共仓库，但批量发货幂等必须按玩家订单隔离。 */
	static FString MakeTerminalKey(const TCHAR* Operation, const FString& StableNetId, FGuid RequestId);

	/** 公共仓库在关卡中的空间根；它只承载交互或摆放位置，不保存任何库存规则。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|CampInventory",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** 公共仓库的准星命中入口；没有网格碰撞的箱子蓝图也能被交互扫描命中。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Interaction",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> InteractionCollision;

	/** 公共仓库当前是否允许玩家交互；编辑器或蓝图可关闭它，提示和打开请求都会一起停用。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction",
		meta = (AllowPrivateAccess = "true"))
	bool bInteractionEnabled = true;

	/** 公共仓库可被确认交互的最大距离，单位厘米；服务器直接取用公共格时用它复核玩家是否仍在箱子旁。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm"))
	double InteractionRadiusCentimeters = 300.0;

	/** 玩家准星命中公共仓库时看到的提示文本；设计可在蓝图里改名，但不影响仓库数据和购买发货。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction",
		meta = (AllowPrivateAccess = "true"))
	FText InteractionPrompt;

	/**
	 * 营地公共仓库交互时要打开的独立 WBP 类，表示这个团队共享箱子自己的页面形态，不复用默认库存页；蓝图或配置写入它，交互时读取它。
	 * 值无效会让本次打开明确失败，而不是退回默认库存页或改动公共仓库真实物品。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|UI",
		meta = (AllowPrivateAccess = "true"))
	TSoftClassPtr<UCatCampInventoryWidget> InventoryViewClass;

	/** 公共仓库最大格子数；它独立于玩家随身格子，默认更大以承载团队购买物。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|CampInventory",
		meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 InventorySlotCapacity = 48;

	/** 营地公共仓库的唯一复制库存事实；服务器写入，客户端只读。 */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FCatCampInventorySnapshot Snapshot;

	/** 公共仓库命令首次终态缓存；同 RequestId 重放不重复入库或取物。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;

	/** 公共仓库命令载荷签名；同 RequestId 换物品、格子、数量或版本会被拒绝。 */
	TMap<FString, FString> TerminalPayloadByKey;
};
