"""Apply the reproducible StarterRodT1 fishing-line tuning baseline.

Run from the Unreal Editor Python console with:
    py "D:/develop/Catfishing/Scripts/configure_starter_rod_line_tuning.py"

The legacy MaximumRodDurability property is consumed by the fishing simulator
as per-session line durability. It does not permanently damage the rod actor.
"""

import unreal


ROD_ASSET_PATH = "/Game/Catfishing/Data/Equipment/Equip_Rod_StarterT1"
STARTER_LINE_DURABILITY = 150.0


def main():
    rod = unreal.EditorAssetLibrary.load_asset(ROD_ASSET_PATH)
    if rod is None:
        raise RuntimeError(f"Could not load asset: {ROD_ASSET_PATH}")

    previous = rod.get_editor_property("maximum_rod_durability")
    rod.set_editor_property("maximum_rod_durability", STARTER_LINE_DURABILITY)
    if not unreal.EditorAssetLibrary.save_asset(
        ROD_ASSET_PATH, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Could not save asset: {ROD_ASSET_PATH}")

    unreal.log(
        "Catfishing starter line tuning completed: "
        f"MaximumRodDurability {previous} -> {STARTER_LINE_DURABILITY}"
    )


if __name__ == "__main__":
    main()
