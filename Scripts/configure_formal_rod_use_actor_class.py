"""Assign the in-world actor class used by formal rod item definitions.

This script is intentionally separate from rod anchor calibration. The item
definition owns which actor is spawned by Use, while anchor transforms describe
how that actor's mesh lines up after it already exists.
"""

import unreal


ROD_ACTOR_CLASS_PATH = "/Game/Blueprint/Actors/BP_CatFishingRodActor.BP_CatFishingRodActor_C"
TARGET_ROD_PATHS = (
    "/Game/Catfishing/Data/Equipment/Equip_Rod_StarterT1",
    "/Game/Catfishing/Data/Equipment/Equip_Rod_ShopT2",
)


def _load_rod_definition(path):
    """Load one rod definition asset so the caller can update only its Use actor field."""
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise RuntimeError(f"Could not load rod definition: {path}")
    return asset


def _soft_class_path(value):
    """Return a stable text form for Unreal soft class values written in the log."""
    if value is None:
        return "None"
    try:
        return str(value.to_soft_object_path())
    except Exception:
        return str(value)


def main():
    """Write and verify UseActorClass without touching anchor transforms or other item data."""
    rod_actor_class = unreal.load_class(None, ROD_ACTOR_CLASS_PATH)
    if rod_actor_class is None:
        raise RuntimeError(f"Could not load rod actor class: {ROD_ACTOR_CLASS_PATH}")

    for target_path in TARGET_ROD_PATHS:
        target = _load_rod_definition(target_path)
        previous_class = target.get_editor_property("use_actor_class")
        target.set_editor_property("use_actor_class", rod_actor_class)

        if not unreal.EditorAssetLibrary.save_asset(target_path, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save rod definition: {target_path}")

        reloaded = _load_rod_definition(target_path)
        saved_class = reloaded.get_editor_property("use_actor_class")
        saved_path = _soft_class_path(saved_class)
        if ROD_ACTOR_CLASS_PATH not in saved_path:
            raise RuntimeError(
                f"UseActorClass readback mismatch for {target_path}: {saved_path}"
            )

        unreal.log(
            "ROD_USE_ACTOR_CLASS_CONFIGURED "
            f"path={target_path} "
            f"previous={_soft_class_path(previous_class)} "
            f"current={saved_path}"
        )


if __name__ == "__main__":
    main()
