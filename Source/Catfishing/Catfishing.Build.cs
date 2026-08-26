using UnrealBuildTool;

// Catfishing 运行时模块的构建合同；公开头直接暴露 UDeveloperSettings、UUserWidget、IAbilitySystemInterface/UAttributeSet、GameplayTag 容器、FFastArraySerializer、OSS 会话类型与 StateTree Task/Condition 七类外部类型，因此对应模块保持 Public；只在 .cpp/私有接线使用的模块保持 Private。
public class Catfishing : ModuleRules
{
	// 依赖装配流程：先把现有单 Runtime 目录的模块根登记为公开 include 根，使公共头沿用 Items/... 等根相对路径且无需新增模块或目录；再声明公开签名依赖，并把仅实现使用的模块留在 Private，运行模块不引入 Editor 依赖。
	public Catfishing(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "DeveloperSettings", "UMG",
			"GameplayAbilities", "GameplayTags", "NetCore", "OnlineSubsystem", "StateTreeModule",
			"GameplayStateTreeModule"
		});
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore", "GameplayTasks", "CableComponent", "EnhancedInput",
			"Slate", "SlateCore", "OnlineSubsystemUtils"
		});
	}
}
