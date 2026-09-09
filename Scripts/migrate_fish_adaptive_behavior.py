"""Audit or migrate the formal fish behavior assets without resetting designer values.

Run with UnrealEditor-Cmd -ExecutePythonScript=<this file>. The default is read-only.
Add -ApplyFishAdaptiveMotion to save the four migrated personalities and balance,
then replace only the audited legacy ST_FishFight.
Add -AuditFishResistanceTuning for a read-only preview of the explicit resistance
tuning, or -ApplyFishResistanceTuning to apply it to the five fingerprinted
adaptive packages. These modes require the new ActiveBoutDurationRangeSeconds
native field; compile first. Resistance tuning never saves the balance asset.
Add -AuditFishUnfulfilledStamina to preview the independent per-meter price, or
-ApplyFishUnfulfilledStamina to save only the fingerprinted balance package after
a complete native rebuild. Its loaded new price is preserved, never reset.
All explicit mode switches are mutually exclusive. Optional
-FishAdaptiveEvidenceDir=<directory under project Saved> selects the evidence root.

Personality PostLoad may migrate values in memory; an audit never saves them.
The old tree fingerprint was verified by the 2026-09-08 read-only topology audit.
A different legacy tree requires a fresh audit, not a forced overwrite. Existing
adaptive trees and version-1 personality tuning are preserved.
Resistance tuning is an explicit exception for the audited 2026-09-08 packages;
it refuses unknown fingerprints, including an already-applied or partial run.
Always verify a successful save with a new UnrealEditor-Cmd read-only process.
"""

from datetime import datetime
import hashlib
import json
import math
from pathlib import Path
import re
import shutil

import unreal


