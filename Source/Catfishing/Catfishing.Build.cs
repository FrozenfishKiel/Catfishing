using UnrealBuildTool;

// Catfishing 运行时模块的构建合同；公开头暴露的设置、UMG、AudioMixer、能力、标签、复制、OSS 与 StateTree 类型保持 Public；Steam SDK 只在 Online 实现中使用，按引擎支持平台私有链接。
public class Catfishing : ModuleRules
{
	// 依赖装配流程：先登记模块根与公开签名依赖，再补实现依赖；按 Steam 插件支持平台同步决定功能宏与 SDK 链接，最后仅对 Editor 目标追加资产编写模块。
	public Catfishing(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "DeveloperSettings", "UMG", "AudioMixer",
			"GameplayAbilities", "GameplayTags", "NetCore", "OnlineSubsystem", "StateTreeModule",
			"GameplayStateTreeModule", "ProceduralMeshComponent"
		});
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore", "ApplicationCore", "GameplayTasks", "EnhancedInput",
			"Slate", "SlateCore", "OnlineSubsystemUtils", "PhysicsCore"
		});
		// Steamworks.Build.cs 只导出 SDK 版本与路径，不定义功能宏；按 UE 5.8 Steam 插件的平台支持范围装配宏和依赖，Win64 ARM64 被插件明确排除。
		bool bWithSteamworks = (Target.Platform == UnrealTargetPlatform.Win64 && Target.Architecture == UnrealArch.X64)
			|| Target.Platform == UnrealTargetPlatform.Mac
			|| Target.Platform == UnrealTargetPlatform.Linux
			|| Target.Platform == UnrealTargetPlatform.Android;
		PrivateDefinitions.Add("WITH_STEAMWORKS=" + (bWithSteamworks ? "1" : "0"));
		if (bWithSteamworks)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "Steamworks");
		}
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"AssetRegistry", "AssetTools", "AnimGraph", "UnrealEd", "UMGEditor"
			});
		}
	}
}
