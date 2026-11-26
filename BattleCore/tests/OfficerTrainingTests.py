import csv
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import train_officer


class OfficerTrainingTests(unittest.TestCase):
    def test_execution_audit_requires_a_later_sighting_in_the_same_battle(self):
        from run_officer_fourth_round import audit_executed_overrides

        row = {"policy": "trained", "family": "ford", "seed": "7", "group": "10", "wing": "12",
               "selected": "1", "baseline": "support", "chosen": "flank", "tick": "4",
               "previous_task_id": "3", "current_task_id": "8", "order_executing": "0", "order_progress": "0"}
        other_battle = row | {"seed": "8", "tick": "22", "previous_task_id": "8",
                              "baseline": "flank", "order_executing": "1", "order_progress": "1"}
        observed = row | {"tick": "22", "previous_task_id": "8", "baseline": "flank",
                          "order_executing": "1", "order_progress": ".2"}
        unselected = observed | {"selected": "0"}
        self.assertEqual(audit_executed_overrides([row, other_battle, unselected])["observed_executing"], 0)
        audit = audit_executed_overrides([observed, row, other_battle])
        self.assertEqual(audit["dispatched"], 1)
        self.assertEqual(audit["observed_executing"], 1)
        self.assertEqual(audit["observed_with_progress"], 1)

    def test_rare_map_and_baseline_pair_gets_more_weight(self):
        common = ("field", "attack")
        sparse = ("field", "flank")
        weights = train_officer.decision_weights([common, common, common, common, sparse])
        self.assertEqual(weights[:4], [0.5] * 4)
        self.assertEqual(weights[-1], 1.0)

    def test_report_counts_completion_when_both_conditions_hold_in_one_battle(self):
        with TemporaryDirectory() as folder:
            directory = Path(folder)
            fields = ["family", "seed", "policy", *train_officer.METRICS]
            with (directory / "evaluation.csv").open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fields)
                writer.writeheader()
                for seed in (1, 2):
                    for policy in ("fsm", "trained"):
                        row = dict.fromkeys(train_officer.METRICS, 0)
                        row.update(family="field", seed=seed, policy=policy,
                                   primary_cleared=int(seed == 1),
                                   objective_reached=int(seed == (1 if policy == "trained" else 2)))
                        writer.writerow(row)
            (directory / "evaluation-decisions.csv").touch()
            (directory / "group-policy-v2.txt").touch()
            train_officer.summarize(SimpleNamespace(directory=directory))
            data = json.loads((directory / "evaluation-summary.json").read_text(encoding="utf-8"))
            self.assertEqual(data["fsm"]["totals"]["mission_success"], 0)
            self.assertEqual(data["trained"]["totals"]["mission_success"], 1)


    def test_v3_training_reference_uses_keep_when_it_is_legal(self):
        rows = [
            {"action": "support", "baseline": "support", "available_count": "2"},
            {"action": "keep", "baseline": "support", "available_count": "2"},
        ]
        self.assertEqual(train_officer.decision_indices("field", rows, 3), (0, 1))
        rows[1]["action"] = "flank"
        self.assertEqual(train_officer.decision_indices("field", rows, 3), (0, 0))

    def test_v3_rollouts_reject_map_family_shared_between_splits(self):
        features = ["choose_support", *[f"feature_{i}" for i in range(26)],
                    "wing_primary_lateral", "choose_keep", "order_pending",
                    "order_executing", "order_age", "order_progress"]
        fields = ["split", "family", "seed", "tick", "group", "wing", "baseline",
                  "action", "available_count", "behavior", *features, "utility"]
        with TemporaryDirectory() as folder:
            source = Path(folder) / "rollouts.csv"
            with source.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fields)
                writer.writeheader()
                for split, seed in (("train", 1), ("validation", 2)):
                    writer.writerow(dict.fromkeys(features, 0) | {
                        "split": split, "family": "same_map", "seed": seed, "tick": 1,
                        "group": 10, "wing": 12, "baseline": "support", "action": "support",
                        "available_count": 2, "behavior": "held", "utility": 0,
                    })
            with self.assertRaisesRegex(ValueError, "map family"):
                train_officer.read_rollouts([source], 3)

    def test_v3_report_compares_trained_against_held_on_identical_seeds(self):
        with TemporaryDirectory() as folder:
            directory = Path(folder)
            fields = ["split", "family", "seed", "policy", *train_officer.METRICS,
                      "meaningful_order_changes", "early_order_changes", "emergency_order_changes"]
            with (directory / "evaluation.csv").open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fields)
                writer.writeheader()
                for seed in (1, 2):
                    for policy in ("fsm", "held", "trained"):
                        row = dict.fromkeys(fields, 0)
                        row.update(split="validation", family="field", seed=seed, policy=policy,
                                   utility={"fsm": 0.1, "held": 0.4, "trained": 0.2}[policy],
                                   primary_cleared=1, objective_reached=int(policy != "fsm"),
                                   early_order_changes=int(policy == "fsm"))
                        writer.writerow(row)
            (directory / "evaluation-decisions.csv").touch()
            (directory / "group-policy-v3.txt").touch()
            train_officer.summarize(SimpleNamespace(directory=directory, schema=3))
            data = json.loads((directory / "evaluation-summary.json").read_text(encoding="utf-8"))
            self.assertEqual(data["pairs"], 2)
            self.assertEqual(data["held"]["totals"]["mission_success"], 2)
            self.assertEqual(data["fsm"]["totals"]["early_order_changes"], 2)
            self.assertAlmostEqual(data["comparisons"]["trained_vs_held"]["mean_paired_utility_delta"],
                                   -0.2)
            self.assertEqual(data["comparisons"]["trained_vs_held"]["lower_utility"], 2)


if __name__ == "__main__":
    unittest.main()
