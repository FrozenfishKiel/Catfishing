#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameFramework/Character.h"
#include "Fishing/CatFishingFightModel.h"
#include "CatCharacter.generated.h"

class UAbilitySystemComponent;
class UCatSurvivalAttributeSet;
class UCatContainerReplicationComponent;
class UCatConditionComponent;
class UCatEquipmentComponent;
class UCameraComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputMappingContext;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

/**
 * Lake 的唯一玩法身体；同时宿主 Character-owned ASC/三属性、Condition、Equipment 与个人鱼护复制出口。
 * Character 同时作为 ASC Owner/Avatar；丢失占有或销毁时先收口 Fishing/Social，所有局内事实都不上移到 Profile/Online。
 */
UCLASS()
class CATFISHING_API ACatCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	/**
	 * 构造 ASC/三属性集、Condition、Equipment 与个人鱼护出口，再挂第三人称相机臂、跟随相机和占位几何体，并设定“身体朝
	 * 移动方向转”的旋转默认值；不在 CDO 写任何运行数值。
	 */
	ACatCharacter();

	/** 返回 Character 持有的唯一 ASC；即使阶段 C gate 关闭也返回组件，让外部只读接缝不需要第二条查找路径。 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	/** 返回 authority 为本 Character 注册的一局个人鱼护 ID；未注册时为无效 GUID。 */
	FGuid GetPersonalFishGuardId() const;

	/** 返回 Character 唯一离散身体状态组件；Wet/Downed/恢复不进入 PlayerState 或 Profile。 */
	UCatConditionComponent* GetConditionComponent() const;

	/** 返回 Character 唯一一局装备组件；永久解锁/选择仍在 LocalPlayer Profile。 */
	UCatEquipmentComponent* GetEquipmentComponent() const;

	/** 返回跟随相机；它是本地玩家的唯一视图来源，外部只读核对附着关系或切视图目标，不得绕过它再造第二台相机。 */
	UCameraComponent* GetFollowCamera() const;

	/** 返回占位身体网格；正式猫模型到位前它是唯一可见身体，替换模型时换这个组件的 StaticMesh 而不是再挂一个，外部只读核对附着与碰撞。 */
	UStaticMeshComponent* GetPlaceholderMesh() const;

