"""在 Unreal Editor 的 Python 控制台执行：创建统一库存右键菜单并迁移现有库存页。"""

import unreal


def migrate_inventory_context_menu():
    """先创建或校验唯一菜单，再迁移三个正式库存 WBP；任一步失败立即抛错，保留资产供编辑器排查。"""
    authoring = unreal.CatInventoryContextMenuAuthoringLibrary
    if not authoring.create_or_validate_inventory_context_menu_widget():
        raise RuntimeError("无法创建或校验 WBP_CatInventoryContextMenu")
    if not authoring.migrate_inventory_action_widgets():
        raise RuntimeError("至少一个库存 WBP 未完成旧固定操作控件迁移")
    unreal.log("Event=inventory_context_menu_migration_complete Asset=/Game/UI/Inventory/WBP_CatInventoryContextMenu")


migrate_inventory_context_menu()
