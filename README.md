# WLED Audio Classifier Usermod

An external out-of-tree [WLED](https://github.com/wled/WLED) Usermod (V2) that repurposes commercial ESP32 WLED controllers with built-in microphones (such as the **Gledopto GL-C-010WL / GL-C-015WL-D WLED+MIC**) or DIY ESP32 + INMP441 builds into ambient acoustic sensors.

Measures sound pressure level (dB SPL), distinguishes silence from ambient noise and active music playback, tracks musical phrasing, and publishes real-time telemetry to **Home Assistant via MQTT Auto-Discovery**.

---

## ✨ Features

- **Decoupled Architecture**: No peer-to-peer WLED meshes. Home Assistant is the central source of truth for all room automations and lighting scenes.
- **Calibrated Sound Level Meter**: Computes real-time RMS sound pressure level with user-configurable dBA calibration offset.
- **Acoustic State Machine**: Classifies live room state into `silence`, `ambient_noise` (speech, appliances, traffic), or `music`.
- **Dynamic Energy Phrasing**: Tracks build-ups, breakdowns, steady grooves, and bass drops (`steady`, `buildup`, `breakdown`, `drop`) to trigger dynamic scene shifts.
- **Home Assistant Auto-Discovery**: Automatically creates and updates Home Assistant entities via MQTT with zero YAML configuration required.
- **WLED Web UI Integration**: Configure I2S pinouts, sensitivity offsets, silence thresholds, and MQTT discovery directly from WLED's **Config > Usermods** interface.
- **Commercial Controller Presets**: Default pinouts pre-configured for flush-mounted Gledopto WLED+MIC hardware (`SD=26`, `WS=5`, `SCK=21`).

---

## 📡 Home Assistant Entities

When MQTT is connected, the usermod publishes discovery messages to `homeassistant/`:

| Entity | Type | Values / Unit | Description |
| :--- | :--- | :--- | :--- |
| `sensor.[id]_sound_level` | Sensor | `dB` | Calibrated Sound Pressure Level (SPL) |
| `sensor.[id]_audio_state` | Sensor | `silence`, `ambient_noise`, `music` | Active acoustic context classification |
| `binary_sensor.[id]_music_active` | Binary Sensor | `ON` / `OFF` | Debounced presence indicator for active music playback |
| `sensor.[id]_music_phrase` | Sensor | `steady`, `buildup`, `breakdown`, `drop` | Structural phrasing and energy transition state |

---

## 🛠️ Hardware & Pinout

### Commercial: Gledopto WLED+MIC (`GL-C-010WL` / `GL-C-015WL-D`)
The usermod defaults to the built-in I2S microphone pinout on Gledopto controllers:
- **I2S SD (Data)**: `GPIO 26`
- **I2S WS (Word Select / Clock)**: `GPIO 5`
- **I2S SCK (Bit Clock)**: `GPIO 21`

### DIY: ESP32 + INMP441 / SPH0645
If using a standard ESP32 development board with an INMP441 breakout:
- **L/R (Channel)**: Connect to **GND** (selects Left Channel)
- **SD**: Connect to any input GPIO (e.g., `GPIO 32`)
- **WS**: Connect to any in/out GPIO (e.g., `GPIO 15`)
- **SCK**: Connect to any in/out GPIO (e.g., `GPIO 14`)
- **VDD**: Connect to **3.3V** (Do not use 5V!)
- **GND**: Connect to **GND**

---

## 📦 Installation in WLED

This usermod uses WLED 0.14+ / 0.16+ external usermod support.

### Option 1: External Usermod (`platformio_override.ini`)
Add the repository URL to `custom_usermods` in your WLED `platformio_override.ini`:

```ini
[env:gledopto_audio_classifier]
extends = env:esp32dev
build_flags =
  ${env:esp32dev.build_flags}
  -D USERMOD_AUDIO_CLASSIFIER
custom_usermods =
  https://github.com/Steppschuh/wled-usermod-audio-classifier.git
```

WLED's build system will automatically fetch, compile, and link the usermod into your firmware binary.

### Option 2: Local Copy
Clone or copy this repository into your local WLED source tree:
```bash
git clone https://github.com/Steppschuh/wled-usermod-audio-classifier.git usermods/AudioClassifier
```
And add to your build environment:
```ini
custom_usermods = AudioClassifier
```

---

## ⚙️ Configuration

In the WLED web UI, navigate to **Config > Usermods**:
1. **`enabled`**: Toggle the audio classifier processing on or off.
2. **`ha-discovery`**: Enable automatic entity publishing to Home Assistant over MQTT.
3. **`pin-sd` / `pin-ws` / `pin-sck`**: Custom GPIO pin assignment (defaults: `26`, `5`, `21`).
4. **`db-offset`**: Fine-tune SPL calibration against a physical sound level meter.
5. **`silence-threshold-db`**: dB cutoff below which room audio is considered silent (default: `45 dB`).

---

## 📄 License

MIT License. See [LICENSE](LICENSE) for details.
