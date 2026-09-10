#include "CatCharacterVariantAuthoringLibrary.h"

#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Components/SceneComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UnrealType.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_AssetPlayerBase.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicsPrototypeVisualComponent.h"
#include "Condition/CatConditionPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMeshSocket.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

FString UCatCharacterVariantAuthoringLibrary::InspectBlueprint(const FString& AssetPath)
{
	const UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!BP) return TEXT("ERROR: Missing Blueprint");
	FString Report = FString::Printf(TEXT("Blueprint=%s Parent=%s\n"), *BP->GetPathName(), *GetPathNameSafe(BP->ParentClass));
	for (const FBPVariableDescription& Variable : BP->NewVariables)
		Report += FString::Printf(TEXT("Variable=%s Type=%s Default=%s\n"), *Variable.VarName.ToString(), *Variable.VarType.PinCategory.ToString(), *Variable.DefaultValue);
	if (BP->SimpleConstructionScript)
		for (const USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
		{
			Report += FString::Printf(TEXT("Component=%s Class=%s Parent=%s Socket=%s\n"), *Node->GetVariableName().ToString(), *GetPathNameSafe(Node->ComponentClass), *Node->ParentComponentOrVariableName.ToString(), *Node->AttachToName.ToString());
			if (const auto* Scene = Cast<USceneComponent>(Node->ComponentTemplate))
				Report += FString::Printf(TEXT(" Transform=%s\n"), *Scene->GetRelativeTransform().ToHumanReadableString());
		}
	TArray<UEdGraph*> Graphs;
	BP->GetAllGraphs(Graphs);
	for (const UEdGraph* Graph : Graphs)
	{
		Report += FString::Printf(TEXT("Graph=%s Nodes=%d\n"), *Graph->GetName(), Graph->Nodes.Num());
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			Report += FString::Printf(TEXT(" Node=%s Class=%s Title=%s\n"), *Node->GetName(), *Node->GetClass()->GetName(), *Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Replace(TEXT("\n"),TEXT(" / ")));
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin->DefaultObject || (!Pin->DefaultValue.IsEmpty() && Pin->LinkedTo.IsEmpty()))
					Report += FString::Printf(TEXT("  Pin=%s Value=%s Object=%s\n"), *Pin->PinName.ToString(), *Pin->DefaultValue, *GetPathNameSafe(Pin->DefaultObject));
				for (const UEdGraphPin* Link : Pin->LinkedTo)
					Report += FString::Printf(TEXT("  Link=%s -> %s.%s\n"), *Pin->PinName.ToString(), *Link->GetOwningNode()->GetName(), *Link->PinName.ToString());
			}
		}
	}
	if (BP->GeneratedClass)
	{
		const UObject* CDO = BP->GeneratedClass->GetDefaultObject();
		for (TFieldIterator<FProperty> It(BP->GeneratedClass); It; ++It)
			if (It->GetOwnerClass() == BP->GeneratedClass)
			{
				FString Value;
				It->ExportText_InContainer(0, Value, CDO, nullptr, nullptr, PPF_None);
				Report += FString::Printf(TEXT("Default=%s Value=%s\n"), *It->GetName(), *Value);
			}
	}
	return Report;
}

