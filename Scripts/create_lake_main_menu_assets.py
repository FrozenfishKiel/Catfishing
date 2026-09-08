"""在 Unreal Editor 中创建或重建局内 ESC 菜单 WBP，并核验它的 View 绑定合同。

执行方式：
    UnrealEditor-Cmd.exe Catfishing.uproject -ExecutePythonScript=Scripts/create_lake_main_menu_assets.py
"""

import unreal


def main() -> None:
    """调用项目 Editor-only WBP 作者入口；菜单父类、返回/设置/保存/退出与设置页合同任一失败都会向 Editor 日志抛出明确错误。"""
    if not unreal.CatFrontendWidgetAuthoringLibrary.create_missing_lake_main_menu_widget_blueprint():
        raise RuntimeError("Could not create or validate /Game/UI/Save/WBP_CatLakeMainMenu")
    unreal.log("Catfishing Lake main menu WBP was rebuilt and validated.")


if __name__ == "__main__":
    main()
