"""Run the fourth paired BattleCore experiment and record the release gate."""

import argparse
from collections import Counter
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
from statistics import mean
import subprocess
import sys
from time import perf_counter

from train_officer import digest, write_json


WEIGHTS = "group-policy-v3.txt"
FIT_TOOL = Path(__file__).with_name("train_officer.py")


def guardrails(report):
    reference = report["held"]["totals"]
    candidate = report["trained"]["totals"]
    comparison = report["comparisons"]["trained_vs_held"]
    return {
        "model_changes_decisions": candidate["policy_overrides"] > 0,
        "positive_utility": comparison["mean_paired_utility_delta"] > 1e-7,
        "no_fewer_completed_tasks": candidate["mission_success"] >= reference["mission_success"],
        "no_more_friendly_losses": candidate["blue_lost"] <= reference["blue_lost"],
        "no_more_premature_replacements": candidate["early_order_changes"] <= reference["early_order_changes"],
    }


def mean_without_one_map(report):
    deltas = {
        name: data["comparisons"]["trained_vs_held"]["mean_paired_utility_delta"]
        for name, data in report["families"].items()
    }
    return {name: mean(delta for other, delta in deltas.items() if name != other) for name in deltas}


def write_margin(source, destination, margin):
    header, *parameters = source.read_text(encoding="ascii").splitlines()
    parts = header.split()
    if parts[:4] != ["BATTLE_GROUP_MLP", "3", "33", "24"] or len(parts) != 5:
        raise ValueError(f"Unexpected weight schema in {source}")
    destination.write_text("\n".join([" ".join(parts[:4] + [str(margin)]), *parameters]) + "\n",
                           encoding="ascii")


def load_report(directory, seeds, split):
    report = json.loads((directory / "evaluation-summary.json").read_text(encoding="utf-8"))
    if (report["split"] != split or report["pairs"] != 4 * seeds or len(report["families"]) != 4
            or any(data["pairs"] != seeds for data in report["families"].values())):
        raise ValueError(f"Invalid {split} groups in {directory}")
    for key, file in (
        ("model_sha256", WEIGHTS),
        ("evaluation_sha256", "evaluation.csv"),
        ("decision_trace_sha256", "evaluation-decisions.csv"),
    ):
        if report[key] != digest(directory / file):
            raise ValueError(f"Artifact digest differs in {directory / file}")
    return report


def read_rows(path):
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def audit_executed_overrides(rows):
    """Count newly selected tasks later seen executing in the same battle."""
    selections = sorted((row for row in rows if row["policy"] == "trained" and row["selected"] == "1"),
                        key=lambda row: int(row["tick"]))
    overrides, executing, moving = {}, set(), set()
    for row in selections:
        trial = tuple(row[key] for key in ("family", "seed", "policy", "group", "wing"))
        previous = (*trial, row["previous_task_id"])
        tick = int(row["tick"])
        if previous in overrides and tick > overrides[previous] and float(row["order_executing"]) == 1.0:
            executing.add(previous)
            if float(row["order_progress"]) >= .15:
                moving.add(previous)
        new_id = row["current_task_id"]
        if row["chosen"] not in (row["baseline"], "keep") and new_id not in ("0", row["previous_task_id"]):
            overrides[(*trial, new_id)] = tick
    return {
        "dispatched": len(overrides),
        "observed_executing": len(executing),
        "observed_with_progress": len(moving),
        "observed_executing_by_family": dict(sorted(Counter(key[0] for key in executing).items())),
    }


