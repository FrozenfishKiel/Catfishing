"""Import the Work01 Feishu-derived runtime catalog into real Unreal DataAssets and default settings.

Run from the project root with UnrealEditor-Cmd and -ExecutePythonScript. The script is intentionally
idempotent: it updates assets in place, saves their packages, and rewrites only the catalog settings it owns.
"""

import json
import pathlib
import re
from typing import Any, Iterable

import unreal

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
MANIFEST_PATH = SCRIPT_DIR / "Data" / "work01_runtime_catalog.json"


def _snake_case(name: str) -> str:
    """Convert C++-style property names into Unreal Python property names without guessing gameplay meaning."""
    first = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", first).lower()


def _property_candidates(name: str) -> list[str]:
    """Return the small set of property spellings Unreal Python may expose for the same reflected C++ field."""
    candidates = [name, _snake_case(name), name[:1].lower() + name[1:]]
    if name.startswith("b") and len(name) > 1 and name[1].isupper():
        stripped = name[1:]
        candidates.extend([stripped[:1].lower() + stripped[1:], _snake_case(stripped)])
    deduped: list[str] = []
    for candidate in candidates:
        if candidate and candidate not in deduped:
            deduped.append(candidate)
    return deduped


def _set_prop(obj: Any, name: str, value: Any) -> None:
    """Write one reflected property and fail with a useful field name if Unreal rejects every spelling."""
    errors: list[str] = []
    for candidate in _property_candidates(name):
        try:
            obj.set_editor_property(candidate, value)
            return
        except Exception as exc:  # Unreal raises generic Python exceptions for unknown reflected fields.
            errors.append(f"{candidate}: {exc}")
    raise RuntimeError(f"Unable to set {name} on {obj}: {' | '.join(errors)}")


def _enum_value(enum_class: Any, manifest_name: str) -> Any:
    """Resolve a manifest enum token against Unreal Python's generated enum member names."""
    normalized = manifest_name.replace("-", "_").replace(" ", "_").lower()
    for attr in dir(enum_class):
        if attr.startswith("_"):
            continue
        attr_normalized = attr.lower()
        if attr_normalized == normalized or attr_normalized.endswith("_" + normalized):
            return getattr(enum_class, attr)
    raise RuntimeError(f"Unable to resolve enum value {manifest_name} on {enum_class}")


def _ensure_directory(asset_root: str) -> None:
    """Create the target Content folder before asset creation; existing folders are left untouched."""
    if not unreal.EditorAssetLibrary.does_directory_exist(asset_root):
        if not unreal.EditorAssetLibrary.make_directory(asset_root):
            raise RuntimeError(f"Unable to create asset directory: {asset_root}")


def _load_or_create_primary_data_asset(asset_root: str, asset_name: str, asset_class: Any) -> Any:
    """Load an existing generated asset or create a new primary data asset with the requested class."""
    _ensure_directory(asset_root)
    asset_path = f"{asset_root}/{asset_name}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        asset = unreal.load_asset(asset_path)
        if not isinstance(asset, asset_class):
            raise RuntimeError(f"Existing asset has unexpected class: {asset_path}")
        return asset
    factory = unreal.DataAssetFactory()
    _set_prop(factory, "DataAssetClass", asset_class)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name, asset_root, asset_class, factory)
    if asset is None:
        raise RuntimeError(f"Unable to create asset: {asset_path}")
    return asset


def _set_source_stamp(settings: Any, stamp: dict[str, Any]) -> None:
    """Write the offline source stamp into DeveloperSettings so runtime validation can trace this landing."""
    source_stamp = unreal.CatDataCatalogSourceStamp()
    _set_prop(source_stamp, "SourceKind", stamp["source_kind"])
    _set_prop(source_stamp, "SourceNodeToken", stamp["source_node_token"])
    _set_prop(source_stamp, "SourceRevision", int(stamp["source_revision"]))
    _set_prop(source_stamp, "SourceSliceName", stamp["source_slice_name"])
    _set_prop(settings, "SourceStamp", source_stamp)


