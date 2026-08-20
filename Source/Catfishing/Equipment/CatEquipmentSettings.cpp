#include "Equipment/CatEquipmentSettings.h"

#include "Algo/Sort.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Hash/Blake3.h"

namespace CatEquipmentCatalog
{
	// 文本编码流程：ContentHash 只需要稳定、可读、跨平台一致的字段序列；这里统一追加换行分隔的 UTF-8 文本。
	static void AppendHashLine(FString& Text, const FString& Line)
	{
		Text += Line;
		Text += TEXT("\n");
	}

	// 枚举编码流程：把 enum class 转成整数文本，避免显示名、本地化或反射重命名影响内容摘要。
	template <typename TEnum>
	static FString EnumToStableString(const TEnum Value)
	{
		return FString::Printf(TEXT("%d"), static_cast<int32>(Value));
	}

	// 问题记录流程：统一填充目录、稳定 ID、字段与机器码；Message 只服务人工定位。
	static void AddIssue(FCatDataCatalogValidationResult& Result, const FName StableId, const FName FieldName,
		const ECatDataCatalogIssueCode Code, const TCHAR* Message)
	{
		FCatDataCatalogIssue& Issue = Result.Issues.AddDefaulted_GetRef();
		Issue.CatalogName = TEXT("EquipmentCatalog");
		Issue.StableId = StableId;
		Issue.FieldName = FieldName;
		Issue.Code = Code;
		Issue.Message = Message;
	}


    // Starter 引用校验流程：只在三件套任一字段被配置时触发；空字段也作为 InvalidReference 暴露，避免半套 starter 偷跑。
    static void ValidateStarterReference(FCatDataCatalogValidationResult& Result,
        const TMap<FName, UCatEquipmentDefinition*>& RuntimeById, const FName DefinitionId,
        const FName FieldName, const ECatEquipmentKind ExpectedKind)
    {
        UCatEquipmentDefinition* const* Definition = RuntimeById.Find(DefinitionId);
        if (DefinitionId.IsNone() || !Definition || !*Definition || (*Definition)->Kind != ExpectedKind)
        {
            AddIssue(Result, DefinitionId, FieldName, ECatDataCatalogIssueCode::InvalidReference,
                TEXT("Starter loadout reference must point to a runtime definition with the expected equipment kind."));
        }
    }
	// 内容序列化流程：只写 EquipmentDefinition 的运行时决策字段，不写资产路径或对象地址，保证同内容迁移文件也不会改变 Hash。
	static void AppendDefinitionForHash(FString& Text, const UCatEquipmentDefinition* Definition)
	{
		if (!Definition)
		{
			AppendHashLine(Text, TEXT("definition:<null>"));
			return;
		}
		AppendHashLine(Text, FString::Printf(TEXT("definition:%s"), *Definition->EquipmentDefinitionId.ToString()));
		AppendHashLine(Text, FString::Printf(TEXT("enabled:%d"), Definition->bEnableRuntimeDefinition ? 1 : 0));
		AppendHashLine(Text, FString::Printf(TEXT("kind:%s"), *EnumToStableString(Definition->Kind)));
		AppendHashLine(Text, FString::Printf(TEXT("slot:%s"), *Definition->LoadoutSlotId.ToString()));
		AppendHashLine(Text, FString::Printf(TEXT("unlock:%s"), *Definition->RequiredUnlockId.ToString()));
		AppendHashLine(Text, FString::Printf(TEXT("run_consumable:%d"), Definition->bRunConsumable ? 1 : 0));
		AppendHashLine(Text, FString::Printf(TEXT("special_bait:%d"), Definition->bSpecialBait ? 1 : 0));
		AppendHashLine(Text, FString::Printf(TEXT("rod_durability:%.6f"), Definition->MaximumRodDurability));
		AppendHashLine(Text, FString::Printf(TEXT("rod_strength:%.6f"), Definition->RodStrength));
		AppendHashLine(Text, FString::Printf(TEXT("rod_line_max_m:%.6f"), Definition->MaximumLineLengthMeters));
		AppendHashLine(Text, FString::Printf(TEXT("float_range_m:%.6f"), Definition->FloatCastRangeMeters));
		AppendHashLine(Text, FString::Printf(TEXT("float_accuracy_radius_m:%.6f"), Definition->FloatAccuracyOffsetRadiusMeters));
		AppendHashLine(Text, FString::Printf(TEXT("route:%s"), *Definition->FunctionalRouteId.ToString()));
		AppendHashLine(Text, FString::Printf(TEXT("chum:%.6f|%.6f|%.6f"),
			Definition->ChumContribution.Fishy,
			Definition->ChumContribution.Fragrant,
			Definition->ChumContribution.Fermented));
	}

