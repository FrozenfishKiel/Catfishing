"""移除旧生成器的固定落地抬升；保留每条鱼的自定义对齐与大小配置。"""
import unreal

for path in unreal.EditorAssetLibrary.list_assets('/Game/Catfishing/Data/Fish/Presentation', recursive=False):
    asset = unreal.load_asset(path)
    if not isinstance(asset, unreal.CatFishPresentationDefinition):
        continue
    transform = asset.get_editor_property('landed_mesh_relative_transform')
    if (abs(transform.translation.x) < 0.001 and abs(transform.translation.y) < 0.001
            and abs(transform.translation.z - 15) < 0.001):
        asset.modify()
        transform.translation = unreal.Vector(0, 0, 0)
        asset.set_editor_property('landed_mesh_relative_transform', transform)
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
            raise RuntimeError('Failed saving ' + path)
        unreal.log('FISH_GROUND_OFFSET_MIGRATED ' + path)
    unreal.log('FISH_GROUND_OFFSET_VERIFIED {} {}'.format(
        path, asset.get_editor_property('landed_mesh_relative_transform').translation))