def _set_soft_definition_array(settings: Any, property_name: str, assets: Iterable[Any]) -> None:
    """Populate a TSoftObjectPtr array using the representation accepted by the current Unreal Python bridge."""
    asset_list = list(assets)
    representations = [
        asset_list,
        [asset.get_path_name() for asset in asset_list],
        [unreal.SoftObjectPath(asset.get_path_name()) for asset in asset_list] if hasattr(unreal, "SoftObjectPath") else [],
    ]
    errors: list[str] = []
    for value in representations:
        if not value and asset_list:
            continue
        try:
            _set_prop(settings, property_name, value)
            return
        except Exception as exc:
            errors.append(str(exc))
    raise RuntimeError(f"Unable to set soft reference array {property_name}: {' | '.join(errors)}")


def _import_fish(manifest: dict[str, Any]) -> list[Any]:
    """Create or update FishDefinition assets from the landed fish-table subset."""
    catalog = manifest["catalogs"]["fish"]
    fish_class = unreal.CatFishDefinition
    body_enum = unreal.CatFishBodyClass
    chum_affinity_enum = unreal.CatChumAffinity
    food_enum = unreal.CatFishFoodSafety
    assets: list[Any] = []
    for row in catalog["definitions"]:
        asset = _load_or_create_primary_data_asset(catalog["asset_root"], row["asset_name"], fish_class)
        _set_prop(asset, "bEnableRuntimeDefinition", True)
        _set_prop(asset, "FishDefinitionId", row["id"])
        _set_prop(asset, "BodyClass", _enum_value(body_enum, row["body_class"]))
        _set_prop(asset, "SacrificeContribution", int(row["sacrifice"]))
        _set_prop(asset, "CaptureImprintEventId", "")
        # Locations and chum affinity are per-row source columns, so they are read from the row and never
        # from a shared mapping: sharing one value across all rows would erase the real per-fish distribution.
        _set_prop(asset, "RegionIds", list(row["regions"]))
        _set_prop(asset, "ChumAffinities", [_enum_value(chum_affinity_enum, value) for value in row["chum_affinity"]])
        _set_prop(asset, "MinimumWeightKilograms", float(row["min_kg"]))
        _set_prop(asset, "MaximumWeightKilograms", float(row["max_kg"]))
        _set_prop(asset, "MinimumFightParticipants", int(row["min_participants"]))
        _set_prop(asset, "FishStrength", float(row["max_kg"]) * float(row["strength_coefficient"]))
        _set_prop(asset, "FishFightStamina", float(row["fight_stamina"]))
        _set_prop(asset, "PreferredSpecialBaitIds", list(row.get("preferred_special_bait_ids", [])))
        _set_prop(asset, "FoodSafety", _enum_value(food_enum, row.get("food_safety", "Unset")))
        _set_prop(asset, "PoisonIncrease", float(row.get("poison_increase", 0.0)))
        _set_prop(asset, "bTankDisplayEligible", bool(row.get("tank", False)))
        unreal.EditorAssetLibrary.save_loaded_asset(asset)
        assets.append(asset)
    return assets


def _chum_vector(row: dict[str, Any]) -> Any:
    """Convert one manifest chum contribution into the reflected FCatChumVector struct."""
    vector = unreal.CatChumVector()
    chum = row.get("chum", {})
    _set_prop(vector, "Fishy", float(chum.get("fishy", 0.0)))
    _set_prop(vector, "Fragrant", float(chum.get("fragrant", 0.0)))
    _set_prop(vector, "Fermented", float(chum.get("fermented", 0.0)))
    return vector


