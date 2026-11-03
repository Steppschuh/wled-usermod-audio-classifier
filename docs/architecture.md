# 🏗️ Architecture & Technical Design: WLED Audio Classifier Usermod

---

## 🛠️ Tech Stack & Components

| Layer | Technology | Rationale / Trade-offs |
| :--- | :--- | :--- |
| **Target Hardware** | Commercial ESP32 WLED+MIC Controllers (e.g. **Gledopto GL-C-010WL / GL-C-015WL-D**) & DIY ESP32 + INMP441 | Inexpensive ($10-$15), flush-mounted, pre-packaged with enclosure, power supply terminals, and built-in I2S microphone (`SD=26`, `WS=5`, `SCK=21`). |
| **Firmware Framework** | **WLED Usermod V2** (`wled-usermod-example` architecture) | Integrates natively into WLED's ecosystem without modifying core firmware files. Supports web UI configuration (`Config > Usermods`), non-volatile storage, and OTA updates. |
| **Audio Pipeline** | I2S DMA Driver (ESP-IDF v4 / v5 compatible) + RMS & Dynamic Baseline Engine | Non-blocking DMA sampling (16 kHz, 32-bit slot, mono). Computes RMS-to-dB SPL, peak level, dynamic background noise floor, and variance. |
| **Smart Home Integration** | **MQTT with Home Assistant Auto-Discovery** + `/json/info` | Automatically registers sensors in Home Assistant without manual YAML. Home Assistant remains the single source of truth for all room automations and lighting scenes. |
| **Configuration Interface** | WLED Web UI (`addToConfig` / `readFromConfig`) | Allows setting GPIO pins, dBA calibration offset, silence dB threshold, and enabling/disabling HA MQTT discovery from the browser. |

---

## 🧩 System Architecture & Data Flow

```mermaid
flowchart TD
    subgraph Controller ["WLED Hardware Controller (e.g. Gledopto WLED+MIC)"]
        MIC[("I2S Digital MEMS Mic\n(GPIO 26/5/21)")] -->|I2S DMA Stream| DSP["Audio Analysis Engine\n• Non-blocking DMA buffer (256 samples @ 16kHz)\n• RMS to dB SPL with calibration offset\n• Rolling background noise baseline\n• Short-term energy variance"]
        
        DSP --> CLASSIFIER["Audio Classifier State Machine\n• silence | ambient_noise | music\n• Confidence threshold & debouncing"]
        DSP --> PHRASE["Dynamic Energy Phrasing\n• steady | buildup | breakdown | drop\n• 8-slot rolling energy memory"]

        subgraph Usermod ["WLED Usermod V2 Hooks"]
            CLASSIFIER & PHRASE --> USERMOD_CORE["AudioClassifierUsermod"]
            USERMOD_CORE -->|addToJsonInfo| JSON["/json/info State API"]
            USERMOD_CORE -->|addToConfig / readFromConfig| CFG["Web UI Settings\n(Config > Usermods)"]
            USERMOD_CORE -->|publishMqtt| MQTT_CLIENT["WLED Internal MQTT Client"]
        end
    end

    subgraph Broker ["MQTT Broker (Mosquitto)"]
        MQTT_CLIENT -->|Retained Discovery JSON| HA_DISCOVERY["homeassistant/sensor/[id]/config"]
        MQTT_CLIENT -->|State Payloads (1-2 Hz)| HA_STATE_TOPIC["wled/[id]/audio_classifier/..."]
    end

    subgraph HomeAssistant ["Home Assistant (Single Source of Truth)"]
        HA_DISCOVERY -.->|Auto-Configures| HA_ENTITIES["Discovered Entities:\n• sensor.[id]_sound_level\n• sensor.[id]_audio_state\n• binary_sensor.[id]_music_active\n• sensor.[id]_music_phrase"]
        HA_STATE_TOPIC --> HA_ENTITIES
        HA_ENTITIES --> AUTOMATIONS["Home Assistant Automations & Scenes\n(Controls lights, media, ambient scene transitions)"]
    end
```

---

## 🏛️ Usermod Class Design (`AudioClassifierUsermod`)

