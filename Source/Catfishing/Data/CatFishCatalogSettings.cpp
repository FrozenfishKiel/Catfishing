#include "Data/CatFishCatalogSettings.h"

#include "Algo/Sort.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Hash/Blake3.h"

namespace CatFishCatalog
{
	// 文本编码流程：ContentHash 使用换行分隔 UTF-8 文本；字段顺序由本文件固定，不受平台端序或资产路径影响。
	static void AppendHashLine(FString& Text, const FString& Line)
	{
		Text += Line;
		Text += TEXT("\n");
	}

	// 枚举编码流程：用整数值进入内容摘要，避免显示名、本地化或反射重命名改变哈希。
	template <typename TEnum>
	static FString EnumToStableString(const TEnum Value)
	{
		return FString::Printf(TEXT("%d"), static_cast<int32>(Value));
	}

	// 问题记录流程：统一填充目录、稳定 ID、字段与机器码；Message 只供人工定位，不参与运行分支。
	static void AddIssue(FCatDataCatalogValidationResult& Result, const FName StableId, const FName FieldName,
		const ECatDataCatalogIssueCode Code, const TCHAR* Message)
	{
		FCatDataCatalogIssue& Issue = Result.Issues.AddDefaulted_GetRef();
		Issue.CatalogName = TEXT("FishCatalog");
		Issue.StableId = StableId;
		Issue.FieldName = FieldName;
		Issue.Code = Code;
		Issue.Message = Message;
	}

	// 名称数组编码流程：把可交换顺序的 ID 集合排序后写入 Hash，避免编辑器列表重排改变内容身份。
	static FString SortedNameArrayToStableString(TArray<FName> Names)
	{
		Algo::Sort(Names, [](const FName Left, const FName Right)
		{
			return Left.ToString() < Right.ToString();
		});
		TArray<FString> Parts;
		for (const FName Name : Names)
		{
			Parts.Add(Name.ToString());
		}
		return FString::Join(Parts, TEXT(","));
	}

	// 窝料归属数组编码流程：按枚举整数排序后写入 Hash，保留一条鱼吃多味的配置，但不让编辑器里的列表顺序改变内容身份。
	static FString SortedChumAffinityArrayToStableString(TArray<ECatChumAffinity> Values)
	{
		Algo::Sort(Values, [](const ECatChumAffinity Left, const ECatChumAffinity Right)
		{
			return static_cast<int32>(Left) < static_cast<int32>(Right);
		});
		TArray<FString> Parts;
		for (const ECatChumAffinity Value : Values)
		{
			Parts.Add(EnumToStableString(Value));
		}
		return FString::Join(Parts, TEXT(","));
	}

	// 重复项检测流程：逐对比较同一数组内的元素，命中一对相等即返回。鱼定义里这几个集合都只有几项，
	// 两两比较比建哈希集合更直接，也不要求元素类型提供 GetTypeHash。
	template <typename TValue>
	static bool HasDuplicateEntry(const TArray<TValue>& Values)
	{
		for (int32 Index = 1; Index < Values.Num(); ++Index)
		{
			for (int32 Prior = 0; Prior < Index; ++Prior)
			{
				if (Values[Index] == Values[Prior])
				{
					return true;
				}
			}
		}
		return false;
	}

