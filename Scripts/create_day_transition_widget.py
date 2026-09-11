"""在 Unreal Editor Python 控制台执行：创建或只读核验正式翻天过渡 WBP。"""

import unreal


# 正式翻天 WBP 的唯一内容路径；脚本只调用对应作者器，不扫描、保存或修改其他 UI 资产。
TARGET_WIDGET = "/Game/UI/Run/WBP_CatDayTransition"


def main():
    """调用编辑器作者器创建缺失资产，既有资产仅做 BindWidget 合同校验并保持人工布局不变。"""
    if not unreal.CatDayTransitionAuthoringLibrary.create_missing_day_transition_widget_blueprint():
        raise RuntimeError("正式翻天 WBP 创建或合同核验失败: {}".format(TARGET_WIDGET))
    unreal.log("Event=day_transition_widget_authoring_complete Asset={}".format(TARGET_WIDGET))


main()
