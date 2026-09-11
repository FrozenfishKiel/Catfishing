"""Create the audited Animalia -> CuteCat retargeter and missing action clips.

Run with UE Python in an idle validation editor. Existing outputs are not overwritten.
The imported CuteCat idle/walk/run remain the native locomotion clips.
"""
import json
import re
from pathlib import Path
import unreal

ROOT = "/Game/Characters/CuteCat"
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def create_rig(name, mesh, pelvis, chains):
    path = ROOT + "/Rig/" + name
    rig = LIB.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(
        name, ROOT + "/Rig", unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    require(rig, "Cannot create " + path)
    controller = unreal.IKRigController.get_controller(rig)
    require(controller.set_skeletal_mesh(mesh), "Cannot set rig mesh")
    require(controller.set_retarget_root(pelvis), "Cannot set retarget pelvis")
    existing = {str(chain.chain_name) for chain in controller.get_retarget_chains()}
    for chain, start, end in chains:
        if chain not in existing:
            require(str(controller.add_retarget_chain(chain, start, end, "None")) == chain,
                    "Invalid retarget chain " + chain)
    require(LIB.save_loaded_asset(rig), "Cannot save rig")
    return rig


def main():
    source = LIB.load_asset("/Game/Animalia/Cat/Meshes/Cat")
    target = LIB.load_asset(ROOT + "/Meshes/SK_CuteCat")
    source_chains = []
    target_chains = []
    for chain, old, new, endpoint in [
        ("FrontLeft", "RigLFLeg", "Arm_L_", "Hand_L_001"),
        ("FrontRight", "RigRFLeg", "Arm_R_", "Hand_R_001"),
        ("RearLeft", "RigLBLeg", "Leg_L_", "Foot_L_001"),
        ("RearRight", "RigRBLeg", "Leg_R_", "Foot_R_001"),
    ]:
        source_chains.append((chain, old + "1", old + "Ankle"))
        target_chains.append((chain, new + "001", endpoint))
    for chain, old, new in [
        ("Spine1", "RigSpine1", "Spine_001"),
        ("Spine2", "RigSpine2", "Spine_002"),
        ("Spine3", "RigSpine3", "Spine_003"),
        ("Chest", "RigChest", "Root_002"),
        ("Head", "RigHead", "Head_001"),
        ("Jaw", "RigJaw1", "Jaw_001"),
    ]:
        source_chains.append((chain, old, old))
        target_chains.append((chain, new, new))
    source_chains.append(("Tail", "RigTail1", "RigTail6"))
    target_chains.append(("Tail", "Tail_001", "Tail_009"))
    source_rig = create_rig("IK_AnimaliaCat", source, "RigPelvis", source_chains)
    target_rig = create_rig("IK_CuteCat", target, "Center_001", target_chains)
    retarget_path = ROOT + "/Rig/RTG_AnimaliaToCuteCat"
    retarget = LIB.load_asset(retarget_path) if LIB.does_asset_exist(retarget_path) else TOOLS.create_asset(
        "RTG_AnimaliaToCuteCat", ROOT + "/Rig", unreal.IKRetargeter, unreal.IKRetargetFactory())
    controller = unreal.IKRetargeterController.get_controller(retarget)
    controller.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
    controller.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    controller.remove_all_ops()
    controller.add_default_ops()
    # Export FK/relative-pelvis animation; terrain and reaching IK has one runtime owner.
    controller.remove_retarget_op(2)
    for chain, _, _ in target_chains:
        require(controller.set_source_chain(chain, chain), "Cannot map chain " + chain)
    require(LIB.save_loaded_asset(retarget), "Cannot save retargeter")

    clips = ["JumpX_Start-IP", "JumpX_Loop-IP", "JumpX_End-IP",
             "Trans_Stand_To_Sitting-IP", "Trans_Sitting_To_Lying-IP", "Lying_00-IP",
             "Trans_Lying_To_Sitting-IP", "Trans_Sitting_To_Stand-IP"]
    paths = {"/Game/Animalia/Cat/Animations/InPlace/" + clip for clip in clips}
    paths.add("/Game/Animalia/Cat/Animations/Comp/Comp_Add_Lean")
    paths.add("/Game/Animalia/Cat/AM_Action_Scratching-IP_Montage")
    paths.add("/Game/Animalia/Cat/AM_Attack_Agressive_Legs_01-IP_Montage")
    paths.add("/Game/Animalia/Cat/AM_Attack_Left-IP_Montage")
    config = Path(unreal.Paths.project_config_dir()) / "DefaultGame.ini"
    for line in config.read_text(encoding="utf-8-sig").splitlines():
        if "Montage=" in line:
            paths.update(match.split(".")[0] for match in re.findall(r"/Game/[^\s,)]+", line))
    assets = []
    for path in sorted(paths):
        asset = LIB.load_asset(path)
        require(isinstance(asset, unreal.AnimationAsset), "Missing animation " + path)
        assets.append(unreal.AssetRegistryHelpers.get_asset_registry().get_asset_by_object_path(asset.get_path_name()))
    destination = ROOT + "/Animation/Retargeted"
    # Supplement newly audited consumers without overwriting already reviewed animation assets.
    assets = [asset for asset in assets if not LIB.does_asset_exist(destination + "/" + str(asset.asset_name))]
    if not assets:
        unreal.log("Event=character_variant_retarget_completed Result=ExistingAssetsPreserved")
        return
    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.assets_to_retarget = assets
    inputs.source_mesh = source
    inputs.target_mesh = target
    inputs.ik_retarget_asset = retarget
    inputs.target_path = destination
    inputs.include_referenced_assets = True
    outputs = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
    require(outputs, "Retarget batch produced no assets")
    require(LIB.save_directory(ROOT + "/Animation", only_if_is_dirty=False, recursive=True), "Cannot save retarget output")
    unreal.log("Event=character_variant_retarget_completed " + json.dumps({
        "requested": len(assets), "outputs": [str(asset.package_name) for asset in outputs]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
