"""在已加载新 CatfishingEditor 模块的 Unreal Editor 中创建正式 WorldInfo WBP。

Python 控制台调用：
    import runpy
    runpy.run_path(r"D:/UnreaProjects/Catfishing-verify-01/Scripts/create_world_info_widgets.py", run_name="__main__")

仅创建缺失的行和面板资产；已有资产只读核验，不覆盖人工布局，不保存地图或其他脏资产。
依赖 /Game/UI/Shop/F_CatShopChinese。摘要信息区容纳两行，详情容纳八行；标题与状态另占两行。
超出容量的内容不会自动扩高，提供者须控制行数；长单行文本省略，设计师可在 WBP 中调整排版。
本脚本不会编译 C++、启动或退出编辑器；新 UCLASS 必须先由外部完成 Editor 构建和模块加载。
"""

import unreal


def main():
    """先检查编辑器反射入口，再调用一次创建器；失败抛出异常供操作者定位日志，成功仅报告两份资产合同成立。"""
    library = getattr(unreal, "CatWorldInfoAuthoringLibrary", None)
    if library is None:
        raise RuntimeError("未加载 CatWorldInfoAuthoringLibrary；请先构建并加载包含该类的 CatfishingEditor 模块。")
    if not library.create_missing_world_info_widget_blueprints():
        raise RuntimeError(
            "WorldInfo WBP 创建或核验失败；请查看 LogCatWorldInfoAuthoring 的 Event=world_info_* 日志。"
            "既有资产不会自动修补；若保存中途失败，已保存资产会保留，未完成内存包需人工检查。"
        )
    unreal.log("Event=world_info_authoring_complete Directory=/Game/UI/WorldInfo Mode=CreateMissingOrValidateExisting")


main()
