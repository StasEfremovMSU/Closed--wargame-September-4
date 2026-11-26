"""Fit and summarize the isolated BattleCore second-regiment scoring policy."""

import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import json
from math import isfinite, sqrt
import os
from pathlib import Path
import random
from statistics import mean


SCHEMA = 2
FEATURE_COUNT = 28
COMMITMENT_SCHEMA = 3
COMMITMENT_FEATURES = ("choose_keep", "order_pending", "order_executing",
                       "order_age", "order_progress")
HIDDEN = 24
SEED = 27182


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def decision_weights(strata):
    counts = Counter(strata)
    return [1 / sqrt(counts[stratum]) for stratum in strata]


def read_rollouts(sources, schema):
    groups = defaultdict(list)
    families = defaultdict(set)
    feature_names = None
    commitment = schema == COMMITMENT_SCHEMA
    for source_index, source in enumerate(sources):
        with source.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            names = reader.fieldnames or []
            try:
                features = names[names.index("choose_support"):names.index("utility")]
            except ValueError as error:
                raise ValueError(f"missing feature schema in {source}") from error
            if (len(features) != FEATURE_COUNT + (len(COMMITMENT_FEATURES) if commitment else 0)
                    or features[FEATURE_COUNT - 1] != "wing_primary_lateral"
                    or (commitment and tuple(features[FEATURE_COUNT:]) != COMMITMENT_FEATURES)
                    or (feature_names is not None and features != feature_names)):
                raise ValueError(f"unexpected local feature schema in {source}")
            feature_names = features
            for row in reader:
                split = row["split"]
                if split not in {"train", "validation"}:
                    raise ValueError("training input admits only train and validation scenario seeds")
                behavior = row.get("behavior")
                if commitment and (behavior not in {"held", "trained"}
                                    or (behavior == "trained" and split != "train")
                                    or (source_index and behavior != "trained")):
                    raise ValueError("model-visited observations may enter only the training split")
                key = (split, row["family"], row["seed"], row["tick"], row["group"], row["wing"])
                families[split].add(row["family"])
                groups[key].append(row)
    if commitment and families["train"] & families["validation"]:
        raise ValueError("V3 map family appears in both train and validation")
    return feature_names, groups


def decision_indices(key, rows, schema):
    actions = [row["action"] for row in rows]
    baseline_name = rows[0]["baseline"]
    if (len(rows) < 2 or len(set(actions)) != len(rows) or baseline_name not in actions
            or any(int(row["available_count"]) != len(rows)
                   or row["baseline"] != baseline_name for row in rows)):
        raise ValueError(f"incomplete decision {key}")
    baseline = actions.index(baseline_name)
    reference = actions.index("keep") if schema == COMMITMENT_SCHEMA and "keep" in actions else baseline
    return baseline, reference


def initialize_weights(model, source, schema, feature_count, tensor):
    header, *body = source.read_text(encoding="ascii").splitlines()
    fields = header.split()
    if len(fields) != 5 or fields[:4] != ["BATTLE_GROUP_MLP", str(schema), str(feature_count), str(HIDDEN)]:
        raise ValueError("initial weights do not match the feature schema")
    parameters = list(model.parameters())
    values = [float(value) for line in body for value in line.split()]
    if len(values) != sum(parameter.numel() for parameter in parameters) or any(
            not isfinite(value) or abs(value) > 256 for value in values):
        raise ValueError("invalid initial weights")
    offset = 0
    for parameter in parameters:
        end = offset + parameter.numel()
        parameter.copy_(tensor(values[offset:end]).reshape(parameter.shape))
        offset = end


