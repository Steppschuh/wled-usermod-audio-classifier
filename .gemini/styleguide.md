# Repository Review Style Guide: wled-usermod-audio-classifier

Follow the architecture boundaries and invariants defined in `AGENTS.md`:

## Pure C++17 Core Isolation
- `src/classifier.{h,cpp}` and `src/auto_preset_controller.{h,cpp}` must remain pure C++17 with zero Arduino, ESP32, FreeRTOS, or WLED dependencies (`pio test -e native` must compile and pass).
- `src/audio_frame.h` must remain header-only with zero dependencies.

## WLED & ArduinoJson Compatibility
- Firmware integration files (`src/ar_source.*`, `src/ha_discovery.*`, `src/usermod_audio_classifier.*`) compile inside WLED on ESP32 using ArduinoJson v6 (`DynamicJsonDocument`, `StaticJsonDocument`), while native tests and newer builds may use ArduinoJson v7 (`JsonDocument`). Guard version differences with `#if ARDUINOJSON_VERSION_MAJOR >= 7`.
- Guard ESP32/WLED files with `#ifdef ARDUINO_ARCH_ESP32` and MQTT code with `#ifndef WLED_DISABLE_MQTT`.
- Preserve `DynamicJsonDocument(1024)` for ArduinoJson v6 (or `JsonDocument` for v7), not undersized `StaticJsonDocument`, for Home Assistant MQTT discovery payloads to prevent silent truncation of `dev.ids`.

## Behavioral Invariants
- If `Audio Type` is not `Music`, `Music Energy` must be `0%` and `Music Phrase` must be `Unknown`.
- `Audio Type == Silence` and `Audio Detected == OFF` must stay strictly coupled.
- Freeze the 10th-percentile noise floor histogram during `SOUND` presence to avoid baseline creep.
- Enforce `Breakdown` dwell (>= 20s / 10 bars without beats) and `Outro` recovery hysteresis.
- Reject BPM octave-jumping and eighth-note jitter; hold BPM across quiet breakdowns and reset cleanly when music ends.

## Python Simulator Parity
- Behavioral changes in `src/classifier.cpp` must be mirrored in `tools/replay_session.py`.