TREE_PATH = "/Game/Data/StateTrees/ST_FishFight"
BALANCE_PATH = "/Game/Catfishing/Data/Fishing/DA_FishingFightBalance_Default"
UNFULFILLED_BALANCE_BASELINE_SHA256 = "4042ca6ea1ae526e1cccf8c763d85d14b7555b85b3b2014b9ac5e7e89dca5d1b"
UNFULFILLED_STAMINA_DEFAULT = 5.0 / 3.0
BALANCE_VALUE_FIELDS = (
    "balance_definition_id", "enable_runtime_definition", "force_model_version",
    "strength_per_kilogram", "force_per_strength_newtons", "cat_body_mass_kilograms",
    "exhausted_reel_force_newtons", "exhausted_cat_tow_acceleration_centimeters_per_second_squared",
    "reel_speed_centimeters_per_second", "exhausted_cat_escape_speed_multiplier",
    "cat_stamina_cost_per_strength_centimeter", "cat_rod_stamina_cost_per_strength_radian",
    "cat_unloaded_work_multiplier", "cat_support_stamina_per_second", "cat_movement_stamina_multiplier",
    "cat_reel_stamina_multiplier", "cat_rod_stamina_multiplier", "cat_hold_stamina_multiplier",
    "cat_load_stamina_multiplier", "slack_stamina_regen_per_second", "fish_exhaustion_threshold",
    "display_tension_newtons", "escape_slack_centimeters", "stalemate_rod_wear_per_fish_strength",
    "held_rod_minimum_leverage_multiplier", "maximum_fish_constraint_correction_speed_centimeters_per_second",
)
BALANCE_LEGACY_FIELDS = (
    "fish_effort_stamina_per_second", "fish_stamina_cost_per_strength_centimeter",
    "fish_load_stamina_multiplier", "isometric_effort_multiplier", "low_stamina_rest_threshold",
    "low_stamina_rest_multiplier", "acceleration_per_strength", "drive_response_seconds",
    "tension_response_range_centimeters", "minimum_carrier_away_speed_multiplier",
)
LEGACY_TREE_SHA256 = "c8e826124eeb8715aa437206a3d9b456a47fe8264c797bb74e752d7c6b81d0ad"
PERSONALITIES = ("SmallRestless", "MediumSteady", "LargePredator", "GiantHeavy")
# Independent FreshReload/Audit.json plus the resistance disk-hash audit establish
# this exact source revision. A designer edit requires review, never a force flag.
RESISTANCE_BASELINE_SHA256 = {
    TREE_PATH: "9bb499b1c937801f39d392675fc3b79dd162f3d79efc42a7f342e7e49566599f",
    "/Game/Catfishing/Data/Fish/Fight_SmallRestless": "73ac78c225f27c7aa9599e86e2dfffc2aa563ab705b09b34cb6efd6d198e374b",
    "/Game/Catfishing/Data/Fish/Fight_MediumSteady": "76179aede3a5c7628525ed1210e3835fee0b7e8d23d5974a1538a9d000e5ca02",
    "/Game/Catfishing/Data/Fish/Fight_LargePredator": "cba6fc0b55052139255bd66f0d9b23aa4c60d5804c9173517387790d903f088b",
    "/Game/Catfishing/Data/Fish/Fight_GiantHeavy": "e1c6b265b7e30202548fdafefdf650484ed05afe60affb0f56264bf9455bca28",
}
RESISTANCE_SHARED_TUNING = {
    "lateral_outward_bias": 0.9,
    "ease_off_inward_bias": 0.0,
    "lateral_effort_range": (0.75, 0.95),
    "ease_off_effort_range": (0.3, 0.45),
    "minimum_behavior_duration_seconds": 1.25,
}
RESISTANCE_PROFILE_DURATIONS = {
    "SmallRestless": ((6.0, 8.0), (1.25, 1.5)),
    "MediumSteady": ((7.0, 10.0), (1.25, 1.75)),
    "LargePredator": ((8.0, 12.0), (1.25, 2.0)),
    "GiantHeavy": ((10.0, 14.0), (1.25, 2.0)),
}
STEERING_FIELDS = (
    "retarget_duration_range_seconds", "maximum_turn_rate_degrees_per_second",
    "outward_angular_spread_degrees", "lateral_outward_bias", "ease_off_inward_bias",
    "outward_effort_range", "lateral_effort_range", "ease_off_effort_range",
    "effort_rise_per_second", "effort_fall_per_second", "outward_duration_range_seconds",
    "lateral_duration_range_seconds", "ease_off_duration_range_seconds",
    "minimum_behavior_duration_seconds", "low_stamina_ratio",
    "low_stamina_active_duration_multiplier", "low_stamina_ease_off_duration_multiplier",
    "blocked_load_threshold", "blocked_progress_fraction", "blocked_confirmation_seconds",
    "load_smoothing_seconds",
)
RESISTANCE_EXPECTED_EDGES = {
    "OutwardRush": [
        ("EaseOff", ["NeedsRecovery"]),
        ("LateralArc", ["MinimumDurationElapsed", "SustainedBlocked"]),
        ("EaseOff", ["DurationExpired"]),
    ],
    "LateralArc": [
        ("EaseOff", ["NeedsRecovery"]),
        ("OutwardRush", ["MinimumDurationElapsed", "SustainedBlocked"]),
        ("OutwardRush", ["DurationExpired"]),
    ],
    "EaseOff": [
        ("LateralArc", ["DurationExpired", "SustainedBlocked"]),
        ("OutwardRush", ["DurationExpired"]),
    ],
}
FISH_NAMES = (
    "RiverPattern", "LittleSilver", "LittleColor", "ForestLongtail", "SilvermoonTrout",
    "LakeGiantShadow", "Petal", "Windbell", "Salted", "Stinky", "Blackfish", "Loach",
    "EstuaryBass", "Puffer", "ElectricEel", "Pike",
)
PROJECT_DIR = Path(unreal.Paths.project_dir()).resolve()


def _require(condition, message):
    if not condition:
        raise RuntimeError(message)


def _file(package_path):
    _require(package_path.startswith("/Game/"), "Only project assets may be migrated")
    return PROJECT_DIR / "Content" / (package_path.removeprefix("/Game/") + ".uasset")


def _hash(package_path):
    return hashlib.sha256(_file(package_path).read_bytes()).hexdigest()


def _value(value):
    if value is None or isinstance(value, (str, int, float, bool)):
        return value
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    if isinstance(value, unreal.Array):
        return [_value(entry) for entry in value]
    if hasattr(value, "export_text"):
        return value.export_text()
    return str(value)


def _properties(obj, names):
    return {name: _value(obj.get_editor_property(name)) for name in names}


def _load(path):
    asset = unreal.load_asset(path)
    _require(asset is not None, "Asset could not be loaded: " + path)
    return asset


