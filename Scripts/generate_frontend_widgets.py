"""UE Editor Python 入口：补齐正式前端页面、动态行，升级 Root 装配并验证合同。

UnrealEditor-Cmd Catfishing.uproject -run=pythonscript -script=<本文件> -unattended
只迁移加入页、好友加入行、Room 文案和 Root 装配，保留其他业务页。
"""
import unreal

if not unreal.CatFrontendWidgetAuthoringLibrary.create_missing_frontend_widget_blueprints(True):
    raise RuntimeError("正式前端 WBP 生成或控件合同验证失败")
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
