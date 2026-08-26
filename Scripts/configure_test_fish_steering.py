"""Configure the test fish for the angle-driven steering fight model.

Run from the Unreal Editor Python console with:
    py "D:/develop/Catfishing/Scripts/configure_test_fish_steering.py"

The script deliberately touches only the two test Data Assets. Keeping the
values here also gives designers a reproducible baseline after asset merges.
"""

import unreal


FISH_ASSET_PATH = "/Game/Data/Fish/DA_Fish_Test01"
PERSONALITY_ASSET_PATH = "/Game/Data/Fish/DA_Fight_Test01"


def _load_asset(asset_path):
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise RuntimeError(f"Could not load asset: {asset_path}")
    return asset


def _set(asset, property_name, value):
    previous = asset.get_editor_property(property_name)
    asset.set_editor_property(property_name, value)
    unreal.log(
        f"Catfishing test tuning: {asset.get_name()}.{property_name} "
        f"{previous} -> {value}"
    )


def _save(asset_path):
    if not unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save asset: {asset_path}")


def main():
    personality = _load_asset(PERSONALITY_ASSET_PATH)
    _set(
        personality,
        "direction_retarget_duration_range_seconds",
        unreal.Vector2D(x=0.6, y=1.4),
    )
    _set(personality, "struggle_movement_speed_centimeters_per_second", 120.0)
    _set(personality, "maximum_turn_rate_degrees_per_second", 120.0)
    _set(personality, "struggle_outward_direction_bias", 0.60)
    _set(personality, "calm_inward_direction_bias", 0.65)
    _set(personality, "lateral_movement_bias", 0.60)
    _set(personality, "feint_probability", 0.10)
    _set(personality, "full_stamina_inward_probability", 0.25)
    _set(personality, "exhausted_inward_probability", 0.80)
    _set(personality, "inward_probability_exponent", 1.10)
    _set(personality, "inward_cone_half_angle_degrees", 60.0)
    _set(personality, "strong_confrontation_alignment_threshold", 0.55)
    _set(personality, "strong_confrontation_confirmation_seconds", 0.20)
    _set(personality, "angle_strength_exponent", 1.0)
    _save(PERSONALITY_ASSET_PATH)

    fish = _load_asset(FISH_ASSET_PATH)
    _set(fish, "fish_fight_stamina", 80.0)
    _save(FISH_ASSET_PATH)

    unreal.log("Catfishing test fish steering configuration completed.")


if __name__ == "__main__":
    main()