def _state(state):
    result = _properties(state, (
        "name", "description", "selection_behavior", "linked_asset", "tasks",
        "transitions", "enter_conditions", "considerations", "parameters",
    ))
    result["children"] = [_state(child) for child in state.get_editor_property("children")]
    return result


def _tree_snapshot(tree):
    editor_data = unreal.StateTreeEditorData.get_editor_data(tree)
    _require(editor_data is not None, "Fish StateTree has no inspectable editor data")
    return {
        "path": tree.get_path_name(),
        "sha256": _hash(TREE_PATH),
        "editor_data": _properties(editor_data, ("schema", "global_tasks", "evaluators")),
        "sub_trees": [_state(state) for state in editor_data.get_editor_property("sub_trees")],
    }


def _tree_kind(snapshot):
    roots = snapshot["sub_trees"]
    if len(roots) == 1 and len(roots[0]["children"]) == 3:
        tasks = [child["tasks"] for child in roots[0]["children"]]
        if all(len(items) == 1 for items in tasks):
            behaviors = [re.search(r"Behavior=(\w+)", items[0]) for items in tasks]
            if all(behaviors) and {match.group(1) for match in behaviors} == {"OutwardRush", "LateralArc", "EaseOff"}:
                return "adaptive"
    if snapshot["sha256"] == LEGACY_TREE_SHA256:
        return "audited_legacy"
    return "unrecognized"


def _protected_paths():
    result = ["/Game/Catfishing/Fishing/Animation/Fish/ABPT_CatFishBase",
              "/Game/Data/StateTrees/ST_FishingSession"]
    for name in FISH_NAMES:
        result.extend((
            "/Game/Catfishing/Data/Fish/Fish_" + name,
            "/Game/Catfishing/Data/Fish/Presentation/FishPresentation_" + name,
            "/Game/Catfishing/Fishing/Animation/Fish/ABP_Fish_" + name,
        ))
    return result


def _steering_values(config):
    result = {}
    for name in STEERING_FIELDS:
        value = config.get_editor_property(name)
        result[name] = ([float(value.x), float(value.y)]
                        if isinstance(value, unreal.Vector2D) else _value(value))
    # Generic audits still work with the first adaptive binary. Only resistance
    # mode requires the newly compiled field before it may write anything.
    try:
        value = config.get_editor_property("active_bout_duration_range_seconds")
        result["active_bout_duration_range_seconds"] = [float(value.x), float(value.y)]
    except Exception:
        pass
    return result


def _balance_snapshot(balance):
    try:
        new_price = float(balance.get_editor_property("fish_stamina_per_unfulfilled_meter"))
    except Exception:
        new_price = None  # Read-only evidence may still come from the old binary.
    return {"path": BALANCE_PATH, "sha256": _hash(BALANCE_PATH),
            "fish_stamina_per_unfulfilled_meter": new_price,
            "runtime_values": _properties(balance, BALANCE_VALUE_FIELDS),
            "legacy_values_not_used_for_pricing": _properties(balance, BALANCE_LEGACY_FIELDS),
            "runtime_ready": bool(balance.is_runtime_definition_ready())}


def _snapshot(tree, personalities, balance, protect_balance=False, protect_behavior=False):
    personality_rows = [{
        "path": path,
        "sha256": _hash(path),
        "post_load_values": _properties(asset, (
            "fight_personality_id", "adaptive_motion_version",
            "full_effort_movement_speed_centimeters_per_second", "adaptive_steering_config",
        )),
        "steering_values": _steering_values(asset.get_editor_property("adaptive_steering_config")),
        "runtime_ready": bool(asset.is_runtime_definition_ready()),
    } for path, asset in personalities]
    fish_rows = []
    ids = {str(asset.get_editor_property("fight_personality_id")) for _, asset in personalities}
    for name in FISH_NAMES:
        path = "/Game/Catfishing/Data/Fish/Fish_" + name
        values = _properties(_load(path), ("fish_definition_id", "fight_personality_id", "presentation_definition"))
        _require(values["fight_personality_id"] in ids, "Formal fish references an unknown personality: " + path)
        fish_rows.append({"path": path, "values": values})
    settings_class = unreal.load_class(None, "/Script/Catfishing.CatFishingSettings")
    _require(settings_class is not None, "Fishing settings class is unavailable")
    settings = unreal.get_default_object(settings_class)
    return {
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "runtime_config": _properties(settings, ("FishBehaviorStateTree", "FightPersonalities", "FightBalanceDefinition")),
        "tree": _tree_snapshot(tree), "personalities": personality_rows, "fish": fish_rows,
        "balance": _balance_snapshot(balance),
        "protected_asset_hashes": {path: _hash(path) for path in (
            _protected_paths() + ([BALANCE_PATH] if protect_balance else [])
            + ([TREE_PATH] + [path for path, _ in personalities] if protect_behavior else []))},
    }


