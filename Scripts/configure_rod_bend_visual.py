"""Reuse the existing rod, preserving source art and canonical gameplay anchors, and calibrate its visual tip.

Run after building the Editor target, with the interactive editor closed:
UnrealEditor-Cmd Catfishing.uproject -unattended -ExecutePythonScript=<this file>
Only the derived mesh and formal Rod Blueprint are saved. Re-running is safe.
"""
import unreal

BLUEPRINT = "/Game/Blueprint/Actors/BP_CatFishingRodActor"
ORIGINAL = "/Game/Boatyard/VOL5_Dockyard/Meshes/LP/SM_Fishing_Rods_01a"
DERIVED = "/Game/Catfishing/Fishing/Presentation/SM_Rod_BendSource"


def component_templates(blueprint):
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    library = unreal.SubobjectDataBlueprintFunctionLibrary
    return [library.get_object(library.get_data(handle))
            for handle in subsystem.k2_gather_subobject_data_for_blueprint(blueprint)]


def transform_signature(component):
    location = component.get_editor_property("relative_location")
    rotation = component.get_editor_property("relative_rotation")
    scale = component.get_editor_property("relative_scale3d")
    return ((location.x, location.y, location.z), (rotation.pitch, rotation.yaw, rotation.roll),
            (scale.x, scale.y, scale.z))


def calibrated_tip(derived):
    vertices = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(derived, 0, 0)[0]
    if not vertices:
        raise RuntimeError("Cannot calibrate an empty rod mesh")
    highest = max(vertex.z for vertex in vertices)
    top = {(vertex.x, vertex.y, vertex.z) for vertex in vertices if abs(vertex.z - highest) < 0.0001}
    # This source is authored along +Z. Use the actual terminal cross-section, not the reel's wider bounds centre.
    tip = unreal.Vector(*(sum(vertex[axis] for vertex in top) / len(top) for axis in range(3)))
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = subsystem.spawn_actor_from_class(unreal.EditorAssetLibrary.load_blueprint_class(BLUEPRINT), unreal.Vector(), transient=True)
    if not actor:
        raise RuntimeError("Cannot inspect formal rod attachment transforms")
    try:
        source = actor.get_components_by_class(unreal.StaticMeshComponent)[0]
        marker = next(component for component in actor.get_components_by_class(unreal.SceneComponent)
                      if "RodTipMarker" in [str(tag) for tag in component.get_editor_property("component_tags")])
        world_tip = unreal.MathLibrary.transform_location(source.get_world_transform(), tip)
        parent = marker.get_attach_parent()
        return unreal.MathLibrary.inverse_transform_location(parent.get_world_transform(), world_tip) if parent else world_tip
    finally:
        subsystem.destroy_actor(actor)


def main():
    blueprint = unreal.load_asset(BLUEPRINT)
    original = unreal.load_asset(ORIGINAL)
    if not blueprint or not original:
        raise RuntimeError("Missing formal rod or original source mesh")
    templates = component_templates(blueprint)
    meshes = [obj for obj in templates if isinstance(obj, unreal.StaticMeshComponent)
              and obj.get_editor_property("static_mesh")]
    markers = [obj for obj in templates if isinstance(obj, unreal.SceneComponent)
               and "RodTipMarker" in [str(tag) for tag in obj.get_editor_property("component_tags")]]
    if len(meshes) != 1 or len(markers) != 1:
        raise RuntimeError(f"Expected one source mesh and one visual tip: meshes={len(meshes)} tips={len(markers)}")
    source = meshes[0]
    current = source.get_editor_property("static_mesh").get_path_name().split(".")[0]
    if current not in (ORIGINAL, DERIVED):
        raise RuntimeError(f"Unexpected rod mesh; refusing to overwrite: {current}")
    before_mesh = transform_signature(source)
    materials = [source.get_material(i) for i in range(source.get_num_materials())]
    derived = unreal.load_asset(DERIVED) if unreal.EditorAssetLibrary.does_asset_exist(DERIVED) else None
    if not derived:
        derived = unreal.EditorAssetLibrary.duplicate_asset(ORIGINAL, DERIVED)
    if not derived:
        raise RuntimeError("Could not duplicate existing rod mesh")
    derived.set_editor_property("allow_cpu_access", True)
    nanite = derived.get_editor_property("nanite_settings")
    nanite.set_editor_property("enabled", False)
    derived.set_editor_property("nanite_settings", nanite)
    if not unreal.EditorAssetLibrary.save_loaded_asset(derived, only_if_is_dirty=False):
        raise RuntimeError("Could not save CPU-readable rod mesh")
    source.set_editor_property("static_mesh", derived)
    tags = list(source.get_editor_property("component_tags"))
    if "RodBendSource" not in [str(tag) for tag in tags]:
        tags.append(unreal.Name("RodBendSource"))
    source.set_editor_property("component_tags", tags)
    for index, material in enumerate(materials):
        source.set_material(index, material)
    if transform_signature(source) != before_mesh:
        raise RuntimeError("Migration changed the source mesh transform")
    markers[0].set_editor_property("relative_location", calibrated_tip(derived))
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
        raise RuntimeError("Could not save formal rod Blueprint")
    unreal.log(f"ROD_BEND_MIGRATION_PASS Blueprint={BLUEPRINT} Source={ORIGINAL} Derived={DERIVED} "
               "CPUAccess=true OriginalPreserved=true MaterialsPreserved=true CanonicalAnchorsUntouched=true VisualTipCalibrated=true")


main()
