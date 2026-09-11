"""在 Unreal Editor 的 Python 控制台执行：迁移 Aegis 的物品悬停框并保留原始布局。"""

import hashlib
import os
import shutil

import unreal


# Aegis 项目的 Content 根目录，代表本次迁移读取的唯一旧资产来源；脚本只从这里复制 WBP 和它的正式依赖。
SOURCE_CONTENT = r"D:\UnreaProjects\AegisOdyssey\Content"
# 当前 Catfishing 工程的 Content 根目录，代表迁移资产落盘位置；所有目标路径都相对它生成。
TARGET_CONTENT = os.path.join(unreal.Paths.project_dir(), "Content")
# 旧 WBP 在 Aegis 中的包路径；它会先被临时复制到当前工程，再经父类重定向加载。
SOURCE_WIDGET = "/Game/Games/UI/InventoryMenu/Information/WBP_InventoryContext_SubWidget"
# 迁移完成后的正式物品提示 WBP 包路径；存在同名资产或文件时脚本拒绝覆盖。
TARGET_WIDGET = "/Game/UI/Inventory/WBP_CatItemTooltip"
# 源 WBP 和它在 Designer 中直接引用的最小资产闭包；脚本只复制这些包，避免把 Aegis UI 目录整体带入项目。
REQUIRED_PACKAGES = (
    "Games/UI/InventoryMenu/Information/WBP_InventoryContext_SubWidget",
    "imagegen/split/basic/basic_header_bar",
    "imagegen/split/basic/basic_panel_body",
    "CraftResourcesIcons/Textures/Tex_meat_08_b",
)
# UE 资产包可能存在的旁挂文件扩展名；复制时以源文件实际存在为准，确保 uasset 与 bulk 数据保持同源。
PACKAGE_SIDECARS = (".uasset", ".uexp", ".ubulk", ".uptnl", ".m.ubulk")


def _sha256(path):
    """读取文件字节并返回稳定摘要，供迁移前拒绝覆盖不同来源的同路径资产。"""
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _copy_required_package(package_path):
    """复制一个源包及实际存在的 sidecar；目标已有同字节文件时复用，内容不同则立即失败。"""
    source_base = os.path.join(SOURCE_CONTENT, package_path.replace("/", os.sep))
    target_base = os.path.join(TARGET_CONTENT, package_path.replace("/", os.sep))
    for extension in PACKAGE_SIDECARS:
        source_file = source_base + extension
        target_file = target_base + extension
        if not os.path.exists(source_file):
            continue
        if os.path.exists(target_file):
            if _sha256(source_file) != _sha256(target_file):
                raise RuntimeError("拒绝覆盖不同内容的资产文件: {}".format(target_file))
            continue
        os.makedirs(os.path.dirname(target_file), exist_ok=True)
        shutil.copy2(source_file, target_file)


def _assert_temporary_source_widget_is_absent():
    """确认临时导入位置完全空白；来源 WBP 已存在时拒绝运行，确保清理阶段只删除本轮脚本复制的文件。"""
    source_base = os.path.join(TARGET_CONTENT, "Games", "UI", "InventoryMenu", "Information", "WBP_InventoryContext_SubWidget")
    if unreal.EditorAssetLibrary.does_asset_exist(SOURCE_WIDGET):
        raise RuntimeError("临时源 WBP 已存在，脚本不会接管或删除用户资产: {}".format(SOURCE_WIDGET))
    existing_files = [source_base + extension for extension in PACKAGE_SIDECARS if os.path.exists(source_base + extension)]
    if existing_files:
        raise RuntimeError("临时源 WBP 文件已存在，脚本不会接管或删除: {}".format(", ".join(existing_files)))


def _delete_temporary_source_widget():
    """只在目标 WBP 已保存后删除本次复制的临时源 WBP；依赖美术资源保留为目标的正式引用。"""
    if unreal.EditorAssetLibrary.does_asset_exist(SOURCE_WIDGET):
        if not unreal.EditorAssetLibrary.delete_asset(SOURCE_WIDGET):
            raise RuntimeError("目标已生成，但未能清理本轮临时源 WBP: {}".format(SOURCE_WIDGET))
        return
    source_base = os.path.join(TARGET_CONTENT, "Games", "UI", "InventoryMenu", "Information", "WBP_InventoryContext_SubWidget")
    for extension in PACKAGE_SIDECARS:
        path = source_base + extension
        if os.path.exists(path):
            os.remove(path)


def migrate_item_tooltip():
    """按固定顺序迁移：先装临时重定向，再导入源包，复制 WBP，补合同，最后清理临时源包。"""
    if unreal.EditorAssetLibrary.does_asset_exist(TARGET_WIDGET):
        raise RuntimeError("目标 WBP 已存在，脚本不会覆盖: {}".format(TARGET_WIDGET))
    target_file = os.path.join(TARGET_CONTENT, "UI", "Inventory", "WBP_CatItemTooltip.uasset")
    if os.path.exists(target_file):
        raise RuntimeError("目标文件已存在但未登记为资产，脚本不会覆盖: {}".format(target_file))
    _assert_temporary_source_widget_is_absent()

    if not unreal.CatItemTooltipAuthoringLibrary.install_legacy_parent_redirect():
        raise RuntimeError("无法安装仅本进程有效的 Aegis 父类重定向")

    for package_path in REQUIRED_PACKAGES:
        _copy_required_package(package_path)

    asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
    asset_registry.scan_paths_synchronous(["/Game/Games", "/Game/imagegen", "/Game/CraftResourcesIcons"], True, False)
    source_asset = unreal.EditorAssetLibrary.load_asset(SOURCE_WIDGET)
    if source_asset is None:
        raise RuntimeError("旧 WBP 无法在临时重定向下加载: {}".format(SOURCE_WIDGET))

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    target_asset = asset_tools.duplicate_asset("WBP_CatItemTooltip", "/Game/UI/Inventory", source_asset)
    if target_asset is None:
        raise RuntimeError("无法创建目标 WBP: {}".format(TARGET_WIDGET))
    if not unreal.CatItemTooltipAuthoringLibrary.finalize_migrated_tooltip_widget(target_asset):
        raise RuntimeError("目标 WBP 未通过父类和 BindWidget 合同收尾；临时源包已保留供排查")

    _delete_temporary_source_widget()
    unreal.log("Event=item_tooltip_migration_complete Asset={}".format(TARGET_WIDGET))


migrate_item_tooltip()
