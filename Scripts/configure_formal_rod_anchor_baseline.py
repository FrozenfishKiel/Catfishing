"""Copy the current shared rod visual's canonical anchors to formal rod assets.

Rod_Basic is the calibrated baseline for BP_CatFishingRodActor. StarterRodT1
and ShopRodT2 currently use the same actor/visual, so their gameplay rod-tip
anchor must match until they receive dedicated art and per-model calibration.
"""

import unreal


SOURCE_ROD_PATH = "/Game/Data/Equipment/DA_Rod_Basic"
TARGET_ROD_PATHS = (
    "/Game/Catfishing/Data/Equipment/Equip_Rod_StarterT1",
    "/Game/Catfishing/Data/Equipment/Equip_Rod_ShopT2",
)


def _load(path):
    """Load a rod definition so anchor copying fails early when an asset path is stale."""
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise RuntimeError(f"Could not load rod definition: {path}")
    return asset


def main():
    """Copy only rod anchor transforms from the calibrated baseline into formal rod definitions."""
    source = _load(SOURCE_ROD_PATH)
    source_tip = source.get_editor_property("rod_tip_local_transform")
    source_stand = source.get_editor_property("stand_local_transform")
    source_grip = source.get_editor_property("grip_local_transform")

    for target_path in TARGET_ROD_PATHS:
        target = _load(target_path)
        previous_tip = target.get_editor_property("rod_tip_local_transform")
        target.set_editor_property("rod_tip_local_transform", source_tip)
        target.set_editor_property("stand_local_transform", source_stand)
        target.set_editor_property("grip_local_transform", source_grip)
        if not unreal.EditorAssetLibrary.save_asset(
            target_path, only_if_is_dirty=False
        ):
            raise RuntimeError(f"Could not save rod definition: {target_path}")
        unreal.log(
            f"Catfishing rod anchor baseline path={target_path} "
            f"tip={previous_tip} -> {source_tip}"
        )


if __name__ == "__main__":
    main()
