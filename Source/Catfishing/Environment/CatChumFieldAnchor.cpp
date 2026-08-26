#include "Environment/CatChumFieldAnchor.h"

ACatChumFieldAnchor::ACatChumFieldAnchor()
{
	// 构造流程：关闭 Tick、复制和碰撞，让锚点只作为服务器扫描的关卡数据存在；自然窝点真正写入仍由 GameMode 调用 ChumFieldSubsystem 完成。
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);
}
