"""在 Unreal Editor 创建缺失的共享鱼缸蓝图；已有资产只读返回，不保存地图。

鱼缸沿用正式库存/交互/信息组件，外观暂用项目既有渔桶网格。设计师可在该蓝图更换正式美术。
"""
import unreal

# 首批真实接入实例的稳定类路径；地图显式关联它，不在游戏运行中生成备用鱼缸。
ASSET_PATH = '/Game/Blueprint/Actors/BP_CatSharedFishTank'
# 项目现有渔桶外观；只写新蓝图组件模板，不修改源网格、材质或原有鱼护。
MESH_PATH = '/Game/Boatyard/VOL5_Dockyard/Meshes/LP/SM_Fishing_Buckets_01a'


def create_missing_blueprint():
    """先拒绝错误类型占用，再创建鱼缸父类和一个网格模板；编译成功才保存唯一新资产，失败不覆盖旧文件。"""
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
        cls = unreal.EditorAssetLibrary.load_blueprint_class(ASSET_PATH)
        if not cls or not isinstance(unreal.get_default_object(cls), unreal.CatFishTankActor):
            raise RuntimeError('Existing shared tank blueprint has an incompatible parent')
        return cls
    mesh_asset = unreal.load_asset(MESH_PATH)
    if not mesh_asset:
        raise RuntimeError('Shared tank presentation mesh missing')
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.CatFishTankActor)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'BP_CatSharedFishTank', '/Game/Blueprint/Actors', unreal.Blueprint, factory)
    if not blueprint:
        raise RuntimeError('Could not create shared tank blueprint')
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    library = unreal.SubobjectDataBlueprintFunctionLibrary
    handles = subsystem.k2_gather_subobject_data_for_blueprint(blueprint)
    root = next(handle for handle in handles if library.get_variable_name(library.get_data(handle)) == 'TankRoot')
    params = unreal.AddNewSubobjectParams(parent_handle=root, new_class=unreal.StaticMeshComponent, blueprint_context=blueprint)
    handle, error = subsystem.add_new_subobject(params)
    if not library.is_handle_valid(handle):
        raise RuntimeError('Could not add tank presentation: {}'.format(error))
    subsystem.rename_subobject(handle, 'TankPresentation')
    mesh = library.get_object_for_blueprint(library.get_data(handle), blueprint)
    mesh.set_static_mesh(mesh_asset)
    mesh.set_collision_profile_name('BlockAll')
    bounds = mesh_asset.get_bounding_box()
    height = bounds.max.z - bounds.min.z
    if height <= 0:
        raise RuntimeError('Tank presentation mesh has invalid bounds')
    scale = 80.0 / height
    mesh.set_relative_scale3d(unreal.Vector(scale, scale, scale))
    mesh.set_editor_property('relative_location', unreal.Vector(0, 0, -bounds.min.z * scale))
    if not unreal.BlueprintEditorLibrary.compile_blueprint(blueprint):
        raise RuntimeError('Shared tank blueprint failed to compile')
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False):
        raise RuntimeError('Could not save shared tank blueprint')
    unreal.log('Event=SharedFishTankBlueprintCreated Asset={} Mesh={} HeightCm=80'.format(ASSET_PATH, MESH_PATH))
    return unreal.EditorAssetLibrary.load_blueprint_class(ASSET_PATH)


if __name__ == '__main__':
    create_missing_blueprint()
