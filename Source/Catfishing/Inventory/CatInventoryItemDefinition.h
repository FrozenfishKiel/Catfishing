#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "CatInventoryItemDefinition.generated.h"

class UCatInventoryItemInstance;
class UTexture2D;
class AActor;

/** 物品定义上的可组合语义片段；定义负责静态配置，片段只声明这份定义额外具备的库存语义。 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, BlueprintType)
class CATFISHING_API UCatInventoryItemFragment : public UObject
{
	GENERATED_BODY()

public:
	/** 验证本片段的静态配置；默认片段没有额外约束，领域片段覆盖后可阻止无效资产进入库存。 */
	virtual bool IsRuntimeReady() const;

	/** 库存实例创建后给片段一次补充运行状态的机会；默认实现不写实例，具体片段可以按自己的语义扩展。 */
	virtual void OnInstanceCreated(UCatInventoryItemInstance* Instance) const;
};

/** Catfishing 的物品静态定义；以 DataAsset 承载，便于商店、存档和策划表共享同一物品目录。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 构造一份静态物品定义；运行期实例会保存资产引用并只读取得这些不可变配置。 */
	UCatInventoryItemDefinition(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 按片段类型查找定义上的静态语义；库存、装备或玩法系统用它读取自己关心的那一片配置。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	UCatInventoryItemFragment* FindFragmentByClass(TSubclassOf<UCatInventoryItemFragment> FragmentClass) const;

	/** 按编译期片段类型读取定义语义；调用者无需重复手写 StaticClass 与 Cast，空结果表示该定义不具备该语义。 */
	template <typename FragmentType>
	FragmentType* FindFragment() const
	{
		return Cast<FragmentType>(FindFragmentByClass(FragmentType::StaticClass()));
	}

	/** 判断定义是否带有某个语义标签；精确匹配用于稳定身份，层级匹配只服务认识该标签的玩法或 UI 汇总。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	bool HasSemanticTag(FGameplayTag Tag, bool bExactMatch = false) const;

	/** 库存目录稳定 ID；商店、存档和 Equipment 读模型都用它对齐同一种物品。 */
	virtual FName GetInventoryDefinitionId() const;

	/** 玩家可见名称；UI 和日志通过这层虚拟读取普通库存资产与装备资产。 */
	virtual FText GetInventoryDisplayName() const;

	/** 玩家可见说明；详情面板通过这层虚拟读取，不参与容量或使用裁决。 */
	virtual FText GetInventoryDescription() const;

	/** 库存格缩略图；表现层通过这层虚拟读取，库存事实不保存贴图资源。 */
	virtual TSoftObjectPtr<UTexture2D> GetInventoryThumbnail() const;

	/** 物品语义标签集合；空集合表示当前定义不声明额外语义信号，调用方不应据此猜测用途。 */
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

	/** 判断两份定义是否可以作为同一种堆叠物合并；稳定 ID 一致或同一资产对象才允许合并。 */
	virtual bool CanStackWith(const UCatInventoryItemDefinition& Other) const;

	/** 支持定义资产被网络引用解析；库存不把这份静态配置当作运行实例注册或复制。 */
	virtual bool IsSupportedForNetworking() const override;

public:
	/** 物品稳定 ID；普通库存资产直接写它，装备资产会通过覆盖方法返回自己的 EquipmentDefinitionId。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FName InventoryDefinitionId = NAME_None;

	/** 玩家可见名称；普通库存资产直接写它，装备资产会通过覆盖方法返回自己的 DisplayName。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FText InventoryDisplayName;

	/** 玩家可见说明；普通库存资产直接写它，不参与库存容量、收货或使用裁决。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation", meta = (MultiLine = "true"))
	FText InventoryDescription;

	/** 库存格缩略图；普通库存资产直接写它，格子事实只保存实例和数量。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> InventoryThumbnail;

	/** 这类物品离开库存后的世界拾取 Actor；丢弃/放置读取它，必须实现 InventoryWorldItem，空值表示尚未配置落地表现。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World")
	TSoftClassPtr<AActor> WorldActorClass;

	/** 物品语义标签；消费者只读取自己认识的语义信号，库存核心不把装备、草药或任务物写成业务枚举。 */
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
