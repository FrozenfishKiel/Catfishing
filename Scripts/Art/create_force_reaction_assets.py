"""Author four one-shot reactions, retarget to CuteCat, normalize units, then bind existing skins.

Run in an idle UE Python commandlet after building CatfishingEditor. Existing clips/montages
are preserved by default. CAT_FORCE_REACTION_REBUILD=1 explicitly regenerates only this
feature's eight sequences. Four directions derive from the original hit's impact/rebound
envelope; they need visual acceptance, not just bone tests.
"""
import os
import unreal


def checked(result):
    unreal.log(result)
    if result.startswith('ERROR:'):
        raise RuntimeError(result)


def main():
    lib = unreal.EditorAssetLibrary
    author = unreal.CatCharacterVariantAuthoringLibrary
    rebuild = os.environ.get('CAT_FORCE_REACTION_REBUILD') == '1'
    checked(author.create_force_reaction_source_clips(rebuild))
    root = '/Game/Characters/CuteCat/Animation/Retargeted'
    assets = []
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for direction in ['Forward', 'Backward', 'Left', 'Right']:
        name = 'AS_Force' + direction
        if rebuild or not lib.does_asset_exist(root + '/' + name):
            source = lib.load_asset('/Game/Animalia/Cat/Animations/ForceReaction/' + name)
            assets.append(registry.get_asset_by_object_path(source.get_path_name()))
    if assets:
        inputs = unreal.IKRetargetBatchOperationInputs()
        inputs.assets_to_retarget = assets
        inputs.source_mesh = lib.load_asset('/Game/Animalia/Cat/Meshes/Cat')
        inputs.target_mesh = lib.load_asset('/Game/Characters/CuteCat/Meshes/SK_CuteCat')
        inputs.ik_retarget_asset = lib.load_asset('/Game/Characters/CuteCat/Rig/RTG_AnimaliaToCuteCat')
        inputs.target_path = root
        inputs.include_referenced_assets = False
        inputs.overwrite_existing_files = rebuild
        if not unreal.IKRetargetBatchOperation.run_batch_retarget(inputs):
            raise RuntimeError('No retarget output')
    checked(author.normalize_cute_cat_retargeted_animations())
    if not lib.save_directory(root, only_if_is_dirty=True, recursive=True):
        raise RuntimeError('Cannot save normalized reactions')
    checked(author.finalize_force_reaction_assets())


if __name__ == '__main__':
    main()
