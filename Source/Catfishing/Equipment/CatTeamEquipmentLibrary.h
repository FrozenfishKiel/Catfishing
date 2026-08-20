#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Equipment/CatEquipmentTypes.h"
#include "CatTeamEquipmentLibrary.generated.h"

/** 团队装备库内容发生变化的服务器本机通知；复制挂载点订阅它来推送新的库存快照。 */
DECLARE_MULTICAST_DELEGATE(FCatTeamEquipmentLibraryChanged);

/**
 * 一局的团队装备库：全队共有一份，买来的东西落在这里，World 结束就清空，不进任何跨局存档。
 * 它是《商店订单不拥有装备实例》这条决策里 Equipment 那一半——实例的创建、编号和版本只发生在这里，
 * 商店拿到的只是一份回执，用来把自己的订单从待交付推到已交付。
 *
 * 它有两条写口："东西进来了"（GrantFromShopOrder）和"有人把它拿走了"（TakeInstance）。
 * 两条写口各配一个只读预检（ValidateShopOrderGrant / ValidateTake），理由是同一件事：它们各自的下一步都不可逆——
 * 入库那一步的上游是商店扣钱、取走那一步的下游是角色装配——所以"库这边同不同意"必须能在动手之前先问出来。
 * 飞书 §3.2 写的竿漂上公共架、备装自取、消耗品从公库领这套取用分类与优先级规则没有重建：
 * 取用只按实例 ID、谁先来谁拿，不分类、不排队；消耗品（普通饵、窝料）也不经过这里，订单直接落到角色耗材栈。
 */
UCLASS()
class CATFISHING_API UCatTeamEquipmentLibrary : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在服务器 Game World 创建；客户端不持有第二份装备库，只消费复制出去的快照。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 退出时关闭入库写口并清空本局实物、订单索引与幂等缓存；装备库不跨局。 */
	virtual void Deinitialize() override;

	/** 团队装备库当前内容与版本；调用方只能读，不能借引用往库里塞东西。 */
	const FCatTeamEquipmentLibrarySnapshot& GetSnapshot() const;

	/**
	 * 声明：只读地回答"这条定义现在入不入得了库"，返回 None 表示入得了，其余值就是 GrantFromShopOrder 遇到同样情况会
	 *       给出的错误码。它不创建实例、不推进版本、不写缓存。
	 * 用途：商店订单那条链要在扣钱之前先知道东西送不送得出去——公款和装备库是两个聚合，钱一旦划走商店没有反向写口，
	 *       所以那条链靠前置 gate 而不是事后退款。"这条 DefinitionId 在装备运行目录里存不存在"商店自己也答不了，
	 *       FCatShopCatalogEntry::IsRuntimeReady 刻意不跨目录校验定义，只有装备库这一侧能回答。判据写在这里一份，入库写口自己也调它。
	 */
	ECatDomainCommandError ValidateShopOrderGrant(FName DefinitionId) const;

	/**
	 * 声明：按一笔已经付过款的商店订单把一件实物放进团队装备库，并返回可以当交付回执用的实例与版本。
	 * 实现：先做载荷与重放判定，再确认写口没关、定义在运行目录里；随后按订单 ID 查这笔订单是不是已经入过库，
	 *       已入库的直接把那件实物原样带回并标成 AlreadyResolved；确认是第一次之后才比对版本前提并真正创建实例。
	 * 边界：它不扣钱、不改商店库存、也不判断谁有权买——那些是商店那一侧已经做完的事；
	 *       订单 ID 的去重排在版本前提之前，因为交付重试拿着的一定是入库前的旧版本，
	 *       这时候要的是把回执找回来，而不是报一个会让订单永远卡在待交付的版本冲突。
	 */
	FCatTeamEquipmentGrantResult GrantFromShopOrder(const FCatTeamEquipmentGrantCommand& Command);

	/**
	 * 声明：只读回答"这条取用命令此刻会不会被库接受"，返回 None 表示会，返回别的错误码就是 TakeInstance 现在会给出的同一个拒绝理由；
	 *       接受时顺带把那件实物拷一份出去，调用方拿它去做取走之前必须先做完的准备（比如先把装备装到角色身上）。
	 *       它存在的理由是取用是一条跨聚合的链：装配一旦发生就没有回头路，所以"库这边同不同意"必须能在装配之前问出来，
	 *       否则只能等 TakeInstance 拒绝，而那时东西已经在角色身上、库里那件却还在，同一件装备会被取第二次。
	 * 实现：把判据整个交给和 TakeInstance 同一份准入求值；通过时按求出的下标拷出实物，不通过时把输出清空。
	 * 边界：完全没有副作用——不写库存、不推进版本、不碰终态缓存，也不做重放判定。
	 *       重放回答的是"这个 RequestId 之前已经有过结论"，和"现在取不取得走"不是一个问题，所以一次已经重放过的取用在这里仍按当前库存事实回答。
	 */
	ECatDomainCommandError ValidateTake(const FCatTeamEquipmentTakeCommand& Command, FCatTeamEquipmentInstance& OutInstance) const;

	/**
	 * 声明：把库里的一件实物按实例 ID 取走并带回给调用方去装配；这是"买来的东西从公库到某只猫手上"的唯一出口，没有它买到的竿永远只能躺在库里。
	 *       按实例而不按定义名取，是为了让"谁拿走了哪一件"可追、同一件东西不会被两个人同时拿到。
	 * 实现：先按同一份准入求值算出这条命令的结论；载荷本身不合法的直接退回，不进缓存。
	 *       随后做重放判定，确认是首次之后，准入不通过的把结论写进终态缓存后退回；
	 *       通过的才真正从库里移除、记进已取走表、推进版本并广播变化，让订单重放仍能找回那件实物。
	 * 边界：它不判断谁有权拿、不按类别排队、也不负责装配；装备库只回答"这件东西还在不在库里"。
	 */
	FCatTeamEquipmentGrantResult TakeInstance(const FCatTeamEquipmentTakeCommand& Command);

	/** 按商店订单 ID 找回这笔订单造出的那件实物；已经被取走的也算找得到（订单重放要的是回执，不是库存）。没有入过库时返回 false 并清空输出。 */
	bool TryFindInstanceBySourceTransaction(FGuid SourceTransactionId, FCatTeamEquipmentInstance& OutInstance) const;

	/** 关闭入库写口；调用后新入库一律返回 CommandsClosed，已有内容和重放照常可读。重复调用没有第二次副作用。 */
	void CloseCommands();

	/** 库内容变化的本机通知；只在真实入库后发一次，重放和拒绝都不发。 */
	FCatTeamEquipmentLibraryChanged OnLibraryChanged;

