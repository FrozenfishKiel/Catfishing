#include "CatFrontendWidgetAuthoringLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "UI/Frontend/CatFrontendRootWidget.h"
#include "UI/Save/CatLakeMainMenuWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Blueprint/UserWidget.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/Widget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Templates/Function.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintOperationUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Styling/CoreStyle.h"
#include "UObject/UnrealType.h"

namespace CatFrontendWidgetAuthoring
{
	/** 所有正式 Frontend WBP 的唯一内容目录；资产构造器只在此目录检查或新建对象。 */
	const FString WidgetDirectory = TEXT("/Game/UI/Frontend");
	/** 局内玩家菜单 WBP 的唯一内容目录；它归属 Save/UI 链路，和 Frontend 页面资产分开维护。 */
	const FString LakeMenuWidgetDirectory = TEXT("/Game/UI/Save");
	/** 正式声音设置资源目录；它与关卡、角色和既有音乐素材隔离，供 Settings 的软引用精确 Cook。 */
	const FString AudioDirectory = TEXT("/Game/Audio/Settings");
	/** 前端正式中文 Font 资产路径；作者器用它覆盖 CoreStyle 默认西文字体，避免中文在 WBP 预览和打包中变成缺字占位。 */
	const TCHAR* const FrontendChineseFontPath = TEXT("/Game/UI/Shop/F_CatShopChinese.F_CatShopChinese");
	/** 前端文本统一使用的字重名；当前中文 Font 资产只承诺 Regular 字面，标题靠字号区分层级。 */
	const FName FrontendTypefaceName = TEXT("Regular");
	/** 输入框和下拉框没有可用字号来源时使用的默认字号；与作者器 1280x720 设计画布的正文尺寸保持一致。 */
	const int32 FrontendDefaultFontSize = 18;

	/** 一个页面资产必须提供的具名控件；资产验证用它确认 Root 的原生子树解析不会落到空指针。 */
	struct FRequiredWidgetControl
	{
		/** 控件在对应 WidgetTree 中的稳定 Designer 名称；Root 的 FindPageControl 按该名称解析。 */
		FName Name;
		/** 控件必须继承的 UMG 类型；同名但类型不符会让实际委托或文本刷新失败，因此不能只查名称。 */
		UClass* RequiredClass;
	};

