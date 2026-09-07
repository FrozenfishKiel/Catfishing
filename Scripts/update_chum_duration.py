"""把四种正式窝料由 60 秒迁移到 180 秒；-VerifyChumTuning 只验证，不保存资产。"""
import math

import unreal


DEFINITIONS = (
    ("Bug", "BugChum"),
    ("FermentedGrain", "FermentedGrainChum"),
    ("FruitFragrance", "FruitFragranceChum"),
    ("HolyLight", "HolyLightChum"),
)
OLD_DURATION_SECONDS = 60.0
NEW_DURATION_SECONDS = 180.0


def preserved_influence(influence):
    contribution = influence.get_editor_property("base_contribution")
    return (
        influence.get_editor_property("radius_centimeters"),
        tuple(contribution.get_editor_property(axis) for axis in ("fishy", "fragrant", "fermented")),
        influence.get_editor_property("distance_falloff_curve"),
        influence.get_editor_property("time_falloff_curve"),
        influence.get_editor_property("maximum_quantity_per_placement"),
        influence.get_editor_property("presentation_id"),
        influence.get_editor_property("presentation_class"),
    )


def main():
    verify_only = "-verifychumtuning" in unreal.SystemLibrary.get_command_line().lower()
    settings = unreal.get_default_object(unreal.load_class(None, "/Script/Catfishing.CatChumFieldSettings"))
    area_multiplier = float(settings.get_editor_property("InfluenceAreaMultiplier"))
    if not math.isclose(area_multiplier, 8.0):
        raise RuntimeError("Expected InfluenceAreaMultiplier=8, got {}".format(area_multiplier))

    # 先检查全部资产，遇到其他人的数值调整就停止，避免覆盖调参或只迁移了一部分。
    pending = []
    for suffix, expected_id in DEFINITIONS:
        path = "/Game/Catfishing/Data/Equipment/Equip_Chum_" + suffix
        asset = unreal.load_asset(path)
        if not asset:
            raise RuntimeError("Missing formal chum: " + path)
        actual_id = str(asset.get_editor_property("equipment_definition_id"))
        if actual_id != expected_id:
            raise RuntimeError("Unexpected definition identity: {} {}".format(path, actual_id))
        influence = asset.get_editor_property("chum_influence")
        duration = float(influence.get_editor_property("duration_seconds"))
        allowed = (NEW_DURATION_SECONDS,) if verify_only else (OLD_DURATION_SECONDS, NEW_DURATION_SECONDS)
        if duration not in allowed:
            raise RuntimeError("Unexpected duration; preserve existing tuning: {} {}".format(path, duration))
        pending.append((asset, influence, duration, preserved_influence(influence)))

    for asset, influence, duration, before in pending:
        if duration != NEW_DURATION_SECONDS:
            influence.set_editor_property("duration_seconds", NEW_DURATION_SECONDS)
            asset.set_editor_property("chum_influence", influence)
            if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
                raise RuntimeError("Failed saving " + asset.get_path_name())
        actual = asset.get_editor_property("chum_influence")
        if preserved_influence(actual) != before:
            raise RuntimeError("Unrelated influence fields changed: " + asset.get_path_name())
        if float(actual.get_editor_property("duration_seconds")) != NEW_DURATION_SECONDS:
            raise RuntimeError("Duration was not applied: " + asset.get_path_name())
        radius = float(actual.get_editor_property("radius_centimeters")) * math.sqrt(area_multiplier)
        unreal.log("CHUM_TUNING_VERIFIED Asset={} EffectiveRadiusCm={:.6f} DiameterM={:.6f} DurationSeconds={:.1f}".format(
            asset.get_path_name(), radius, radius * 2.0 / 100.0, NEW_DURATION_SECONDS))
    unreal.log("CHUM_TUNING_PASS Mode={}".format("VerifyOnly" if verify_only else "Migrate"))


if __name__ == "__main__":
    main()
