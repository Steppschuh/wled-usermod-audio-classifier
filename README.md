# WLED Audio Classifier Usermod

[![Build Firmware](https://github.com/Steppschuh/wled-usermod-audio-classifier/actions/workflows/build-firmware.yml/badge.svg)](https://github.com/Steppschuh/wled-usermod-audio-classifier/actions/workflows/build-firmware.yml)
[![License: EUPL-1.2](https://img.shields.io/badge/License-EUPL--1.2-blue.svg)](LICENSE)
[![GitHub Release](https://img.shields.io/github/v/release/Steppschuh/wled-usermod-audio-classifier?include_prereleases&style=flat)](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases)

An external out-of-tree [WLED](https://github.com/wled/WLED) Usermod (V2) that couples with WLED's native **AudioReactive** usermod to transform commercial ESP32 controllers with built-in microphones (such as the **Gledopto GL-C-015WL-D / GL-C-010WL WLED+MIC**) or DIY ESP32 + INMP441 builds into ambient acoustic presence sensors.

Measures relative sound volume, tracks ambient noise baseline using a sliding 10th-percentile filter, provides debounced sound presence detection, and streams real-time telemetry to **Home Assistant via MQTT Auto-Discovery**—all while preserving your existing audio-reactive LED lighting effects.

---

## ✨ Key Features

- **AudioReactive Coexistence**: Runs alongside WLED's native `AudioReactive` engine without hardware conflicts. Your audio-reactive LED animations continue running at full frame rate.
- **Sliding Noise Floor Estimation**: Computes room ambient baseline using a 10th-percentile histogram across a 5-minute sliding window. The baseline freezes automatically during active sound to prevent continuous music playback from drifting into "ambient noise".
- **Debounced Presence Detection**: Separate configurable `on-delay` (1.2s) and `off-delay` (8.0s) timers prevent transient noises (doors closing, claps) from triggering false presence and prevent automations from flapping during pauses between songs.
- **Home Assistant Auto-Discovery**: Automatically creates and updates Home Assistant entities via MQTT, linked directly to your parent WLED device with zero manual YAML required.
- **WLED Web UI Integration**: Configure thresholds, debouncing durations, and experimental tracking directly from WLED's **Config > Usermods** interface.
- **Experimental Phrasing Engine**: Evaluates beat regularity from FFT peak timing and short-vs-long bass band energy ratios (`steady`, `buildup`, `breakdown`, `drop`).

---

## 📡 Home Assistant Entities

When MQTT is connected, the usermod publishes discovery payloads to Home Assistant, attaching entities directly to the WLED device:

| Entity Name | Domain | Type | Values / Unit | Description |
| :--- | :--- | :--- | :--- | :--- |
| **`Sound Level`** | `sensor` | Diagnostic / Measurement | `0.0` – `100.0 %` | Relative sound volume percentage. Updates at 1 Hz. |
| **`Sound Detected`** | `binary_sensor` | `sound` | `ON` / `OFF` | Debounced presence indicator for active sound in the room. |
| **`Audio State`** | `sensor` | `enum` | `silence`, `ambient_noise`, `music` | Acoustic context classification. |
| **`Audio Source Status`** | `sensor` | Diagnostic | Text | Live health and coupling state with AudioReactive. |
| **`Music Phrase`** | `sensor` | Diagnostic / Enum | `steady`, `buildup`, `breakdown`, `drop` | Structural phrasing state (enabled via `experimental-music`). |

---

## 🛠️ Supported Hardware

| Hardware | Status | Microphone Type | Pin Configuration |
| :--- | :---: | :--- | :--- |
| **Gledopto GL-C-015WL-D** | 🟢 Tested | Built-in I2S MEMS | Defaults configured in AudioReactive (`SD: 26`, `WS: 5`, `SCK: 21`) |
| **Gledopto GL-C-010WL** | 🟡 Supported | Built-in I2S MEMS | Defaults configured in AudioReactive (`SD: 26`, `WS: 5`, `SCK: 21`) |
| **ESP32 + INMP441** | 🟢 Tested | Digital I2S Breakout | Configure in AudioReactive (e.g. `SD: 32`, `WS: 15`, `SCK: 14`) |
| **ESP32 + SPH0645** | 🟢 Supported | Digital I2S Breakout | Select SPH0645 in AudioReactive settings |
| **ESP32-S3 + I2S / PDM** | 🟢 Supported | Digital I2S / PDM | Fully supported via AudioReactive |

> **Note**: Audio hardware pins are managed by `AudioReactive`. You do **not** need to configure GPIO pins in AudioClassifier settings.

---

## 🚀 Quick Start: Flashing Your Device

### Method 1: Browser Over-The-Air (OTA) Update (Existing WLED Device)
1. Download the latest OTA binary (`*_ota.bin`, e.g. `wled_audio_classifier_esp32dev_ota.bin`) from **[Releases](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases)**.
2. In your browser, open your WLED controller's web interface (`http://<wled-ip>`).
3. Navigate to **Config (⚙️) > Security & Updates**.
4. Under **Manual OTA Update**, click **Choose File**, select the `*_ota.bin` file, and click **Update!**.
5. The device will reboot after ~30 seconds, preserving your existing WiFi credentials and LED settings.
6. Verify under **Config > Usermods** that both `AudioReactive` and `AudioClassifier` are enabled.

### Method 2: Browser Web Flasher via USB (New or Unconfigured Device)
1. Connect your controller to your computer with a USB data cable.
2. Download the latest **factory** binary (`*_factory.bin`) from [Releases](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases).
3. Open **[ESP Web Tools](https://web.esphome.io/)** in Chrome or Edge and connect to your device's serial port.
4. Flash the `*_factory.bin` at address `0x0000`.

📖 For complete step-by-step instructions, see the **[Flashing Guide](docs/flashing-guide.md)**.

---

## 📦 Building from Source (PlatformIO)

This usermod is compatible with WLED `main` (0.16+).

1. Clone WLED:
   ```bash
   git clone https://github.com/wled/WLED.git wled-core
   cd wled-core
   ```

2. Add the usermod to `platformio_override.ini`:
   ```ini
   [platformio]
   default_envs = esp32dev

   [env:esp32dev]
   custom_usermods =
     ${common.default_usermods}
     https://github.com/Steppschuh/wled-usermod-audio-classifier.git
   ```

3. Build and upload:
   ```bash
   pio run -e esp32dev -t upload
   ```

---

## ⚠️ Limitations & Notes

- **Relative Volume (%) vs Absolute dB SPL**: `AudioClassifier` v0.1.0 reads the audio stream after AudioReactive's input stage. It provides normalized relative volume percentage (`0–100%`) rather than uncalibrated dB values. If AudioReactive's `AGC` (Automatic Gain Control) is enabled, the volume baseline will adjust dynamically with room volume.
- **AudioReactive Requirement**: AudioClassifier requires the `AudioReactive` usermod to be present in the firmware build.
- **100% Local & Privacy Preserving**: Audio is processed in volatile RAM buffers and immediately discarded. Zero raw audio or audio recordings leave your device.

---

## 📚 Documentation

- **[Flashing Guide](docs/flashing-guide.md)**: Browser OTA, WebSerial, and PlatformIO flashing instructions.
- **[AudioReactive Coupling](docs/audioreactive.md)**: How AudioClassifier and AudioReactive communicate without conflicts.
- **[Configuration Reference](docs/configuration.md)**: Detailed explanation of all web UI settings and tuning tips.
- **[MQTT & Home Assistant](docs/mqtt.md)**: Full topic paths, payload schemas, and entity specifications.
- **[Supported Hardware](docs/hardware.md)**: Controller pinouts and tested microphone modules.
- **[Design Notes](docs/design-notes.md)**: Architecture, noise floor algorithms, and state machines.
- **[Roadmap](ROADMAP.md)**: Upcoming milestones and features.

---

## 📄 License

Licensed under the **European Union Public Licence v1.2** ([EUPL-1.2](LICENSE)).

*Disclaimer: This project is an independent community open-source usermod. It is not affiliated with or endorsed by Gledopto or the core WLED project.*