	/** 加载项目内的中文 Font 资产；该资产是 WBP 序列化引用，打包时会随正式前端资源一起被依赖收集。 */
	UObject* LoadFrontendChineseFont()
	{
		// 字体加载流程：先复用同进程缓存，缓存不可用时按固定内容路径加载；缺失时记录明确事件，让作者脚本失败而不是静默生成缺字界面。
		static TWeakObjectPtr<UObject> CachedFont;
		if (!CachedFont.IsValid())
		{
			CachedFont = LoadObject<UObject>(nullptr, FrontendChineseFontPath);
		}
		if (!CachedFont.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("Event=frontend_chinese_font_missing Font=%s"), FrontendChineseFontPath);
		}
		return CachedFont.Get();
	}

	/** 生成前端文本控件使用的 Slate 字体信息；字号来自控件语义，字体对象统一指向项目中文 Font。 */
	FSlateFontInfo MakeFrontendFont(const int32 RequestedFontSize)
	{
		// 字体信息创建流程：保留调用方给出的字号，绑定中文 Font 的 Regular 字面；缺字体时退回 CoreStyle 只为保持编辑器可继续暴露错误。
		const int32 FontSize = FMath::Max(1, RequestedFontSize);
		if (UObject* FontObject = LoadFrontendChineseFont())
		{
			return FSlateFontInfo(FontObject, FontSize, FrontendTypefaceName);
		}
		return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FontSize);
	}

	/** 写入下拉框模板的字体数据；作者器在保存 WBP 前使用它保持当前值和列表项共用同一中文 Font，失败时返回 false 阻止上层计入已修复。 */
	bool SetComboBoxTemplateFont(UComboBoxString* ComboBox, const FSlateFontInfo& FontInfo)
	{
		// 下拉框模板字体写入流程：先确认控件实例、引擎保存字段和结构类型都匹配，再写入可序列化模板值；任一不满足都会记录资产作者器错误并返回失败。
		FStructProperty* FontProperty = FindFProperty<FStructProperty>(UComboBoxString::StaticClass(), TEXT("Font"));
		if (!ComboBox || !FontProperty || FontProperty->Struct != FSlateFontInfo::StaticStruct())
		{
			UE_LOG(LogTemp, Error, TEXT("Event=frontend_combo_authoring_font_field_missing"));
			return false;
		}
		*FontProperty->ContainerPtrToValuePtr<FSlateFontInfo>(ComboBox) = FontInfo;
		return true;
	}

	/** 写入下拉框模板的前景色；作者器只在 WBP 模板阶段设置它，运行时颜色交给生成出的 Slate 控件消费，失败时返回 false。 */
	bool SetComboBoxTemplateForegroundColor(UComboBoxString* ComboBox, const FSlateColor& ForegroundColor)
	{
		// 下拉框模板颜色写入流程：先确认控件实例、引擎保存字段和结构类型都匹配，再写入前景色；任一不满足都会记录资产作者器错误并返回失败。
		FStructProperty* ForegroundColorProperty = FindFProperty<FStructProperty>(
			UComboBoxString::StaticClass(), TEXT("ForegroundColor"));
		if (!ComboBox || !ForegroundColorProperty || ForegroundColorProperty->Struct != FSlateColor::StaticStruct())
		{
			UE_LOG(LogTemp, Error, TEXT("Event=frontend_combo_authoring_foreground_field_missing"));
			return false;
		}
		*ForegroundColorProperty->ContainerPtrToValuePtr<FSlateColor>(ComboBox) = ForegroundColor;
		return true;
	}

	/** 只替换单个前端控件的文本字体；返回是否命中文本类控件，供现有 WBP 修复流程统计保存。 */
	bool ApplyFrontendFontToWidget(UWidget* Widget, int32& OutChangedFontCount)
	{
		// 控件字体修复流程：TextBlock 保留原字号，输入框和下拉框沿用已有样式或默认正文尺寸；非文本控件完全跳过，避免扰动布局和交互。
		if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
		{
			TextBlock->SetFont(MakeFrontendFont(TextBlock->GetFont().Size));
			++OutChangedFontCount;
			return true;
		}
		if (UEditableTextBox* TextBox = Cast<UEditableTextBox>(Widget))
		{
			FEditableTextBoxStyle Style = TextBox->GetWidgetStyle();
			Style.SetFont(MakeFrontendFont(FrontendDefaultFontSize));
			TextBox->SetWidgetStyle(Style);
			++OutChangedFontCount;
			return true;
		}
		if (UComboBoxString* ComboBox = Cast<UComboBoxString>(Widget))
		{
			const int32 FontSize = ComboBox->GetFont().Size > 0 ? ComboBox->GetFont().Size : FrontendDefaultFontSize;
			if (SetComboBoxTemplateFont(ComboBox, MakeFrontendFont(FontSize)))
			{
				++OutChangedFontCount;
			}
			return true;
		}
		return false;
	}

	/** 核验一个字体信息是否已经引用正式中文 Font；调用方负责保证期望字体已加载成功。 */
	bool IsFrontendFontApplied(const FSlateFontInfo& FontInfo, const UObject* ExpectedFont)
	{
		// 字体判断流程：只比较序列化字体对象和字体面名称；字号、颜色和外描边属于排版语义，不参与“是否能显示中文”的判定。
		return ExpectedFont && FontInfo.FontObject == ExpectedFont && FontInfo.TypefaceFontName == FrontendTypefaceName;
	}

	/** 只读核验单个前端控件的字体引用；返回 false 时会给出资产与控件名，方便直接回到 WBP 定位。 */
	bool ValidateFrontendFontOnWidget(const TCHAR* AssetName, UWidget* Widget, const UObject* ExpectedFont, int32& OutCheckedFontCount)
	{
		// 控件字体核验流程：逐类读取 TextBlock、输入框和下拉框字体；非文本控件跳过，任一文本控件未指向中文 Font 都让资产校验失败。
		if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
		{
			++OutCheckedFontCount;
			if (!IsFrontendFontApplied(TextBlock->GetFont(), ExpectedFont))
			{
				UE_LOG(LogTemp, Error, TEXT("Event=frontend_widget_font_invalid Asset=%s Control=%s Type=TextBlock"), AssetName, *Widget->GetName());
				return false;
			}
			return true;
		}
		if (UEditableTextBox* TextBox = Cast<UEditableTextBox>(Widget))
		{
			++OutCheckedFontCount;
			if (!IsFrontendFontApplied(TextBox->GetWidgetStyle().TextStyle.Font, ExpectedFont))
			{
				UE_LOG(LogTemp, Error, TEXT("Event=frontend_widget_font_invalid Asset=%s Control=%s Type=EditableTextBox"), AssetName, *Widget->GetName());
				return false;
			}
			return true;
		}
		if (UComboBoxString* ComboBox = Cast<UComboBoxString>(Widget))
		{
			++OutCheckedFontCount;
			// 这里读的是 WBP 模板上的序列化字段，不是运行时生成的 Slate 子控件；验证目标是确认资产重载后仍会拿到中文 Font。
			const bool bValid = IsFrontendFontApplied(ComboBox->GetFont(), ExpectedFont);
			if (!bValid)
			{
				UE_LOG(LogTemp, Error, TEXT("Event=frontend_widget_font_invalid Asset=%s Control=%s Type=ComboBoxString"), AssetName, *Widget->GetName());
				return false;
			}
			return true;
		}
		return true;
	}

	/** 将新建控件注册为 Blueprint 变量，使 Root 的 BindWidgetOptional 合同能在编译阶段解析；重复生成时同名映射保持幂等。 */
	void ExposeWidget(UWidgetBlueprint* WidgetBlueprint, UWidget* Widget)
	{
		// 控件变量注册流程：先标记 Designer 变量，再只为尚未登记的名称分配 GUID，最后由外层统一结构化编译生成运行时绑定。
		if (!WidgetBlueprint || !Widget)
		{
			return;
		}
		FWidgetBlueprintOperationUtils::ToggleWidgetAsVariable(WidgetBlueprint, Widget, true, false);
		if (!WidgetBlueprint->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
		{
			WidgetBlueprint->OnVariableAdded(Widget->GetFName());
		}
	}

	/** 把控件铺满 Canvas；全屏背景、遮罩和页面缩放层都用同一锚点，避免固定分辨率在窄视口发生重叠。 */
	void AddFullCanvasChild(UCanvasPanel* Canvas, UWidget* Child, const int32 ZOrder)
	{
		// Canvas 布局流程：添加子控件后锁定四角锚点、清空边距并指定层级；无 Canvas 或控件时保持无副作用。
		if (!Canvas || !Child)
		{
			return;
		}
		if (UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child))
		{
			Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			Slot->SetOffsets(FMargin(0.0f));
			Slot->SetZOrder(ZOrder);
		}
	}

	/** 统一盒式容器的自动尺寸、剩余空间分配和间距；作者阶段调用，使列表拿到可滚动高度而文字与命令保留内容尺寸。 */
	void SetBoxSlot(UWidget* Widget, const bool bFill, const FMargin& Padding = FMargin(0.0f))
	{
		// 槽位布局流程：读取控件当前所属的横列或纵列槽，写入空间分配和边距；其他容器的锚点由创建它们的布局方法负责。
		if (!Widget)
		{
			return;
		}
		if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(Widget->Slot))
		{
			VerticalSlot->SetSize(FSlateChildSize(bFill ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic));
			VerticalSlot->SetPadding(Padding);
			VerticalSlot->SetHorizontalAlignment(HAlign_Fill);
			VerticalSlot->SetVerticalAlignment(bFill ? VAlign_Fill : VAlign_Center);
		}
		else if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(Widget->Slot))
		{
			HorizontalSlot->SetSize(FSlateChildSize(bFill ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic));
			HorizontalSlot->SetPadding(Padding);
			HorizontalSlot->SetHorizontalAlignment(HAlign_Fill);
			HorizontalSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

	/** 提供按钮与下拉框共用的深色交互表面；样式值直接序列化到新 WBP，Designer 可以独立修改。 */
	FButtonStyle MakeFrontendButtonStyle()
	{
		// 样式创建流程：为普通、悬停、按下和禁用状态指定平面笔刷及前景色，固定内容边距以避免按下时文本跳位。
		FButtonStyle Style;
		Style.SetNormal(FSlateColorBrush(FLinearColor(0.055f, 0.065f, 0.064f, 0.94f)));
		Style.SetHovered(FSlateColorBrush(FLinearColor(0.035f, 0.23f, 0.20f, 1.0f)));
		Style.SetPressed(FSlateColorBrush(FLinearColor(0.025f, 0.31f, 0.26f, 1.0f)));
		Style.SetDisabled(FSlateColorBrush(FLinearColor(0.055f, 0.058f, 0.058f, 0.8f)));
		Style.SetNormalForeground(FLinearColor(0.9f, 0.94f, 0.93f));
		Style.SetHoveredForeground(FLinearColor::White);
		Style.SetPressedForeground(FLinearColor::White);
		Style.SetDisabledForeground(FLinearColor(0.46f, 0.51f, 0.50f));
		Style.SetNormalPadding(FMargin(18.0f, 10.0f));
		Style.SetPressedPadding(FMargin(18.0f, 10.0f));
		return Style;
	}

	/** 创建有明确字号、颜色和溢出规则的文本；页面标题可传入更大字号，默认正文按 1280x720 设计画布使用 18px。 */
	UTextBlock* AddText(UWidgetTree* WidgetTree, UPanelWidget* Parent, const TCHAR* Name, const TCHAR* Text, const int32 FontSize = 18)
	{
		// 文本创建流程：在当前树内构造具名文本，设置字体与换行，再加入父容器并登记变量；布局裁剪与省略号防止动态长文本盖住相邻控件。
		if (!WidgetTree || !Parent)
		{
			return nullptr;
		}
		UTextBlock* TextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
		if (TextBlock)
		{
			TextBlock->SetText(FText::FromString(Text));
			TextBlock->SetFont(MakeFrontendFont(FontSize));
			TextBlock->SetColorAndOpacity(FLinearColor(0.9f, 0.94f, 0.93f));
			TextBlock->SetAutoWrapText(true);
			TextBlock->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
			TextBlock->SetClipping(EWidgetClipping::ClipToBounds);
			Parent->AddChild(TextBlock);
			SetBoxSlot(TextBlock, false, FMargin(0.0f, 4.0f));
			ExposeWidget(Cast<UWidgetBlueprint>(WidgetTree->GetOuter()), TextBlock);
		}
		return TextBlock;
	}

	/** 创建深色命令按钮并保持现有原生点击合同；直接挂入盒式容器，让 Root 折叠按钮时同步释放其布局空间。 */
	UButton* AddButton(UWidgetTree* WidgetTree, UPanelWidget* Parent, const TCHAR* Name, const TCHAR* Label)
	{
		// 按钮创建流程：设置四态笔刷和 18px 标签，横排命令均分宽度、纵排命令保留内容高度；按钮变量由 Root 解析后绑定业务请求。
		if (!WidgetTree || !Parent)
		{
			return nullptr;
		}
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
		if (!Button)
		{
			return nullptr;
		}
		Button->SetStyle(MakeFrontendButtonStyle());
		Button->SetToolTipText(FText::FromString(Label));
		UTextBlock* LabelText = AddText(WidgetTree, Button, *FString::Printf(TEXT("%sLabel"), Name), Label);
		if (!LabelText)
		{
			return nullptr;
		}
		LabelText->SetColorAndOpacity(FSlateColor::UseForeground());
		LabelText->SetJustification(ETextJustify::Center);
		Parent->AddChild(Button);
		SetBoxSlot(Button, Parent->IsA<UHorizontalBox>(), FMargin(0.0f, 4.0f, 8.0f, 4.0f));
		ExposeWidget(Cast<UWidgetBlueprint>(WidgetTree->GetOuter()), Button);
		return Button;
	}

	/** 创建深色单行输入框；玩家输入由现有 Root 读取，作者阶段只设定字体、内边距与焦点外观。 */
	UEditableTextBox* AddTextBox(UWidgetTree* WidgetTree, UPanelWidget* Parent, const TCHAR* Name, const TCHAR* Hint)
	{
		// 输入框创建流程：构造后复制引擎文本样式并覆盖字号与四态底色，再加入父容器；取消固定最小宽度，避免窄列挤出边界。
		if (!WidgetTree || !Parent)
		{
			return nullptr;
		}
		UEditableTextBox* TextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), FName(Name));
		if (TextBox)
		{
			FEditableTextBoxStyle Style = TextBox->GetWidgetStyle();
			Style.SetFont(MakeFrontendFont(FrontendDefaultFontSize));
			Style.SetPadding(FMargin(14.0f, 10.0f));
			Style.SetForegroundColor(FLinearColor(0.9f, 0.94f, 0.93f));
			Style.SetBackgroundImageNormal(FSlateColorBrush(FLinearColor(0.065f, 0.075f, 0.074f)));
			Style.SetBackgroundImageHovered(FSlateColorBrush(FLinearColor(0.08f, 0.12f, 0.11f)));
			Style.SetBackgroundImageFocused(FSlateColorBrush(FLinearColor(0.035f, 0.17f, 0.145f)));
			Style.SetBackgroundImageReadOnly(FSlateColorBrush(FLinearColor(0.05f, 0.055f, 0.054f)));
			TextBox->SetWidgetStyle(Style);
			TextBox->SetHintText(FText::FromString(Hint));
			Parent->AddChild(TextBox);
			SetBoxSlot(TextBox, Parent->IsA<UHorizontalBox>(), FMargin(0.0f, 4.0f, 12.0f, 4.0f));
			ExposeWidget(Cast<UWidgetBlueprint>(WidgetTree->GetOuter()), TextBox);
		}
		return TextBox;
	}

	/** 创建并序列化离散选项下拉框；DefaultOptions 是保存来源，运行时 SettingsModel 仍可替换为真实枚举结果。 */
	UComboBoxString* AddComboBox(UWidgetTree* WidgetTree, UPanelWidget* Parent, const TCHAR* Name, TConstArrayView<const TCHAR*> Options)
	{
		// 下拉框创建流程：先核实引擎默认选项属性，再同时写入可保存的 DefaultOptions 和当前预览选项；随后配置字体、按钮与列表选中态，暴露原生绑定变量。
		if (!WidgetTree || !Parent)
		{
			return nullptr;
		}
		UComboBoxString* ComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), FName(Name));
		FArrayProperty* OptionsProperty = FindFProperty<FArrayProperty>(UComboBoxString::StaticClass(), TEXT("DefaultOptions"));
		if (!ComboBox || !OptionsProperty || !CastField<FStrProperty>(OptionsProperty->Inner))
		{
			UE_LOG(LogTemp, Error, TEXT("Event=frontend_combo_authoring_missing_default_options Control=%s"), Name);
			return nullptr;
		}
		// UE 的 AddOption 只更新运行期 Options；通过反射写入私有 EditAnywhere 数组，保证重载 WBP 后仍有默认项，不修改引擎类。
		TArray<FString>* DefaultOptions = OptionsProperty->ContainerPtrToValuePtr<TArray<FString>>(ComboBox);
		for (const TCHAR* Option : Options)
		{
			DefaultOptions->Add(Option);
			ComboBox->AddOption(Option);
		}
		if (Options.Num() > 0)
		{
			ComboBox->SetSelectedOption(Options[0]);
		}
		SetComboBoxTemplateFont(ComboBox, MakeFrontendFont(FrontendDefaultFontSize));
		SetComboBoxTemplateForegroundColor(ComboBox, FSlateColor(FLinearColor(0.9f, 0.94f, 0.93f)));
		FComboBoxStyle Style = ComboBox->GetWidgetStyle();
		Style.ComboButtonStyle.SetButtonStyle(MakeFrontendButtonStyle());
		Style.ComboButtonStyle.SetMenuBorderBrush(FSlateColorBrush(FLinearColor(0.035f, 0.042f, 0.04f)));
		ComboBox->SetWidgetStyle(Style);
		FTableRowStyle ItemStyle = ComboBox->GetItemStyle();
		ItemStyle.SetEvenRowBackgroundBrush(FSlateColorBrush(FLinearColor(0.04f, 0.05f, 0.047f)));
		ItemStyle.SetOddRowBackgroundBrush(FSlateColorBrush(FLinearColor(0.04f, 0.05f, 0.047f)));
		ItemStyle.SetEvenRowBackgroundHoveredBrush(FSlateColorBrush(FLinearColor(0.03f, 0.20f, 0.17f)));
		ItemStyle.SetOddRowBackgroundHoveredBrush(FSlateColorBrush(FLinearColor(0.03f, 0.20f, 0.17f)));
		ItemStyle.SetActiveBrush(FSlateColorBrush(FLinearColor(0.03f, 0.27f, 0.23f)));
		ItemStyle.SetInactiveBrush(FSlateColorBrush(FLinearColor(0.03f, 0.20f, 0.17f)));
		ItemStyle.SetTextColor(FLinearColor(0.9f, 0.94f, 0.93f));
		ItemStyle.SetSelectedTextColor(FLinearColor::White);
		ComboBox->SetItemStyle(ItemStyle);
		ComboBox->SetContentPadding(FMargin(10.0f, 6.0f));
		ComboBox->SetMaxListHeight(280.0f);
		Parent->AddChild(ComboBox);
		SetBoxSlot(ComboBox, true, FMargin(12.0f, 4.0f, 0.0f, 4.0f));
		ExposeWidget(Cast<UWidgetBlueprint>(WidgetTree->GetOuter()), ComboBox);
		return ComboBox;
	}

	/** 归一化设置滑块表示 0..1 的 UI 草稿输入面；作者库只固定可交互控件与视觉尺寸，具体业务单位由 Root/Model 转换。 */
	USlider* AddSlider(UWidgetTree* WidgetTree, UPanelWidget* Parent, const TCHAR* Name, const float InitialValue)
	{
		// 滑块创建流程：设置 0..1 范围与预览值，保留引擎交互并配置青绿色手柄，随后给横排条目分配剩余宽度。
		if (!WidgetTree || !Parent)
		{
			return nullptr;
		}
		USlider* Slider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), FName(Name));
		if (Slider)
		{
			Slider->SetMinValue(0.0f);
			Slider->SetMaxValue(1.0f);
			Slider->SetValue(InitialValue);
			FSliderStyle Style = Slider->GetWidgetStyle();
			Style.SetBarThickness(4.0f);
			Style.NormalThumbImage.ImageSize = FVector2D(14.0f, 22.0f);
			Style.HoveredThumbImage.ImageSize = FVector2D(14.0f, 22.0f);
			Style.DisabledThumbImage.ImageSize = FVector2D(14.0f, 22.0f);
			Slider->SetWidgetStyle(Style);
			Slider->SetSliderBarColor(FLinearColor(0.18f, 0.23f, 0.21f));
			Slider->SetSliderHandleColor(FLinearColor(0.18f, 0.77f, 0.63f));
			Parent->AddChild(Slider);
			SetBoxSlot(Slider, true, FMargin(12.0f, 4.0f));
			ExposeWidget(Cast<UWidgetBlueprint>(WidgetTree->GetOuter()), Slider);
		}
		return Slider;
	}

	/** 创建有明确勾选色的布尔控件；空标签用于已有独立左侧名称的设置行，业务状态仍由 SettingsModel 刷新。 */
	UCheckBox* AddCheckBox(UWidgetTree* WidgetTree, UPanelWidget* Parent, const TCHAR* Name, const TCHAR* Label)
	{
		// 复选框创建流程：沿用引擎勾选图形，仅调整选中色和间距；添加可选标签后暴露变量，作者器不提交设置值。
		if (!WidgetTree || !Parent)
		{
			return nullptr;
		}
		UCheckBox* CheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), FName(Name));
		if (!CheckBox || !AddText(WidgetTree, CheckBox, *FString::Printf(TEXT("%sLabel"), Name), Label))
		{
			return nullptr;
		}
		FCheckBoxStyle Style = CheckBox->GetWidgetStyle();
		Style.CheckedImage.TintColor = FLinearColor(0.18f, 0.77f, 0.63f);
		Style.CheckedHoveredImage.TintColor = FLinearColor(0.3f, 0.9f, 0.76f);
		Style.CheckedPressedImage.TintColor = FLinearColor(0.14f, 0.62f, 0.51f);
		Style.SetPadding(FMargin(4.0f));
		CheckBox->SetWidgetStyle(Style);
		Parent->AddChild(CheckBox);
		SetBoxSlot(CheckBox, true, FMargin(12.0f, 4.0f));
		ExposeWidget(Cast<UWidgetBlueprint>(WidgetTree->GetOuter()), CheckBox);
		return CheckBox;
	}

	/** 为一个设置项提供固定行高和左侧名称列；调用方将实际 slider/check/combobox 加到返回横列的剩余空间。 */
	UHorizontalBox* AddSettingRow(UWidgetTree* WidgetTree, UVerticalBox* Parent, const TCHAR* Name, const TCHAR* Label)
	{
		// 设置行创建流程：以 52px 高度约束行，用 196px 名称列为输入保留稳定起点；行内不存储设置值，也不参与分类切换。
		USizeBox* Bounds = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *FString::Printf(TEXT("%sRowBounds"), Name));
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *FString::Printf(TEXT("%sRow"), Name));
		Bounds->SetHeightOverride(52.0f);
		Bounds->SetContent(Row);
		Parent->AddChild(Bounds);
		UTextBlock* LabelText = AddText(WidgetTree, Row, *FString::Printf(TEXT("%sLabelText"), Name), Label);
		LabelText->SetMinDesiredWidth(196.0f);
		LabelText->SetAutoWrapText(false);
		return Row;
	}

	/** 为子页面提供铺满设计画布的透明深色表面和固定安全边距；只有 Root 拥有缩放盒，页面内部使用可分配空间。 */
	UVerticalBox* CreatePageColumn(UWidgetBlueprint* WidgetBlueprint, const TCHAR* PageRootName)
	{
		// 页面布局流程：验证 Canvas 根后创建满幅表面与填充内容列，由 64px 水平及 48px 垂直安全边距决定页面内部可用空间。
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			return nullptr;
		}
		UCanvasPanel* Canvas = Cast<UCanvasPanel>(WidgetBlueprint->WidgetTree->RootWidget);
		if (!Canvas)
		{
			return nullptr;
		}
		UBorder* PageShade = WidgetBlueprint->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
			*FString::Printf(TEXT("%sShade"), PageRootName));
		PageShade->SetBrush(FSlateColorBrush(FLinearColor(0.018f, 0.023f, 0.021f, 0.90f)));
		PageShade->SetPadding(FMargin(64.0f, 48.0f));
		PageShade->SetHorizontalAlignment(HAlign_Fill);
		PageShade->SetVerticalAlignment(VAlign_Fill);
		AddFullCanvasChild(Canvas, PageShade, 0);
		UVerticalBox* Column = WidgetBlueprint->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(PageRootName));
		PageShade->SetContent(Column);
		return Column;
	}

	/** 构造左上品牌、左下菜单与同页退出模态层；原生 Root 通过既有按钮和 MenuCommands 控制焦点及可操作状态。 */
	bool BuildMenuWidget(UWidgetBlueprint* WidgetBlueprint)
	{
		// 首页布局流程：在安全边距内放品牌与副标题，中间让出背景空间，将定宽命令列压到下方；退出层独立铺满 Canvas，初始折叠，显示时以全屏底遮罩阻断穿透。
		UVerticalBox* Column = CreatePageColumn(WidgetBlueprint, TEXT("MenuRoot"));
		if (!Column)
		{
			return false;
		}
		UWidgetTree* Tree = WidgetBlueprint->WidgetTree;
		AddText(Tree, Column, TEXT("NorthStarTitleText"), TEXT("北极星愿景"), 38);
		AddText(Tree, Column, TEXT("MenuSubtitleText"), TEXT("与朋友一同启程"), 16);
		USpacer* Space = Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("MenuOpenSpace"));
		Column->AddChild(Space);
		SetBoxSlot(Space, true);
		USizeBox* CommandBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("MenuCommandBounds"));
		UVerticalBox* MenuCommands = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MenuCommands"));
		CommandBounds->SetWidthOverride(300.0f);
		CommandBounds->SetContent(MenuCommands);
		Column->AddChild(CommandBounds);
		CastChecked<UVerticalBoxSlot>(CommandBounds->Slot)->SetHorizontalAlignment(HAlign_Left);
		ExposeWidget(WidgetBlueprint, MenuCommands);
		AddButton(Tree, MenuCommands, TEXT("StartGameButton"), TEXT("开始游戏"));
		AddButton(Tree, MenuCommands, TEXT("JoinPartyButton"), TEXT("加入队伍"));
		AddButton(Tree, MenuCommands, TEXT("FrontendSettingsButton"), TEXT("设置"));
		AddButton(Tree, MenuCommands, TEXT("ExitGameButton"), TEXT("退出游戏"));

		UOverlay* Modal = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ExitConfirmationOverlay"));
		UBorder* Scrim = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ExitConfirmationScrim"));
		Scrim->SetBrush(FSlateColorBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.76f)));
		UOverlaySlot* ScrimSlot = Modal->AddChildToOverlay(Scrim);
		ScrimSlot->SetHorizontalAlignment(HAlign_Fill);
		ScrimSlot->SetVerticalAlignment(VAlign_Fill);
		USizeBox* DialogBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ExitDialogBounds"));
		DialogBounds->SetWidthOverride(480.0f);
		DialogBounds->SetHeightOverride(240.0f);
		UBorder* DialogSurface = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ExitDialogSurface"));
		DialogSurface->SetBrush(FSlateColorBrush(FLinearColor(0.035f, 0.045f, 0.042f)));
		DialogSurface->SetPadding(FMargin(28.0f));
		UVerticalBox* Dialog = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ExitConfirmationPanel"));
		DialogSurface->SetContent(Dialog);
		DialogBounds->SetContent(DialogSurface);
		UOverlaySlot* DialogSlot = Modal->AddChildToOverlay(DialogBounds);
		DialogSlot->SetHorizontalAlignment(HAlign_Center);
		DialogSlot->SetVerticalAlignment(VAlign_Center);
		AddText(Tree, Dialog, TEXT("ExitDialogTitleText"), TEXT("退出游戏？"), 26);
		AddText(Tree, Dialog, TEXT("ExitConfirmationText"), TEXT("未保存的进度可能丢失。"));
		USpacer* DialogSpace = Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("ExitDialogSpace"));
		Dialog->AddChild(DialogSpace);
		SetBoxSlot(DialogSpace, true);
		UHorizontalBox* Actions = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ExitDialogActions"));
		Dialog->AddChild(Actions);
		SetBoxSlot(AddButton(Tree, Actions, TEXT("ConfirmExitButton"), TEXT("确认退出")), true);
		SetBoxSlot(AddButton(Tree, Actions, TEXT("CancelExitButton"), TEXT("继续游戏")), true, FMargin(12.0f, 0.0f, 0.0f, 0.0f));
		AddFullCanvasChild(CastChecked<UCanvasPanel>(Tree->RootWidget), Modal, 10);
		Modal->SetVisibility(ESlateVisibility::Collapsed);
		ExposeWidget(WidgetBlueprint, Modal);
		return true;
	}

	/** 构造一套与主界面一致的设置控件内容；调用方提供页面容器，运行时仍由对应 View 和 SettingsModel 完成草稿回填。 */
	bool BuildLakeSettingsContent(UWidgetBlueprint* WidgetBlueprint, UVerticalBox* Column)
	{
		// 局内设置内容流程：先生成分类栏和左右详情布局，再按主界面同名合同添加游戏、画面、声音和控制面板。
		// 语音输入与麦克风只放正式禁用占位，声音页保留输出设备刷新入口，底部提供应用、恢复默认和取消动作；任一关键容器缺失都返回失败阻止保存半成品。
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree || !Column)
		{
			return false;
		}
		UWidgetTree* Tree = WidgetBlueprint->WidgetTree;
		AddText(Tree, Column, TEXT("SettingsTitleText"), TEXT("设置"), 30);
		UHorizontalBox* Categories = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SettingsCategories"));
		Column->AddChild(Categories);
		SetBoxSlot(Categories, false, FMargin(0.0f, 12.0f, 0.0f, 20.0f));
		AddButton(Tree, Categories, TEXT("GameSettingsCategoryButton"), TEXT("游戏"));
		AddButton(Tree, Categories, TEXT("GraphicsSettingsCategoryButton"), TEXT("画面"));
		AddButton(Tree, Categories, TEXT("AudioSettingsCategoryButton"), TEXT("声音"));
		AddButton(Tree, Categories, TEXT("ControlsSettingsCategoryButton"), TEXT("控制"));

		UHorizontalBox* SettingsLayout = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SettingsLayout"));
		Column->AddChild(SettingsLayout);
		SetBoxSlot(SettingsLayout, true);
		UScrollBox* DetailsScroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SettingsDetailsScrollBox"));
		UVerticalBox* Details = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SettingsDetails"));
		DetailsScroll->AddChild(Details);
		DetailsScroll->SetClipping(EWidgetClipping::ClipToBounds);
		SettingsLayout->AddChild(DetailsScroll);
		SetBoxSlot(DetailsScroll, true, FMargin(0.0f, 0.0f, 32.0f, 0.0f));
		CastChecked<UHorizontalBoxSlot>(DetailsScroll->Slot)->SetVerticalAlignment(VAlign_Fill);
		ExposeWidget(WidgetBlueprint, DetailsScroll);

		USizeBox* DescriptionBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SettingsDescriptionBounds"));
		DescriptionBounds->SetWidthOverride(300.0f);
		UScrollBox* DescriptionScroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SettingsDescriptionScrollBox"));
		DescriptionBounds->SetContent(DescriptionScroll);
		SettingsLayout->AddChild(DescriptionBounds);
		SetBoxSlot(DescriptionBounds, false);
		CastChecked<UHorizontalBoxSlot>(DescriptionBounds->Slot)->SetVerticalAlignment(VAlign_Fill);
		UTextBlock* Description = AddText(Tree, DescriptionScroll, TEXT("SettingsDescriptionTextBlock"), TEXT("调整语言、语音聊天与手柄震动。"));
		Description->SetWrapTextAt(280.0f);

		UVerticalBox* GamePanel = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GameSettingsPanel"));
		UVerticalBox* GraphicsPanel = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GraphicsSettingsPanel"));
		UVerticalBox* AudioPanel = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AudioSettingsPanel"));
		UVerticalBox* ControlsPanel = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ControlsSettingsPanel"));
		for (UVerticalBox* Panel : { GamePanel, GraphicsPanel, AudioPanel, ControlsPanel })
		{
			Details->AddChild(Panel);
			SetBoxSlot(Panel, false);
			Panel->SetVisibility(Panel == GamePanel ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			ExposeWidget(WidgetBlueprint, Panel);
		}

		const TCHAR* const Languages[] = { TEXT("简体中文"), TEXT("English") };
		const TCHAR* const FullscreenModes[] = { TEXT("全屏"), TEXT("无边框窗口"), TEXT("窗口") };
		const TCHAR* const Resolutions[] = { TEXT("1920 x 1080"), TEXT("1600 x 900"), TEXT("1280 x 720") };
		const TCHAR* const QualityLevels[] = { TEXT("低"), TEXT("中"), TEXT("高"), TEXT("史诗") };
		const TCHAR* const UnavailableVoiceOptions[] = { TEXT("当前平台不支持此设置") };
		AddComboBox(Tree, AddSettingRow(Tree, GamePanel, TEXT("Language"), TEXT("语言")), TEXT("LanguageComboBox"), Languages);
		AddCheckBox(Tree, AddSettingRow(Tree, GamePanel, TEXT("VoiceChat"), TEXT("语音聊天")), TEXT("VoiceChatCheckBox"), TEXT(""));
		UComboBoxString* VoiceInputMode = AddComboBox(Tree, AddSettingRow(Tree, GamePanel, TEXT("VoiceInputMode"), TEXT("语音输入模式")), TEXT("VoiceInputModeComboBox"), UnavailableVoiceOptions);
		UTextBlock* VoiceInputModeUnavailable = AddText(Tree, GamePanel, TEXT("VoiceInputModeUnavailableText"),
			TEXT("当前语音服务不支持切换输入模式。"), 14);
		UComboBoxString* Microphone = AddComboBox(Tree, AddSettingRow(Tree, GamePanel, TEXT("Microphone"), TEXT("麦克风设备")), TEXT("MicrophoneComboBox"), UnavailableVoiceOptions);
		UTextBlock* MicrophoneUnavailable = AddText(Tree, GamePanel, TEXT("MicrophoneUnavailableText"),
			TEXT("当前语音服务不支持选择麦克风，请在系统声音设置中更改默认输入设备。"), 14);
		if (!VoiceInputMode || !Microphone)
		{
			return false;
		}
		VoiceInputMode->SetIsEnabled(false);
		Microphone->SetIsEnabled(false);
		VoiceInputModeUnavailable->SetColorAndOpacity(FLinearColor(0.60f, 0.67f, 0.65f));
		MicrophoneUnavailable->SetColorAndOpacity(FLinearColor(0.60f, 0.67f, 0.65f));
		SetBoxSlot(VoiceInputModeUnavailable, false, FMargin(12.0f, 0.0f, 12.0f, 8.0f));
		SetBoxSlot(MicrophoneUnavailable, false, FMargin(12.0f, 0.0f, 12.0f, 8.0f));
		AddCheckBox(Tree, AddSettingRow(Tree, GamePanel, TEXT("Vibration"), TEXT("手柄震动")), TEXT("VibrationCheckBox"), TEXT(""));

		AddComboBox(Tree, AddSettingRow(Tree, GraphicsPanel, TEXT("FullscreenMode"), TEXT("窗口模式")), TEXT("FullscreenModeComboBox"), FullscreenModes);
		AddComboBox(Tree, AddSettingRow(Tree, GraphicsPanel, TEXT("ScreenResolution"), TEXT("分辨率")), TEXT("ScreenResolutionComboBox"), Resolutions);
		AddComboBox(Tree, AddSettingRow(Tree, GraphicsPanel, TEXT("OverallQuality"), TEXT("画面质量")), TEXT("OverallQualityComboBox"), QualityLevels);
		AddCheckBox(Tree, AddSettingRow(Tree, GraphicsPanel, TEXT("VSync"), TEXT("垂直同步")), TEXT("VSyncCheckBox"), TEXT(""));
		AddSlider(Tree, AddSettingRow(Tree, GraphicsPanel, TEXT("Brightness"), TEXT("亮度")), TEXT("BrightnessSlider"), (2.2f - 0.5f) / 4.5f);
		AddSlider(Tree, AddSettingRow(Tree, GraphicsPanel, TEXT("UIScale"), TEXT("界面缩放")), TEXT("UIScaleSlider"), (1.0f - 0.75f) / 1.25f);

		AddSlider(Tree, AddSettingRow(Tree, AudioPanel, TEXT("MasterVolume"), TEXT("主音量")), TEXT("MasterVolumeSlider"), 1.0f);
		AddSlider(Tree, AddSettingRow(Tree, AudioPanel, TEXT("MusicVolume"), TEXT("音乐")), TEXT("MusicVolumeSlider"), 1.0f);
		AddSlider(Tree, AddSettingRow(Tree, AudioPanel, TEXT("SFXVolume"), TEXT("音效")), TEXT("SFXVolumeSlider"), 1.0f);
		AddSlider(Tree, AddSettingRow(Tree, AudioPanel, TEXT("AmbienceVolume"), TEXT("环境音")), TEXT("AmbienceVolumeSlider"), 1.0f);
		AddSlider(Tree, AddSettingRow(Tree, AudioPanel, TEXT("VoiceVolume"), TEXT("语音")), TEXT("VoiceVolumeSlider"), 1.0f);
		AddCheckBox(Tree, AddSettingRow(Tree, AudioPanel, TEXT("MuteAudioWhenUnfocused"), TEXT("失焦时静音")), TEXT("MuteAudioWhenUnfocusedCheckBox"), TEXT(""));
		UComboBoxString* AudioOutputDevice = AddComboBox(Tree, AddSettingRow(Tree, AudioPanel, TEXT("AudioOutputDevice"), TEXT("输出设备")), TEXT("AudioOutputDeviceComboBox"), {});
		if (!AudioOutputDevice)
		{
			return false;
		}
		AudioOutputDevice->SetIsEnabled(false);
		UButton* RefreshDevices = AddButton(Tree, AudioPanel, TEXT("RefreshAudioOutputDevicesButton"), TEXT("刷新输出设备"));
		CastChecked<UVerticalBoxSlot>(RefreshDevices->Slot)->SetHorizontalAlignment(HAlign_Right);

		AddText(Tree, Column, TEXT("FrontendSettingsResultTextBlock"), TEXT(""), 16);
		UHorizontalBox* Actions = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SettingsActions"));
		Column->AddChild(Actions);
		SetBoxSlot(Actions, false, FMargin(0.0f, 12.0f, 0.0f, 0.0f));
		SetBoxSlot(AddButton(Tree, Actions, TEXT("ApplySettingsButton"), TEXT("应用")), false, FMargin(0.0f, 0.0f, 12.0f, 0.0f));
		SetBoxSlot(AddButton(Tree, Actions, TEXT("RestoreSettingsDefaultsButton"), TEXT("恢复默认")), false);
		USpacer* ActionSpace = Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("SettingsActionsSpace"));
		Actions->AddChild(ActionSpace);
		SetBoxSlot(ActionSpace, true);
		SetBoxSlot(AddButton(Tree, Actions, TEXT("CancelSettingsButton"), TEXT("取消")), false);
		return true;
	}

	/** 构造局内 ESC 菜单的正式 WBP；它提供暂停命令页、设置页和同名设置控件，回主菜单等待表现由全局 Loading WBP 负责。 */
	bool BuildLakeMainMenuWidget(UWidgetBlueprint* WidgetBlueprint)
	{
		// 局内菜单布局流程：先铺全屏半透明遮罩，再用 Switcher 承载命令页和设置页；命令页居中纵排，设置页复用主界面同名控件合同。
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			return false;
		}
		UCanvasPanel* Canvas = Cast<UCanvasPanel>(WidgetBlueprint->WidgetTree->RootWidget);
		if (!Canvas)
		{
			return false;
		}
		UWidgetTree* Tree = WidgetBlueprint->WidgetTree;
		UBorder* Scrim = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LakeMainMenuScrim"));
		UWidgetSwitcher* PageSwitcher = Tree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("LakeMainMenuPageSwitcher"));
		UOverlay* CommandPage = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("LakeCommandPanel"));
		UOverlay* SettingsPage = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("LakeSettingsPanel"));
		if (!Scrim || !PageSwitcher || !CommandPage || !SettingsPage)
		{
			return false;
		}

		Scrim->SetBrush(FSlateColorBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f)));
		AddFullCanvasChild(Canvas, Scrim, 0);
		AddFullCanvasChild(Canvas, PageSwitcher, 1);
		PageSwitcher->AddChild(CommandPage);
		PageSwitcher->AddChild(SettingsPage);
		ExposeWidget(WidgetBlueprint, PageSwitcher);
		ExposeWidget(WidgetBlueprint, CommandPage);
		ExposeWidget(WidgetBlueprint, SettingsPage);

		USizeBox* MenuBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LakeMainMenuBounds"));
		UBorder* MenuSurface = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LakeMainMenuSurface"));
		UVerticalBox* MenuColumn = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LakeMainMenuColumn"));
		if (!MenuBounds || !MenuSurface || !MenuColumn)
		{
			return false;
		}

		MenuBounds->SetWidthOverride(340.0f);
		MenuBounds->SetMinDesiredWidth(300.0f);
		MenuSurface->SetBrush(FSlateColorBrush(FLinearColor(0.035f, 0.045f, 0.042f, 0.96f)));
		MenuSurface->SetPadding(FMargin(26.0f, 24.0f));
		MenuSurface->SetContent(MenuColumn);
		MenuBounds->SetContent(MenuSurface);
		if (UOverlaySlot* MenuSlot = CommandPage->AddChildToOverlay(MenuBounds))
		{
			MenuSlot->SetHorizontalAlignment(HAlign_Center);
			MenuSlot->SetVerticalAlignment(VAlign_Center);
		}

		UTextBlock* Title = AddText(Tree, MenuColumn, TEXT("LakeMainMenuTitleText"), TEXT("暂停菜单"), 28);
		if (!Title)
		{
			return false;
		}
		Title->SetJustification(ETextJustify::Center);
		SetBoxSlot(Title, false, FMargin(0.0f, 0.0f, 0.0f, 18.0f));

		UButton* Close = AddButton(Tree, MenuColumn, TEXT("CloseButton"), TEXT("返回游戏"));
		UButton* Settings = AddButton(Tree, MenuColumn, TEXT("SettingsButton"), TEXT("设置"));
		UButton* Save = AddButton(Tree, MenuColumn, TEXT("SaveButton"), TEXT("保存"));
		UButton* ReturnToMainMenu = AddButton(Tree, MenuColumn, TEXT("ReturnToMainMenuButton"), TEXT("退出到主菜单"));
		UButton* ExitGame = AddButton(Tree, MenuColumn, TEXT("ExitGameButton"), TEXT("退出游戏"));
		UTextBlock* Status = AddText(Tree, MenuColumn, TEXT("StatusTextBlock"), TEXT(""), 14);
		if (!Close || !Settings || !Save || !ReturnToMainMenu || !ExitGame || !Status)
		{
			return false;
		}
		for (UButton* Button : { Close, Settings, Save, ReturnToMainMenu, ExitGame })
		{
			if (UVerticalBoxSlot* ButtonSlot = Cast<UVerticalBoxSlot>(Button->Slot))
			{
				ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
			}
		}
		Status->SetVisibility(ESlateVisibility::Collapsed);
		Status->SetJustification(ETextJustify::Center);
		Status->SetColorAndOpacity(FLinearColor(0.68f, 0.76f, 0.72f));
		SetBoxSlot(Status, false, FMargin(0.0f, 4.0f, 0.0f, 0.0f));

		UScaleBox* SettingsScale = Tree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("LakeSettingsScale"));
		USizeBox* SettingsBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LakeSettingsBounds"));
		UBorder* SettingsSurface = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LakeSettingsSurface"));
		UVerticalBox* SettingsColumn = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LakeSettingsColumn"));
		if (!SettingsScale || !SettingsBounds || !SettingsSurface || !SettingsColumn)
		{
			return false;
		}
		SettingsScale->SetStretch(EStretch::ScaleToFit);
		SettingsBounds->SetMinDesiredWidth(760.0f);
		SettingsBounds->SetMinDesiredHeight(500.0f);
		SettingsBounds->SetWidthOverride(980.0f);
		SettingsBounds->SetHeightOverride(620.0f);
		SettingsSurface->SetBrush(FSlateColorBrush(FLinearColor(0.035f, 0.045f, 0.042f, 0.96f)));
		SettingsSurface->SetPadding(FMargin(28.0f, 24.0f));
		SettingsSurface->SetContent(SettingsColumn);
		SettingsBounds->SetContent(SettingsSurface);
		SettingsScale->SetContent(SettingsBounds);
		if (UOverlaySlot* SettingsSlot = SettingsPage->AddChildToOverlay(SettingsScale))
		{
			SettingsSlot->SetHorizontalAlignment(HAlign_Center);
			SettingsSlot->SetVerticalAlignment(VAlign_Center);
		}
		SettingsPage->SetVisibility(ESlateVisibility::Collapsed);
		return BuildLakeSettingsContent(WidgetBlueprint, SettingsColumn);
	}

	/** 构造保留大块目录空间的纵向存档页；命名输入和操作位于列表下方，不随目录是否为空改变页面结构。 */
	bool BuildSaveListWidget(UWidgetBlueprint* WidgetBlueprint)
	{
		// 存档页布局流程：标题之后将剩余高度交给 ScrollBox，再放同一行命名输入与新建命令、结果及横排操作；删除确认仍由 Root 现有文本和按钮可见性控制。
		UVerticalBox* Column = CreatePageColumn(WidgetBlueprint, TEXT("SaveListRoot"));
		if (!Column)
		{
			return false;
		}
		UWidgetTree* Tree = WidgetBlueprint->WidgetTree;
		SetBoxSlot(AddText(Tree, Column, TEXT("SaveListTitleText"), TEXT("选择存档"), 30), false, FMargin(0.0f, 0.0f, 0.0f, 18.0f));
		UScrollBox* SaveRows = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SaveRowsScrollBox"));
		SaveRows->SetClipping(EWidgetClipping::ClipToBounds);
		Column->AddChild(SaveRows);
		SetBoxSlot(SaveRows, true, FMargin(0.0f, 0.0f, 0.0f, 16.0f));
		ExposeWidget(WidgetBlueprint, SaveRows);
		UHorizontalBox* CreateRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CreateSaveRow"));
		Column->AddChild(CreateRow);
		AddTextBox(Tree, CreateRow, TEXT("CreateSaveNameTextBox"), TEXT("输入新存档名称"));
		SetBoxSlot(AddButton(Tree, CreateRow, TEXT("CreateSaveButton"), TEXT("新建存档")), false);
		AddText(Tree, Column, TEXT("SaveResultTextBlock"), TEXT(""), 16);
		UHorizontalBox* DeleteConfirmation = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("DeleteConfirmationRow"));
		Column->AddChild(DeleteConfirmation);
		UTextBlock* DeleteText = AddText(Tree, DeleteConfirmation, TEXT("DeleteConfirmationText"), TEXT("删除后无法恢复。"), 16);
		UButton* DeleteConfirm = AddButton(Tree, DeleteConfirmation, TEXT("ConfirmDeleteSaveButton"), TEXT("确认删除"));
		DeleteText->SetVisibility(ESlateVisibility::Collapsed);
		DeleteConfirm->SetVisibility(ESlateVisibility::Collapsed);
		UHorizontalBox* Actions = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SaveListActions"));
		Column->AddChild(Actions);
		SetBoxSlot(AddButton(Tree, Actions, TEXT("LoadSelectedSaveButton"), TEXT("读取存档")), false);
		SetBoxSlot(AddButton(Tree, Actions, TEXT("DeleteSelectedSaveButton"), TEXT("删除存档")), false);
		USpacer* Space = Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("SaveActionsSpace"));
		Actions->AddChild(Space);
		SetBoxSlot(Space, true);
		SetBoxSlot(AddButton(Tree, Actions, TEXT("CancelSaveButton"), TEXT("返回")), false);
		return true;
	}

	/** 构造左右等宽的好友与房间成员列表；搜索属于好友列，邀请码与权限属于房间列，两个列表独立滚动。 */
	bool BuildRoomWidget(UWidgetBlueprint* WidgetBlueprint)
	{
		// 房间布局流程：标题下用填充槽分配两列，每列先放自己的信息再把余高交给滚动列表；底部统一承载结果、离开和开始命令，不制造玩家占位。
		UVerticalBox* Column = CreatePageColumn(WidgetBlueprint, TEXT("RoomRoot"));
		if (!Column)
		{
			return false;
		}
		UWidgetTree* Tree = WidgetBlueprint->WidgetTree;
		SetBoxSlot(AddText(Tree, Column, TEXT("RoomTitleText"), TEXT("房间"), 30), false, FMargin(0.0f, 0.0f, 0.0f, 18.0f));
		UHorizontalBox* Lists = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RoomLists"));
		UVerticalBox* FriendsColumn = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FriendsColumn"));
		UVerticalBox* PlayersColumn = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PlayersColumn"));
		Column->AddChild(Lists);
		SetBoxSlot(Lists, true);
		Lists->AddChild(FriendsColumn);
		Lists->AddChild(PlayersColumn);
		SetBoxSlot(FriendsColumn, true, FMargin(0.0f, 0.0f, 24.0f, 0.0f));
		SetBoxSlot(PlayersColumn, true, FMargin(24.0f, 0.0f, 0.0f, 0.0f));
		CastChecked<UHorizontalBoxSlot>(FriendsColumn->Slot)->SetVerticalAlignment(VAlign_Fill);
		CastChecked<UHorizontalBoxSlot>(PlayersColumn->Slot)->SetVerticalAlignment(VAlign_Fill);
		AddText(Tree, FriendsColumn, TEXT("FriendsTitleText"), TEXT("好友"), 22);
		AddTextBox(Tree, FriendsColumn, TEXT("FriendSearchTextBox"), TEXT("搜索好友"));
		UScrollBox* Friends = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("FriendsScrollBox"));
		FriendsColumn->AddChild(Friends);
		SetBoxSlot(Friends, true, FMargin(0.0f, 8.0f));
		AddButton(Tree, FriendsColumn, TEXT("RefreshFriendsButton"), TEXT("刷新好友"));
		AddText(Tree, PlayersColumn, TEXT("PlayersTitleText"), TEXT("当前房间"), 22);
		AddText(Tree, PlayersColumn, TEXT("RoomInviteCodeText"), TEXT("邀请码：未提供"), 16);
		AddText(Tree, PlayersColumn, TEXT("RoomAccessPolicyText"), TEXT("房间权限：未提供"), 16);
		AddButton(Tree, PlayersColumn, TEXT("CopyInviteCodeButton"), TEXT("复制邀请码"));
		UScrollBox* Players = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("PlayersScrollBox"));
		PlayersColumn->AddChild(Players);
		SetBoxSlot(Players, true, FMargin(0.0f, 8.0f));
		Friends->SetClipping(EWidgetClipping::ClipToBounds);
		Players->SetClipping(EWidgetClipping::ClipToBounds);
		ExposeWidget(WidgetBlueprint, Friends);
		ExposeWidget(WidgetBlueprint, Players);
		AddText(Tree, Column, TEXT("RoomResultTextBlock"), TEXT(""), 16);
		UHorizontalBox* Actions = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RoomActions"));
		Column->AddChild(Actions);
		SetBoxSlot(AddButton(Tree, Actions, TEXT("LeaveRoomButton"), TEXT("离开房间")), false);
		USpacer* Space = Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("RoomActionsSpace"));
		Actions->AddChild(Space);
		SetBoxSlot(Space, true);
		SetBoxSlot(AddButton(Tree, Actions, TEXT("StartRoomGameButton"), TEXT("开始游戏")), false);
		return true;
	}

	/** 构造四分类设置页；顶部切页、左侧编辑、右侧说明和底部命令各占独立布局区，原生 Root 负责草稿与分类状态。 */
	bool BuildSettingsWidget(UWidgetBlueprint* WidgetBlueprint)
	{
		// 设置布局流程：先确认页面根，再建立顶部分类栏和占满剩余高度的左右区域；条目只在左侧滚动，说明在右侧独立折行。
		// 四个具名面板沿用现有分类 getter 切换显隐；作者器只生成实际可编辑控件，设备查询、草稿写入与应用仍由 Root/SettingsModel 执行。
		UVerticalBox* Column = CreatePageColumn(WidgetBlueprint, TEXT("SettingsRoot"));
		if (!Column)
		{
			return false;
		}
		UWidgetTree* Tree = WidgetBlueprint->WidgetTree;
		AddText(Tree, Column, TEXT("SettingsTitleText"), TEXT("设置"), 30);
		UHorizontalBox* Categories = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SettingsCategories"));
		Column->AddChild(Categories);
		SetBoxSlot(Categories, false, FMargin(0.0f, 12.0f, 0.0f, 20.0f));
		AddButton(Tree, Categories, TEXT("GameSettingsCategoryButton"), TEXT("游戏"));
		AddButton(Tree, Categories, TEXT("GraphicsSettingsCategoryButton"), TEXT("画面"));
		AddButton(Tree, Categories, TEXT("AudioSettingsCategoryButton"), TEXT("声音"));
		AddButton(Tree, Categories, TEXT("ControlsSettingsCategoryButton"), TEXT("控制"));

		UHorizontalBox* SettingsLayout = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SettingsLayout"));
		Column->AddChild(SettingsLayout);
		SetBoxSlot(SettingsLayout, true);
		UScrollBox* DetailsScroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SettingsDetailsScrollBox"));
		UVerticalBox* Details = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SettingsDetails"));
		DetailsScroll->AddChild(Details);
		DetailsScroll->SetClipping(EWidgetClipping::ClipToBounds);
		SettingsLayout->AddChild(DetailsScroll);
		SetBoxSlot(DetailsScroll, true, FMargin(0.0f, 0.0f, 32.0f, 0.0f));
		CastChecked<UHorizontalBoxSlot>(DetailsScroll->Slot)->SetVerticalAlignment(VAlign_Fill);
		ExposeWidget(WidgetBlueprint, DetailsScroll);

		USizeBox* DescriptionBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SettingsDescriptionBounds"));
		DescriptionBounds->SetWidthOverride(300.0f);
		UScrollBox* DescriptionScroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SettingsDescriptionScrollBox"));
		DescriptionBounds->SetContent(DescriptionScroll);
		SettingsLayout->AddChild(DescriptionBounds);
		SetBoxSlot(DescriptionBounds, false);
		CastChecked<UHorizontalBoxSlot>(DescriptionBounds->Slot)->SetVerticalAlignment(VAlign_Fill);
		UTextBlock* Description = AddText(Tree, DescriptionScroll, TEXT("SettingsDescriptionTextBlock"), TEXT("调整语言、语音聊天与手柄震动。"));
		Description->SetWrapTextAt(280.0f);

		UVerticalBox* GamePanel = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GameSettingsPanel"));
		UVerticalBox* GraphicsPanel = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GraphicsSettingsPanel"));
		UVerticalBox* AudioPanel = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AudioSettingsPanel"));
		UVerticalBox* ControlsPanel = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ControlsSettingsPanel"));
		for (UVerticalBox* Panel : { GamePanel, GraphicsPanel, AudioPanel, ControlsPanel })
		{
			Details->AddChild(Panel);
			SetBoxSlot(Panel, false);
			Panel->SetVisibility(Panel == GamePanel ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			ExposeWidget(WidgetBlueprint, Panel);
		}

		const TCHAR* const Languages[] = { TEXT("简体中文"), TEXT("English") };
		const TCHAR* const FullscreenModes[] = { TEXT("全屏"), TEXT("无边框窗口"), TEXT("窗口") };
		const TCHAR* const Resolutions[] = { TEXT("1920 x 1080"), TEXT("1600 x 900"), TEXT("1280 x 720") };
		const TCHAR* const QualityLevels[] = { TEXT("低"), TEXT("中"), TEXT("高"), TEXT("史诗") };
		const TCHAR* const UnavailableVoiceOptions[] = { TEXT("当前平台不支持此设置") };
		AddComboBox(Tree, AddSettingRow(Tree, GamePanel, TEXT("Language"), TEXT("语言")), TEXT("LanguageComboBox"), Languages);
		AddCheckBox(Tree, AddSettingRow(Tree, GamePanel, TEXT("VoiceChat"), TEXT("语音聊天")), TEXT("VoiceChatCheckBox"), TEXT(""));
		UComboBoxString* VoiceInputMode = AddComboBox(Tree, AddSettingRow(Tree, GamePanel, TEXT("VoiceInputMode"), TEXT("语音输入模式")), TEXT("VoiceInputModeComboBox"), UnavailableVoiceOptions);
		UTextBlock* VoiceInputModeUnavailable = AddText(Tree, GamePanel, TEXT("VoiceInputModeUnavailableText"),
			TEXT("当前语音服务不支持切换输入模式。"), 14);
		UComboBoxString* Microphone = AddComboBox(Tree, AddSettingRow(Tree, GamePanel, TEXT("Microphone"), TEXT("麦克风设备")), TEXT("MicrophoneComboBox"), UnavailableVoiceOptions);
		UTextBlock* MicrophoneUnavailable = AddText(Tree, GamePanel, TEXT("MicrophoneUnavailableText"),
			TEXT("当前语音服务不支持选择麦克风，请在系统声音设置中更改默认输入设备。"), 14);
		if (!VoiceInputMode || !Microphone)
		{
			return false;
		}
		// 两项没有 Settings 写入接口，保留禁用的原位控件和明确原因；说明参与纵向测量与滚动，不塞入固定行高而被裁掉。
		VoiceInputMode->SetIsEnabled(false);
		Microphone->SetIsEnabled(false);
		VoiceInputModeUnavailable->SetColorAndOpacity(FLinearColor(0.60f, 0.67f, 0.65f));
		MicrophoneUnavailable->SetColorAndOpacity(FLinearColor(0.60f, 0.67f, 0.65f));
		SetBoxSlot(VoiceInputModeUnavailable, false, FMargin(12.0f, 0.0f, 12.0f, 8.0f));
		SetBoxSlot(MicrophoneUnavailable, false, FMargin(12.0f, 0.0f, 12.0f, 8.0f));
		AddCheckBox(Tree, AddSettingRow(Tree, GamePanel, TEXT("Vibration"), TEXT("手柄震动")), TEXT("VibrationCheckBox"), TEXT(""));

		AddComboBox(Tree, AddSettingRow(Tree, GraphicsPanel, TEXT("FullscreenMode"), TEXT("窗口模式")), TEXT("FullscreenModeComboBox"), FullscreenModes);
		AddComboBox(Tree, AddSettingRow(Tree, GraphicsPanel, TEXT("ScreenResolution"), TEXT("分辨率")), TEXT("ScreenResolutionComboBox"), Resolutions);
		AddComboBox(Tree, AddSettingRow(Tree, GraphicsPanel, TEXT("OverallQuality"), TEXT("画面质量")), TEXT("OverallQualityComboBox"), QualityLevels);
		AddCheckBox(Tree, AddSettingRow(Tree, GraphicsPanel, TEXT("VSync"), TEXT("垂直同步")), TEXT("VSyncCheckBox"), TEXT(""));
		// 两个初始滑块都用 Root 的归一化映射：Gamma 2.2 位于 0.5..5.0，UI 比例 1.0 位于 0.75..2.0；刷新时仍以真实草稿覆盖。
		AddSlider(Tree, AddSettingRow(Tree, GraphicsPanel, TEXT("Brightness"), TEXT("亮度")), TEXT("BrightnessSlider"), (2.2f - 0.5f) / 4.5f);
		AddSlider(Tree, AddSettingRow(Tree, GraphicsPanel, TEXT("UIScale"), TEXT("界面缩放")), TEXT("UIScaleSlider"), (1.0f - 0.75f) / 1.25f);

		AddSlider(Tree, AddSettingRow(Tree, AudioPanel, TEXT("MasterVolume"), TEXT("主音量")), TEXT("MasterVolumeSlider"), 1.0f);
		AddSlider(Tree, AddSettingRow(Tree, AudioPanel, TEXT("MusicVolume"), TEXT("音乐")), TEXT("MusicVolumeSlider"), 1.0f);
		AddSlider(Tree, AddSettingRow(Tree, AudioPanel, TEXT("SFXVolume"), TEXT("音效")), TEXT("SFXVolumeSlider"), 1.0f);
		AddSlider(Tree, AddSettingRow(Tree, AudioPanel, TEXT("AmbienceVolume"), TEXT("环境音")), TEXT("AmbienceVolumeSlider"), 1.0f);
		AddSlider(Tree, AddSettingRow(Tree, AudioPanel, TEXT("VoiceVolume"), TEXT("语音")), TEXT("VoiceVolumeSlider"), 1.0f);
		AddCheckBox(Tree, AddSettingRow(Tree, AudioPanel, TEXT("MuteAudioWhenUnfocused"), TEXT("失焦时静音")), TEXT("MuteAudioWhenUnfocusedCheckBox"), TEXT(""));
		UComboBoxString* AudioOutputDevice = AddComboBox(Tree, AddSettingRow(Tree, AudioPanel, TEXT("AudioOutputDevice"), TEXT("输出设备")), TEXT("AudioOutputDeviceComboBox"), {});
		if (!AudioOutputDevice)
		{
			return false;
		}
		AudioOutputDevice->SetIsEnabled(false);
		UButton* RefreshDevices = AddButton(Tree, AudioPanel, TEXT("RefreshAudioOutputDevicesButton"), TEXT("刷新输出设备"));
		CastChecked<UVerticalBoxSlot>(RefreshDevices->Slot)->SetHorizontalAlignment(HAlign_Right);

		// 控制分类只保留已有入口与右侧说明，不在作者器中发明按键设置；底部命令独立于滚动区，始终保持同一行。
		AddText(Tree, Column, TEXT("FrontendSettingsResultTextBlock"), TEXT(""), 16);
		UHorizontalBox* Actions = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SettingsActions"));
		Column->AddChild(Actions);
		SetBoxSlot(Actions, false, FMargin(0.0f, 12.0f, 0.0f, 0.0f));
		SetBoxSlot(AddButton(Tree, Actions, TEXT("ApplySettingsButton"), TEXT("应用")), false, FMargin(0.0f, 0.0f, 12.0f, 0.0f));
		SetBoxSlot(AddButton(Tree, Actions, TEXT("RestoreSettingsDefaultsButton"), TEXT("恢复默认")), false);
		USpacer* ActionSpace = Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("SettingsActionsSpace"));
		Actions->AddChild(ActionSpace);
		SetBoxSlot(ActionSpace, true);
		SetBoxSlot(AddButton(Tree, Actions, TEXT("CancelSettingsButton"), TEXT("取消")), false);
		return true;
	}

	/** 构造全局 Loading 遮罩的真实状态展示位；资产只提供 View 控件，阶段和地图包百分比由 LocalPlayer UI 从 Online 快照写入。 */
	bool BuildLoadingWidget(UWidgetBlueprint* WidgetBlueprint)
	{
		// 加载遮罩布局流程：检查页面根，保留上方标题和背景空间，再把阶段文本、提示与细条形控件排列在底部；初始值只是占位，运行时以 Online 快照为准。
		// 条形控件默认确定进度，由 LocalPlayer UI 在 Online 没有真实包百分比时才切成 marquee；资产本身不创建计时器、不伪造百分比。
		UVerticalBox* Column = CreatePageColumn(WidgetBlueprint, TEXT("LoadingRoot"));
		if (!Column)
		{
			return false;
		}
		UWidgetTree* Tree = WidgetBlueprint->WidgetTree;
		AddText(Tree, Column, TEXT("LoadingTitleText"), TEXT("正在加载"), 30);
		USpacer* Space = Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("LoadingOpenSpace"));
		Column->AddChild(Space);
		SetBoxSlot(Space, true);
		AddText(Tree, Column, TEXT("LoadingDayTextBlock"), TEXT("正在切换世界"));
		AddText(Tree, Column, TEXT("LoadingDetailTextBlock"), TEXT("请稍候"));
		AddText(Tree, Column, TEXT("LoadingHintText"), TEXT("旅程即将开始"), 16);
		AddText(Tree, Column, TEXT("LoadingProgressTextBlock"), TEXT("正在加载。"));
		USizeBox* ProgressBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LoadingProgressBounds"));
		ProgressBounds->SetHeightOverride(12.0f);
		UProgressBar* ProgressBar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("LoadingProgressBar"));
		FProgressBarStyle Style = ProgressBar->GetWidgetStyle();
		Style.SetBackgroundImage(FSlateColorBrush(FLinearColor(0.12f, 0.15f, 0.14f)));
		Style.SetFillImage(FSlateColorBrush(FLinearColor::White));
		ProgressBar->SetWidgetStyle(Style);
		ProgressBar->SetFillColorAndOpacity(FLinearColor(0.08f, 0.67f, 0.52f));
		ProgressBar->SetIsMarquee(false);
		ProgressBar->SetPercent(0.0f);
		ProgressBounds->SetContent(ProgressBar);
		Column->AddChild(ProgressBounds);
		SetBoxSlot(ProgressBounds, false, FMargin(0.0f, 12.0f, 0.0f, 8.0f));
		ExposeWidget(WidgetBlueprint, ProgressBar);
		return true;
	}

	/** 为动态列表创建紧凑行；固定高度只服务行内容，宽度由父列表分配，不携带页面遮罩或缩放。 */
	UHorizontalBox* CreateCompactRow(UWidgetBlueprint* WidgetBlueprint, const TCHAR* RowRootName)
	{
		// 紧凑行流程：验证工厂提供的 SizeBox 根，写入 72 个设计像素的高度，然后加入带内边距的深色横行；根类型不符时中止该资产创建。
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			return nullptr;
		}
		USizeBox* RowBounds = Cast<USizeBox>(WidgetBlueprint->WidgetTree->RootWidget);
		if (!RowBounds)
		{
			return nullptr;
		}
		RowBounds->SetHeightOverride(72.0f);
		UBorder* RowBackground = WidgetBlueprint->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
			FName(FString::Printf(TEXT("%sBackground"), RowRootName)));
		UHorizontalBox* RowContent = WidgetBlueprint->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(RowRootName));
		RowBackground->SetBrush(FSlateColorBrush(FLinearColor(0.045f, 0.055f, 0.051f, 0.86f)));
		RowBackground->SetPadding(FMargin(16.0f, 8.0f));
		RowBackground->SetHorizontalAlignment(HAlign_Fill);
		RowBackground->SetVerticalAlignment(VAlign_Fill);
		RowBackground->SetContent(RowContent);
		RowBounds->SetContent(RowBackground);
		RowBounds->SetClipping(EWidgetClipping::ClipToBounds);
		return RowContent;
	}

	/** 构造存档行模板；名称和元数据共享左侧弹性空间，右侧保留独立的选择按钮。 */
	bool BuildSaveSlotRowWidget(UWidgetBlueprint* WidgetBlueprint)
	{
		// 存档行流程：确认紧凑根，把名称与摘要排成两行并分配剩余宽度，最后加入自动宽度按钮；长存档名省略而不把按钮推到视口外。
		UHorizontalBox* Row = CreateCompactRow(WidgetBlueprint, TEXT("SaveSlotRowRoot"));
		if (!Row)
		{
			return false;
		}
		UWidgetTree* Tree = WidgetBlueprint->WidgetTree;
		UVerticalBox* TextColumn = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SaveSlotTextColumn"));
		Row->AddChild(TextColumn);
		SetBoxSlot(TextColumn, true, FMargin(0.0f, 0.0f, 16.0f, 0.0f));
		UTextBlock* Name = AddText(Tree, TextColumn, TEXT("SaveSlotNameText"), TEXT("存档名称"));
		UTextBlock* Meta = AddText(Tree, TextColumn, TEXT("SaveSlotMetaText"), TEXT(""), 14);
		Name->SetAutoWrapText(false);
		Meta->SetAutoWrapText(false);
		Meta->SetColorAndOpacity(FLinearColor(0.60f, 0.67f, 0.65f));
		SetBoxSlot(Name, false);
		SetBoxSlot(Meta, false, FMargin(0.0f, 4.0f, 0.0f, 0.0f));
		SetBoxSlot(AddButton(Tree, Row, TEXT("SelectSaveSlotButton"), TEXT("选择")), false);
		return true;
	}

	/** 构造好友行模板；名称与在线状态分层显示，邀请仍是唯一行命令。 */
	bool BuildRoomFriendRowWidget(UWidgetBlueprint* WidgetBlueprint)
	{
		// 好友行流程：在 72px 行内建立可收缩的两行文本与自动宽度邀请按钮；名字不会参与计算整页尺寸，句柄和邀请逻辑仍属于原生父类。
		UHorizontalBox* Row = CreateCompactRow(WidgetBlueprint, TEXT("RoomFriendRowRoot"));
		if (!Row)
		{
			return false;
		}
		UWidgetTree* Tree = WidgetBlueprint->WidgetTree;
		UVerticalBox* TextColumn = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FriendTextColumn"));
		Row->AddChild(TextColumn);
		SetBoxSlot(TextColumn, true, FMargin(0.0f, 0.0f, 12.0f, 0.0f));
		UTextBlock* Name = AddText(Tree, TextColumn, TEXT("FriendNameText"), TEXT("好友"));
		UTextBlock* Status = AddText(Tree, TextColumn, TEXT("FriendStatusText"), TEXT(""), 14);
		Name->SetAutoWrapText(false);
		Status->SetAutoWrapText(false);
		Status->SetColorAndOpacity(FLinearColor(0.60f, 0.67f, 0.65f));
		SetBoxSlot(Name, false);
		SetBoxSlot(Status, false, FMargin(0.0f, 4.0f, 0.0f, 0.0f));
		SetBoxSlot(AddButton(Tree, Row, TEXT("InviteFriendButton"), TEXT("邀请")), false);
		return true;
	}

	/** 构造成员槽模板；左侧区分玩家名与身份，右侧只显示 RoomModel 提供的成员状态。 */
	bool BuildRoomPlayerSlotWidget(UWidgetBlueprint* WidgetBlueprint)
	{
		// 成员槽流程：验证行根，建立可收缩的玩家信息列，再添加右对齐的状态文本；作者器不生成准备状态或示例人数。
		UHorizontalBox* Row = CreateCompactRow(WidgetBlueprint, TEXT("RoomPlayerSlotRoot"));
		if (!Row)
		{
			return false;
		}
		UWidgetTree* Tree = WidgetBlueprint->WidgetTree;
		UVerticalBox* TextColumn = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PlayerTextColumn"));
		Row->AddChild(TextColumn);
		SetBoxSlot(TextColumn, true, FMargin(0.0f, 0.0f, 12.0f, 0.0f));
		UTextBlock* Name = AddText(Tree, TextColumn, TEXT("PlayerNameText"), TEXT("玩家"));
		UTextBlock* Role = AddText(Tree, TextColumn, TEXT("PlayerRoleText"), TEXT(""), 14);
		UTextBlock* State = AddText(Tree, Row, TEXT("PlayerSlotStateText"), TEXT(""), 14);
		Name->SetAutoWrapText(false);
		Role->SetAutoWrapText(false);
		State->SetAutoWrapText(false);
		State->SetJustification(ETextJustify::Right);
		Role->SetColorAndOpacity(FLinearColor(0.60f, 0.67f, 0.65f));
		SetBoxSlot(Name, false);
		SetBoxSlot(Role, false, FMargin(0.0f, 4.0f, 0.0f, 0.0f));
		SetBoxSlot(State, false);
		return true;
	}

	/** 从已编译的子 WBP 读取生成类；根 WBP 仅在九件资产均有效时装配，避免留下无法实例化的页面占位。 */
	TSubclassOf<UUserWidget> LoadChildWidgetClass(const TCHAR* AssetName)
	{
		// 子页面类解析流程：根据固定正式包路径加载 GeneratedClass；加载失败返回空，由根布局创建失败并保留可定位日志。
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s_C"), *WidgetDirectory, AssetName, AssetName);
		return LoadClass<UUserWidget>(nullptr, *ObjectPath);
	}

	/** 构造根 WBP；静态与动态背景位并存，所有页面经过一个 Switcher 切换，视觉布局不参与流程判断。 */
	bool BuildRootWidget(UWidgetBlueprint* WidgetBlueprint)
	{
		// 根布局流程：先保留双背景层与透明遮罩，再建立缩放后的页面 Switcher，最后装入五个已编译子 WBP 并暴露 BindWidget 合同。
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			return false;
		}
		UCanvasPanel* Canvas = Cast<UCanvasPanel>(WidgetBlueprint->WidgetTree->RootWidget);
		if (!Canvas)
		{
			return false;
		}

		UImage* StaticBackground = WidgetBlueprint->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("StaticBackgroundImage"));
		UOverlay* DynamicBackground = WidgetBlueprint->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("DynamicBackgroundContainer"));
		UBorder* VisualShade = WidgetBlueprint->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FrontendVisualShade"));
		UScaleBox* PageScale = WidgetBlueprint->WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("FrontendPageScale"));
		USizeBox* PageBounds = WidgetBlueprint->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FrontendPageBounds"));
		UWidgetSwitcher* PageSwitcher = WidgetBlueprint->WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("FrontendPageSwitcher"));
		if (!StaticBackground || !DynamicBackground || !VisualShade || !PageScale || !PageBounds || !PageSwitcher)
		{
			return false;
		}
		VisualShade->SetBrushColor(FLinearColor(0.008f, 0.012f, 0.025f, 0.65f));
		PageScale->SetStretch(EStretch::ScaleToFit);
		PageBounds->SetMinDesiredWidth(760.0f);
		PageBounds->SetMinDesiredHeight(500.0f);
		PageScale->SetContent(PageBounds);
		PageBounds->SetContent(PageSwitcher);
		AddFullCanvasChild(Canvas, StaticBackground, 0);
		AddFullCanvasChild(Canvas, DynamicBackground, 1);
		AddFullCanvasChild(Canvas, VisualShade, 2);
		AddFullCanvasChild(Canvas, PageScale, 3);
		ExposeWidget(WidgetBlueprint, StaticBackground);
		ExposeWidget(WidgetBlueprint, DynamicBackground);
		ExposeWidget(WidgetBlueprint, PageSwitcher);

		const TSubclassOf<UUserWidget> MenuClass = LoadChildWidgetClass(TEXT("WBP_CatFrontendMenu"));
		const TSubclassOf<UUserWidget> SaveListClass = LoadChildWidgetClass(TEXT("WBP_CatFrontendSaveList"));
		const TSubclassOf<UUserWidget> RoomClass = LoadChildWidgetClass(TEXT("WBP_CatFrontendRoom"));
		const TSubclassOf<UUserWidget> SettingsClass = LoadChildWidgetClass(TEXT("WBP_CatFrontendSettings"));
		if (!MenuClass || !SaveListClass || !RoomClass || !SettingsClass)
		{
			return false;
		}

		UUserWidget* MenuPage = WidgetBlueprint->WidgetTree->ConstructWidget<UUserWidget>(MenuClass, TEXT("MenuPage"));
		UUserWidget* SaveListPage = WidgetBlueprint->WidgetTree->ConstructWidget<UUserWidget>(SaveListClass, TEXT("SaveListPage"));
		UUserWidget* RoomPage = WidgetBlueprint->WidgetTree->ConstructWidget<UUserWidget>(RoomClass, TEXT("RoomPage"));
		UUserWidget* FrontendSettingsPage = WidgetBlueprint->WidgetTree->ConstructWidget<UUserWidget>(SettingsClass, TEXT("FrontendSettingsPage"));
		if (!MenuPage || !SaveListPage || !RoomPage || !FrontendSettingsPage)
		{
			return false;
		}
		PageSwitcher->AddChild(MenuPage);
		PageSwitcher->AddChild(SaveListPage);
		PageSwitcher->AddChild(RoomPage);
		PageSwitcher->AddChild(FrontendSettingsPage);
		ExposeWidget(WidgetBlueprint, MenuPage);
		ExposeWidget(WidgetBlueprint, SaveListPage);
		ExposeWidget(WidgetBlueprint, RoomPage);
		ExposeWidget(WidgetBlueprint, FrontendSettingsPage);
		return true;
	}

	/** 同步 WBP 当前源控件的变量 GUID 表；重建资产会替换整棵 WidgetTree，这一步负责移除不存在的控件登记并补齐新树控件。 */
	void SyncWidgetVariableGuids(UWidgetBlueprint* WidgetBlueprint)
	{
		// GUID 同步流程：收集当前 WidgetTree 下的源控件名，移除未出现在当前树里的登记，再为缺少 GUID 的当前控件补登记。
		if (!WidgetBlueprint)
		{
			return;
		}

		TSet<FName> SourceWidgetNames;
		WidgetBlueprint->ForEachSourceWidget([&SourceWidgetNames](UWidget* Widget)
		{
			const FName WidgetName = Widget->GetFName();
			SourceWidgetNames.Add(WidgetName);
		});
		for (auto It = WidgetBlueprint->WidgetVariableNameToGuidMap.CreateIterator(); It; ++It)
		{
			if (!SourceWidgetNames.Contains(It.Key()))
			{
				It.RemoveCurrent();
			}
		}
		// UE 编译器在 GUID 表非空时检查全部源控件，包括非变量布局容器；只同步当前 WidgetTree，避免重建资产时把不存在的源控件带进编译。
		for (const FName WidgetName : SourceWidgetNames)
		{
			if (!WidgetBlueprint->WidgetVariableNameToGuidMap.Contains(WidgetName))
			{
				WidgetBlueprint->OnVariableAdded(WidgetName);
			}
		}
	}

	/** 为新建、修复或重建的 WBP 执行结构刷新、编译和保存；只有全新资产才额外通知资产注册表。 */
	bool CompileRegisterAndSaveWidget(UWidgetBlueprint* WidgetBlueprint, const bool bRegisterNewAsset)
	{
		// WBP 保存流程：空蓝图直接失败；先给 UE 内部结构刷新准备好 GUID，再标记结构变化，随后按刷新后的源控件再次同步并显式编译保存。
		if (!WidgetBlueprint)
		{
			return false;
		}
		SyncWidgetVariableGuids(WidgetBlueprint);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
		SyncWidgetVariableGuids(WidgetBlueprint);
		FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
		if (WidgetBlueprint->Status == BS_Error)
		{
			return false;
		}
		WidgetBlueprint->MarkPackageDirty();
		if (bRegisterNewAsset)
		{
			FAssetRegistryModule::AssetCreated(WidgetBlueprint);
		}
		const FString Filename = FPackageName::LongPackageNameToFilename(WidgetBlueprint->GetOutermost()->GetName(),
			FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(WidgetBlueprint->GetOutermost(), WidgetBlueprint, *Filename, SaveArgs);
	}

	/** 在指定内容目录创建一个尚不存在的 WBP；已存在资产直接返回成功，确保重复执行不会破坏美术、蓝图事件或人工布局。 */
	bool CreateMissingWidgetInDirectory(const FString& Directory, const TCHAR* AssetName, TSubclassOf<UUserWidget> ParentClass,
		TSubclassOf<UWidget> RootWidgetClass, const TCHAR* AuthoringTag, TFunctionRef<bool(UWidgetBlueprint*)> BuildWidget)
	{
		// WBP 创建流程：先检查指定目录下的同名正式资产，缺失时按页面、行或菜单根类型创建 Blueprint、拼装首份结构、编译保存；任一步失败都记录路径并返回 false。
		const FString PackageName = FString::Printf(TEXT("%s/%s"), *Directory, AssetName);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, AssetName);
		if (LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath))
		{
			return true;
		}
		UPackage* Package = CreatePackage(*PackageName);
		UWidgetBlueprint* WidgetBlueprint = FWidgetBlueprintOperationUtils::CreateWidgetBlueprint(Package, FName(AssetName), BPTYPE_Normal,
			ParentClass, RootWidgetClass, FName(AuthoringTag), false);
		if (!WidgetBlueprint || !BuildWidget(WidgetBlueprint) || !CompileRegisterAndSaveWidget(WidgetBlueprint, true))
		{
			UE_LOG(LogTemp, Error, TEXT("Event=ui_widget_authoring_failed Tag=%s Asset=%s"), AuthoringTag, *PackageName);
			return false;
		}
		UE_LOG(LogTemp, Display, TEXT("Event=ui_widget_authoring_created Tag=%s Asset=%s"), AuthoringTag, *PackageName);
		return true;
	}

	/** 重建指定目录中的单个正式 WBP；当前用于局内菜单资产升级和合同重刷。 */
	bool RebuildWidgetInDirectory(const FString& Directory, const TCHAR* AssetName, TSubclassOf<UUserWidget> ParentClass,
		TSubclassOf<UWidget> RootWidgetClass, const TCHAR* AuthoringTag, TFunctionRef<bool(UWidgetBlueprint*)> BuildWidget)
	{
		// WBP 重建流程：缺失时沿用创建路径；存在时确认父类合同，先同步当前控件的变量 GUID，再换入新的 WidgetTree 根并重新构造、编译和保存。
		const FString PackageName = FString::Printf(TEXT("%s/%s"), *Directory, AssetName);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, AssetName);
		UWidgetBlueprint* WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
		if (!WidgetBlueprint)
		{
			return CreateMissingWidgetInDirectory(Directory, AssetName, ParentClass, RootWidgetClass, AuthoringTag, BuildWidget);
		}
		if (!WidgetBlueprint->GeneratedClass || !WidgetBlueprint->GeneratedClass->IsChildOf(ParentClass))
		{
			UE_LOG(LogTemp, Error, TEXT("Event=ui_widget_rebuild_parent_mismatch Tag=%s Asset=%s ExpectedParent=%s"),
				AuthoringTag, *ObjectPath, ParentClass ? *ParentClass->GetName() : TEXT("None"));
			return false;
		}
		WidgetBlueprint->Modify();
		WidgetBlueprint->WidgetVariableNameToGuidMap.Reset();
		WidgetBlueprint->WidgetTree = NewObject<UWidgetTree>(WidgetBlueprint, TEXT("WidgetTree"), RF_Transactional);
		if (!WidgetBlueprint->WidgetTree)
		{
			UE_LOG(LogTemp, Error, TEXT("Event=ui_widget_rebuild_tree_failed Tag=%s Asset=%s"), AuthoringTag, *ObjectPath);
			return false;
		}
		if (RootWidgetClass)
		{
			const FName RootWidgetName(*FString::Printf(TEXT("%sRoot"), AssetName));
			WidgetBlueprint->WidgetTree->RootWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UWidget>(RootWidgetClass, RootWidgetName);
			if (WidgetBlueprint->WidgetTree->RootWidget)
			{
				WidgetBlueprint->OnVariableAdded(WidgetBlueprint->WidgetTree->RootWidget->GetFName());
			}
		}
		if (!WidgetBlueprint->WidgetTree->RootWidget || !BuildWidget(WidgetBlueprint)
			|| !CompileRegisterAndSaveWidget(WidgetBlueprint, false))
		{
			UE_LOG(LogTemp, Error, TEXT("Event=ui_widget_rebuild_failed Tag=%s Asset=%s"), AuthoringTag, *PackageName);
			return false;
		}
		UE_LOG(LogTemp, Display, TEXT("Event=ui_widget_rebuilt Tag=%s Asset=%s"), AuthoringTag, *PackageName);
		return true;
	}

	/** 创建一个 Frontend 目录下尚不存在的 WBP；它把正式入口固定在 Frontend 资产目录，避免运行时再创建原生替身。 */
	bool CreateMissingWidget(const TCHAR* AssetName, TSubclassOf<UUserWidget> ParentClass, TSubclassOf<UWidget> RootWidgetClass,
		TFunctionRef<bool(UWidgetBlueprint*)> BuildWidget)
	{
		// 前端 WBP 创建流程：固定使用 Frontend 资产目录和作者标签，确保九个正式资产的包路径不因局内菜单作者入口而变化。
		return CreateMissingWidgetInDirectory(WidgetDirectory, AssetName, ParentClass, RootWidgetClass,
			TEXT("CatFrontendWidgetAuthoring"), BuildWidget);
	}

	/** 修复一个既有正式前端 WBP 的字体引用；加载成功后统一编译保存，Root 这类无直接文本的资产也会保留最新编译状态。 */
	bool RepairWidgetFonts(const TCHAR* AssetName)
	{
		// 既有 WBP 字体修复流程：加载正式资产，遍历源控件并只替换文本相关字体；编译保存后保留原布局、变量名和蓝图事件。
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *WidgetDirectory, AssetName, AssetName);
		UWidgetBlueprint* WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			UE_LOG(LogTemp, Error, TEXT("Event=frontend_widget_font_repair_missing_asset Asset=%s"), *ObjectPath);
			return false;
		}
		int32 ChangedFontCount = 0;
		WidgetBlueprint->ForEachSourceWidget([&ChangedFontCount](UWidget* Widget)
		{
			ApplyFrontendFontToWidget(Widget, ChangedFontCount);
		});
		if (!CompileRegisterAndSaveWidget(WidgetBlueprint, false))
		{
			UE_LOG(LogTemp, Error, TEXT("Event=frontend_widget_font_repair_save_failed Asset=%s"), *ObjectPath);
			return false;
		}
		UE_LOG(LogTemp, Display, TEXT("Event=frontend_widget_font_repaired Asset=%s Count=%d Font=%s"),
			*ObjectPath, ChangedFontCount, FrontendChineseFontPath);
		return true;
	}

	/** 修复九个正式 Frontend WBP 的中文字体引用；列表与创建入口保持一致，避免漏掉动态行控件。 */
	bool RepairFrontendWidgetFonts()
	{
		// 批量字体修复流程：先确认中文 Font 资产可加载，再逐一修复 Root、页面和动态行 WBP；任一失败都会阻止脚本报成功。
		if (!LoadFrontendChineseFont())
		{
			return false;
		}
		const TCHAR* const AssetNames[] = {
			TEXT("WBP_CatFrontendMenu"),
			TEXT("WBP_CatFrontendSaveList"),
			TEXT("WBP_CatFrontendRoom"),
			TEXT("WBP_CatFrontendSettings"),
			TEXT("WBP_CatFrontendLoading"),
			TEXT("WBP_CatSaveSlotRow"),
			TEXT("WBP_CatRoomFriendRow"),
			TEXT("WBP_CatRoomPlayerSlot"),
			TEXT("WBP_CatFrontendRoot")
		};
		for (const TCHAR* AssetName : AssetNames)
		{
			if (!RepairWidgetFonts(AssetName))
			{
				return false;
			}
		}
		return true;
	}

	/** 核验一个既有正式前端 WBP 的全部文本类控件字体；Root 允许没有文本控件，其余页面会在控件缺失时由合同核验暴露。 */
	bool ValidateWidgetFonts(const TCHAR* AssetName, const UObject* ExpectedFont)
	{
		// 既有 WBP 字体核验流程：加载正式资产后只读遍历源控件，统计文本类控件并检查 FontObject；本方法不标脏、不编译、不保存。
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *WidgetDirectory, AssetName, AssetName);
		UWidgetBlueprint* WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			UE_LOG(LogTemp, Error, TEXT("Event=frontend_widget_font_validation_missing_asset Asset=%s"), *ObjectPath);
			return false;
		}
		bool bAllFontsValid = true;
		int32 CheckedFontCount = 0;
		WidgetBlueprint->ForEachSourceWidget([AssetName, ExpectedFont, &bAllFontsValid, &CheckedFontCount](UWidget* Widget)
		{
			bAllFontsValid &= ValidateFrontendFontOnWidget(AssetName, Widget, ExpectedFont, CheckedFontCount);
		});
		UE_LOG(LogTemp, Display, TEXT("Event=frontend_widget_font_validated Asset=%s Count=%d Font=%s Result=%s"),
			*ObjectPath, CheckedFontCount, FrontendChineseFontPath, bAllFontsValid ? TEXT("Pass") : TEXT("Fail"));
		return bAllFontsValid;
	}

	/** 核验九个正式 Frontend WBP 的文本字体对象；它与修复列表同源，防止新行 WBP 被遗漏。 */
	bool ValidateFrontendWidgetFonts()
	{
		// 批量字体核验流程：先加载正式中文 Font，再逐一只读检查 Root、页面和动态行 WBP；任一字体不匹配都会让脚本失败。
		const UObject* ExpectedFont = LoadFrontendChineseFont();
		if (!ExpectedFont)
		{
			return false;
		}
		const TCHAR* const AssetNames[] = {
			TEXT("WBP_CatFrontendMenu"),
			TEXT("WBP_CatFrontendSaveList"),
			TEXT("WBP_CatFrontendRoom"),
			TEXT("WBP_CatFrontendSettings"),
			TEXT("WBP_CatFrontendLoading"),
			TEXT("WBP_CatSaveSlotRow"),
			TEXT("WBP_CatRoomFriendRow"),
			TEXT("WBP_CatRoomPlayerSlot"),
			TEXT("WBP_CatFrontendRoot")
		};
		for (const TCHAR* AssetName : AssetNames)
		{
			if (!ValidateWidgetFonts(AssetName, ExpectedFont))
			{
				return false;
			}
		}
		return true;
	}

	/** 将一个新建资源包保存到固定内容路径；声音资产先全部创建再连父子关系，确保失败不会误把半套树说成完整。 */
	bool SaveNewAsset(UObject* Asset)
	{
		// 通用资产保存流程：标记所属包、登记新对象并按挂载路径保存；调用者仅传入本工具刚创建的正式音频资产。
		if (!Asset)
		{
			return false;
		}
		Asset->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Asset);
		const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),
			FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, SaveArgs);
	}

	/** 检查六个声音设置资源是否完全缺失或完全存在；部分存在意味着手工编辑或中断生成，不能由本工具猜测性覆盖。 */
	bool HasCompleteOrEmptyAudioAssetSet(bool& bOutComplete)
	{
		// 音频集检查流程：逐一探测固定 SoundMix/SoundClass 对象，统计存在数量；数量为零允许首次生成，数量为六表示保持人工资源，其余状态直接拒绝。
		const TCHAR* const AssetNames[] = {
			TEXT("SMX_CatFrontendSettings"), TEXT("SC_CatMaster"), TEXT("SC_CatMusic"), TEXT("SC_CatSFX"), TEXT("SC_CatAmbience"), TEXT("SC_CatVoice")
		};
		int32 ExistingCount = 0;
		for (const TCHAR* AssetName : AssetNames)
		{
			const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *AudioDirectory, AssetName, AssetName);
			ExistingCount += StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath) ? 1 : 0;
		}
		bOutComplete = ExistingCount == UE_ARRAY_COUNT(AssetNames);
		return ExistingCount == 0 || bOutComplete;
	}

	/** 核验指定目录下已创建 WBP 的核心控件合同；只读检查 Designer 树，绝不对已有资产补控件或重排布局。 */
	bool ValidateWidgetContractInDirectory(const FString& Directory, const TCHAR* AssetName,
		TConstArrayView<FRequiredWidgetControl> RequiredControls)
	{
		// WBP 合同核验流程：加载指定目录内的正式资产与其 WidgetTree，逐项按名称查找并核对类型；任一项缺失都会给出资产、控件和期望类型。
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Directory, AssetName, AssetName);
		UWidgetBlueprint* WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			UE_LOG(LogTemp, Error, TEXT("Event=frontend_widget_contract_missing_asset Asset=%s"), *ObjectPath);
			return false;
		}
		for (const FRequiredWidgetControl& Required : RequiredControls)
		{
			UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(Required.Name);
			if (!Widget || !Required.RequiredClass || !Widget->IsA(Required.RequiredClass) || !Widget->bIsVariable)
			{
				UE_LOG(LogTemp, Error, TEXT("Event=frontend_widget_contract_missing_control Asset=%s Control=%s ExpectedClass=%s DesignerVariable=%s"),
					*ObjectPath, *Required.Name.ToString(), Required.RequiredClass ? *Required.RequiredClass->GetName() : TEXT("None"),
					Widget && Widget->bIsVariable ? TEXT("true") : TEXT("false"));
				return false;
			}
		}
		return true;
	}

	/** 核验一个已创建前端 WBP 的核心控件合同；固定读取 Frontend 目录，保持既有九资产合同入口不变。 */
	bool ValidateWidgetContract(const TCHAR* AssetName, TConstArrayView<FRequiredWidgetControl> RequiredControls)
	{
		// 前端 WBP 合同核验流程：保持 Frontend 目录的合同核验，不因局内菜单新增而改变已有资产查找路径。
		return ValidateWidgetContractInDirectory(WidgetDirectory, AssetName, RequiredControls);
	}

	/** 核验指定目录下 WBP 的 C++ 父类合同；动态列表行或局内菜单必须继承对应 View，不能因同名控件齐全而丢失稳定回调。 */
	bool ValidateWidgetParentInDirectory(const FString& Directory, const TCHAR* AssetName, UClass* RequiredParentClass)
	{
		// 父类核验流程：加载正式 WBP 的生成类，确认其仍继承指定 C++ View 基类；已有资产不被重建，但错误继承会阻止作者脚本报成功。
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Directory, AssetName, AssetName);
		UWidgetBlueprint* WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
		if (!WidgetBlueprint || !WidgetBlueprint->GeneratedClass || !RequiredParentClass
			|| !WidgetBlueprint->GeneratedClass->IsChildOf(RequiredParentClass))
		{
			UE_LOG(LogTemp, Error, TEXT("Event=frontend_widget_contract_parent_mismatch Asset=%s ExpectedParent=%s"),
				*ObjectPath, RequiredParentClass ? *RequiredParentClass->GetName() : TEXT("None"));
			return false;
		}
		return true;
	}

	/** 核验一个 Frontend 目录下 WBP 的原生父类合同；动态列表行继续走 Frontend 目录和当前事件口径。 */
	bool ValidateWidgetParent(const TCHAR* AssetName, UClass* RequiredParentClass)
	{
		// 前端父类合同核验流程：固定使用 Frontend 目录，确保列表行和 Root 的原生类型检查不被局内菜单路径影响。
		return ValidateWidgetParentInDirectory(WidgetDirectory, AssetName, RequiredParentClass);
	}

	/** 核验正式局内菜单 WBP 的父类与命名控件；它证明 C++ View 只绑定项目资产，不创建 C++ 菜单替身。 */
	bool ValidateLakeMainMenuWidgetContract()
	{
		// 局内菜单合同核验流程：先检查 WBP 继承 UCatLakeMainMenuWidget，再核对命令页和设置页同名控件；任一项缺失都会让资产脚本失败。
		const FRequiredWidgetControl LakeMenuControls[] = {
			{ TEXT("LakeMainMenuPageSwitcher"), UWidgetSwitcher::StaticClass() },
			{ TEXT("LakeCommandPanel"), UPanelWidget::StaticClass() },
			{ TEXT("LakeSettingsPanel"), UPanelWidget::StaticClass() },
			{ TEXT("CloseButton"), UButton::StaticClass() },
			{ TEXT("SettingsButton"), UButton::StaticClass() },
			{ TEXT("SaveButton"), UButton::StaticClass() },
			{ TEXT("ReturnToMainMenuButton"), UButton::StaticClass() },
			{ TEXT("ExitGameButton"), UButton::StaticClass() },
			{ TEXT("StatusTextBlock"), UTextBlock::StaticClass() },
			{ TEXT("GameSettingsCategoryButton"), UButton::StaticClass() },
			{ TEXT("GraphicsSettingsCategoryButton"), UButton::StaticClass() },
			{ TEXT("AudioSettingsCategoryButton"), UButton::StaticClass() },
			{ TEXT("ControlsSettingsCategoryButton"), UButton::StaticClass() },
			{ TEXT("ApplySettingsButton"), UButton::StaticClass() },
			{ TEXT("RestoreSettingsDefaultsButton"), UButton::StaticClass() },
			{ TEXT("CancelSettingsButton"), UButton::StaticClass() },
			{ TEXT("FrontendSettingsResultTextBlock"), UTextBlock::StaticClass() },
			{ TEXT("SettingsDescriptionTextBlock"), UTextBlock::StaticClass() },
			{ TEXT("GameSettingsPanel"), UPanelWidget::StaticClass() },
			{ TEXT("GraphicsSettingsPanel"), UPanelWidget::StaticClass() },
			{ TEXT("AudioSettingsPanel"), UPanelWidget::StaticClass() },
			{ TEXT("ControlsSettingsPanel"), UPanelWidget::StaticClass() },
			{ TEXT("LanguageComboBox"), UComboBoxString::StaticClass() },
			{ TEXT("FullscreenModeComboBox"), UComboBoxString::StaticClass() },
			{ TEXT("ScreenResolutionComboBox"), UComboBoxString::StaticClass() },
			{ TEXT("OverallQualityComboBox"), UComboBoxString::StaticClass() },
			{ TEXT("VSyncCheckBox"), UCheckBox::StaticClass() },
			{ TEXT("BrightnessSlider"), USlider::StaticClass() },
			{ TEXT("VibrationCheckBox"), UCheckBox::StaticClass() },
			{ TEXT("VoiceChatCheckBox"), UCheckBox::StaticClass() },
			{ TEXT("MuteAudioWhenUnfocusedCheckBox"), UCheckBox::StaticClass() },
			{ TEXT("AudioOutputDeviceComboBox"), UComboBoxString::StaticClass() },
			{ TEXT("RefreshAudioOutputDevicesButton"), UButton::StaticClass() },
			{ TEXT("VoiceInputModeComboBox"), UComboBoxString::StaticClass() },
			{ TEXT("MicrophoneComboBox"), UComboBoxString::StaticClass() },
			{ TEXT("VoiceInputModeUnavailableText"), UTextBlock::StaticClass() },
			{ TEXT("MicrophoneUnavailableText"), UTextBlock::StaticClass() },
			{ TEXT("UIScaleSlider"), USlider::StaticClass() },
			{ TEXT("MasterVolumeSlider"), USlider::StaticClass() },
			{ TEXT("MusicVolumeSlider"), USlider::StaticClass() },
			{ TEXT("SFXVolumeSlider"), USlider::StaticClass() },
			{ TEXT("AmbienceVolumeSlider"), USlider::StaticClass() },
			{ TEXT("VoiceVolumeSlider"), USlider::StaticClass() }
		};
		return ValidateWidgetParentInDirectory(LakeMenuWidgetDirectory, TEXT("WBP_CatLakeMainMenu"),
				UCatLakeMainMenuWidget::StaticClass())
			&& ValidateWidgetContractInDirectory(LakeMenuWidgetDirectory, TEXT("WBP_CatLakeMainMenu"),
				LakeMenuControls);
	}

	/** 核验九个正式 WBP 的 Root、全局 Loading 与子树接线点；它证明对象树可供原生代码解析，但不替代 Editor 中的运行期交互验收。 */
	bool ValidateFrontendWidgetContracts()
	{
		// 整体合同核验流程：逐页检查具名控件的类型和变量标记，再检查 Root、全局 Loading 与三类行的原生父类；设置包含真实输入、设备刷新、语音禁用下拉框及原因文本，全部满足才报告成功。
		const FRequiredWidgetControl RootControls[] = {
			{ TEXT("FrontendPageSwitcher"), UWidgetSwitcher::StaticClass() },
			{ TEXT("MenuPage"), UUserWidget::StaticClass() },
			{ TEXT("SaveListPage"), UUserWidget::StaticClass() },
			{ TEXT("RoomPage"), UUserWidget::StaticClass() },
			{ TEXT("FrontendSettingsPage"), UUserWidget::StaticClass() }
		};
		const FRequiredWidgetControl MenuControls[] = {
			{ TEXT("StartGameButton"), UButton::StaticClass() },
			{ TEXT("JoinPartyButton"), UButton::StaticClass() },
			{ TEXT("FrontendSettingsButton"), UButton::StaticClass() },
			{ TEXT("ExitGameButton"), UButton::StaticClass() },
			{ TEXT("ConfirmExitButton"), UButton::StaticClass() },
			{ TEXT("CancelExitButton"), UButton::StaticClass() }
		};
		const FRequiredWidgetControl SettingsControls[] = {
			{ TEXT("GameSettingsCategoryButton"), UButton::StaticClass() },
			{ TEXT("GraphicsSettingsCategoryButton"), UButton::StaticClass() },
			{ TEXT("AudioSettingsCategoryButton"), UButton::StaticClass() },
			{ TEXT("ControlsSettingsCategoryButton"), UButton::StaticClass() },
			{ TEXT("FrontendSettingsResultTextBlock"), UTextBlock::StaticClass() }
		};
		const FRequiredWidgetControl SaveListControls[] = {
			{ TEXT("SaveRowsScrollBox"), UScrollBox::StaticClass() },
			{ TEXT("CreateSaveNameTextBox"), UEditableTextBox::StaticClass() },
			{ TEXT("SaveResultTextBlock"), UTextBlock::StaticClass() },
			{ TEXT("CreateSaveButton"), UButton::StaticClass() },
			{ TEXT("LoadSelectedSaveButton"), UButton::StaticClass() },
			{ TEXT("DeleteSelectedSaveButton"), UButton::StaticClass() },
			{ TEXT("ConfirmDeleteSaveButton"), UButton::StaticClass() },
			{ TEXT("CancelSaveButton"), UButton::StaticClass() },
			{ TEXT("DeleteConfirmationText"), UTextBlock::StaticClass() }
		};
		const FRequiredWidgetControl RoomControls[] = {
			{ TEXT("RoomInviteCodeText"), UTextBlock::StaticClass() },
			{ TEXT("RoomAccessPolicyText"), UTextBlock::StaticClass() },
			{ TEXT("FriendSearchTextBox"), UEditableTextBox::StaticClass() },
			{ TEXT("FriendsScrollBox"), UScrollBox::StaticClass() },
			{ TEXT("PlayersScrollBox"), UScrollBox::StaticClass() },
			{ TEXT("RoomResultTextBlock"), UTextBlock::StaticClass() },
			{ TEXT("RefreshFriendsButton"), UButton::StaticClass() },
			{ TEXT("LeaveRoomButton"), UButton::StaticClass() },
			{ TEXT("StartRoomGameButton"), UButton::StaticClass() },
			{ TEXT("CopyInviteCodeButton"), UButton::StaticClass() }
		};
		const FRequiredWidgetControl LoadingControls[] = {
			{ TEXT("LoadingProgressTextBlock"), UTextBlock::StaticClass() },
			{ TEXT("LoadingHintText"), UTextBlock::StaticClass() },
			{ TEXT("LoadingProgressBar"), UProgressBar::StaticClass() }
		};
		const FRequiredWidgetControl ExpandedSettingsControls[] = {
			{ TEXT("SettingsDescriptionTextBlock"), UTextBlock::StaticClass() },
			{ TEXT("LanguageComboBox"), UComboBoxString::StaticClass() },
			{ TEXT("FullscreenModeComboBox"), UComboBoxString::StaticClass() },
			{ TEXT("ScreenResolutionComboBox"), UComboBoxString::StaticClass() },
			{ TEXT("OverallQualityComboBox"), UComboBoxString::StaticClass() },
			{ TEXT("VSyncCheckBox"), UCheckBox::StaticClass() },
			{ TEXT("BrightnessSlider"), USlider::StaticClass() },
			{ TEXT("VibrationCheckBox"), UCheckBox::StaticClass() },
			{ TEXT("VoiceChatCheckBox"), UCheckBox::StaticClass() },
			{ TEXT("MuteAudioWhenUnfocusedCheckBox"), UCheckBox::StaticClass() },
			{ TEXT("AudioOutputDeviceComboBox"), UComboBoxString::StaticClass() },
			{ TEXT("RefreshAudioOutputDevicesButton"), UButton::StaticClass() },
			{ TEXT("VoiceInputModeComboBox"), UComboBoxString::StaticClass() },
			{ TEXT("MicrophoneComboBox"), UComboBoxString::StaticClass() },
			{ TEXT("VoiceInputModeUnavailableText"), UTextBlock::StaticClass() },
			{ TEXT("MicrophoneUnavailableText"), UTextBlock::StaticClass() },
			{ TEXT("UIScaleSlider"), USlider::StaticClass() },
			{ TEXT("MasterVolumeSlider"), USlider::StaticClass() },
			{ TEXT("MusicVolumeSlider"), USlider::StaticClass() },
			{ TEXT("SFXVolumeSlider"), USlider::StaticClass() },
			{ TEXT("AmbienceVolumeSlider"), USlider::StaticClass() },
			{ TEXT("VoiceVolumeSlider"), USlider::StaticClass() }
		};
		const FRequiredWidgetControl SaveRowControls[] = {
			{ TEXT("SaveSlotNameText"), UTextBlock::StaticClass() },
			{ TEXT("SaveSlotMetaText"), UTextBlock::StaticClass() },
			{ TEXT("SelectSaveSlotButton"), UButton::StaticClass() }
		};
		const FRequiredWidgetControl FriendRowControls[] = {
			{ TEXT("FriendNameText"), UTextBlock::StaticClass() },
			{ TEXT("FriendStatusText"), UTextBlock::StaticClass() },
			{ TEXT("InviteFriendButton"), UButton::StaticClass() }
		};
		const FRequiredWidgetControl PlayerSlotControls[] = {
			{ TEXT("PlayerNameText"), UTextBlock::StaticClass() },
			{ TEXT("PlayerRoleText"), UTextBlock::StaticClass() },
			{ TEXT("PlayerSlotStateText"), UTextBlock::StaticClass() }
		};
		return ValidateWidgetParent(TEXT("WBP_CatFrontendRoot"), UCatFrontendRootWidget::StaticClass())
			&& ValidateWidgetContract(TEXT("WBP_CatFrontendRoot"), RootControls)
			&& ValidateWidgetContract(TEXT("WBP_CatFrontendMenu"), MenuControls)
			&& ValidateWidgetContract(TEXT("WBP_CatFrontendSaveList"), SaveListControls)
			&& ValidateWidgetContract(TEXT("WBP_CatFrontendRoom"), RoomControls)
			&& ValidateWidgetContract(TEXT("WBP_CatFrontendSettings"), SettingsControls)
			&& ValidateWidgetContract(TEXT("WBP_CatFrontendSettings"), ExpandedSettingsControls)
			&& ValidateWidgetContract(TEXT("WBP_CatFrontendLoading"), LoadingControls)
			&& ValidateWidgetParent(TEXT("WBP_CatSaveSlotRow"), UCatFrontendSaveSlotRowWidget::StaticClass())
			&& ValidateWidgetContract(TEXT("WBP_CatSaveSlotRow"), SaveRowControls)
			&& ValidateWidgetParent(TEXT("WBP_CatRoomFriendRow"), UCatFrontendRoomFriendRowWidget::StaticClass())
			&& ValidateWidgetContract(TEXT("WBP_CatRoomFriendRow"), FriendRowControls)
			&& ValidateWidgetParent(TEXT("WBP_CatRoomPlayerSlot"), UCatFrontendRoomPlayerSlotWidget::StaticClass())
			&& ValidateWidgetContract(TEXT("WBP_CatRoomPlayerSlot"), PlayerSlotControls);
}
}

