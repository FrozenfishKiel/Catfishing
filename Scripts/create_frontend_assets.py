"""在 Unreal Editor 中创建缺失的正式 Frontend WBP，并重建 Root / 两类 Loading 以落实全局加载遮罩合同。

执行方式：
    py "D:/UnreaProjects/Catfishing-verify-01/Scripts/create_frontend_assets.py"
"""

import unreal


def main() -> None:
    """调用 WBP 创建、Root/进图 Loading/回主菜单 Loading 重建、字体修复与设置音频入口；任一正式资产创建、编译或保存失败都会向 Editor 日志抛出明确错误。"""
    if not unreal.CatFrontendWidgetAuthoringLibrary.create_missing_frontend_widget_blueprints():
        raise RuntimeError("Could not create missing /Game/UI/Frontend widget blueprints")
    if not unreal.CatFrontendWidgetAuthoringLibrary.validate_frontend_widget_blueprint_fonts():
        raise RuntimeError("Could not validate /Game/UI/Frontend widget blueprint fonts")
    if not unreal.CatFrontendWidgetAuthoringLibrary.create_missing_frontend_audio_settings_assets():
        raise RuntimeError("Could not create missing /Game/Audio/Settings frontend audio assets")
    unreal.log("Catfishing Frontend WBP, Chinese fonts, and settings audio assets were prepared with split global loading overlay contracts.")


if __name__ == "__main__":
    main()
