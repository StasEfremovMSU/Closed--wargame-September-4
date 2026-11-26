"""Collect progressively larger BattleCore batches and train and evaluate an officer policy."""
import argparse
import ctypes
import json
from pathlib import Path
import shutil
from statistics import mean
import subprocess
import sys
import time

from train_officer import digest, write_json


WEIGHTS = "group-policy-v3.txt"
FIT_TOOL = Path(__file__).with_name("train_officer.py").resolve()
VALIDATION_BLOCK = 901
HOLDOUT_BLOCK = 902


def parse_report(directory, split, seeds, families):
    report = json.loads((directory / "evaluation-summary.json").read_text(encoding="utf-8"))
    if (report["split"] != split or len(report["families"]) != families
        or report["pairs"] != families * seeds
        or any(family["pairs"] != seeds for family in report["families"].values())
        or report["model_sha256"] != digest(directory / WEIGHTS)
        or report["evaluation_sha256"] != digest(directory / "evaluation.csv")):
        raise ValueError(f"Invalid {split} evaluation in {directory}")
    return report


def metrics(report):
    held, trained = (report[name]["totals"] for name in ("held", "trained"))
    comparison = report["comparisons"]["trained_vs_held"]
    changes = {key: trained[key] - held[key]
               for key in ("mission_success", "blue_lost", "early_order_changes", "policy_overrides")}
    checks = {
        "changes_decisions": changes["policy_overrides"] > 0,
        "positive_paired_utility": comparison["mean_paired_utility_delta"] > 0,
        "no_fewer_completed_tasks": changes["mission_success"] >= 0,
        "no_more_friendly_losses": changes["blue_lost"] <= 0,
        "no_more_premature_replacements": changes["early_order_changes"] <= 0,
    }
    return {"checks": checks, "deltas": changes, "comparison": comparison}