def _import_equipment(manifest: dict[str, Any]) -> list[Any]:
    """Create or update EquipmentDefinition assets from equipment, bait, float and chum source slices."""
    catalog = manifest["catalogs"]["equipment"]
    equipment_class = unreal.CatEquipmentDefinition
    kind_enum = unreal.CatEquipmentKind
    assets: list[Any] = []
    for row in catalog["definitions"]:
        asset = _load_or_create_primary_data_asset(catalog["asset_root"], row["asset_name"], equipment_class)
        kind = row["kind"]
        is_special_bait = bool(row.get("special_bait", False))
        _set_prop(asset, "bEnableRuntimeDefinition", True)
        _set_prop(asset, "EquipmentDefinitionId", row["id"])
        _set_prop(asset, "Kind", _enum_value(kind_enum, kind))
        _set_prop(asset, "LoadoutSlotId", row.get("slot", ""))
        _set_prop(asset, "RequiredUnlockId", row.get("required_unlock", ""))
        _set_prop(asset, "bRunConsumable", bool(row.get("run_consumable", False)))
        _set_prop(asset, "bSpecialBait", is_special_bait)
        _set_prop(asset, "MaximumRodDurability", float(row.get("max_rod_durability", 0.0)))
        _set_prop(asset, "RodStrength", float(row.get("rod_strength", 0.0)))
        _set_prop(asset, "MaximumLineLengthMeters", float(row.get("max_line_meters", 0.0)))
        # 漂的射程与精准度偏移半径：只有 Float 行在 manifest 里给这两个键，其余类别落 0，与目录校验"非 Float 必须为 0"一致。
        _set_prop(asset, "FloatCastRangeMeters", float(row.get("float_range_meters", 0.0)))
        _set_prop(asset, "FloatAccuracyOffsetRadiusMeters", float(row.get("float_accuracy_radius_meters", 0.0)))
        _set_prop(asset, "FunctionalRouteId", row["route"])
        _set_prop(asset, "ChumContribution", _chum_vector(row))
        unreal.EditorAssetLibrary.save_loaded_asset(asset)
        assets.append(asset)
    return assets


def _script_class(class_name: str) -> Any:
    """Load a native project UClass even when Unreal Python does not publish it as a module attribute."""
    loaded = unreal.load_class(None, f"/Script/Catfishing.{class_name}")
    if loaded is None:
        raise RuntimeError(f"Unable to load native class: {class_name}")
    return loaded


def _mutable_default(cls: Any) -> Any:
    """Return the mutable CDO for DeveloperSettings across Unreal Python versions."""
    if hasattr(unreal, "get_mutable_default"):
        return unreal.get_mutable_default(cls)
    if hasattr(unreal, "get_default_object"):
        return unreal.get_default_object(cls)
    return cls.get_default_object()


def _asset_config_path(asset: Any) -> str:
    """Return the stable object path used by config soft references."""
    return asset.get_path_name()


def _ini_escape(value: Any) -> str:
    """Escape one scalar for an Unreal ini value without changing its semantic token."""
    text = str(value).replace("\\", "\\\\").replace('"', '\\"')
    return f'"{text}"'


def _source_stamp_ini(stamp: dict[str, Any]) -> str:
    """Serialize the source stamp exactly as the Config USTRUCT expects it."""
    return (
        f'(SourceKind={_ini_escape(stamp["source_kind"])},'
        f'SourceNodeToken={_ini_escape(stamp["source_node_token"])},'
        f'SourceRevision={int(stamp["source_revision"])},'
        f'SourceSliceName={_ini_escape(stamp["source_slice_name"])})'
    )


def _replace_ini_section(text: str, section_name: str, section_body: str) -> str:
    """Replace only one owned Settings section, leaving all unrelated project config text untouched."""
    pattern = re.compile(rf"(?ms)^\[{re.escape(section_name)}\]\r?\n.*?(?=^\[|\Z)")
    replacement = f"[{section_name}]\n{section_body.rstrip()}\n\n"
    if pattern.search(text):
        return pattern.sub(replacement, text, count=1)
    if text and not text.endswith("\n"):
        text += "\n"
    return text + "\n" + replacement


