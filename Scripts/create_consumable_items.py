"""导入指定目录的七张原图并调用正式道具资产接线；不修改已有贴图或策划资产。

在启动 Unreal Editor 前通过 CAT_ITEM_ICON_SOURCE 指定素材目录。
"""
import os
from pathlib import Path
import unreal


def create_consumables():
    """先核对全部源图，再只导入缺失图标；最后让 C++ 按项目类型登记资产。"""
    source = os.environ.get("CAT_ITEM_ICON_SOURCE")
    if not source:
        raise RuntimeError("请设置 CAT_ITEM_ICON_SOURCE 为七张道具图片所在目录")
    names = [("小鱼干", "DriedFish"), ("爪爪套", "PawGloves"), ("幸运四叶草", "LuckyClover"),
             ("湿毛器", "WaterSprayer"), ("恶作剧假鱼", "FakeFish"), ("响响筒", "Horn"), ("渔网", "CastNet")]
    for name, _ in names:
        if not (Path(source) / (name + ".jpeg")).is_file():
            raise RuntimeError("缺少素材：" + name + ".jpeg")
    destination = "/Game/Catfishing/Items/Icons"
    for name, key in names:
        target = destination + "/T_" + key
        if unreal.EditorAssetLibrary.does_asset_exist(target):
            continue
        task = unreal.AssetImportTask()
        task.filename = str(Path(source) / (name + ".jpeg"))
        task.destination_path = destination
        task.destination_name = "T_" + key
        task.automated = True
        task.save = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = unreal.load_asset(target)
        if not texture:
            raise RuntimeError("图标导入失败：" + target)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        unreal.EditorAssetLibrary.save_loaded_asset(texture)
    if not unreal.CatConsumableAuthoringLibrary.create_missing_consumables():
        raise RuntimeError("道具资产接线失败，请查看编辑器日志中的资产校验错误")
    unreal.log("ConsumableAuthoring: seven items and icons ready")


create_consumables()
