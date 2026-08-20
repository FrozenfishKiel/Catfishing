#include "Input/CatLocalPlayerInputSubsystem.h"

#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Input/CatInputSettings.h"

// 初始化流程：先交还父类建立 LocalPlayer 生命周期，再尝试装配显式输入上下文；配置未就绪时保持无副作用。
void UCatLocalPlayerInputSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RefreshConfiguredInputContexts();
}

// 销毁流程：只移除自己添加过的 MappingContext，再释放弱引用和列表，避免影响 Character 或其他插件输入层。
void UCatLocalPlayerInputSubsystem::Deinitialize()
{
	RemoveAppliedInputContexts();
	Super::Deinitialize();
}

// Controller 变化流程：先让父类更新 LocalPlayer 关系，再按新 Controller/LocalPlayer 重新装配全局输入上下文。
void UCatLocalPlayerInputSubsystem::PlayerControllerChanged(APlayerController* NewController)
{
	Super::PlayerControllerChanged(NewController);
	RefreshConfiguredInputContexts();
}

// 刷新流程：先精确移除旧上下文；配置 gate、LocalPlayer 或 EnhancedInput 不可用时返回 false；资产同步加载成功后按配置优先级添加并记录。
bool UCatLocalPlayerInputSubsystem::RefreshConfiguredInputContexts()
{
	RemoveAppliedInputContexts();
	const UCatInputSettings* Settings = GetDefault<UCatInputSettings>();
	if (!Settings || !Settings->IsRuntimeReady() || !GetLocalPlayer())
	{
		return false;
	}
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!InputSubsystem)
	{
		return false;
	}

	TArray<FCatInputMappingContextConfig> Contexts;
	Settings->GetRuntimeContexts(Contexts);
	TSet<TObjectPtr<UInputMappingContext>> SeenContexts;
	for (const FCatInputMappingContextConfig& ContextConfig : Contexts)
	{
		UInputMappingContext* MappingContext = ContextConfig.MappingContext.LoadSynchronous();
		if (!MappingContext || SeenContexts.Contains(MappingContext))
		{
			continue;
		}
		InputSubsystem->AddMappingContext(MappingContext, ContextConfig.Priority);
		AppliedMappingContexts.Add(MappingContext);
		SeenContexts.Add(MappingContext);
	}
	if (AppliedMappingContexts.IsEmpty())
	{
		return false;
	}
	AppliedInputSubsystem = InputSubsystem;
	return true;
}

// 移除流程：只对保存的 EnhancedInput 子系统调用 RemoveMappingContext；无论对象是否存活都会清空本地记录。
void UCatLocalPlayerInputSubsystem::RemoveAppliedInputContexts()
{
	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = AppliedInputSubsystem.Get())
	{
		for (UInputMappingContext* MappingContext : AppliedMappingContexts)
		{
			if (MappingContext)
			{
				InputSubsystem->RemoveMappingContext(MappingContext);
			}
		}
	}
	AppliedMappingContexts.Reset();
	AppliedInputSubsystem.Reset();
}