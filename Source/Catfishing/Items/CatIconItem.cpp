#include "Items/CatIconItem.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Texture2D.h"
#include "Net/UnrealNetwork.h"

// 构造流程：图标作为可见子组件，物理体仍是父类唯一盒体；缩小盒子以适合小道具。
ACatIconItem::ACatIconItem()
{
	PickupCollision->SetBoxExtent(FVector(12));
	Icon = CreateDefaultSubobject<UBillboardComponent>(TEXT("ItemIcon")); Icon->SetupAttachment(GetRootComponent());
	Icon->SetHiddenInGame(false); Icon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
// 接收流程：父类成功绑定身份后设置显示定义并立即刷新；失败不更改当前图标。
bool ACatIconItem::InitializeFromInventoryFromAuthority(UCatInventoryItemInstance* Item, int32 Quantity)
{
	if (!Super::InitializeFromInventoryFromAuthority(Item, Quantity)) return false;
	DisplayDefinition = Item->GetItemDefinition(); RefreshIcon(); ForceNetUpdate(); return true;
}
// 复制流程：父类负责世界运动，本类只增加静态图标来源。
void ACatIconItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ThisClass, DisplayDefinition); }
// 图标流程：按最长边归一到二十四厘米，素材分辨率变化不会放大物理物品。
void ACatIconItem::RefreshIcon()
{
	UTexture2D* Texture = DisplayDefinition ? DisplayDefinition->GetInventoryThumbnail().LoadSynchronous() : nullptr;
	Icon->SetSprite(Texture);
	if (Texture) Icon->SetRelativeScale3D(FVector(24.0 / FMath::Max(1, FMath::Max(Texture->GetSizeX(), Texture->GetSizeY()))));
}