def _write_default_game_ini(manifest: dict[str, Any], fish_assets: list[Any], equipment_assets: list[Any]) -> None:
    """Persist the generated catalog settings without relying on editor-only SaveConfig Python bindings."""
    fish_catalog = manifest["catalogs"]["fish"]
    equipment_catalog = manifest["catalogs"]["equipment"]
    ini_path = PROJECT_DIR / "Config" / "DefaultGame.ini"
    existing = ini_path.read_text(encoding="utf-8") if ini_path.exists() else ""

    fish_lines = [
        f'ContentSchemaVersion={int(fish_catalog["content_schema_version"])}',
        f'DataRevision={int(fish_catalog["data_revision"])}',
        f'SourceStamp={_source_stamp_ini(fish_catalog["source_stamp"])}',
    ]
    fish_lines.extend(f'+Definitions={_asset_config_path(asset)}' for asset in fish_assets)

    equipment_lines = [
        f'ContentSchemaVersion={int(equipment_catalog["content_schema_version"])}',
        f'DataRevision={int(equipment_catalog["data_revision"])}',
        f'SourceStamp={_source_stamp_ini(equipment_catalog["source_stamp"])}',
    ]
    equipment_lines.extend(f'+Definitions={_asset_config_path(asset)}' for asset in equipment_assets)
    equipment_lines.extend([
        f'ProfileLoadoutTrustPolicy={equipment_catalog["profile_loadout_trust_policy"]}',
        f'StarterRodDefinitionId={equipment_catalog["starter_loadout"]["rod"]}',
        f'StarterBaitDefinitionId={equipment_catalog["starter_loadout"]["bait"]}',
        f'StarterFloatDefinitionId={equipment_catalog["starter_loadout"]["float"]}',
        f'RunConsumableStackCapacity={int(equipment_catalog.get("run_consumable_stack_capacity", 0))}',
        'RodFailureDurabilityLoss=0.0',
        'DriftwoodDefinitionId=None',
    ])

    updated = _replace_ini_section(existing, "/Script/Catfishing.CatFishCatalogSettings", "\n".join(fish_lines))
    updated = _replace_ini_section(updated, "/Script/Catfishing.CatEquipmentSettings", "\n".join(equipment_lines))
    ini_path.write_text(updated, encoding="utf-8")


def _configure_settings(manifest: dict[str, Any], fish_assets: list[Any], equipment_assets: list[Any]) -> None:
    """Write the explicit runtime catalogs and starter loadout into project DeveloperSettings."""
    fish_catalog = manifest["catalogs"]["fish"]
    equipment_catalog = manifest["catalogs"]["equipment"]
    fish_settings = _mutable_default(_script_class("CatFishCatalogSettings"))
    equipment_settings = _mutable_default(_script_class("CatEquipmentSettings"))

    _set_prop(fish_settings, "ContentSchemaVersion", int(fish_catalog["content_schema_version"]))
    _set_prop(fish_settings, "DataRevision", int(fish_catalog["data_revision"]))
    _set_source_stamp(fish_settings, fish_catalog["source_stamp"])
    _set_soft_definition_array(fish_settings, "Definitions", fish_assets)

    _set_prop(equipment_settings, "ContentSchemaVersion", int(equipment_catalog["content_schema_version"]))
    _set_prop(equipment_settings, "DataRevision", int(equipment_catalog["data_revision"]))
    _set_source_stamp(equipment_settings, equipment_catalog["source_stamp"])
    _set_soft_definition_array(equipment_settings, "Definitions", equipment_assets)
    _set_prop(equipment_settings, "ProfileLoadoutTrustPolicy", _enum_value(unreal.CatDomainPolicy, equipment_catalog["profile_loadout_trust_policy"]))
    _set_prop(equipment_settings, "StarterRodDefinitionId", equipment_catalog["starter_loadout"]["rod"])
    _set_prop(equipment_settings, "StarterBaitDefinitionId", equipment_catalog["starter_loadout"]["bait"])
    _set_prop(equipment_settings, "StarterFloatDefinitionId", equipment_catalog["starter_loadout"]["float"])
    _set_prop(equipment_settings, "RunConsumableStackCapacity", int(equipment_catalog.get("run_consumable_stack_capacity", 0)))
    _set_prop(equipment_settings, "RodFailureDurabilityLoss", 0.0)
    _set_prop(equipment_settings, "DriftwoodDefinitionId", "")

    _write_default_game_ini(manifest, fish_assets, equipment_assets)


def main() -> None:
    """Run the whole landing process once: read manifest, create assets, write settings, then save packages."""
    with MANIFEST_PATH.open("r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    fish_assets = _import_fish(manifest)
    equipment_assets = _import_equipment(manifest)
    _configure_settings(manifest, fish_assets, equipment_assets)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.log(f"Catfishing Work01 data catalog imported: fish={len(fish_assets)} equipment={len(equipment_assets)} manifest={MANIFEST_PATH}")


main()