def fit(args):
    os.environ["CUBLAS_WORKSPACE_CONFIG"] = ":4096:8"
    import torch
    from torch import nn

    if args.device == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA requested and unavailable. Select --device cpu to fit on CPU.")
    if args.epochs < 1:
        raise ValueError("epochs must be positive")
    if args.margin is not None and not 0 <= args.margin <= 1:
        raise ValueError("native baseline margin must be between 0 and 1")
    device = torch.device(args.device)
    torch.manual_seed(SEED)
    torch.use_deterministic_algorithms(True)
    directory = args.directory
    schema = args.schema
    if schema == SCHEMA and (args.extra_rollouts or args.initial_weights):
        raise ValueError("extra training observations and initialization require feature schema 3")
    sources = [directory / "rollouts.csv", *args.extra_rollouts]
    feature_names, groups = read_rollouts(sources, schema)
    feature_count = len(feature_names)

    def tensor(values, dtype=torch.float32):
        return torch.tensor(values, dtype=dtype, device=device)

    def samples(split):
        features, outcomes, reference_indices, decisions, strata = [], [], [], [], []
        for key, rows in groups.items():
            if key[0] != split:
                continue
            baseline, reference = decision_indices(key, rows, schema)
            first = len(features)
            index = first + reference
            decisions.append((first, len(rows), first + baseline, index))
            stratum = (key[1], rows[0]["baseline"])
            if schema == COMMITMENT_SCHEMA:
                stratum = (*stratum, rows[0]["behavior"], rows[reference]["action"])
            strata.append(stratum)
            for row in rows:
                features.append([float(row[name]) for name in feature_names])
                outcomes.append(float(row["utility"]))
                reference_indices.append(index)
        if not decisions:
            raise ValueError(f"no {split} observations")
        return tensor(features), tensor(outcomes), tensor(reference_indices, torch.long), decisions, strata

    train_x, train_y, train_reference, train_decisions, train_strata = samples("train")
    val_x, val_y, val_reference, val_decisions, _ = samples("validation")
    model = nn.Sequential(nn.Linear(feature_count, HIDDEN), nn.ReLU(), nn.Linear(HIDDEN, 1)).to(device)
    if args.initial_weights:
        with torch.no_grad():
            initialize_weights(model, args.initial_weights, schema, feature_count, tensor)
    optimizer = torch.optim.AdamW(model.parameters(),
                                  lr=0.002 if args.initial_weights else 0.008, weight_decay=0.001)

    objective_weights = None
    if args.balance:
        row_weights = [0.] * len(train_x)
        for (start, count, _, reference), weight in zip(train_decisions, decision_weights(train_strata)):
            for index in range(start, start + count):
                if index != reference:
                    row_weights[index] = weight / (count - 1)
        objective_weights = tensor(row_weights)

    def loss(features, outcomes, reference, weights=None):
        scores = model(features).squeeze(1)
        predicted = scores - scores[reference]
        actual = outcomes - outcomes[reference]
        if weights is None:
            return nn.functional.mse_loss(predicted, actual)
        return ((predicted - actual).square() * weights).sum() / weights.sum()

    with torch.no_grad():
        start_loss = loss(train_x, train_y, train_reference).item()
    best_loss = float("inf")
    best_epoch = 0
    for epoch in range(1, args.epochs + 1):
        optimizer.zero_grad(set_to_none=True)
        objective = loss(train_x, train_y, train_reference, objective_weights)
        objective.backward()
        optimizer.step()
        with torch.no_grad():
            validation = loss(val_x, val_y, val_reference).item()
            if validation < best_loss:
                best_loss = validation
                best_epoch = epoch
                checkpoint = {name: parameter.detach().clone()
                              for name, parameter in model.state_dict().items()}
    model.load_state_dict(checkpoint)
    with torch.no_grad():
        predicted = model(val_x).squeeze(1).cpu().tolist()
        selected_train_loss = loss(train_x, train_y, train_reference).item()
    observed = val_y.cpu().tolist()
    margins = (0.0, .01, .025, .05, .075, .10, .15, .20)

    def validate_margin(margin):
        gains, overrides = [], 0
        for start, count, baseline, _ in val_decisions:
            chosen = baseline
            best_score = predicted[baseline] + margin
            for candidate in range(start, start + count):
                if predicted[candidate] > best_score:
                    chosen = candidate
                    best_score = predicted[candidate]
            overrides += chosen != baseline
            gains.append(observed[chosen] - observed[baseline])
        return {"margin": margin, "mean_gain": mean(gains), "overrides": overrides}

    trial_margins = [validate_margin(margin) for margin in margins]
    calibrated_margin = max(trial_margins, key=lambda trial: (trial["mean_gain"], -trial["overrides"]))["margin"]
    best_margin = calibrated_margin if args.margin is None else args.margin
    weights = directory / f"group-policy-v{schema}.txt"
    with weights.open("w", encoding="ascii", newline="\n") as stream:
        stream.write(f"BATTLE_GROUP_MLP {schema} {feature_count} {HIDDEN} {best_margin:.9g}\n")
        for parameter in (model[0].weight, model[0].bias, model[2].weight, model[2].bias):
            numbers = parameter.detach().cpu().flatten().tolist()
            stream.write(" ".join(f"{value:.9g}" for value in numbers) + "\n")

    meta = {
        "feature_schema": schema,
        "features": feature_names,
        "architecture": [feature_count, HIDDEN, 1],
        "parameters": sum(p.numel() for p in model.parameters()),
        "training_seed": SEED,
        "torch_version": str(torch.__version__),
        "cuda_version": torch.version.cuda,
        "device": str(device),
        "device_name": torch.cuda.get_device_name(device) if device.type == "cuda" else "CPU",
        "epochs": args.epochs,
        "decision_balance": args.balance,
        "validation_calibrated_margin": calibrated_margin,
        "selected_epoch": best_epoch,
        "train_decisions": len(train_decisions),
        "validation_decisions": len(val_decisions),
        "train_rows": len(train_x),
        "validation_rows": len(val_x),
        "train_pairwise_mse_initial": start_loss,
        "train_pairwise_mse_selected": selected_train_loss,
        "validation_pairwise_mse_selected": best_loss,
        "validation_margins": trial_margins,
        "selected_margin": best_margin,
        "data_sha256": digest(sources[0]),
        "model_sha256": digest(weights),
    }
    if schema == COMMITMENT_SCHEMA:
        meta["reference"] = "keep_when_legal_else_rule_baseline"
        meta["training_source_files"] = [{"path": str(source), "sha256": digest(source)}
                                         for source in sources]
        meta["training_visits_by_behavior"] = dict(Counter(
            rows[0]["behavior"] for key, rows in groups.items() if key[0] == "train"))
        if args.initial_weights:
            meta["initial_weights_sha256"] = digest(args.initial_weights)
    write_json(directory / "training.json", meta)
    print(f"{meta['device_name']}: {len(train_x)} train / {len(val_x)} validation rows. "
          f"Selected epoch {best_epoch}, baseline margin {best_margin:g}, "
          f"validation MSE {best_loss:.5f}.")


