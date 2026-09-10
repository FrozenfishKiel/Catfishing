"""Restore retarget bone proportions and migrate the audited rod animation consumer.

Run after create_cute_cat_retarget.py and create_character_family.py, with editor assets unlocked.
Both native operations are idempotent. The animation operation also repairs the earlier
root-only normalization and removes translation from the three additive lean poses.
"""
from pathlib import Path
import shutil
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
content = Path(unreal.Paths.project_content_dir()).resolve()
relative = Path("Blueprint/Actors/BP_CatFishingRodActor.uasset")
backup = project / "Saved/CharacterFamilyBackup" / relative
if not backup.exists():
    backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(content / relative, backup)
result = unreal.CatCharacterVariantAuthoringLibrary.normalize_cute_cat_retargeted_animations()
unreal.log(result)
if result.startswith("ERROR:"):
    raise RuntimeError(result)
if not unreal.EditorAssetLibrary.save_directory("/Game/Characters/CuteCat/Animation/Retargeted", only_if_is_dirty=True, recursive=True):
    raise RuntimeError("Failed to save normalized retarget animations")
result = unreal.CatCharacterVariantAuthoringLibrary.migrate_rod_character_consumer()
unreal.log(result)
if result.startswith("ERROR:"):
    raise RuntimeError(result)
