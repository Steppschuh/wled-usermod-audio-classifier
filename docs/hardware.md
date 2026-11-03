# Supported Hardware & Controller Presets

`wled-usermod-audio-classifier` couples with WLED's native `AudioReactive` usermod. Any microphone supported by `AudioReactive` works with this usermod.

---

## 📋 Tested & Supported Hardware

| Hardware | Status | Microphone Type | Default Pins (Configured in AudioReactive) | Notes |
| :--- | :---: | :--- | :--- | :--- |
| **Gledopto GL-C-015WL-D** | 🟢 Tested | Built-in I2S MEMS | `SD: 26`, `WS: 5`, `SCK: 21` | Flush-mounted commercial WLED+MIC controller. |
| **Gledopto GL-C-010WL** | 🟡 Supported | Built-in I2S MEMS | `SD: 26`, `WS: 5`, `SCK: 21` | Commercial WLED+MIC controller. |
| **ESP32 + INMP441** | 🟢 Tested | I2S MEMS Breakout | Custom (e.g. `SD: 32`, `WS: 15`, `SCK: 14`) | Connect L/R to GND (left channel). Power from 3.3V only! |
| **ESP32 + SPH0645** | 🟢 Supported | I2S MEMS Breakout | Custom | Select `SPH0645` in AudioReactive digital mic settings. |
| **ESP32-S3 + I2S / PDM** | 🟢 Supported | I2S / PDM | Custom | Fully supported via AudioReactive. |

---

## ⚠️ Important Note on Pin Configuration

Because this usermod couples directly with `AudioReactive`, **you do not need to configure GPIO pins in AudioClassifier settings**. 

Configure your microphone pins once in **Config > Usermods > AudioReactive**, and `AudioClassifier` will automatically acquire the audio telemetry.

