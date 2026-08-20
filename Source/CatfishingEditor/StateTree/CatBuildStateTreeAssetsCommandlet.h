#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CatBuildStateTreeAssetsCommandlet.generated.h"

/**
 * 用 C++ 重建并落盘项目的两棵 StateTree 资产（ST_RunFlow、ST_FishingSession）的 Editor/CI 入口。
 * 拓扑写死在本 Commandlet 里，State/Task/Transition 的 ID 全部按稳定字符串派生，因此重复运行得到同一份资产；
 * 编译失败、状态/转移数与预期不符、落盘失败都以非零退出码暴露。加 -verify 时只从磁盘加载并复核，不重建。
 */
UCLASS()
class UCatBuildStateTreeAssetsCommandlet final : public UCommandlet
{
	GENERATED_BODY()

public:
	/** 配置无 world 的 Editor 命令行环境，并把 Main 的返回值直接作为进程退出码交给 CI。 */
	UCatBuildStateTreeAssetsCommandlet();

	/** 重建两棵树、编译、自检并保存到 Content/Catfishing/StateTree/；全部成功返回 0，任一环节失败返回 1。带 -verify 时仅加载磁盘资产复核。 */
	virtual int32 Main(const FString& Params) override;
};
