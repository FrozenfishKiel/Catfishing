#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "AbilitySystem/Items/CatItemAbilityTargetData.h"
#include "CatItemAbilityComponent.generated.h"

class UCatInventoryItemInstance;
class UCatAbilitySystemComponent;

/** 角色物品与 GAS 的授予接线；不拥有库存、不结算效果，负责来源能力和复制未就绪时的一次待处理输入。 */
UCLASS()
class CATFISHING_API UCatItemAbilityComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	/** 默认关闭轮询，只有待处理输入等待复制时才开启 Tick。 */
	UCatItemAbilityComponent();
	/** 订阅随身库存变化并建立来源能力；ASC 尚未准备好时由初始化入口补刷。 */
	virtual void BeginPlay() override;
	/** 解绑库存并撤销本组件授予的所有能力，避免换 Pawn 后旧来源继续响应。 */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	/** 重试待处理输入；库存与世界鱼来源都失效或等待超时后报告失败，已处理的激活不再重试。 */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/** 服务器根据当前随身实例授予或回收能力；仅配有使用片段的物品参与。 */
	void RefreshGrantedAbilities();
	/** 本地使用入口；冻结来源并激活对应 Spec，共享容器使用角色常驻的食用能力。 */
	bool RequestUse(UCatInventoryComponent* Inventory, FGuid ItemId, FGuid RequestId, bool bContinuousInput = false);
	/** 本地嘴叼鱼入口；固定当前世界鱼身份，沿角色常驻能力传输，不创建背包副本。 */
	bool RequestUseCarriedFish(ACatFishPickupActor* Fish, FGuid RequestId);
	/** 能力激活时读取一次本机输入；Spec 不匹配时拒绝，防止能力间串来源。 */
	bool TakeLocalTarget(FGameplayAbilitySpecHandle Handle, FCatItemAbilityTargetData& OutTarget);
	/** 松开或取消本地冻结的来源输入；通过同一 Spec 的标准 GAS 事件传递，不再发物品专用 RPC。 */
	void ReleaseUseInput(bool bCancelled);
private:
	/** 本次本地按住的来源能力；激活前写入，松开清除，永远不通过新选中格寻找。 */
	FGameplayAbilitySpecHandle HeldInputHandle;
	/** 观察中的真实背包；只用于解绑通知，不保存库存副本。 */
	UPROPERTY() TObjectPtr<UCatInventoryComponent> ObservedInventory;
	/** 每份随身实例对应的能力句柄；仅服务器维护，客户端从 ASC 的 Spec 复制读取来源。 */
	TMap<FGuid, FGameplayAbilitySpecHandle> Granted;
	/** 唯一待处理本地意图；激活时被取走，等待期间的新请求被拒绝，超时或组件结束时清空。 */
	UPROPERTY() FCatItemAbilityTargetData PendingTarget;
	/** 本地正在启动的 Spec；能力读取输入时必须与它一致。 */
	FGameplayAbilitySpecHandle PendingHandle;
	/** 等待复制的截止世界秒数；到期只报告失败，不另发一条服务器使用 RPC。 */
	double PendingDeadline = 0.0;
	/** 查找已复制的来源 Spec 并尝试激活；找不到时等待复制，不创建客户端能力。 */
	bool TryActivatePending();
};
