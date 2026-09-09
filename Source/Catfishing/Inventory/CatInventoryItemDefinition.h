#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatInventoryItemDefinition.generated.h"

class UCatInventoryItemInstance;
class UTexture2D;

/** 库存物品 Use 成功后的库存处理策略；它描述实例仍由谁持有、是否扣数量，不描述具体装备、GAS 或 Fishing 效果。 */
UENUM(BlueprintType)
enum class ECatInventoryItemUseEffect : uint8
{
	/** 当前定义不声明通用库存 Use，具体子类也没有兼容推导时按无效果处理。 */
	Auto,
	/** 这类物品没有通用库存 Use；右键使用可以返回稳定终态，但不移动实例也不扣数量。 */
	None,
	/** Use 成功后整份不可堆叠实例离开可见背包，由库存活动区保管到停止使用再放回。 */
	HoldInstanceUntilUnUse,
	/** Use 成功后从同一槽位扣减指定数量；真实效果必须已由物品实例或下游领域先裁决成功。 */
	ConsumeQuantity
};

/** 物品定义上的可组合语义片段；定义负责静态配置，片段负责声明某类物品额外具备的库存语义。 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, BlueprintType)
class CATFISHING_API UCatInventoryItemFragment : public UObject
{
	GENERATED_BODY()

public:
	/** 库存实例创建后给片段一次补充运行状态的机会；默认实现不写实例，具体片段可以按自己的语义扩展。 */
	virtual void OnInstanceCreated(UCatInventoryItemInstance* Instance) const;
};

/** Aegis 风格的物品静态定义；在 Catfishing 中以 DataAsset 承载，便于商店、存档和策划表共享同一物品目录。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 构造一份静态物品定义；运行期实例会保存资产引用并只读取得这些不可变配置。 */
	UCatInventoryItemDefinition(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 按片段类型查找定义上的静态语义；库存、装备或玩法适配层用它读取自己关心的那一片配置。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	UCatInventoryItemFragment* FindFragmentByClass(TSubclassOf<UCatInventoryItemFragment> FragmentClass) const;

	/** 判断定义是否带有某个语义标签；精确匹配用于稳定身份，层级匹配用于玩法分类和 UI 汇总。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	bool HasSemanticTag(FGameplayTag Tag, bool bExactMatch = false) const;

	/** 库存目录稳定 ID；商店、存档和旧 Equipment 迁移都用它对齐同一种物品。 */
	virtual FName GetInventoryDefinitionId() const;

	/** 玩家可见名称；UI 和日志通过这层虚拟读取兼容库存资产与旧装备资产。 */
	virtual FText GetInventoryDisplayName() const;

	/** 玩家可见说明；详情面板通过这层虚拟读取，不参与容量或使用裁决。 */
	virtual FText GetInventoryDescription() const;

	/** 库存格缩略图；表现层通过这层虚拟读取，库存事实不保存贴图资源。 */
	virtual TSoftObjectPtr<UTexture2D> GetInventoryThumbnail() const;

	/** 物品语义标签集合；空集合表示当前定义不声明额外分类，不应被调用方猜测类型。 */
	virtual const FGameplayTagContainer& GetInventorySemanticTags() const;

	/** 库存运行就绪边界；收货、生成实例和使用前都读它，缺稳定 ID 或实例类型时统一拒绝而不是生成半有效物品。 */
	virtual bool IsInventoryRuntimeDefinitionReady() const;

	/** 解析这类物品默认生成的运行实例类型；没有显式配置时回到通用库存实例。 */
	virtual TSubclassOf<UCatInventoryItemInstance> GetPreferredInstanceType() const;

	/** 统一解析最终实例类；调用方传入覆盖类时优先使用覆盖类，否则读取定义资产自己的 PreferredInstanceType。 */
	static TSubclassOf<UCatInventoryItemInstance> ResolveItemInstanceClass(
		const UCatInventoryItemDefinition* ItemDefinition,
		TSubclassOf<UCatInventoryItemInstance> ItemInstanceOverrideClass = nullptr);

	/** 读取这类物品在单格内允许的最大数量；返回值始终至少为 1，调用方不用处理非法配置。 */
	virtual int32 GetMaxStackCount() const;

	/** 读取 Use 成功后的库存处理策略；基础库存定义默认无通用 Use，装备和后续道具定义可以覆盖。 */
	virtual ECatInventoryItemUseEffect GetInventoryUseEffect() const;

	/** 判断 Use 成功后是否应由库存活动区暂存整份实例；部署物和长期占用物用它离开可见背包但不丢身份。 */
	virtual bool KeepsInventoryInstanceWhileUsed() const;

	/** 消费类物品的统一清算口径；草药、窝料这类效果已成立的物品通过它交给库存组件扣减同一槽位数量。 */
	virtual bool ConsumesInventoryQuantityOnUse() const;

	/** 判断两份定义是否可以作为同一种堆叠物合并；稳定 ID 一致或同一资产对象才允许合并。 */
	virtual bool CanStackWith(const UCatInventoryItemDefinition& Other) const;

	/** 支持作为复制子对象被库存实例引用；物品定义本身仍应被当作静态配置读取。 */
	virtual bool IsSupportedForNetworking() const override;

public:
	/** 物品稳定 ID；普通库存资产直接写它，旧 Equipment 资产会通过覆盖方法返回自己的 EquipmentDefinitionId。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FName InventoryDefinitionId = NAME_None;

	/** 玩家可见名称；普通库存资产直接写它，旧 Equipment 资产会通过覆盖方法返回自己的 DisplayName。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FText InventoryDisplayName;

	/** 玩家可见说明；普通库存资产直接写它，不参与库存容量、收货或使用裁决。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation", meta = (MultiLine = "true"))
	FText InventoryDescription;

	/** 库存格缩略图；普通库存资产直接写它，格子事实只保存实例和数量。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> InventoryThumbnail;

	/** 物品语义标签；用于把装备、鱼饵、草药、任务物等分类交给消费者读取，不让库存核心硬编码业务枚举。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FGameplayTagContainer InventorySemanticTags;

	/** 单格最大堆叠数；1 表示不可堆叠，非法值会在读取时被压到 1。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "1"))
	int32 InventoryMaxStackCount = 1;

	/** 默认运行实例类型；鱼竿、消耗品或后续特殊物品可以用实例子类承载自己的运行语义。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<UCatInventoryItemInstance> PreferredInstanceType;

	/** 片段是定义上的可选语义扩展；库存核心只保存和查找它们，避免把装备、鱼饵或任务物规则写死在背包里。 */
	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "Inventory")
	TArray<TObjectPtr<UCatInventoryItemFragment>> Fragments;
};

/** 给蓝图读取物品定义片段的轻量工具；它不创建实例，也不改变任何库存状态。 */
UCLASS()
class CATFISHING_API UCatInventoryBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 从定义资产上查找指定片段；UI 和蓝图逻辑用它只读取得静态物品语义。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory", meta = (DeterminesOutputType = "FragmentClass"))
	static const UCatInventoryItemFragment* FindFragmentByClass(
		const UCatInventoryItemDefinition* ItemDefinition,
		TSubclassOf<UCatInventoryItemFragment> FragmentClass);
};
