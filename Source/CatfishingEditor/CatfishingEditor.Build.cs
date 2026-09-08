using UnrealBuildTool;

public class CatfishingEditor : ModuleRules
{
	public CatfishingEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Catfishing"
		});
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd", "AssetRegistry", "BlueprintGraph", "StateTreeModule", "StateTreeEditorModule",
			"GameplayStateTreeModule", "PropertyBindingUtils", "PropertyBindingUtilsEditor", "GameplayAbilities"
		});
	}
}
