#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Object.h"
#include "CatInventoryItemDefinition.generated.h"

class UCatInventoryItemInstance;
class UTexture2D;

/** 物品定义上的可组合语义片段；定义负责静态配置，片段负责声明某类物品额外具备的库存语义。 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, BlueprintType)
class CATFISHING_API UCatInventoryItemFragment : public UObject
{
	GENERATED_BODY()

public:
	/** 库存实例创建后给片段一次补充运行状态的机会；默认实现不写实例，具体片段可以按自己的语义扩展。 */
	virtual void OnInstanceCreated(UCatInventoryItemInstance* Instance) const;
};

/** Aegis 风格的物品静态定义；它只描述这是什么物品、如何展示、能否堆叠以及默认生成哪类运行实例。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryItemDefinition : public UObject
{
	GENERATED_BODY()

public:
	/** 构造一份类默认物品定义；运行期实例会通过定义类读取这些不可变配置。 */
	UCatInventoryItemDefinition(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 按片段类型查找定义上的静态语义；库存、装备或玩法适配层用它读取自己关心的那一片配置。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	UCatInventoryItemFragment* FindFragmentByClass(TSubclassOf<UCatInventoryItemFragment> FragmentClass) const;

	/** 判断定义是否带有某个语义标签；精确匹配用于稳定身份，层级匹配用于玩法分类和 UI 汇总。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	bool HasSemanticTag(FGameplayTag Tag, bool bExactMatch = false) const;

	/** 解析这类物品默认生成的运行实例类型；没有显式配置时回到通用库存实例。 */
	TSubclassOf<UCatInventoryItemInstance> GetPreferredInstanceType() const;

	/** 统一解析最终实例类；调用方传入覆盖类时优先使用覆盖类，否则读取定义自己的 PreferredInstanceType。 */
	static TSubclassOf<UCatInventoryItemInstance> ResolveItemInstanceClass(
		TSubclassOf<UCatInventoryItemDefinition> ItemDefinitionClass,
		TSubclassOf<UCatInventoryItemInstance> ItemInstanceOverrideClass = nullptr);

	/** 读取这类物品在单格内允许的最大数量；返回值始终至少为 1，调用方不用处理非法配置。 */
	int32 GetMaxStackCount() const;

	/** 判断两份定义是否可以作为同一种堆叠物合并；迁移期优先承认稳定 ID，其次承认相同定义类。 */
	bool CanStackWith(const UCatInventoryItemDefinition& Other) const;

	/** 支持作为复制子对象被库存实例引用；物品定义本身仍应被当作静态配置读取。 */
	virtual bool IsSupportedForNetworking() const override;

public:
	/** 物品稳定 ID；后续从旧 EquipmentDefinition 迁移时用它和存档、配置、日志里的名称对齐。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FName ItemDefinitionId = NAME_None;

	/** 玩家可见名称；UI 和日志展示优先读它，稳定判断仍使用定义类或 ItemDefinitionId。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FText DisplayName;

	/** 玩家可见说明；它只服务表现和详情面板，不参与库存容量、收货或使用裁决。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation", meta = (MultiLine = "true"))
	FText Description;

	/** 库存格缩略图；格子事实只保存实例和数量，表现资源留在静态定义上。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	/** 物品语义标签；用于把装备、鱼饵、草药、任务物等分类交给消费者读取，不让库存核心硬编码业务枚举。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FGameplayTagContainer SemanticTags;

	/** 单格最大堆叠数；1 表示不可堆叠，非法值会在读取时被压到 1。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "1"))
	int32 MaxStackCount = 1;

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
	/** 从定义类默认对象上查找指定片段；UI 和蓝图逻辑用它只读取得静态物品语义。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory", meta = (DeterminesOutputType = "FragmentClass"))
	static const UCatInventoryItemFragment* FindFragmentByClass(
		TSubclassOf<UCatInventoryItemDefinition> ItemDefinitionClass,
		TSubclassOf<UCatInventoryItemFragment> FragmentClass);
};
