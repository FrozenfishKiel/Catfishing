"""将正式浮漂射程从占位的 3/5/7 米迁移至 10/15/20 米；不重建或覆盖其他装备字段。"""
import unreal

for name, expected_old, new_range in [
    ("Feather", 300.0, 1000.0),
    ("YarnBall", 500.0, 1500.0),
    ("Bell", 700.0, 2000.0),
]:
    path = "/Game/Catfishing/Data/Equipment/Equip_Float_" + name
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError("Missing float: " + path)
    current = asset.get_editor_property("maximum_cast_distance_centimeters")
    if current not in (expected_old, new_range):
        raise RuntimeError("Unexpected range; preserve existing tuning: {} {}".format(path, current))
    if current != new_range:
        asset.set_editor_property("maximum_cast_distance_centimeters", new_range)
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
            raise RuntimeError("Failed saving " + path)
    unreal.log("CAST_RANGE_VERIFIED {} {}cm".format(path, asset.get_editor_property("maximum_cast_distance_centimeters")))
