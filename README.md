# WLED Audio Classifier Usermod

[![Build Firmware](https://github.com/Steppschuh/wled-usermod-audio-classifier/actions/workflows/build-firmware.yml/badge.svg)](https://github.com/Steppschuh/wled-usermod-audio-classifier/actions/workflows/build-firmware.yml)
[![License: EUPL-1.2](https://img.shields.io/badge/License-EUPL--1.2-blue.svg)](LICENSE)
[![GitHub Release](https://img.shields.io/github/v/release/Steppschuh/wled-usermod-audio-classifier?include_prereleases&sort=date&style=flat)](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases)

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
A debounced binary sensor reporting `ON` when active sound is present and `OFF` when the room is quiet. It filters out brief acoustic transients like door slams or hand claps using a configurable on-delay (1.2s default) and prevents presence automations from flapping between songs via an off-delay (8.0s default).

---

### 🎶 Audio Type
Contextual acoustic environment sensor that classifies the room into one of three states:
- `Silence`: Room noise is at or below the resting ambient noise floor.
- `Ambient Noise`: Sustained non-musical sound such as conversation, television dialogue, HVAC fans, or kitchen activity.
- `Music`: Harmonically organized sound featuring persistent rhythmic pulses or musical structure.

---

### 📊 Audio Level RMS
Continuous measurement reporting the 1-second rolling RMS (Root Mean Square) average volume from `0.0%` to `100.0%`. Updated every second, making it ideal for dashboard gauges, sound level charts, and automation conditions.

---

### 📈 Audio Level Peak
Continuous measurement reporting the maximum acoustic peak recorded over the last 1-second window from `0.0%` to `100.0%`. Captures instantaneous transients, loud impacts, and percussion hits.

---

### 🎼 Music Phrase
*(Available when `experimental-music` is enabled)*

Song macro-structure classification designed around classic tension-and-release music arrangement:
- `Intro`: Stripped-back initial beat or melodic theme.
- `Verse`: Steady vocal or melodic groove.
- `Buildup`: Escalating riser, snare roll, or volume crescendo building toward a drop.
- `Chorus`: Climax drop with high-impact bass and full arrangement.
- `Breakdown`: De-energized breather section where drums drop out; holds for at least 20 seconds before decaying.
- `Outro`: Stripped-back mix-out section at the end of a track.
- `Unknown`: Default state during silence, ambient noise, or initial analysis.

---

### ⚡ Music Energy
*(Available when `experimental-music` is enabled)*

Quantized dynamic track and room energy measurement ranging from `0%` to `100%`:
- `0%`: Silence or non-musical audio.
- `10%`: Ambient background sound.
- `30%`: Outro.
- `40%`: Breakdown.
- `50%`: Buildup.
- `60%`: Intro.
- `80%`: Verse.
- `100%`: Chorus / Drop.

---

### ⏱️ BPM
*(Available when `experimental-music` is enabled)*

Detected musical tempo ranging from `60` to `240 bpm` (`0` when music is not active). Rejects octave jumping and eighth-note jitter, locks during steady beat sections, and is held stably across quiet breakdowns.

---

### 💓 Music Beat Probability
*(Available when `experimental-music` is enabled)*

Confidence score from `0.0%` to `100.0%` indicating the likelihood of regular rhythmic beats based on low-frequency variance analysis (0% represents non-rhythmic noise or silence, while 100% indicates consistent, high-confidence bass rhythm).

---

### 🩺 Audio Source Status
Diagnostic sensor reporting live coupling health between AudioClassifier and WLED's AudioReactive engine (e.g. `OK (Coupled with AudioReactive)`), alerting you if AudioReactive is disabled, if AGC is active, or if microphone frames stop arriving.

---

## 💡 Example Automations & Use Cases

Looking for inspiration? Here are a few ways to supercharge your smart home using these entities:

- **Automatic Music Mode**: Switch from a cozy ambient light preset to an energetic Audio-Reactive mode as soon as music starts playing, and smoothly revert to relaxing ambient lighting once the music stops.
- **Song Structure & Phrase Lighting**: Dynamically adapt lighting effects and intensity to track arrangement. Unleash peak brightness and vivid palettes on the **Chorus** / Drop, trigger accelerating risers during a **Buildup**, and settle into a gentle breathing pulse during a **Breakdown**.
- **BPM-Synchronized Speed**: Scale WLED effect speeds proportionally to the detected track tempo so LED pulses, waves, and color sweeps groove in time with the beat.
- **True Room Occupancy**: Combine PIR motion or mmWave sensors with acoustic presence (`Audio Detected`) to eliminate false "room clear" timeouts while you are sitting still watching movies, reading, or conversing.
- **Late-Night Disturbance Alerts**: Send a subtle notification or flash a hallway strip if sudden acoustic spikes or loud noises occur late at night.

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

### Setting Up MQTT in Home Assistant & WLED

To receive auto-discovered entities, WLED and Home Assistant must be connected to the same MQTT broker:

1. **Configure MQTT in Home Assistant**:
   - Ensure the [Home Assistant MQTT Integration](https://www.home-assistant.io/integrations/mqtt/#broker-configuration) is set up in Home Assistant (**Settings > Devices & Services > Add Integration > MQTT**).
   - If you do not have an MQTT broker running yet, install the official **Mosquitto broker** add-on via **Settings > Add-ons > Add-on Store**.
   - Ensure **Enable discovery** is active in the MQTT integration configuration (enabled by default).
2. **Configure MQTT in WLED**:
   - Open your controller's web interface and navigate to **Config (⚙️) > Sync Interfaces > MQTT**.
   - Check **Enable MQTT**.
   - Enter your MQTT **Broker** IP or hostname (typically your Home Assistant server IP), **Port** (`1883`), **User**, and **Password**.
   - Keep the default **Device Topic** (`wled/%mac%`) or choose a custom topic.
   - Click **Save**.
3. **Verify Discovery**:
   - Home Assistant will automatically detect your WLED controller and add all AudioClassifier entities under the WLED device page.

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
