#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Equipment/CatEquipmentTypes.h"
#include "CatTeamEquipmentLibrary.generated.h"

DECLARE_MULTICAST_DELEGATE(FCatTeamEquipmentLibraryChanged);

/**
 * 服务器唯一的局内团队装备库。商店只创建订单，这里才创建装备实例；角色取走实例后，
 * 后续装配继续交给现有 EquipmentComponent，不在此处维护第二套角色装备状态。
 */
UCLASS()
class CATFISHING_API UCatTeamEquipmentLibrary : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	const FCatTeamEquipmentLibrarySnapshot& GetSnapshot() const;
	ECatDomainCommandError ValidateShopOrderGrant(FName DefinitionId) const;
	FCatTeamEquipmentGrantResult GrantFromShopOrder(const FCatTeamEquipmentGrantCommand& Command);
	ECatDomainCommandError ValidateTake(const FCatTeamEquipmentTakeCommand& Command,
		FCatTeamEquipmentInstance& OutInstance) const;
	FCatTeamEquipmentGrantResult TakeInstance(const FCatTeamEquipmentTakeCommand& Command);
	bool TryFindInstanceBySourceTransaction(FGuid SourceTransactionId,
		FCatTeamEquipmentInstance& OutInstance) const;
	void CloseCommands();

	FCatTeamEquipmentLibraryChanged OnLibraryChanged;

private:
	ECatDomainCommandError EvaluateTakeAdmission(const FCatTeamEquipmentTakeCommand& Command,
		int32& OutInstanceIndex) const;
	static FString MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, FGuid RequestId);
	static FString MakePayloadSignature(const FCatTeamEquipmentGrantCommand& Command);

	FCatTeamEquipmentLibrarySnapshot Snapshot;
	TMap<FGuid, FCatTeamEquipmentInstance> TakenInstanceById;
	TMap<FGuid, FGuid> InstanceIdBySourceTransaction;
	TMap<FString, FCatTeamEquipmentGrantResult> TerminalCache;
	TMap<FString, FString> TerminalPayloadByKey;
	bool bCommandsOpen = true;
};
