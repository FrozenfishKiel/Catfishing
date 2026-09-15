"""迁移已有正式可食用鱼的即时 GE 配置；通过项目 Editor 的 Python 命令入口执行。"""
import unreal

if not unreal.CatFishUseAuthoringLibrary.migrate_existing_fish_use_effects():
    raise RuntimeError("已有鱼的 Use 效果迁移失败；请检查定义冲突及编辑器日志。")
unreal.log("Existing fish Use effect migration completed.")
