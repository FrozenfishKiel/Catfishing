"""在 Unreal Editor 的 Python 环境中迁移正式库存定义动作清单。"""

import unreal


def migrate_inventory_actions() -> None:
    """调用一次性 C++ 作者器；失败时中止脚本并保留可按 Event 前缀检索的资产日志。"""
    if not unreal.CatInventoryActionsAuthoringLibrary.migrate_formal_inventory_definition_actions():
        raise RuntimeError("Inventory action migration failed; inspect Event=inventory_actions_migration in the Editor log.")
    unreal.log("Event=inventory_actions_migration_script_completed")


migrate_inventory_actions()