	// 内容序列化流程：只写 FishDefinition 的运行时决策字段，不写资产路径或对象地址，保证文件迁移不改变内容 Hash。
	static void AppendDefinitionForHash(FString& Text, const UCatFishDefinition* Definition)
	{
		if (!Definition)
		{
			AppendHashLine(Text, TEXT("definition:<null>"));
			return;
		}
		AppendHashLine(Text, FString::Printf(TEXT("definition:%s"), *Definition->FishDefinitionId.ToString()));
		AppendHashLine(Text, FString::Printf(TEXT("enabled:%d"), Definition->bEnableRuntimeDefinition ? 1 : 0));
		AppendHashLine(Text, FString::Printf(TEXT("body:%s"), *EnumToStableString(Definition->BodyClass)));
		AppendHashLine(Text, FString::Printf(TEXT("sacrifice:%d"), Definition->SacrificeContribution));
		AppendHashLine(Text, FString::Printf(TEXT("imprint:%s"), *Definition->CaptureImprintEventId.ToString()));
		AppendHashLine(Text, FString::Printf(TEXT("regions:%s"), *SortedNameArrayToStableString(Definition->RegionIds)));
		AppendHashLine(Text, FString::Printf(TEXT("chum_affinity:%s"), *SortedChumAffinityArrayToStableString(Definition->ChumAffinities)));
		AppendHashLine(Text, FString::Printf(TEXT("kg:%.6f|%.6f"), Definition->MinimumWeightKilograms, Definition->MaximumWeightKilograms));
		AppendHashLine(Text, FString::Printf(TEXT("participants:%d"), Definition->MinimumFightParticipants));
		AppendHashLine(Text, FString::Printf(TEXT("strength:%.6f"), Definition->FishStrength));
		AppendHashLine(Text, FString::Printf(TEXT("stamina:%.6f"), Definition->FishFightStamina));
		AppendHashLine(Text, FString::Printf(TEXT("special_bait:%s"), *SortedNameArrayToStableString(Definition->PreferredSpecialBaitIds)));
		AppendHashLine(Text, FString::Printf(TEXT("food:%s"), *EnumToStableString(Definition->FoodSafety)));
		AppendHashLine(Text, FString::Printf(TEXT("poison:%.6f"), Definition->PoisonIncrease));
		AppendHashLine(Text, FString::Printf(TEXT("tank:%d"), Definition->bTankDisplayEligible ? 1 : 0));
	}

	// 定义收集流程：解析显式目录并按稳定鱼种 ID 排序；确定性选择和 Hash 都复用这条顺序。
	static TArray<UCatFishDefinition*> LoadDefinitionsSorted(const TArray<TSoftObjectPtr<UCatFishDefinition>>& Definitions)
	{
		TArray<UCatFishDefinition*> Loaded;
		for (const TSoftObjectPtr<UCatFishDefinition>& Ref : Definitions)
		{
			Loaded.Add(Ref.LoadSynchronous());
		}
		Algo::Sort(Loaded, [](const UCatFishDefinition* Left, const UCatFishDefinition* Right)
		{
			const FString LeftId = Left ? Left->FishDefinitionId.ToString() : FString();
			const FString RightId = Right ? Right->FishDefinitionId.ToString() : FString();
			return LeftId < RightId;
		});
		return Loaded;
	}

	// 候选判断流程：只使用冻结环境、人数和能力快照，不回读 Character、ASC、WaterRegion 或当前 World 状态。
	// 鱼表格没有按时段或天气区分鱼种，所以这里不再拿 Request 的时段与天气过滤候选；它们仍是 Cast 时刻必须已裁决的前置条件，由调用入口拒绝 Unknown。
	// 体力也不参与筛选：钓鱼规则 rev212 §4.4 把猫体力和鱼体力都定义成搏斗中逐秒扣减的资源（猫体力 -= 鱼力量 × 0.12，放线时还能 +1.5 点/秒 再生），
	// §4.3 的四条判定出口没有一行比较体力。拿存量体力做一次性准入会把可再生的消耗品当成资格门槛，直接后果是猫体力上限 100 而湖心巨影体力 260，
	// 那条鱼再多人也永远选不出来。这里保留的三项是"刷新按在场协作能力设上限"的粗粒度近似：地点、最低协作人数、合计力量。
	static bool IsDefinitionSelectable(const UCatFishDefinition* Definition, const FCatFishEncounterSelectionRequest& Request)
	{
		return Definition && Definition->IsRuntimeDefinitionReady()
			&& Definition->RegionIds.Contains(Request.RegionId)
			&& Definition->MinimumFightParticipants <= Request.ActivePlayerCount
			&& Definition->FishStrength <= Request.CombinedFishingStrength;
	}
}

