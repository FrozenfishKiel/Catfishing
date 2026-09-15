#pragma once

#include "CoreMinimal.h"

class UCatFishDefinition;

/**
 * 图鉴分层里「这条鱼有没有这一层」的唯一判据（图鉴 §3.1.4:122「各层字段按鱼配置」）。
 * 09-08 裁：不可食用的咸鱼与湖心巨影没有知识层，它们的页面只有线索层与收集层，
 * 也不留「待解锁」空位——所以这里回答的是「有没有」，不是「解没解锁」。
 */
namespace CatFishCollectionLayers
{
	/**
	 * 该鱼是否存在知识层。判据就是鱼表已裁的食用结论列：Inedible（咸鱼、湖心巨影）与 Unset 都没有这一层。
	 * 鱼表不需要第二列「有无知识层」——能不能吃和有没有吃鱼效果是同一件事；
	 * 吃鱼链本身对这两档也是 fail-closed（UCatConditionComponent::ValidateFishConsumption），两处判据同源。
	 */
	CATFISHING_API bool HasKnowledgeLayer(const UCatFishDefinition* Definition);
}
