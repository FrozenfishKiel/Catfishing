#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "CatCollectionWidget.generated.h"

class UCatCollectionPageController;
class UCatFishCardWidget;
class SUniformGridPanel;

/** 图鉴与库存追踪共用的鱼卡投影；只含允许展示的信息，未捕获时偏好与名字均已脱敏。 */
USTRUCT(BlueprintType)
struct FCatCollectionEntryView
{
	GENERATED_BODY()
	/** 总表数字鱼种编号；用于选中与追踪，不是实物 GUID。 */
	UPROPERTY(BlueprintReadOnly) int32 ItemId = 0;
	/** 卡片名称；Model 仅在成功捕获后提供真名，否则为问号。 */
	UPROPERTY(BlueprintReadOnly) FText DisplayName;
	/** 对应鱼自己的图片；本版按用户确认直接压暗作未解锁占位，不使用其他鱼冒充。 */
	UPROPERTY(BlueprintReadOnly) TSoftObjectPtr<UTexture2D> Thumbnail;
	/** 是否已成功捕获；页面据此开放选择和追踪，吃鱼记录不能置真。 */
	UPROPERTY(BlueprintReadOnly) bool bRecordedUnlocked = false;
	/** 倍率大于 1 的鱼饵名称；Model 按鱼定义关联编号查总表，未解锁只给问号，卡片直接展示。 */
	UPROPERTY(BlueprintReadOnly) FText BaitPreferenceText;
	/** 现有窝料需求类别；按腥、香、发酵属性展示，未解锁只含问号。 */
	UPROPERTY(BlueprintReadOnly) FText ChumPreferenceText;
	/** 策划指定推荐鱼饵的总表图片；Model 仅为已解锁鱼解析，无配置或无效 ID 时为空。 */
	UPROPERTY(BlueprintReadOnly) TSoftObjectPtr<UTexture2D> RecommendedBaitThumbnail;
	/** 策划指定推荐窝料的总表图片；团队库存只展示，不反推类别或自行选择替代物品。 */
	UPROPERTY(BlueprintReadOnly) TSoftObjectPtr<UTexture2D> RecommendedChumThumbnail;
	/** 个人最佳重量保留在读模型供已有记录消费者读取；本版鱼卡不绘制这一栏。 */
	UPROPERTY(BlueprintReadOnly) double BestWeightKilograms = 0.0;
};

/** 当前账号的鱼卡集合；不可用与空集合明确区分，追踪只保存数字身份。 */
USTRUCT(BlueprintType)
struct FCatCollectionViewState
{
	GENERATED_BODY()
	/** 账号档案与物品目录均可读时为真；否则界面显示未就绪，不能提交追踪。 */
	UPROPERTY(BlueprintReadOnly) bool bAvailable = false;
	/** 按数字身份排序的真实鱼种；页面按每跨页十二张划分。 */
	UPROPERTY(BlueprintReadOnly) TArray<FCatCollectionEntryView> Entries;
	/** 已持久化的追踪鱼种；零表示未追踪。 */
	UPROPERTY(BlueprintReadOnly) int32 TrackedItemId = 0;
	/** 标题或未就绪提示；不含日期与完成度。 */
	UPROPERTY(BlueprintReadOnly) FText SummaryText;
};

/** 正式 WBP 的双页书本基类；选择、翻页只改变页面状态，追踪通过控制器提交持久化。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatCollectionWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 更新同源鱼卡并校正页码；无效或已消失的选择清除。 */
	void RenderCollection(const FCatCollectionViewState& ViewState);
	/** 只读返回最近投影供检查；不暴露 Profile 写权限。 */
	UFUNCTION(BlueprintPure, Category="Catfishing|Collection")
	const FCatCollectionViewState& GetLastCollectionViewState() const;
	/** 关闭页并交还输入；按钮和关闭键走同一个控制器入口。 */
	UFUNCTION(BlueprintCallable, Category="Catfishing|Collection") void RequestCloseCollection();
protected:
	/** 构造分层书本、鱼卡网格和按钮，背景不包含动态文字与鱼图。 */
	virtual TSharedRef<SWidget> RebuildWidget() override;
	/** 释放 Slate 网格及卡片引用；重新打开时允许重建。 */
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	/** 子控件消费前先处理图鉴关闭键，保持输入恢复。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	/** 根页收到关闭键时复用同一入口。 */
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
	/** 只绘制当前跨页的真实条目；最后一页不填假鱼种。 */
	void RefreshCards();
	/** 只记录待操作鱼种，不改变鱼卡追踪外观或账号记录。 */
	void SelectFish(int32 ItemId);
	/** 从本地玩家 UI 解析已有页面控制器，避免第二套数据入口。 */
	UCatCollectionPageController* ResolveCollectionPageController() const;
	/** 对照控制器打开状态与正式输入配置判断关闭键。 */
	bool ShouldCloseCollectionFromKey(const FKeyEvent& Event) const;
	/** 最近一次只读鱼卡数据；由 Model 推送，页面不改解锁记录。 */
	UPROPERTY(Transient) FCatCollectionViewState LastCollectionViewState;
	/** 当前跨页索引；零起始，刷新时按真实鱼数夹限。 */
	int32 PageIndex = 0;
	/** 页面待操作的鱼种；只决定按钮操作对象，不控制常驻高亮，不等于账号追踪。 */
	int32 SelectedItemId = 0;
	/** 最近一次按钮提交失败的短提示；选择、翻页或下一次操作清除，不表示追踪事实。 */
	FText TrackingErrorText;
	/** 当前页卡片保活；重建时释放旧卡和选择回调。 */
	UPROPERTY(Transient) TArray<TObjectPtr<UCatFishCardWidget>> Cards;
	/** 独立书本背景画刷；反射保活纹理，文字与卡片叠加绘制。 */
	UPROPERTY(Transient) FSlateBrush BookBrush;
	/** 正式中文字体资产；书页生命周期保活。 */
	UPROPERTY(Transient) TObjectPtr<UObject> FontAsset;
	/** 当前 Slate 卡片网格；释放 Slate 时清空，防止持有旧控件树。 */
	TSharedPtr<SUniformGridPanel> Grid;
};
