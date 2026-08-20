#include "Environment/CatChumFieldAnchor.h"

ACatChumFieldAnchor::ACatChumFieldAnchor()
{
	PrimaryActorTick.bCanEverTick = false; // 纯标记点，不需要每帧更新
	bReplicates = false; // 只作为关卡里的静态锚点使用，不需要网络复制
	SetActorEnableCollision(false); // 不参与碰撞，避免干扰投掷弹道或玩家移动
}