def experiment(args):
    trainer = args.trainer.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True)
    binary_digest = digest(trainer)

    def run(command):
        command = [str(part) for part in command]
        started = perf_counter()
        print(f"Running {command[0]} {command[1]} with {command[2]}", flush=True)
        result = subprocess.run(command, check=False)
        with (output / "commands.jsonl").open("a", encoding="utf-8") as log:
            log.write(json.dumps({
                "argv": command, "seconds": round(perf_counter() - started, 3),
                "exit_code": result.returncode,
            }) + "\n")
        result.check_returncode()

    def simulate(mode, directory, seeds, *model):
        run([trainer, f"{mode}-historical-v4", directory, seeds, args.workers, *model])

    def fit(directory, epochs, *extra):
        run([sys.executable, FIT_TOOL, "fit", directory, "--schema", 3,
             "--device", "cuda", "--epochs", epochs, "--balance", *extra])

    def repeat_fit(directory, epochs, *extra):
        repeat = directory.with_name(directory.name + "-repeat")
        repeat.mkdir()
        shutil.copyfile(directory / "rollouts.csv", repeat / "rollouts.csv")
        fit(repeat, epochs, *extra)
        return digest(directory / WEIGHTS) == digest(repeat / WEIGHTS)

    def evaluate(mode, directory, seeds, split):
        simulate(mode, directory, seeds, directory / WEIGHTS)
        run([sys.executable, FIT_TOOL, "summarize", directory, "--schema", 3])
        return load_report(directory, seeds, split)

    initial = output / "initial"
    simulate("collect", initial, args.train_seeds)
    fit(initial, 500)
    initial_reproduced = repeat_fit(initial, 500)

    visits = output / "model-visits"
    simulate("collect", visits, args.visit_seeds, initial / WEIGHTS)
    refined = output / "refined"
    refined.mkdir()
    shutil.copyfile(initial / "rollouts.csv", refined / "rollouts.csv")
    refinement = ("--extra-rollouts", visits / "rollouts.csv", "--initial-weights", initial / WEIGHTS)
    fit(refined, 350, *refinement)
    refined_reproduced = repeat_fit(refined, 350, *refinement)
    same_weights = initial_reproduced and refined_reproduced

    candidates = []
    validation_baselines = None
    for variant in (initial, refined):
        for margin in (0.0, 0.01):
            directory = output / f"validation-{variant.name}-{margin:g}"
            directory.mkdir()
            write_margin(variant / WEIGHTS, directory / WEIGHTS, margin)
            report = evaluate("validate", directory, args.validation_seeds, "validation")
            baselines = [row for row in read_rows(directory / "evaluation.csv") if row["policy"] != "trained"]
            if validation_baselines is not None and baselines != validation_baselines:
                raise ValueError("Validation candidates did not share identical start states and rule baselines")
            validation_baselines = baselines
            checks = guardrails(report)
            candidates.append({
                "name": directory.name,
                "weights_sha256": report["model_sha256"],
                "validation_sha256": report["evaluation_sha256"],
                "validation_summary_sha256": digest(directory / "evaluation-summary.json"),
                "checks": checks,
                "eligible": all(checks.values()),
                "mean_delta": report["comparisons"]["trained_vs_held"]["mean_paired_utility_delta"],
            })
    selected = max(candidates, key=lambda item: (
        item["eligible"], item["checks"]["model_changes_decisions"], item["mean_delta"]))

    development = [*read_rows(initial / "rollouts.csv"), *read_rows(visits / "rollouts.csv")]
    final = output / "holdout"
    final.mkdir()
    shutil.copyfile(output / selected["name"] / WEIGHTS, final / WEIGHTS)
    frozen = {
        "frozen_utc": datetime.now(timezone.utc).isoformat(),
        "trainer_sha256": binary_digest,
        "fit_tool_sha256": digest(FIT_TOOL),
        "initial_fit_reproduced": initial_reproduced,
        "refined_fit_reproduced": refined_reproduced,
        "holdout_seeds_per_family": args.holdout_seeds,
        "candidate": selected,
        "validation_candidates": candidates,
        "gate": "Eligible validation, matching CUDA repeat, positive paired 95% lower bound and each "
                "leave-one-map-out mean, no decline in completed tasks, losses or premature replacements, "
                "and at least one new override ID later observed executing in the same holdout battle.",
    }
    freeze_path = final / "freeze.json"
    write_json(freeze_path, frozen)
    if digest(trainer) != binary_digest or digest(final / WEIGHTS) != selected["weights_sha256"]:
        raise ValueError("Trainer or candidate changed before holdout")

    report = evaluate("evaluate", final, args.holdout_seeds, "holdout")
    if report["model_sha256"] != selected["weights_sha256"] or digest(trainer) != binary_digest:
        raise ValueError("The frozen build or weights changed during holdout")
    withheld = read_rows(final / "evaluation.csv")
    for key in ("family", "seed"):
        if {row[key] for row in development} & {row[key] for row in withheld}:
            raise ValueError(f"Holdout overlaps the development split at {key}")
    comparison = report["comparisons"]["trained_vs_held"]
    omissions = mean_without_one_map(report)
    execution = audit_executed_overrides(read_rows(final / "evaluation-decisions.csv"))
    checks = {
        **guardrails(report),
        "eligible_validation": selected["eligible"],
        "reproduced_cuda_fit": same_weights,
        "positive_paired_interval": comparison["paired_bootstrap_95_percent_interval"][0] > 0,
        "positive_without_any_one_map": all(value > 1e-7 for value in omissions.values()),
        "observed_execution_of_overridden_task": execution["observed_executing"] > 0,
    }
    decision = {
        "qualified_for_game": all(checks.values()),
        "checks": checks,
        "leave_one_map_out_utility_deltas": omissions,
        "execution_audit": execution,
        "freeze_sha256": digest(freeze_path),
        "evaluation_sha256": report["evaluation_sha256"],
    }
    write_json(final / "verification.json", decision)
    print(json.dumps(decision, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trainer", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--workers", type=int, choices=range(1, 33), default=8)
    parser.add_argument("--train-seeds", type=int, choices=range(1, 501), default=48, metavar="N")
    parser.add_argument("--visit-seeds", type=int, choices=range(1, 501), default=32, metavar="N")
    parser.add_argument("--validation-seeds", type=int, choices=range(1, 501), default=32, metavar="N")
    parser.add_argument("--holdout-seeds", type=int, choices=range(1, 501), default=80, metavar="N")
    experiment(parser.parse_args())


if __name__ == "__main__":
    main()
