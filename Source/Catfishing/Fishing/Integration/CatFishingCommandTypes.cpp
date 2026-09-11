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
