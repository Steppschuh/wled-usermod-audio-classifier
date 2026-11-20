# WLED Audio Classifier Wiki

Step-by-step guides for installing the **WLED Audio Classifier** firmware, setting up standalone preset switching on the controller, and integrating with Home Assistant.

---

## Guides

### 1. [Getting Started: From Box to Audio-Reactive WLED](Getting-Started)
Start here if you have a new WLED controller or are installing the Audio Classifier firmware for the first time.
- Flashing a blank ESP32 via USB or updating an existing WLED controller over Wi-Fi.
- Connecting to your home network and configuring your LED strip length.
- Enabling and tuning the microphone (`AudioReactive` **Gain**, **Squelch**, and **AGC**).
- Testing live sound and music classification inside WLED.

---

### 2. [Standalone Auto-Reactive Preset Switching](Standalone-Preset-Switching)
Configure your WLED controller to switch lighting presets automatically as music starts, builds up, drops, and stops—without Home Assistant or an external server.
- How the 5 energy slots work (`Ambient`, `AR Low`, `AR Mid`, `AR High`, `AR Max`).
- Step-by-step guide to creating and naming presets in WLED.
- Starter effect and palette ideas for each energy slot.
- Cooldown timers, fixed preset IDs, and manual override hold.

---

### 3. [Home Assistant Integration via MQTT](Home-Assistant-Integration)
Connect your controller to Home Assistant over MQTT to unlock 11 auto-discovered sensors, switches, and diagnostic entities.
- Setting up the Mosquitto MQTT broker in Home Assistant.
- Connecting WLED to MQTT for automatic device discovery.
- Using `Audio Level RMS`, `Audio Level Peak`, and `Audio Source Status` for microphone calibration.
- Ready-to-use Home Assistant automation YAML examples.