// ID 查询流程：先用默认 Equipment 目录一并校验跨目录引用，再从校验顺手带出来的映射里取这一条；只解析冻结 ID 的调用方也不能绕过特殊饵引用门禁。
// 不算内容摘要，是因为这里既不看它也不返回它。重复鱼种 ID 的 fail-closed 仍由校验保证：同 ID 出现两次会记成
// DuplicateStableId，目录随之不可运行，下面的 bValid 分支已经返回空，映射里被覆盖过的那条读不到。
UCatFishDefinition* UCatFishCatalogSettings::FindRuntimeDefinition(const FName FishDefinitionId) const
{
	if (FishDefinitionId.IsNone())
	{
		return nullptr;
	}
	TMap<FName, UCatFishDefinition*> RuntimeById;
	if (!ValidateRuntimeCatalog(GetDefault<UCatEquipmentSettings>(), /*bComputeContentHash*/ false, &RuntimeById).bValid)
	{
		return nullptr;
	}
	UCatFishDefinition** Match = RuntimeById.Find(FishDefinitionId);
	return Match ? *Match : nullptr;
}

// Encounter 生成流程：先校验目录和冻结输入，再在按稳定 ID 排序的候选集里抽取鱼种和重量；相同内容、输入和 seed 必须返回同一结果。
FCatFishEncounterSelectionResult UCatFishCatalogSettings::GenerateEncounterSelection(
	const FCatFishEncounterSelectionRequest& Request,
	const UCatEquipmentSettings* EquipmentCatalog) const
{
	FCatFishEncounterSelectionResult Result;
	const FCatDataCatalogValidationResult Catalog = ValidateRuntimeCatalog(EquipmentCatalog);
	Result.SchemaVersion = Catalog.SchemaVersion;
	Result.DataRevision = Catalog.DataRevision;
	Result.ContentHashHex = Catalog.ContentHashHex;
	Result.SelectionSeed = Request.SelectionSeed;
	Result.FightParticipantCount = Request.ActivePlayerCount;
	Result.CombinedFishingStrength = Request.CombinedFishingStrength;
	Result.CombinedFightStamina = Request.CombinedFightStamina;
	if (!Catalog.bValid || Request.RegionId.IsNone() || Request.TimeOfDay == ECatEnvironmentTimeOfDay::Unknown
		|| Request.Weather == ECatEnvironmentWeather::Unknown || Request.ActivePlayerCount < 1 || Request.ActivePlayerCount > 8
		|| !FMath::IsFinite(Request.CombinedFishingStrength) || Request.CombinedFishingStrength <= 0.0
		|| !FMath::IsFinite(Request.CombinedFightStamina) || Request.CombinedFightStamina <= 0.0)
	{
		return Result;
	}

	TArray<UCatFishDefinition*> Candidates;
	for (UCatFishDefinition* Definition : CatFishCatalog::LoadDefinitionsSorted(Definitions))
	{
		if (CatFishCatalog::IsDefinitionSelectable(Definition, Request))
		{
			Candidates.Add(Definition);
		}
	}
	if (Candidates.IsEmpty())
	{
		return Result;
	}

	// 候选集内按均匀概率抽取：鱼表格没有出现权重列，正式抽鱼模型也不用权重，而是让鱼群构成跟随窝料三味占比
	// （模型出处见 UCatFishDefinition::ChumAffinities 的注释）。那套模型的消费端还没实现，这里先不引入任何代码自造的
	// 档位概率；候选顺序已由稳定 ID 排序固定，所以同 seed 结果可复现。
	FRandomStream Random(Request.SelectionSeed);
	const UCatFishDefinition* Selected = Candidates[Random.RandRange(0, Candidates.Num() - 1)];
	Result.bSelected = true;
	Result.FishDefinitionId = Selected->FishDefinitionId;
	Result.BodyClass = Selected->BodyClass;
	Result.FishFightStamina = Selected->FishFightStamina;
	Result.FishWeightKilograms = Random.FRandRange(static_cast<float>(Selected->MinimumWeightKilograms),
		static_cast<float>(Selected->MaximumWeightKilograms));
	return Result;
}