class NightRun:
    def __init__(self, args):
        self.args = args
        self.output = args.output.resolve()
        self.output.mkdir(parents=True)
        self.trainer = args.trainer.resolve(strict=True)
        self.initial_weights = args.initial_weights.resolve(strict=True) if args.initial_weights else None
        self.started = time.monotonic()
        self.deadline = self.started + args.hours * 3600
        self.binary_digest = digest(self.trainer)
        self.stages = 0

    def status(self, state, phase, **facts):
        target = self.output / "status.json"
        temporary = self.output / "status.tmp"
        write_json(temporary, {"state": state, "phase": phase, "completed_stages": self.stages,
                               "elapsed_seconds": round(time.monotonic() - self.started, 1),
                               "trainer_sha256": self.binary_digest, **facts})
        temporary.replace(target)

    def run(self, phase, *arguments):
        command = [str(argument) for argument in arguments]
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("The night budget has ended")
        self.status("running", phase)
        then = time.monotonic()
        print(phase, flush=True)
        run = subprocess.run(command, timeout=remaining, check=False,
                             creationflags=getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0))
        with (self.output / "commands.jsonl").open("a", encoding="utf-8") as journal:
            journal.write(json.dumps({"argv": command, "phase": phase, "exit_code": run.returncode,
                                      "seconds": round(time.monotonic() - then, 2)}) + "\n")
        run.check_returncode()
        self.stages += 1

    def simulate(self, mode, target, count, block, weights=None):
        arguments = [self.trainer, f"{mode}-historical-v5", target, count, self.args.workers]
        if weights is not None:
            arguments.append(weights)
        self.run(f"{mode} {target.name}", *arguments, f"--seed-block={block}")

    def fit(self, name, sources, initial=None):
        target = self.output / name
        target.mkdir()
        shutil.copyfile(sources[0], target / "rollouts.csv")
        extra = [argument for source in sources[1:] for argument in ("--extra-rollouts", source)]
        if initial is not None:
            extra.extend(("--initial-weights", initial))
        self.run(f"CUDA fit {name}", sys.executable, FIT_TOOL, "fit", target,
                 "--device", "cuda", "--schema", 3, "--epochs", self.args.epochs, "--balance", *extra)
        return target

    def evaluate(self, mode, target, candidate, count, block, families):
        target.mkdir()
        weights = target / WEIGHTS
        shutil.copyfile(candidate / WEIGHTS, weights)
        self.simulate(mode, target, count, block, weights)
        self.run(f"summarize {target.name}", sys.executable, FIT_TOOL, "summarize", target, "--schema", 3)
        split = "holdout" if mode == "evaluate" else "validation"
        return parse_report(target, split, count, families)

    def train(self):
        args = self.args
        data, candidates, warm_start = [], [], self.initial_weights
        holdout_window = max(55., args.holdout_seeds * 2.5)
        previous_cycle = 0.
        for cycle in range(1, args.max_cycles + 1):
            if cycle > 1 and self.deadline - time.monotonic() < holdout_window + 1.25 * previous_cycle:
                break
            then = time.monotonic()
            batch = self.output / f"exploration-{cycle:02d}"
            self.simulate("collect", batch, args.train_seeds, cycle)
            data.append(batch / "rollouts.csv")
            if warm_start is None:
                warm_start = self.fit(f"initial-{cycle:02d}", data) / WEIGHTS
            visits = self.output / f"policy-visits-{cycle:02d}"
            self.simulate("collect", visits, args.visit_seeds, cycle, warm_start)
            data.append(visits / "rollouts.csv")

            variants = (
                self.fit(f"warm-{cycle:02d}", data, warm_start),
                self.fit(f"cold-{cycle:02d}", data),
            )
            for variant in variants:
                counts = args.validation_seeds
                report = self.evaluate("validate", self.output / f"validation-{variant.name}", variant,
                                       counts, VALIDATION_BLOCK, 3)
                result = {"candidate": variant.name, **metrics(report)}
                candidates.append((variant, result))
            chosen, _ = max(candidates, key=lambda item: (
                all(item[1]["checks"].values()),
                item[1]["checks"]["changes_decisions"],
                item[1]["comparison"]["mean_paired_utility_delta"],
            ))
            warm_start = chosen / WEIGHTS
            write_json(self.output / "validation.json", [result for _, result in candidates])
            self.status("running", "cycle complete", cycle=cycle, leading_candidate=chosen.name)
            previous_cycle = time.monotonic() - then

        chosen, validation = next((candidate, result) for candidate, result in candidates
                                  if candidate / WEIGHTS == warm_start)
        frozen = {"candidate": chosen.name, "weights_sha256": digest(warm_start),
                  "trainer_sha256": self.binary_digest, "validation": validation,
                  "fit_tool_sha256": digest(FIT_TOOL),
                  "holdout_seed_block": HOLDOUT_BLOCK, "holdout_seeds_per_family": args.holdout_seeds}
        write_json(self.output / "frozen.json", frozen)
        final = self.evaluate("evaluate", self.output / "holdout", chosen, args.holdout_seeds, HOLDOUT_BLOCK, 4)
        if final["model_sha256"] != frozen["weights_sha256"] or digest(self.trainer) != self.binary_digest:
            raise ValueError("Candidate or trainer changed after selection")
        result = metrics(final)
        by_family = {family: info["comparisons"]["trained_vs_held"]["mean_paired_utility_delta"]
                     for family, info in final["families"].items()}
        result["checks"].update(
            eligible_validation=all(validation["checks"].values()),
            positive_interval=result["comparison"]["paired_bootstrap_95_percent_interval"][0] > 0,
            positive_without_each_map=all(mean(delta for name, delta in by_family.items() if name != omitted) > 0
                                          for omitted in by_family),
        )
        result.update(pairs=final["pairs"], by_family=by_family, qualified=all(result["checks"].values()))
        write_json(self.output / "result.json", result)
        self.status("complete", "holdout", candidate=chosen.name, qualified=result["qualified"])
        print(json.dumps(result, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trainer", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--initial-weights", type=Path)
    parser.add_argument("--hours", type=float, default=6)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--train-seeds", type=int, default=420)
    parser.add_argument("--visit-seeds", type=int, default=300)
    parser.add_argument("--validation-seeds", type=int, default=112)
    parser.add_argument("--holdout-seeds", type=int, default=260)
    parser.add_argument("--epochs", type=int, default=1000)
    parser.add_argument("--max-cycles", type=int, default=12)
    args = parser.parse_args()
    if (not 0 < args.hours <= 12 or not 1 <= args.workers <= 32 or not 1 <= args.max_cycles <= 30
        or not 1 <= args.epochs or any(not 1 <= count <= 500 for count in
        (args.train_seeds, args.visit_seeds, args.validation_seeds, args.holdout_seeds))):
        parser.error("Invalid time, seed, worker, cycle, or epoch count")
    run = NightRun(args)
    keep_awake = ctypes.windll.kernel32.SetThreadExecutionState if sys.platform == "win32" else None
    if keep_awake:
        keep_awake(0x80000001)  # ES_CONTINUOUS | ES_SYSTEM_REQUIRED, for this process only.
    try:
        run.train()
    except Exception as error:
        run.status("failed", "night run", error=str(error))
        raise
    finally:
        if keep_awake:
            keep_awake(0x80000000)


if __name__ == "__main__":
    main()
