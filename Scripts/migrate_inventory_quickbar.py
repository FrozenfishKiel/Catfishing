"""在 Unreal Editor 的 Python 控制台执行：创建独立快捷栏格、恢复共享背包格并迁移统一背包输入资产。"""

import unreal


def migrate_inventory_quickbar() -> None:
    """先创建独立快捷栏格并定向恢复共享背包格，再迁移唯一 IMC 与 Native Input Config；任一步失败都会抛错停止，已经按顺序保存成功的前序结果会保留。"""
    authoring = unreal.CatInventoryQuickbarAuthoringLibrary
    if not authoring.create_or_validate_inventory_quickbar_widgets():
        raise RuntimeError("无法创建或校验 WBP_CatInventoryQuickbar、独立快捷栏格或共享背包格恢复合同")
    if not authoring.migrate_backpack_quickbar_input_assets():
        raise RuntimeError("无法迁移正式 IMC_InputContext 或 DA_CatAbilityInputConfig")
    unreal.log("Event=inventory_quickbar_migration_complete Quickbar=/Game/UI/Inventory/WBP_CatInventoryQuickbar")


migrate_inventory_quickbar()
