import csv
import importlib.util
from pathlib import Path
import tempfile
import types
import unittest

SPEC = importlib.util.spec_from_file_location("fish_migration", Path(__file__).parents[1] / "prepare_fish_runtime_migration.py")
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)


class FishMigrationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def table(self, headers, rows, name="fish.csv"):
        path = self.root / name
        with path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(headers)
            writer.writerows(rows)
        return path

    def test_missing_columns_remain_gaps_and_empty_base_pool(self):
        patch = M.prepare(self.table(["fish_id"], [["Fish_A"]]))
        self.assertIsNone(patch["fish"][0]["ProbeDurationSeconds"])
        self.assertIsNone(patch["fish"][0]["TrueBiteWindowSeconds"])
        self.assertEqual(patch["fish"][0]["TimeOfDay"], [])
        self.assertEqual(patch["base_pool"], [])
        self.assertEqual(len(patch["gaps"]), 5)

    def test_three_windows_are_not_aliased(self):
        patch = M.prepare(self.table(["fish_id", "试探期", "真咬响应窗", "时段", "天气"], [["Fish_A", 6.25, 12, "中午", "晴"]]))
        self.assertEqual(patch["fish"][0]["ProbeDurationSeconds"], 6.25)
        self.assertEqual(patch["fish"][0]["TrueBiteWindowSeconds"], 12)
        self.assertNotIn("PerfectHookWindowSeconds", patch["fish"][0])
        self.assertEqual(patch["fish"][0]["TimeOfDay"], ["Day"])

    def test_invalid_values_do_not_become_missing(self):
        for bad in ("0", "-1", "nan", "inf", "fish"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                M.prepare(self.table(["fish_id", "试探期"], [["Fish_A", bad]]))

    def test_placeholder_pool_is_never_promoted(self):
        fish = self.table(["fish_id"], [["Fish_A"]])
        pool = self.table(["fish_id", "基础池概率（占位）"], [["Fish_A", 1]], "pool.csv")
        with self.assertRaises(ValueError):
            M.prepare(fish, pool)
        pool = self.table(["fish_id", "权重"], [["Fish_A", 2]], "pool.csv")
        self.assertEqual(M.prepare(fish, pool)["base_pool"], [{"FishDefinitionId": "Fish_A", "Probability": 2.0}])
        pool = self.table(["fish_id", "权重"], [["Fish_A", 1], ["Fish_A", 2]], "pool.csv")
        with self.assertRaises(ValueError):
            M.prepare(fish, pool)

    def test_editor_import_preflights_all_identities_and_uses_fish_properties(self):
        writes = []
        asset = types.SimpleNamespace(get_editor_property=lambda field: "Fish_A", set_editor_property=lambda field, value: writes.append((field, value)))
        ue = types.SimpleNamespace(load_asset=lambda path: asset if path.endswith("Fish_A") else None,
                                   CatEnvironmentTimeOfDay=types.SimpleNamespace(DAY="day"), CatEnvironmentWeather=types.SimpleNamespace(CLEAR="clear"))
        patch = M.prepare(self.table(["fish_id", "试探期", "真咬响应窗"], [["Fish_A", 2, 12], ["Fish_B", 3, 15]]))
        with self.assertRaises(ValueError):
            M.import_into_editor_memory(patch, ue)
        self.assertEqual(writes, [])
        patch["fish"].pop()
        self.assertEqual(M.import_into_editor_memory(patch, ue), 1)
        self.assertIn(("probe_duration_seconds", 2.0), writes)
        self.assertIn(("true_bite_window_seconds", 12.0), writes)


if __name__ == "__main__":
    unittest.main()
