"""Read-only pre/post removal audit for retired fishing entry points and nodes.

Run in Unreal Editor Python, or with UnrealEditor-Cmd -ExecutePythonScript.
Never saves assets. Reports go to Saved/Automation/FishingLegacyCleanup/AssetAudit.json.
Any unreadable package, unresolved LFS pointer, or legacy reference fails the audit.
"""

import hashlib
import json
import mmap
from pathlib import Path
import re

import unreal


LEGACY_NAMES = (
    "CatFishingFightExchangeTask", "CatFishingFightExchangeTaskInstanceData",
    "CatFishingResolveTrueBiteSelectionTask", "ResolveFightExchangeFromStateTree",
    "SubmitFightAssist", "ForwardLegacyAssist", "ForwardLegacyScoop",
    "CatGE_FightStaminaRegen", "CatFightStaminaRegenEffect", "FightStaminaRegenPerPeriod",
)


def main():
    project = Path(__file__).resolve().parents[1]
    output = Path(unreal.Paths.project_saved_dir()).resolve() / "Automation/FishingLegacyCleanup"
    output.mkdir(parents=True, exist_ok=True)
    report = {"packages_scanned": 0, "hits": [], "errors": [], "trees": [], "tree_hashes": {}}
    pattern = re.compile(b"|".join(re.escape(name.encode(encoding))
        for name in LEGACY_NAMES for encoding in ("ascii", "utf-16-le")))
    packages = sorted({p for root in (project / "Content", project / "Plugins")
        for p in root.rglob("*") if p.suffix in (".uasset", ".umap")})
    for path in packages:
        try:
            with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
                if data[:64].startswith(b"version https://git-lfs.github.com/spec/v1"):
                    raise RuntimeError("Unresolved Git LFS pointer")
                hits = sorted({hit.group().replace(b"\x00", b"").decode("ascii")
                    for hit in pattern.finditer(data)})
                if hits:
                    report["hits"].append({"package": str(path.relative_to(project)), "names": hits})
                report["packages_scanned"] += 1
                if "StateTree" in str(path):
                    report["tree_hashes"][str(path.relative_to(project))] = hashlib.sha256(data).hexdigest()
        except Exception as error:
            report["errors"].append({"package": str(path), "error": str(error)})

    def export(value):
        if isinstance(value, unreal.Array):
            return [export(item) for item in value]
        return value.export_text() if hasattr(value, "export_text") else str(value)

    def state_snapshot(state):
        return {"name": str(state.get_editor_property("name")),
            "tasks": export(state.get_editor_property("tasks")),
            "transitions": export(state.get_editor_property("transitions")),
            "conditions": export(state.get_editor_property("enter_conditions")),
            "children": [state_snapshot(child) for child in state.get_editor_property("children")]}

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    for asset in registry.get_all_assets():
        package = str(asset.package_name)
        if str(asset.asset_class_path.asset_name) != "StateTree" or package.startswith("/Engine/"):
            continue
        try:
            tree = asset.get_asset()
            if tree is None:
                raise RuntimeError("Cannot load StateTree")
            editor_data = unreal.StateTreeEditorData.get_editor_data(tree)
            if editor_data is None:
                raise RuntimeError("Cannot inspect StateTree editor data")
            snapshot = {"package": package,
                "global_tasks": export(editor_data.get_editor_property("global_tasks")),
                "evaluators": export(editor_data.get_editor_property("evaluators")),
                "states": [state_snapshot(state) for state in editor_data.get_editor_property("sub_trees")]}
            serialized = json.dumps(snapshot)
            hits = [name for name in LEGACY_NAMES if name in serialized]
            if hits:
                report["hits"].append({"package": package, "names": hits})
            report["trees"].append(snapshot)
        except Exception as error:
            report["errors"].append({"package": package, "error": str(error)})
    required = {"/Game/Data/StateTrees/ST_FishingSession", "/Game/Data/StateTrees/ST_FishFight"}
    if not required.issubset({tree["package"] for tree in report["trees"]}):
        report["errors"].append({"error": "Configured fishing StateTrees were not inspected"})
    report["passed"] = report["packages_scanned"] > 0 and not report["hits"] and not report["errors"]
    path = output / "AssetAudit.json"
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("Event=fishing_legacy_node_audit Packages={} Trees={} Hits={} Errors={} Passed={} Report={}".format(
        report["packages_scanned"], len(report["trees"]), len(report["hits"]), len(report["errors"]), report["passed"], path))
    if not report["passed"]:
        raise RuntimeError("Fishing legacy node audit failed; inspect " + str(path))


if __name__ == "__main__":
    main()
