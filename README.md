# WLED Audio Classifier Usermod

[![Build Firmware](https://github.com/Steppschuh/wled-usermod-audio-classifier/actions/workflows/build-firmware.yml/badge.svg)](https://github.com/Steppschuh/wled-usermod-audio-classifier/actions/workflows/build-firmware.yml)
[![License: EUPL-1.2](https://img.shields.io/badge/License-EUPL--1.2-blue.svg)](LICENSE)
[![GitHub Release](https://img.shields.io/github/v/release/Steppschuh/wled-usermod-audio-classifier?include_prereleases&style=flat)](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases)

**Bring ambient acoustic intelligence into Home Assistant using your existing WLED hardware.**

This [WLED](https://github.com/wled/WLED) Usermod (V2) bridges the gap between WLED and Home Assistant. While WLED's native **AudioReactive** engine uses the microphone to animate your LED strips, this usermod taps into that same audio stream to classify room acoustics and automatically expose rich sensors to **Home Assistant via MQTT Auto-Discovery**—with zero manual YAML configuration and zero impact on your LED frame rates.

---

## 🎯 Purpose & Why This Exists

Traditional smart home presence sensors have major blind spots:
- **PIR Motion Sensors** regularly trigger false "room clear" timeouts when occupants are sitting still (watching a movie, reading, or listening to music).
- **mmWave Radars** can detect tiny chest movements, but cannot tell whether a room is silent, active with conversation, or rocking with music.

Since commercial WLED controllers (such as the **Gledopto GL-C-015WL-D / GL-C-010WL**) and custom DIY builds frequently reside in living rooms, kitchens, and media centers with built-in microphones, this usermod turns them into multi-purpose acoustic sensors:

1. **Keep your light shows**: Audio-reactive LED effects continue running in real time without I2S conflicts or latency.
2. **Supercharge Home Assistant automations**: Home Assistant gains debounced acoustic presence, sound levels, room state classification (`Silence`, `Ambient Noise`, `Music`), rhythm metrics (`BPM`, `Beat Probability`), and structural song phrasing (`Intro`, `Verse`, `Buildup`, `Chorus`, `Breakdown`, `Outro`).

---

## 💡 Example Automations & Use Cases

Here are real-world automations made possible by combining WLED and Home Assistant with this usermod:

### 1. Automatic Party Mode (Switch WLED Preset When Music Plays)
Automatically switch your WLED strip from a cozy ambient light to an energetic audio-reactive preset when music begins, and smoothly restore ambient lighting when the music stops:

```yaml
alias: "WLED: Auto-Switch Music Preset"
description: "Switch to Audio-Reactive preset when music starts; revert when quiet"
trigger:
  - platform: state
    entity_id: sensor.wled_audio_type
    to: "Music"
    id: "music_started"
  - platform: state
    entity_id: sensor.wled_audio_type
    from: "Music"
    for:
      seconds: 15
    id: "music_stopped"
action:
  - choose:
      - conditions:
          - condition: trigger
            id: "music_started"
        sequence:
          - action: select.select_option
            target:
              entity_id: select.wled_preset
            data:
              option: "Audio Reactive Waves" # Your audio-reactive preset name
      - conditions:
          - condition: trigger
            id: "music_stopped"
        sequence:
          - action: select.select_option
            target:
              entity_id: select.wled_preset
            data:
              option: "Warm White Relax" # Your relaxing ambient preset
```

---

### 2. Track Structure Lighting (Chorus Drop & Buildup Escalation)
React dynamically to the macro structure of songs. Crank the brightness and unleash vivid palettes on the `Chorus` / Drop, trigger escalating strobes during a `Buildup`, and settle into a smooth, dim glow during a `Breakdown`:

```yaml
alias: "WLED: Dynamic Song Phrase FX"
description: "Adjust WLED effects and brightness based on song structure"
trigger:
  - platform: state
    entity_id: sensor.wled_music_phrase
action:
  - choose:
      # Peak Energy: Chorus / Drop
      - conditions:
          - condition: state
            entity_id: sensor.wled_music_phrase
            state: "Chorus"
        sequence:
          - action: light.turn_on
            target:
              entity_id: light.wled
            data:
              brightness_pct: 100
              effect: "Freqwave" # High-energy effect

      # Escalation: Buildup
      - conditions:
          - condition: state
            entity_id: sensor.wled_music_phrase
            state: "Buildup"
        sequence:
          - action: light.turn_on
            target:
              entity_id: light.wled
            data:
              brightness_pct: 80
              effect: "Chunky"

      # Tension Release / Breather: Breakdown
      - conditions:
          - condition: state
            entity_id: sensor.wled_music_phrase
            state: "Breakdown"
        sequence:
          - action: light.turn_on
            target:
              entity_id: light.wled
            data:
              brightness_pct: 35
              effect: "Breathe"
```

---

### 3. BPM-Adaptive Animation Speed
Sync the animation speed of your LED effects directly to the detected track tempo using WLED's native JSON API:

```yaml
alias: "WLED: Sync Speed to BPM"
description: "Scale WLED effect speed proportionally to detected track tempo"
trigger:
  - platform: state
    entity_id: sensor.wled_bpm
condition:
  - condition: numeric_state
    entity_id: sensor.wled_bpm
    above: 59
action:
  - action: mqtt.publish
    data:
      topic: "wled/my_light/api"
      payload: >
        {
          "seg": [{
            "sx": {{ [([((states('sensor.wled_bpm') | float - 60) * 1.5 + 64), 255] | min), 32] | max | int }}
          }]
        }
```

---

### 4. True Room Occupancy (Acoustic Presence Fallback)
Prevent PIR motion sensors from plunging you into darkness while watching movies or talking on the couch. Combine PIR with `binary_sensor.audio_detected`:

```yaml
template:
  - binary_sensor:
      - name: "Living Room Occupied"
        device_class: occupancy
        state: >
          {{ is_state('binary_sensor.living_room_pir_motion', 'on') or
             is_state('binary_sensor.wled_audio_detected', 'on') }}
        delay_off:
          minutes: 5
```

---

### 5. Late Night Noise & Disturbance Guard
Send an alert or gently flash hallway lights red if sudden, loud sounds or shouting occur late at night:

```yaml
alias: "Alert: Late Night Noise Spike"
trigger:
  - platform: numeric_state
    entity_id: sensor.wled_audio_level_peak
    above: 75
condition:
  - condition: time
    after: "23:00:00"
    before: "06:30:00"
action:
  - action: notify.notify
    data:
      title: "Loud Noise Detected"
      message: "Sound peak reached {{ states('sensor.wled_audio_level_peak') }}% in the living room."
```

---

## ✨ Features

- **Home Assistant Auto-Discovery**: Automatically configures and attaches entities directly to your existing WLED device in Home Assistant over MQTT. Zero manual YAML configuration required.
- **AudioReactive Coexistence**: Reads internal in-memory FFT and volume telemetry (`getUMData`) without competing for I2S microphone hardware or dropping animation frames.
- **Freeze-Locked Noise Floor**: Computes the true ambient room baseline using a 10th-percentile filter over a 5-minute rolling window. Freezes automatically during active sound so continuous music playback never drifts into "ambient noise".
- **Debounced Presence Detection**: Configurable on-delay (1.2s) and off-delay (8.0s) suppress transient spikes (doors closing, claps) and prevent automations from flapping during song pauses.
- **Song Structure & Phrase Engine**: Distinguishes between `Intro`, `Verse`, `Buildup`, `Chorus`, `Breakdown`, and `Outro` with strict anti-churn hysteresis and macro-structure stability.
- **Tempo & Beat Probability Tracking**: Estimates rhythm tempo (`BPM`) and confidence (`Beat Probability`) using low-frequency variance analysis.
- **100% Local & Privacy Preserving**: All classification runs entirely on-device in volatile RAM. No audio is ever recorded, stored, or transmitted over the network.

---

## 📡 Home Assistant Entities

When MQTT is connected, the following entities appear automatically under your WLED device in Home Assistant:

### 🔊 Audio Detected
- **Domain**: `binary_sensor`
- **Device Class**: `sound`
- **Values**: `ON` (Sound Detected) / `OFF` (Clear)
- **MQTT Topic**: `<device>/audio/detected`
- **Update Frequency**: On state change + periodic heartbeat

Debounced presence indicator for active sound in the room. Suppresses transient acoustic spikes (such as claps or door slams) via the configurable `on-delay` (default: 1.2s) and prevents automations from flapping during quiet moments or between songs via the `off-delay` (default: 8.0s).

> **Contract**: When `Audio Type` is `Silence`, `Audio Detected` is guaranteed to be `OFF`, and vice-versa.

---

### 🎶 Audio Type
- **Domain**: `sensor`
- **Device Class**: `enum`
- **Values**: `Silence`, `Ambient Noise`, `Music`
- **MQTT Topic**: `<device>/audio/type` (also mirrored to `<device>/audio/state`)
- **Update Frequency**: On state change + periodic heartbeat

High-level acoustic environment classification. Uses spectral entropy, low-frequency variance, and rhythmic periodicity to distinguish between:
- `Silence`: Room sound is at or below the ambient noise floor.
- `Ambient Noise`: Active acoustic energy (e.g. conversation, HVAC, dishes, footsteps) without rhythmic beat cadence.
- `Music`: Harmonically structured audio with persistent rhythmic cadence or bass pulses.

> **Contract**: Whenever `Audio Type` is not `Music`, `Music Energy` is reset to `0%` and `Music Phrase` is set to `Unknown`.

---

### 📊 Audio Level RMS
- **Domain**: `sensor`
- **Device Class**: `measurement`
- **Values / Unit**: `0.0` – `100.0 %`
- **Icon**: `mdi:volume-medium`
- **MQTT Topic**: `<device>/audio/level`
- **Update Frequency**: 1 Hz + periodic heartbeat

Root Mean Square (RMS) rolling average sound volume over the last 1-second window. Ideal for real-time sound gauges, dashboards, history graphs, and automation thresholds.

---

### 📈 Audio Level Peak
- **Domain**: `sensor`
- **Device Class**: `measurement`
- **Values / Unit**: `0.0` – `100.0 %`
- **Icon**: `mdi:volume-high`
- **MQTT Topic**: `<device>/audio/peak`
- **Update Frequency**: 1 Hz + periodic heartbeat

Maximum acoustic amplitude peak recorded within the last 1-second window. Responsive to instantaneous percussive transients, claps, bass drum hits, or sudden room noises.

---

### 🎼 Music Phrase
- **Domain**: `sensor`
- **Device Class**: `enum`
- **Values**: `Unknown`, `Intro`, `Verse`, `Buildup`, `Chorus`, `Breakdown`, `Outro`
- **Icon**: `mdi:playlist-music`
- **MQTT Topic**: `<device>/audio/phrase`
- **Update Frequency**: On state change + periodic heartbeat
*(Available when `experimental-music` is enabled)*

Song macro-structure classification designed around classic tension-and-release music arrangement:
- `Intro`: Stripped-back initial beat or melodic hook.
- `Verse`: Steady groove or vocal narrative section.
- `Buildup`: Energy ramping up via volume crescendos, rising pitch sweeps, or accelerating snare rolls.
- `Chorus`: Peak impact drop featuring heavy bass and the main melodic motif.
- `Breakdown`: De-energized breather section where drums cut out and melodic pads/vocals remain. Protected by minimum dwell persistence (at least 20s / 10 bars) to prevent premature flipping.
- `Outro`: Stripped-back mix-out section leading toward track conclusion.
- `Unknown`: Audio is either silent, ambient noise, or still undergoing initial classification.

---

### ⚡ Music Energy
- **Domain**: `sensor`
- **Device Class**: `measurement`
- **Values / Unit**: `0` – `100 %`
- **Icon**: `mdi:lightning-bolt`
- **MQTT Topic**: `<device>/audio/energy`
- **Update Frequency**: 1 Hz + on state change + periodic heartbeat
*(Available when `experimental-music` is enabled)*

Quantized acoustic and musical energy level:
- `0%`: Silence or non-music audio.
- `10%`: Ambient background sound.
- `30%`: Outro.
- `40%`: Breakdown.
- `50%`: Buildup.
- `60%`: Intro.
- `80%`: Verse.
- `100%`: Chorus / Drop.

---

### ⏱️ BPM
- **Domain**: `sensor`
- **Device Class**: `measurement`
- **Values / Unit**: `60` – `240 bpm` (`0` when inactive)
- **Icon**: `mdi:metronome`
- **MQTT Topic**: `<device>/audio/bpm`
- **Update Frequency**: 1 Hz + on state change + periodic heartbeat
*(Available when `experimental-music` is enabled)*

Detected musical tempo in Beats Per Minute. Rejects octave jumping and eighth-note jitter. Locks during steady beat sections (Verse/Chorus), persists across quiet breakdowns and bridges, and resets cleanly to `0` when music ends.

---

### 💓 Music Beat Probability
- **Domain**: `sensor`
- **Device Class**: `measurement`
- **Values / Unit**: `0.0` – `100.0 %`
- **Icon**: `mdi:pulse`
- **MQTT Topic**: `<device>/audio/beat_probability`
- **Update Frequency**: 1 Hz + periodic heartbeat
*(Available when `experimental-music` is enabled)*

Confidence score indicating the presence of regular low-frequency rhythmic beats (0% = silence or non-rhythmic noise, 100% = persistent, high-confidence bass drum / beat sequence).

---

### 🩺 Audio Source Status
- **Domain**: `sensor`
- **Device Class**: Diagnostic
- **Values**: Text string (e.g. `OK (Coupled with AudioReactive)`)
- **Icon**: `mdi:microphone-outline`
- **MQTT Topic**: `<device>/audio/status`
- **Update Frequency**: On state change + periodic heartbeat

Diagnostics reporting the coupling health between AudioClassifier and WLED's AudioReactive engine. Displays warnings if AudioReactive is disabled, if AGC is enabled, or if no microphone frames have been received.

---

## 🚀 Installation & Flashing

Pre-compiled binaries are available on the **[Releases](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases)** page.

### Method 1: Web Browser OTA Update (Existing WLED Device)
1. Download the latest OTA binary (`*_ota.bin`, e.g. `wled_audio_classifier_esp32dev_ota.bin`) from [Releases](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases).
2. Open your controller's web interface (`http://<wled-ip>`).
3. Navigate to **Config (⚙️) > Security & Updates**.
4. Under **Manual OTA Update**, click **Choose File**, select the `*_ota.bin` file, and click **Update!**.
5. The device reboots after ~30–45 seconds, preserving your existing WiFi credentials and LED settings.
6. Verify under **Config > Usermods** that both `AudioReactive` and `AudioClassifier` are enabled.
7. Ensure your MQTT broker is configured under **Config > Sync Interfaces > MQTT** so entities can publish to Home Assistant.

### Method 2: Browser USB Web Flasher (New or Unconfigured Device)
1. Connect your controller to your computer with a USB data cable.
2. Download the latest factory binary (`*_factory.bin`) from [Releases](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases).
3. Open **[ESP Web Tools](https://web.esphome.io/)** or **[Adafruit ESPTool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/)** in Chrome or Edge.
4. Click **Connect**, select your serial port, and flash the `*_factory.bin` at address `0x0000`.
5. Connect to the setup Wi-Fi hotspot (`WLED-AP`, password: `wled1234`) to configure your network credentials.

### Method 3: Compile Locally with PlatformIO
Add the usermod to your `platformio_override.ini`:
```ini
[platformio]
default_envs = esp32dev

[common]
default_usermods = audioreactive

[env:esp32dev]
custom_usermods =
  ${common.default_usermods}
  https://github.com/Steppschuh/wled-usermod-audio-classifier.git
```
Then build and flash:
```bash
pio run -e esp32dev -t upload
```

---

## ⚙️ Configuration & Tuning

### 1. AudioClassifier Usermod Settings
Configure under **Config > Usermods > AudioClassifier**:

| Setting | Default | Description |
| :--- | :--- | :--- |
| **`Enabled`** | `true` | Master toggle for audio classification. |
| **`MQTT Auto-Discovery`** | `true` | Automatically publish Home Assistant MQTT discovery entities. |
| **`Quiet Threshold`** | `6.0 %` | Absolute silence cutoff floor. |
| **`Margin Above Floor`** | `4.0 %` | Trigger margin required above ambient noise floor. |
| **`On Delay`** | `1200 ms` | Continuous sound duration required before triggering presence. |
| **`Off Delay`** | `8000 ms` | Continuous silence duration required before clearing presence. |
| **`Music Hold`** | `30000 ms` | Hold time in music state during quiet bridges/breakdowns. |
| **`Experimental Music`** | `false` | Enable beat regularity and musical phrasing tracking (`Intro`, `Verse`, `Buildup`, `Chorus`, `Breakdown`, `Outro`). |
| **`Stream Raw`** | `false` | Stream raw audio frames over MQTT (`wled/<device>/audio/raw`) for analysis. |

### 2. AudioReactive Settings
Open **Config > Usermods > AudioReactive** to configure the audio input stage:

| Setting | Recommended | Meaning & Impact |
| :--- | :--- | :--- |
| **`enabled`** | `Checked` | Required for audio acquisition. |
| **`AGC` (Gain Control)** | **`0 (Off)`** | **`0 (Off)`** provides true room dynamics (recommended for acoustic presence).<br>**`1 (Normal)`** or **`3 (Lazy)`**: Adapts gain over time for lighting effects while keeping sensing usable.<br>**`2 (Vivid)`**: Aggressively boosts quiet sounds, causing volume levels to float. |
| **`squelch`** | `4` – `10` | Baseline noise gate. Cuts out microphone electrical self-noise and inaudible hiss. |
| **`gain`** | `40` – `70` | Digital amplification multiplier. Scales signal so normal conversation hits ~20–35%. |

#### Diagnostic Statuses
The info modal (ℹ️) and Home Assistant report coupling health:
- `OK (Coupled with AudioReactive)`: Operational.
- `Warning: AudioReactive is disabled in settings`: Enable AudioReactive in Usermods settings.
- `Warning: AudioReactive AGC is enabled (relative levels will float)`: Relative levels adjust dynamically with AGC.
- `Info: AudioReactive is in network receive mode`: Audio telemetry is streamed from another WLED instance.
- `Warning: No audio signal detected from AudioReactive (>30s)`: Check microphone hardware wiring.
- `Error: AudioReactive usermod not present in build`: Ensure firmware includes AudioReactive.

### 3. Step-by-Step Calibration Guide

Follow this 3-step procedure using WLED's live status readouts (in the **Info modal ℹ️** or Home Assistant `Sound Level`):

#### Step 1: Calibrate Squelch (Noise Gate)
1. Keep the room **completely quiet** (no speech, no media, only resting ambient background).
2. In **Config > Usermods > AudioReactive**, set **`AGC` to `0 (Off)`**, **`Gain` to `50`**, and **`Squelch` to `0`**. Save.
3. Observe `Sound Level (%)`. With squelch at `0`, microphone electrical self-noise will typically produce a jittery `2% – 8%` reading.
4. Increase **`Squelch` in steps of +2** (e.g. `2`, `4`, `6`, `8`...) and save until the silent room baseline drops to **`0.0% – 1.0%`**.
   * *Typical values*: `4`–`8` for I2S/PDM digital microphones, `8`–`15` for analog microphones.

#### Step 2: Calibrate Gain (Sensitivity)
1. Speak at your **normal conversational volume** from where you typically sit in the room (or play typical background music).
2. Check the live `Sound Level (%)`:
   * **Target**: Conversational speech should register around **`20% – 35%`**.
   * **Target**: Loud claps or hand gestures should peak around **`70% – 90%`** without constantly clipping at `100%`.
3. Adjust **`Gain`**:
   * If normal speech registers below `15%`: Increase Gain (e.g. `65`–`80`).
   * If normal speech easily pegs at `80% – 100%`: Decrease Gain (e.g. `35`–`45`).
   * *Typical values*: `40`–`70`.

#### Step 3: Set AudioClassifier Thresholds
1. With Squelch and Gain dialed in, observe the resting room baseline with normal background sounds (HVAC, fans) running (e.g. `2.5%`).
2. In **Config > Usermods > AudioClassifier**:
   * Set **`Quiet Threshold`** to **~2% – 3% above** your quiet room baseline (e.g. `5.0%` for a `2.5%` baseline).
   * Leave **`Margin Above Floor`** at `4.0%` (sound must rise 4% above the rolling ambient floor to trigger).
   * If brief transient noises (footfalls, closing doors) trigger presence, increase **`On Delay`** (`1500`–`2000 ms`).
   * If presence clears during quiet song bridges or pauses between tracks, increase **`Off Delay`** (`10000`–`15000 ms`).

#### Quick Troubleshooting

| Symptom | Cause | Action |
| :--- | :--- | :--- |
| Baseline never drops to `0–1%` in dead silence | Squelch too low | **Increase Squelch** (+2) |
| Whispers or soft speaking aren't detected | Squelch too high | **Decrease Squelch** (-2) |
| Normal speech registers below `15%` | Gain too low | **Increase Gain** |
| Ambient background triggers high volume | Gain too high | **Decrease Gain** |
| Reported volume drifts or floats over time | AudioReactive AGC enabled | **Set AGC to `0 (Off)`** |

---

## 📬 MQTT Topic Structure

Topics derive from WLED's configured MQTT Device Topic (`wled/<mac>`):

| Subtopic | Format | Update Rate | Description |
| :--- | :--- | :--- | :--- |
| `<device>/audio/level` | Float (`24.5`) | 1 Hz + Heartbeat | 1-second RMS average sound percentage |
| `<device>/audio/peak` | Float (`45.2`) | 1 Hz + Heartbeat | 1-second maximum sound peak percentage |
| `<device>/audio/detected` | `ON` / `OFF` | On change + Heartbeat | Debounced sound presence |
| `<device>/audio/type` | String (`Silence`, `Ambient Noise`, `Music`) | On change + Heartbeat | Semantic acoustic type (also mirrored to `audio/state`) |
| `<device>/audio/energy` | Integer (`0` – `100`) | 1 Hz + On change + Heartbeat | Track acoustic energy percentage |
| `<device>/audio/bpm` | Integer (`128`) | 1 Hz + On change + Heartbeat | Detected rhythm speed (BPM) |
| `<device>/audio/beat_probability` | Float (`0.0` – `100.0`) | 1 Hz + Heartbeat | Beat presence probability based on low-frequency variance |
| `<device>/audio/phrase` | String (`Intro`, `Verse`, `Buildup`, `Chorus`, `Breakdown`, `Outro`) | On change + Heartbeat | Phrasing state (if enabled) |
| `<device>/audio/status` | String (diagnostic message) | On change + Heartbeat | AudioReactive coupling state |

- **Availability**: Tracked automatically via `<device>/status` (`online` / `offline`).
- **Clean Unenrollment**: Turning off `mqtt-auto-discovery` publishes empty retained messages to automatically remove entities from Home Assistant.

---

## 🧩 Architecture & Technical Design

### Noise Floor Estimation & Stability
Simple moving-average baselines suffer from **baseline creep**: continuous music or conversation gradually inflates the baseline, causing the system to classify ongoing sound as ambient room silence.

AudioClassifier solves this using two mechanisms:
1. **10th-Percentile Sliding Window**: Sound levels are binned into a 101-bucket histogram over a 300-second (5-minute) circular buffer. The true room noise floor is extracted at the 10th percentile.
2. **Noise Floor Freezing**: Whenever sound presence is detected (`SOUND` state), histogram ingestion is frozen. The baseline remains locked to the true room baseline even during hours of continuous music. Once sound stops and the `off-delay` timer elapses, normal background tracking resumes.

### Beat Regularity & Tempo Tracking
Beat detection processes energy in sub-bass and bass frequency bands (Bands 0 and 1). A circular history of peak timestamps calculates inter-beat intervals (IBI), rejects spurious eighth-note harmonics, and locks into stable BPM (60–240 BPM). Tempo estimates are held across quiet breakdowns rather than dropping to zero.

### Phrasing State Machine & Anti-Churn Stability
The phrase classifier models the two-wave tension and release structure of electronic music:
- Transitions into **Breakdown** enforce minimum dwell persistence ($\ge 20$s / 10 bars) to prevent rapid bouncing into `Outro`.
- Transitions into **Buildup** identify volume crescendos without kicks, rising pitch sweeps, and snare rolls.
- Hysteresis prevents oscillation between quiet breakdowns and outros, with an automated phrase stability penalty test guarding against micro-flips (< 2s).

---

## 🧪 Testing & Development

The core classification logic (`src/classifier.{h,cpp}`) is 100% decoupled from the ESP32 hardware and WLED framework. You can run all 25 unit tests on your host machine in under 2 seconds:

```bash
pio test -e native
```

### Recording Real-World Telemetry & Adding Tests
You can capture live acoustic telemetry from an active controller and turn it into a native test case:
```bash
# Record live frames from WLED via MQTT and save a synchronized JSON test fixture:
python3 tools/record_session.py --broker <mqtt-ip> --wled-ip <wled-ip> --duration 60 --label music

# Replay and inspect with interactive visualization:
python3 tools/replay_session.py test/fixtures/session_*.json --plot
```
Fixtures are replayed and verified directly within the Unity test suite via `FixtureReplayer`. See [AGENTS.md](AGENTS.md) and [CONTRIBUTING.md](CONTRIBUTING.md) for step-by-step instructions.

---

## 📄 License

Licensed under the **European Union Public Licence v1.2** ([EUPL-1.2](LICENSE)).

*Disclaimer: This project is an independent community open-source usermod and is not affiliated with or endorsed by Gledopto or the core WLED project.*
