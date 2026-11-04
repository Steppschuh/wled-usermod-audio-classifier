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


class SoundEnergy:
    SILENCE = 0
    NOISE = 10
    OUTRO = 30
    BREAKDOWN = 40
    BUILDUP = 50
    INTRO = 60
    VERSE = 80
    CHORUS = 100


class RollingBuffer:
    def __init__(self, capacity: int):
        self.capacity = capacity
        self.values = [0.0] * capacity
        self.index = 0
        self.count = 0
        self.min_val = 0.0
        self.max_val = 0.0
        self.mean = 0.0
        self.rms = 0.0
        self.variance = 0.0
        self.std_dev = 0.0

    def reset(self):
        self.values = [0.0] * self.capacity
        self.index = 0
        self.count = 0
        self.min_val = self.max_val = self.mean = self.rms = self.variance = self.std_dev = 0.0

    def add(self, val: float):
        self.values[self.index] = float(val)
        self.index = (self.index + 1) % self.capacity
        if self.count < self.capacity:
            self.count += 1

    def get_latest(self) -> float:
        if self.count == 0:
            return 0.0
        last_idx = (self.index + self.capacity - 1) % self.capacity
        return self.values[last_idx]

    def calculate(self):
        if self.count == 0:
            self.min_val = self.max_val = self.mean = self.rms = self.variance = self.std_dev = 0.0
            return
        vals = self.values[:self.count]
        self.min_val = min(vals)
        self.max_val = max(vals)
        s = sum(vals)
        sum_sq = sum(v * v for v in vals)
        self.mean = s / self.count
        self.rms = math.sqrt(sum_sq / self.count)
        diff_sq = sum((v - self.mean) ** 2 for v in vals)
        self.variance = diff_sq / self.count
        self.std_dev = math.sqrt(self.variance)