bool UCatFrontendWidgetAuthoringLibrary::CreateMissingFrontendWidgetBlueprints()
{
	// 前端 WBP 创建流程：先补齐普通业务子资产，再重建全局 Loading 和 Root，确保 Root 只挂独立 Loading 页面合同；最后修复文本字体并核验全部合同。
	using namespace CatFrontendWidgetAuthoring;
	const bool bCreated = CreateMissingWidget(TEXT("WBP_CatFrontendMenu"), UUserWidget::StaticClass(), UCanvasPanel::StaticClass(), BuildMenuWidget)
		&& CreateMissingWidget(TEXT("WBP_CatFrontendSaveList"), UUserWidget::StaticClass(), UCanvasPanel::StaticClass(), BuildSaveListWidget)
		&& CreateMissingWidget(TEXT("WBP_CatFrontendRoom"), UUserWidget::StaticClass(), UCanvasPanel::StaticClass(), BuildRoomWidget)
		&& CreateMissingWidget(TEXT("WBP_CatFrontendSettings"), UUserWidget::StaticClass(), UCanvasPanel::StaticClass(), BuildSettingsWidget)
		&& CreateMissingWidget(TEXT("WBP_CatSaveSlotRow"), UCatFrontendSaveSlotRowWidget::StaticClass(), USizeBox::StaticClass(), BuildSaveSlotRowWidget)
		&& CreateMissingWidget(TEXT("WBP_CatRoomFriendRow"), UCatFrontendRoomFriendRowWidget::StaticClass(), USizeBox::StaticClass(), BuildRoomFriendRowWidget)
		&& CreateMissingWidget(TEXT("WBP_CatRoomPlayerSlot"), UCatFrontendRoomPlayerSlotWidget::StaticClass(), USizeBox::StaticClass(), BuildRoomPlayerSlotWidget)
		&& RebuildWidgetInDirectory(WidgetDirectory, TEXT("WBP_CatFrontendLoading"),
			UUserWidget::StaticClass(), UCanvasPanel::StaticClass(), TEXT("CatFrontendWidgetAuthoring"), BuildLoadingWidget)
		&& RebuildWidgetInDirectory(WidgetDirectory, TEXT("WBP_CatFrontendRoot"),
			UCatFrontendRootWidget::StaticClass(), UCanvasPanel::StaticClass(), TEXT("CatFrontendWidgetAuthoring"), BuildRootWidget);
	return bCreated && RepairFrontendWidgetFonts() && ValidateFrontendWidgetContracts();
}

