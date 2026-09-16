#include "Equipment/Fragments/CatEquipmentFragment_Bait.h"

bool UCatEquipmentFragment_Bait::IsRuntimeReady() const
{
    // 废弃的等待倍率不能继续拒绝正式鱼饵；偏好矩阵由鱼目录单独校验。
    return Super::IsRuntimeReady();
}
