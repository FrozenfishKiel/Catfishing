#pragma once

#include "CoreMinimal.h"

/** 只控制落盘采样，不参与随机、复制、受力或资源结算。 */
namespace CatFishingMotionDiagnostics
{
	CATFISHING_API bool IsDetailedEnabled();
	/** 运动帧最多采样 60 Hz；关闭详细诊断时恢复原 1 Hz。 */
	CATFISHING_API double SampleIntervalSeconds();
}
