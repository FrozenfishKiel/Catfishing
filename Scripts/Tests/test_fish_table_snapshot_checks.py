"""鱼表发布快照的 contract 回归，不启动 UE、不读取正式资产或 Saved 报告。

运行：python -m unittest discover -s Scripts/Tests -p test_fish_table_snapshot_checks.py -v
需要检查器已有依赖 PyYAML。正式包名来自仓库输入包，包内容与 CSV 均在临时目录模拟。
这些测试验证文件快照与迁移链，不能证明 UE 资产字段或表现正确。
"""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("fish_snapshot_checker", ROOT / "Scripts/check_fish_table_vs_definition.py")
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)
FORMAL_PACKAGE = json.loads((ROOT / ".harness/formal-fish-asset-input-package.json").read_text(encoding="utf-8-sig"))


def sha256(value):
    return hashlib.sha256(value).hexdigest()


class FishTableSnapshotChecksTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="fish-snapshot-checks-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        roster = FORMAL_PACKAGE["generated_assets"]["fish_definitions"]
        self.assertEqual(len(roster), 16, "正式名册变化后需复核本迁移的完整包集合")
        self.assertEqual(len(set(roster)), 16)
        self.assets = {
            "Content/" + package.removeprefix("/Game/") + ".uasset": ("geometry-v2:" + package).encode("utf-8")
            for package in roster
        }
        self.first_asset = next(iter(self.assets))
        numeric = FORMAL_PACKAGE["current_migration"]
        self.sources = {
            numeric["source"]: b"fish_id,numeric_value\nFish_RiverPattern,2\n",
            numeric["bait_source"]: b"bait_id,weight\nMeatBait,1\n",
        }
        for name, content in {**self.assets, **self.sources}.items():
            self.write(name, content)
        before = {name: sha256(b"numeric-v1:" + content) for name, content in self.assets.items()}
        after = {name: sha256(content) for name, content in self.assets.items()}
        self.package = {
            "generated_assets": {"fish_definitions": list(roster)},
            "current_migration": {
                "asset_sha256": before,
                "source": numeric["source"],
                "source_sha256": sha256(self.sources[numeric["source"]]),
                "bait_source": numeric["bait_source"],
                "bait_sha256": sha256(self.sources[numeric["bait_source"]]),
            },
            "subsequent_migrations": [{"before_asset_sha256": dict(before), "asset_sha256": after}],
        }

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    def errors(self, package=None):
        return CHECKER.check_migration_snapshots(self.package if package is None else package, self.root)[0]

    def test_valid_chain_and_complete_formal_package_set(self):
        self.assertEqual(CHECKER.check_migration_snapshots(self.package, self.root), ([], 16))

    def test_original_snapshot_without_followup_remains_supported(self):
        original = copy.deepcopy(self.package)
        original["current_migration"]["asset_sha256"] = original.pop("subsequent_migrations")[0]["asset_sha256"]
        self.assertEqual(self.errors(original), [])

    def test_changed_or_missing_disk_asset_fails(self):
        for mutation in ("changed", "missing"):
            with self.subTest(mutation=mutation):
                path = self.root / self.first_asset
                if mutation == "changed":
                    path.write_bytes(b"unverified edit")
                else:
                    path.unlink()
                self.assertTrue(any(self.first_asset in error for error in self.errors()))
                self.write(self.first_asset, self.assets[self.first_asset])

    def test_changed_source_or_bait_still_fails_after_geometry_migration(self):
        for name, content in self.sources.items():
            with self.subTest(source=name):
                self.write(name, b"unverified source change")
                self.assertTrue(any(name in error for error in self.errors()))
                self.write(name, content)

    def test_discontinuous_before_hash_fails_even_if_latest_files_match(self):
        self.package["subsequent_migrations"][0]["before_asset_sha256"][self.first_asset] = "0" * 64
        self.assertTrue(any("不连续" in error for error in self.errors()))

    def test_missing_or_extra_asset_fails_in_every_snapshot(self):
        for stage, field in (("current_migration", "asset_sha256"),
                             ("subsequent_migrations", "before_asset_sha256"),
                             ("subsequent_migrations", "asset_sha256")):
            for mutation in ("missing", "extra"):
                with self.subTest(stage=stage, field=field, mutation=mutation):
                    package = copy.deepcopy(self.package)
                    record = package[stage] if stage == "current_migration" else package[stage][0]
                    if mutation == "missing":
                        del record[field][self.first_asset]
                    else:
                        record[field]["Content/Catfishing/Data/Fish/Fish_NotInRoster.uasset"] = "0" * 64
                    self.assertTrue(any("集合不一致" in error for error in self.errors(package)))

    def test_invalid_hash_fails(self):
        self.package["subsequent_migrations"][0]["asset_sha256"][self.first_asset] = ""
        self.assertTrue(any("非法 SHA256" in error for error in self.errors()))

    def test_duplicate_roster_fails(self):
        roster = self.package["generated_assets"]["fish_definitions"]
        roster.append(roster[0])
        self.assertTrue(any("名册为空或重复" in error for error in self.errors()))

    def test_malformed_followup_fails(self):
        for malformed in ({}, [None]):
            with self.subTest(value=malformed):
                package = copy.deepcopy(self.package)
                package["subsequent_migrations"] = malformed
                self.assertTrue(self.errors(package))

    def test_second_followup_uses_previous_output_and_checks_latest_files(self):
        previous = self.package["subsequent_migrations"][-1]["asset_sha256"]
        latest = {name: b"geometry-v3:" + content for name, content in self.assets.items()}
        self.package["subsequent_migrations"].append({
            "before_asset_sha256": dict(previous),
            "asset_sha256": {name: sha256(content) for name, content in latest.items()},
        })
        # 记录新快照但文件尚未迁移，应拒绝；完整写入第三版后才通过。
        self.assertTrue(any("末次快照" in error for error in self.errors()))
        for name, content in latest.items():
            self.write(name, content)
        self.assertEqual(self.errors(), [])
        # 即使最新文件全部匹配，也不能跳过上一版把链直接接回首次数值迁移。
        self.package["subsequent_migrations"][-1]["before_asset_sha256"] = dict(self.package["current_migration"]["asset_sha256"])
        self.assertTrue(any("不连续" in error for error in self.errors()))


if __name__ == "__main__":
    unittest.main()
