// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;
using System.Collections.Generic;

public class CatfishingEditorTarget : TargetRules
{
	public CatfishingEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;

		// Editor 目标同时链接运行时模块与只进 Editor 的 CatfishingEditor 工具模块（资产生成 Commandlet）；Game 目标不含后者。
		ExtraModuleNames.AddRange( new string[] { "Catfishing", "CatfishingEditor" } );
	}
}
