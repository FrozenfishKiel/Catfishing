using UnrealBuildTool;

// 编辑器资产生产模块；StateTree 和正式 WBP 的构造、编译与保存能力只随 Editor 加载，不进入打包游戏。
public class CatfishingEditor : ModuleRules
{
	// 先声明资产所引用的 Runtime 类型，再私有链接编辑器工具；GameplayAbilities 供经济回归直接观察真实 ASC，BlueprintGraph/UMGEditor/AssetTools 供资产构造，Slate/InputCore 供编辑器界面与停止运行快捷键。
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
			"GameplayStateTreeModule", "GameplayAbilities", "PropertyBindingUtils", "PropertyBindingUtilsEditor",
			"UMG", "UMGEditor", "Slate", "SlateCore", "InputCore", "AssetTools"
		});
	}
}
