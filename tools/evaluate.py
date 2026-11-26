#!/usr/bin/env python3
# Copyright (c) 2026 Stephan Schultz
# SPDX-License-Identifier: EUPL-1.2
"""
WLED Audio Classifier - Suite-Wide Evaluator & Regression Gate

Executes the native C++ eval_cli binary against all labeled fixtures (.json / .json.gz),
computes confusion matrices, transition latency, octave-aware BPM accuracy, beat-prob
separation, phrase anti-churn, and composite scores, and optionally enforces a baseline
regression gate.
"""

import argparse
import concurrent.futures
import gzip
import json
import os
import shutil
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_EVAL_BIN = REPO_ROOT / ".pio" / "build" / "eval" / "program"
DEFAULT_FIXTURES_DIR = REPO_ROOT / "test" / "fixtures"
DEFAULT_BASELINE_PATH = REPO_ROOT / "test" / "baseline_scores.json"

FALSE_MUSIC_HIGH_PENALTY_CATEGORIES = {"speech_meeting", "podcast", "kitchen_domestic", "speech"}


def ensure_eval_binary(bin_path: Path = DEFAULT_EVAL_BIN, force_rebuild: bool = False) -> Path:
    env_bin = os.environ.get("EVAL_CLI_BIN")
    if env_bin and os.path.isfile(env_bin):
        return Path(env_bin)

    sources = [
        REPO_ROOT / "src" / "classifier.cpp",
        REPO_ROOT / "src" / "classifier.h",
        REPO_ROOT / "tools" / "eval_cli.cpp",
    ]
    needs_build = force_rebuild or not bin_path.is_file()
    if not needs_build:
        bin_mtime = bin_path.stat().st_mtime
        if any(s.is_file() and s.stat().st_mtime > bin_mtime for s in sources):
            needs_build = True

    if not needs_build:
        return bin_path

    bin_path.parent.mkdir(parents=True, exist_ok=True)
    arduinojson_inc = None
    for candidate in [
        REPO_ROOT / ".pio" / "libdeps" / "eval" / "ArduinoJson" / "src",
        REPO_ROOT / ".pio" / "libdeps" / "native" / "ArduinoJson" / "src",
    ]:
        if candidate.is_dir():
            arduinojson_inc = candidate
            break

    compiler = shutil.which("clang++") or shutil.which("g++")
    if compiler and arduinojson_inc is not None:
        cmd = [
            compiler,
            "-std=gnu++17",
            "-O2",
            f"-I{REPO_ROOT / 'src'}",
            f"-I{arduinojson_inc}",
            str(REPO_ROOT / "src" / "classifier.cpp"),
            str(REPO_ROOT / "tools" / "eval_cli.cpp"),
            "-o",
            str(bin_path),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode == 0:
            return bin_path

    pio = shutil.which("pio") or os.path.expanduser("~/.platformio/penv/bin/pio")
    res = subprocess.run([pio, "run", "-d", str(REPO_ROOT), "-e", "eval"], capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Failed to build eval_cli:\n{res.stderr or res.stdout}")
    return bin_path


def load_fixture_meta(path: Path) -> dict:
    opener = gzip.open if path.name.endswith(".gz") else open
    with opener(path, "rt", encoding="utf-8") as f:
        data = json.load(f)
    return data.get("metadata", {}) if isinstance(data, dict) else {}


def infer_category(path: Path, meta: dict, fixtures_root: Path) -> str:
    if meta.get("category"):
        return str(meta["category"]).lower()
    rel = path.relative_to(fixtures_root)
    if len(rel.parts) > 1:
        return rel.parts[0].lower()
    return str(meta.get("primary_label", "unknown")).lower()


def discover_fixtures(fixtures_root: Path, split: str = "all", category: str = "all"):
    paths = sorted(list(fixtures_root.rglob("*.json")) + list(fixtures_root.rglob("*.json.gz")))
    selected = []
    for p in paths:
        try:
            meta = load_fixture_meta(p)
        except Exception:
            continue
        if not meta:
            continue
        fix_split = str(meta.get("split", "train")).lower()
        fix_cat = infer_category(p, meta, fixtures_root)
        if split != "all" and fix_split != split:
            continue
        if category != "all" and fix_cat != category:
            continue
        selected.append((p, meta, fix_cat, fix_split))
    return selected


def run_eval_cli(bin_path: Path, fixture_path: Path, config_json: str = None, step_ms: int = 1000, trace: bool = False) -> dict:
    cmd = [str(bin_path), "--step-ms", str(step_ms)]
    if trace:
        cmd.append("--trace")
    if config_json:
        cmd.extend(["--config", config_json])

    if fixture_path.name.endswith(".gz"):
        cmd.extend(["--fixture", "-"])
        with gzip.open(fixture_path, "rb") as gz:
            raw_bytes = gz.read()
        res = subprocess.run(cmd, input=raw_bytes, capture_output=True)
    else:
        cmd.extend(["--fixture", str(fixture_path)])
        res = subprocess.run(cmd, capture_output=True)

    if res.returncode != 0:
        err = res.stderr.decode("utf-8", errors="replace") if isinstance(res.stderr, bytes) else res.stderr
        raise RuntimeError(f"eval_cli failed on {fixture_path}: {err}")
    out = res.stdout.decode("utf-8") if isinstance(res.stdout, bytes) else res.stdout
    return json.loads(out)


def compute_phrase_penalty(transitions: list) -> dict:
    history = []
    dwells = []
    penalty = 0.0
    micro_flips = 0
    rapid_flips = 0
    ping_pongs = 0

    for tr in transitions:
        phrase = tr.get("phrase", "").lower()
        t_sec = float(tr.get("t_sec", 0.0))
        if not phrase or phrase == "unknown":
            continue
        if not history:
            history.append((phrase, t_sec))
            continue
        if history[-1][0] == phrase:
            continue
        dwell = t_sec - history[-1][1]
        if dwell > 0.0:
            dwells.append(dwell)
            if dwell < 2.0:
                micro_flips += 1
                penalty += max(8.0, 15.0 * (1.0 - (dwell / 2.0)))
            elif dwell < 5.0:
                rapid_flips += 1
                penalty += 6.0 * ((5.0 - dwell) / 3.0)
            if len(history) >= 2 and history[-2][0] == phrase and (t_sec - history[-2][1]) <= 4.0:
                ping_pongs += 1
                penalty += 10.0
        history.append((phrase, t_sec))

    return {
        "penalty_score": round(penalty, 2),
        "stability_score": round(max(0.0, 100.0 - penalty), 2),
        "micro_flips": micro_flips,
        "rapid_flips": rapid_flips,
        "ping_pongs": ping_pongs,
        "min_dwell_sec": round(min(dwells), 2) if dwells else 0.0,
    }


def score_cli_output(cli_out: dict, fixture_path: Path, category: str, split: str) -> dict:
    meta = cli_out.get("metadata", {})
    snaps = cli_out.get("snapshots", [])
    segments = meta.get("segments", [])
    primary_label = str(meta.get("primary_label", "silence")).lower()
    ground_truth_bpm = float(meta.get("ground_truth_bpm", 0.0))

    boundaries = []
    for seg in segments:
        boundaries.append(float(seg.get("start_sec", 0.0)))
        boundaries.append(float(seg.get("end_sec", 0.0)))

    confusion = defaultdict(int)
    pres_tp = pres_fp = pres_fn = 0
    pres_errors = 0.0
    sem_errors = 0.0
    phr_errors = 0.0
    phr_eval_count = 0
    bpm_errors = 0.0
    bpm_eval_count = 0
    bpm_octave_errors = 0
    active_bp = []
    quiet_bp = []
    hotspots = []
    state_flips = 0
    phrase_flips = 0
    prev_state = None
    prev_phrase = None

    fp_music_weight = 1.5 if category in FALSE_MUSIC_HIGH_PENALTY_CATEGORIES else 1.0

    for idx, snap in enumerate(snaps):
        sec = int(round(float(snap.get("t_sec", idx))))
        if prev_state is not None and snap["state"] != prev_state:
            state_flips += 1
        if prev_phrase is not None and snap["phrase"] != prev_phrase:
            phrase_flips += 1
        prev_state = snap["state"]
        prev_phrase = snap["phrase"]

        matching_seg = None
        for seg in segments:
            if float(seg.get("start_sec", 0.0)) <= sec < float(seg.get("end_sec", 0.0)):
                matching_seg = seg
                break

        if matching_seg:
            exp_state = str(matching_seg.get("state", "music")).lower()
            exp_phrase = str(matching_seg.get("phrase", "verse")).lower()
            seg_bpm = float(matching_seg.get("bpm", ground_truth_bpm))
            ignore_phrase = bool(matching_seg.get("ignore_phrase", False))
        else:
            exp_state = primary_label
            exp_phrase = "steady"
            seg_bpm = ground_truth_bpm
            ignore_phrase = False

        exp_presence = "quiet" if exp_state == "silence" else "sound"
        in_slack = any(abs(sec - b) <= 1.5 for b in boundaries)

        act_pres = str(snap["presence"]).lower()
        act_state = str(snap["state"]).lower()
        act_phrase = str(snap["phrase"]).lower()

        confusion[f"{exp_state}->{act_state}"] += 1

        if exp_presence == "sound" and act_pres == "sound":
            pres_tp += 1
        elif exp_presence == "quiet" and act_pres == "sound":
            pres_fp += 1
        elif exp_presence == "sound" and act_pres == "quiet":
            pres_fn += 1

        if act_pres != exp_presence and not in_slack:
            pres_errors += 1.0

        if act_state != exp_state and not in_slack:
            if (exp_state == "music" and act_state == "silence") or (exp_state == "silence" and act_state == "music"):
                s_err = 1.0 * (fp_music_weight if act_state == "music" else 1.0)
            elif exp_state == "ambient_noise" and act_state == "music":
                s_err = 0.8 * fp_music_weight
            elif exp_state == "music" and act_state == "ambient_noise":
                s_err = 0.8
            elif exp_state in ("silence", "ambient_noise") and act_state in ("silence", "ambient_noise"):
                s_err = 0.25
            else:
                s_err = 0.5
            sem_errors += s_err
            if s_err >= 0.5:
                hotspots.append({
                    "fixture": fixture_path.name,
                    "t_sec": sec,
                    "kind": "state",
                    "expected": exp_state,
                    "actual": act_state,
                    "trace": {k: snap[k] for k in ("level", "floor", "beat_prob", "bpm", "bar_novelty", "high_flux_slope", "sound_energy", "decayed_max") if k in snap},
                })

        if exp_state == "music" and not ignore_phrase:
            phr_eval_count += 1
            if act_phrase != exp_phrase and not in_slack:
                if (exp_phrase, act_phrase) in [("verse", "chorus"), ("chorus", "verse")]:
                    ph_err = 0.25
                elif exp_phrase in ("breakdown", "outro") and act_phrase in ("breakdown", "outro"):
                    ph_err = 0.20
                elif exp_phrase == "buildup" or act_phrase == "buildup":
                    ph_err = 0.25
                elif (exp_phrase == "chorus" and act_phrase in ("breakdown", "outro")) or \
                     (exp_phrase in ("breakdown", "outro") and act_phrase == "chorus"):
                    ph_err = 1.0
                else:
                    ph_err = 0.50
                phr_errors += ph_err
                if ph_err >= 0.5:
                    hotspots.append({
                        "fixture": fixture_path.name,
                        "t_sec": sec,
                        "kind": "phrase",
                        "expected": exp_phrase,
                        "actual": act_phrase,
                        "trace": {k: snap[k] for k in ("level", "floor", "beat_prob", "bpm", "bar_novelty", "high_flux_slope", "sound_energy", "decayed_max") if k in snap},
                    })

        if seg_bpm > 0.0 and exp_state == "music" and exp_phrase not in ("breakdown", "outro"):
            act_bpm = float(snap.get("bpm", 0))
            if act_bpm > 0:
                err_bpm = abs(act_bpm - seg_bpm)
                bpm_errors += err_bpm
                bpm_eval_count += 1
                if abs(act_bpm - seg_bpm * 2.0) <= seg_bpm * 0.08 or abs(act_bpm - seg_bpm * 0.5) <= seg_bpm * 0.08:
                    bpm_octave_errors += 1

        if exp_state == "music" and not in_slack and sec >= 3:
            bp = float(snap.get("beat_prob", 0.0))
            if exp_phrase in ("chorus", "verse"):
                active_bp.append(bp)
            elif exp_phrase in ("breakdown", "outro"):
                quiet_bp.append(bp)

    # Transition latency (seconds after segment start until expected state matches)
    latencies = []
    for seg in segments:
        s_start = int(round(float(seg.get("start_sec", 0.0))))
        s_end = int(round(float(seg.get("end_sec", 0.0))))
        exp_s = str(seg.get("state", "music")).lower()
        for off in range(0, min(12, max(1, s_end - s_start))):
            t_idx = s_start + off
            if t_idx < len(snaps) and str(snaps[t_idx]["state"]).lower() == exp_s:
                latencies.append(float(off))
                break

    total_secs = max(1, len(snaps))
    presence_acc = max(0.0, 100.0 * (1.0 - (pres_errors / total_secs)))
    denom_f1 = (2 * pres_tp + pres_fp + pres_fn)
    presence_f1 = (100.0 * (2 * pres_tp) / denom_f1) if denom_f1 > 0 else presence_acc
    semantic_acc = max(0.0, 100.0 * (1.0 - (sem_errors / total_secs)))
    phrase_acc = max(0.0, 100.0 * (1.0 - (phr_errors / phr_eval_count))) if phr_eval_count > 0 else 100.0

    expected_flips = max(1, len(segments))
    excess_flips = max(0, (state_flips + phrase_flips) - expected_flips * 2)
    macro_stability = max(0.0, 100.0 - (excess_flips * 2.5))
    phrase_metrics = compute_phrase_penalty(cli_out.get("phrase_transitions", []))
    combined_stability = (0.5 * macro_stability) + (0.5 * phrase_metrics["stability_score"])

    bpm_mae = (bpm_errors / bpm_eval_count) if bpm_eval_count > 0 else 0.0
    bpm_acc = max(0.0, 100.0 * (1.0 - (bpm_mae / 20.0))) if bpm_eval_count > 0 else 100.0
    active_bp_avg = (sum(active_bp) / len(active_bp) * 100.0) if active_bp else 0.0
    quiet_bp_avg = (sum(quiet_bp) / len(quiet_bp) * 100.0) if quiet_bp else 0.0
    bp_sep = max(0.0, active_bp_avg - quiet_bp_avg) if (active_bp and quiet_bp) else 100.0

    if bpm_eval_count > 0 and active_bp and quiet_bp:
        rhythm_score = 0.6 * bpm_acc + 0.4 * min(100.0, bp_sep * 2.0)
        overall_score = (0.35 * semantic_acc) + (0.20 * presence_acc) + (0.20 * phrase_acc) + (0.15 * combined_stability) + (0.10 * rhythm_score)
    elif bpm_eval_count > 0:
        overall_score = (0.35 * semantic_acc) + (0.20 * presence_acc) + (0.20 * phrase_acc) + (0.15 * combined_stability) + (0.10 * bpm_acc)
    else:
        overall_score = (0.35 * semantic_acc) + (0.25 * presence_acc) + (0.25 * phrase_acc) + (0.15 * combined_stability)

    return {
        "fixture": fixture_path.name,
        "path": str(fixture_path.relative_to(REPO_ROOT)),
        "category": category,
        "split": split,
        "duration_secs": len(snaps),
        "overall_score": round(overall_score, 2),
        "semantic_acc": round(semantic_acc, 2),
        "presence_acc": round(presence_acc, 2),
        "presence_f1": round(presence_f1, 2),
        "phrase_acc": round(phrase_acc, 2),
        "stability_score": round(combined_stability, 2),
        "bpm_acc": round(bpm_acc, 2),
        "bpm_mae": round(bpm_mae, 2),
        "bpm_eval_secs": bpm_eval_count,
        "bpm_octave_errors": bpm_octave_errors,
        "beat_prob_sep": round(bp_sep, 2) if (active_bp and quiet_bp) else None,
        "avg_latency_sec": round(sum(latencies) / len(latencies), 2) if latencies else 0.0,
        "micro_flips": phrase_metrics["micro_flips"],
        "ping_pongs": phrase_metrics["ping_pongs"],
        "passed_assertions": cli_out.get("passed_assertions", 0),
        "failed_assertions": cli_out.get("failed_assertions", 0),
        "assertion_failures": [a["error"] for a in cli_out.get("assertions", []) if not a.get("passed", True)],
        "confusion": dict(confusion),
        "hotspots": hotspots,
    }


def evaluate_suite(
    fixtures_root: Path = DEFAULT_FIXTURES_DIR,
    split: str = "all",
    category: str = "all",
    config_json: str = None,
    trace: bool = False,
    bin_path: Path = DEFAULT_EVAL_BIN,
) -> dict:
    bin_path = ensure_eval_binary(bin_path)
    items = discover_fixtures(fixtures_root, split=split, category=category)
    if not items:
        raise RuntimeError(f"No fixtures found under {fixtures_root} (split={split}, category={category})")

    def _run_one(item):
        path, _, cat, sp = item
        cli_out = run_eval_cli(bin_path, path, config_json=config_json, step_ms=1000, trace=trace)
        return score_cli_output(cli_out, path, cat, sp)

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, len(items))) as pool:
        for res in pool.map(_run_one, items):
            results.append(res)

    results.sort(key=lambda r: (r["category"], r["fixture"]))

    by_category = defaultdict(list)
    total_confusion = defaultdict(int)
    all_hotspots = []
    for r in results:
        by_category[r["category"]].append(r)
        for k, v in r["confusion"].items():
            total_confusion[k] += v
        all_hotspots.extend(r["hotspots"])

    category_summary = {}
    for cat, rows in sorted(by_category.items()):
        n = len(rows)
        category_summary[cat] = {
            "count": n,
            "overall_score": round(sum(r["overall_score"] for r in rows) / n, 2),
            "semantic_acc": round(sum(r["semantic_acc"] for r in rows) / n, 2),
            "presence_acc": round(sum(r["presence_acc"] for r in rows) / n, 2),
            "phrase_acc": round(sum(r["phrase_acc"] for r in rows) / n, 2),
            "stability_score": round(sum(r["stability_score"] for r in rows) / n, 2),
            "failed_assertions": sum(r["failed_assertions"] for r in rows),
        }

    n_all = len(results)
    suite_overall = round(sum(r["overall_score"] for r in results) / n_all, 2)
    suite_semantic = round(sum(r["semantic_acc"] for r in results) / n_all, 2)
    suite_presence = round(sum(r["presence_acc"] for r in results) / n_all, 2)
    suite_phrase = round(sum(r["phrase_acc"] for r in results) / n_all, 2)
    suite_stability = round(sum(r["stability_score"] for r in results) / n_all, 2)
    total_failed_asserts = sum(r["failed_assertions"] for r in results)
    total_micro_flips = sum(r["micro_flips"] for r in results)
    total_ping_pongs = sum(r["ping_pongs"] for r in results)

    return {
        "overall_score": suite_overall,
        "semantic_acc": suite_semantic,
        "presence_acc": suite_presence,
        "phrase_acc": suite_phrase,
        "stability_score": suite_stability,
        "total_fixtures": n_all,
        "total_failed_assertions": total_failed_asserts,
        "total_micro_flips": total_micro_flips,
        "total_ping_pongs": total_ping_pongs,
        "categories": category_summary,
        "confusion": dict(total_confusion),
        "fixtures": [{k: v for k, v in r.items() if k != "hotspots"} for r in results],
        "hotspots": all_hotspots,
    }


