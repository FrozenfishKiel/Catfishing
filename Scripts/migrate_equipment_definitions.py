"""通过项目编辑器校验并重存已拆分的装备物品资产，保留原物品编号和配置。"""
import unreal
from pathlib import Path

if not unreal.CatInventoryActionsAuthoringLibrary.migrate_equipment_definitions():
    raise RuntimeError("装备定义迁移失败，请检查 equipment_definition_migrated 日志与未保存资产。")
if not unreal.CatInventoryActionsAuthoringLibrary.migrate_formal_equipment_use_instance_types():
    raise RuntimeError("装备使用能力迁移失败，请检查 equipment_use_instance_migration 日志。")
if not unreal.CatInventoryActionsAuthoringLibrary.migrate_equipment_ability_set_grants():
    raise RuntimeError("角色常驻与装备能力集迁移失败。")

# 所有已登记物品重存后才检查旧窝料能力集；仍有资产引用时保留并报错，避免二进制消费者被静默破坏。
legacy_set = "/Game/Data/Abilities/DA_CatAbilitySet_ChumUse"
registry = unreal.AssetRegistryHelpers.get_asset_registry()
dependencies = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True, include_hard_package_references=True,
    include_searchable_names=True, include_soft_management_references=True,
    include_hard_management_references=True)
if unreal.EditorAssetLibrary.does_asset_exist(legacy_set):
    references = registry.get_referencers(legacy_set, dependencies)
    if references:
        raise RuntimeError("旧窝料能力集仍有消费者：{}".format(list(references)))
    if not unreal.EditorAssetLibrary.delete_asset(legacy_set):
        raise RuntimeError("旧窝料能力集删除失败。")
    # 某些无界面编辑器只注销包而未移除磁盘文件；引用检查已通过，仅清除此确定包，不扫描或删除其他资产。
    legacy_file = Path(__file__).resolve().parents[1] / "Content/Data/Abilities/DA_CatAbilitySet_ChumUse.uasset"
    if legacy_file.exists():
        legacy_file.unlink()
unreal.log("Equipment definition migration completed.")
