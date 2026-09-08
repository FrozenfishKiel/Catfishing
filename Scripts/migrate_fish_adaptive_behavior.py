"""Audit or migrate the formal fish behavior assets without resetting designer values.

Run with UnrealEditor-Cmd -ExecutePythonScript=<this file>. The default is read-only.
Add -ApplyFishAdaptiveMotion to save the four migrated personalities and balance,
then replace only the audited legacy ST_FishFight. Optional
-FishAdaptiveEvidenceDir=<directory under project Saved> selects the evidence root.

Personality PostLoad may migrate values in memory; an audit never saves them.
The old tree fingerprint was verified by the 2026-09-08 read-only topology audit.
A different legacy tree requires a fresh audit, not a forced overwrite. Existing
adaptive trees and version-1 personality tuning are preserved.
"""

from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil

import unreal


TREE_PATH = "/Game/Data/StateTrees/ST_FishFight"
BALANCE_PATH = "/Game/Catfishing/Data/Fishing/DA_FishingFightBalance_Default"
LEGACY_TREE_SHA256 = "c8e826124eeb8715aa437206a3d9b456a47fe8264c797bb74e752d7c6b81d0ad"
PERSONALITIES = ("SmallRestless", "MediumSteady", "LargePredator", "GiantHeavy")
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


def _snapshot(tree, personalities, balance):
    personality_rows = [{
        "path": path,
        "sha256": _hash(path),
        "post_load_values": _properties(asset, (
            "fight_personality_id", "adaptive_motion_version",
            "full_effort_movement_speed_centimeters_per_second", "adaptive_steering_config",
        )),
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
        "balance": {"path": BALANCE_PATH, "sha256": _hash(BALANCE_PATH),
                    "fish_effort_stamina_per_second": float(balance.get_editor_property("fish_effort_stamina_per_second")),
                    "runtime_ready": bool(balance.is_runtime_definition_ready())},
        "protected_asset_hashes": {path: _hash(path) for path in _protected_paths()},
    }


def _write(path, report):
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


def main():
    command_line = unreal.SystemLibrary.get_command_line()
    apply = bool(re.search(r"(?:^|\s)-ApplyFishAdaptiveMotion(?:\s|$)", command_line, re.IGNORECASE))
    match = re.search(r'-FishAdaptiveEvidenceDir=(?:"([^"]+)"|(\S+))', command_line, re.IGNORECASE)
    evidence = Path(match.group(1) or match.group(2)).resolve() if match else (
        PROJECT_DIR / "Saved/Automation/FishAdaptiveMotion" / datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
    _require(evidence.is_relative_to(PROJECT_DIR / "Saved"), "Evidence must stay under project Saved")
    evidence.mkdir(parents=True, exist_ok=True)
    report_path = evidence / ("Migration.json" if apply else "Audit.json")
    _require(not report_path.exists(), "Evidence already exists; use a new evidence directory")
    tree = _load(TREE_PATH)
    personalities = [("/Game/Catfishing/Data/Fish/Fight_" + name,
                      _load("/Game/Catfishing/Data/Fish/Fight_" + name)) for name in PERSONALITIES]
    balance = _load(BALANCE_PATH)
    before = _snapshot(tree, personalities, balance)
    kind = _tree_kind(before["tree"])
    report = {"apply": apply, "status": "audited", "tree_kind": kind, "before": before, "saved_assets": []}
    _write(report_path, report)
    _require(kind != "unrecognized", "Fish tree differs from both audited legacy and adaptive topology; review before replacing")
    _require(all(row["runtime_ready"] for row in before["personalities"]), "A personality fails native readiness")
    _require(before["balance"]["runtime_ready"], "Fight balance fails native readiness")
    if apply:
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
            _require(before["balance"]["fish_effort_stamina_per_second"] == after["balance"]["fish_effort_stamina_per_second"],
                     "Existing new fish effort price was overwritten")
            report.update(status="saved_requires_fresh_process_verification", after=after)
        except Exception as exc:
            report.update(status="failed_may_have_partial_saves", error=str(exc))
            _write(report_path, report)
            raise
        _write(report_path, report)
    unreal.log("FISH_ADAPTIVE_ASSET_PASS Apply={} Tree={} Fish=16 Personalities=4 Report={}".format(apply, kind, report_path))


if __name__ == "__main__":
    main()