	// 定义收集流程：解析显式目录并按稳定 ID 排序；调用方决定非法定义是进入 Hash 诊断还是进入运行候选。
	static TArray<UCatEquipmentDefinition*> LoadDefinitionsSorted(const TArray<TSoftObjectPtr<UCatEquipmentDefinition>>& Definitions)
	{
		TArray<UCatEquipmentDefinition*> Loaded;
		for (const TSoftObjectPtr<UCatEquipmentDefinition>& Ref : Definitions)
		{
			Loaded.Add(Ref.LoadSynchronous());
		}
		Algo::Sort(Loaded, [](const UCatEquipmentDefinition* Left, const UCatEquipmentDefinition* Right)
		{
			const FString LeftId = Left ? Left->EquipmentDefinitionId.ToString() : FString();
			const FString RightId = Right ? Right->EquipmentDefinitionId.ToString() : FString();
			return LeftId < RightId;
		});
		return Loaded;
	}
}

// 定义查询流程：先要求目录整体可运行，再从校验顺手带出来的映射里取这一条；整条路径只解析一遍定义集合。
// 不算内容摘要，是因为这里根本不看它——摘要不参与 bValid，查询也不返回它，算了直接丢掉。
// 重复 ID 的 fail-closed 由校验保证而不是靠这里再数一遍：同一个 ID 出现两次会记成 DuplicateStableId，
// 目录随之不可运行，下面的 bValid 分支就已经返回空了，映射里那条被覆盖过的记录不会有机会被读到。
UCatEquipmentDefinition* UCatEquipmentSettings::FindRuntimeDefinition(const FName DefinitionId) const
{
	if (DefinitionId.IsNone())
	{
		return nullptr;
	}
	TMap<FName, UCatEquipmentDefinition*> RuntimeById;
	if (!ValidateRuntimeCatalog(/*bComputeContentHash*/ false, &RuntimeById).bValid)
	{
		return nullptr;
	}
	UCatEquipmentDefinition** Match = RuntimeById.Find(DefinitionId);
	return Match ? *Match : nullptr;
}


// 初始装配读取流程：先清输出；只有三件套全部显式配置才返回 true，避免 Character 用半套数据自动装备。
bool UCatEquipmentSettings::TryGetStarterLoadout(FName& OutRodDefinitionId, FName& OutBaitDefinitionId,
    FName& OutFloatDefinitionId) const
{
    OutRodDefinitionId = NAME_None;
    OutBaitDefinitionId = NAME_None;
    OutFloatDefinitionId = NAME_None;
    if (StarterRodDefinitionId.IsNone() || StarterBaitDefinitionId.IsNone() || StarterFloatDefinitionId.IsNone())
    {
        return false;
    }
    OutRodDefinitionId = StarterRodDefinitionId;
    OutBaitDefinitionId = StarterBaitDefinitionId;
    OutFloatDefinitionId = StarterFloatDefinitionId;
    return true;
}

