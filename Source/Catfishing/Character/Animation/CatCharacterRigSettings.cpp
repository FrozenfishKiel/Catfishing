#include "Character/Animation/CatCharacterRigSettings.h"
#include "Animation/AnimSequence.h"

FCatCharacterRigSettings::FCatCharacterRigSettings()
{
	for (const TCHAR* Prefix : {TEXT("RigLFLeg"), TEXT("RigRFLeg"), TEXT("RigLBLeg"), TEXT("RigRBLeg")})
	{
		FCatCharacterLimbChain& Chain = Feet.AddDefaulted_GetRef();
		for (int32 Joint=1; Joint<=3; ++Joint) Chain.Bones.Add(FName(FString::Printf(TEXT("%s%d"), Prefix, Joint)));
		Chain.Bones.Add(FName(FString(Prefix) + TEXT("Ankle")));
	}
	StandingAnimations.Add(TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Animalia/Cat/Animations/InPlace/Stand_00-IP.Stand_00-IP"))));
	LocomotionAnimations.Add(TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Animalia/Cat/Animations/InPlace/Loco_Walk-IP.Loco_Walk-IP"))));
	LocomotionAnimations.Add(TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Animalia/Cat/Animations/InPlace/Loco_Run-IP.Loco_Run-IP"))));
}
