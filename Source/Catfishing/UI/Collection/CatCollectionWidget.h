#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Core/CatProfileContracts.h"
#include "CatCollectionWidget.generated.h"

class UButton;
class UCatCollectionPageController;
class UTextBlock;

/** 图鉴 UI 的单行展示投影；它来自 Profile durable 快照，不引用任何实物鱼容器。 */
USTRUCT(BlueprintType)
struct FCatCollectionEntryView
{
	GENERATED_BODY()

	/** 鱼定义稳定 ID；图鉴只展示记录，不反向查找鱼护中的实物鱼。 */
	UPROPERTY(BlueprintReadOnly)
	FName FishDefinitionId = NAME_None;

	/** 本地 Profile 记录的公开三态；UI 不通过它补 Grant。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishCollectionState State = ECatFishCollectionState::Unknown;

	/** 本地记录中的最佳重量，单位千克；不是鱼护中当前鱼的重量。 */
	UPROPERTY(BlueprintReadOnly)
	double BestWeightKilograms = 0.0;

	/** 合格交手累计次数；只用于展示进度。 */
	UPROPERTY(BlueprintReadOnly)
	int32 EncounterCount = 0;

	/** 给 TextBlock 直接绑定的中文行文本。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayText;
};

/** 图鉴/相册界面的完整只读投影；它和地面鱼护等实物容器完全分开。 */
USTRUCT(BlueprintType)
struct FCatCollectionViewState
{
	GENERATED_BODY()

	/** Profile 是否已能提供 durable 图鉴快照；false 表示数据未就绪，不代表空图鉴。 */
	UPROPERTY(BlueprintReadOnly)
	bool bAvailable = false;

	/** 图鉴条目展示副本；数组只读，不包含 Journal、相册隐藏写口或实物鱼引用。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatCollectionEntryView> Entries;

	/** 给 WBP 顶部文本直接绑定的摘要。 */
	UPROPERTY(BlueprintReadOnly)
	FText SummaryText;
};

/** 图鉴/相册 WBP 基类；它只读 Profile 记录，不和地面鱼护、商店或 HUD 混在一起。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatCollectionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收 Collection Model 的只读投影并同步给 WBP；不访问任何鱼护容器。 */
	void RenderCollection(const FCatCollectionViewState& ViewState);

	/** 暴露最近一次图鉴投影给蓝图表现；它没有 Profile 引用，不能被蓝图当作图鉴写入口。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Collection")
	const FCatCollectionViewState& GetLastCollectionViewState() const;

	/** 提交关闭图鉴页意图；按钮、图鉴键和 Escape 都走这个入口，输入恢复只由 PageController 成对处理。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Collection")
	void RequestCloseCollection();

protected:
	/** Slate 构造完成后对可选关闭按钮去重绑定；没有该按钮的 WBP 仍可用图鉴键或 Escape 关闭。 */
	virtual void NativeConstruct() override;

	/** 离开视口时解除关闭按钮绑定，避免重建 Slate 后重复提交关闭意图。 */
	virtual void NativeDestruct() override;

	/** 在子控件消费之前处理关闭键，避免焦点落在列表控件上后无法关闭整页。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** 根控件直接收到按键时复用同一关闭判断；其余输入保持默认传播。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** WBP 可选渲染扩展点；正式列表表现可在蓝图里根据 Entries 构建。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Collection")
	void BP_RenderCollection(const FCatCollectionViewState& ViewState);

private:
	/** 只为页面关闭解析 owning LocalPlayer 的图鉴页面控制器；图鉴数据仍由 Model 单向推送。 */
	UCatCollectionPageController* ResolveCollectionPageController() const;

	/** 关闭条件读取页面控制器的唯一打开状态；接受 Escape 与配置解析出的图鉴键。 */
	bool ShouldCloseCollectionFromKey(const FKeyEvent& InKeyEvent) const;

	/** 最近一次图鉴只读投影；本 Widget 不持有 Profile 子系统。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Collection", meta = (AllowPrivateAccess = "true"))
	FCatCollectionViewState LastCollectionViewState;

	/** 给 WBP TextBlock 直接绑定的图鉴摘要文本。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Collection", meta = (AllowPrivateAccess = "true"))
	FText BlueprintSummaryText;

	/** 给 WBP TextBlock 直接绑定的图鉴列表文本；简单 WBP 可先显示它，复杂列表再用 Entries 创建行控件。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Collection", meta = (AllowPrivateAccess = "true"))
	FText BlueprintEntriesText;

	/** WBP Designer 中的图鉴摘要文本控件；存在时 RenderCollection 会直接写入记录数量。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SummaryTextBlock;

	/** WBP Designer 中的图鉴列表文本控件；存在时 RenderCollection 会直接写入只读记录列表。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EntriesTextBlock;

	/** WBP Designer 中的可选关闭按钮；存在时点击只提交关闭意图，不改变任何图鉴记录。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;
};
