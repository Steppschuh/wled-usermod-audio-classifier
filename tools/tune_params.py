#!/usr/bin/env python3
# Copyright (c) 2026 Stephan Schultz
# SPDX-License-Identifier: EUPL-1.2
"""
WLED Audio Classifier - Closed-Loop Parameter Optimizer

Performs coordinate search + random local perturbations over ClassifierConfig
parameters on the "train" fixture split using the native C++ eval_cli binary,
rejects candidates with assertion failures or micro-flips, and validates the best
candidate on the held-out "eval" split (if present).
"""

import argparse
import json
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import evaluate  # noqa: E402

DEFAULT_SEARCH_SPACE = {
    "relChorusLoudRatio": (1.25, 1.65, 0.05, "float"),
    "relChorusPeakRatio": (0.86, 0.98, 0.02, "float"),
    "relChorusDeltaFloor": (4.0, 12.0, 1.0, "float"),
    "relChorusBassTrend": (0.85, 1.25, 0.05, "float"),
    "slopeBuildupCentroid": (0.45, 1.00, 0.05, "float"),
    "slopeBuildupHighToBass": (0.15, 0.50, 0.05, "float"),
    "slopeBuildupHighFlux": (0.20, 0.60, 0.05, "float"),
    "phraseDwellMinMs": (1500, 3500, 250, "int"),
    "outroRecoveryMedRatio": (0.55, 0.85, 0.05, "float"),
}

DEFAULT_CONFIG = {
    "relChorusLoudRatio": 1.45,
    "relChorusPeakRatio": 0.94,
    "relChorusDeltaFloor": 8.0,
    "relChorusBassTrend": 1.05,
    "slopeBuildupCentroid": 0.70,
    "slopeBuildupHighToBass": 0.30,
    "slopeBuildupHighFlux": 0.35,
    "phraseDwellMinMs": 2000,
    "outroRecoveryMedRatio": 0.70,
}


def objective(report: dict) -> float:
    if report["total_failed_assertions"] > 0:
        return -1000.0 - float(report["total_failed_assertions"] * 100)
    penalty = (report["total_micro_flips"] * 3.0) + (report["total_ping_pongs"] * 3.0)
    return float(report["overall_score"]) - penalty


def eval_config(cfg: dict, split: str, fixtures_dir: Path) -> dict:
    return evaluate.evaluate_suite(
        fixtures_root=fixtures_dir,
        split=split,
        config_json=json.dumps(cfg),
        trace=False,
    )


def main():
    parser = argparse.ArgumentParser(description="Closed-loop ClassifierConfig parameter tuner")
    parser.add_argument("--fixtures-dir", default=str(evaluate.DEFAULT_FIXTURES_DIR), help="Fixtures directory")
    parser.add_argument("--train-split", default="train", help="Split used for optimization (default: train)")
    parser.add_argument("--eval-split", default="eval", help="Held-out split for validation (default: eval)")
    parser.add_argument("--rounds", type=int, default=2, help="Coordinate descent rounds (default: 2)")
    parser.add_argument("--random-trials", type=int, default=15, help="Random local perturbation trials (default: 15)")
    parser.add_argument("--seed", type=int, default=42, help="Random seed")
    parser.add_argument("--out", default=None, help="Optional path to save best config JSON")
    args = parser.parse_args()

    fixtures_dir = Path(args.fixtures_dir)
    train_items = evaluate.discover_fixtures(fixtures_dir, split=args.train_split)
    if not train_items:
        print(f"No fixtures found for split='{args.train_split}'. Falling back to split='all'.")
        train_split = "all"
    else:
        train_split = args.train_split

    eval_items = evaluate.discover_fixtures(fixtures_dir, split=args.eval_split)
    has_eval = len(eval_items) > 0

    best_cfg = dict(DEFAULT_CONFIG)
    base_report = eval_config(best_cfg, train_split, fixtures_dir)
    best_report = base_report
    best_obj = objective(base_report)

    print(f"🎯 Baseline ({train_split}, {base_report['total_fixtures']} fixtures): "
          f"score={base_report['overall_score']:.2f}% | obj={best_obj:.2f} | "
          f"micro_flips={base_report['total_micro_flips']} | failed={base_report['total_failed_assertions']}")

    # 1. Coordinate descent rounds
    for rnd in range(1, args.rounds + 1):
        improved = False
        for param, (lo, hi, step, kind) in DEFAULT_SEARCH_SPACE.items():
            cur_val = best_cfg[param]
            candidates = []
            for delta in (-2 * step, -step, step, 2 * step):
                cand_val = cur_val + delta
                if kind == "int":
                    cand_val = int(round(max(lo, min(hi, cand_val))))
                else:
                    cand_val = round(max(lo, min(hi, cand_val)), 4)
                if cand_val != cur_val and cand_val not in candidates:
                    candidates.append(cand_val)

            for cand_val in candidates:
                trial_cfg = dict(best_cfg)
                trial_cfg[param] = cand_val
                rep = eval_config(trial_cfg, train_split, fixtures_dir)
                obj = objective(rep)
                if obj > best_obj + 1e-4:
                    best_obj = obj
                    best_cfg = trial_cfg
                    best_report = rep
                    improved = True
                    print(f"  [Round {rnd}] {param}={cand_val} -> score={rep['overall_score']:.2f}% (obj={obj:.2f})")
        if not improved:
            break

    # 2. Random local perturbations
    rng = random.Random(args.seed)
    for t in range(1, args.random_trials + 1):
        trial_cfg = dict(best_cfg)
        for param in rng.sample(list(DEFAULT_SEARCH_SPACE.keys()), k=rng.randint(2, 4)):
            lo, hi, step, kind = DEFAULT_SEARCH_SPACE[param]
            val = trial_cfg[param] + rng.choice([-2, -1, 1, 2]) * step
            trial_cfg[param] = int(round(max(lo, min(hi, val)))) if kind == "int" else round(max(lo, min(hi, val)), 4)
        rep = eval_config(trial_cfg, train_split, fixtures_dir)
        obj = objective(rep)
        if obj > best_obj + 1e-4:
            best_obj = obj
            best_cfg = trial_cfg
            best_report = rep
            print(f"  [Random {t}] new best -> score={rep['overall_score']:.2f}% (obj={obj:.2f})")

    print("\n" + "=" * 72)
    print("🏁 TUNING SUMMARY")
    print("=" * 72)
    print(f"Train ({train_split}): {base_report['overall_score']:.2f}% -> {best_report['overall_score']:.2f}%")
    if has_eval:
        base_eval = eval_config(DEFAULT_CONFIG, args.eval_split, fixtures_dir)
        best_eval = eval_config(best_cfg, args.eval_split, fixtures_dir)
        print(f"Eval  ({args.eval_split}): {base_eval['overall_score']:.2f}% -> {best_eval['overall_score']:.2f}%")

    diffs = {k: (DEFAULT_CONFIG[k], best_cfg[k]) for k in DEFAULT_CONFIG if best_cfg[k] != DEFAULT_CONFIG[k]}
    if diffs:
        print("\nSuggested ClassifierConfig changes:")
        for k, (old_v, new_v) in diffs.items():
            print(f"  • {k}: {old_v} -> {new_v}")
    else:
        print("\nNo parameter changes outperformed the current defaults.")

    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            json.dump(best_cfg, f, indent=2)
        print(f"\nSaved best config to {args.out}")


if __name__ == "__main__":
    main()
