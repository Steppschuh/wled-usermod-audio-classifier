# Architecture & Technical Design Notes

---

## 🧩 System Architecture

`wled-usermod-audio-classifier` processes ambient acoustic telemetry in a decoupled, layered pipeline:

```mermaid
flowchart TD
    subgraph Microphone ["Acoustic Input"]
        MIC[("I2S / PDM Digital MEMS Mic")]
    end

    subgraph WLED_AR ["WLED Core & AudioReactive"]
        MIC -->|I2S DMA Stream| AR_CORE["AudioReactive Usermod\n• DMA acquisition\n• 512-sample FFT @ 22kHz\n• 16-band GEQ filter & peak detection"]
        AR_CORE -->|Fast LED Buffer| LEDS["WLED Light Engine / NeoPixels\n(Audio-Reactive Visual Effects)"]
    end

    subgraph AudioClassifier ["Audio Classifier Usermod"]
        AR_CORE -->|getUMData API| AR_SRC["AudioReactiveSource\n(Extracts volumeRaw, bands, peak, freq)"]
        AR_SRC --> FRAME["AudioFrame\n(Normalized relative level & 16-band energy)"]
        
        FRAME --> CLASSIFIER["AudioClassifier State Machine\n• Dynamic 10th-percentile noise floor (5min ring)\n• Noise floor freezing during active sound\n• Configurable On/Off debouncing\n• Beat regularity & Phrasing evaluation"]
        
        CLASSIFIER --> HA_DISCOVERY["HomeAssistantAudioDiscovery\n• Formats ArduinoJson discovery payloads\n• Links to parent WLED device via MAC\n• Emits state changes & 1Hz telemetry"]
    end

    subgraph HomeAssistant ["Smart Home Ecosystem"]
        HA_DISCOVERY -->|MQTT Auto-Discovery| MQTT_BROKER["MQTT Broker (Mosquitto)"]
        MQTT_BROKER --> HA["Home Assistant\n• sensor.sound_level (%)\n• binary_sensor.sound_detected\n• sensor.audio_state\n• sensor.audio_status\n• sensor.music_phrase"]
        HA --> AUTOMATIONS["Automations & Scenes\n(Lighting, media control, noise alerts)"]
    end
```

---

## 📊 Noise Floor Estimation & Stability

A major design flaw in naive sound sensors is **baseline creep**: when music or speech plays continuously for 15–30 minutes, an exponential moving average baseline slowly elevates to match the music volume, eventually classifying loud music as "ambient noise".

### 10th-Percentile Sliding Window
To prevent baseline creep:
1. Every 1000 ms, the 1-second energy average level is calculated.
2. If `presence == QUIET`, the sample is inserted into a 101-bin histogram across a 300-second (5-minute) circular buffer.
3. The 10th percentile is evaluated directly from the histogram.
4. **Noise Floor Freezing**: When active sound presence is detected (`presence == SOUND`), histogram updates are frozen. Even if a party plays loud music for 4 hours, the noise floor remains locked to the true ambient room baseline. When music stops, normal ambient tracking resumes after the `offDelayMs` debounce expires.

