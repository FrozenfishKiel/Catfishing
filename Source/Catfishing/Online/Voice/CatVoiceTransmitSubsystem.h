#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Interfaces/VoiceInterface.h"
#include "Tickable.h"
#include "CatVoiceTransmitSubsystem.generated.h"

UENUM()
enum class ECatVoiceInputMode : uint8
{
	Disabled,
	AlwaysOn,
	PushToTalk
};

/** 本机发送状态只归本地玩家所有；模式由 GameUserSettings 持久化，不复制按键或远端播放状态。 */
UCLASS()
class CATFISHING_API UCatVoiceTransmitSubsystem : public ULocalPlayerSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
public:
	void Configure(UWorld* World, ECatVoiceInputMode Mode);
	void Suspend(FName Reason);
	void SetPushToTalkHeld(bool bHeld);
	void CancelHeldInput(FName Reason);
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return !IsTemplate() && !bDeinitialized; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCatVoiceTransmitSubsystem, STATGROUP_Tickables); }
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }

private:
	friend class FCatVoiceTransmitLifecycleTest;
	bool IsContextAllowed() const;
	void Reconcile(bool bContextAllowed, FName Reason);
	void Dispatch(bool bEnable, FName Reason, bool bForce = false);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	TWeakObjectPtr<UWorld> ConfiguredWorld;
	TWeakObjectPtr<UWorld> DepartingWorld;
	TWeakObjectPtr<APlayerController> ObservedController;
	TWeakObjectPtr<UWorld> ObservedWorld;
	IOnlineVoicePtr ActiveVoice;
	int32 ConfiguredLocalUser = INDEX_NONE;
	ECatVoiceInputMode InputMode = ECatVoiceInputMode::Disabled;
	bool bReady = false;
	bool bHeld = false;
	bool bSending = false;
	bool bDeinitialized = false;
	FDelegateHandle WorldCleanupHandle;
};
