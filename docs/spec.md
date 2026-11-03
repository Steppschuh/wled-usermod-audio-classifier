# 📋 Feature Specification & Scope: WLED Audio Classifier Usermod

---

## 🚶 Primary User Journeys

### Journey 1: Commercial WLED Controller Onboarding & Auto-Discovery
1. **Trigger**: User acquires a Gledopto ESP32 WLED+MIC controller (GL-C-010WL / GL-C-015WL-D) and flashes the custom WLED build with `AudioClassifierUsermod`.
2. **Action**: User connects WLED to home WiFi and enters MQTT broker details in WLED settings.
3. **System Response**:
   - The usermod connects to MQTT and immediately publishes Home Assistant Auto-Discovery payloads.
   - Home Assistant instantly surfaces 4 entities under the WLED device: `sensor.[id]_sound_level`, `sensor.[id]_audio_state`, `binary_sensor.[id]_music_active`, and `sensor.[id]_music_phrase`.
4. **Outcome**: Zero manual YAML writing in Home Assistant; the device starts streaming room acoustic state instantly.

### Journey 2: Context-Aware Home Assistant Automations
1. **Trigger**: Music begins playing in the living room on a standalone vinyl player, Bluetooth speaker, or smart speaker.
2. **Action**: The usermod registers continuous harmonic/rhythmic energy above ambient noise floor.
3. **System Response**:
   - Updates `sensor.audio_state` to `music` and flips `binary_sensor.music_active` to `on`.
   - Home Assistant evaluates automations: dims overhead lights, turns on warm accent lamps, and pauses robot vacuums.
   - During music listening, track transitions (e.g., build-up or drop) emit phrase changes (`buildup`, `drop`) allowing dynamic scene shifts.
4. **Outcome**: Home Assistant acts as the central brain controlling all home devices based on acoustic context.

---

## 🎯 Scope Matrix (MoSCoW)

### 🔴 Must-Have (MVP Scope)
*Features without which the product cannot deliver its core value.*
- [x] **WLED Usermod V2 Implementation**: Inherits from `Usermod`, cleanly integrating into WLED v0.14+ / v0.15+ / v0.16+ build system.
- [x] **I2S Microphone Acquisition**: Non-blocking I2S DMA audio capture configured for Gledopto WLED+MIC default pins (`SD=26`, `WS=5`, `SCK=21`) and customizable via WLED web UI.
- [x] **Calibrated dB SPL Noisemeter**: Accurate RMS sound pressure estimation with peak tracking and ambient floor baseline.
- [x] **Acoustic State Machine**: High-level classification between `silence`, `ambient_noise` (speech/appliances/traffic), and `music`.
- [x] **Dynamic Energy Phrasing**: State machine detecting musical energy shifts: `steady`, `buildup`, `breakdown`, and `drop`.
- [x] **Home Assistant MQTT Auto-Discovery**: Automatic entity creation in Home Assistant for dB sound level, audio state, music active binary sensor, and phrase state.
- [x] **WLED JSON Info API Integration**: Adds current audio telemetry to `/json/info` (`root["u"]["AudioClassifier"]`).
- [x] **WLED Web UI Settings (`Config > Usermods`)**: Web controls for calibration offset (dB), silence threshold (dB), GPIO pin assignment, and MQTT toggle.

### 🟡 Should-Have (Next Iteration)
*Important features that add significant value, but MVP can launch without them.*
- [ ] **Metric Bar / Beat Counting (V2 Phrasing)**: Real-time BPM onset detection counting recurring 4/8/16-bar phrase boundaries.
- [ ] **3-Band Spectral Energy Exposure**: Exposing raw Bass, Mid, and High normalized energy to Home Assistant over MQTT.
- [ ] **WLED Onboard LED Effect Integration**: Optional flag to let local LED strip effects react to phrase drops if an LED strip is physically wired to the controller.

### 🟢 Nice-to-Have (Backlog / Delight)
*Ideas, inspirations, and polish that can wait until the foundation is proven.*
- [ ] **Web UI Live Waveform / Spectrogram**: Small diagnostic visualizer inside WLED's web interface for testing microphone response.
- [ ] **Multi-Point In-Room Calibration Wizard**: Simple step-by-step button in WLED UI to calibrate ambient silence vs loud music.

### ⛔ Won't-Have / Non-Goals (Explicit Out of Scope)
*Explicit boundaries to avoid scope creep during early building.*
- ❌ **Direct Peer-to-Peer WLED UDP Sync**: The controller will NOT broadcast direct UDP/DDP sync packets to other WLED devices. Home Assistant is the central coordinator for all automations and lighting scenes.
- ❌ **Voice Assistant / Speech-to-Text**: No keyword wake words or audio streaming outside the device.
- ❌ **Exact Song Fingerprinting (Shazam)**: No cloud music identification or hashing against track databases.
- ❌ **Raw Audio Recording / Storage**: Zero raw audio leaves the controller; purely processed metrics and classifications are reported.

---

## ❓ Edge Cases & Open Questions

| Question / Edge Case | Impact | Current Working Assumption | Status |
| :--- | :--- | :--- | :--- |
| *WiFi or MQTT Disconnects?* | Medium | DSP engine continues running; telemetry buffer drops older frames until MQTT reconnects with backoff. | 🟢 Working Plan |
| *WLED Memory Footprint on ESP32?* | High | Standard ESP32 has limited heap (~30-50KB free during WLED runtime). Keep buffer sizes modest (256-512 samples) to fit well within RAM. | 🟢 Implemented (256-sample DMA buffer) |
| *AudioReactive usermod coexistence?* | Medium | If user already has `AudioReactive` enabled in WLED, can we tap their existing audio buffer rather than re-initializing I2S? | 🟡 Optimization Spike |

