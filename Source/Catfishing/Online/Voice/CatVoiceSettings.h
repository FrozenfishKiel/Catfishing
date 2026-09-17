#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatVoiceSettings.generated.h"

/** 局内距离语音的播放配置；发送偏好仍由 CatGameUserSettings 唯一持久化。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Voice"))
class CATFISHING_API UCatVoiceSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	/** 此半径内为原音量，单位厘米；不随角色模型缩放。 */
	UPROPERTY(Config, EditAnywhere, Category = "Proximity", meta = (ClampMin = "0", Units = "cm"))
	float FullVolumeDistanceCm = 300.0f;

	/** 此距离及以外静音，单位厘米；必须严格大于原音量半径，中间按增益线性衰减。 */
	UPROPERTY(Config, EditAnywhere, Category = "Proximity", meta = (ClampMin = "1", Units = "cm"))
	float SilentDistanceCm = 2000.0f;

	bool IsRangeValid() const;
	/** 角色间世界距离到 [0,1] 增益；无效配置或非有限距离一律静音。 */
	float GetGainForDistance(double DistanceCm) const;
};