bool UCatFrontendWidgetAuthoringLibrary::CreateMissingLakeMainMenuWidgetBlueprint()
{
	// 局内菜单 WBP 创建流程：固定在 /Game/UI/Save 下创建或重建正式菜单资产，并立即核验全部控件合同。
	using namespace CatFrontendWidgetAuthoring;
	const bool bCreated = RebuildWidgetInDirectory(LakeMenuWidgetDirectory, TEXT("WBP_CatLakeMainMenu"),
		UCatLakeMainMenuWidget::StaticClass(), UCanvasPanel::StaticClass(), TEXT("CatLakeMainMenuWidgetAuthoring"),
		BuildLakeMainMenuWidget);
	return bCreated && ValidateLakeMainMenuWidgetContract();
}

bool UCatFrontendWidgetAuthoringLibrary::RepairFrontendWidgetBlueprintFonts()
{
	// 公开字体修复入口流程：复用命名空间内的九资产批处理，专门给已生成 WBP 或人工调整后重新落中文字体使用。
	return CatFrontendWidgetAuthoring::RepairFrontendWidgetFonts();
}

bool UCatFrontendWidgetAuthoringLibrary::ValidateFrontendWidgetBlueprintFonts()
{
	// 公开字体核验入口流程：只读复核九个正式 WBP 的文本字体对象，让资产脚本能在中文缺字回归时直接失败。
	return CatFrontendWidgetAuthoring::ValidateFrontendWidgetFonts();
}

