#pragma once

#include "CoreMinimal.h"
#include "Net/VoiceConfig.h"
#include "CatProximityVoiceComponent.generated.h"

/** 每个 PlayerState 的唯一接收语音表现；复用 OSS 解码音频，不采集、不发送、不复制玩法状态。 */
UCLASS()
class CATFISHING_API UCatProximityVoiceComponent : public UVOIPTalker
{
	GENERATED_BODY()
public:
	UCatProximityVoiceComponent(const FObjectInitializer& ObjectInitializer);
	/** PlayerState 身份就绪/更换时调用；先注册接收器，即使 Pawn 晚到也能静音而非全图播放。 */
	void RefreshPlayerBinding();
	virtual void OnTalkingBegin(UAudioComponent* AudioComponent) override;
	virtual void OnTalkingEnd() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	friend class FCatProximityVoiceLifecycleTest;
	UFUNCTION()
	void HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn);
	void UpdatePlayback();
	void MuteAndReleasePlayback();
	void LogEvent(const TCHAR* Event, const TCHAR* Result) const;

	/** 只负责空间方向；距离增益按双方 Pawn 计算，避免相机距离影响语音或改动全局监听器。 */
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> SpatialSettings;
	/** 稳定的播放附着点随 PlayerState 生存，逐帧跟随当前 Pawn；换 Pawn 或语音空闲时不解除引擎复用音频的附着。 */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> VoiceSource;
	TWeakObjectPtr<UAudioComponent> Playback;
	TWeakObjectPtr<USceneComponent> BoundSource;
	FUniqueNetIdRepl RegisteredId;
	/** 同一进程 PIE 的引擎 TalkerMap 按账号而非 World 索引；发生冲突时明确拒绝跨 World 抢占。 */
	bool bRegistrationConflict = false;
	bool bEndingPlay = false;
	bool bStreamActive = false;
	bool bWasAudible = false;
};
