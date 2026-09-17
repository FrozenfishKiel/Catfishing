"""通过项目编辑器校验并重存已拆分的装备物品资产，保留原物品编号和配置。"""
import unreal

if not unreal.CatInventoryActionsAuthoringLibrary.migrate_equipment_definitions():
    raise RuntimeError("装备定义迁移失败，请检查 equipment_definition_migrated 日志与未保存资产。")
if not unreal.CatInventoryActionsAuthoringLibrary.migrate_formal_equipment_use_instance_types():
    raise RuntimeError("装备使用能力迁移失败，请检查 equipment_use_instance_migration 日志。")
unreal.log("Equipment definition migration completed.")