bool UCatFrontendWidgetAuthoringLibrary::CreateMissingFrontendAudioSettingsAssets()
{
	// 音频设置资产创建流程：先拒绝资源集合不完整的状态，再新建 Master、四个子分类和 SoundMix，连接子分类后逐包保存；运行时音量覆写仍由 Settings Model 负责。
	using namespace CatFrontendWidgetAuthoring;
	bool bAlreadyComplete = false;
	if (!HasCompleteOrEmptyAudioAssetSet(bAlreadyComplete))
	{
		UE_LOG(LogTemp, Error, TEXT("Event=frontend_audio_authoring_partial_set Directory=%s"), *AudioDirectory);
		return false;
	}
	if (bAlreadyComplete)
	{
		return true;
	}

	auto CreateSoundClass = [](const TCHAR* AssetName) -> USoundClass*
	{
		// 声音分类创建流程：创建固定包与可独立编辑的 SoundClass；父子关系由外层在全部对象存在后一次连线。
		const FString PackageName = FString::Printf(TEXT("%s/%s"), *AudioDirectory, AssetName);
		UPackage* Package = CreatePackage(*PackageName);
		return Package ? NewObject<USoundClass>(Package, FName(AssetName), RF_Public | RF_Standalone | RF_Transactional) : nullptr;
	};

	USoundClass* Master = CreateSoundClass(TEXT("SC_CatMaster"));
	USoundClass* Music = CreateSoundClass(TEXT("SC_CatMusic"));
	USoundClass* SFX = CreateSoundClass(TEXT("SC_CatSFX"));
	USoundClass* Ambience = CreateSoundClass(TEXT("SC_CatAmbience"));
	USoundClass* Voice = CreateSoundClass(TEXT("SC_CatVoice"));
	UPackage* MixPackage = CreatePackage(*FString::Printf(TEXT("%s/SMX_CatFrontendSettings"), *AudioDirectory));
	USoundMix* Mix = MixPackage ? NewObject<USoundMix>(MixPackage, TEXT("SMX_CatFrontendSettings"), RF_Public | RF_Standalone | RF_Transactional) : nullptr;
	if (!Master || !Music || !SFX || !Ambience || !Voice || !Mix)
	{
		UE_LOG(LogTemp, Error, TEXT("Event=frontend_audio_authoring_create_failed Directory=%s"), *AudioDirectory);
		return false;
	}

	// 分类关系写入流程：Master 是唯一总线，四个产品分类直接归属它；Music 标记为音乐，其他分类保留引擎默认声音属性。
	Master->ChildClasses = { Music, SFX, Ambience, Voice };
	Music->ParentClass = Master;
	SFX->ParentClass = Master;
	Ambience->ParentClass = Master;
	Voice->ParentClass = Master;
	Music->Properties.bIsMusic = true;
	Mix->Duration = -1.0f;
	Mix->FadeInTime = 0.0f;
	Mix->FadeOutTime = 0.0f;

	const bool bSaved = SaveNewAsset(Master)
		&& SaveNewAsset(Music)
		&& SaveNewAsset(SFX)
		&& SaveNewAsset(Ambience)
		&& SaveNewAsset(Voice)
		&& SaveNewAsset(Mix);
	if (bSaved)
	{
		UE_LOG(LogTemp, Display, TEXT("Event=frontend_audio_authoring_created Directory=%s"), *AudioDirectory);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Event=frontend_audio_authoring_save_failed Directory=%s"), *AudioDirectory);
	}
	return bSaved;
}
