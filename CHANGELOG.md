# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-11-03

### Added
- **AudioReactive Coupling**: Non-invasive integration with WLED's native AudioReactive usermod, allowing ambient acoustic sensing without conflicting with AudioReactive LED effects.
- **Ambient Acoustic Presence**: Robust noise-floor tracking (10th-percentile 5-minute sliding window) with configurable on/off debouncing to accurately detect sound vs quiet without drift during sustained playback.
- **Home Assistant Auto-Discovery**: Automatic zero-YAML registration of entities (`Sound Level`, `Sound Detected`, `Audio State`, `Audio Source Status`, and optional `Music Phrase`) linked directly to the parent WLED device.
- **Diagnostics & Status Monitoring**: Real-time detection of AudioReactive state (enabled, disabled, AGC status, network receive mode).
- **Experimental Phrasing Engine**: Beat regularity tracking from FFT peaks and short-vs-long bass energy trend evaluation (`steady`, `buildup`, `breakdown`, `drop`).
- **Comprehensive Unit Tests**: Native host-side test suite covering debouncing, noise floor freezing during continuous playback, and phrasing state transitions.
- **Automated CI/CD**: Matrix builds generating both OTA binaries and full factory images with SHA256 checksums.

