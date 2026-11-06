# Contributing to WLED Audio Classifier

Thank you for your interest in improving `wled-usermod-audio-classifier`!

## Code Architecture

This usermod is divided into distinct, decoupled components:

- `src/audio_frame.h`: Lightweight data struct representing an acoustic frame.
- `src/ar_source.{h,cpp}`: Driver reading raw metrics and frequency bands from WLED's AudioReactive usermod.
- `src/classifier.{h,cpp}`: Pure C++ state machines (presence, noise floor histogram, beat regularity, phrasing). **Must remain free of platform/WLED includes** to enable native host testing.
- `src/ha_discovery.{h,cpp}`: Home Assistant MQTT discovery and real-time state telemetry generation using ArduinoJson.
- `src/usermod_audio_classifier.{h,cpp}`: WLED Usermod V2 hooks and web configuration.

## Development & Testing Workflow

### 1. Running Native Unit Tests
To test classifier logic, state transitions, and debounce timers on your computer without flashing an ESP32:

```bash
pio test -e native
```

### 2. Building with WLED Core
To build the firmware binary locally:

```bash
git clone https://github.com/wled/WLED.git wled-core
cd wled-core
```

Add to `platformio_override.ini`:

```ini
[platformio]
default_envs = esp32dev

[common]
default_usermods = audioreactive

[env:esp32dev]
custom_usermods =
  ${common.default_usermods}
  symlink:///path/to/wled-usermod-audio-classifier
```

Then build:

```bash
pio run -e esp32dev
```

## Pull Request Guidelines

1. Ensure all native unit tests pass: `pio test -e native`.
2. Adhere to the code formatting guidelines defined in `.editorconfig` and `.clang-format` (2-space indentation).
3. Update documentation and `CHANGELOG.md` when introducing new features or modifying configuration options.
4. For AI-assisted workflows, OTA flashing recipes, and architecture invariants, see [AGENTS.md](AGENTS.md).