def print_report(report: dict, hotspot_limit: int = 10):
    print("\n" + "=" * 92)
    print("📊 WLED AUDIO CLASSIFIER - SUITE BENCHMARK SCORECARD")
    print("=" * 92)
    print(f"{'Fixture':<30} | {'Cat':<13} | {'Score':>6} | {'Sem':>6} | {'Pres':>6} | {'Phr':>6} | {'Stab':>6} | {'Asserts':>7}")
    print("-" * 92)
    for r in report["fixtures"]:
        assert_str = f"{r['passed_assertions']}/{r['passed_assertions'] + r['failed_assertions']}"
        print(
            f"{r['fixture']:<30} | {r['category']:<13} | "
            f"{r['overall_score']:5.1f}% | {r['semantic_acc']:5.1f}% | "
            f"{r['presence_acc']:5.1f}% | {r['phrase_acc']:5.1f}% | "
            f"{r['stability_score']:5.1f}% | {assert_str:>7}"
        )
        for err in r["assertion_failures"]:
            print(f"   ❌ {err}")

    print("-" * 92)
    print(
        f"{'SUITE AVERAGE':<30} | {report['total_fixtures']:<13} | "
        f"{report['overall_score']:5.1f}% | {report['semantic_acc']:5.1f}% | "
        f"{report['presence_acc']:5.1f}% | {report['phrase_acc']:5.1f}% | "
        f"{report['stability_score']:5.1f}% | "
        f"fail={report['total_failed_assertions']}"
    )
    print(f"Micro-Flips: {report['total_micro_flips']} | Ping-Pongs: {report['total_ping_pongs']}")

    if report["confusion"]:
        conf_str = ", ".join(f"{k}: {v}s" for k, v in sorted(report["confusion"].items()))
        print(f"Confusion (sec): {conf_str}")

    if hotspot_limit > 0 and report["hotspots"]:
        print(f"\n⚠️  Top {min(hotspot_limit, len(report['hotspots']))} Error Hotspots:")
        for h in report["hotspots"][:hotspot_limit]:
            tr = f" | trace={h['trace']}" if h.get("trace") else ""
            print(f"   • [{h['fixture']} @ {h['t_sec']}s] {h['kind']}: expected '{h['expected']}', got '{h['actual']}'{tr}")
    print("=" * 92 + "\n")


