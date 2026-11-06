# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-11-06

### Added
- **Rave-Totem 8-State Musical Phrasing Engine**: Discrete musical energy and phrasing tracking (`Intro`, `Verse`, `Buildup`, `Chorus`, `Breakdown`, `Outro`) and dynamic `Music Energy` (`0–100%`) metric.
- **Phrase Outlier Detection & Revert Mechanism**: Detects momentary pre-drop hits and 1-2 beat tension drops, automatically reverting state if energy does not sustain.
- **Sub-Bass Onset Discrimination**: Sub-60Hz spectral flux extraction with mid-frequency band suppression and rolling low-frequency variance gating to reject snares, handclaps, and speech transients.
- **Harmonic Octave BPM Tracking**: Stable tempo extraction with octave folding into [85–175 BPM], freeze across breakdowns/bridges, and reset to 0 upon exiting music state.
- **Dedicated Beat Probability Sensor**: Real-time `Music Beat Probability` (`0.0–100.0%`) sensor and separate 1s `Audio Level RMS` / `Audio Level Peak` sensors.
- **Home Assistant Telemetry Alignment**: Rate-limited (250 ms) on-change MQTT telemetry for `energy` and `bpm`, updated MDI icons (`mdi:volume-high`, `mdi:pulse`), and legacy discovery topic cleanup.
- **Session Recording & Benchmark Tooling**: Fixture capture (`tools/record_session.py`), replay simulator (`tools/replay_session.py`), and automated continuous benchmark scoring (`test/fixture_loader.h`).
- **Official WLED Binary Packaging**: Artifact naming aligns with official WLED releases (`WLED_<WLED_VER>_MOD_<USERMOD_VER>_<TARGET>.bin` and `_factory.bin`).
- **Dynamic Firmware Versioning in HA**: Device information dynamically displays compound versioning (e.g. `wled-v16.0.1-mod-v0.2.0`).

## [0.1.0] - 2026-11-03

### Added
- **AudioReactive Coupling**: Non-invasive integration with WLED's native AudioReactive usermod, allowing ambient acoustic sensing without conflicting with AudioReactive LED effects.
- **Ambient Acoustic Presence**: Robust noise-floor tracking (10th-percentile 5-minute sliding window) with configurable on/off debouncing to accurately detect sound vs quiet without drift during sustained playback.
- **Home Assistant Auto-Discovery**: Automatic zero-YAML registration of entities (`Sound Level`, `Sound Detected`, `Audio State`, `Audio Source Status`, and optional `Music Phrase`) linked directly to the parent WLED device.
- **Diagnostics & Status Monitoring**: Real-time detection of AudioReactive state (enabled, disabled, AGC status, network receive mode).
- **Experimental Phrasing Engine**: Beat regularity tracking from FFT peaks and short-vs-long bass energy trend evaluation (`steady`, `buildup`, `breakdown`, `drop`).
- **Comprehensive Unit Tests**: Native host-side test suite covering debouncing, noise floor freezing during continuous playback, and phrasing state transitions.
- **Automated CI/CD**: Matrix builds generating both OTA binaries and full factory images with SHA256 checksums.

