#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CatCarryableActor.generated.h"

/** 可以在物理掉落和角色附着之间切换的世界实物。客户端仅还原服务器移动与附件，不从库存或表现状态推导携带者。 */
UCLASS(Abstract)
class CATFISHING_API ACatCarryableActor : public AActor
{
	GENERATED_BODY()

protected:
	/** 延后附件通知的应用，确保本批移动通知已处理；派生类不能重新引入先附着后停止物理的路径。 */
	virtual void OnRep_AttachmentReplication() override final;
	/** 本批复制及未解析引用补齐后统一收敛物理和附件，再通知派生类刷新自身表现；不维护第二份携带状态。 */
	virtual void PostRepNotifies() override final;
	/** 共同同步完成后的表现扩展点；派生类可改网格姿态和碰撞，但不得重新裁决根附件或物理模拟。强制刷新用于自身表现资源刚更新时。 */
	virtual void RefreshCarryPresentation(bool bForceRefresh);
};
