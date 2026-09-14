"""T32/T34 内容迁移；默认只审计，不保存资产。

在编辑器 Python 控制台调用 migrate(level_package, rack_cm, store_cm,
driedfish_package, stock_policies, apply=True)。位置必须由关卡负责人提供，单位厘米。
stock_policies 按新增 DefinitionId 提供原目录字段（库存／每日补货策略），不猜进货量。
运行前须通过 Catfishing.Unit.Save.MultiStorageFishTierWalletWorldColdDiskRoundTrip。
小鱼干定义及其既定使用行为由道具负责人提供，本脚本不生成无效果的占位道具。
"""

import csv
import json
import re
from pathlib import Path

import unreal


ROOT = Path(__file__).resolve().parents[1]
CATALOG = "/Game/Catfishing/Data/Shop/DT_ShopCatalog_Default"
ITEM_ID = "buff_driedfish"
REQUIRED_ROWS = {"FishGuard", "StarterScoopNet", "FishTankCapacityT2", "FishTankCapacityT3", ITEM_ID}


def _design_prices():
    """只映射已确认的定义；旧声响饵、闪光饵、巨物饵、圣光窝不能猜对应物。"""
    items = ROOT / "Knowledge/Design/GDD 系统分册/道具"
    prices = {}
    sources = (
        ("鱼饵/Sheet1.csv", 2, {"虫虫饵": "BugBait", "肉块饵": "MeatBait", "果实饵": "FruitBait",
                              "花蜜饵": "NectarBait", "月光饵": "MoonlightBait"}),
        ("鱼漂/Sheet1.csv", 2, {"羽毛漂": "FeatherFloat", "毛线球漂": "YarnBallFloat", "铃铛漂": "BellFloat"}),
        ("窝料/Sheet1.csv", 5, {"虫虫窝": "BugChum", "花果香窝": "FruitFragranceChum", "发酵谷物窝": "FermentedGrainChum"}),
    )
    for relative, column, aliases in sources:
        with (items / relative).open(encoding="utf-8-sig", newline="") as handle:
            for row in csv.reader(handle):
                if row and row[0] in aliases:
                    prices[aliases[row[0]]] = int(row[column])
        if not set(aliases.values()).issubset(prices):
            raise RuntimeError(f"Design price mapping changed: {relative}")
    with (items / "鱼竿/Sheet1.csv").open(encoding="utf-8-sig", newline="") as handle:
        rods = {row[0]: row[5] for row in csv.reader(handle) if len(row) > 5}
    if "免费" not in rods.get("树枝竿", ""):
        raise RuntimeError("Starter rod free-supply contract changed")
    prices["StarterRodT1"] = 0
    toy_price = re.fullmatch(r"公款 (\d+) 金币购买", rods.get("玩具竿", ""))
    if not toy_price:
        raise RuntimeError("Toy rod price needs an explicit design mapping")
    prices["ShopRodT2"] = int(toy_price.group(1))
    aliases = {"gear_net": "StarterScoopNet", "gear_keepnet": "FishGuard", ITEM_ID: ITEM_ID}
    with (items / "道具总表/道具.csv").open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.reader(handle):
            if row and row[-1] in aliases:
                prices[aliases[row[-1]]] = int(row[2])
    parameters = (ROOT / "Knowledge/Design/数值模拟与参数记录.md").read_text(encoding="utf-8-sig")
    upgrade_prices = set(re.findall(r"升级价占位 (\d+)／(\d+) 金币", parameters))
    if len(upgrade_prices) != 1 or not set(aliases.values()).issubset(prices):
        raise RuntimeError("Tank/item prices need an explicit current design mapping")
    tier2, tier3 = next(iter(upgrade_prices))
    prices.update(FishTankCapacityT2=int(tier2), FishTankCapacityT3=int(tier3))
    return prices


