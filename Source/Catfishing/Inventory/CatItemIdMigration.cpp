#include "Inventory/CatInventorySettings.h"
#include "UObject/UnrealType.h"

namespace
{
	/** 迁移前已冻结的旧编号对应表；只用于读取旧数据，正式物品查询始终读取 DT_ItemCatalog。 */
	const TMap<FName, int32> LegacyItemIds = {
		{TEXT("Bait_Basic"), 1},
		{TEXT("BellFloat"), 2},
		{TEXT("Blackfish"), 3},
		{TEXT("BugBait"), 4},
		{TEXT("BugChum"), 5},
		{TEXT("Chum_Basic"), 6},
		{TEXT("ElectricEel"), 7},
		{TEXT("EstuaryBass"), 8},
		{TEXT("FeatherFloat"), 9},
		{TEXT("FermentedGrainChum"), 10},
		{TEXT("FishGuard"), 11},
		{TEXT("Fish_Test01"), 12},
		{TEXT("FlashingBait"), 13},
		{TEXT("Float_Basic"), 14},
		{TEXT("ForestLongtailFish"), 15},
		{TEXT("FruitBait"), 16},
		{TEXT("FruitFragranceChum"), 17},
		{TEXT("GiantLureBait"), 18},
		{TEXT("HolyLightChum"), 19},
		{TEXT("LakeGiantShadow"), 20},
		{TEXT("LittleColorFish"), 21},
		{TEXT("LittleSilverFish"), 22},
		{TEXT("Loach"), 23},
		{TEXT("MeatBait"), 24},
		{TEXT("MoonlightBait"), 25},
		{TEXT("NectarBait"), 26},
		{TEXT("PetalFish"), 27},
		{TEXT("Pike"), 28},
		{TEXT("PufferFish"), 29},
		{TEXT("RiverPatternFish"), 30},
		{TEXT("Rod_Basic"), 31},
		{TEXT("SaltedFish"), 32},
		{TEXT("ScoopNet_Basic"), 33},
		{TEXT("ShopRodT2"), 34},
		{TEXT("SilvermoonTrout"), 35},
		{TEXT("SoundBait"), 36},
		{TEXT("StarterRodT1"), 37},
		{TEXT("StarterScoopNet"), 38},
		{TEXT("StinkyFish"), 39},
		{TEXT("WindbellFish"), 40},
		{TEXT("YarnBallFloat"), 41},
		{TEXT("FishTankCapacityT2"), 42},
		{TEXT("FishTankCapacityT3"), 43},
	};

	/** 旧反射字段到数字字段的明确对应；槽位、皮肤、天气等非物品身份不进入此表。 */
	const TMap<FName, FName> LegacyFields = {
		{TEXT("InventoryDefinitionId"), TEXT("ItemId")},
		{TEXT("EquipmentDefinitionId"), TEXT("ItemId")},
		{TEXT("FishDefinitionId"), TEXT("ItemId")},
		{TEXT("DefinitionId"), TEXT("ItemId")},
		{TEXT("RodDefinitionId"), TEXT("RodItemId")},
		{TEXT("BaitDefinitionId"), TEXT("BaitItemId")},
		{TEXT("FloatDefinitionId"), TEXT("FloatItemId")},
		{TEXT("ScoopNetDefinitionId"), TEXT("ScoopNetItemId")},
		{TEXT("ChumDefinitionId"), TEXT("ChumItemId")},
		{TEXT("CompatibleRodDefinitionIds"), TEXT("CompatibleRodItemIds")},
		{TEXT("EquipmentSelectionBySlot"), TEXT("EquipmentItemBySlot")}
	};

