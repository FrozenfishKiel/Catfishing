#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Core/CatProfileContracts.h"
#include "CatCollectionWidget.generated.h"

class UButton;
class UCatCollectionPageController;
class UTextBlock;

/** 图鉴 UI 的单行展示投影；它来自鱼目录骨架 ＋ Profile durable 快照，不引用任何实物鱼容器。 */
USTRUCT(BlueprintType)
struct FCatCollectionEntryView
{
	GENERATED_BODY()

	/** 鱼定义稳定 ID；图鉴只展示记录，不反向查找鱼护中的实物鱼。 */
	UPROPERTY(BlueprintReadOnly)
	FName FishDefinitionId = NAME_None;

	/** 鱼名；未解锁收集层时留空——纯黑影不给名字（图鉴 §3.1.5:130）。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayName;

	/** 本地 Profile 记录的整页层级；UI 不通过它补 Grant。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishCollectionState State = ECatFishCollectionState::Unknown;

	/** 线索层已解锁：轮廓清晰，给窝料与鱼饵偏好、出现条件。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSilhouetteUnlocked = false;

	/** 收集层已解锁：名字、彩页、出没区域、个人最佳重量、首次遇上的条件回显。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRecordedUnlocked = false;

	/** 知识层已解锁：吃鱼效果补齐。 */
	UPROPERTY(BlueprintReadOnly)
	bool bKnowledgeUnlocked = false;

	/**
	 * 这条鱼有没有知识层。不可食用的咸鱼与湖心巨影没有（图鉴 §3.1.4:122）——
	 * 没有的信息在页面上不存在，连「待解锁」都不留一行。
	 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasKnowledgeLayer = false;

	/** 本地记录中的最佳重量，单位千克；只在收集层解锁后有意义。 */
	UPROPERTY(BlueprintReadOnly)
	double BestWeightKilograms = 0.0;

	/** 个人最佳重量的成文；收集层未解锁时是「待解锁」，不给灰掉的 0.00kg。 */
	UPROPERTY(BlueprintReadOnly)
	FText BestWeightText;

	/** 首次遇上的条件回显（水域／时段／天气）；收集层未解锁时是「待解锁」。 */
	UPROPERTY(BlueprintReadOnly)
	FText FirstConditionText;

	/** 吃鱼效果一栏的成文；没有知识层的鱼这一栏是空的（整栏不存在），未解锁时才是「待解锁」。 */
	UPROPERTY(BlueprintReadOnly)
	FText KnowledgeText;

	/** 合格交手累计次数；只用于展示进度。 */
	UPROPERTY(BlueprintReadOnly)
	int32 EncounterCount = 0;

	/** 给 TextBlock 直接绑定的中文行文本。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayText;
};

/** 相册里一张印记的只读投影；只带稳定 ID 与本人隐藏位，不带图片路径，也不含别人的相册。 */
USTRUCT(BlueprintType)
struct FCatImprintAlbumEntryView
{
	GENERATED_BODY()

	/** 稳定印记 ID；隐藏开关按它提交。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ImprintId;

	/** 该印记所属的一局相册 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RunAlbumId;

	/** 是否是那一局的篝火封面。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRunAlbumCover = false;

	/** 本人是否已在自己的相册里隐藏它。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHidden = false;
};

/** 图鉴/相册界面的完整只读投影；它和地面鱼护等实物容器完全分开。 */
USTRUCT(BlueprintType)
struct FCatCollectionViewState
{
	GENERATED_BODY()

	/** Profile 是否已能提供 durable 图鉴快照；false 表示数据未就绪，不代表空图鉴。 */
	UPROPERTY(BlueprintReadOnly)
	bool bAvailable = false;

	/**
	 * 图鉴条目展示副本，以鱼目录为骨架：每一种正式鱼都有一行，没解锁的那些就是纯黑影
	 * （图鉴 §3.1.5:130「开局满图都是影，你知道湖里有多少种」）。
	 * 数组只读，不包含 Journal、相册隐藏写口或实物鱼引用。
	 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatCollectionEntryView> Entries;

	/** 本人相册索引；一键隐藏的列表来源，不含别人的印记。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatImprintAlbumEntryView> Imprints;

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

	/**
	 * 本人一键隐藏／取消隐藏相册里的任意一张印记（印记册：本人可隐藏任意一张，只影响自己这份索引）。
	 * 它只转交意图，真正的 durable 写口在 UCatProfileSubsystem::SetImprintHidden；
	 * 不发服务器 RPC，不删图片，也不撤下其他参与者手里的同一张印记。
	 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Collection")
	bool RequestSetImprintHidden(FGuid ImprintId, bool bHidden);

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
	/** 只为页面关闭与印记隐藏解析 owning LocalPlayer 的图鉴页面控制器；图鉴数据仍由 Model 单向推送。 */
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
