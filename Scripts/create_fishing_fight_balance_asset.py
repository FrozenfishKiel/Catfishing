"""Create or validate the formal designer-facing fishing fight balance asset.

Run from the Unreal Editor Python console with:
    py "D:/develop/Catfishing/Scripts/create_fishing_fight_balance_asset.py"

``UCatFishingSettings`` only stores the soft reference to this asset. The values
below initialize new assets only. Existing designer values are preserved and
validated by the same C++ readiness gate used by runtime. Values are intentionally
absent from ``DefaultGame.ini`` so runtime has one tuning source.
"""

import unreal


ASSET_NAME = "DA_FishingFightBalance_Default"
PACKAGE_PATH = "/Game/Catfishing/Data/Fishing"
ASSET_PATH = f"{PACKAGE_PATH}/{ASSET_NAME}"

VALUES = {
    "balance_definition_id": unreal.Name("DefaultFishingFightBalance"),
    "enable_runtime_definition": True,
    "strength_per_kilogram": 10.0,
    "acceleration_per_strength": 5.0,
    "drive_response_seconds": 1.0,
    "reel_speed_centimeters_per_second": 80.0,
    "exhausted_cat_escape_speed_multiplier": 2.0,
    "cat_stamina_cost_per_strength_centimeter": 0.002,
    "cat_rod_stamina_cost_per_strength_radian": 0.03,
    "cat_unloaded_work_multiplier": 0.15,
    "cat_support_stamina_per_second": 2.0,
    "fish_stamina_cost_per_strength_centimeter": 0.002,
    "cat_movement_stamina_multiplier": 1.0,
    "cat_reel_stamina_multiplier": 1.0,
    "cat_rod_stamina_multiplier": 1.0,
    "cat_hold_stamina_multiplier": 1.0,
    "cat_load_stamina_multiplier": 1.0,
    "fish_load_stamina_multiplier": 1.0,
    "isometric_effort_multiplier": 1.0,
    "slack_stamina_regen_per_second": 3.0,
    "fish_exhaustion_threshold": 0.5,
    "low_stamina_rest_threshold": 0.5,
    "low_stamina_rest_multiplier": 1.5,
    "tension_response_range_centimeters": 10.0,
    "escape_slack_centimeters": 100.0,
    "stalemate_rod_wear_per_fish_strength": 0.1,
    "held_rod_minimum_leverage_multiplier": 0.4,
    "maximum_fish_constraint_correction_speed_centimeters_per_second": 160.0,
    "minimum_carrier_away_speed_multiplier": 0.15,
}


def _create_or_load_definition():
    unreal.EditorAssetLibrary.make_directory(PACKAGE_PATH)
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
        asset = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
        if asset is None:
            raise RuntimeError(f"fight balance exists but cannot be loaded: {ASSET_PATH}")
        return asset, False

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.CatFishingFightBalanceDefinition)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        ASSET_NAME,
        PACKAGE_PATH,
        unreal.CatFishingFightBalanceDefinition,
        factory,
    )
    if asset is None:
        raise RuntimeError(f"failed to create fight balance: {ASSET_PATH}")
    return asset, True


def main():
    asset, created = _create_or_load_definition()
    if not isinstance(asset, unreal.CatFishingFightBalanceDefinition):
        raise RuntimeError(f"wrong asset type: path={ASSET_PATH} actual={type(asset).__name__}")

    if created:
        for property_name, value in VALUES.items():
            asset.set_editor_property(property_name, value)

    if not asset.is_runtime_definition_ready():
        actual_values = {
            property_name: asset.get_editor_property(property_name)
            for property_name in VALUES
        }
        message = (
            "FISHING_FIGHT_BALANCE_ASSET_REJECTED "
            f"Asset={asset.get_path_name()} Created={created} "
            f"PreservedExisting={not created} Reason=RuntimeDefinitionNotReady "
            f"Values={actual_values!r} "
            "Action=ReviewEnableFlagIdAndNumericValuesInEditor"
        )
        unreal.log_error(message)
        raise RuntimeError(message)

    if created and not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"failed to save fight balance: {ASSET_PATH}")

    unreal.log(
        "FISHING_FIGHT_BALANCE_ASSET_PASS "
        f"Asset={asset.get_path_name()} Created={created} "
        f"PreservedExisting={not created} "
        f"BalanceId={asset.get_editor_property('balance_definition_id')} "
        f"StrengthPerKg={asset.get_editor_property('strength_per_kilogram'):.3f} "
        f"AccelerationPerStrength={asset.get_editor_property('acceleration_per_strength'):.3f} "
        f"ExhaustedCatEscapeSpeedMultiplier={asset.get_editor_property('exhausted_cat_escape_speed_multiplier'):.3f} "
        f"CatCost={asset.get_editor_property('cat_stamina_cost_per_strength_centimeter'):.6f} "
        f"CatRodWorkCostPerRadian={asset.get_editor_property('cat_rod_stamina_cost_per_strength_radian'):.6f} "
        f"CatUnloadedWorkMultiplier={asset.get_editor_property('cat_unloaded_work_multiplier'):.3f} "
        f"CatSupportPerSecond={asset.get_editor_property('cat_support_stamina_per_second'):.3f} "
        f"FishCost={asset.get_editor_property('fish_stamina_cost_per_strength_centimeter'):.6f} "
        f"FishIsometricMultiplier={asset.get_editor_property('isometric_effort_multiplier'):.3f} "
        f"CatMovementMultiplier={asset.get_editor_property('cat_movement_stamina_multiplier'):.3f} "
        f"CatReelMultiplier={asset.get_editor_property('cat_reel_stamina_multiplier'):.3f} "
        f"CatRodMultiplier={asset.get_editor_property('cat_rod_stamina_multiplier'):.3f} "
        f"CatHoldMultiplier={asset.get_editor_property('cat_hold_stamina_multiplier'):.3f} "
        f"CatLoadMultiplier={asset.get_editor_property('cat_load_stamina_multiplier'):.3f} "
        f"FishLoadMultiplier={asset.get_editor_property('fish_load_stamina_multiplier'):.3f}"
    )


main()