// 目录校验流程：按需先算内容摘要，再验证 Schema/DataRevision/来源戳、显式清单、单条 readiness、重复稳定 ID 和维修浮木引用；
// 任何问题都让运行目录 fail-closed。最后按调用方要求把解析好的运行定义映射交出去，非法目录只交出空表。
FCatDataCatalogValidationResult UCatEquipmentSettings::ValidateRuntimeCatalog(const bool bComputeContentHash,
	TMap<FName, UCatEquipmentDefinition*>* OutRuntimeById) const
{
	FCatDataCatalogValidationResult Result;
	Result.SchemaVersion = ContentSchemaVersion;
	Result.DataRevision = DataRevision;
	Result.SourceStamp = SourceStamp;
	if (bComputeContentHash)
	{
		Result.ContentHashHex = ComputeContentHashHex();
	}
	if (ContentSchemaVersion != CurrentContentSchemaVersion)
	{
		CatEquipmentCatalog::AddIssue(Result, NAME_None, TEXT("ContentSchemaVersion"),
			ECatDataCatalogIssueCode::UnsupportedSchemaVersion,
			TEXT("Equipment catalog schema version is not supported by current code."));
	}
	if (DataRevision <= 0)
	{
		CatEquipmentCatalog::AddIssue(Result, NAME_None, TEXT("DataRevision"),
			ECatDataCatalogIssueCode::InvalidDefinition,
			TEXT("Equipment catalog requires a positive data revision."));
	}
	if (SourceStamp.SourceKind.IsNone() || SourceStamp.SourceNodeToken.IsEmpty() || SourceStamp.SourceRevision <= 0)
	{
		CatEquipmentCatalog::AddIssue(Result, NAME_None, TEXT("SourceStamp"),
			ECatDataCatalogIssueCode::MissingSource,
			TEXT("Equipment catalog requires a traceable source stamp from the landing process."));
	}
	if (Definitions.IsEmpty())
	{
		CatEquipmentCatalog::AddIssue(Result, NAME_None, TEXT("Definitions"),
			ECatDataCatalogIssueCode::MissingDefinition,
			TEXT("Equipment catalog has no explicit definitions."));
	}

	TMap<FName, int32> IdCounts;
	TMap<FName, UCatEquipmentDefinition*> RuntimeById;
	for (const TSoftObjectPtr<UCatEquipmentDefinition>& Ref : Definitions)
	{
		UCatEquipmentDefinition* Definition = Ref.LoadSynchronous();
		if (!Definition)
		{
			CatEquipmentCatalog::AddIssue(Result, NAME_None, TEXT("Definitions"),
				ECatDataCatalogIssueCode::MissingDefinition,
				TEXT("Equipment catalog contains a missing definition reference."));
			continue;
		}
		IdCounts.FindOrAdd(Definition->EquipmentDefinitionId)++;
		if (!Definition->IsRuntimeDefinitionReady())
		{
			CatEquipmentCatalog::AddIssue(Result, Definition->EquipmentDefinitionId, TEXT("Definition"),
				ECatDataCatalogIssueCode::InvalidDefinition,
				TEXT("Equipment definition is not runtime ready."));
			continue;
		}
		RuntimeById.Add(Definition->EquipmentDefinitionId, Definition);
	}
	for (const TPair<FName, int32>& Pair : IdCounts)
	{
		if (!Pair.Key.IsNone() && Pair.Value > 1)
		{
			CatEquipmentCatalog::AddIssue(Result, Pair.Key, TEXT("EquipmentDefinitionId"),
				ECatDataCatalogIssueCode::DuplicateStableId,
				TEXT("Equipment catalog contains duplicate stable IDs."));
		}
	}
	const bool bHasStarterLoadout = !StarterRodDefinitionId.IsNone() || !StarterBaitDefinitionId.IsNone()
		|| !StarterFloatDefinitionId.IsNone();
	if (bHasStarterLoadout)
	{
		CatEquipmentCatalog::ValidateStarterReference(Result, RuntimeById, StarterRodDefinitionId,
			TEXT("StarterRodDefinitionId"), ECatEquipmentKind::Rod);
		CatEquipmentCatalog::ValidateStarterReference(Result, RuntimeById, StarterBaitDefinitionId,
			TEXT("StarterBaitDefinitionId"), ECatEquipmentKind::Bait);
		CatEquipmentCatalog::ValidateStarterReference(Result, RuntimeById, StarterFloatDefinitionId,
			TEXT("StarterFloatDefinitionId"), ECatEquipmentKind::Float);
	}
	if (!DriftwoodDefinitionId.IsNone())
	{
		UCatEquipmentDefinition* const* Driftwood = RuntimeById.Find(DriftwoodDefinitionId);
		if (!Driftwood || !*Driftwood || (*Driftwood)->Kind != ECatEquipmentKind::Driftwood)
		{
			CatEquipmentCatalog::AddIssue(Result, DriftwoodDefinitionId, TEXT("DriftwoodDefinitionId"),
				ECatDataCatalogIssueCode::InvalidReference,
				TEXT("Repair driftwood reference must point to a runtime Driftwood definition."));
		}
	}
	Result.bValid = Result.Issues.IsEmpty();
	if (OutRuntimeById)
	{
		OutRuntimeById->Reset();
		if (Result.bValid)
		{
			*OutRuntimeById = MoveTemp(RuntimeById);
		}
	}
	return Result;
}

// Hash 计算流程：把目录版本、数据修订、关键策略字段和按稳定 ID 排序后的定义字段写入 Blake3；不读取资产路径或飞书来源。
FString UCatEquipmentSettings::ComputeContentHashHex() const
{
	FString Canonical;
	CatEquipmentCatalog::AppendHashLine(Canonical, TEXT("catalog:equipment"));
	CatEquipmentCatalog::AppendHashLine(Canonical, FString::Printf(TEXT("schema:%d"), ContentSchemaVersion));
	CatEquipmentCatalog::AppendHashLine(Canonical, FString::Printf(TEXT("revision:%lld"), static_cast<long long>(DataRevision)));
	CatEquipmentCatalog::AppendHashLine(Canonical, FString::Printf(TEXT("loadout_policy:%s"), *CatEquipmentCatalog::EnumToStableString(ProfileLoadoutTrustPolicy)));
	CatEquipmentCatalog::AppendHashLine(Canonical, FString::Printf(TEXT("starter_rod:%s"), *StarterRodDefinitionId.ToString()));
	CatEquipmentCatalog::AppendHashLine(Canonical, FString::Printf(TEXT("starter_bait:%s"), *StarterBaitDefinitionId.ToString()));
	CatEquipmentCatalog::AppendHashLine(Canonical, FString::Printf(TEXT("starter_float:%s"), *StarterFloatDefinitionId.ToString()));
	CatEquipmentCatalog::AppendHashLine(Canonical, FString::Printf(TEXT("rod_failure_loss:%.6f"), RodFailureDurabilityLoss));
	CatEquipmentCatalog::AppendHashLine(Canonical, FString::Printf(TEXT("driftwood:%s"), *DriftwoodDefinitionId.ToString()));
	for (const UCatEquipmentDefinition* Definition : CatEquipmentCatalog::LoadDefinitionsSorted(Definitions))
	{
		CatEquipmentCatalog::AppendDefinitionForHash(Canonical, Definition);
	}
	FTCHARToUTF8 Utf8(*Canonical);
	return LexToString(FBlake3::HashBuffer(Utf8.Get(), static_cast<uint64>(Utf8.Length())));
}