private:
	/**
	 * 声明：取用准入的唯一判据，按"载荷是否完整 → 写口是否还开着 → 这件东西是否还在库里 → 版本前提是否对得上"依次裁定，
	 *       返回 None 时 OutInstanceIndex 是那件实物在 Snapshot.Instances 里的下标，否则是 INDEX_NONE。
	 *       只读预检和真正取走都从这里拿结论，是为了让两条路径永远给出同一个答案——判据一旦分成两份，
	 *       预检说"能取"而写口说"不能取"就会重新造出"东西已经装上、库里却没少"的分叉。
	 * 实现：四项检查按上面的顺序短路返回；下标只在四项全过之后才写出去。
	 * 边界：const 且不广播、不写缓存；它只看当前库存事实，不认识 RequestId 的历史。
	 */
	ECatDomainCommandError EvaluateTakeAdmission(const FCatTeamEquipmentTakeCommand& Command, int32& OutInstanceIndex) const;

	/** 组合服务器身份、操作名与 RequestId 的幂等键；同一个人用同一 RequestId 重试同一种操作才算重放，入库和取用互不混淆。 */
	static FString MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, FGuid RequestId);

	/** 入库载荷签名；同 RequestId 换订单、换定义或换版本前提都不是合法重放。 */
	static FString MakePayloadSignature(const FCatTeamEquipmentGrantCommand& Command);

	/** 本局装备库的唯一内容与版本事实。 */
	FCatTeamEquipmentLibrarySnapshot Snapshot;

	/** 已经被取走、不再出现在 Snapshot 里的实物；只给订单重放按回执找回用，不是第二份库存。 */
	TMap<FGuid, FCatTeamEquipmentInstance> TakenInstanceById;

	/** 商店订单 ID 到已入库实例 ID 的索引；它保证一笔订单在库里只会有一件对应实物。 */
	TMap<FGuid, FGuid> InstanceIdBySourceTransaction;

	/** 入库命令的首次终态缓存；重放返回首次实例但不再创建第二件。 */
	TMap<FString, FCatTeamEquipmentGrantResult> TerminalCache;

	/** 入库命令首次受理时的业务载荷签名；载荷漂移按非法处理。 */
	TMap<FString, FString> TerminalPayloadByKey;

	/** teardown 后关闭新入库；缓存重放仍可读首次终态。 */
	bool bCommandsOpen = true;
};