def _prepare_rows(rows, stock_policies):
    prices = _design_prices()
    unresolved = sorted({row["DefinitionId"] for row in rows if row["DefinitionId"] not in prices})
    if unresolved:
        raise RuntimeError(f"Content owner must resolve legacy definitions against design before applying: {unresolved}")
    if len({row["DefinitionId"] for row in rows}) != len(rows):
        raise RuntimeError("Resolve duplicate definition rows before original-price migration")
    missing = REQUIRED_ROWS - {row["DefinitionId"] for row in rows}
    if missing - stock_policies.keys():
        raise RuntimeError(f"Provide explicit stock policies for new rows: {sorted(missing - stock_policies.keys())}")
    for definition in sorted(missing):
        policy = stock_policies[definition]
        allowed = {"bUnlimitedStock", "InitialStock", "bDailyRestock", "DailyRestockQuantity"}
        if set(policy) - allowed or "bUnlimitedStock" not in policy:
            raise ValueError(f"Supply stock fields only for {definition}")
        unlimited = policy["bUnlimitedStock"]
        daily = policy.get("bDailyRestock", False)
        if (not unlimited and policy.get("InitialStock", 0) <= 0) or (daily and (unlimited or policy.get("DailyRestockQuantity", 0) <= 0)):
            raise ValueError(f"Incomplete stock policy: {definition}")
        rows.append(dict(Name=definition, EntryId=definition, DefinitionId=definition,
                         PurchaseQuantity=1, bEnabled=True, bAlwaysStocked=True, **policy))
    for row in rows:
        quantity = int(row.get("PurchaseQuantity", 0))
        if quantity <= 0:
            raise ValueError(f"Invalid purchase quantity: {row['DefinitionId']}")
        row["UnitPrice"] = prices[row["DefinitionId"]] * quantity
        if row["DefinitionId"] == ITEM_ID:
            row["DisplayNameOverride"] = "小鱼干"
    return rows


def _catalog():
    table = unreal.EditorAssetLibrary.load_asset(CATALOG)
    if table is None:
        raise RuntimeError(f"Missing catalog: {CATALOG}")
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    return table, rows


def audit():
    """只读导出完整商品原价，用于核对随机货架以外的装备兑换来源。"""
    _, rows = _catalog()
    prices = _design_prices()
    for row in rows:
        unreal.log("Event=ShopOriginalPriceAudit Definition={} Price={} Quantity={} DesignEach={}".format(
            row.get("DefinitionId"), row.get("UnitPrice"), row.get("PurchaseQuantity"),
            prices.get(row.get("DefinitionId"), "UNRESOLVED")))
    matches = [row for row in rows if row.get("DefinitionId") == ITEM_ID]
    unreal.log(f"Event=ShopDriedFishCatalogAudit Rows={len(matches)}")
    unreal.log(f"Event=ShopRequiredRowsAudit Missing={sorted(REQUIRED_ROWS - {row['DefinitionId'] for row in rows})}")
    return rows


