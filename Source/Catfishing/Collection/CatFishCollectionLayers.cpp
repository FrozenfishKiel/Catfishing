#include "Collection/CatFishCollectionLayers.h"

#include "Data/CatFishDefinition.h"

// 知识层存在性：只读鱼表已裁决的食用结论，不按鱼名猜。
// Inedible＝已裁「这条鱼根本不能吃」（咸鱼、湖心巨影），它没有知识层——页面上连「吃鱼效果」这一栏都不存在；
// Unset＝还没填，同样不给这一层（fail-closed，吃鱼链本身对 Unset 也是拒绝的）；
// Safe / SevereToxic 都有这一层，毒鱼照样吃得出知识（这正是猫册毒鱼防盗陷阱成立的前提）。
bool CatFishCollectionLayers::HasKnowledgeLayer(const UCatFishDefinition* Definition)
{
	return Definition != nullptr
		&& (Definition->FoodSafety == ECatFishFoodSafety::Safe
			|| Definition->FoodSafety == ECatFishFoodSafety::SevereToxic);
}