def _write(path, report):
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


def _resistance_targets(path):
    profile = path.rsplit("/Fight_", 1)[-1]
    _require(profile in RESISTANCE_PROFILE_DURATIONS, "Unknown resistance profile: " + path)
    active, ease = RESISTANCE_PROFILE_DURATIONS[profile]
    return dict(RESISTANCE_SHARED_TUNING,
                active_bout_duration_range_seconds=active,
                ease_off_duration_range_seconds=ease)


def _matches_value(actual, expected):
    if isinstance(expected, (tuple, list)):
        return isinstance(actual, (tuple, list)) and len(actual) == len(expected) and all(
            _matches_value(left, right) for left, right in zip(actual, expected))
    return isinstance(actual, (int, float)) and math.isclose(actual, expected, rel_tol=0.0, abs_tol=1e-6)


def _resistance_edges(snapshot):
    if _tree_kind(snapshot) != "adaptive":
        return None
    root = snapshot["sub_trees"][0]
    if (root["tasks"] or root["transitions"] or root["enter_conditions"] or
            snapshot["editor_data"]["global_tasks"] or snapshot["editor_data"]["evaluators"]):
        return None
    children = root["children"]
    names = {child["name"]: re.search(r"Behavior=(\w+)", child["tasks"][0]).group(1)
             for child in children}
    result = {}
    for child in children:
        if child["children"] or child["enter_conditions"]:
            return None
        edges = []
        for text in child["transitions"]:
            target = re.search(r'State=\(Name="([^"]+)"', text)
            if (not target or target.group(1) not in names or "Trigger=OnTick," not in text or
                    "LinkType=GotoState," not in text or "bTransitionEnabled=True" not in text or
                    "bDelayTransition=False" not in text or "bInvert=True" in text or
                    "Operand=Or" in text):
                return None
            conditions = re.findall(r"Instance=/Script/Catfishing.CatFishBehaviorConditionInstanceData\(Condition=(\w+)", text)
            edges.append((names[target.group(1)], conditions))
        result[names[child["name"]]] = edges
    return result


def _resistance_preview(before):
    current_hashes = {before["tree"]["path"].split(".", 1)[0]: before["tree"]["sha256"]}
    current_hashes.update({row["path"]: row["sha256"] for row in before["personalities"]})
    rows = []
    for row in before["personalities"]:
        values = row["steering_values"]
        _require("active_bout_duration_range_seconds" in values,
                 "Compile the resistance native code before tuning; ActiveBoutDurationRangeSeconds is unavailable")
        _require(row["post_load_values"]["adaptive_motion_version"] == 1,
                 "Resistance tuning requires an already migrated version-1 personality: " + row["path"])
        targets = _resistance_targets(row["path"])
        rows.append({"path": row["path"], "changes": {
            name: {"before": values[name], "target": value} for name, value in targets.items()},
            "matches_target": all(_matches_value(values[name], value) for name, value in targets.items())})
    return {"baseline_fingerprints_match": current_hashes == RESISTANCE_BASELINE_SHA256,
            "personality_tuning": rows,
            "all_personalities_match_target": all(row["matches_target"] for row in rows),
            "tree_matches_target": _resistance_edges(before["tree"]) == RESISTANCE_EXPECTED_EDGES,
            "expected_tree_edges": RESISTANCE_EXPECTED_EDGES,
            "protected_package_count": len(before["protected_asset_hashes"])}