def migrate(level_package, rack_cm, store_cm, driedfish_package, stock_policies, apply=False):
    """复用 CampHub 的原仓作为公共架，增加公库，保留旧档的原仓绑定。"""
    if not level_package.startswith("/Game/") or not driedfish_package.startswith("/Game/"):
        raise ValueError("Supply /Game package names, not filesystem paths")
    if len(rack_cm) != 3 or len(store_cm) != 3 or tuple(rack_cm) == tuple(store_cm):
        raise ValueError("Provide distinct rack/store positions in centimeters")
    dried = unreal.EditorAssetLibrary.load_asset(driedfish_package)
    if not isinstance(dried, unreal.CatInventoryItemDefinition):
        raise RuntimeError("Item owner must provide the real buff_driedfish definition")
    identity_field = "equipment_definition_id" if isinstance(dried, unreal.CatEquipmentDefinition) else "inventory_definition_id"
    if str(dried.get_editor_property(identity_field)) != ITEM_ID:
        raise RuntimeError("Item owner must provide the real buff_driedfish definition")
    table, rows = _catalog()
    rows = _prepare_rows(rows, stock_policies)
    price = _design_prices()[ITEM_ID]
    if price <= 0:
        raise RuntimeError("Design selling price must be positive")
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not levels.load_level(level_package):
        raise RuntimeError(f"Cannot load {level_package}")
    hubs = [actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.CatCampHubActor)]
    if len(hubs) != 1:
        raise RuntimeError("Expected one explicit CampHub binding")
    rack = hubs[0].get_editor_property("public_inventory")
    if rack is None:
        raise RuntimeError("Keep the original CampHub.PublicInventory binding for legacy saves")
    stores = []
    for actor in actors.get_all_level_actors():
        if not isinstance(actor, unreal.CatCampInventoryActor):
            continue
        inventory = actor.get_component_by_class(unreal.CatInventoryComponent)
        role = inventory.get_editor_property("team_storage_role")
        if role == unreal.CatTeamStorageRole.SUPPLY_STORE:
            stores.append(actor)
        if role == unreal.CatTeamStorageRole.EQUIPMENT_RACK and actor != rack:
            raise RuntimeError("Another equipment rack already exists; resolve its consumers first")
    if len(stores) > 1 or (stores and stores[0] == rack):
        raise RuntimeError("Ambiguous supply-store role")
    config = ROOT / "Config/DefaultGame.ini"
    text = config.read_text(encoding="utf-8-sig")
    section = "[/Script/Catfishing.CatInventorySettings]"
    if section not in text:
        raise RuntimeError("Missing project inventory settings section")
    binding = f'+Definitions=(DefinitionId={ITEM_ID},ItemDefinition="{dried.get_path_name()}")'
    existing = [line for line in text.splitlines() if line.startswith("+Definitions=")
                and re.search(rf'DefinitionId\s*=\s*"?{ITEM_ID}"?\s*[,)]', line)]
    if existing and (len(existing) != 1 or not re.search(
            rf'ItemDefinition\s*=\s*"?{re.escape(dried.get_path_name())}"?\s*[,)]', existing[0])):
        raise RuntimeError("Conflicting inventory definition binding; resolve it explicitly")
    unreal.log(f"Event=ShopMigrationPrepared Level={level_package} DriedFish={driedfish_package} Price={price} Apply={apply}")
    if not apply:
        return
    # 不改原仓名字或 Hub 绑定；新增公库后，旧单仓档仍有明确迁移目标。
    rack.get_component_by_class(unreal.CatInventoryComponent).set_editor_property(
        "team_storage_role", unreal.CatTeamStorageRole.EQUIPMENT_RACK)
    rack.set_actor_location(unreal.Vector(*rack_cm), False, True)
    store = stores[0] if stores else actors.spawn_actor_from_class(rack.get_class(), unreal.Vector(*store_cm))
    if store is None:
        raise RuntimeError("Could not create supply store")
    store.set_actor_label("CampSupplyStore")
    store.set_actor_location(unreal.Vector(*store_cm), False, True)
    store.get_component_by_class(unreal.CatInventoryComponent).set_editor_property(
        "team_storage_role", unreal.CatTeamStorageRole.SUPPLY_STORE)
    if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows, ensure_ascii=False)):
        raise RuntimeError("Catalog import rejected; do not save the level")
    if not unreal.EditorAssetLibrary.save_loaded_asset(table, False) or not levels.save_current_level():
        raise RuntimeError("Migration save failed; inspect the table and level before continuing")
    # 仅修改项目默认目录的精确一行；不向本机 Saved/Config 写入第二套商品映射。
    if not existing:
        config.write_text(text.replace(section, section + "\n" + binding, 1), encoding="utf-8")
    unreal.log("Event=ShopMigrationSaved Next=RestartEditorThenSaveColdRoundTripAndTwoEndpointShopping")


if __name__ == "__main__":
    audit()
