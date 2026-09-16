#pragma once

#include "CoreMinimal.h"
#include "Collection/CatImprintTypes.h"
#include "Framework/Core/CatProfileContracts.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "CatProfileSubsystem.generated.h"

class UCatProfileSaveGame;

/** 外部本地成像桥监听的新计划广播；监听方只在真实图片 durable 后回报成功。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatCapturePlanReceived, const FCatCapturePlan&);

/** 账号图鉴内容、追踪或可读状态变化通知；订阅者重读快照，身份切换时也会收到不可用状态，不取得档案写权限。 */
DECLARE_MULTICAST_DELEGATE(FCatFishCollectionChanged);

/**
 * 某个鱼种第一次成功捕获已写入当前账号的本机图鉴缓存（int32 数字鱼种 ID，double 本次重量千克）。
 * 「首次解锁新鱼种」这件事的唯一事实源就是这里：收集层解锁位从 false 翻成 true 的那一次，
 * 而且必须已经第二次落盘成功——否则特写弹了、档案没写上，玩家下次进来会发现图鉴里没有它。
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCatFishSpeciesFirstRecorded, int32, double);

/** LocalPlayer 的持久化协调器；账号图鉴与本机相册/装备分别持有对象和账本，不接触服务器实物容器，也不将缓存写入等同于云同步完成。 */
UCLASS()
class CATFISHING_API UCatProfileSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** 当前账号缓存中已保存的追踪鱼种编号；图鉴与库存读取，账号档案未就绪或未追踪时返回 0，不读取旧本机图鉴。 */
	int32 GetTrackedFish() const;
	/** 仅接受已成功捕获且总表有效的鱼；零取消，写盘失败恢复旧编号，不发成功通知。 */
	bool SetTrackedFish(int32 ItemId);

	/** 安装账号检查并加载独立图鉴，再解析本机 Profile 槽并重放其非图鉴 Pending；旧图鉴不自动归属当前账号，未配置时不可写。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 释放内存 SaveGame 与瞬时 CapturePlan 广播；不在销毁阶段把未落盘状态冒充成功。 */
	virtual void Deinitialize() override;

	/** 应用一份不可变 Grant：先写 Pending Journal，再合并内容并写 Complete，只有第二次落盘成功才允许 ACK。 */
	FCatProfileApplyResult ApplyGrant(const FCatProfileGrant& Grant);

	/** 接收服务器 CapturePlan；只有计划完整且外部桥已接入时才广播并返回 true，否则返回 false 让 Controller 明确回报失败终态。 */
	bool ReceiveCapturePlan(const FCatCapturePlan& Plan);

	/** 本地记录一个功能装备选择；验证槽位和正式定义后 durable 保存，不写 Character 运行态或装备所有权。 */
	FCatDomainCommandResult SetEquipmentSelection(FGuid RequestId, FName SlotId, int32  ItemId);

	/** 读取本机档案中某个稳定槽位的跨局数字物品选择；未就绪或不存在时返回 false 且输出 0。 */
	bool TryGetEquipmentSelection(FName SlotId, int32& OutItemId) const;

	/** 复制当前已就绪账号的图鉴记录供只读展示；不读取旧本机图鉴，不包含相册、Journal、功能解锁或装备选择。 */
	bool GetFishCollectionSnapshot(TArray<FCatFishCollectionRecord>& OutRecords) const;

	/** 复制本人本机相册索引（含 bHidden）；供独立相册页隐藏操作使用，不出网、不进入鱼图鉴页。 */
	bool GetLocalImprintSnapshot(TArray<FCatLocalImprintRecord>& OutRecords) const;

	/**
	 * 只在本地相册切换本人隐藏状态并 durable 保存；不产生服务器全局撤下或修改其他玩家副本。
	 * 印记册「本人可一键隐藏任意一张」的唯一写口，独立相册入口与蓝图从这里调用。
	 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Collection")
	FCatDomainCommandResult SetImprintHidden(FGuid RequestId, FGuid ImprintId, bool bHidden);

	/** 外部本地成像桥订阅入口；订阅者负责自己的图片格式、原子文件写与容量策略。 */
	FCatCapturePlanReceived OnCapturePlanReceived;

	/** 账号图鉴读模型刷新入口；授予完成、追踪保存和账号可用性变化时触发，消费者必须重新检查可读状态。 */
	FCatFishCollectionChanged OnFishCollectionChanged;

	/** 首次解锁新鱼种的订阅入口；只在收集层解锁位第一次翻成 true 且已 durable 之后触发一次，同种鱼的第二条不再触发。 */
	FCatFishSpeciesFirstRecorded OnFishSpeciesFirstRecorded;

