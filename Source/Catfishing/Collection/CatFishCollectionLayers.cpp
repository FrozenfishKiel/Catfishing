#include "Collection/CatFishCollectionLayers.h"

#include "Data/CatFishDefinition.h"

// 知识层只随已支持的食用成长开放；读取鱼定义的统一资格，不维护另一套安全或中毒分类。
bool CatFishCollectionLayers::HasKnowledgeLayer(const UCatFishDefinition* Definition)
{
	return Definition && Definition->IsEdible();
}
