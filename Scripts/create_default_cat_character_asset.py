"""Create or refresh the formal default cat definition.

Run from the Unreal Editor Python console with:
    py "D:/develop/Catfishing/Scripts/create_default_cat_character_asset.py"

The saved DataAsset is the per-cat tuning source. ``CatAbilitySettings`` binds
it through ``DefaultCharacterDefinitionId`` when a character Blueprint leaves
``CatDefinitionId`` as None. The global values remain the final fallback when
no default definition ID is configured.
"""

import unreal


ASSET_NAME = "Cat_Default"
PACKAGE_PATH = "/Game/Catfishing/Data/Character"
ASSET_PATH = f"{PACKAGE_PATH}/{ASSET_NAME}"
CAT_DEFINITION_ID = unreal.Name("DefaultCat")

INITIAL_POISON = 0.0
FISHING_STRENGTH = 50.0
FIGHT_STAMINA_MAXIMUM = 100.0


def _create_or_load_definition():
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
        asset = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
        if asset is None:
            raise RuntimeError(f"default cat definition exists but cannot be loaded: {ASSET_PATH}")
        return asset, False

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.CatCharacterDefinition)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        ASSET_NAME,
        PACKAGE_PATH,
        unreal.CatCharacterDefinition,
        factory,
    )
    if asset is None:
        raise RuntimeError(f"failed to create default cat definition: {ASSET_PATH}")
    return asset, True


def main():
    asset, created = _create_or_load_definition()
    if not isinstance(asset, unreal.CatCharacterDefinition):
        raise RuntimeError(f"wrong asset type: path={ASSET_PATH} actual={type(asset).__name__}")

    asset.set_editor_property("cat_definition_id", CAT_DEFINITION_ID)
    asset.set_editor_property("display_name", "Default Cat")
    asset.set_editor_property("initial_poison", INITIAL_POISON)
    asset.set_editor_property("fishing_strength", FISHING_STRENGTH)
    asset.set_editor_property("fight_stamina_maximum", FIGHT_STAMINA_MAXIMUM)
    asset.set_editor_property("enable_runtime_definition", True)

    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"failed to save default cat definition: {ASSET_PATH}")

    unreal.log(
        "DEFAULT_CAT_CHARACTER_ASSET_PASS "
        f"Asset={asset.get_path_name()} DefinitionId={CAT_DEFINITION_ID} "
        f"Poison={INITIAL_POISON:.1f} FishingStrength={FISHING_STRENGTH:.1f} "
        f"FightStaminaMaximum={FIGHT_STAMINA_MAXIMUM:.1f} "
        f"Created={created}"
    )


main()