Following the modern external usermod pattern demonstrated by [`intermittech/wled-usermod-powermanager`](https://github.com/intermittech/wled-usermod-powermanager) and [`lost-hope/sensor-hub`](https://github.com/lost-hope/sensor-hub):

```cpp
#pragma once
#include "wled.h"
#include <esp_idf_version.h>

#ifndef USERMOD_ID_AUDIO_CLASSIFIER
  #define USERMOD_ID_AUDIO_CLASSIFIER 205 // Unique external usermod ID
#endif

class AudioClassifierUsermod : public Usermod {
private:
  // PROGMEM string keys to prevent heap fragmentation
  static const char _name[];
  static const char _enabled[];
  static const char _haDiscovery[];
  static const char _pinSD[];
  static const char _pinWS[];
  static const char _pinSCK[];
  static const char _dbOffset[];
  static const char _silenceThreshold[];

  // Configuration variables (persisted via readFromConfig / addToConfig)
  bool enabled = true;
  bool haDiscoveryEnabled = true;
  int8_t i2sSd = 26;                // Default: Gledopto WLED+MIC
  int8_t i2sWs = 5;
  int8_t i2sSck = 21;
  float dbOffset = 0.0f;            // Calibration offset for absolute dB SPL
  float silenceThresholdDb = 45.0f; // Ambient silence cutoff

  // Live acoustic state
  float currentDbSpl = 35.0f;
  float peakDb = 35.0f;
  float rollingBaseline = 40.0f;
  String audioState = "silence";    // "silence" | "ambient_noise" | "music"
  bool musicActive = false;
  String phraseState = "steady";    // "steady" | "buildup" | "breakdown" | "drop"
  float musicConfidence = 0.0f;
  unsigned long lastTelemetryPublish = 0;
  bool discoveryPublished = false;

  bool initI2S();
  void analyzeAudio();
  void publishHomeAssistantDiscovery();
  void publishTelemetry();

public:
  void setup() override;
  void loop() override;
  void onMqttConnect(bool sessionPresent) override;

  // JSON API for status inspection under /json/info ("u")
  void addToJsonInfo(JsonObject& root) override;

  // Web UI settings integration (Config > Usermods)
  void addToConfig(JsonObject& root) override;
  bool readFromConfig(JsonObject& root) override;

  uint16_t getId() override { return USERMOD_ID_AUDIO_CLASSIFIER; }
};

// Self-registration for WLED 0.14+ / 0.16+
static AudioClassifierUsermod audioClassifier;
REGISTER_USERMOD(audioClassifier);
```

---

## 📡 Home Assistant MQTT Discovery Entities

When MQTT connects, the usermod publishes standard discovery payloads to `homeassistant/`:

1. **Sound Level Meter** (`sensor.[id]_sound_level`):
   - `device_class`: `sound_pressure`
   - `unit_of_measurement`: `dB`
   - `state_class`: `measurement`
2. **Audio State** (`sensor.[id]_audio_state`):
   - `icon`: `mdi:waveform`
   - States: `silence`, `ambient_noise`, `music`
3. **Music Active** (`binary_sensor.[id]_music_active`):
   - `device_class`: `sound`
   - `icon`: `mdi:music`
   - Payload: `ON` / `OFF`
4. **Music Phrase** (`sensor.[id]_music_phrase`):
   - `icon`: `mdi:music-note-eighth`
   - States: `steady`, `buildup`, `breakdown`, `drop`

---

## 📦 Repository Structure

```text
wled-usermod-audio-classifier/
├── library.json                 # PlatformIO library manifest ({ "build": { "libArchive": false } })
├── usermod_audio_classifier.h   # Core Usermod V2 class definition & audio analysis engine
├── usermod_audio_classifier.cpp # String definitions & static registration
├── examples/
│   └── platformio_override.ini  # Ready-to-copy env demonstrating custom_usermods usage
├── docs/
│   ├── overview.md              # Project overview, maturity checklist & navigation
│   ├── architecture.md          # Architecture, data flow & class design (this document)
│   ├── spec.md                  # Feature spec, MoSCoW scope & edge cases
│   ├── vision.md                # Vision, problem, UVP & success metrics
│   ├── evaluation.md            # Competitive evaluation & risk analysis
│   ├── flashing-guide.md        # Step-by-step flashing guide (OTA, WebSerial, PlatformIO)
│   └── braindumps/              # Brainstorming notes & concept history
└── README.md
```

