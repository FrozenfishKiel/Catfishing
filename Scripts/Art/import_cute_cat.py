"""Import CuteCat into an independent asset folder; never changes the player BP.

Run in UE with -run=pythonscript -script=<this file> -unattended -nop4.
Set CUTE_CAT_SOURCE to the extracted directory containing cutecat_fbx.fbx.
Set CUTE_CAT_FBX to the copy produced by prepare_cute_cat.py.
Set CUTE_CAT_VERIFY_ONLY=1 in a fresh process to verify saved assets.
"""

import json
import os
from pathlib import Path

import unreal


ROOT = "/Game/Characters/CuteCat"
SOURCE = Path(os.environ.get("CUTE_CAT_SOURCE", ""))
VARIANTS = {
    "Calico": "calico",
    "ClassicTabby": "classictabby",
    "SolidGrey": "solidgrey",
    "Tuxedo": "tuxedo",
}
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def emit(event, **fields):
    unreal.log("Event=cute_cat_{} {}".format(event, json.dumps(fields, ensure_ascii=False, default=str)))


def import_file(filename, destination, name, options=None, factory=None, replace_existing=False):
    task = unreal.AssetImportTask()
    task.filename = str(filename)
    task.destination_path = destination
    task.destination_name = name
    task.automated = True
    task.replace_existing = replace_existing
    task.save = True
    if options is not None:
        task.options = options
    if factory is not None:
        task.factory = factory
    TOOLS.import_asset_tasks([task])
    objects = list(task.get_objects())
    emit("file_imported", source=str(filename), assets=[o.get_path_name() for o in objects])
    require(objects, "No assets imported from {}".format(filename))
    return objects


def make_fbx_options():
    options = unreal.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    options.import_as_skeletal = True
    options.import_mesh = True
    options.skeleton = None
    options.import_animations = True
    options.import_materials = False
    options.import_textures = False
    options.create_physics_asset = True
    data = options.skeletal_mesh_import_data
    data.import_uniform_scale = 1.0
    data.convert_scene = True
    data.convert_scene_unit = True
    data.set_editor_property("import_morph_targets", True)
    data.set_editor_property("update_skeleton_reference_pose", False)
    return options


