"""Create or refresh the formal starter ScoopNet definition used by the temporary development grant.

This runs inside Unreal Editor Python. Runtime code only reads the saved DataAsset; asset generation never runs in game.
"""

import unreal


ASSET_NAME = "Equip_ScoopNet_Starter"
PACKAGE_PATH = "/Game/Catfishing/Data/Equipment"
ASSET_PATH = f"{PACKAGE_PATH}/{ASSET_NAME}"


def _create_or_load():
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
        asset = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
        if asset is None:
            raise RuntimeError(f"formal starter scoop exists but cannot be loaded: {ASSET_PATH}")
        return asset, False

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.CatEquipmentDefinition)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        ASSET_NAME,
        PACKAGE_PATH,
        unreal.CatEquipmentDefinition,
        factory,
    )
    if asset is None:
        raise RuntimeError(f"failed to create formal starter scoop asset: {ASSET_PATH}")
    return asset, True


def main():
    asset, created = _create_or_load()
    if not isinstance(asset, unreal.CatEquipmentDefinition):
        raise RuntimeError(f"wrong asset type: path={ASSET_PATH} actual={type(asset).__name__}")

    asset.set_editor_property("equipment_definition_id", unreal.Name("StarterScoopNet"))
    asset.set_editor_property("kind", unreal.CatEquipmentKind.SCOOP_NET)
    asset.set_editor_property("loadout_slot_id", unreal.Name("ScoopNet"))
    asset.set_editor_property("required_unlock_id", unreal.Name("None"))
    asset.set_editor_property("run_consumable", False)
    asset.set_editor_property("special_bait", False)
    asset.set_editor_property("scoop_reach_centimeters", 200.0)
    asset.set_editor_property("functional_route_id", unreal.Name("Route_Standard"))
    asset.set_editor_property("enable_runtime_definition", True)

    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"failed to save formal starter scoop asset: {ASSET_PATH}")
    unreal.log(
        "FORMAL_STARTER_SCOOP_ASSET_PASS "
        f"Asset={asset.get_path_name()} Definition=StarterScoopNet ReachCm=200 Created={created}"
    )


main()