FString UCatCharacterVariantAuthoringLibrary::NormalizeCuteCatRetargetedAnimations()
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/CuteCat/Meshes/SK_CuteCat"));
	if (!Mesh) return TEXT("ERROR: Missing target mesh");
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const int32 Pelvis = Ref.FindBoneIndex(TEXT("Center_001"));
	const int32 Head = Ref.FindBoneIndex(TEXT("Head_001"));
	if (Pelvis == INDEX_NONE || Head == INDEX_NONE || !Ref.GetRefBonePose()[0].GetScale3D().Equals(FVector(100), 0.001))
		return TEXT("ERROR: CuteCat reference skeleton changed");
	TArray<FAssetData> Assets;
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByPath(
		TEXT("/Game/Characters/CuteCat/Animation/Retargeted"), Assets, true);
	int32 Changed=0;
	FString Report;
	for (const FAssetData& Asset : Assets)
	{
		UAnimSequence* Sequence = Cast<UAnimSequence>(Asset.GetAsset());
		if (!Sequence) continue;
		const IAnimationDataModel* Model = Sequence->GetDataModel();
		const FTransform Root = Model->GetBoneTrackTransform(Ref.GetBoneName(0), FFrameNumber(0));
		Report += FString::Printf(TEXT("Animation=%s ExportedRoot=%s TargetRoot=%s\n"), *Sequence->GetName(),
			*Root.ToString(), *Ref.GetRefBonePose()[0].ToString());
		const bool bRawExport = Root.GetScale3D().Equals(FVector::OneVector, 0.001);
		const bool bNormalizedRoot = Root.GetScale3D().Equals(FVector(100), 0.001);
		const FVector HeadPosition = Model->GetBoneTrackTransform(Ref.GetBoneName(Head), FFrameNumber(0)).GetTranslation();
		const FVector RefHeadPosition = Ref.GetRefBonePose()[Head].GetTranslation();
		const bool bLegacyCollapsed = bNormalizedRoot && HeadPosition.Equals(RefHeadPosition * 0.01, 0.00001);
		if ((!bRawExport && !bNormalizedRoot) || (!bLegacyCollapsed && !HeadPosition.Equals(RefHeadPosition, 0.0001)))
			return TEXT("ERROR: Unrecognized retarget translation convention; preserve asset for review\n") + Report;
		const bool bLean = Sequence->GetName() == TEXT("Add_Neutral") || Sequence->GetName() == TEXT("Add_Loco_Left") || Sequence->GetName() == TEXT("Add_Loco_Right");
		const bool bNeutral = Sequence->GetName() == TEXT("Add_Neutral");
		bool bChanged = false;
		TArray<TArray<FVector>> Positions, Scales;
		TArray<TArray<FQuat>> Rotations;
		Positions.SetNum(Ref.GetNum()); Scales.SetNum(Ref.GetNum()); Rotations.SetNum(Ref.GetNum());
		for (int32 Frame=0; Frame<Model->GetNumberOfKeys(); ++Frame)
		{
			for (int32 Bone=0; Bone<Ref.GetNum(); ++Bone)
			{
				const FTransform Local = Model->GetBoneTrackTransform(Ref.GetBoneName(Bone), FFrameNumber(Frame));
				const FTransform& Reference = Ref.GetRefBonePose()[Bone];
				FTransform RebasedLocal = Local;
				// UE 5.8 strips scale from both retarget reference poses, but only rebakes the
				// pelvis local translation. Other local offsets remain in imported bone units.
				// Converting the whole exported global pose therefore collapses them by 100x.
				if (bRawExport && Bone == Pelvis) RebasedLocal.ScaleTranslation(0.01);
				if (bLegacyCollapsed && Bone != 0 && Bone != Pelvis) RebasedLocal.ScaleTranslation(100.0);
				RebasedLocal.SetScale3D(Reference.GetScale3D());
				if (Bone != 0 && Bone != Pelvis && !RebasedLocal.GetTranslation().Equals(Reference.GetTranslation(), 0.0002))
					return FString::Printf(TEXT("ERROR: Unexpected animated FK translation Animation=%s Bone=%s Frame=%d"), *Sequence->GetName(), *Ref.GetBoneName(Bone).ToString(), Frame);
				// A lean is a rotation offset, not a second body pose. Its zero sample must
				// evaluate to additive identity, including the retargeted pelvis offset.
				if (bLean) RebasedLocal.SetTranslation(Reference.GetTranslation());
				if (bNeutral) RebasedLocal.SetRotation(Reference.GetRotation().GetNormalized());
				if (RebasedLocal.ContainsNaN()) return TEXT("ERROR: Invalid normalized retarget transform");
				// Float rotation-channel round trips introduce ~1e-6 error on eyelid bones.
				bChanged |= !RebasedLocal.Equals(Local, 0.00001);
				Positions[Bone].Add(RebasedLocal.GetTranslation());
				Rotations[Bone].Add(RebasedLocal.GetRotation().GetNormalized());
				Scales[Bone].Add(RebasedLocal.GetScale3D());
			}
		}
		if (!bChanged) continue;
		IAnimationDataController& Controller = Sequence->GetController();
		Controller.OpenBracket(FText::FromString(TEXT("Restore CuteCat local proportions and neutral additive pose")), false);
		for (int32 Bone=0; Bone<Ref.GetNum(); ++Bone)
			Controller.SetBoneTrackKeys(Ref.GetBoneName(Bone), Positions[Bone], Rotations[Bone], Scales[Bone], false);
		Controller.CloseBracket(false);
		Sequence->MarkPackageDirty();
		UE_LOG(LogTemp, Display, TEXT("Event=character_retarget_proportions_repaired Animation=%s RawExport=%d LegacyCollapsed=%d RotationOnlyLean=%d Result=ReferenceBoneUnits"),
			*Sequence->GetPathName(), bRawExport, bLegacyCollapsed, bLean);
		++Changed;
	}
	Report += FString::Printf(TEXT("Event=character_retarget_scale_normalized Changed=%d"), Changed);
	return Report;
}

