#!/usr/bin/env python3
# Copyright (c) 2026 Stephan Schultz
# SPDX-License-Identifier: EUPL-1.2
"""
WLED Audio Classifier - Fixture Replay & Timeline Visualizer

Loads a recorded session fixture (JSON), feeds frames through the audio
classification algorithm, displays a second-by-second timeline, and verifies
all defined assertion checkpoints.
"""

import argparse
import json
import math
import os
import sys


class SimulatedAudioClassifier:
    def __init__(self, quiet_thresh=6.0, margin=4.0, on_delay_ms=1200, off_delay_ms=8000, music_hold_ms=30000):
        self.quiet_thresh = quiet_thresh
        self.margin = margin
        self.on_delay_ms = on_delay_ms
        self.off_delay_ms = off_delay_ms
        self.music_hold_ms = music_hold_ms

        self.current_level = 0.0
        self.average_1s_level = 0.0
        self.noise_floor = 2.0

        self.sum_squared_energy = 0.0
        self.frame_count_1s = 0
        self.last_1s_tick = 0

        self.histogram = [0] * 101
        self.history_ring = [0] * 300
        self.history_idx = 0
        self.history_count = 0

        self.presence = "quiet"
        self.is_pending_on = False
        self.pending_on_time = 0
        self.is_pending_off = False
        self.pending_off_time = 0

        self.semantic_state = "silence"
        self.phrase_state = "steady"
        self.music_confidence = 0.0
        self.last_music_evidence = 0

        self.beat_times = []
        self.beat_regularity = 0.0
        self.short_bass_energy = 0.0
        self.long_bass_energy = 0.0

    def process_frame(self, t, lvl, raw, smth, pk, hz, mag, bands):
        # 1. Smooth instantaneous level
        self.current_level = (self.current_level * 0.7) + (lvl * 0.3)

        # 2. Accumulate energy for 1-second metric
        self.sum_squared_energy += (lvl * lvl)
        self.frame_count_1s += 1

        if self.last_1s_tick == 0:
            self.last_1s_tick = t

        # 3. 1-second calculations & noise floor update
        if t - self.last_1s_tick >= 1000:
            self._update_1s_metrics(t)
            self.last_1s_tick = t

        # 4. Presence State Machine
        self._update_presence(t)

        # 5. Semantic classification & phrasing
        if self.presence == "quiet":
            hangover = (self.last_music_evidence > 0 and (t - self.last_music_evidence <= self.music_hold_ms))
            if not hangover:
                self.semantic_state = "silence"
                self.phrase_state = "steady"
                self.music_confidence = 0.0
            else:
                self._update_music_and_phrasing(t, lvl, pk, bands)
        else:
            self._update_music_and_phrasing(t, lvl, pk, bands)

    def _update_1s_metrics(self, t):
        if self.frame_count_1s > 0:
            self.average_1s_level = math.sqrt(self.sum_squared_energy / self.frame_count_1s)
        else:
            self.average_1s_level = self.current_level
        self.sum_squared_energy = 0.0
        self.frame_count_1s = 0

        # Only adapt noise floor when QUIET and not pending on
        if self.presence == "quiet" and not self.is_pending_on:
            bin_idx = min(100, max(0, int(round(self.average_1s_level))))
            if self.history_count == 300:
                old_bin = self.history_ring[self.history_idx]
                if self.histogram[old_bin] > 0:
                    self.histogram[old_bin] -= 1
            else:
                self.history_count += 1

            self.history_ring[self.history_idx] = bin_idx
            self.histogram[bin_idx] += 1
            self.history_idx = (self.history_idx + 1) % 300

            target_rank = max(1, (self.history_count * 10) // 100)
            cum_sum = 0
            p10 = 0
            for b in range(101):
                cum_sum += self.histogram[b]
                if cum_sum >= target_rank:
                    p10 = b
                    break
            self.noise_floor = float(p10)

    def _update_presence(self, t):
        threshold = max(self.quiet_thresh, self.noise_floor + self.margin)

        if self.current_level > threshold:
            self.is_pending_off = False
            if self.presence == "quiet":
                if not self.is_pending_on:
                    self.is_pending_on = True
                    self.pending_on_time = t
                if t - self.pending_on_time >= self.on_delay_ms:
                    self.presence = "sound"
                    self.is_pending_on = False
        else:
            self.is_pending_on = False
            if self.presence == "sound":
                if not self.is_pending_off:
                    self.is_pending_off = True
                    self.pending_off_time = t
                if t - self.pending_off_time >= self.off_delay_ms:
                    self.presence = "quiet"
                    self.is_pending_off = False

    def _update_music_and_phrasing(self, t, lvl, pk, bands):
        # Beat tracking
        if pk:
            if not self.beat_times or (t - self.beat_times[-1] >= 200):
                self.beat_times.append(t)
                if len(self.beat_times) > 16:
                    self.beat_times.pop(0)

        self.beat_regularity = 0.0
        if len(self.beat_times) >= 4:
            intervals = [self.beat_times[i] - self.beat_times[i-1] for i in range(1, len(self.beat_times))]
            avg = sum(intervals) / len(intervals)
            if 250 <= avg <= 1500:
                dev = sum(abs(iv - avg) for iv in intervals) / len(intervals)
                reg = 1.0 - (dev / avg)
                self.beat_regularity = max(0.0, min(1.0, reg))

        # Bass ratio
        bass = bands[0] + bands[1] if len(bands) >= 2 else 0
        mids = bands[8] + bands[9] if len(bands) >= 10 else 0
        bass_ratio = (bass / (bass + mids)) if (bass + mids) > 0 else 0.0

        # Phrasing energy
        self.short_bass_energy = (self.short_bass_energy * 0.90) + (bass * 0.10)
        self.long_bass_energy = (self.long_bass_energy * 0.98) + (bass * 0.02)

        has_musical_evidence = (
            (self.beat_regularity > 0.45)
            or (self.beat_regularity > 0.30 and bass_ratio > 0.40)
            or (self.current_level > 15.0 and bass_ratio > 0.45)
        )

        if has_musical_evidence:
            self.last_music_evidence = t
            self.music_confidence = min(1.0, self.music_confidence + 0.08)
        else:
            self.music_confidence = max(0.0, self.music_confidence - 0.02)

        is_hangover = (self.last_music_evidence > 0 and (t - self.last_music_evidence <= self.music_hold_ms))

        if self.music_confidence >= 0.50:
            self.semantic_state = "music"
        elif is_hangover:
            self.semantic_state = "music"
        else:
            self.semantic_state = "ambient_noise" if self.current_level >= (self.noise_floor + self.margin) else "silence"

        # Phrase state detection with 8-bar chorus lock
        if self.semantic_state == "music":
            if not hasattr(self, "last_phrase_change"):
                self.last_phrase_change = 0
            time_in_phrase = (t - self.last_phrase_change) / 1000.0

            # 8 bars = 32 beats (approx 12-16s depending on tempo)
            phrase_dur = 13.0
            if len(self.beat_times) >= 4:
                intervals = [self.beat_times[i] - self.beat_times[i-1] for i in range(1, len(self.beat_times))]
                avg_ms = sum(intervals) / len(intervals)
                if 250 <= avg_ms <= 1200:
                    phrase_dur = (avg_ms * 32) / 1000.0

            if self.phrase_state == "chorus" and time_in_phrase < phrase_dur:
                # Hold chorus for minimum 8 bars unless complete volume drop
                if self.current_level < (self.noise_floor + 2.0) or (self.long_bass_energy > 15.0 and self.short_bass_energy < (self.long_bass_energy * 0.35)):
                    self.phrase_state = "breakdown"
                    self.last_phrase_change = t
            elif self.current_level < (self.noise_floor + 3.0):
                if self.phrase_state in ("breakdown", "outro"):
                    self.phrase_state = "outro"
                else:
                    self.phrase_state = "breakdown"
                    self.last_phrase_change = t
            else:
                prev_ph = self.phrase_state
                if self.long_bass_energy > 15.0 and self.short_bass_energy < (self.long_bass_energy * 0.45):
                    self.phrase_state = "breakdown"
                elif (self.short_bass_energy > (self.long_bass_energy * 1.35) and self.short_bass_energy > 20.0) or (self.current_level > 35.0 and self.beat_regularity > 0.40):
                    self.phrase_state = "chorus"
                elif self.short_bass_energy > (self.long_bass_energy * 1.15):
                    self.phrase_state = "buildup"
                else:
                    self.phrase_state = "verse"

                if self.phrase_state != prev_ph:
                    self.last_phrase_change = t
        else:
            self.phrase_state = "steady"


def format_duration(seconds: float) -> str:
    m = int(seconds // 60)
    s = int(seconds % 60)
    return f"{m:02d}:{s:02d}"


def main():
    parser = argparse.ArgumentParser(description="Replay recorded session fixture and verify classifications.")
    parser.add_argument("fixture", help="Path to fixture JSON file (e.g. test/fixtures/session_*.json)")
    parser.add_argument("--step", type=float, default=1.0, help="Timeline display interval in seconds (default: 1.0)")
    args = parser.parse_args()

    if not os.path.exists(args.fixture):
        print(f"❌ File not found: {args.fixture}")
        sys.exit(1)

    with open(args.fixture, "r") as f:
        data = json.load(f)

    meta = data.get("metadata", {})
    frames = data.get("frames", [])
    assertions = meta.get("assertions", [])

    print("\n" + "=" * 80)
    print("▶️  WLED Audio Classifier - Fixture Replay & Timeline")
    print("=" * 80)
    print(f"📁 Fixture:       {os.path.basename(args.fixture)}")
    print(f"📝 Description:   {meta.get('description', 'N/A')}")
    print(f"⏱️  Duration:      {meta.get('duration_seconds', 0)}s ({len(frames)} frames)")
    print(f"🏷️  Primary Label: {meta.get('primary_label', 'unknown')}")
    print(f"🎯 Assertions:    {len(assertions)} defined")
    print("-" * 80)

    if not frames:
        print("❌ Fixture has no frames to replay.")
        sys.exit(1)

    t0 = frames[0]["t"]
    classifier = SimulatedAudioClassifier()

    print(f"{'Time':<8} | {'Level':<6} | {'Floor':<5} | {'Presence':<8} | {'State':<13} | {'Phrase':<9} | {'Conf':<6} | {'Beats':<6}")
    print("-" * 80)

    # Evaluate continuous 1-second benchmark metrics across labeled segments
    segments = meta.get("segments", [])
    boundaries = []
    for seg in segments:
        boundaries.append(seg.get("start_sec", 0.0))
        boundaries.append(seg.get("end_sec", 0.0))

    second_snapshots = {}
    prev_state = None
    prev_phrase = None
    state_flips = 0
    phrase_flips = 0

    last_snap_sec = -1
    last_report_sec = -1.0
    passed_asserts = 0
    failed_asserts = 0
    failures = []

    for f in frames:
        raw_t = f["t"]
        t = raw_t - t0 if raw_t >= t0 else raw_t
        t_sec = t / 1000.0

        raw = f.get("raw", 0.0)
        smth = f.get("smth", 0.0)
        eff_vol = max(raw, smth)
        lvl = (eff_vol / 255.0) * 100.0

        pk = f.get("pk", 0) != 0
        hz = f.get("hz", 0.0)
        mag = f.get("mag", 0.0)
        bands = f.get("bands", [0] * 16)

        classifier.process_frame(t, lvl, raw, smth, pk, hz, mag, bands)

        cur_sec = int(t_sec)
        if cur_sec != last_snap_sec:
            last_snap_sec = cur_sec
            second_snapshots[cur_sec] = {
                "presence": classifier.presence,
                "state": classifier.semantic_state,
                "phrase": classifier.phrase_state,
                "conf": classifier.music_confidence,
                "lvl": classifier.current_level,
            }
            if prev_state is not None and classifier.semantic_state != prev_state:
                state_flips += 1
            if prev_phrase is not None and classifier.phrase_state != prev_phrase:
                phrase_flips += 1
            prev_state = classifier.semantic_state
            prev_phrase = classifier.phrase_state

        # Print periodic timeline row
        if t_sec >= last_report_sec + args.step:
            last_report_sec = t_sec
            conf_bar = "█" * int(classifier.music_confidence * 5)
            print(
                f"{format_duration(t_sec):<8} | "
                f"{classifier.current_level:5.1f}% | "
                f"{classifier.noise_floor:4.1f}% | "
                f"{classifier.presence.upper():<8} | "
                f"{classifier.semantic_state:<13} | "
                f"{classifier.phrase_state:<9} | "
                f"{classifier.music_confidence:4.2f} {conf_bar:<5} | "
                f"{len(classifier.beat_times):<6}"
            )

        # Evaluate assertions
        for a in assertions:
            # Point assertion
            time_sec = a.get("time_sec")
            if time_sec is not None and not a.get("_fired", False) and t_sec >= time_sec:
                a["_fired"] = True
                exp_p = a.get("expected_presence")
                exp_s = a.get("expected_state")
                exp_ph = a.get("expected_phrase")

                errs = []
                if exp_p and exp_p.lower() != classifier.presence.lower():
                    errs.append(f"presence expected '{exp_p}', got '{classifier.presence}'")
                if exp_s and exp_s.lower() != classifier.semantic_state.lower():
                    errs.append(f"state expected '{exp_s}', got '{classifier.semantic_state}'")
                if exp_ph and exp_ph.lower() != classifier.phrase_state.lower():
                    errs.append(f"phrase expected '{exp_ph}', got '{classifier.phrase_state}'")

                if errs:
                    failed_asserts += 1
                    failures.append(f"[{a.get('description', '')}] at {t_sec:.2f}s: {', '.join(errs)}")
                else:
                    passed_asserts += 1

            # Window assertion
            min_t = a.get("min_time_sec")
            max_t = a.get("max_time_sec")
            if min_t is not None and max_t is not None:
                if min_t <= t_sec <= max_t:
                    a["_window_checked"] = True
                    exp_p = a.get("expected_presence")
                    exp_s = a.get("expected_state")
                    if exp_p and exp_p.lower() != classifier.presence.lower():
                        if not a.get("_failed", False):
                            a["_failed"] = True
                            failed_asserts += 1
                            failures.append(f"[Window: {a.get('description', '')}] at {t_sec:.2f}s: presence '{classifier.presence}' != '{exp_p}'")
                    if exp_s and exp_s.lower() != classifier.semantic_state.lower():
                        if not a.get("_failed", False):
                            a["_failed"] = True
                            failed_asserts += 1
                            failures.append(f"[Window: {a.get('description', '')}] at {t_sec:.2f}s: state '{classifier.semantic_state}' != '{exp_s}'")
                    exp_ph = a.get("expected_phrase")
                    if exp_ph and exp_ph.lower() != classifier.phrase_state.lower():
                        if not a.get("_failed", False):
                            a["_failed"] = True
                            failed_asserts += 1
                            failures.append(f"[Window: {a.get('description', '')}] at {t_sec:.2f}s: phrase '{classifier.phrase_state}' != '{exp_ph}'")

    # Finalize window assertions
    for a in assertions:
        if a.get("min_time_sec") is not None and a.get("_window_checked", False) and not a.get("_failed", False):
            passed_asserts += 1

    print("-" * 80)
    print("\n🏁 ASSERTION RESULTS:")
    if failures:
        for f in failures:
            print(f"  ❌ {f}")
        print(f"\n❌ FAILED: {failed_asserts} failed, {passed_asserts} passed.")
    else:
        print(f"  ✅ All {passed_asserts} assertion checkpoints PASSED!")
        print(f"  Overall classification matches: '{meta.get('primary_label')}'\n")

    # =========================================================================
    # Continuous Benchmark Scoring & Error Metric Evaluation
    # =========================================================================
    if second_snapshots:
        total_eval_secs = len(second_snapshots)
        pres_errors = 0.0
        sem_errors = 0.0
        phr_errors = 0.0
        phr_eval_count = 0
        hotspots = []

        for sec, snap in sorted(second_snapshots.items()):
            # Find segment matching this second
            matching_seg = None
            for seg in segments:
                if seg.get("start_sec", 0.0) <= sec < seg.get("end_sec", 0.0):
                    matching_seg = seg
                    break

            if matching_seg:
                exp_state = matching_seg.get("state", "music").lower()
                exp_phrase = matching_seg.get("phrase", "verse").lower()
            else:
                exp_state = meta.get("primary_label", "silence").lower()
                exp_phrase = "steady"

            exp_presence = "quiet" if exp_state == "silence" else "sound"

            # Check if within slack window (1.5s) of any segment boundary
            in_slack = any(abs(sec - b) <= 1.5 for b in boundaries)

            # 1. Presence Error
            actual_pres = snap["presence"].lower()
            if actual_pres != exp_presence:
                p_err = 0.0 if in_slack else 1.0
                pres_errors += p_err

            # 2. Semantic Error
            actual_state = snap["state"].lower()
            if actual_state != exp_state:
                if in_slack:
                    s_err = 0.0
                elif (exp_state == "music" and actual_state == "silence") or (exp_state == "silence" and actual_state == "music"):
                    s_err = 1.0
                elif (exp_state == "music" and actual_state == "ambient_noise") or (exp_state == "ambient_noise" and actual_state == "music"):
                    s_err = 0.8
                elif exp_state in ("silence", "ambient_noise") and actual_state in ("silence", "ambient_noise"):
                    s_err = 0.25
                else:
                    s_err = 0.5
                sem_errors += s_err
                if s_err >= 0.5:
                    hotspots.append((sec, f"State: expected '{exp_state}', got '{actual_state}'"))

            # 3. Phrase Error (evaluated during music)
            actual_phrase = snap["phrase"].lower()
            if exp_state == "music":
                phr_eval_count += 1
                if actual_phrase == exp_phrase:
                    ph_err = 0.0
                elif in_slack:
                    ph_err = 0.0
                else:
                    # Close energy family
                    if (exp_phrase, actual_phrase) in [("verse", "chorus"), ("chorus", "verse")]:
                        ph_err = 0.25
                    elif exp_phrase in ("breakdown", "outro") and actual_phrase in ("breakdown", "outro"):
                        ph_err = 0.20
                    elif exp_phrase == "buildup" or actual_phrase == "buildup":
                        ph_err = 0.25
                    elif (exp_phrase == "chorus" and actual_phrase in ("breakdown", "outro")) or \
                         (exp_phrase in ("breakdown", "outro") and actual_phrase == "chorus"):
                        ph_err = 1.0
                    else:
                        ph_err = 0.50
                phr_errors += ph_err
                if ph_err >= 0.5:
                    hotspots.append((sec, f"Phrase: expected '{exp_phrase}', got '{actual_phrase}'"))

        presence_acc = max(0.0, 100.0 * (1.0 - (pres_errors / total_eval_secs)))
        semantic_acc = max(0.0, 100.0 * (1.0 - (sem_errors / total_eval_secs)))
        phrase_acc = max(0.0, 100.0 * (1.0 - (phr_errors / phr_eval_count))) if phr_eval_count > 0 else 100.0

        # Stability score: expect roughly len(segments) transitions; penalize excessive flapping
        expected_flips = max(1, len(segments))
        excess_flips = max(0, (state_flips + phrase_flips) - expected_flips * 2)
        stability_score = max(0.0, 100.0 - (excess_flips * 2.5))

        # Overall composite benchmark score (0% - 100%)
        overall_score = (0.35 * semantic_acc) + (0.25 * presence_acc) + (0.25 * phrase_acc) + (0.15 * stability_score)

        print("=" * 80)
        print("📊 CONTINUOUS BENCHMARK EVALUATION & ACCURACY METRICS")
        print("=" * 80)
        print(f"⏱️  Evaluated Timeline:      {total_eval_secs} seconds (1 Hz continuous grid)")
        print(f"🎯 Presence Accuracy:        {presence_acc:5.1f}%  (Sound vs Quiet detection)")
        print(f"🎶 Semantic State Accuracy:  {semantic_acc:5.1f}%  (Music vs Ambient vs Silence)")
        print(f"🎼 Phrase Alignment Score:   {phrase_acc:5.1f}%  (Verse / Chorus / Breakdown / Outro)")
        print(f"🌊 Stability / Anti-Flap:    {stability_score:5.1f}%  ({state_flips} state flips, {phrase_flips} phrase flips)")
        print("-" * 80)
        print(f"🏆 OVERALL BENCHMARK SCORE:  {overall_score:5.1f}%")
        print("=" * 80)

        if hotspots:
            print("\n⚠️  Top Error Hotspots (first 5 seconds of divergence):")
            for sec, desc in hotspots[:5]:
                m = sec // 60
                s = sec % 60
                print(f"   • [{m:02d}:{s:02d}] {desc}")
        print("")

    if failures:
        sys.exit(1)


if __name__ == "__main__":
    main()
