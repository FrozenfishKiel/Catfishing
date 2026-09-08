"""创建并校验 DefaultGame 使用的正式 BodyAction Montage 资产。

脚本在 Unreal Editor Python 内运行，把 Animalia 的猫 AnimSequence 转成
BodyAction 包下的 UAnimMontage。清单只保留当前六个身体动作；库存、献祭、偷鱼
和湿身反馈等事务属于各自系统表现，不再生成 BodyAction Montage。
"""

import unreal


PACKAGE_ROOT = "/Game/Catfishing/Animation/BodyAction"

# 当前仍属于 BodyAction 的 Montage 清单；配置脚本逐项加载源动画并创建或复核对应 Montage。
BODY_ACTION_MONTAGES = [
    ("AM_BodyAction_CampRest", "/Game/Animalia/Cat/Animations/InPlace/Lying_00-IP.Lying_00-IP"),
    ("AM_BodyAction_CampfirePlayback", "/Game/Animalia/Cat/Animations/InPlace/Sitting_01-IP.Sitting_01-IP"),
    ("AM_BodyAction_RescueCharacterToCamp", "/Game/Animalia/Cat/Animations/InPlace/Trans_Sitting_To_Stand-IP.Trans_Sitting_To_Stand-IP"),
    ("AM_BodyAction_RequestManualHelp", "/Game/Animalia/Cat/Animations/InPlace/Agressive_01-IP.Agressive_01-IP"),
    ("AM_BodyAction_RequestMischief", "/Game/Animalia/Cat/Animations/InPlace/Attack_Right-IP.Attack_Right-IP"),
    ("AM_BodyAction_PlaceProtectionSign", "/Game/Animalia/Cat/Animations/InPlace/Action_Scratching-IP.Action_Scratching-IP"),
]


def _load_asset(path, expected_type):
    """加载一个已存在的源资产；路径漂移或类型不符时立即失败，避免生成错误 Montage。"""
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise RuntimeError(f"missing source asset: {path}")
    if not isinstance(asset, expected_type):
        raise RuntimeError(f"asset has wrong type: {path} expected={expected_type.__name__} actual={type(asset).__name__}")
    return asset


def _create_or_load_montage(asset_name, source_animation):
    """为一个动作创建或复核 Montage；已有资产保持原样，只保存以确认它仍可被加载。"""
    asset_path = f"{PACKAGE_ROOT}/{asset_name}.{asset_name}"
    montage = unreal.EditorAssetLibrary.load_asset(asset_path)
    created = False
    if montage is None:
        factory = unreal.AnimMontageFactory()
        factory.set_editor_property("source_animation", source_animation)
        montage = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name,
            PACKAGE_ROOT,
            unreal.AnimMontage,
            factory,
        )
        created = True
    if montage is None:
        raise RuntimeError(f"failed to create montage: {asset_path}")
    if not isinstance(montage, unreal.AnimMontage):
        raise RuntimeError(f"asset has wrong montage type: {asset_path} actual={type(montage).__name__}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(montage, only_if_is_dirty=False):
        raise RuntimeError(f"failed to save montage: {asset_path}")
    unreal.log(f"BODY_ACTION_MONTAGE_ASSET_READY Asset={asset_path} Source={source_animation.get_path_name()} Created={created}")
    return montage


def main():
    """按当前 BodyAction 清单逐项复核 Montage，并输出稳定日志标记供自动化读取。"""
    created_or_verified = 0
    for asset_name, source_path in BODY_ACTION_MONTAGES:
        source_animation = _load_asset(source_path, unreal.AnimSequence)
        _create_or_load_montage(asset_name, source_animation)
        created_or_verified += 1
    unreal.log(f"CREATE_BODY_ACTION_MONTAGE_ASSETS_PASS AssetCount={created_or_verified} Directory={PACKAGE_ROOT}")


main()
