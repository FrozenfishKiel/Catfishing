"""用 Unreal Editor 创建缺失的祭坛确认 WBP；既有资产只校验绑定，不覆盖设计者布局。"""
import unreal

if not unreal.CatAltarConfirmationAuthoringLibrary.create_missing_altar_confirmation_widget_blueprint():
    raise RuntimeError("祭坛确认 WBP 创建或绑定校验失败")
