"""Apply the reproducible StarterRodT1 rod-durability tuning baseline.

Run from the Unreal Editor Python console with:
    py "D:/develop/Catfishing/Scripts/configure_starter_rod_tuning.py"

MaximumRodDurability defines the full durability of a new or repaired rod.
Fishing consumes the equipment instance's remaining durability across sessions;
this asset authoring script does not repair existing runtime rod instances.
"""

import unreal


ROD_ASSET_PATH = "/Game/Catfishing/Data/Equipment/Equip_Rod_StarterT1"
STARTER_ROD_DURABILITY = 150.0


def main():
    rod = unreal.EditorAssetLibrary.load_asset(ROD_ASSET_PATH)
    if rod is None:
        raise RuntimeError(f"Could not load asset: {ROD_ASSET_PATH}")

    previous = rod.get_editor_property("maximum_rod_durability")
    rod.set_editor_property("maximum_rod_durability", STARTER_ROD_DURABILITY)
    if not unreal.EditorAssetLibrary.save_asset(
        ROD_ASSET_PATH, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Could not save asset: {ROD_ASSET_PATH}")

    unreal.log(
        "Catfishing starter rod tuning completed: "
        f"MaximumRodDurability {previous} -> {STARTER_ROD_DURABILITY}"
    )


if __name__ == "__main__":
    main()
