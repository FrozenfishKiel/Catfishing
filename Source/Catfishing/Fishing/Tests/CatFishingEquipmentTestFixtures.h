#pragma once

#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#include "Equipment/Fragments/CatEquipmentFragment_Bait.h"
#include "Equipment/Fragments/CatEquipmentFragment_Float.h"
#include "Equipment/Fragments/CatEquipmentFragment_Scoop.h"
#include "Inventory/CatInventorySettings.h"

namespace CatFishingTest
{
	/** Test fixture definitions explicitly own the same fragments as production assets. */
	template<class T>
	T* Fragment(UCatEquipmentDefinition* Definition)
	{
		if (T* Existing = Definition->FindFragment<T>()) return Existing;
		T* Created = NewObject<T>(Definition);
		Definition->Fragments.Add(Created);
		return Created;
	}
	template<class T>
	const T* Fragment(const UCatEquipmentDefinition* Definition)
	{
		return Definition->FindFragment<T>();
	}

	enum class EFixtureKind { Rod, Bait, Float, ScoopNet, Driftwood };
	inline void Configure(UCatEquipmentDefinition* Definition, EFixtureKind Kind)
	{
		switch (Kind)
		{
		case EFixtureKind::Rod: Definition->LoadoutSlotId = UCatEquipmentDefinition::FishingRodLoadoutSlotId(); Fragment<UCatEquipmentFragment_Rod>(Definition); break;
		case EFixtureKind::Bait: Definition->LoadoutSlotId = UCatEquipmentDefinition::FishingBaitLoadoutSlotId(); Fragment<UCatEquipmentFragment_Bait>(Definition); break;
		case EFixtureKind::Float: Definition->LoadoutSlotId = UCatEquipmentDefinition::FishingFloatLoadoutSlotId(); Fragment<UCatEquipmentFragment_Float>(Definition); break;
		case EFixtureKind::ScoopNet: Definition->LoadoutSlotId = UCatEquipmentDefinition::ScoopNetLoadoutSlotId(); Fragment<UCatEquipmentFragment_Scoop>(Definition); break;
		case EFixtureKind::Driftwood: Definition->LoadoutSlotId = NAME_None; break;
		}
	}
	inline UCatInventoryComponent* Inventory(const UCatEquipmentComponent* Equipment)
	{
		return Equipment->GetOwner()->FindComponentByClass<UCatInventoryComponent>();
	}
	inline TArray<FCatInventoryEntry> Entries(const UCatEquipmentComponent* Equipment)
	{
		return Inventory(Equipment)->GetInventoryEntries();
	}
	inline FName DefinitionId(const FCatInventoryEntry& Entry) { return Entry.Instance ? Entry.Instance->GetItemDefinitionId() : NAME_None; }
	inline FGuid InstanceId(const FCatInventoryEntry& Entry) { return Entry.Instance ? Entry.Instance->GetItemInstanceId() : FGuid(); }
	inline double Durability(const FCatInventoryEntry& Entry)
	{
		const auto* Instance = Cast<UCatEquipmentInventoryItemInstance>(Entry.Instance);
		return Instance ? Instance->GetRodDurability() : 0.0;
	}
	inline bool Broken(const FCatInventoryEntry& Entry)
	{
		const auto* Instance = Cast<UCatEquipmentInventoryItemInstance>(Entry.Instance);
		return Instance && Instance->IsRodBroken();
	}
	inline ECatDomainCommandError ReadHeldRod(const UCatEquipmentComponent* Equipment, FGuid Id, FCatInventoryEntry& Out)
	{
		const auto* Entry = Inventory(Equipment)->FindHeldInventoryEntryFromAuthority(Id);
		if (!Entry) return ECatDomainCommandError::NotFound;
		Out = *Entry;
		return Equipment->IsFishingRodInUse(Id) ? ECatDomainCommandError::InvalidPhase : ECatDomainCommandError::None;
	}
}
