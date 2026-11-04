# Project Roadmap

This document outlines planned improvements, milestones, and future directions for `wled-usermod-audio-classifier`.

---

## 🎯 Version 0.1.x (Current Baseline)
- [x] Coupling with WLED's AudioReactive usermod via `getUMData`.
- [x] Relative volume (%) level reporting.
- [x] Dynamic background noise floor tracking (10th percentile over 5-minute sliding window).
- [x] Noise floor freezing during active sound presence to prevent continuous music drift.
- [x] Robust debounced presence detection (`quiet` vs `sound`).
- [x] Home Assistant MQTT Auto-Discovery with device linkage (`connections: [["mac", ...]]`).
- [x] Tuned Metric Phrasing Engine: Rave-Totem 8-state musical phrasing engine, beat probability, and harmonic BPM tracking verified against recorded session fixtures.
- [x] Native host test suite for deterministic state machine verification.
- [x] GitHub Actions automated CI generating OTA and factory binaries.

---

## 🚀 Version 0.2.0 (Planned)
- [ ] **Broaden Benchmark Coverage**: Record and benchmark acoustic fixtures across diverse genres (rock, acoustic, jazz, classical, and speech podcasts).
- [ ] **Sub-Bass Envelope Follower**: Soft-attack sub-bass tracking to improve kick detection in genres lacking sharp transients.
- [ ] **Buildup Snare-Roll Triggers**: Rapid transient accumulation detection to fire preemptive lighting drop effects before musical drops.
- [ ] **Multi-band Frequency Telemetry**: Expose normalized Bass, Mid, and High energy percentages to Home Assistant as diagnostic/state entities.
- [ ] **Web UI Live Acoustic Monitor**: Compact live visualizer in WLED's web interface showing real-time relative volume and current noise floor.
- [ ] **Listen-on-I2S1 Calibration Mode**: Prototype secondary I2S slave receiver (`I2S_NUM_1`) on ESP32/S3 to sample raw PCM in parallel with AudioReactive for calibrated dBA measurements.