FString UCatCharacterVariantAuthoringLibrary::InspectSkeleton(const FString& AssetPath)
{
	const USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *AssetPath);
	if (!Mesh) return TEXT("ERROR: Missing Mesh");
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	TArray<FTransform> Pose;
	Pose.SetNum(Ref.GetNum());
	FString Report = FString::Printf(TEXT("Mesh=%s Bones=%d\n"), *Mesh->GetPathName(), Ref.GetNum());
	for (int32 Index=0; Index<Ref.GetNum(); ++Index)
	{
		const int32 Parent = Ref.GetParentIndex(Index);
		Pose[Index] = Parent == INDEX_NONE ? Ref.GetRefBonePose()[Index] : Ref.GetRefBonePose()[Index] * Pose[Parent];
		Report += FString::Printf(TEXT("Bone=%s Parent=%s Position=%s Scale=%s\n"), *Ref.GetBoneName(Index).ToString(), Parent == INDEX_NONE ? TEXT("None") : *Ref.GetBoneName(Parent).ToString(), *Pose[Index].GetTranslation().ToString(), *Pose[Index].GetScale3D().ToString());
	}
	return Report;
}

namespace CatVariantAuthoring
{
	static bool Save(UObject* Asset)
	{
		Asset->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Asset->GetPackage(), Asset,
			*FPackageName::LongPackageNameToFilename(Asset->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
	}
	static bool Compile(UBlueprint* BP)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
		return BP->Status != BS_Error && BP->GeneratedClass;
	}
	static void ClearLocalGraphs(UBlueprint* BP)
	{
		TArray<UEdGraph*> Roots;
		Roots.Append(BP->UbergraphPages);
		Roots.Append(BP->FunctionGraphs);
		Roots.Append(BP->MacroGraphs);
		FBlueprintEditorUtils::RemoveGraphs(BP, Roots);
		BP->NewVariables.Empty();
	}
	static UBlueprint* Child(UBlueprint* Parent, const FString& Path, bool bAnimation)
	{
		UBlueprint* Result = FKismetEditorUtilities::CreateBlueprint(Parent->GeneratedClass, CreatePackage(*Path),
			FName(FPackageName::GetLongPackageAssetName(Path)), BPTYPE_Normal,
			bAnimation ? UAnimBlueprint::StaticClass() : UBlueprint::StaticClass(),
			bAnimation ? UAnimBlueprintGeneratedClass::StaticClass() : UBlueprintGeneratedClass::StaticClass());
		FAssetRegistryModule::AssetCreated(Result);
		return Result;
	}
	static void SetObject(UObject* Owner, FName Name, UObject* Value)
	{
		if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Owner->GetClass(), Name))
			Property->SetObjectPropertyValue_InContainer(Owner, Value);
	}
}

