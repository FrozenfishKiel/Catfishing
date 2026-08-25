"""Create and verify the formal BodyAction Montage assets used by DefaultGame.

This script runs inside Unreal Editor Python. It converts existing Animalia cat
AnimSequence assets into saved UAnimMontage assets under one BodyAction package
root, so the C++ BodyAction presentation settings can load real Montage assets
without putting asset-generation code on the runtime path.
"""

import unreal


PACKAGE_ROOT = "/Game/Catfishing/Animation/BodyAction"

BODY_ACTION_MONTAGES = [
    ("AM_BodyAction_RequestSacrifice", "/Game/Animalia/Cat/Animations/InPlace/Sitting_02-IP.Sitting_02-IP"),
    ("AM_BodyAction_CampRest", "/Game/Animalia/Cat/Animations/InPlace/Lying_00-IP.Lying_00-IP"),
    ("AM_BodyAction_CampfirePlayback", "/Game/Animalia/Cat/Animations/InPlace/Sitting_01-IP.Sitting_01-IP"),
    ("AM_BodyAction_TransferFishToTank", "/Game/Animalia/Cat/Animations/InPlace/Stand_00-IP.Stand_00-IP"),
    ("AM_BodyAction_RescueCharacterToCamp", "/Game/Animalia/Cat/Animations/InPlace/Trans_Sitting_To_Stand-IP.Trans_Sitting_To_Stand-IP"),
    ("AM_BodyAction_RepairRodAtCamp", "/Game/Animalia/Cat/Animations/InPlace/Action_Scratching-IP.Action_Scratching-IP"),
    ("AM_BodyAction_UseHerbOnCharacter", "/Game/Animalia/Cat/Animations/InPlace/Stand_Drinking_01-IP.Stand_Drinking_01-IP"),
    ("AM_BodyAction_ConsumeFish", "/Game/Animalia/Cat/Animations/InPlace/Eating_01-IP.Eating_01-IP"),
    ("AM_BodyAction_BeginTheft", "/Game/Animalia/Cat/Animations/InPlace/Loco_Sneak-IP.Loco_Sneak-IP"),
    ("AM_BodyAction_CatchTheft", "/Game/Animalia/Cat/Animations/InPlace/Attack_Left-IP.Attack_Left-IP"),
    ("AM_BodyAction_RequestManualHelp", "/Game/Animalia/Cat/Animations/InPlace/Agressive_01-IP.Agressive_01-IP"),
    ("AM_BodyAction_RequestMischief", "/Game/Animalia/Cat/Animations/InPlace/Attack_Right-IP.Attack_Right-IP"),
    ("AM_BodyAction_PlaceProtectionSign", "/Game/Animalia/Cat/Animations/InPlace/Action_Scratching-IP.Action_Scratching-IP"),
    ("AM_BodyAction_CompleteShakeDry", "/Game/Animalia/Cat/Animations/InPlace/Stand_03_LookAround-IP.Stand_03_LookAround-IP"),
]


def _load_asset(path, expected_type):
    """Load one existing asset and fail loudly if the configured source path drifts."""
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise RuntimeError(f"missing source asset: {path}")
    if not isinstance(asset, expected_type):
        raise RuntimeError(f"asset has wrong type: {path} expected={expected_type.__name__} actual={type(asset).__name__}")
    return asset


def _create_or_load_montage(asset_name, source_animation):
    """Create a Montage from one sequence only when it is missing; existing Montage assets are left for art polish."""
    asset_path = f"{PACKAGE_ROOT}/{asset_name}.{asset_name}"
    montage = unreal.EditorAssetLibrary.load_asset(asset_path)
    created = False
    if montage is None:
        factory = unreal.AnimMontageFactory()
        factory.set_editor_property("source_animation", source_animation)
        montage = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name,
            PACKAGE_ROOT,
            unreal.AnimMontage,
            factory,
        )
        created = True
    if montage is None:
        raise RuntimeError(f"failed to create montage: {asset_path}")
    if not isinstance(montage, unreal.AnimMontage):
        raise RuntimeError(f"asset has wrong montage type: {asset_path} actual={type(montage).__name__}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(montage, only_if_is_dirty=False):
        raise RuntimeError(f"failed to save montage: {asset_path}")
    unreal.log(f"BODY_ACTION_MONTAGE_ASSET_READY Asset={asset_path} Source={source_animation.get_path_name()} Created={created}")
    return montage


def main():
    """Generate every BodyAction Montage in one pass and emit the stable evidence marker."""
    created_or_verified = 0
    for asset_name, source_path in BODY_ACTION_MONTAGES:
        source_animation = _load_asset(source_path, unreal.AnimSequence)
        _create_or_load_montage(asset_name, source_animation)
        created_or_verified += 1
    unreal.log(f"CREATE_BODY_ACTION_MONTAGE_ASSETS_PASS AssetCount={created_or_verified} Directory={PACKAGE_ROOT}")


main()
