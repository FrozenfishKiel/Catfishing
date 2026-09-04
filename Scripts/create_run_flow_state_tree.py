"""通过 CatfishingEditor 的长期资产生成入口创建或更新 ST_RunFlow。

可在 Unreal Editor Python Console 中执行：
    py "D:/UnreaProjects/Catfishing-verify-01/Scripts/create_run_flow_state_tree.py"
"""

import unreal


def main():
    """调用编辑器专用 C++ 入口保存 RunFlow 资产；失败时直接抛错，让命令行日志暴露资产未更新。"""
    if not unreal.CatRunStateTreeAuthoringLibrary.create_or_update_default_run_flow_state_tree():
        raise RuntimeError("Could not create /Game/Data/StateTrees/ST_RunFlow")
    unreal.log("Catfishing run-flow StateTree created and compiled.")


if __name__ == "__main__":
    main()