METRICS = (
    "utility", "primary_cleared", "objective_reached", "preserved", "victory",
    "primary_lost", "blue_lost", "wing_lost", "red_lost",
    "orders_issued", "order_changes", "decisions", "policy_overrides",
)


COMMITMENT_METRICS = ("meaningful_order_changes", "early_order_changes", "emergency_order_changes")


def summarize(args):
    schema = getattr(args, "schema", SCHEMA)
    commitment = schema == COMMITMENT_SCHEMA
    policy_names = ("fsm", "held", "trained") if commitment else ("fsm", "trained")
    directory = args.directory
    source = directory / "evaluation.csv"
    pairs = defaultdict(dict)
    splits = set()
    with source.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            if commitment:
                splits.add(row["split"])
            policies = pairs[row["family"], row["seed"]]
            if row["policy"] in policies:
                raise ValueError("duplicated evaluation seed/policy")
            row["mission_success"] = int(row["primary_cleared"] == "1" and row["objective_reached"] == "1")
            policies[row["policy"]] = row
    if not pairs or any(set(pair) != set(policy_names) for pair in pairs.values()):
        raise ValueError("evaluation contains incomplete policy pairs")
    if commitment and (len(splits) != 1 or splits - {"validation", "holdout"}):
        raise ValueError("V3 evaluation must contain one validation or holdout split")

    def compare(items, left, right):
        deltas = [float(pair[left]["utility"]) - float(pair[right]["utility"]) for pair in items]
        rng = random.Random(SEED)
        boot = sorted(mean(rng.choices(deltas, k=len(deltas))) for _ in range(4000))
        return {
            "mean_paired_utility_delta": mean(deltas),
            "paired_bootstrap_95_percent_interval": [boot[100], boot[3899]],
            "higher_utility": sum(delta > 1e-7 for delta in deltas),
            "lower_utility": sum(delta < -1e-7 for delta in deltas),
            "ties": sum(abs(delta) <= 1e-7 for delta in deltas),
        }

    def aggregate(items):
        data = {}
        metrics = (*METRICS, "mission_success", *(COMMITMENT_METRICS if commitment else ()))
        for policy in policy_names:
            data[policy] = {metric: mean(float(pair[policy][metric]) for pair in items)
                            for metric in metrics}
            data[policy]["totals"] = {
                metric: sum(int(pair[policy][metric]) for pair in items)
                for metric in metrics if metric != "utility"
            }
        result = {"pairs": len(items), **data}
        baseline_comparison = compare(items, "trained", "fsm")
        if commitment:
            result["comparisons"] = {
                "held_vs_fsm": compare(items, "held", "fsm"),
                "trained_vs_held": compare(items, "trained", "held"),
                "trained_vs_fsm": baseline_comparison,
            }
        else:
            result.update(
                mean_paired_utility_delta=baseline_comparison["mean_paired_utility_delta"],
                paired_bootstrap_95_percent_interval=baseline_comparison["paired_bootstrap_95_percent_interval"],
                trained_higher_utility=baseline_comparison["higher_utility"],
                trained_lower_utility=baseline_comparison["lower_utility"],
                ties=baseline_comparison["ties"],
            )
        return result

    result = aggregate(list(pairs.values()))
    result["families"] = {
        family: aggregate([pair for (key, _), pair in pairs.items() if key == family])
        for family in sorted({family for family, _ in pairs})
    }
    result["evaluation_sha256"] = digest(source)
    result["decision_trace_sha256"] = digest(directory / "evaluation-decisions.csv")
    result["model_sha256"] = digest(directory / f"group-policy-v{schema}.txt")
    if commitment:
        result["split"] = splits.pop()
    write_json(directory / "evaluation-summary.json", result)
    if commitment:
        comparison = result["comparisons"]["trained_vs_held"]
        print(f"{result['pairs']} matched starts, trained vs held utility delta "
              f"{comparison['mean_paired_utility_delta']:+.4f}, "
              f"{comparison['higher_utility']} higher / {comparison['lower_utility']} lower / "
              f"{comparison['ties']} tied.")
    else:
        print(f"{result['pairs']} pairs, utility delta {result['mean_paired_utility_delta']:+.4f}, "
              f"{result['trained_higher_utility']} higher / {result['trained_lower_utility']} lower / "
              f"{result['ties']} tied.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)
    fit_parser = modes.add_parser("fit")
    fit_parser.add_argument("directory", type=Path)
    fit_parser.add_argument("--device", choices=("cpu", "cuda"), default="cpu")
    fit_parser.add_argument("--epochs", type=int, default=500)
    fit_parser.add_argument("--balance", action="store_true",
                            help="rebalance decisions by scenario family and baseline action")
    fit_parser.add_argument("--margin", type=float,
                            help="override the native baseline margin for paired validation")
    fit_parser.add_argument("--extra-rollouts", type=Path, action="append", default=[],
                            help="add V3 train-only model-visited rollout CSVs")
    fit_parser.add_argument("--initial-weights", type=Path,
                            help="warm start V3 fitting from existing portable weights")
    report_parser = modes.add_parser("summarize")
    report_parser.add_argument("directory", type=Path)
    for mode in (fit_parser, report_parser):
        mode.add_argument("--schema", type=int, choices=(SCHEMA, COMMITMENT_SCHEMA), default=SCHEMA)
    args = parser.parse_args()
    (fit if args.mode == "fit" else summarize)(args)


if __name__ == "__main__":
    main()
