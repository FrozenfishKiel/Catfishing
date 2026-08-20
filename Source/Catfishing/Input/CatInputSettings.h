#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatInputSettings.generated.h"

class UInputAction;
class UInputMappingContext;

/** 全局输入上下文层级；只描述 LocalPlayer 装配顺序，不代表某个玩法命令已经可用。 */
UENUM(BlueprintType)
enum class ECatInputContextLayer : uint8
{
	/** 前端/联机会话界面的输入层。 */
	Frontend,
	/** Lake 内角色与世界交互输入层。 */
	LakeGameplay,
	/** 菜单、相册、商店等覆盖层输入；优先级通常高于玩法层。 */
	OverlayUI
};

/** 一条可装配的 EnhancedInput MappingContext 配置；资产为空时整条配置不可用。 */
USTRUCT(BlueprintType)
struct FCatInputMappingContextConfig
{
	GENERATED_BODY()

	/** 该 MappingContext 所在的全局输入层，用于审查优先级是否符合预期。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Input")
	ECatInputContextLayer Layer = ECatInputContextLayer::LakeGameplay;

	/** 实际 EnhancedInput MappingContext 软引用；没有正式资产时保持空并 fail-closed。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (AllowedClasses = "/Script/EnhancedInput.InputMappingContext"))
	TSoftObjectPtr<UInputMappingContext> MappingContext;

	/** AddMappingContext 使用的优先级；数值越高越晚处理覆盖。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Input")
	int32 Priority = 0;
};

/** LocalPlayer 全局输入装配设置；默认关闭，避免临时诊断输入被误认为正式输入方案。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Input"))
class CATFISHING_API UCatInputSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 判断是否至少有一条完整 MappingContext 可装配；关闭或全空时输入协调器不触碰 EnhancedInput。 */
	bool IsRuntimeReady() const;

	/** 收集当前可用 MappingContext 配置并按优先级从低到高排序；无效条目会被跳过。 */
	void GetRuntimeContexts(TArray<FCatInputMappingContextConfig>& OutContexts) const;

	/** 判断角色移动/视角输入是否可绑定：总 gate 开启且 MoveAction、LookAction 都已配置；JumpAction 可选，不参与判断。 */
	bool IsCharacterInputReady() const;

	/** LocalPlayer 全局输入装配总 gate；默认关闭，正式资产接线后再开启。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableGlobalInputContexts = false;

	/** 全局 MappingContext 列表；同一资产重复配置会被协调器去重后只装配一次。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	TArray<FCatInputMappingContextConfig> MappingContexts;

	/** 角色平面移动的 Axis2D InputAction 软引用（X=右，Y=前）；ACatCharacter 在 SetupPlayerInputComponent 读取并绑定，为空时角色不绑定移动。 */
	UPROPERTY(Config, EditAnywhere, Category = "Character", meta = (AllowedClasses = "/Script/EnhancedInput.InputAction"))
	TSoftObjectPtr<UInputAction> MoveAction;

	/** 角色视角的 Axis2D InputAction 软引用（X=偏航，Y=俯仰）；ACatCharacter 读取并绑定到 Controller 旋转输入，为空时角色不绑定视角。 */
	UPROPERTY(Config, EditAnywhere, Category = "Character", meta = (AllowedClasses = "/Script/EnhancedInput.InputAction"))
	TSoftObjectPtr<UInputAction> LookAction;

	/** 角色跳跃的 Boolean InputAction 软引用；可选，为空时 ACatCharacter 只绑定移动和视角。 */
	UPROPERTY(Config, EditAnywhere, Category = "Character", meta = (AllowedClasses = "/Script/EnhancedInput.InputAction"))
	TSoftObjectPtr<UInputAction> JumpAction;

	/** 遛鱼"拖"（飞书：左键拖/提竿）的 Boolean InputAction 软引用；可选，ACatCharacter 按 Started/Completed 把按住状态报给服务器，为空时不绑定。 */
	UPROPERTY(Config, EditAnywhere, Category = "Fishing", meta = (AllowedClasses = "/Script/EnhancedInput.InputAction"))
	TSoftObjectPtr<UInputAction> FishingPullAction;

	/** 遛鱼"松"（飞书：右键放线）的 Boolean InputAction 软引用；可选，与 FishingPullAction 同一套绑定方式。 */
	UPROPERTY(Config, EditAnywhere, Category = "Fishing", meta = (AllowedClasses = "/Script/EnhancedInput.InputAction"))
	TSoftObjectPtr<UInputAction> FishingReleaseAction;
};