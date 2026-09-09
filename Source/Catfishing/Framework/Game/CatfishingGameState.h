#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "Framework/Core/CatRunContracts.h"
#include "GameFramework/GameStateBase.h"
#include "ShopEconomy/Trading/CatShopTradingTypes.h"
#include "Social/CatSocialTypes.h"
#include "CatfishingGameState.generated.h"

class UAbilitySystemComponent;
class UCatChumFieldReplicationComponent;
class UCatRunAttributeSet;
class UCatRunModifierAttributeSet;

/** GameState Run/Environment 完整公开快照变化通知；本机 UI 必须重新读取 GetRunPublicState。 */
DECLARE_MULTICAST_DELEGATE(FCatRunPublicStateChanged);

/** GameState 最近求助完整快照变化通知；本机 UI 必须重新读取 GetLastHelpSignal。 */
DECLARE_MULTICAST_DELEGATE(FCatHelpSignalChanged);

/** GameState 团队经济快照变化通知；表现层收到后重读整份复制事实。 */
DECLARE_MULTICAST_DELEGATE(FCatShopEconomySnapshotChanged);

/** Lake 共享比赛状态；复制由服务器 GameMode 组合的 Run/Environment、Social 求助与 Shop 公开经济事实。 */
UCLASS()
class CATFISHING_API ACatfishingGameState : public AGameStateBase, public IAbilitySystemInterface
{
	GENERATED_BODY()
public:
	/** 构造 GameState 的公开复制组件和唯一 Run ASC/稳定属性集；Owner/Avatar 在组件初始化后绑定为本 GameState。 */
	ACatfishingGameState();
	/** 返回 GameState 持有的唯一 Run ASC；GAS 查询只读到这一份全队公共数值宿主。 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	/** 返回本局 Run 专用 ASC，供 GameMode 创建 GE Spec；调用方不得直接 SetNumericAttributeBase 写额度。 */
	UAbilitySystemComponent* GetRunAbilitySystemComponent() const;
	/** 返回 authority 上可写的 Run ASC；客户端返回空，防止 UI 或复制回调绕过 GameMode 命令协议。 */
	UAbilitySystemComponent* GetRunAbilitySystemComponentFromAuthority() const;
	/** 注册 Run/Help/Shop 三类公开快照复制；客户端分别经 RepNotify 消费，不在本地推进领域状态。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 仅允许 authority GameMode 写入组合公开事实；每次写入都会触发网络更新。 */
	void SetRunPublicStateFromAuthority(const FCatRunPublicState& NewState);
	/** 提供服务器最终值或客户端最近复制值，调用方据此渲染一局状态；返回 const 引用保证外部不能绕过 GameMode 写口推进 Run。 */
	const FCatRunPublicState& GetRunPublicState() const;
	/** 仅允许 authority Social 服务发布最近一次求助；它不启动任务或自动加入 Fishing。 */
	void SetHelpSignalFromAuthority(const FCatHelpSignalSnapshot& NewSignal);
	/** 提供最近一次服务器求助信号供表现去重；它不是任务分配或自动加入玩法的授权依据。 */
	const FCatHelpSignalSnapshot& GetLastHelpSignal() const;
	/** 仅 authority 写入团队公款、货架库存与公开交易记录的整份快照。 */
	void SetShopEconomySnapshotFromAuthority(const FCatShopPublicEconomySnapshot& NewSnapshot);
	/** 提供服务器最终值或客户端最近复制值，商店 UI 只能只读展示余额、库存和公开交易。 */
	const FCatShopPublicEconomySnapshot& GetShopEconomySnapshot() const;
	/** 返回 ChumField 的公开复制组件；客户端只读窝点表现事实，不能通过它创建或修改窝点。 */
	const UCatChumFieldReplicationComponent* GetChumFieldReplication() const { return ChumFieldReplication; }
	/** authority 写口使用的 ChumField 复制组件；非服务器返回空，防止客户端绕开 Subsystem 发布窝点。 */
	UCatChumFieldReplicationComponent* GetChumFieldReplicationFromAuthority();
	/** 本机 Run/Environment 完整快照变化通知；不授权订阅者推进 StateTree。 */
	FCatRunPublicStateChanged OnRunPublicStateChanged;
	/** 本机最近求助完整快照变化通知；不授权订阅者接受任务或改变 Social 权限。 */
	FCatHelpSignalChanged OnHelpSignalChanged;
	/** 本机商店公开经济快照变化通知；只提示 UI 重读，不授权客户端确认交付或改余额。 */
	FCatShopEconomySnapshotChanged OnShopEconomySnapshotChanged;
protected:
	/** 组件完成注册后按 Lyra 口径初始化 Run ASC 的 Owner/Avatar；这里不计算额度、不推进 StateTree。 */
	virtual void PostInitializeComponents() override;
	/** 实例进入 World 后记录实际类；Run ASC 已在组件初始化阶段绑定，不在这里补算额度或推进 StateTree。 */
	virtual void BeginPlay() override;
	/** 客户端收到新 Revision 后记录结构化诊断，UI/玩法只能继续读取复制快照。 */
	UFUNCTION()
	void OnRep_RunPublicState();
	/** 客户端收到新求助 Revision 后只记录/供表现读取，不自动执行互动。 */
	UFUNCTION()
	void OnRep_HelpSignal();
	/** 客户端收到商店公开快照后只记录诊断并通知只读 UI，不在本地确认购买或改库存。 */
	UFUNCTION()
	void OnRep_ShopEconomySnapshot();

private:
	/** 全队共享 Run 数值的唯一 GAS 组件，构造期创建并复制；GameMode 只通过它应用正式 GE。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Run", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAbilitySystemComponent> RunAbilitySystemComponent;

	/** Run ASC 持有的最终额度属性集，保存目标和进度；GameMode 只投影它，不维护第二套最终数值公式。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Run", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatRunAttributeSet> RunAttributes;

	/** Run ASC 持有的来源倍率属性集，保存压力、效率和目标倍率；ExecCalc 捕获它，业务模块不得各自重算来源修正。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Run", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatRunModifierAttributeSet> RunModifiers;

	/** 自然事件与玩家打窝的公开复制组件；服务器 ChumFieldSubsystem 写入，客户端只用它驱动窝点表现。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCatChumFieldReplicationComponent> ChumFieldReplication;

	/** Run 对外展示的局内进度真相；服务器在昼夜/额度变化时写入，客户端只通过 OnRep 观察。 */
	UPROPERTY(ReplicatedUsing = OnRep_RunPublicState)
	FCatRunPublicState RunPublicState;

	/** Social 在 authority 写入的最近一条手动/巨鱼信号；客户 OnRep 只通知表现，范围和全局标记始终由服务器裁决。 */
	UPROPERTY(ReplicatedUsing = OnRep_HelpSignal)
	FCatHelpSignalSnapshot LastHelpSignal;

	/** 商店公开经济快照；只复制团队公款、货架库存和公开交易记录，购买结果仍由服务器命令返回。 */
	UPROPERTY(ReplicatedUsing = OnRep_ShopEconomySnapshot)
	FCatShopPublicEconomySnapshot ShopEconomySnapshot;
};
