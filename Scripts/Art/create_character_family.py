"""One-time character/template migration after import and action retargeting.

Use the matching, freshly compiled Editor module. Originals are backed up under Saved.
The C++ authoring entry refuses existing output assets; it is never a startup migration.
"""
from pathlib import Path
import shutil
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
content = Path(unreal.Paths.project_content_dir()).resolve()
backup = project / "Saved" / "CharacterFamilyBackup"
backup.mkdir(parents=True, exist_ok=True)
for relative in ("Character/BP_CatCharacter.uasset", "Animalia/Cat/ABP_Cat.uasset",
                 "Characters/CuteCat/Meshes/SK_CuteCat_Skeleton.uasset"):
    target = backup / relative
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(content / relative, target)
result = unreal.CatCharacterVariantAuthoringLibrary.create_character_family()
unreal.log(result)
if result.startswith("ERROR:"):
    raise RuntimeError(result)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
unreal.log("Event=character_family_referencers Original=" + str(registry.get_referencers(
    "/Game/Character/BP_CatCharacter", unreal.AssetRegistryDependencyOptions())))
