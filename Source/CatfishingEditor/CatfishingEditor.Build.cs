using UnrealBuildTool;

// CatfishingEditor 是只进 Editor 目标的工具模块：它承载需要 UnrealEd / StateTreeEditorModule 的内容生成 Commandlet，让运行时模块 Catfishing 继续不带任何 Editor 依赖。
public class CatfishingEditor : ModuleRules
{
	// 依赖装配流程：模块根登记为私有 include 根，使 StateTree/ 子目录头按根相对路径引用；运行时类型来自 Catfishing，资产建树/编译来自 StateTree 的两个 Editor 模块，Schema 来自 GameplayStateTree。
	public CatfishingEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateIncludePaths.Add(ModuleDirectory);
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Catfishing",
			"UnrealEd",
			"GameplayTags",
			"StateTreeModule",
			"StateTreeEditorModule",
			"GameplayStateTreeModule",
			"PropertyBindingUtils"
		});
	}
}
