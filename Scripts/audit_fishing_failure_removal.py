"""Read-only audit for removing the obsolete fishing failure-budget scheme.

Run with UE 5.8 UnrealEditor-Cmd -ExecutePythonScript=<this file>, before
removing native types and again after rebuilding. Never saves assets.
-FailureRemovalEvidenceDir=<path under project Saved> selects the report folder.
Scans serialized names in every project/plugin package (ASCII and UTF-16), then
loads and inspects every registered project StateTree, including editor topology.
An unreadable, unresolved LFS, or legacy-dependent package fails the audit.
"""

import hashlib
import json
import mmap
from pathlib import Path
import re

import unreal


LEGACY_NAMES = (
    "CatFishingFailureBudgetTask", "CatFishingFailureBudgetTaskInstanceData",
    "CatFishingResolveRetryExhaustedTask", "CatFishingFailurePenalty",
    "CatFishingFailureResult", "CommitFishingFailure",
    "CommitFailureBudgetFromStateTree", "ResolveRetryExhaustedEscapeFromStateTree",
    "RecordRetryExhaustedSilhouette", "RodFailureDurabilityLoss",
    "LoseSpecialBait", "FailureTerminalCache", "FailureBudgetResult",
    "bFailureBudgetCommitted", "SilhouetteGrantByFishingSession",
)
REQUIRED_TREE = "/Game/Data/StateTrees/ST_FishingSession.ST_FishingSession"


def value(item):
    if item is None or isinstance(item, (str, int, float, bool)):
        return item
    if isinstance(item, unreal.Object):
        return item.get_path_name()
    if isinstance(item, unreal.Array):
        return [value(entry) for entry in item]
    if hasattr(item, "export_text"):
        return item.export_text()
    return str(item)


def properties(obj, names):
    return {name: value(obj.get_editor_property(name)) for name in names}


def state_snapshot(state):
    result = properties(state, (
        "name", "tasks", "transitions", "enter_conditions", "considerations",
        "parameters", "linked_asset",
    ))
    result["children"] = [state_snapshot(child) for child in state.get_editor_property("children")]
    return result


def main():
    project = Path(unreal.Paths.project_dir()).resolve()
    match = re.search(r'-FailureRemovalEvidenceDir=(?:"([^"]+)"|(\S+))', unreal.SystemLibrary.get_command_line())
    output = Path(next(part for part in match.groups() if part)) if match else project / "Saved/Automation/FishingFailureRemoval/Audit"
    output = output.resolve()
    if not output.is_relative_to(project / "Saved"):
        raise RuntimeError("Evidence must stay inside project Saved")
    output.mkdir(parents=True, exist_ok=True)
    report = {"packages_scanned": 0, "serialized_hits": [], "errors": [], "trees": [], "package_hashes": {}}
    pattern = re.compile(b"|".join(re.escape(name.encode(encoding)) for name in LEGACY_NAMES for encoding in ("ascii", "utf-16-le")))
    packages = sorted(set(project.joinpath("Content").rglob("*.uasset"))
                      | set(project.joinpath("Content").rglob("*.umap"))
                      | {p for p in project.joinpath("Plugins").rglob("*")
                         if p.suffix in (".uasset", ".umap") and "Content" in p.parts})
    mounts = {"/Game"}
    for path in packages:
        relative = str(path.relative_to(project))
        try:
            with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
                if data[:64].startswith(b"version https://git-lfs.github.com/spec/v1"):
                    raise RuntimeError("Unresolved Git LFS pointer")
                hits = sorted({hit.group().replace(b"\x00", b"").decode("ascii") for hit in pattern.finditer(data)})
                if hits:
                    report["serialized_hits"].append({"package": relative, "names": hits})
                report["packages_scanned"] += 1
                if "StateTree" in relative or hits:
                    report["package_hashes"][relative] = hashlib.sha256(data).hexdigest()
            if path.is_relative_to(project / "Plugins"):
                content = next(parent for parent in path.parents if parent.name == "Content")
                descriptors = list(content.parent.glob("*.uplugin"))
                if len(descriptors) != 1:
                    raise RuntimeError("Cannot resolve plugin mount")
                mounts.add("/" + descriptors[0].stem)
        except Exception as error:
            report["errors"].append({"package": relative, "error": str(error)})
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    for mount in sorted(mounts):
        for asset in registry.get_assets_by_path(mount, recursive=True):
            if str(asset.asset_class_path.asset_name) != "StateTree":
                continue
            path = str(asset.package_name) + "." + str(asset.asset_name)
            try:
                tree = asset.get_asset()
                if tree is None:
                    raise RuntimeError("Cannot load StateTree")
                editor_data = unreal.StateTreeEditorData.get_editor_data(tree)
                if editor_data is None:
                    raise RuntimeError("Cannot inspect StateTree editor data")
                snapshot = {"path": path, "editor_data": properties(editor_data, ("schema", "global_tasks", "evaluators")),
                            "sub_trees": [state_snapshot(s) for s in editor_data.get_editor_property("sub_trees")]}
                text = json.dumps(snapshot, ensure_ascii=False)
                found = [name for name in LEGACY_NAMES if name in text]
                if found:
                    report["errors"].append({"package": path, "legacy_nodes": found})
                report["trees"].append(snapshot)
            except Exception as error:
                report["errors"].append({"package": path, "error": str(error)})
    if not any(tree["path"] == REQUIRED_TREE for tree in report["trees"]):
        report["errors"].append({"error": "Configured fishing session StateTree was not inspected"})
    report["passed"] = report["packages_scanned"] > 0 and not report["serialized_hits"] and not report["errors"]
    (output / "Audit.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("Event=fishing_failure_removal_asset_audit Packages={} Trees={} Hits={} Errors={} Passed={} Report={}".format(
        report["packages_scanned"], len(report["trees"]), len(report["serialized_hits"]), len(report["errors"]), report["passed"], output / "Audit.json"))
    if not report["passed"]:
        raise RuntimeError("Failure-budget asset reference audit failed; inspect Audit.json")


main()