FString UCatCharacterVariantAuthoringLibrary::CreateCharacterFamily()
{
	using namespace CatVariantAuthoring;
#define REQUIRE_VARIANT(Condition, Message) if (!(Condition)) return TEXT("ERROR: ") Message
	const FString CuteRoot = TEXT("/Game/Characters/CuteCat");
	const FString TemplatePath = TEXT("/Game/Character/Animation/ABPT_CatCharacterBase");
	const FString BasePath = TEXT("/Game/Character/BP_CatCharacterBase");
	const FString CuteBPPath = TEXT("/Game/Character/BP_CuteCatCharacter");
	const FString CuteABPPath = CuteRoot + TEXT("/Animation/ABP_CuteCat");
	const FString BlendPath = CuteRoot + TEXT("/Animation/BS_CuteCat_WalkToRun");
	for (const FString& Path : {TemplatePath, BasePath, CuteBPPath, CuteABPPath, BlendPath})
		REQUIRE_VARIANT(!FPackageName::DoesPackageExist(Path), "Output already exists; refusing to overwrite a character family");
	UBlueprint* OldCharacter = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Character/BP_CatCharacter"));
	UAnimBlueprint* OldAnim = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Animalia/Cat/ABP_Cat"));
	USkeletalMesh* CuteMesh = LoadObject<USkeletalMesh>(nullptr, *(CuteRoot + TEXT("/Meshes/SK_CuteCat")));
	UBlendSpace* OldBlend = LoadObject<UBlendSpace>(nullptr, TEXT("/Game/Animalia/Cat/WalkToRun"));
	REQUIRE_VARIANT(OldCharacter && OldAnim && CuteMesh && OldBlend, "Missing audited source assets");
	REQUIRE_VARIANT(OldCharacter->ParentClass == ACatCharacter::StaticClass(), "Unexpected original character parent");
	USkeleton* OldSkeleton = OldAnim->TargetSkeleton;
	USkeleton* CuteSkeleton = CuteMesh->GetSkeleton();
	ACatCharacter* OldCDO = CastChecked<ACatCharacter>(OldCharacter->GeneratedClass->GetDefaultObject());
	USkeletalMesh* OldMesh = OldCDO->GetMesh()->GetSkeletalMeshAsset();
	const FTransform MeshTransform = OldCDO->GetMesh()->GetRelativeTransform();
	TMap<FString, UAnimationAsset*> NewAnimations;
	auto NativeClip = [&CuteRoot](const TCHAR* Name) { return LoadObject<UAnimSequence>(nullptr,
		*(CuteRoot + TEXT("/Meshes/SK_CuteCat_Anim_Armature_") + Name)); };
	UAnimSequence* Idle = NativeClip(TEXT("idle_A_0"));
	UAnimSequence* Walk = NativeClip(TEXT("walk_A_0"));
	UAnimSequence* Run = NativeClip(TEXT("run_A_0"));
	REQUIRE_VARIANT(Idle && Walk && Run, "Missing imported locomotion animations");
	NewAnimations.Add(TEXT("Stand_00-IP"), Idle);
	NewAnimations.Add(TEXT("Loco_Walk-IP"), Walk);
	NewAnimations.Add(TEXT("Loco_Run-IP"), Run);
	TArray<FAssetData> Retargeted;
	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	Registry.Get().ScanPathsSynchronous({CuteRoot + TEXT("/Animation/Retargeted")}, true);
	Registry.Get().GetAssetsByPath(FName(CuteRoot + TEXT("/Animation/Retargeted")), Retargeted, true);
	for (const FAssetData& Asset : Retargeted)
		if (UAnimationAsset* Animation = Cast<UAnimationAsset>(Asset.GetAsset()))
		{
			REQUIRE_VARIANT(Animation->GetSkeleton() == CuteSkeleton, "Retargeted animation has wrong skeleton");
			NewAnimations.Add(Animation->GetName(), Animation);
		}
	REQUIRE_VARIANT(NewAnimations.Contains(TEXT("Comp_Add_Lean")) && NewAnimations.Contains(TEXT("JumpX_Start-IP")), "Missing retargeted action assets");
	IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UBlendSpace* CuteBlend = CastChecked<UBlendSpace>(Tools.DuplicateAsset(FPackageName::GetLongPackageAssetName(BlendPath),
		FPackageName::GetLongPackagePath(BlendPath), OldBlend));
	CuteBlend->SetSkeleton(CuteSkeleton);
	for (int32 Index=0; Index<CuteBlend->GetBlendSamples().Num(); ++Index)
	{
		const UAnimSequence* Source = CuteBlend->GetBlendSamples()[Index].Animation;
		REQUIRE_VARIANT(Source && NewAnimations.Contains(Source->GetName()), "Unknown locomotion sample");
		REQUIRE_VARIANT(CuteBlend->ReplaceSampleAnimation(Index, CastChecked<UAnimSequence>(NewAnimations[Source->GetName()])), "Cannot replace locomotion sample");
	}
	CuteBlend->ValidateSampleData();
	NewAnimations.Add(OldBlend->GetName(), CuteBlend);

	UAnimBlueprint* Template = CastChecked<UAnimBlueprint>(Tools.DuplicateAsset(FPackageName::GetLongPackageAssetName(TemplatePath),
		FPackageName::GetLongPackagePath(TemplatePath), OldAnim));
	TArray<FAnimParentNodeAssetOverride> OriginalOverrides;
	TArray<FAnimParentNodeAssetOverride> CuteOverrides;
	TArray<UEdGraph*> Graphs;
	Template->GetAllGraphs(Graphs);
	int32 GuardedDivisions = 0;
	for (UEdGraph* Graph : Graphs)
	{
		const auto Nodes = Graph->Nodes;
		for (UEdGraphNode* Node : Nodes)
		{
			if (UAnimGraphNode_AssetPlayerBase* Player = Cast<UAnimGraphNode_AssetPlayerBase>(Node))
			{
				const bool bActive = Node->Pins.ContainsByPredicate([](const UEdGraphPin* Pin) { return Pin->Direction == EGPD_Output && !Pin->LinkedTo.IsEmpty(); });
				if (!bActive) { FBlueprintEditorUtils::RemoveNode(Template, Node, true); continue; }
				UAnimationAsset* Original = Player->GetAnimationAsset();
				REQUIRE_VARIANT(Original && NewAnimations.Contains(Original->GetName()), "Active template player has no CuteCat equivalent");
				OriginalOverrides.Emplace(Node->NodeGuid, Original);
				CuteOverrides.Emplace(Node->NodeGuid, NewAnimations[Original->GetName()]);
				Player->SetAnimationAsset(nullptr);
			}
			if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
				if (Call->FunctionReference.GetMemberName().ToString().StartsWith(TEXT("Divide_")))
				{
					UEdGraphPin* Denominator = Call->FindPin(TEXT("B"));
					REQUIRE_VARIANT(Denominator && Denominator->LinkedTo.Num() == 1, "Unexpected division input");
					UEdGraphPin* Delta = Denominator->LinkedTo[0];
					UK2Node_CallFunction* Clamp = NewObject<UK2Node_CallFunction>(Graph);
					Clamp->SetFromFunction(UKismetMathLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, FMax)));
					Graph->AddNode(Clamp, false, false);
					Clamp->CreateNewGuid();
					Clamp->AllocateDefaultPins();
					Clamp->NodePosX = Call->NodePosX - 230;
					Clamp->NodePosY = Call->NodePosY + 140;
					Clamp->NodeComment = TEXT("Delta seconds can be zero during initialization / editor preview.");
					Clamp->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("0.000001");
					Denominator->BreakAllPinLinks();
					const UEdGraphSchema* Schema = Graph->GetSchema();
					REQUIRE_VARIANT(Schema->TryCreateConnection(Delta, Clamp->FindPinChecked(TEXT("A")))
						&& Schema->TryCreateConnection(Clamp->GetReturnValuePin(), Denominator), "Cannot guard animation delta time");
					++GuardedDivisions;
				}
		}
	}
	REQUIRE_VARIANT(OriginalOverrides.Num() == 6 && GuardedDivisions == 1, "Source animation graph changed since audit");
	Template->bIsTemplate = true;
	Template->TargetSkeleton = nullptr;
	Template->SetPreviewMesh(nullptr);
	REQUIRE_VARIANT(Compile(Template), "Template compilation failed");
	ClearLocalGraphs(OldAnim);
	OldAnim->ParentClass = Template->GeneratedClass;
	OldAnim->ParentAssetOverrides = OriginalOverrides;
	OldAnim->bIsTemplate = false;
	OldAnim->TargetSkeleton = OldSkeleton;
	REQUIRE_VARIANT(Compile(OldAnim), "Original animation child compilation failed");
	UAnimBlueprint* CuteAnim = CastChecked<UAnimBlueprint>(Child(Template, CuteABPPath, true));
	CuteAnim->bIsTemplate = false;
	CuteAnim->TargetSkeleton = CuteSkeleton;
	CuteAnim->ParentAssetOverrides = CuteOverrides;
	CuteAnim->SetPreviewMesh(CuteMesh);
	REQUIRE_VARIANT(Compile(CuteAnim), "CuteCat animation child compilation failed");

	UBlueprint* Base = CastChecked<UBlueprint>(Tools.DuplicateAsset(FPackageName::GetLongPackageAssetName(BasePath),
		FPackageName::GetLongPackagePath(BasePath), OldCharacter));
	Base->bGenerateAbstractClass = true;
	REQUIRE_VARIANT(Compile(Base), "Character base compilation failed");
	ACatCharacter* BaseCDO = CastChecked<ACatCharacter>(Base->GeneratedClass->GetDefaultObject());
	BaseCDO->GetMesh()->SetSkeletalMesh(nullptr);
	BaseCDO->GetMesh()->SetAnimInstanceClass(nullptr);
	ClearLocalGraphs(OldCharacter);
	const auto OldNodes = OldCharacter->SimpleConstructionScript->GetAllNodes();
	// Remove leaves before roots so SCS cannot resurrect a removed child as an orphan.
	for (int32 Index=OldNodes.Num()-1; Index>=0; --Index)
		OldCharacter->SimpleConstructionScript->RemoveNode(OldNodes[Index], false);
	OldCharacter->ParentClass = Base->GeneratedClass;
	REQUIRE_VARIANT(Compile(OldCharacter), "Original character child compilation failed");
	UBlueprint* CuteCharacter = Child(Base, CuteBPPath, false);
	REQUIRE_VARIANT(Compile(CuteCharacter), "CuteCat character child compilation failed");
	auto ConfigureMesh = [&MeshTransform](UBlueprint* BP, USkeletalMesh* Mesh, UAnimBlueprint* Anim)
	{
		ACatCharacter* CDO = CastChecked<ACatCharacter>(BP->GeneratedClass->GetDefaultObject());
		CDO->GetMesh()->SetSkeletalMesh(Mesh);
		CDO->GetMesh()->SetAnimInstanceClass(Anim->GeneratedClass);
		CDO->GetMesh()->SetRelativeTransform(MeshTransform);
		return CDO;
	};
	ConfigureMesh(OldCharacter, OldMesh, OldAnim);
	ACatCharacter* CuteCDO = ConfigureMesh(CuteCharacter, CuteMesh, CuteAnim);
	UCatPhysicsPrototypeVisualComponent* Visual = CuteCDO->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
	REQUIRE_VARIANT(Visual, "Missing shared presentation component");
	Visual->RigSettings.RigId = TEXT("CuteCat");
	Visual->RigSettings.PelvisBone = TEXT("Center_001");
	Visual->RigSettings.JumpRootBone = TEXT("Cat_Root");
	Visual->RigSettings.Feet[0].Bones = {TEXT("Arm_L_001"), TEXT("Arm_L_002"), TEXT("Arm_L_005"), TEXT("Arm_L_006"), TEXT("Hand_L_001")};
	Visual->RigSettings.Feet[1].Bones = {TEXT("Arm_R_001"), TEXT("Arm_R_002"), TEXT("Arm_R_005"), TEXT("Arm_R_006"), TEXT("Hand_R_001")};
	Visual->RigSettings.Feet[2].Bones = {TEXT("Leg_L_001"), TEXT("Leg_L_002"), TEXT("Leg_L_005"), TEXT("Leg_L_006"), TEXT("Foot_L_001")};
	Visual->RigSettings.Feet[3].Bones = {TEXT("Leg_R_001"), TEXT("Leg_R_002"), TEXT("Leg_R_005"), TEXT("Leg_R_006"), TEXT("Foot_R_001")};
	Visual->RigSettings.StandingAnimations = {Idle};
	Visual->RigSettings.LocomotionAnimations = {Walk, Run};
	SetObject(Visual, TEXT("CharacterMesh"), CuteMesh);
	SetObject(Visual, TEXT("IdleAnimation"), Idle);
	SetObject(Visual, TEXT("WalkAnimation"), Walk);
	SetObject(Visual, TEXT("JumpStartAnimation"), NewAnimations.FindRef(TEXT("JumpX_Start-IP")));
	SetObject(Visual, TEXT("JumpLoopAnimation"), NewAnimations.FindRef(TEXT("JumpX_Loop-IP")));
	SetObject(Visual, TEXT("JumpEndAnimation"), NewAnimations.FindRef(TEXT("JumpX_End-IP")));
	TArray<FAssetData> SourceAnimations;
	Registry.Get().GetAssetsByPath(TEXT("/Game/Animalia/Cat"), SourceAnimations, true);
	Registry.Get().GetAssetsByPath(TEXT("/Game/Catfishing/Animation/BodyAction"), SourceAnimations, true);
	for (const FAssetData& Asset : SourceAnimations)
		if (UAnimationAsset* Replacement = NewAnimations.FindRef(Asset.AssetName.ToString()))
			Visual->AnimationOverrides.Add(Asset.GetSoftObjectPath(), Replacement);
	UCatConditionPresentationComponent* Condition = CuteCDO->FindComponentByClass<UCatConditionPresentationComponent>();
	REQUIRE_VARIANT(Condition, "Missing condition presentation component");
	FArrayProperty* ClipsProperty = FindFProperty<FArrayProperty>(Condition->GetClass(), TEXT("PoseClips"));
	REQUIRE_VARIANT(ClipsProperty, "Condition pose contract changed");
	FScriptArrayHelper Clips(ClipsProperty, ClipsProperty->ContainerPtrToValuePtr<void>(Condition));
	FObjectPropertyBase* ClipProperty = CastFieldChecked<FObjectPropertyBase>(ClipsProperty->Inner);
	for (int32 Index=0; Index<Clips.Num(); ++Index)
	{
		UObject* OldClip = ClipProperty->GetObjectPropertyValue(Clips.GetRawPtr(Index));
		REQUIRE_VARIANT(OldClip && NewAnimations.Contains(OldClip->GetName()), "Missing condition clip mapping");
		ClipProperty->SetObjectPropertyValue(Clips.GetRawPtr(Index), NewAnimations[OldClip->GetName()]);
	}
	REQUIRE_VARIANT(Clips.Num() == 5, "Unexpected condition phase count");
	// Socket uses the muzzle bone's frame; local offsets are centimetres before the authored 100x root scale.
	if (!CuteSkeleton->FindSocket(TEXT("Mouth")))
	{
		USkeletalMeshSocket* Mouth = NewObject<USkeletalMeshSocket>(CuteSkeleton);
		Mouth->SocketName = TEXT("Mouth");
		Mouth->BoneName = TEXT("Nose_001");
		Mouth->RelativeLocation = FVector::ZeroVector;
		Mouth->RelativeScale = FVector(0.01);
		CuteSkeleton->Sockets.Add(Mouth);
	}
	for (UObject* Asset : TArray<UObject*>{CuteBlend, Template, OldAnim, CuteAnim, Base, OldCharacter, CuteCharacter, CuteSkeleton})
		REQUIRE_VARIANT(Save(Asset), "Cannot save character family asset");
	return FString::Printf(TEXT("Event=character_family_created TemplatePlayers=%d GuardedDivisions=%d AnimationMappings=%d ConditionClips=%d"),
		OriginalOverrides.Num(), GuardedDivisions, Visual->AnimationOverrides.Num(), Clips.Num());