	// 结构迁移分两遍调用：预检遍只读取，应用遍才写数字并清旧字段；不沿 UObject 引用递归，避免修改其他资产或循环引用。
	// 嵌套结构和结构数组在同一对象边界内递归；其他 Map/Set 不作通用递归，仅转换明确登记的身份映射。
	bool ConvertStruct(const UStruct* Type, void* Data, bool bApply, FString& Error, TMap<const void*, int32>& Planned)
	{
		for (TFieldIterator<FProperty> It(Type); It; ++It)
		{
			FProperty* Property = *It;
			if (const FName* Target = LegacyFields.Find(Property->GetFName()))
			{
				if (FNameProperty* Old = CastField<FNameProperty>(Property))
				{
					// 旧 None 不提供迁移来源；有来源时，数字目标只能为空或同值，不能用旧记录覆盖另一种物品。
					const FName Name = Old->GetPropertyValue_InContainer(Data);
					if (Name.IsNone()) continue;
					FIntProperty* Numeric = FindFProperty<FIntProperty>(Type, *Target);
					const int32* Id = LegacyItemIds.Find(Name);
					const void* Address = Numeric ? Numeric->ContainerPtrToValuePtr<void>(Data) : nullptr;
					// 继承层可能有多个旧字段指向同一个数字字段；按目标地址记录计划，预检阶段就拒绝彼此冲突的身份。
					const int32* Prior = Planned.Find(Address);
					if (!Numeric || !Id || (Prior && *Prior != *Id) || (Numeric->GetPropertyValue_InContainer(Data) != 0
						&& Numeric->GetPropertyValue_InContainer(Data) != *Id))
					{
						Error = FString::Printf(TEXT("Unknown or conflicting item reference: %s.%s=%s"),
							*Type->GetName(), *Property->GetName(), *Name.ToString());
						return false;
					}
					Planned.Add(Address, *Id);
					if (bApply)
					{
						Numeric->SetPropertyValue_InContainer(Data, *Id);
						Old->SetPropertyValue_InContainer(Data, NAME_None);
					}
					continue;
				}
				if (FMapProperty* Old = CastField<FMapProperty>(Property))
				{
					// 装备选择保留原槽位键，仅转换物品值；同键数字值冲突则拒绝，应用时合并且清空旧表。
					FScriptMapHelper Source(Old, Old->ContainerPtrToValuePtr<void>(Data));
					if (!Source.Num()) continue;
					FMapProperty* Numeric = FindFProperty<FMapProperty>(Type, *Target);
					FNameProperty* Names = CastField<FNameProperty>(Old->ValueProp);
					FIntProperty* Numbers = Numeric ? CastField<FIntProperty>(Numeric->ValueProp) : nullptr;
					if (!Names || !Numbers || !Old->KeyProp->SameType(Numeric->KeyProp))
					{ Error = TEXT("Unsupported legacy item map"); return false; }
					FScriptMapHelper Destination(Numeric, Numeric->ContainerPtrToValuePtr<void>(Data));
					for (int32 Index = 0; Index < Source.GetMaxIndex(); ++Index)
					{
						if (!Source.IsValidIndex(Index)) continue;
						const int32* Id = LegacyItemIds.Find(Names->GetPropertyValue(Source.GetValuePtr(Index)));
						const int32 Found = Destination.FindMapIndexWithKey(Source.GetKeyPtr(Index));
						if (!Id || (Found != INDEX_NONE && Numbers->GetPropertyValue(Destination.GetValuePtr(Found)) != *Id))
						{ Error = TEXT("Unknown or conflicting legacy equipment selection"); return false; }
						if (bApply) Destination.AddPair(Source.GetKeyPtr(Index), Id);
					}
					if (bApply) Source.EmptyValues();
					continue;
				}
				if (FArrayProperty* Old = CastField<FArrayProperty>(Property))
				{
					// 兼容列表保留顺序；已有数字列表必须与转换结果逐项一致，不能静默合并成更宽的兼容范围。
					FScriptArrayHelper Source(Old, Old->ContainerPtrToValuePtr<void>(Data));
					if (!Source.Num()) continue;
					FArrayProperty* Numeric = FindFProperty<FArrayProperty>(Type, *Target);
					FNameProperty* Names = CastField<FNameProperty>(Old->Inner);
					FIntProperty* Numbers = Numeric ? CastField<FIntProperty>(Numeric->Inner) : nullptr;
					if (!Names || !Numbers) { Error = TEXT("Unsupported legacy item array"); return false; }
					FScriptArrayHelper Destination(Numeric, Numeric->ContainerPtrToValuePtr<void>(Data));
					TArray<int32> Converted;
					for (int32 Index = 0; Index < Source.Num(); ++Index)
					{
						const int32* Id = LegacyItemIds.Find(Names->GetPropertyValue(Source.GetRawPtr(Index)));
						if (!Id) { Error = TEXT("Unknown legacy item in array"); return false; }
						Converted.Add(*Id);
					}
					if (Destination.Num() && Destination.Num() != Converted.Num())
					{ Error = TEXT("Conflicting item array lengths"); return false; }
					for (int32 Index = 0; Index < Destination.Num(); ++Index)
						if (Numbers->GetPropertyValue(Destination.GetRawPtr(Index)) != Converted[Index])
						{ Error = TEXT("Conflicting item array values"); return false; }
					if (bApply)
					{
						Destination.Resize(Converted.Num());
						for (int32 Index = 0; Index < Converted.Num(); ++Index)
							Numbers->SetPropertyValue(Destination.GetRawPtr(Index), Converted[Index]);
						Source.EmptyValues();
					}
					continue;
				}
			}
			if (FStructProperty* Nested = CastField<FStructProperty>(Property))
			{
				if (!ConvertStruct(Nested->Struct, Nested->ContainerPtrToValuePtr<void>(Data), bApply, Error, Planned)) return false;
			}
			else if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
			{
				if (FStructProperty* Element = CastField<FStructProperty>(Array->Inner))
				{
					FScriptArrayHelper Values(Array, Array->ContainerPtrToValuePtr<void>(Data));
					for (int32 Index = 0; Index < Values.Num(); ++Index)
						if (!ConvertStruct(Element->Struct, Values.GetRawPtr(Index), bApply, Error, Planned)) return false;
				}
			}
		}
		return true;
	}
}

// 对象迁移流程：先验证全部可迁移字段，成功才执行第二遍写入；失败不修改任何字段，也不保存磁盘文件。
// DataTable 的行是独立内存块，必须连同全部行一起预检，不能只检查 UObject 外壳。
bool UCatInventorySettings::MigrateLegacyItemReferences(UObject* Object, FString& OutError)
{
	OutError.Reset();
	if (!Object) { OutError = TEXT("Missing migration object"); return false; }
	UDataTable* Table = Cast<UDataTable>(Object);
	TMap<const void*, int32> Planned;
	for (const bool bApply : {false, true})
	{
		if (!ConvertStruct(Object->GetClass(), Object, bApply, OutError, Planned)) return false;
		if (Table)
			for (const auto& Pair : Table->GetRowMap())
				if (!ConvertStruct(Table->GetRowStruct(), Pair.Value, bApply, OutError, Planned)) return false;
	}
	return true;
}