def _apply_resistance_tuning(tree, personalities, balance, before, evidence, report, report_path):
    _require(report["resistance_tuning"]["baseline_fingerprints_match"],
             "Resistance tuning refuses a changed, already tuned, or partly saved package; audit before any replacement")
    _require(not report["dirty_target_packages_before_load"], "An owned package has unsaved editor changes")
    # Back up all five owned packages and verify each copy before the first edit.
    for path, expected in RESISTANCE_BASELINE_SHA256.items():
        _require(_hash(path) == expected, "Package changed during preflight: " + path)
        source = _file(path)
        target = evidence / "BeforePackages" / source.relative_to(PROJECT_DIR)
        _require(not target.exists(), "Backup already exists: " + str(target))
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        _require(hashlib.sha256(target.read_bytes()).hexdigest() == expected, "Backup hash mismatch: " + path)
    _require(all(_hash(path) == expected for path, expected in before["protected_asset_hashes"].items()),
             "Protected assets changed during preflight")
    report.update(status="backed_up_ready_to_apply", save_attempted=False)
    _write(report_path, report)
    try:
        # Validate all four configured objects in memory before saving any package.
        for path, asset in personalities:
            config = asset.get_editor_property("adaptive_steering_config")
            for name, value in _resistance_targets(path).items():
                config.set_editor_property(name, unreal.Vector2D(*value) if isinstance(value, tuple) else value)
            asset.set_editor_property("adaptive_steering_config", config)
            _require(asset.is_runtime_definition_ready(), "Tuned personality fails native readiness: " + path)
        report["save_attempted"] = True
        _write(report_path, report)
        for path, asset in personalities:
            _require(unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False), "Save failed: " + path)
            report["saved_assets"].append(path)
            _write(report_path, report)
        _require(unreal.CatFishStateTreeAuthoringLibrary.create_or_update_default_fish_behavior_state_tree(),
                 "Could not compile and save resistance fish StateTree")
        report["saved_assets"].append(TREE_PATH)
        _write(report_path, report)
        after = _snapshot(tree, personalities, balance, protect_balance=True)
        report["after"] = after
        _require(_resistance_edges(after["tree"]) == RESISTANCE_EXPECTED_EDGES,
                 "Saved StateTree does not match the eight resistance edges; verify the editor module was rebuilt")
        for old, new in zip(before["personalities"], after["personalities"]):
            targets = _resistance_targets(old["path"])
            _require(all(_matches_value(new["steering_values"][name], value) for name, value in targets.items()),
                     "Tuned value did not persist: " + old["path"])
            _require({key: value for key, value in old["steering_values"].items() if key not in targets} ==
                     {key: value for key, value in new["steering_values"].items() if key not in targets},
                     "An unowned steering setting changed: " + old["path"])
            _require(all(old["post_load_values"][key] == new["post_load_values"][key] for key in (
                "fight_personality_id", "adaptive_motion_version", "full_effort_movement_speed_centimeters_per_second")),
                "Personality identity, schema version, or reference speed changed: " + old["path"])
        _require(before["protected_asset_hashes"] == after["protected_asset_hashes"], "A protected package changed")
        _require(before["fish"] == after["fish"] and before["runtime_config"] == after["runtime_config"],
                 "Formal fish references or runtime configuration changed")
        _require(before["balance"] == after["balance"], "The balance package or effort price changed")
        report.update(status="saved_requires_fresh_process_verification", resistance_tuning_after=_resistance_preview(after))
    except Exception as exc:
        report.update(status=("failed_may_have_partial_saves" if report["save_attempted"] else "failed_before_asset_save"),
                      error=str(exc))
        _write(report_path, report)
        raise
    _write(report_path, report)


def _unfulfilled_price_preview(before):
    price = before["balance"]["fish_stamina_per_unfulfilled_meter"]
    _require(price is not None and math.isfinite(price) and price >= 0.0,
             "A finite nonnegative FishStaminaPerUnfulfilledMeter requires the completely rebuilt native module")
    return {"balance_fingerprint_matches": before["balance"]["sha256"] == UNFULFILLED_BALANCE_BASELINE_SHA256,
            "loaded_price_to_preserve": price, "independent_native_default": UNFULFILLED_STAMINA_DEFAULT,
            "matches_native_default": math.isclose(price, UNFULFILLED_STAMINA_DEFAULT, rel_tol=0.0, abs_tol=1e-9),
            "price_unit": "stamina_points_per_unfulfilled_meter", "legacy_price_converted": False,
            "protected_package_count": len(before["protected_asset_hashes"])}


