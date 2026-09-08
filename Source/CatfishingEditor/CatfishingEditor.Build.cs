using UnrealBuildTool;

// 编辑器资产生产模块；StateTree 和正式 WBP 的构造、编译与保存能力只随 Editor 加载，不进入打包游戏。
public class CatfishingEditor : ModuleRules
{
	// 先声明资产所引用的 Runtime 类型，再私有链接编辑器构造工具和快捷键修复依赖；BlueprintGraph 只服务编辑器内 K2 节点检查，UMGEditor 与 AssetTools 供本模块生成资产，Slate/SlateCore/InputCore 供 PlayWorld 停止运行快捷键改为 Shift+Escape。
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
			"GameplayStateTreeModule", "PropertyBindingUtils", "PropertyBindingUtilsEditor",
			"UMG", "UMGEditor", "Slate", "SlateCore", "InputCore", "AssetTools"
		});
	}
}