class SimulatedAudioClassifier:
    def __init__(self, quiet_thresh=6.0, margin=4.0, on_delay_ms=1200, off_delay_ms=8000, music_hold_ms=30000):
        self.quiet_thresh = quiet_thresh
        self.margin = margin
        self.on_delay_ms = on_delay_ms
        self.off_delay_ms = off_delay_ms
        self.music_hold_ms = music_hold_ms

        self.current_level = 0.0
        self.average_1s_level = 0.0
        self.peak_1s_level = 0.0
        self.current_1s_peak = 0.0
        self.noise_floor = 2.0

        self.sum_squared_energy = 0.0
        self.frame_count_1s = 0
        self.last_1s_tick = 0
        self.has_1s_tick_start = False

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
        self.phrase_state = "verse"
        self.sound_energy = SoundEnergy.SILENCE
        self.music_confidence = 0.0
        self.last_phrase_change_ms = 0
        self.last_music_evidence_time = 0
        self.last_frame_time_ms = 0
        self.last_energy_update_time = 0

        # Rolling Statistical Buffers (Rave-Totem architecture)
        self.loudness_buffer = RollingBuffer(75)
        self.bass_buffer = RollingBuffer(75)
        self.low_freq_buffer = RollingBuffer(75)
        self.spread_buffer = RollingBuffer(75)
        self.beat_feature_buffer = RollingBuffer(75)
        self.beat_delay_buffer = RollingBuffer(8)

        self.beat_probability = 0.0

        self.max_loudness = 20.0
        self.decayed_max_loudness = 20.0
        self.last_max_loudness_time = 0
        self.average_loudness_percent = 0.0

        # Beat & Tempo (BPM) Tracking
        self.beat_times = [0] * 16
        self.beat_idx = 0
        self.beat_count = 0
        self.beat_regularity = 0.0

        self.last_beat_timestamp = 0
        self.beats_per_minute = 0
        self.raw_bpm = 0.0
        self.bpm_lock_count = 0
        self.duration_between_beats = 0
        self.duration_between_bars = 0
        self.duration_between_phrases = 0
        self.last_bpm_update_time = 0
        self.missed_beats = 0
        self.expected_beat_prob = 0.0

        # Drop Detection & 4/4 Phrase Grid Tracking
        self.last_drop_timestamp = 0
        self.last_expected_phrase_start = 0
        self.expected_beat_index = 0
        self.expected_bar_index = 0
        self.expected_phrase_index = 0

        # Energy & Phrasing Memory
        self.prev_bass_energy = 0.0
        self.short_bass_energy = 0.0
        self.long_bass_energy = 0.0

        # Outlier Detection & Revert Tracking
        self.tentative_prev_energy = SoundEnergy.SILENCE
        self.tentative_prev_phrase = "verse"
        self.tentative_prev_phrase_start_ms = 0
        self.tentative_transition_ms = 0
        self.is_tentative_transition = False
        self.tentative_beats_seen = 0

    @property
    def bpm(self) -> int:
        if self.semantic_state != "music":
            return 0
        return self.beats_per_minute

    def process_frame(self, t, lvl, raw, smth, pk, hz, mag, bands):
        now = t

        # Frame deduplication
        if self.last_frame_time_ms > 0 and now <= self.last_frame_time_ms:
            return
        self.last_frame_time_ms = now

        # 1. Smooth instantaneous level and track 1s peak
        self.current_level = (self.current_level * 0.7) + (lvl * 0.3)
        if lvl > self.current_1s_peak:
            self.current_1s_peak = lvl

        # 2. Accumulate energy for 1-second metric
        self.sum_squared_energy += (lvl * lvl)
        self.frame_count_1s += 1

        if not self.has_1s_tick_start:
            self.last_1s_tick = now
            self.has_1s_tick_start = True

        # 3. Periodic 1-second calculations & noise floor update
        if now - self.last_1s_tick >= 1000:
            self._update_1s_metrics(now)
            self.last_1s_tick = now

        # 4. Presence State Machine
        self._update_presence(now)

        # 5. Experimental semantic classification & phrasing
        if self.presence == "quiet":
            hangover_active = (self.last_music_evidence_time > 0 and (now - self.last_music_evidence_time <= self.music_hold_ms))
            if not hangover_active:
                self.semantic_state = "silence"
                self.phrase_state = "verse"
                self.music_confidence = 0.0
                self.beats_per_minute = 0
                self.raw_bpm = 0.0
                self.bpm_lock_count = 0
                self.duration_between_beats = 0
                self.duration_between_bars = 0
                self.duration_between_phrases = 0
                self.beat_probability = 0.0
            else:
                self._update_music_and_phrasing(now, lvl, raw, smth, pk, hz, mag, bands)
        else:
            self._update_music_and_phrasing(now, lvl, raw, smth, pk, hz, mag, bands)

    def _update_1s_metrics(self, now):
        if self.frame_count_1s > 0:
            self.average_1s_level = math.sqrt(self.sum_squared_energy / self.frame_count_1s)
            self.peak_1s_level = self.current_1s_peak
        else:
            self.average_1s_level = self.current_level
            self.peak_1s_level = self.current_level
        self.sum_squared_energy = 0.0
        self.current_1s_peak = 0.0
        self.frame_count_1s = 0

        # Only adapt noise floor when QUIET and not pending on
        if self.presence == "quiet" and not self.is_pending_on:
            self._update_noise_floor(self.average_1s_level)

    def _update_noise_floor(self, new_1s_sample):
        bin_idx = int(round(new_1s_sample))
        if bin_idx >= 101:
            bin_idx = 100
        if bin_idx < 0:
            bin_idx = 0

        if self.history_count == 300:
            old_bin = self.history_ring[self.history_idx]
            if self.histogram[old_bin] > 0:
                self.histogram[old_bin] -= 1
        else:
            self.history_count += 1

        self.history_ring[self.history_idx] = bin_idx
        self.histogram[bin_idx] += 1
        self.history_idx = (self.history_idx + 1) % 300

        target_rank = (self.history_count * 10) // 100
        if target_rank == 0:
            target_rank = 1

        cum_sum = 0
        p10 = 0
        for b in range(101):
            cum_sum += self.histogram[b]
            if cum_sum >= target_rank:
                p10 = b
                break
        self.noise_floor = float(p10)

    def _update_presence(self, now):
        threshold = self.quiet_thresh
        dynamic_threshold = self.noise_floor + self.margin
        if dynamic_threshold > threshold:
            threshold = dynamic_threshold

        if self.current_level > threshold:
            self.is_pending_off = False
            if self.presence == "quiet":
                if not self.is_pending_on:
                    self.is_pending_on = True
                    self.pending_on_time = now
                if now - self.pending_on_time >= self.on_delay_ms:
                    self.presence = "sound"
                    self.is_pending_on = False
        else:
            self.is_pending_on = False
            if self.presence == "sound":
                if not self.is_pending_off:
                    self.is_pending_off = True
                    self.pending_off_time = now
                if now - self.pending_off_time >= self.off_delay_ms:
                    self.presence = "quiet"
                    self.is_pending_off = False

    def _update_music_and_phrasing(self, now, lvl, raw, smth, pk, hz, mag, bands):
        # 1. Band Energy Distribution & Spectral Flux
        num_bands = len(bands)
        total_band_energy = sum(bands[:16])
        bass_energy = sum(bands[:4])

        bass_ratio = (bass_energy / total_band_energy) if total_band_energy > 5.0 else 0.0
        bass_percent = (bass_energy / 4.0) * (100.0 / 255.0)
        spread = abs(raw - smth) / 2.55

        # Positive Bass Spectral Flux (kick drum transient onset)
        b0 = bands[0] if num_bands > 0 else 0
        b1 = bands[1] if num_bands > 1 else 0
        raw_bass = float(b0) * 1.3 + float(b1) * 0.7
        bass_flux = (raw_bass - self.prev_bass_energy) if raw_bass > self.prev_bass_energy else 0.0
        self.prev_bass_energy = raw_bass

        # 2. Feed 1.5s Rolling Feature Buffers (Rave-Totem architecture)
        self.loudness_buffer.add(lvl)
        self.loudness_buffer.calculate()
        self.bass_buffer.add(bass_percent)
        self.bass_buffer.calculate()
        low_freq_val = max(float(b0), float(b1))
        self.low_freq_buffer.add(low_freq_val)
        self.low_freq_buffer.calculate()
        self.spread_buffer.add(spread)
        self.spread_buffer.calculate()

        # 3. Track Decayed Maximum Loudness (5-minute peak hold with decay)
        if self.loudness_buffer.rms > self.decayed_max_loudness:
            self.max_loudness = self.loudness_buffer.rms
            self.decayed_max_loudness = self.max_loudness
            self.last_max_loudness_time = now
        else:
            dt = now - self.last_max_loudness_time
            if dt > 300000:
                decay = 1.0 - (float(dt - 300000) / 300000.0)
                if decay < 0.0:
                    decay = 0.0
                min_decayed = max(self.max_loudness * 0.5, 15.0)
                self.decayed_max_loudness = max(self.max_loudness * decay, min_decayed)
        self.average_loudness_percent = (100.0 * self.loudness_buffer.rms / self.decayed_max_loudness) if self.decayed_max_loudness > 0.1 else 0.0

        # 4. Z-Score Onset Feature Extraction (beatFeature)
        bf2 = self.bass_buffer.get_latest() - self.bass_buffer.mean
        bf2 -= (0.5 * self.bass_buffer.std_dev)
        if bf2 < 0.0:
            bf2 = 0.0
        bf2 = bf2 * bf2 - (2.0 * self.bass_buffer.std_dev)
        if bf2 < 0.0:
            bf2 = 0.0
        bf2 = math.sqrt(bf2)

        beat_feature = (bf2 * 0.3) + (bass_flux * 0.7)

        eff_raw_vol = max(raw, smth, lvl * 2.55)

        b2 = bands[2] if num_bands > 2 else 0
        b3 = bands[3] if num_bands > 3 else 0
        b4 = bands[4] if num_bands > 4 else 0

        # Middleground bass onset requirement:
        # Accept punchy sub-bass (b0 >= 35) OR punchy mid-bass (b1 >= 40) OR combined bass (b0 + b1 >= 60)
        has_bass = (b0 >= 35 or b1 >= 40 or (b0 + b1) >= 60) and (
            bass_ratio >= 0.22 or (bass_flux >= 20.0 and bass_ratio >= 0.18)
        )

        mid_avg = float(b2 + b3 + b4) / 3.0
        bass_avg = float(b0 + b1) / 2.0
        is_mid_overpowering = (mid_avg > bass_avg * 2.2)

        if not has_bass:
            beat_feature = 0.0
        elif is_mid_overpowering:
            beat_feature *= 0.25
        elif eff_raw_vol < 25.0:
            beat_feature *= 0.30
        elif spread > bass_percent * 1.2 and bass_ratio < 0.35:
            beat_feature *= 0.20
        elif pk and (hz <= 140.0 or hz == 0.0):
            if beat_feature > 8.0:
                beat_feature *= 1.25
        if beat_feature > 100.0:
            beat_feature = 100.0

        self.beat_feature_buffer.add(beat_feature)
        self.beat_feature_buffer.calculate()

        # 5. Phase-Locked Beat Expectation & Dynamic Adaptive Thresholding
        duration_since_last_beat = (now - self.last_beat_timestamp) if self.last_beat_timestamp > 0 else 10000

        if self.duration_between_beats > 0:
            last_beat_ref = self.last_bpm_update_time if (now - self.last_bpm_update_time < 10 * self.duration_between_beats) else self.last_beat_timestamp
            next_expected_beat = last_beat_ref + self.duration_between_beats
            delta_next = next_expected_beat - now
            self.missed_beats = 0

            if delta_next < 0:
                self.missed_beats = 2 + int((now - next_expected_beat) // self.duration_between_beats)
                next_expected_beat = last_beat_ref + (self.missed_beats * self.duration_between_beats)
                delta_next = next_expected_beat - now
            if delta_next > int(self.duration_between_beats // 2):
                delta_next = int(self.duration_between_beats) - delta_next
            if delta_next < 0:
                delta_next = -delta_next

            self.expected_beat_prob = float(self.duration_between_beats - delta_next) / float(self.duration_between_beats)
            self.expected_beat_prob = max(0.0, min(1.0, self.expected_beat_prob ** 5.0))
        else:
            self.expected_beat_prob = 0.0
            self.missed_beats = 0

        BEAT_THRESH_MAX = 40.0
        BEAT_THRESH_DEF = 18.0
        BEAT_THRESH_MIN = 12.0
        MIN_BEAT_DELAY = 280

        beat_thresh = BEAT_THRESH_MAX
        is_offbeat = (self.duration_between_beats > 0 and duration_since_last_beat < int(self.duration_between_beats * 0.72))
        enough_delay = (duration_since_last_beat >= MIN_BEAT_DELAY and not is_offbeat)

        if enough_delay:
            recency = float(duration_since_last_beat - MIN_BEAT_DELAY) / 120.0 if duration_since_last_beat > MIN_BEAT_DELAY else 0.0
            if recency > 1.0:
                recency = 1.0
            beat_thresh -= (BEAT_THRESH_MAX - BEAT_THRESH_DEF) * recency
            if beat_thresh < BEAT_THRESH_DEF:
                beat_thresh = BEAT_THRESH_DEF
            beat_thresh -= (BEAT_THRESH_DEF - BEAT_THRESH_MIN) * self.expected_beat_prob
            if beat_thresh < BEAT_THRESH_MIN:
                beat_thresh = BEAT_THRESH_MIN

        # Low-frequency variance check: reject static drones/pads while accepting real kicks
        span = self.low_freq_buffer.max_val - self.low_freq_buffer.min_val
        has_low_freq_variance = (self.low_freq_buffer.count < 8) or (
            (self.low_freq_buffer.std_dev >= 3.0 and span >= 12.0) or (self.low_freq_buffer.std_dev >= 4.5)
        )

        beat_detected = has_bass and has_low_freq_variance and (beat_feature > beat_thresh and enough_delay)

        if beat_detected:
            self.beat_times[self.beat_idx] = now
            self.beat_idx = (self.beat_idx + 1) % 16
            if self.beat_count < 16:
                self.beat_count += 1

            in_beat_phrase = self.phrase_state in ("verse", "chorus", "buildup")
            has_recent_bpm = (self.beats_per_minute > 0 and self.duration_between_beats > 0 and (now - self.last_bpm_update_time) <= 8 * self.duration_between_beats)
            if (self.beat_probability >= 0.15 or has_recent_bpm or (self.beats_per_minute == 0 and self.beat_count >= 2)) and in_beat_phrase:
                if self.last_beat_timestamp > 0 and duration_since_last_beat <= 2000:
                    num_beats = int(round(float(duration_since_last_beat) / float(self.duration_between_beats))) if self.duration_between_beats > 0 else 1
                    if num_beats == 0:
                        num_beats = 1
                    norm_interval_ms = duration_since_last_beat // num_beats

                    if 250 <= norm_interval_ms <= 750:
                        self.beat_delay_buffer.add(float(norm_interval_ms // 10))
                        self.beat_delay_buffer.calculate()
                        if self.beat_delay_buffer.count >= 4 and self.beat_delay_buffer.std_dev < 14.0:
                            avg_interval_ms = self.beat_delay_buffer.mean * 10.0
                            if 250.0 <= avg_interval_ms <= 1000.0:
                                cur_bpm = int(round(60000.0 / avg_interval_ms))

                                if 40 <= cur_bpm <= 400:
                                    if self.raw_bpm <= 0.0 or self.beats_per_minute == 0:
                                        while cur_bpm > 175:
                                            cur_bpm //= 2
                                        while cur_bpm < 85:
                                            cur_bpm *= 2
                                        self.raw_bpm = float(cur_bpm)
                                        self.beats_per_minute = cur_bpm
                                        self.bpm_lock_count = 3
                                    else:
                                        cand1 = float(cur_bpm)
                                        cand2 = float(cur_bpm) * 2.0
                                        cand_half = float(cur_bpm) * 0.5

                                        diff1 = abs(cand1 - self.raw_bpm)
                                        diff2 = abs(cand2 - self.raw_bpm)
                                        diff_half = abs(cand_half - self.raw_bpm)

                                        best_cand = cand1
                                        min_diff = diff1

                                        if diff2 < min_diff:
                                            min_diff = diff2
                                            best_cand = cand2
                                        if diff_half < min_diff:
                                            min_diff = diff_half
                                            best_cand = cand_half

                                        max_diff = max(10.0, self.raw_bpm * 0.12)

                                        if min_diff <= max_diff:
                                            alpha = 0.35 if self.bpm_lock_count < 8 else 0.10
                                            self.raw_bpm = (self.raw_bpm * (1.0 - alpha)) + (best_cand * alpha)
                                            if self.bpm_lock_count < 30:
                                                self.bpm_lock_count += 1
                                        else:
                                            if self.bpm_lock_count > 1:
                                                self.bpm_lock_count -= 1
                                            else:
                                                while best_cand > 175.0:
                                                    best_cand *= 0.5
                                                while best_cand < 85.0:
                                                    best_cand *= 2.0
                                                self.raw_bpm = best_cand
                                                self.bpm_lock_count = 1

                                        if self.bpm_lock_count >= 3:
                                            target_bpm = int(round(self.raw_bpm))
                                            if target_bpm != self.beats_per_minute:
                                                self.beats_per_minute = target_bpm
                                        else:
                                            self.beats_per_minute = int(round(self.raw_bpm))

                                    self.duration_between_beats = int(round(60000.0 / float(self.beats_per_minute)))
                                    self.duration_between_bars = self.duration_between_beats * 4
                                    self.duration_between_phrases = self.duration_between_bars * 8
                                    self.last_bpm_update_time = now
                                    self.beat_regularity = 0.90
            else:
                self.beat_delay_buffer.reset()

            # Inter-beat regularity from circular history
            if self.beat_count >= 4:
                intervals = []
                for i in range(1, self.beat_count):
                    curr_idx = (self.beat_idx + 16 - i) % 16
                    prev_idx = (self.beat_idx + 16 - i - 1) % 16
                    dt = self.beat_times[curr_idx] - self.beat_times[prev_idx]
                    if 250 <= dt <= 1200:
                        intervals.append(float(dt))

                if len(intervals) >= 3:
                    mean = sum(intervals) / len(intervals)
                    var = sum((x - mean) ** 2 for x in intervals) / len(intervals)
                    stddev = math.sqrt(var)
                    cv = stddev / (mean + 1.0)

                    if cv < 0.25:
                        if self.beat_regularity < 0.85:
                            self.beat_regularity = 0.85
                    elif cv < 0.40:
                        if self.beat_regularity < 0.55:
                            self.beat_regularity = 0.55

            self.last_beat_timestamp = now
            duration_since_last_beat = 0
        else:
            self.beat_regularity *= 0.995
            if duration_since_last_beat > 3000:
                self.beat_count = 0
                self.beat_delay_buffer.reset()
            if self.semantic_state != "music":
                self.beats_per_minute = 0
                self.raw_bpm = 0.0
                self.bpm_lock_count = 0
                self.duration_between_beats = 0
                self.duration_between_bars = 0
                self.duration_between_phrases = 0

        # 6. Drop Detection & 4/4 Phrase Grid Tracking
        drop_feature = self.bass_buffer.get_latest() - self.bass_buffer.mean
        drop_feature -= (1.25 * self.bass_buffer.std_dev)
        if drop_feature < 0.0:
            drop_feature = 0.0
        drop_feature = drop_feature * drop_feature - (2.0 * self.bass_buffer.std_dev)
        if drop_feature < 0.0:
            drop_feature = 0.0
        drop_feature = math.sqrt(drop_feature)
        drop_feature += (self.spread_buffer.get_latest() - self.spread_buffer.mean) * 0.5

        duration_since_last_drop = (now - self.last_drop_timestamp) if self.last_drop_timestamp > 0 else 999999
        drop_thresh = 4.0 * self.bass_buffer.std_dev

        if self.last_phrase_change_ms > 0 and self.duration_between_phrases > 0:
            time_since_change = (now - self.last_phrase_change_ms) if now >= self.last_phrase_change_ms else 0
            phrase_offset = time_since_change % self.duration_between_phrases
            dist_to_boundary = phrase_offset if (phrase_offset < self.duration_between_phrases // 2) else (self.duration_between_phrases - phrase_offset)
            if dist_to_boundary <= self.duration_between_bars:
                factor = 1.0 - (float(dist_to_boundary) / float(self.duration_between_bars))
                drop_thresh *= (1.0 - 0.35 * factor)
            else:
                drop_thresh *= 1.35
        elif self.last_expected_phrase_start > 0 and self.duration_between_phrases > 0:
            delta_phrase = now - (self.last_expected_phrase_start + self.duration_between_phrases)
            phrase_factor = (500.0 - abs(float(delta_phrase))) / 500.0
            if phrase_factor < 0.0:
                phrase_factor = 0.0
            if phrase_factor > 1.0:
                phrase_factor = 1.0
            drop_thresh *= (1.0 - 0.25 * phrase_factor)

        if self.sound_energy == SoundEnergy.CHORUS:
            drop_thresh *= 2.0
        elif self.sound_energy < SoundEnergy.VERSE:
            drop_thresh *= 0.75

        if drop_thresh < 15.0:
            drop_thresh = 15.0
        if drop_feature > drop_thresh and duration_since_last_drop > 4000:
            self.last_drop_timestamp = now
            duration_since_last_drop = 0

        if self.last_drop_timestamp > 0 and self.duration_between_beats > 0:
            if self.last_expected_phrase_start == 0 or self.last_drop_timestamp == now:
                self.last_expected_phrase_start = self.last_drop_timestamp
            dur_since_phrase = now - self.last_expected_phrase_start
            if dur_since_phrase >= self.duration_between_phrases:
                dur_since_phrase %= self.duration_between_phrases
            self.last_expected_phrase_start = now - dur_since_phrase

            beats_since_drop = (now - self.last_drop_timestamp) // self.duration_between_beats
            self.expected_beat_index = beats_since_drop % 4
            self.expected_bar_index = (beats_since_drop // 4) % 8
            self.expected_phrase_index = beats_since_drop // 32

        # 7. Musical Evidence & Confidence Filter
        has_musical_evidence = False
        instant_confidence = 0.0

        if self.beat_regularity > 0.50 and (now - self.last_beat_timestamp <= 3000):
            has_musical_evidence = True
            instant_confidence = self.beat_regularity
        elif bass_ratio > 0.35 and self.current_level > (self.noise_floor + 5.0):
            has_musical_evidence = True
            instant_confidence = 0.75

        if has_musical_evidence:
            self.last_music_evidence_time = now
            self.music_confidence = (self.music_confidence * 0.85) + (instant_confidence * 0.15)
            if self.music_confidence > 1.0:
                self.music_confidence = 1.0
        else:
            self.music_confidence *= 0.999

        # 8. Acoustic State Machine Transitions with Hangover Protection
        if self.semantic_state == "music":
            hangover_active = (self.last_music_evidence_time > 0 and (now - self.last_music_evidence_time <= self.music_hold_ms))
            if not hangover_active:
                if self.presence == "sound":
                    self.semantic_state = "ambient_noise"
                    self.sound_energy = SoundEnergy.NOISE
                else:
                    self.semantic_state = "silence"
                    self.sound_energy = SoundEnergy.SILENCE
                self.phrase_state = "verse"
        else:
            if self.music_confidence >= 0.60 or (self.beat_regularity >= 0.70 and self.current_level > self.noise_floor + 5.0) or (self.beats_per_minute > 0 and self.current_level > self.noise_floor + 4.0):
                self.semantic_state = "music"
                self.last_music_evidence_time = now
                if self.music_confidence < 0.70:
                    self.music_confidence = 0.70
            elif self.presence == "sound":
                self.semantic_state = "ambient_noise"
                self.sound_energy = SoundEnergy.NOISE
            else:
                self.semantic_state = "silence"
                self.sound_energy = SoundEnergy.SILENCE

        # 9. Phrasing & SoundEnergy State Machine (Rave-Totem Algorithm)
        if self.semantic_state == "music":
            energy_before_update = self.sound_energy
            phrase_before_update = self.phrase_state

            self.short_bass_energy = (self.short_bass_energy * 0.92) + (bass_energy * 0.08)
            self.long_bass_energy = (self.long_bass_energy * 0.99) + (bass_energy * 0.01)
            bass_trend = (self.short_bass_energy / self.long_bass_energy) if self.long_bass_energy > 2.0 else 1.0

            phrase_dur = self.duration_between_phrases if self.duration_between_phrases > 0 else 16000
            bar_dur = self.duration_between_bars if self.duration_between_bars > 0 else 2000
            beat_dur = self.duration_between_beats if self.duration_between_beats > 0 else 500
            outlier_window_ms = (bar_dur + beat_dur) if bar_dur > 0 else 2500
            if outlier_window_ms < 1500:
                outlier_window_ms = 1500
            if outlier_window_ms > 3500:
                outlier_window_ms = 3500

            # Outlier Detection & Revert Evaluation
            if self.is_tentative_transition:
                dt_tentative = (now - self.tentative_transition_ms) if now >= self.tentative_transition_ms else 0
                if beat_detected:
                    self.tentative_beats_seen += 1

                if dt_tentative > outlier_window_ms:
                    self.is_tentative_transition = False
                else:
                    if self.sound_energy >= SoundEnergy.VERSE and self.tentative_prev_energy < SoundEnergy.VERSE:
                        if self.tentative_beats_seen >= 2 or (self.peak_1s_level >= 48.0 and self.bass_buffer.mean >= 20.0):
                            self.is_tentative_transition = False
                        else:
                            gap_since_beat = (now - self.last_beat_timestamp) if self.last_beat_timestamp > 0 else dt_tentative
                            no_followup_beat = (gap_since_beat >= int(beat_dur * 2.2))
                            energy_dropped = (self.peak_1s_level < 42.0 and self.bass_buffer.mean < 15.0)

                            if no_followup_beat and energy_dropped:
                                self.sound_energy = self.tentative_prev_energy
                                self.phrase_state = self.tentative_prev_phrase
                                self.last_phrase_change_ms = self.tentative_prev_phrase_start_ms
                                self.last_expected_phrase_start = self.tentative_prev_phrase_start_ms
                                self.last_energy_update_time = now
                                self.is_tentative_transition = False
                                energy_before_update = self.sound_energy
                                phrase_before_update = self.phrase_state
                    elif self.sound_energy < self.tentative_prev_energy and self.tentative_prev_energy >= SoundEnergy.VERSE:
                        bass_resumed = (has_bass or bass_ratio >= 0.25 or self.peak_1s_level >= 40.0)
                        if beat_detected and bass_resumed:
                            self.sound_energy = self.tentative_prev_energy
                            self.phrase_state = self.tentative_prev_phrase
                            self.last_phrase_change_ms = self.tentative_prev_phrase_start_ms
                            self.last_expected_phrase_start = self.tentative_prev_phrase_start_ms
                            self.last_energy_update_time = now
                            self.is_tentative_transition = False
                            energy_before_update = self.sound_energy
                            phrase_before_update = self.phrase_state

            time_since_phrase_change = (now - self.last_phrase_change_ms) if (self.last_phrase_change_ms > 0 and now >= self.last_phrase_change_ms) else phrase_dur
            phrase_offset = (time_since_phrase_change % self.duration_between_phrases) if self.duration_between_phrases > 0 else 0
            dist_to_boundary = phrase_offset if (phrase_offset < phrase_dur // 2) else (phrase_dur - phrase_offset)
            near_phrase_boundary = (self.duration_between_bars > 0) and (dist_to_boundary <= self.duration_between_bars)

            drop_in_last_phrase = (now - self.last_drop_timestamp <= phrase_dur)
            beat_in_last_phrase = (now - self.last_beat_timestamp <= phrase_dur)
            beat_in_last_bar = (now - self.last_beat_timestamp <= bar_dur)
            constant_beats = (self.beats_per_minute > 0) and (now - self.last_bpm_update_time <= 2 * bar_dur)
            bars_without_beats = (duration_since_last_beat // self.duration_between_bars) if self.duration_between_bars > 0 else (duration_since_last_beat // 2000)
            bars_since_energy_update = ((now - self.last_energy_update_time) // bar_dur) if bar_dur > 0 else 0

            is_volume_collapsed = (self.current_level < (self.noise_floor + 5.0) and self.average_1s_level < (self.noise_floor + 8.0)) or (self.peak_1s_level < 28.0)
            no_beats_recent = (duration_since_last_beat > 1200 or bars_without_beats >= 1)
            has_active_beat_groove = (duration_since_last_beat <= (bar_dur * 3 // 2)) and (self.beat_probability >= 0.15 or constant_beats or beat_detected)

            updated_energy = self.sound_energy

            if is_volume_collapsed and no_beats_recent:
                if self.sound_energy in (SoundEnergy.BREAKDOWN, SoundEnergy.OUTRO) and (bars_since_energy_update >= 8 or self.current_level < self.noise_floor + 3.0):
                    updated_energy = SoundEnergy.OUTRO
                else:
                    updated_energy = SoundEnergy.BREAKDOWN
            elif (self.peak_1s_level >= 48.0 or self.average_1s_level >= 36.0 or drop_in_last_phrase) and (beat_in_last_phrase or beat_detected):
                updated_energy = SoundEnergy.CHORUS
            elif has_active_beat_groove and (self.peak_1s_level >= 28.0 or self.average_1s_level >= 20.0):
                if self.sound_energy == SoundEnergy.CHORUS:
                    if not self.is_tentative_transition and time_since_phrase_change < phrase_dur and self.peak_1s_level >= 38.0:
                        updated_energy = SoundEnergy.CHORUS
                    elif near_phrase_boundary and (not drop_in_last_phrase or bass_trend < 0.90) and (self.peak_1s_level < 52.0):
                        updated_energy = SoundEnergy.VERSE
                    else:
                        updated_energy = SoundEnergy.CHORUS
                else:
                    updated_energy = SoundEnergy.VERSE
            else:
                if self.sound_energy in (SoundEnergy.CHORUS, SoundEnergy.BREAKDOWN, SoundEnergy.OUTRO) or is_volume_collapsed or no_beats_recent or bass_trend < 0.50 or (bass_ratio < 0.15 and self.current_level > self.noise_floor + 2.0):
                    if self.sound_energy in (SoundEnergy.BREAKDOWN, SoundEnergy.OUTRO) and (bars_since_energy_update >= 8 or self.current_level < self.noise_floor + 3.0):
                        updated_energy = SoundEnergy.OUTRO
                    else:
                        updated_energy = SoundEnergy.BREAKDOWN
                elif bass_trend > 1.15 or (now - self.last_drop_timestamp > phrase_dur and bars_without_beats <= 4 and self.peak_1s_level >= 24.0):
                    updated_energy = SoundEnergy.BUILDUP
                else:
                    updated_energy = SoundEnergy.INTRO

            immediate_beat_transition = beat_detected and (updated_energy != self.sound_energy)
            if (now - self.last_energy_update_time >= 1000) or immediate_beat_transition:
                if updated_energy > self.sound_energy:
                    if self.sound_energy == SoundEnergy.OUTRO:
                        if drop_in_last_phrase or self.peak_1s_level >= 52.0:
                            self.sound_energy = SoundEnergy.CHORUS
                        elif bars_since_energy_update >= 2:
                            self.sound_energy = SoundEnergy.VERSE
                    elif self.sound_energy == SoundEnergy.INTRO:
                        if drop_in_last_phrase or self.peak_1s_level >= 52.0:
                            self.sound_energy = SoundEnergy.CHORUS
                        elif bars_since_energy_update >= 2:
                            self.sound_energy = SoundEnergy.VERSE
                    elif self.sound_energy == SoundEnergy.VERSE:
                        if drop_in_last_phrase or self.peak_1s_level >= 52.0:
                            self.sound_energy = SoundEnergy.CHORUS
                        elif bass_trend > 1.25:
                            self.sound_energy = SoundEnergy.BUILDUP
                    elif self.sound_energy == SoundEnergy.BUILDUP:
                        if drop_in_last_phrase or (bass_trend > 1.4 and (beat_in_last_bar or beat_detected)) or self.peak_1s_level >= 52.0:
                            self.sound_energy = SoundEnergy.CHORUS
                    else:
                        self.sound_energy = updated_energy
                    self.last_energy_update_time = now
                elif updated_energy < self.sound_energy:
                    if self.sound_energy == SoundEnergy.CHORUS:
                        if not self.is_tentative_transition and time_since_phrase_change < phrase_dur and not is_volume_collapsed:
                            if updated_energy in (SoundEnergy.BREAKDOWN, SoundEnergy.OUTRO) and (is_volume_collapsed or bars_without_beats >= 1):
                                self.sound_energy = SoundEnergy.BREAKDOWN
                                self.last_energy_update_time = now
                        else:
                            if beat_in_last_bar and has_active_beat_groove:
                                self.sound_energy = SoundEnergy.VERSE
                            else:
                                self.sound_energy = SoundEnergy.BREAKDOWN
                            self.last_energy_update_time = now
                    elif self.sound_energy == SoundEnergy.VERSE:
                        if updated_energy == SoundEnergy.BUILDUP:
                            self.sound_energy = SoundEnergy.BUILDUP
                            self.last_energy_update_time = now
                        elif updated_energy in (SoundEnergy.BREAKDOWN, SoundEnergy.OUTRO) or bass_trend < 0.50 or bars_without_beats >= 1 or is_volume_collapsed:
                            self.sound_energy = updated_energy
                            self.last_energy_update_time = now
                    elif self.sound_energy == SoundEnergy.BUILDUP:
                        if bars_since_energy_update >= 4 or self.current_level < self.noise_floor + 3.0 or is_volume_collapsed:
                            self.sound_energy = SoundEnergy.BREAKDOWN
                            self.last_energy_update_time = now
                    elif self.sound_energy == SoundEnergy.BREAKDOWN:
                        if bars_since_energy_update >= 8 or self.current_level < self.noise_floor + 3.0:
                            self.sound_energy = SoundEnergy.OUTRO
                            self.last_energy_update_time = now
                        elif bars_since_energy_update >= 4:
                            self.sound_energy = SoundEnergy.INTRO
                            self.last_energy_update_time = now
                    else:
                        self.sound_energy = updated_energy
                        self.last_energy_update_time = now

            if self.sound_energy == SoundEnergy.CHORUS:
                self.phrase_state = "chorus"
            elif self.sound_energy == SoundEnergy.BUILDUP:
                self.phrase_state = "buildup"
            elif self.sound_energy == SoundEnergy.BREAKDOWN:
                self.phrase_state = "breakdown"
            elif self.sound_energy == SoundEnergy.OUTRO:
                self.phrase_state = "outro"
            elif self.sound_energy == SoundEnergy.INTRO:
                self.phrase_state = "intro"
            elif self.sound_energy == SoundEnergy.VERSE:
                self.phrase_state = "verse"
            else:
                self.phrase_state = "verse"

            if self.phrase_state != phrase_before_update:
                if not self.is_tentative_transition:
                    self.tentative_prev_energy = energy_before_update
                    self.tentative_prev_phrase = phrase_before_update
                    self.tentative_prev_phrase_start_ms = self.last_phrase_change_ms if self.last_phrase_change_ms > 0 else now
                    self.tentative_transition_ms = now
                    self.is_tentative_transition = True
                    self.tentative_beats_seen = 1 if beat_detected else 0
                self.last_phrase_change_ms = now
                self.last_expected_phrase_start = now
        else:
            self.phrase_state = "verse"
            self.is_tentative_transition = False

        # 10. Dedicated Music Beat Probability
        if self.presence == "quiet" or self.semantic_state != "music":
            self.beat_probability = 0.0
        else:
            span = self.low_freq_buffer.max_val - self.low_freq_buffer.min_val
            m = self.low_freq_buffer.mean
            std_dev = self.low_freq_buffer.std_dev
            has_bass_energy = (b0 >= 28 or b1 >= 32 or m >= 18.0 or span >= 20.0)
            dt_beat = (now - self.last_beat_timestamp) if self.last_beat_timestamp > 0 else 99999

            if not has_bass_energy or std_dev < 2.5 or span < 10.0 or dt_beat > 2500:
                self.beat_probability *= 0.75
                if self.beat_probability < 0.02:
                    self.beat_probability = 0.0
            else:
                var_factor = max(0.0, min(1.0, (std_dev - 2.5) / 5.0))
                span_factor = max(0.0, min(1.0, (span - 12.0) / 25.0))
                level_factor = max(0.0, min(1.0, (m - 12.0) / 28.0))

                recency = 0.0
                if dt_beat <= 1200:
                    recency = 1.0
                elif dt_beat <= 2200:
                    recency = 1.0 - (float(dt_beat - 1200) / 1000.0)

                target_prob = var_factor * (span_factor * 0.40 + level_factor * 0.25 + recency * 0.35)
                recent_beats = sum(1 for bt in self.beat_times if bt > 0 and (now - bt) <= 3000)
                if recent_beats == 0:
                    target_prob = 0.0
                elif recent_beats == 1:
                    target_prob = min(target_prob, 0.15)
                else:
                    if var_factor > 0.35 and span_factor > 0.35 and recency > 0.5:
                        boost = 0.70 + 0.30 * var_factor
                        if boost > target_prob:
                            target_prob = boost
                target_prob = max(0.0, min(1.0, target_prob))

                if target_prob < self.beat_probability:
                    self.beat_probability = (self.beat_probability * 0.75) + (target_prob * 0.25)
                else:
                    self.beat_probability = (self.beat_probability * 0.85) + (target_prob * 0.15)
                if self.beat_probability < 0.02:
                    self.beat_probability = 0.0

        # 11. BPM Gating: BPM is held during music state and cleared when leaving music
        if self.semantic_state != "music":
            self.beats_per_minute = 0
            self.raw_bpm = 0.0
            self.bpm_lock_count = 0
            self.beat_delay_buffer.reset()
            self.duration_between_beats = 0
            self.duration_between_bars = 0
            self.duration_between_phrases = 0


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

    print(f"{'Time':<8} | {'Level':<6} | {'Floor':<5} | {'Presence':<8} | {'State':<13} | {'Phrase':<9} | {'Conf':<6} | {'BPM':<7} | {'Beats':<6}")
    print("-" * 88)

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
    last_raw_t = None

    for f in frames:
        raw_t = f["t"]
        if raw_t == last_raw_t:
            continue
        last_raw_t = raw_t

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
                "bpm": classifier.bpm,
                "beat_prob": classifier.beat_probability,
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
            bpm_str = f"{classifier.bpm} BPM" if classifier.bpm > 0 else "-- BPM"
            state_disp = classifier.semantic_state.replace('_', ' ').title()
            phrase_disp = classifier.phrase_state.title()
            print(
                f"{format_duration(t_sec):<8} | "
                f"{classifier.current_level:5.1f}% | "
                f"{classifier.noise_floor:4.1f}% | "
                f"{classifier.presence.upper():<8} | "
                f"{state_disp:<13} | "
                f"{phrase_disp:<9} | "
                f"{classifier.music_confidence:4.2f} {conf_bar:<5} | "
                f"{bpm_str:<7} | "
                f"{classifier.beat_count:<6}"
            )

        def norm_str(s: str) -> str:
            return s.replace(" ", "_").replace("-", "_").lower() if s else ""

        # Evaluate assertions
        for a in assertions:
            # Point assertion
            time_sec = a.get("time_sec")
            if time_sec is not None and not a.get("_fired", False) and t_sec >= time_sec:
                a["_fired"] = True
                exp_p = a.get("expected_presence")
                exp_s = a.get("expected_state")
                exp_ph = a.get("expected_phrase")
                min_bpm = a.get("min_bpm")
                max_bpm = a.get("max_bpm")
                min_beat_prob = a.get("min_beat_prob")
                max_beat_prob = a.get("max_beat_prob")

                errs = []
                if exp_p and norm_str(exp_p) != norm_str(classifier.presence):
                    errs.append(f"presence expected '{exp_p}', got '{classifier.presence}'")
                if exp_s and norm_str(exp_s) != norm_str(classifier.semantic_state):
                    errs.append(f"state expected '{exp_s}', got '{classifier.semantic_state}'")
                if exp_ph and norm_str(exp_ph) != norm_str(classifier.phrase_state):
                    errs.append(f"phrase expected '{exp_ph}', got '{classifier.phrase_state}'")
                if min_bpm and classifier.bpm < min_bpm:
                    errs.append(f"bpm expected >= {min_bpm}, got {classifier.bpm}")
                if max_bpm and classifier.bpm > max_bpm:
                    errs.append(f"bpm expected <= {max_bpm}, got {classifier.bpm}")
                if min_beat_prob is not None and classifier.beat_probability < min_beat_prob:
                    errs.append(f"beat_probability expected >= {min_beat_prob:.2f}, got {classifier.beat_probability:.2f}")
                if max_beat_prob is not None and classifier.beat_probability > max_beat_prob:
                    errs.append(f"beat_probability expected <= {max_beat_prob:.2f}, got {classifier.beat_probability:.2f}")

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
                    if exp_p and norm_str(exp_p) != norm_str(classifier.presence):
                        if not a.get("_failed", False):
                            a["_failed"] = True
                            failed_asserts += 1
                            failures.append(f"[Window: {a.get('description', '')}] at {t_sec:.2f}s: presence '{classifier.presence}' != '{exp_p}'")
                    if exp_s and norm_str(exp_s) != norm_str(classifier.semantic_state):
                        if not a.get("_failed", False):
                            a["_failed"] = True
                            failed_asserts += 1
                            failures.append(f"[Window: {a.get('description', '')}] at {t_sec:.2f}s: state '{classifier.semantic_state}' != '{exp_s}'")
                    exp_ph = a.get("expected_phrase")
                    if exp_ph and norm_str(exp_ph) != norm_str(classifier.phrase_state):
                        if not a.get("_failed", False):
                            a["_failed"] = True
                            failed_asserts += 1
                            failures.append(f"[Window: {a.get('description', '')}] at {t_sec:.2f}s: phrase '{classifier.phrase_state}' != '{exp_ph}'")
                    min_bpm = a.get("min_bpm")
                    if min_bpm and classifier.bpm < min_bpm:
                        if not a.get("_failed", False):
                            a["_failed"] = True
                            failed_asserts += 1
                            failures.append(f"[Window: {a.get('description', '')}] at {t_sec:.2f}s: bpm {classifier.bpm} < {min_bpm}")
                    max_bpm = a.get("max_bpm")
                    if max_bpm and classifier.bpm > max_bpm:
                        if not a.get("_failed", False):
                            a["_failed"] = True
                            failed_asserts += 1
                            failures.append(f"[Window: {a.get('description', '')}] at {t_sec:.2f}s: bpm {classifier.bpm} > {max_bpm}")
                    min_beat_prob = a.get("min_beat_prob")
                    if min_beat_prob is not None and classifier.beat_probability < min_beat_prob:
                        if not a.get("_failed", False):
                            a["_failed"] = True
                            failed_asserts += 1
                            failures.append(f"[Window: {a.get('description', '')}] at {t_sec:.2f}s: beat_probability {classifier.beat_probability:.2f} < {min_beat_prob:.2f}")
                    max_beat_prob = a.get("max_beat_prob")
                    if max_beat_prob is not None and classifier.beat_probability > max_beat_prob:
                        if not a.get("_failed", False):
                            a["_failed"] = True
                            failed_asserts += 1
                            failures.append(f"[Window: {a.get('description', '')}] at {t_sec:.2f}s: beat_probability {classifier.beat_probability:.2f} > {max_beat_prob:.2f}")

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
        ground_truth_bpm = float(meta.get("ground_truth_bpm", 0.0))
        sum_bpm_errors = 0.0
        bpm_eval_count = 0
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

            # 4. Continuous BPM Error
            if ground_truth_bpm > 0.0 and exp_phrase not in ("breakdown", "outro"):
                act_bpm = snap.get("bpm", 0)
                if act_bpm > 0:
                    sum_bpm_errors += abs(act_bpm - ground_truth_bpm)
                    bpm_eval_count += 1

        presence_acc = max(0.0, 100.0 * (1.0 - (pres_errors / total_eval_secs)))
        semantic_acc = max(0.0, 100.0 * (1.0 - (sem_errors / total_eval_secs)))
        phrase_acc = max(0.0, 100.0 * (1.0 - (phr_errors / phr_eval_count))) if phr_eval_count > 0 else 100.0
        bpm_mae = (sum_bpm_errors / bpm_eval_count) if bpm_eval_count > 0 else 0.0
        bpm_acc = max(0.0, 100.0 * (1.0 - (bpm_mae / 20.0))) if bpm_eval_count > 0 else 100.0

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
        if ground_truth_bpm > 0.0:
            print(f"🎵 BPM Accuracy:             {bpm_acc:5.1f}%  (MAE: {bpm_mae:.1f} BPM, Ground Truth: {ground_truth_bpm} BPM, {bpm_eval_count}s)")
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
