#include "Framework/Game/CatfishingPlayerController.h"
#include "Character/CatCharacter.h"
#include "Data/CatFishDefinition.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Logging/CatLog.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
namespace
{
	// 只接受十进制正整数，逐位检查上界后才累加；负数、小数、溢出和名称都不能被宽松转换成另一个物品。
	bool ParsePositiveItemArgument(const FString& Text, const int32 Maximum, int32& OutValue)
	{
		OutValue = 0;
		if (Text.IsEmpty()) return false;
		for (const TCHAR Character : Text)
		{
			if (Character < TEXT('0') || Character > TEXT('9')) return false;
			const int32 Digit = Character - TEXT('0');
			if (OutValue > (Maximum - Digit) / 10) return false;
			OutValue = OutValue * 10 + Digit;
		}
		return OutValue > 0 && OutValue <= Maximum;
	}

	// 控制台所属 World 只取本地输入者，不按服务器玩家数组索引选人；语法成立后下一帧走本人 Controller 的可靠服务器入口。
	void GiveItemFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		auto* Controller = World ? Cast<ACatfishingPlayerController>(World->GetFirstPlayerController()) : nullptr;
		int32 ItemId = 0, Quantity = 1;
		if (!Controller || !Controller->IsLocalController() || Args.Num() < 1 || Args.Num() > 2
			|| !ParsePositiveItemArgument(Args[0], MAX_int32, ItemId)
			|| (Args.Num() == 2 && !ParsePositiveItemArgument(Args[1], 999, Quantity)))
		{
			UE_LOG(LogCatfishing, Warning, TEXT("Event=item_debug_give_rejected Reason=InvalidArgumentsOrLocalPlayer World=%s Usage=cat.Item.Give_ID_[1..999]"), *GetNameSafe(World));
			if (Controller) Controller->ClientMessage(TEXT("用法：cat.Item.Give <数字ID> [数量1..999]，默认1件"));
			return;
		}
		const FGuid RequestId = FGuid::NewGuid();
		UE_LOG(LogCatfishing, Log, TEXT("Event=item_debug_give_requested RequestId=%s ItemId=%d Quantity=%d Player=%s World=%s NetMode=%d Authority=%d"),
			*RequestId.ToString(), ItemId, Quantity, *Controller->GetName(), *GetNameSafe(World), World->GetNetMode(), Controller->HasAuthority());
		// 编辑器 Python 也会执行控制台命令；退出其脚本保护作用域后再调用 RPC，防止客户端请求被当成本地函数执行。
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(Controller, [Controller, RequestId, ItemId, Quantity]()
		{
			Controller->ServerDebugGiveItem(RequestId, ItemId, Quantity);
		}));
	}

	/** 所有总表物品共用的开发指令；只解析身份和数量，不提供独立的鱼或装备发货入口。 */
	FAutoConsoleCommandWithWorldAndArgs GiveItemCommand(TEXT("cat.Item.Give"),
		TEXT("给予当前角色总表物品：cat.Item.Give <数字ID> [数量1..999]。满包余量掉在角色附近。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GiveItemFromConsole), ECVF_Cheat);
}
#endif

// 服务器重查本人当前 Pawn、总表和数量；普通定义按批次创建，真鱼先补齐独立身份及配置中值重量，再共用正式溢出收货。
// 这里没有上鱼事件，不调用捕获奖励或图鉴登记；成功和失败都回送输入者，请求号仅作日志关联，不把两次调试调用合并。
// Shipping 保留 RPC 签名供反射生成，但不读取资产、不写库存，也不发结果回执。
void ACatfishingPlayerController::ServerDebugGiveItem_Implementation(const FGuid RequestId, const int32 ItemId, const int32 Quantity)
{
#if !UE_BUILD_SHIPPING
	FName Failure;
	auto* TargetCharacter = Cast<ACatCharacter>(GetPawn());
	UCatInventoryItemDefinition* Definition = nullptr;
	if (!RequestId.IsValid() || ItemId <= 0 || Quantity <= 0 || Quantity > 999) Failure = TEXT("InvalidArguments");
	else if (!TargetCharacter || !TargetCharacter->HasAuthority()) Failure = TEXT("NoCharacter");
	else if (!(Definition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(ItemId))) Failure = TEXT("UnknownOrInvalidItem");
	FCatInventoryReceiveBatch Batch;
	if (Failure.IsNone())
	{
		if (auto* Fish = Cast<UCatFishDefinition>(Definition))
		{
			const FString OwnerId = PlayerState && PlayerState->GetUniqueId().IsValid() ? PlayerState->GetUniqueId()->ToString() : FString();
			const double Weight = Fish->MinimumWeightKilograms * 0.5 + Fish->MaximumWeightKilograms * 0.5;
			const auto InstanceClass = UCatInventoryItemDefinition::ResolveItemInstanceClass(Fish, nullptr);
			if (OwnerId.IsEmpty() || !InstanceClass || !InstanceClass->IsChildOf(UCatFishInventoryItemInstance::StaticClass())
				|| !FMath::IsFinite(Weight) || Weight <= 0.0) Failure = TEXT("InvalidFishInitialization");
			else for (int32 Index = 0; Index < Quantity; ++Index)
			{
				auto* Instance = NewObject<UCatFishInventoryItemInstance>(TargetCharacter, InstanceClass);
				Instance->SetItemDefinition(Fish);
				Instance->SetRuntimeOwnerActor(TargetCharacter);
				if (!Instance->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), OwnerId, Weight))
				{ Failure = TEXT("InvalidFishInitialization"); break; }
				Batch.InstanceEntries.Add({1, Instance});
			}
		}
		else Batch.DefinitionEntries.Add({Quantity, Definition, nullptr});
		if (Failure.IsNone() && !UCatInventoryStatics::ReceiveInventoryWithOverflowFromAuthority(TargetCharacter, Batch)) Failure = TEXT("ReceiveFailed");
	}
	UE_LOG(LogCatfishing, Log, TEXT("Event=item_debug_give_result RequestId=%s ItemId=%d Quantity=%d Player=%s Actor=%s World=%s NetMode=%d Authority=%d Result=%s"),
		*RequestId.ToString(), ItemId, Quantity, *GetName(), *GetNameSafe(TargetCharacter), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(),
		Failure.IsNone() ? TEXT("Granted") : *Failure.ToString());
	ClientDebugGiveItemResult(RequestId, ItemId, Quantity, UCatInventoryItemDefinition::GetPlayerFacingName(Definition), Failure);
	UE_CLOG(!Failure.IsNone(), LogCatfishing, Warning, TEXT("Event=item_debug_give_rejected RequestId=%s ItemId=%d Quantity=%d Player=%s World=%s NetMode=%d Reason=%s"),
		*RequestId.ToString(), ItemId, Quantity, *GetName(), *GetNameSafe(GetWorld()), GetNetMode(), *Failure.ToString());
#endif
}

// 仅在命令输入者本机输出服务器结果，不据此再次入库；保留请求号和数字 ID 方便核对双端日志。
void ACatfishingPlayerController::ClientDebugGiveItemResult_Implementation(const FGuid RequestId, const int32 ItemId,
	const int32 Quantity, const FText& ItemName, const FName FailureReason)
{
#if !UE_BUILD_SHIPPING
	const FString Message = FString::Printf(TEXT("%s：%s（ID %d）× %d%s"),
		FailureReason.IsNone() ? TEXT("已给予") : TEXT("给予失败"), *ItemName.ToString(), ItemId, Quantity,
		FailureReason.IsNone() ? TEXT("") : *FString::Printf(TEXT("，原因：%s"), *FailureReason.ToString()));
	ClientMessage(Message);
	UE_LOG(LogCatfishing, Log, TEXT("Event=item_debug_give_received RequestId=%s ItemId=%d Quantity=%d Player=%s World=%s NetMode=%d Result=%s"),
		*RequestId.ToString(), ItemId, Quantity, *GetName(), *GetNameSafe(GetWorld()), GetNetMode(), *Message);
#endif
}