def _apply_unfulfilled_stamina(tree, personalities, balance, before, evidence, report, report_path):
    _require(report["unfulfilled_stamina"]["balance_fingerprint_matches"],
             "Balance package changed since the audited baseline; inspect it before saving the new schema")
    _require(not report["dirty_target_packages_before_load"], "Balance has unsaved editor changes")
    _require(_hash(BALANCE_PATH) == UNFULFILLED_BALANCE_BASELINE_SHA256, "Balance changed during preflight")
    _require(all(_hash(path) == expected for path, expected in before["protected_asset_hashes"].items()),
             "A protected asset changed during preflight")
    source = _file(BALANCE_PATH)
    backup = evidence / "BeforePackages" / source.relative_to(PROJECT_DIR)
    _require(not backup.exists(), "Balance backup already exists")
    backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, backup)
    _require(hashlib.sha256(backup.read_bytes()).hexdigest() == UNFULFILLED_BALANCE_BASELINE_SHA256,
             "Balance backup hash mismatch")
    report.update(status="backed_up_ready_to_apply", save_attempted=False)
    _write(report_path, report)
    try:
        # Loading introduces the independent native default for old packages.
        # Never assign a default here: a designer may already have a new price.
        _require(balance.is_runtime_definition_ready(), "Balance is not ready for the new price model")
        report["save_attempted"] = True
        _write(report_path, report)
        _require(unreal.EditorAssetLibrary.save_loaded_asset(balance, only_if_is_dirty=False), "Save failed: " + BALANCE_PATH)
        report["saved_assets"].append(BALANCE_PATH)
        after = _snapshot(tree, personalities, balance, protect_behavior=True)
        report["after"] = after
        _require(before["balance"]["fish_stamina_per_unfulfilled_meter"] == after["balance"]["fish_stamina_per_unfulfilled_meter"],
                 "An existing new per-meter price was overwritten")
        _require(before["balance"]["runtime_values"] == after["balance"]["runtime_values"],
                 "Another balance runtime value changed")
        _require(before["protected_asset_hashes"] == after["protected_asset_hashes"], "A protected package changed")
        _require(all(before[key] == after[key] for key in ("tree", "personalities", "fish", "runtime_config")),
                 "Behavior assets, formal fish references, or runtime configuration changed")
        report.update(status="saved_requires_fresh_process_verification", unfulfilled_stamina_after=_unfulfilled_price_preview(after))
    except Exception as exc:
        report.update(status=("failed_may_have_partial_saves" if report["save_attempted"] else "failed_before_asset_save"),
                      error=str(exc))
        _write(report_path, report)
        raise
    _write(report_path, report)