protected:
	/** 组件注册完成后幂等刷新 Owner/Avatar；未裁 runtime 会清除引擎自动建立的 ActorInfo 并保持 fail-closed。 */
	virtual void BeginPlay() override;

	/** 父类完成占有后刷新 Owner/Avatar 和一次初值；authority 才授诊断 Ability、注册个人鱼护并尝试 starter 装配。 */
	virtual void PossessedBy(AController* NewController) override;

	/** Controller 复制变化后刷新拥有客户端 ActorInfo；Controller 失效时先移除自有 MappingContext 再 ClearActorInfo。 */
	virtual void OnRep_Controller() override;

	/** 本地 Pawn 重启后刷新 ActorInfo，并以 remove-own/add-own 顺序重装唯一临时 MappingContext。 */
	virtual void PawnClientRestart() override;

	/**
	 * 只在 EnhancedInputComponent 上绑定 CatInputSettings 的 Move/Look/Jump、遛鱼拖/松两个 Boolean 动作与
	 * CatAbilitySettings 的诊断 InputAction；任一配置缺失时对应绑定静默跳过，不创建传统输入旁路。
	 */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** 失去占有前先收口 Fishing/Social，移除自有 MappingContext 并取消 Ability；父类断开 Controller 后才清 ActorInfo。 */
	virtual void UnPossessed() override;

	/** Actor 离开 World 时幂等收口 Fishing/Social、解注册鱼护，再移输入/取消 Ability/清 ActorInfo，最后交父类销毁。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 按 Stage C fail-closed 配置初始化 Character=this 的 Owner/Avatar；未裁复制策略时主动 Clear 而不是猜 Mixed。 */
	void InitializeAbilityActorInfo();

	/** authority 在 ActorInfo 就绪后授予一次诊断 Ability；有效 SpecHandle 是唯一去重事实，重占有不会重复授予。 */
	void GrantStageCTestAbility();

	/** authority 新 Character 首次 ActorInfo 就绪时整体应用三项显式初值（Poison/FishingStrength/FightStamina）；重占有不重置已消耗的局内状态。 */
	void ApplyInitialAttributesOnce();

	/** authority 在真实占有后尝试配置 starter 三件套；已有装配或配置缺失都保持当前 Equipment Snapshot 不变。 */
	void ApplyStarterEquipmentOnce();

	/** 为当前本地拥有者重装临时 MappingContext；只移除本 Character 自己曾添加的实例，不调用 ClearAllMappings。 */
	void RefreshProvisionalMappingContext();

	/** 从保存的 LocalPlayer 输入子系统成对移除自有 MappingContext；旧 World 或空引用下重复调用安全。 */
	void RemoveProvisionalMappingContext();

	/** 临时 InputAction 的触发入口；只调用 ASC TryActivateAbilityByClass，让 ServerOnly Ability 决定最终执行。 */
	void HandleDiagnosticAbilityInput();

	/** Move InputAction 的 Triggered 入口；把 Axis2D（X=右，Y=前）按 Controller 偏航换算成世界方向后交给 CharacterMovement。 */
	void HandleMoveInput(const FInputActionValue& Value);

	/** Look InputAction 的 Triggered 入口；X 进偏航、Y 进俯仰，只影响 Controller 旋转，身体朝向由移动方向决定。 */
	void HandleLookInput(const FInputActionValue& Value);

	/** 遛鱼左键按下/抬起入口：记下"拖"是否按着，再把合成意图报给服务器。 */
	void HandleFishingPullStarted();
	void HandleFishingPullCompleted();

	/** 遛鱼右键按下/抬起入口：记下"松"是否按着，再把合成意图报给服务器。 */
	void HandleFishingReleaseStarted();
	void HandleFishingReleaseCompleted();

	/** 把两个按键状态合成一个意图（左键优先于右键）并在变化时通过 Controller 的 ServerSetFishingFightIntent 上报；只在本地控制时发送。 */
	void SendFishingFightIntent();

	/** authority 从 PlayerState::UniqueId 注册个人鱼护；StableNetId 只进入 Items 私有记录，不进入复制组件。 */
	void RegisterPersonalFishGuard();

	/** 在失去占有或销毁前终止本 Character 参与的钓鱼与偷鱼协议，随后才允许身体和 ASC 清理。 */
	void NotifyFishingOwnerUnavailable();

	/** 猫身体唯一 AbilitySystemComponent；构造期创建、随 Character 复制，并拥有本 Actor 的 AttributeSet 与 Ability。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Abilities", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	/** 猫身体唯一 Survival 属性集；构造期显式交给 ASC 持有，Poison/FishingStrength/FightStamina 都只从这份局内真相读写。饥饿与疲惫数值都已删除。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Survival", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatSurvivalAttributeSet> SurvivalAttributes;

	/** 个人鱼护的只读复制出口；数组只由 authority Items Service 提交，本 Character 不提供写口。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Items", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatContainerReplicationComponent> PersonalFishGuard;

	/** 猫身体唯一离散状态组件；复制 Wet/Downed/Recovery，数值仍由 Survival AttributeSet 拥有。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Survival", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatConditionComponent> ConditionComponent;

	/** 一局功能型装配、耗材与鱼竿耐久宿主；没有等级、词条、战力或偷取接口。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Equipment", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatEquipmentComponent> EquipmentComponent;

	/** 第三人称相机臂；附着在胶囊上并跟随 Controller 旋转，只决定相机位置，不参与任何玩法判定。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** 本地玩家的跟随相机；挂在相机臂末端，由引擎视图目标机制读取，不写任何游戏状态。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	/** 正式猫模型到位前的占位圆柱体；贴合胶囊尺寸、无碰撞，只为让玩家在 PIE 里看见身体，随时可被正式 Mesh 替换。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> PlaceholderMesh;

	/** 指向身体 +X 的占位小锥体；无碰撞，只用于肉眼核对 Look/Move 后身体朝向是否正确，随正式模型一起移除。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> PlaceholderFacingCone;

	/** 本 Character 一局内的个人鱼护 ID；由 authority 首次注册生成，不复制成第二份容器快照。 */
	FGuid PersonalFishGuardId;

	/** 服务器已授予的阶段 C 诊断 Ability 句柄；无效表示尚未授予，Character 销毁时随 ASC 一起释放。 */
	FGameplayAbilitySpecHandle StageCTestAbilityHandle;

	/** 本 Character 是否已经整体应用过初始属性；只在 authority 写，重占有保持 true，重连新身体重新开始。 */
	bool bInitialAttributesApplied = false;

	/** 本地玩家当前是否按着遛鱼"拖"键；只在拥有客户端维护，用来合成上报的意图。 */
	bool bFishingPullHeld = false;

	/** 本地玩家当前是否按着遛鱼"松"键；只在拥有客户端维护。 */
	bool bFishingReleaseHeld = false;

	/** 最近一次已经上报给服务器的遛鱼意图；按键状态合成结果没变就不重复发 RPC。 */
	ECatFishingFightIntent LastSentFishingIntent = ECatFishingFightIntent::None;

	/** 本 Character 最近一次成功添加的临时 MappingContext；只为成对 Remove 保留，不代表最终键位配置。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> AppliedMappingContext;

	/** 实际接收 AddMappingContext 的 LocalPlayer 子系统弱引用；跨 World 清理时不强持 LocalPlayer 生命周期。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> AppliedInputSubsystem;
};
