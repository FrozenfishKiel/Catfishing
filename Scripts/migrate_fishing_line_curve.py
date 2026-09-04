"""Compile and resave only the formal Hook after replacing its inherited Cable with the curve mesh.

Run with UnrealEditor-Cmd -ExecutePythonScript=<this file>. Re-running is safe.
The script checks visual anchors and meshes, and refuses to save an actor that still instantiates Cable.
"""
import unreal

PATH = "/Game/Blueprint/Actors/BP_CatFishingHookActor"


def snapshot(actor):
    result = {}
    for component in actor.get_components_by_class(unreal.SceneComponent):
        if component.get_name() in ("FishingLine", "FishingLineCurve", "FishingLineStartAnchor"):
            continue
        mesh = None
        if isinstance(component, unreal.StaticMeshComponent):
            mesh = component.get_editor_property("static_mesh")
        elif isinstance(component, unreal.SkeletalMeshComponent):
            mesh = component.get_editor_property("skeletal_mesh_asset")
        location = component.get_editor_property("relative_location")
        rotation = component.get_editor_property("relative_rotation")
        scale = component.get_editor_property("relative_scale3d")
        result[component.get_name()] = (
            (location.x, location.y, location.z),
            (rotation.pitch, rotation.yaw, rotation.roll),
            (scale.x, scale.y, scale.z),
            mesh.get_path_name() if mesh else None,
        )
    return result


def inspect_instance():
    cls = unreal.EditorAssetLibrary.load_blueprint_class(PATH)
    if not cls:
        raise RuntimeError("Formal Hook class could not be loaded")
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = subsystem.spawn_actor_from_class(cls, unreal.Vector(0, 0, 0), transient=True)
    if not actor:
        raise RuntimeError("Formal Hook could not be spawned")
    try:
        components = actor.get_components_by_class(unreal.ActorComponent)
        obsolete = [c.get_name() for c in components if c.get_class().get_name() in ("CableComponent", "CatFishingLineComponent")]
        curves = [c for c in components if c.get_class().get_name() == "CatFishingLineCurveComponent"]
        if obsolete or len(curves) != 1:
            raise RuntimeError("Invalid Hook line components: old={} curves={}".format(obsolete, len(curves)))
        if curves[0].get_collision_enabled() != unreal.CollisionEnabled.NO_COLLISION:
            raise RuntimeError("Visual curve must not participate in collision")
        return snapshot(actor)
    finally:
        subsystem.destroy_actor(actor)


bp = unreal.load_asset(PATH)
if not bp:
    raise RuntimeError("Missing formal Hook Blueprint")
before = inspect_instance()
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
after = inspect_instance()
if before != after:
    raise RuntimeError("Hook visual anchors or meshes changed during compilation")
if not unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False):
    raise RuntimeError("Saving formal Hook failed")
unreal.log("FISHING_LINE_CURVE_MIGRATION_PASS Asset={} CurveComponents=1 CableComponents=0 VisualComponents={}".format(PATH, len(after)))