def check_baseline(report: dict, baseline_path: Path, max_regression: float) -> list:
    if not baseline_path.is_file():
        return [f"Baseline file not found: {baseline_path}"]
    with open(baseline_path, "r", encoding="utf-8") as f:
        base = json.load(f)

    regressions = []
    if report["total_failed_assertions"] > 0:
        regressions.append(f"{report['total_failed_assertions']} fixture assertions failed")

    overall_delta = report["overall_score"] - float(base.get("overall_score", 0.0))
    if overall_delta < -max_regression:
        regressions.append(
            f"Suite overall score regressed by {overall_delta:.2f}% "
            f"({report['overall_score']:.2f}% vs baseline {base['overall_score']:.2f}%)"
        )

    base_cats = base.get("categories", {})
    for cat, metrics in report["categories"].items():
        if cat in base_cats:
            delta = metrics["overall_score"] - float(base_cats[cat]["overall_score"])
            if delta < -max_regression:
                regressions.append(
                    f"Category '{cat}' regressed by {delta:.2f}% "
                    f"({metrics['overall_score']:.2f}% vs baseline {base_cats[cat]['overall_score']:.2f}%)"
                )
    return regressions


def main():
    parser = argparse.ArgumentParser(description="Suite-wide WLED Audio Classifier evaluator")
    parser.add_argument("--fixtures-dir", default=str(DEFAULT_FIXTURES_DIR), help="Fixtures root directory")
    parser.add_argument("--split", choices=["all", "train", "eval"], default="all", help="Filter by fixture split")
    parser.add_argument("--category", default="all", help="Filter by scenario category")
    parser.add_argument("--config", default=None, help="JSON string or path of ClassifierConfig overrides")
    parser.add_argument("--trace", action="store_true", help="Include internal trace diagnostics in hotspots")
    parser.add_argument("--hotspots", type=int, default=10, help="Number of error hotspots to print")
    parser.add_argument("--json-out", default=None, help="Write full JSON report to path")
    parser.add_argument("--save-baseline", default=None, help="Save baseline JSON to path")
    parser.add_argument("--check-baseline", default=None, help="Compare against baseline JSON and fail on regression")
    parser.add_argument("--max-regression", type=float, default=1.5, help="Max allowed score drop (%%) vs baseline")
    args = parser.parse_args()

    report = evaluate_suite(
        fixtures_root=Path(args.fixtures_dir),
        split=args.split,
        category=args.category,
        config_json=args.config,
        trace=args.trace,
    )
    print_report(report, hotspot_limit=args.hotspots)

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)
        print(f"Saved JSON report to {args.json_out}")

    if args.save_baseline:
        baseline_payload = {
            "overall_score": report["overall_score"],
            "semantic_acc": report["semantic_acc"],
            "presence_acc": report["presence_acc"],
            "phrase_acc": report["phrase_acc"],
            "stability_score": report["stability_score"],
            "categories": report["categories"],
            "fixtures": {
                r["fixture"]: {
                    "overall_score": r["overall_score"],
                    "semantic_acc": r["semantic_acc"],
                    "presence_acc": r["presence_acc"],
                    "phrase_acc": r["phrase_acc"],
                    "stability_score": r["stability_score"],
                }
                for r in report["fixtures"]
            },
        }
        with open(args.save_baseline, "w", encoding="utf-8") as f:
            json.dump(baseline_payload, f, indent=2)
        print(f"Saved baseline scores to {args.save_baseline}")

    if args.check_baseline:
        regressions = check_baseline(report, Path(args.check_baseline), args.max_regression)
        if regressions:
            print("❌ REGRESSION GATE FAILED:")
            for r in regressions:
                print(f"   • {r}")
            sys.exit(1)
        print(f"✅ Regression gate passed (within {args.max_regression:.1f}% of baseline).")

    if report["total_failed_assertions"] > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