// 目录校验流程：验证 Schema/DataRevision/来源戳、显式清单、单条 readiness、地点与窝料归属和特殊饵 ID 的元素合法性、
// 重复鱼种 ID，并在给出 Equipment 目录时校验特殊饵偏好确实指向真实特殊饵。
// 内容摘要按调用方要求计算，运行定义映射按调用方要求交出；非法目录只交出空表。
FCatDataCatalogValidationResult UCatFishCatalogSettings::ValidateRuntimeCatalog(
	const UCatEquipmentSettings* EquipmentCatalog,
	const bool bComputeContentHash,
	TMap<FName, UCatFishDefinition*>* OutRuntimeById) const
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
		CatFishCatalog::AddIssue(Result, NAME_None, TEXT("ContentSchemaVersion"),
			ECatDataCatalogIssueCode::UnsupportedSchemaVersion,
			TEXT("Fish catalog schema version is not supported by current code."));
	}
	if (DataRevision <= 0)
	{
		CatFishCatalog::AddIssue(Result, NAME_None, TEXT("DataRevision"),
			ECatDataCatalogIssueCode::InvalidDefinition,
			TEXT("Fish catalog requires a positive data revision."));
	}
	if (SourceStamp.SourceKind.IsNone() || SourceStamp.SourceNodeToken.IsEmpty() || SourceStamp.SourceRevision <= 0)
	{
		CatFishCatalog::AddIssue(Result, NAME_None, TEXT("SourceStamp"),
			ECatDataCatalogIssueCode::MissingSource,
			TEXT("Fish catalog requires a traceable source stamp from the landing process."));
	}
	if (Definitions.IsEmpty())
	{
		CatFishCatalog::AddIssue(Result, NAME_None, TEXT("Definitions"),
			ECatDataCatalogIssueCode::MissingDefinition,
			TEXT("Fish catalog has no explicit definitions."));
	}

	// 跨目录引用校验需要的是一份"装备稳定 ID → 运行定义"映射，这里在进入鱼循环前一次性取到。
	// 原来的写法是在最内层对每个 PreferredSpecialBaitId 调一次装备目录的 FindRuntimeDefinition，而那个入口每次都要
	// 把整份装备目录重新校验一遍；鱼表里有多少条偏好饵，就要为同一份装备内容付多少遍整表代价。
	// bEquipmentRuntimeValid 承接的是原来那条链的 fail-closed 语义：装备目录本身不可运行时，任何饵 ID 都查不出定义，
	// 于是每条偏好饵都记 InvalidReference——这和原来逐条调用 FindRuntimeDefinition 各拿一个 nullptr 的结果一致。
	TMap<FName, UCatEquipmentDefinition*> EquipmentRuntimeById;
	const bool bEquipmentRuntimeValid = EquipmentCatalog
		&& EquipmentCatalog->ValidateRuntimeCatalog(/*bComputeContentHash*/ false, &EquipmentRuntimeById).bValid;

	TMap<FName, int32> IdCounts;
	TMap<FName, UCatFishDefinition*> RuntimeById;
	for (const TSoftObjectPtr<UCatFishDefinition>& DefinitionRef : Definitions)
	{
		UCatFishDefinition* Definition = DefinitionRef.LoadSynchronous();
		if (!Definition)
		{
			CatFishCatalog::AddIssue(Result, NAME_None, TEXT("Definitions"),
				ECatDataCatalogIssueCode::MissingDefinition,
				TEXT("Fish catalog contains a missing definition reference."));
			continue;
		}
		IdCounts.FindOrAdd(Definition->FishDefinitionId)++;
		if (!Definition->IsRuntimeDefinitionReady())
		{
			CatFishCatalog::AddIssue(Result, Definition->FishDefinitionId, TEXT("Definition"),
				ECatDataCatalogIssueCode::InvalidDefinition,
				TEXT("Fish definition is not runtime ready."));
			continue;
		}
		RuntimeById.Add(Definition->FishDefinitionId, Definition);
		// readiness 只数了数组长度，数组里装的是什么它管不到；下面三段补上元素级检查，让空 ID、Unknown 和重复项在目录层就暴露成结构化问题，
		// 而不是留到选鱼、窝料匹配或跨目录引用阶段才表现成"匹配不到"这类看不出原因的症状。
		if (Definition->RegionIds.Contains(NAME_None) || CatFishCatalog::HasDuplicateEntry(Definition->RegionIds))
		{
			CatFishCatalog::AddIssue(Result, Definition->FishDefinitionId, TEXT("RegionIds"),
				ECatDataCatalogIssueCode::InvalidDefinition,
				TEXT("Fish region ids must be unique and must not contain an empty name."));
		}
		if (Definition->ChumAffinities.Contains(ECatChumAffinity::Unknown)
			|| CatFishCatalog::HasDuplicateEntry(Definition->ChumAffinities))
		{
			CatFishCatalog::AddIssue(Result, Definition->FishDefinitionId, TEXT("ChumAffinities"),
				ECatDataCatalogIssueCode::InvalidDefinition,
				TEXT("Fish chum affinities must be unique and must not contain an undecided value."));
		}
		// 这里报 InvalidDefinition：空 ID 和重复项是本行自己的填写缺陷，与 Equipment 目录里有没有那个饵无关。
		// 本块只登记问题，不跳过下面的跨目录引用校验，空 ID 仍会走进那个循环。
		if (Definition->PreferredSpecialBaitIds.Contains(NAME_None)
			|| CatFishCatalog::HasDuplicateEntry(Definition->PreferredSpecialBaitIds))
		{
			CatFishCatalog::AddIssue(Result, Definition->FishDefinitionId, TEXT("PreferredSpecialBaitIds"),
				ECatDataCatalogIssueCode::InvalidDefinition,
				TEXT("Preferred special bait ids must be unique and must not contain an empty name."));
		}
		if (EquipmentCatalog)
		{
			for (const FName PreferredBaitId : Definition->PreferredSpecialBaitIds)
			{
				// 这个 continue 才是挡住二次报错的那道闸：空 ID 上面已经登记过 InvalidDefinition，放它进来只会再拿一个
				// InvalidReference。两个码的语义和修法不同——InvalidReference 表示填了饵但 Equipment 目录里没有，要改饵 ID；
				// InvalidDefinition 表示这一行根本没填饵，要补内容。删掉这里，同一个缺口就会同时命中两个码。
				if (PreferredBaitId.IsNone())
				{
					continue;
				}
				UCatEquipmentDefinition* const* Found = bEquipmentRuntimeValid
					? EquipmentRuntimeById.Find(PreferredBaitId) : nullptr;
				const UCatEquipmentDefinition* PreferredBait = Found ? *Found : nullptr;
				if (!PreferredBait || PreferredBait->Kind != ECatEquipmentKind::Bait || !PreferredBait->bSpecialBait)
				{
					CatFishCatalog::AddIssue(Result, Definition->FishDefinitionId, TEXT("PreferredSpecialBaitIds"),
						ECatDataCatalogIssueCode::InvalidReference,
						TEXT("Preferred special bait must reference a runtime special bait definition."));
				}
			}
		}
	}
	for (const TPair<FName, int32>& Pair : IdCounts)
	{
		if (!Pair.Key.IsNone() && Pair.Value > 1)
		{
			CatFishCatalog::AddIssue(Result, Pair.Key, TEXT("FishDefinitionId"),
				ECatDataCatalogIssueCode::DuplicateStableId,
				TEXT("Fish catalog contains duplicate stable IDs."));
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

// Hash 计算流程：把目录版本、数据修订和按稳定 ID 排序后的鱼定义运行字段写入 Blake3；不读取资产路径或飞书来源。
FString UCatFishCatalogSettings::ComputeContentHashHex() const
{
	FString Canonical;
	CatFishCatalog::AppendHashLine(Canonical, TEXT("catalog:fish"));
	CatFishCatalog::AppendHashLine(Canonical, FString::Printf(TEXT("schema:%d"), ContentSchemaVersion));
	CatFishCatalog::AppendHashLine(Canonical, FString::Printf(TEXT("revision:%lld"), static_cast<long long>(DataRevision)));
	for (const UCatFishDefinition* Definition : CatFishCatalog::LoadDefinitionsSorted(Definitions))
	{
		CatFishCatalog::AppendDefinitionForHash(Canonical, Definition);
	}
	FTCHARToUTF8 Utf8(*Canonical);
	return LexToString(FBlake3::HashBuffer(Utf8.Get(), static_cast<uint64>(Utf8.Length())));
}