def import_assets():
    fbx = Path(os.environ.get("CUTE_CAT_FBX", str(SOURCE / "cutecat_fbx.fbx")))
    require(fbx.is_file(), "Set CUTE_CAT_SOURCE to the extracted FBX directory")
    for suffix in VARIANTS.values():
        require((SOURCE / "cat.texture" / ("cat.texture.{}.001.png".format(suffix))).is_file(),
                "Missing texture: " + suffix)
    require(not LIB.does_directory_exist(ROOT), "Destination already exists; refusing to overwrite " + ROOT)

    imported = import_file(fbx, ROOT + "/Meshes", "SK_CuteCat", make_fbx_options(), unreal.FbxFactory())
    meshes = [o for o in imported if isinstance(o, unreal.SkeletalMesh)]
    require(meshes, "FBX did not produce a skeletal mesh")

    materials = {}
    for variant, suffix in VARIANTS.items():
        texture_objects = import_file(SOURCE / "cat.texture" / ("cat.texture.{}.001.png".format(suffix)),
                                      ROOT + "/Textures", "T_CuteCat_" + variant)
        texture = next(o for o in texture_objects if isinstance(o, unreal.Texture2D))
        texture.set_editor_property("srgb", True)
        for fur in (False, True):
            key = variant + ("_Fur" if fur else "")
            material = TOOLS.create_asset("M_CuteCat_" + key, ROOT + "/Materials",
                                          unreal.Material, unreal.MaterialFactoryNew())
            require(material, "Failed to create material " + key)
            sample = unreal.MaterialEditingLibrary.create_material_expression(
                material, unreal.MaterialExpressionTextureSample, -400, 0)
            sample.texture = texture
            unreal.MaterialEditingLibrary.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
            if fur:
                material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
                material.set_editor_property("two_sided", True)
                unreal.MaterialEditingLibrary.connect_material_property(sample, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
            roughness = unreal.MaterialEditingLibrary.create_material_expression(
                material, unreal.MaterialExpressionConstant, -400, 200)
            roughness.set_editor_property("r", 0.8)
            unreal.MaterialEditingLibrary.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
            unreal.MaterialEditingLibrary.recompile_material(material)
            materials[key] = material
            require(LIB.save_loaded_asset(material), "Failed saving material")
        require(LIB.save_loaded_asset(texture), "Failed saving texture")

    for mesh in meshes:
        slots = list(mesh.get_editor_property("materials"))
        emit("material_slots", mesh=mesh.get_path_name(), slots=[str(s.material_slot_name) for s in slots])
        require(len(slots) == 2, "Expected source skin and fur material slots")
        for slot in slots:
            name = str(slot.material_slot_name).lower().replace(".", "_")
            require(name in ("cat_skin", "cat_fur"), "Unrecognized material slot " + name)
            slot.material_interface = materials["Calico_Fur" if name == "cat_fur" else "Calico"]
        mesh.set_editor_property("materials", slots)
        require(LIB.save_loaded_asset(mesh), "Failed saving mesh")
    # AssetImportTask.save only saves its returned mesh, not every side asset.
    require(LIB.save_directory(ROOT, only_if_is_dirty=False, recursive=True), "Failed saving import dependencies")
    emit("import_completed", root=ROOT)


def verify():
    paths = LIB.list_assets(ROOT, recursive=True, include_folder=False)
    require(paths, "No saved CuteCat assets found")
    summary = []
    mesh_count = 0
    animation_count = 0
    for path in paths:
        asset = LIB.load_asset(path)
        require(asset, "Cannot load " + path)
        row = {"path": path, "class": asset.get_class().get_name()}
        if isinstance(asset, unreal.SkeletalMesh):
            mesh_count += 1
            skeleton = asset.get_editor_property("skeleton")
            require(skeleton and skeleton.get_path_name().startswith(ROOT + "/"), "Mesh must use its own skeleton")
            slots = list(asset.get_editor_property("materials"))
            require(slots and all(s.material_interface and s.material_interface.get_path_name().startswith(ROOT + "/Materials/") for s in slots),
                    "Missing CuteCat material binding")
            row.update(skeleton=skeleton.get_path_name(), bounds=str(asset.get_bounds()),
                       physics_asset=str(asset.get_editor_property("physics_asset")),
                       materials=[s.material_interface.get_path_name() for s in slots])
            require(asset.get_editor_property("physics_asset"), "Missing physics asset")
        if isinstance(asset, unreal.AnimSequence):
            animation_count += 1
            row["length_seconds"] = asset.get_editor_property("sequence_length")
            require(row["length_seconds"] > 0, "Empty animation " + path)
            anim_skeleton = asset.get_editor_property("skeleton")
            require(anim_skeleton and anim_skeleton.get_path_name().startswith(ROOT + "/"),
                    "Animation must use the imported skeleton")
        if isinstance(asset, unreal.Texture2D):
            row.update(width=asset.blueprint_get_size_x(), height=asset.blueprint_get_size_y())
        summary.append(row)
    require(mesh_count > 0, "Missing skeletal mesh")
    require(animation_count == 21, "Expected 21 animations; the source static pose has no valid keys")
    for variant in VARIANTS:
        require(LIB.does_asset_exist(ROOT + "/Materials/M_CuteCat_" + variant), "Missing material " + variant)
        fur = LIB.load_asset(ROOT + "/Materials/M_CuteCat_" + variant + "_Fur")
        require(fur and fur.get_editor_property("blend_mode") == unreal.BlendMode.BLEND_MASKED
                and fur.get_editor_property("two_sided"), "Missing masked, two-sided fur material " + variant)
        texture = LIB.load_asset(ROOT + "/Textures/T_CuteCat_" + variant)
        for suffix in ("", "_Fur"):
            material = LIB.load_asset(ROOT + "/Materials/M_CuteCat_" + variant + suffix)
            # Inspect the saved graph directly; NullRHI has no compiled shader resource.
            color_node = unreal.MaterialEditingLibrary.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
            require(color_node and color_node.get_editor_property("texture") == texture, "Material texture mismatch")
        alpha_node = unreal.MaterialEditingLibrary.get_material_property_input_node(fur, unreal.MaterialProperty.MP_OPACITY_MASK)
        require(alpha_node and alpha_node.get_editor_property("texture") == texture
                and unreal.MaterialEditingLibrary.get_material_property_input_node_output_name(fur, unreal.MaterialProperty.MP_OPACITY_MASK) == "A",
                "Fur opacity mask must use texture alpha")
        require(LIB.does_asset_exist(ROOT + "/Textures/T_CuteCat_" + variant), "Missing texture " + variant)
    emit("verification_pass", meshes=mesh_count, animations=animation_count, assets=summary)


if __name__ == "__main__":
    if os.environ.get("CUTE_CAT_VERIFY_ONLY") != "1":
        import_assets()
    verify()
