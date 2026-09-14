"""UE Editor Python 入口：迁移正式加入页与 Root，应用前端五页样式并验证合同。

UnrealEditor-Cmd Catfishing.uproject -run=pythonscript -script=<本文件> -unattended
迁移加入页、好友加入行、Room 文案和 Root 装配，再依次应用首页、存档及其余前端页面样式。
"""
from pathlib import Path
import runpy

import unreal

if not unreal.CatFrontendWidgetAuthoringLibrary.create_missing_frontend_widget_blueprints(True):
    raise RuntimeError("正式前端 WBP 生成或控件合同验证失败")
# Root 会被作者器重建；统一在生成后落实首页样式和设计尺寸，避免生成流程恢复旧外观。
runpy.run_path(str(Path(__file__).with_name("style_frontend_menu.py")), run_name="__main__")
runpy.run_path(str(Path(__file__).with_name("style_frontend_save.py")), run_name="__main__")
runpy.run_path(str(Path(__file__).with_name("style_frontend_pages.py")), run_name="__main__")
runpy.run_path(str(Path(__file__).with_name("style_frontend_room.py")), run_name="__main__")
runpy.run_path(str(Path(__file__).with_name("style_lake_party.py")), run_name="__main__")
if not unreal.CatFrontendWidgetAuthoringLibrary.validate_frontend_widget_blueprint_fonts():
    raise RuntimeError("正式前端字体验证失败")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
options = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True, include_hard_package_references=True,
    include_searchable_names=False, include_soft_management_references=False,
    include_hard_management_references=False)
for package in ("/Game/UI/Frontend/WBP_CatFrontendRoot", "/Game/UI/Frontend/WBP_CatFrontendRoom",
                "/Game/UI/Frontend/WBP_CatFrontendJoin", "/Game/UI/Frontend/WBP_CatJoinFriendRow"):
    unreal.log("Event=frontend_join_asset_references Package={} Referencers={}".format(
        package, list(registry.get_referencers(package, options))))
unreal.log("Event=frontend_join_assets_ready")
