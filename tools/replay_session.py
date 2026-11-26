#!/usr/bin/env python3
# Copyright (c) 2026 Stephan Schultz
# SPDX-License-Identifier: EUPL-1.2
"""
WLED Audio Classifier - Fixture Replay & Timeline Visualizer

Executes the native C++ AudioClassifier (via eval_cli) against a recorded session
fixture (.json or .json.gz), displays a second-by-second timeline, and verifies
all defined assertion checkpoints and continuous benchmark metrics.
"""

import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import evaluate  # noqa: E402


def format_duration(seconds: float) -> str:
    m = int(seconds // 60)
    s = int(seconds % 60)
    return f"{m:02d}:{s:02d}"


def main():
    parser = argparse.ArgumentParser(
        description="Replay a recorded audio session fixture through the native C++ AudioClassifier."
    )
    parser.add_argument("fixture", help="Path to recorded session .json or .json.gz fixture")
    parser.add_argument("--step", type=float, default=1.0, help="Timeline print interval in seconds (default: 1.0)")
    parser.add_argument("--config", default=None, help="JSON string or path of ClassifierConfig overrides")
    parser.add_argument("--trace", action="store_true", help="Include internal trace diagnostics in hotspots")
    args = parser.parse_args()

    fixture_path = Path(args.fixture).resolve()
    if not fixture_path.is_file():
        print(f"Error: Fixture file not found: {fixture_path}")
        sys.exit(1)

    step_ms = max(20, int(round(args.step * 1000.0)))
    bin_path = evaluate.ensure_eval_binary()
    cli_out = evaluate.run_eval_cli(bin_path, fixture_path, config_json=args.config, step_ms=step_ms, trace=args.trace)
    meta = cli_out.get("metadata", {})
    snaps = cli_out.get("snapshots", [])

    print("\n" + "=" * 80)
    print("▶️  WLED Audio Classifier - Fixture Replay & Timeline (Native C++ Engine)")
    print("=" * 80)
    print(f"📁 Fixture:       {fixture_path.name}")
    print(f"📝 Description:   {meta.get('description', 'None')}")
    print(f"⏱️  Duration:      {cli_out.get('duration_sec', 0.0):.2f}s ({cli_out.get('frame_count', 0)} frames)")
    print(f"🏷️  Primary Label: {meta.get('primary_label', 'unknown')}")
    print(f"🎯 Assertions:    {len(meta.get('assertions', []))} defined")
    print("-" * 80)
    print(f"{'Time':<8} | {'Level':<6} | {'Floor':<5} | {'Presence':<8} | {'State':<13} | {'Phrase':<9} | {'Conf':<6} | {'BPM':<7} | {'Beats':<6}")
    print("-" * 88)

    for snap in snaps:
        t_sec = float(snap.get("t_sec", 0.0))
        conf = float(snap.get("conf", 0.0))
        conf_bar = "█" * int(conf * 5)
        bpm = int(snap.get("bpm", 0))
        bpm_str = f"{bpm} BPM" if bpm > 0 else "-- BPM"
        state_disp = str(snap.get("state", "silence")).replace("_", " ").title()
        phrase_disp = str(snap.get("phrase", "unknown")).title()
        print(
            f"{format_duration(t_sec):<8} | "
            f"{float(snap.get('level', 0.0)):5.1f}% | "
            f"{float(snap.get('floor', 0.0)):4.1f}% | "
            f"{str(snap.get('presence', 'quiet')).upper():<8} | "
            f"{state_disp:<13} | "
            f"{phrase_disp:<9} | "
            f"{conf:4.2f} {conf_bar:<5} | "
            f"{bpm_str:<7} | "
            f"{int(snap.get('beats', 0)):<6}"
        )

    print("-" * 80)
    print("\n🏁 ASSERTION RESULTS:")
    failed = cli_out.get("failed_assertions", 0)
    passed = cli_out.get("passed_assertions", 0)
    if failed > 0:
        for a in cli_out.get("assertions", []):
            if not a.get("passed", True):
                print(f"  ❌ {a.get('error', a.get('description', 'Assertion failed'))}")
        print(f"\n❌ FAILED: {failed} failed, {passed} passed.")
    else:
        print(f"  ✅ All {passed} assertion checkpoints PASSED!")
        print(f"  Overall classification matches: '{meta.get('primary_label')}'\n")

    # Compute 1 Hz benchmark scorecard if step != 1.0
    cli_1hz = cli_out if step_ms == 1000 else evaluate.run_eval_cli(bin_path, fixture_path, config_json=args.config, step_ms=1000, trace=args.trace)
    cat = evaluate.infer_category(fixture_path, meta, evaluate.DEFAULT_FIXTURES_DIR) if evaluate.DEFAULT_FIXTURES_DIR in fixture_path.parents else str(meta.get("primary_label", "unknown"))
    split = str(meta.get("split", "train")).lower()
    score = evaluate.score_cli_output(cli_1hz, fixture_path, cat, split)

    print("=" * 80)
    print("📊 CONTINUOUS BENCHMARK EVALUATION & ACCURACY METRICS")
    print("=" * 80)
    print(f"⏱️  Evaluated Timeline:      {score['duration_secs']} seconds (1 Hz continuous grid)")
    print(f"🎯 Presence Accuracy:        {score['presence_acc']:5.1f}%  (F1: {score['presence_f1']:5.1f}%)")
    print(f"🎶 Semantic State Accuracy:  {score['semantic_acc']:5.1f}%  (Music vs Ambient vs Silence)")
    print(f"🎼 Phrase Alignment Score:   {score['phrase_acc']:5.1f}%  (Verse / Chorus / Breakdown / Outro)")
    print(f"🌊 Stability / Anti-Churn:   {score['stability_score']:5.1f}%  (Micro-Flips: {score['micro_flips']}, Ping-Pongs: {score['ping_pongs']})")
    if score["bpm_eval_secs"] > 0:
        print(f"🎵 BPM Accuracy:             {score['bpm_acc']:5.1f}%  (MAE: {score['bpm_mae']:.1f} BPM, Octave Errs: {score['bpm_octave_errors']}, {score['bpm_eval_secs']}s)")
    if score["beat_prob_sep"] is not None:
        print(f"🥁 Beat Prob Separation:     {score['beat_prob_sep']:5.1f}%  (Active - Quiet avg)")
    print(f"⏱️  Avg Transition Latency:   {score['avg_latency_sec']:4.2f}s")
    print("-" * 80)
    print(f"🏆 OVERALL BENCHMARK SCORE:  {score['overall_score']:5.1f}%")
    print("=" * 80)

    if score["hotspots"]:
        print("\n⚠️  Top Error Hotspots (first 5):")
        for h in score["hotspots"][:5]:
            m = h["t_sec"] // 60
            s = h["t_sec"] % 60
            tr = f" | {h['trace']}" if h.get("trace") else ""
            print(f"   • [{m:02d}:{s:02d}] {h['kind'].title()}: expected '{h['expected']}', got '{h['actual']}'{tr}")
    print("")

    if failed > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