private:
	/** 按真实 Steam 身份加载图鉴；编辑器使用隔离开发槽，身份未就绪只等待，不创建正式空档。 */
	bool RefreshCollectionAccount(float DeltaSeconds);
	/** 检查专用图鉴版本、账号、编号、解锁与账本；损坏数据保持不可写。 */
	bool ValidateCollectionSave() const;
	/** 本账号唯一可写图鉴对象；账号变化时先清空再加载，旧本地档案不自动导入。 */
	UPROPERTY(Transient) TObjectPtr<class UCatCollectionSaveGame> CurrentCollection;
	/** 当前已解析账号键；SteamID 只用于隔离，不写诊断日志。 */
	FString CollectionAccountKey;
	/** 专用保存槽；仅该目录可配置 Auto-Cloud，开发槽位在另一个目录。 */
	FString CollectionSlotName;
	/** 身份检查句柄；初始化安装，销毁时移除，不拥有游戏世界计时器。 */
	FTSTicker::FDelegateHandle AccountTicker;
	/** 身份与专用档案均已校验且写盘可用；只影响图鉴，不影响本地相册。 */
	bool bCollectionReady = false;

	/** 校验 Grant 内容是否足以进入 Journal；拒绝发生在任何 SaveGame 写入之前。 */
	static ECatDomainCommandError ValidateGrant(const FCatProfileGrant& Grant);

	/** 把一份已落 Pending 的 Grant 幂等合并到内存档案；不自行保存或发 ACK。
	 *  bOutFirstRecordedUnlock 报告本次是否把收集层解锁位第一次翻成 true，供落盘成功后决定要不要弹首解锁特写。 */
	bool MergeGrantIntoProfile(const FCatProfileGrant& Grant, bool& bOutFirstRecordedUnlock);

	/** 完成一个已存在的 Pending Journal：合并、标 Complete、保存；失败时重新加载磁盘 Pending 事实。 */
	FCatProfileApplyResult CompletePendingGrant(FGuid GrantId, bool bCollection = false);

	/** 按 bCollection 选择账号图鉴缓存或本机 Profile 并同步写盘；图鉴先校验载荷，失败不改变账本阶段，成功不代表云同步完成。 */
	bool SaveCurrentProfile(bool bCollection = false) const;

	/** 按 bCollection 重载对应缓存并校验，返回是否可用；调用者据返回值关闭对应写入能力，不能将校验失败的对象作为成功数据读取。 */
	bool ReloadDurableProfile(bool bCollection = false);

	/** 当前 LocalPlayer 的本机相册、解锁和装备档案；初始化加载、非图鉴授予写入，与 CurrentCollection 分开，不向当前账号认领旧图鉴。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatProfileSaveGame> CurrentProfile;

	/** 由基础名和 ControllerId 组成的实际槽位；不从服务器或 StableNetId 派生。 */
	FString ResolvedSlotName;

	/** SaveGame API 的本地用户索引；直接取 LocalPlayer ControllerId，负值时不启用持久化。 */
	int32 ResolvedUserIndex = INDEX_NONE;

	/** 本机 Profile 已就绪的标记；初始化和重载维护，控制非图鉴写入与 ACK，不代替账号图鉴的 bCollectionReady。 */
	bool bPersistenceReady = false;
};
