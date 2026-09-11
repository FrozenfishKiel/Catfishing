#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "UI/WorldInfo/CatWorldInfoTypes.h"
#include "CatWorldInfoWidget.generated.h"

class UImage;
class UListView;
class UProgressBar;
class UTextBlock;
class UWidgetSwitcher;

/** UMG ListView 的只读行载荷；列表负责复用正式行 WBP，不在运行时拼接控件树。 */
UCLASS(BlueprintType)
class CATFISHING_API UCatWorldInfoListItem : public UObject
{
	GENERATED_BODY()
public:
	/** 本行最新显示值；信息牌构建时复制，行 WBP 仅读取，不引用源 Actor。 */
	UPROPERTY(BlueprintReadOnly)
	FCatWorldInfoRow Data;
};

/** 正式信息行 WBP 的数据绑定基类；字体、图标大小、进度样式全部保留在资产中。 */
UCLASS(Abstract, Blueprintable)
class CATFISHING_API UCatWorldInfoRowWidget : public UUserWidget, public IUserObjectListEntry
{
	GENERATED_BODY()
protected:
	/** ListView 复用一行时覆盖全部展示字段，缺失图标或进度必须收起旧内容。 */
	virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;
	/** 概念名称控件；WBP 提供布局，绑定入口只写文本。 */
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> LabelText;
	/** 数值与单位控件；绑定入口写入完整格式化结果。 */
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> ValueText;
	/** 可选字段图标的控件；空资源时绑定入口隐藏它。 */
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UImage> RowIcon;
	/** 可选的归一化进度条；行没有进度语义时绑定入口收起，不绘制伪零值。 */
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> RowProgress;
};

/** 对象信息牌的只读 WBP 基类；摘要和详情两套布局由设计资产持有，C++ 不构建 Slate 或 WidgetTree。 */
UCLASS(Abstract, Blueprintable)
class CATFISHING_API UCatWorldInfoWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 显示控制器交付一次只读内容；自定义 WBP 可覆盖渲染，不访问玩法写入口。 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="World Info")
	void RenderInfo(const FCatWorldInfoViewData& Data, ECatWorldInfoDetail Detail);
	/** 控制器交付可见快照后选择摘要或详情并替换对应列表载荷；调用前须排除 Hidden，本方法不执行显隐资格判断。 */
	virtual void RenderInfo_Implementation(const FCatWorldInfoViewData& Data, ECatWorldInfoDetail Detail);
protected:
	/** 资产初始化后禁用焦点；所有信息牌都不接管玩家操作。 */
	virtual void NativeOnInitialized() override;
	/** 摘要与详情的资产级布局选择器；WBP 提供两页，RenderInfo 写入索引零选择摘要、一选择详情。 */
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UWidgetSwitcher> DetailSwitcher;
	/** 完整信息的对象名称控件；WBP 绑定，RenderInfo 在详情页激活时写入快照标题。 */
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TitleText;
	/** 完整信息的阶段或拒绝说明控件；WBP 绑定，RenderInfo 写入快照状态，无说明时收起文本，外层布局仍由资产决定。 */
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> StatusText;
	/** 完整信息的列表控件；WBP 绑定并配置行模板，RenderInfo 写入全部行载荷，由列表生成与复用行视图。 */
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UListView> InfoList;
	/** 摘要布局的对象名称控件；WBP 绑定，RenderInfo 写入与完整标题相同的快照标题。 */
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> SummaryTitleText;
	/** 摘要布局的状态说明控件；WBP 绑定，RenderInfo 每次覆盖文本和显隐，避免残留上一份状态。 */
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> SummaryStatusText;
	/** 摘要布局的重点行列表控件；WBP 绑定，RenderInfo 只写入提供者标为摘要的行，由列表消费载荷。 */
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UListView> SummaryInfoList;
};