def main():
    command_line = unreal.SystemLibrary.get_command_line()
    def flag(name):
        return bool(re.search(r"(?:^|\s)-" + name + r"(?:\s|$)", command_line, re.IGNORECASE))
    apply_adaptive = flag("ApplyFishAdaptiveMotion")
    apply_resistance = flag("ApplyFishResistanceTuning")
    audit_resistance = flag("AuditFishResistanceTuning")
    apply_unfulfilled = flag("ApplyFishUnfulfilledStamina")
    audit_unfulfilled = flag("AuditFishUnfulfilledStamina")
    _require(sum((apply_adaptive, apply_resistance, audit_resistance, apply_unfulfilled, audit_unfulfilled)) <= 1,
             "Migration modes are mutually exclusive")
    resistance = apply_resistance or audit_resistance
    unfulfilled = apply_unfulfilled or audit_unfulfilled
    apply = apply_adaptive or apply_resistance or apply_unfulfilled
    match = re.search(r'-FishAdaptiveEvidenceDir=(?:"([^"]+)"|(\S+))', command_line, re.IGNORECASE)
    evidence = Path(match.group(1) or match.group(2)).resolve() if match else (
        PROJECT_DIR / "Saved/Automation/FishAdaptiveMotion" / datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
    _require(evidence.is_relative_to(PROJECT_DIR / "Saved"), "Evidence must stay under project Saved")
    evidence.mkdir(parents=True, exist_ok=True)
    report_path = evidence / ("Migration.json" if apply else "Audit.json")
    _require(not report_path.exists(), "Evidence already exists; use a new evidence directory")
    owned_paths = [BALANCE_PATH] if unfulfilled else list(RESISTANCE_BASELINE_SHA256)
    dirty_targets = ([package.get_path_name() for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
                      if package.get_path_name() in owned_paths] if resistance or unfulfilled else [])
    tree = _load(TREE_PATH)
    personalities = [("/Game/Catfishing/Data/Fish/Fight_" + name,
                      _load("/Game/Catfishing/Data/Fish/Fight_" + name)) for name in PERSONALITIES]
    balance = _load(BALANCE_PATH)
    before = _snapshot(tree, personalities, balance, protect_balance=resistance, protect_behavior=unfulfilled)
    kind = _tree_kind(before["tree"])
    report = {"apply": apply, "mode": "unfulfilled_stamina" if unfulfilled else (
                  "resistance_tuning" if resistance else "adaptive_migration"),
              "status": "audited", "tree_kind": kind, "before": before, "saved_assets": [],
              "dirty_target_packages_before_load": dirty_targets}
    _write(report_path, report)
    _require(kind != "unrecognized", "Fish tree differs from both audited legacy and adaptive topology; review before replacing")
    _require(all(row["runtime_ready"] for row in before["personalities"]), "A personality fails native readiness")
    _require(before["balance"]["runtime_ready"], "Fight balance fails native readiness")
    if apply:
        _require(before["balance"]["fish_stamina_per_unfulfilled_meter"] is not None,
                 "Rebuild the native price field before saving any current balance schema")
    if unfulfilled:
        report["unfulfilled_stamina"] = _unfulfilled_price_preview(before)
        _write(report_path, report)
        if apply_unfulfilled:
            _apply_unfulfilled_stamina(tree, personalities, balance, before, evidence, report, report_path)
    elif resistance:
        report["resistance_tuning"] = _resistance_preview(before)
        _write(report_path, report)
        _require(kind == "adaptive", "Resistance tuning requires the audited adaptive source tree")
        if apply_resistance:
            _apply_resistance_tuning(tree, personalities, balance, before, evidence, report, report_path)
    elif apply_adaptive:
        # Back up only the six explicitly owned packages before the first write.
        paths_to_save = [path for path, _ in personalities] + [BALANCE_PATH]
        if kind == "audited_legacy":
            paths_to_save.append(TREE_PATH)
        for path in paths_to_save:
            source = _file(path)
            target = evidence / "BeforePackages" / source.relative_to(PROJECT_DIR)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        try:
            for path, asset in personalities:
                asset.migrate_legacy_motion_settings()
                _require(asset.get_editor_property("adaptive_motion_version") == 1 and asset.is_runtime_definition_ready(),
                         "Migrated personality is not ready: " + path)
                _require(unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False), "Save failed: " + path)
                report["saved_assets"].append(path)
                _write(report_path, report)
            # Preserve the independent native default or an already authored new price.
            _require(unreal.EditorAssetLibrary.save_loaded_asset(balance, only_if_is_dirty=False), "Save failed: " + BALANCE_PATH)
            report["saved_assets"].append(BALANCE_PATH)
            _write(report_path, report)
            if kind == "audited_legacy":
                _require(unreal.CatFishStateTreeAuthoringLibrary.create_or_update_default_fish_behavior_state_tree(),
                         "Could not compile and save adaptive fish StateTree")
                report["saved_assets"].append(TREE_PATH)
            after = _snapshot(tree, personalities, balance)
            _require(_tree_kind(after["tree"]) == "adaptive", "Saved tree does not use all three adaptive behaviors")
            _require(before["protected_asset_hashes"] == after["protected_asset_hashes"], "An asset outside migration scope changed")
            _require(before["fish"] == after["fish"], "Formal fish references changed")
            _require(before["balance"]["fish_stamina_per_unfulfilled_meter"] == after["balance"]["fish_stamina_per_unfulfilled_meter"],
                     "Existing new fish unfulfilled-distance price was overwritten")
            report.update(status="saved_requires_fresh_process_verification", after=after)
        except Exception as exc:
            report.update(status="failed_may_have_partial_saves", error=str(exc))
            _write(report_path, report)
            raise
        _write(report_path, report)
    unreal.log("FISH_ADAPTIVE_ASSET_PASS Mode={} Apply={} Tree={} Fish=16 Personalities=4 Report={}".format(
        report["mode"], apply, kind, report_path))


if __name__ == "__main__":
    main()
