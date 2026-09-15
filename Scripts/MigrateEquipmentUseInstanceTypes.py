"""在 Unreal Editor Python 环境中，把正式装备的现有片段行为迁移为明确的库存行为实例类。"""

import unreal


def migrate_equipment_use_instance_types() -> None:
    """调用一次性作者器；失败时保留资产并通过 Editor 日志定位失败包。"""
    if not unreal.CatInventoryActionsAuthoringLibrary.migrate_formal_equipment_use_instance_types():
        raise RuntimeError("Equipment use instance migration failed; inspect Event=equipment_use_instance_migration.")
    if not unreal.CatInventoryActionsAuthoringLibrary.migrate_equipment_ability_set_grants():
        raise RuntimeError("Equipment ability-set migration failed; inspect the Editor log.")
    unreal.log("Event=equipment_use_instance_migration_script_completed")


migrate_equipment_use_instance_types()
