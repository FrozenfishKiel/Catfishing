#include "Fishing/Integration/CatFishingCommandTypes.h"

// 领域错误映射流程：保留钓鱼 UI 已裁的细分错误码；抄网几何/策略谓词失败必须保留为 ScoopGeometryFailed，
// 避免打包日志把“角色不在岸上、射线未命中”等可定位拒绝误报为系统依赖缺失。
ECatFishingCommandError MapDomainCommandError(const ECatDomainCommandError Error)
{
	switch (Error)
	{
	case ECatDomainCommandError::None: return ECatFishingCommandError::None;
	case ECatDomainCommandError::InvalidPayload: return ECatFishingCommandError::InvalidPayload;
	case ECatDomainCommandError::InvalidIdentity: return ECatFishingCommandError::InvalidIdentity;
	case ECatDomainCommandError::InvalidPhase: return ECatFishingCommandError::InvalidPhase;
	case ECatDomainCommandError::NotFound: return ECatFishingCommandError::SessionNotFound;
	case ECatDomainCommandError::RevisionConflict: return ECatFishingCommandError::RevisionConflict;
	case ECatDomainCommandError::AlreadyResolved: return ECatFishingCommandError::AlreadyResolved;
	case ECatDomainCommandError::CommandsClosed: return ECatFishingCommandError::CommandsClosed;
	case ECatDomainCommandError::CapacityExceeded: return ECatFishingCommandError::GuardCapacityExceeded;
	case ECatDomainCommandError::PolicyUndecided: return ECatFishingCommandError::ScoopGeometryFailed;
	default: return ECatFishingCommandError::DependencyUnavailable;
	}
}

// 抢抄拒绝原因映射流程：把服务器裁出的六个细分原因换成命令错误码；None 表示本次拒绝不是可抄几何，
// 调用方必须保留 MapDomainCommandError 给出的原错误（阶段错、版本冲突、依赖缺失等），不要改写成几何失败。
ECatFishingCommandError MapScoopRejectReason(const ECatScoopRejectReason Reason)
{
	switch (Reason)
	{
	case ECatScoopRejectReason::OutOfReach: return ECatFishingCommandError::ScoopOutOfReach;
	case ECatScoopRejectReason::LineOfSightBlocked: return ECatFishingCommandError::ScoopLineOfSightBlocked;
	case ECatScoopRejectReason::GroundTooSteep: return ECatFishingCommandError::ScoopGroundTooSteep;
	case ECatScoopRejectReason::VerticalDeltaTooLarge: return ECatFishingCommandError::ScoopVerticalDeltaTooLarge;
	case ECatScoopRejectReason::MouthOccupied: return ECatFishingCommandError::ScoopMouthOccupied;
	case ECatScoopRejectReason::NotOnShore: return ECatFishingCommandError::ScoopNotOnShore;
	default: return ECatFishingCommandError::None;
	}
}

// 玩家提示映射流程：只回答「这句话给玩家看」，不承担日志与排查。四种几何原因同一句「没够着」是设计要求
// （钓鱼规则 §5.5:273 三条拒绝条件「任一不满足都判没够着」，加上射线本身没够着），
// 细分只保留在错误码与服务器日志里，玩家不需要知道是坡度还是高差。
FText CatFishingCommandFeedback::GetPlayerFacingText(const ECatFishingCommandError Error)
{
	switch (Error)
	{
	case ECatFishingCommandError::ScoopOutOfReach:
	case ECatFishingCommandError::ScoopLineOfSightBlocked:
	case ECatFishingCommandError::ScoopGroundTooSteep:
	case ECatFishingCommandError::ScoopVerticalDeltaTooLarge:
	case ECatFishingCommandError::ScoopGeometryFailed:
		return NSLOCTEXT("Catfishing", "ScoopMissedReach", "没够着");
	case ECatFishingCommandError::ScoopMouthOccupied:
		return NSLOCTEXT("Catfishing", "ScoopMouthOccupied", "嘴里叼着鱼，抄不了");
	case ECatFishingCommandError::ScoopNotOnShore:
		return NSLOCTEXT("Catfishing", "ScoopNotOnShore", "得站在岸上抄");
	case ECatFishingCommandError::CooldownActive:
		return NSLOCTEXT("Catfishing", "ScoopStillNumb", "爪子还在麻");
	case ECatFishingCommandError::CastOutOfRange:
	case ECatFishingCommandError::InvalidWaterTarget:
		return NSLOCTEXT("Catfishing", "CastOutOfReach", "够不到那边");
	case ECatFishingCommandError::RodDeploymentLimitReached:
		return NSLOCTEXT("Catfishing", "RodDeploymentLimit", "场上鱼竿已达上限，请先收起一根。");
	// 换人接手的体力门槛（多人钓鱼附篇 §2.4）：告诉他是体力不够，否则按了没反应会以为键坏了。
	// HandoffNotRequested 不给文案：那多半只是误按，没有人在等接手的时候不该弹提示。
	case ECatFishingCommandError::HandoffStaminaTooLow:
		return NSLOCTEXT("Catfishing", "HandoffStaminaTooLow", "体力还没缓过来，接不了竿");
	default:
		return FText::GetEmpty();
	}
}