#undef REQUIRE_VARIANT
}

FString UCatCharacterVariantAuthoringLibrary::MigrateRodCharacterConsumer()
{
	using namespace CatVariantAuthoring;
	UBlueprint* Rod = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Blueprint/Actors/BP_CatFishingRodActor"));
	UBlueprint* Cute = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Character/BP_CuteCatCharacter"));
	UAnimationAsset* Original = LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/Animalia/Cat/AM_Attack_Left-IP_Montage"));
	UAnimationAsset* Replacement = LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/Characters/CuteCat/Animation/Retargeted/AM_Attack_Left-IP_Montage"));
	if (!Rod || !Cute || !Original || !Replacement) return TEXT("ERROR: Missing rod presentation dependency");
	TArray<UEdGraph*> Graphs;
	Rod->GetAllGraphs(Graphs);
	UK2Node_DynamicCast* OldCast = nullptr;
	UEdGraphNode* OldPlay = nullptr;
	for (UEdGraph* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (auto* Cast = ::Cast<UK2Node_DynamicCast>(Node))
			if (Cast->TargetType && Cast->TargetType->GetName() == TEXT("BP_CatCharacter_C")) OldCast = Cast;
		if (Node->GetClass()->GetName() == TEXT("K2Node_PlayMontage")) OldPlay = Node;
	}
	if (OldCast && OldPlay)
	{
		for (const UEdGraphPin* Pin : OldPlay->Pins)
			if (Pin->Direction == EGPD_Output && !Pin->LinkedTo.IsEmpty()) return TEXT("ERROR: Rod montage callbacks changed since audit");
		UEdGraphPin* Execute = OldCast->FindPinChecked(TEXT("execute"));
		UEdGraphPin* Object = OldCast->FindPinChecked(TEXT("Object"));
		UEdGraphPin* Mesh = OldPlay->FindPinChecked(TEXT("InSkeletalMeshComponent"));
		if (Execute->LinkedTo.Num()!=1 || Object->LinkedTo.Num()!=1 || Mesh->LinkedTo.Num()!=1)
			return TEXT("ERROR: Rod graph connections changed since audit");
		UEdGraphPin* InputExec = Execute->LinkedTo[0];
		UEdGraphPin* InputObject = Object->LinkedTo[0];
		UEdGraphNode* MeshGet = Mesh->LinkedTo[0]->GetOwningNode();
		UEdGraphNode* CachedGet = MeshGet->FindPinChecked(TEXT("self"))->LinkedTo[0]->GetOwningNode();
		UEdGraphNode* CachedSet = OldCast->FindPinChecked(TEXT("then"))->LinkedTo[0]->GetOwningNode();
		UEdGraph* Graph = OldCast->GetGraph();
		UK2Node_DynamicCast* NewCast = NewObject<UK2Node_DynamicCast>(Graph);
		NewCast->TargetType = ACatCharacter::StaticClass();
		Graph->AddNode(NewCast, false, false); NewCast->CreateNewGuid(); NewCast->AllocateDefaultPins();
		NewCast->NodePosX = OldCast->NodePosX; NewCast->NodePosY = OldCast->NodePosY;
		UK2Node_CallFunction* NewPlay = NewObject<UK2Node_CallFunction>(Graph);
		NewPlay->SetFromFunction(ACatCharacter::StaticClass()->FindFunctionByName(TEXT("PlayAnimMontage")));
		Graph->AddNode(NewPlay, false, false); NewPlay->CreateNewGuid(); NewPlay->AllocateDefaultPins();
		NewPlay->NodePosX = OldPlay->NodePosX; NewPlay->NodePosY = OldPlay->NodePosY;
		NewPlay->FindPinChecked(TEXT("AnimMontage"))->DefaultObject = Original;
		NewPlay->FindPinChecked(TEXT("InPlayRate"))->DefaultValue = TEXT("1.0");
		for (UEdGraphNode* Node : TArray<UEdGraphNode*>{OldPlay, OldCast, MeshGet, CachedGet, CachedSet})
			FBlueprintEditorUtils::RemoveNode(Rod, Node, true);
		Rod->NewVariables.RemoveAll([](const FBPVariableDescription& Variable) { return Variable.VarName == TEXT("As BP Cat Character"); });
		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (!Schema->TryCreateConnection(InputExec, NewCast->FindPinChecked(TEXT("execute")))
			|| !Schema->TryCreateConnection(InputObject, NewCast->FindPinChecked(TEXT("Object")))
			|| !Schema->TryCreateConnection(NewCast->FindPinChecked(TEXT("then")), NewPlay->FindPinChecked(TEXT("execute")))
			|| !Schema->TryCreateConnection(NewCast->GetCastResultPin(), NewPlay->FindPinChecked(TEXT("self"))))
			return TEXT("ERROR: Cannot reconnect shared rod presentation");
		if (!Compile(Rod)) return TEXT("ERROR: Rod consumer compilation failed");
	}
	else if (OldCast || OldPlay) return TEXT("ERROR: Partial rod migration requires review");
	auto* Visual = Cute->GeneratedClass->GetDefaultObject<ACatCharacter>()->FindComponentByClass<UCatPhysicsPrototypeVisualComponent>();
	if (Replacement->GetSkeleton() != Cute->GeneratedClass->GetDefaultObject<ACatCharacter>()->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton())
		return TEXT("ERROR: Wrong rod replacement skeleton");
	Visual->AnimationOverrides.Add(FSoftObjectPath(Original), Replacement);
	return Save(Rod) && Save(Cute) ? TEXT("Event=rod_character_consumer_migrated Result=SharedNativeCharacterPlayback") : TEXT("ERROR: Cannot save rod consumer");
}
