#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatWorldInfoRegistry.generated.h"

class UCatWorldInfoComponent;

/** 本世界信息锚点的弱引用目录；注册不创建 UI，也不保存任何玩家的显隐结果。 */
UCLASS()
class CATFISHING_API UCatWorldInfoRegistry : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	/** 信息组件进入世界时登记一次，供之后加入的本地玩家发现。 */
	void RegisterSource(UCatWorldInfoComponent* Source);
	/** 组件离开世界时移除；其他玩家不再拿到失效的信息源。 */
	void UnregisterSource(UCatWorldInfoComponent* Source);
	/** 本地控制器发现信息源时读取当前目录；返回弱引用数组的只读引用，调用方跳过失效项，不借此取得 Actor 所有权。 */
	const TArray<TWeakObjectPtr<UCatWorldInfoComponent>>& GetSources() const;
private:
	/** 当前世界的非拥有锚点目录；组件通过注册和注销入口成对写入，本地控制器读取以发现源，World 销毁时释放。 */
	TArray<TWeakObjectPtr<UCatWorldInfoComponent>> Sources;
};
