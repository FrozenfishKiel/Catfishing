"""在 Unreal Editor 的 Python 环境中迁移正式商店摊位蓝图。"""

import unreal


def migrate_formal_shop_kiosk() -> None:
    """调用 C++ 作者器，失败时中止脚本并保留可搜索的 Editor 日志事件。"""
    if not unreal.CatShopKioskAuthoringLibrary.migrate_formal_shop_kiosk_blueprint():
        raise RuntimeError("BP_CatShopKiosk migration failed; inspect Event=shop_kiosk_migration in the Editor log.")
    unreal.log("Event=shop_kiosk_migration_script_completed Asset=/Game/UI/Shop/BP_CatShopKiosk")


migrate_formal_shop_kiosk()
