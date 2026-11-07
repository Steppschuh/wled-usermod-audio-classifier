![Cover Image](media/cover.png)

# WLED Audio Classifier Usermod

[![Build Firmware](https://github.com/Steppschuh/wled-usermod-audio-classifier/actions/workflows/build-firmware.yml/badge.svg)](https://github.com/Steppschuh/wled-usermod-audio-classifier/actions/workflows/build-firmware.yml)
[![License: EUPL-1.2](https://img.shields.io/badge/License-EUPL--1.2-blue.svg)](LICENSE)
[![GitHub Release](https://img.shields.io/github/v/release/Steppschuh/wled-usermod-audio-classifier?include_prereleases&sort=date&style=flat)](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases)

A [WLED](https://github.com/wled/WLED) usermod (v2) that turns your controller's microphone into an acoustic presence and music sensor for Home Assistant over MQTT.

While WLED's built-in AudioReactive usermod uses microphone input to animate LED strips, this usermod taps into that same audio stream in memory to classify room sound. It detects presence, identifies sound types (silence, speech/noise, music), tracks tempo (BPM), and recognizes song sections (intro, verse, buildup, drop/chorus, breakdown, outro). All entities appear automatically in Home Assistant through MQTT auto-discovery, with no manual YAML required and no impact on LED animation frame rates.

---

## Why audio classification?

Standard presence sensors have well-known blind spots:
- PIR motion detectors trigger false "room clear" timeouts when people are sitting still reading, working, or watching a movie.
- mmWave radar detects breathing and small movements, but cannot tell whether a room is quiet, active with conversation, or playing music.

Commercial WLED controllers (such as the Gledopto GL-C-015WL-D or GL-C-010WL) and DIY ESP32 builds with microphones are often already placed in living rooms, kitchens, and media spaces. This usermod reuses that hardware as a room audio sensor without interfering with normal audio-reactive lighting effects.

---

## Features

- **Home Assistant auto-discovery**: Publishes MQTT discovery payloads that attach entities directly to your existing WLED device in Home Assistant.
- **AudioReactive coexistence**: Reads FFT and volume data in memory (`getUMData`) without competing for I2S bus access or dropping frames.
- **Adaptive noise floor**: Tracks ambient room sound using a 10th-percentile filter over a 5-minute rolling window, freezing during active audio so continuous music does not skew the baseline.
- **Debounced presence**: Configurable on-delay (default 1.2 s) and off-delay (default 8.0 s) filter out claps, door slams, and pauses between songs.
- **Song structure and phrase tracking**: Identifies intro, verse, buildup, chorus/drop, breakdown, and outro with hysteresis to prevent rapid state flipping.
- **BPM and beat confidence**: Estimates tempo between 60 and 240 bpm with octave rejection, holding the value across quiet breakdowns.
- **Fully local**: Runs entirely on the ESP32 in volatile RAM. No audio is recorded, saved, or streamed over the network.

---

## Home Assistant entities

When MQTT is enabled, these entities are added to your WLED device in Home Assistant:

### Audio Detected
Binary sensor (`sound` device class) that turns `on` during sustained sound and `off` when the room is quiet. Default delays require 1.2 s of continuous sound to trigger and 8.0 s of silence to clear, filtering out short noises like door slams.

### Audio Type
Contextual sensor that classifies the room into one of three states:
- `Silence`: Sound level is at or below the ambient room baseline.
- `Ambient Noise`: Sustained non-musical sound such as conversation, TV dialogue, fans, or kitchen activity.
- `Music`: Rhythmic and harmonic sound with structural musical properties.

### Music Phrase
Song structure classification based on tension-and-release electronic music arrangement:
- `Intro`: Initial beat or melodic introduction.
- `Verse`: Steady melodic groove or vocal section.
- `Buildup`: Rising snare roll, pitch riser, or volume crescendo before a drop.
- `Chorus`: Main drop or high-energy section.
- `Breakdown`: Quieter section where drums drop out; holds for at least 20 seconds before decaying.
- `Outro`: Ending section of a track.
- `Unknown`: Default state during silence, ambient noise, or before enough frames are collected.

### Music Energy
Dynamic energy metric (`0%` to `100%`) mapped to distinct, non-overlapping windows that breathe with live volume and song tension:
- `0%`: Silence or non-musical audio.
- `15% – 25%`: Breakdown (quiet breather, modulated by ambient pad and vocal presence).
- `30% – 40%`: Outro (mix-out section modulated by remaining percussion level).
- `45% – 55%`: Intro (incoming beat and initial melodic theme).
- `65% – 78%`: Verse (main steady groove modulated by vocal and bass punch).
- `80% – 95%`: Buildup (crescendo ramp escalating with riser volume and elapsed phrase duration).
- `95% – 100%`: Chorus / drop (high-impact climax hitting 100% on bass drops).

### Music Beat Probability
Confidence score from `0.0%` to `100.0%` indicating rhythm regularity based on low-frequency variance analysis (0% represents non-rhythmic noise or silence; 100% indicates a steady bass beat).

### BPM
Detected musical tempo from `60` to `240 bpm` (`0` when music is not active). Rejects octave doubling and syncopated eighth notes, and holds steady through quiet breakdowns.

### Audio Level RMS
Diagnostic sensor reporting the continuous 1-second rolling RMS sound level from `0.0%` to `100.0%`. Updates every second for dashboard gauges and automation conditions.

### Audio Level Peak
Diagnostic sensor reporting the peak volume recorded over the last 1-second window (`0.0%` to `100.0%`). Tracks instantaneous transients, percussion hits, and loud impacts.

### Audio Source Status
Diagnostic sensor reporting coupling health with WLED's AudioReactive engine (`OK`, `AudioReactive mod disabled`, or `AudioReactive mod missing`).

---

## Example automations

- **Automatic audio-reactive lighting**: Switch from warm static lighting to an audio-reactive preset when music starts playing, and return to ambient lighting when it stops.
- **Song-aware lighting effects**: Increase brightness and switch to vivid color palettes during the chorus or drop, speed up animations during buildups, and dim to a slow pulse during breakdowns.
- **Tempo-matched animation speed**: Sync WLED effect speed to detected BPM so light pulses and color sweeps match the music tempo.
- **Occupancy fallback**: Combine motion sensors with `Audio Detected` to keep lights on while people are talking, reading, or watching TV without moving.
- **Late-night disturbance detection**: Flash a nightlight or send a notification if loud sounds occur during quiet hours.

---

## Installation & flashing

Pre-compiled binaries are available on the [Releases](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases) page.

### Method 1: OTA update from WLED web UI (existing device)
1. Download the release binary (e.g. `WLED_16.0.1_MOD_0.2.0_ESP32.bin`) from [Releases](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases).
2. Open your controller's web interface (`http://<wled-ip>`).
3. Navigate to **Config > Security & Updates**.
4. Under **Manual OTA Update**, click **Choose File**, select the `.bin` file, and click **Update!**.
5. Wait ~30–45 seconds for the controller to reboot. Existing Wi-Fi and LED settings are preserved.
6. Check **Config > Usermods** to confirm that both `AudioReactive` and `AudioClassifier` are enabled.

### Method 2: USB web flasher (new or unconfigured device)
1. Connect the controller to your computer with a USB data cable.
2. Download the factory binary (`*_factory.bin`, e.g. `WLED_16.0.1_MOD_0.2.0_ESP32_factory.bin`) from [Releases](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases).
3. Open [ESP Web Tools](https://web.esphome.io/) or [Adafruit ESPTool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/) in Chrome or Edge.
4. Click **Connect**, select your serial port, and flash the `*_factory.bin` at offset `0x0000`.
5. Connect to the setup Wi-Fi hotspot (`WLED-AP`, password: `wled1234`) to configure your network credentials.

### Method 3: Build with PlatformIO
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
Then compile and upload:
```bash
pio run -e esp32dev -t upload
```

### MQTT setup in Home Assistant and WLED

To receive auto-discovered entities, WLED and Home Assistant must connect to the same MQTT broker:

1. **Configure MQTT in Home Assistant**:
   - Ensure the [Home Assistant MQTT integration](https://www.home-assistant.io/integrations/mqtt/#broker-configuration) is set up under **Settings > Devices & Services > Add Integration > MQTT**.
   - If you do not have an MQTT broker running, install the official **Mosquitto broker** add-on via **Settings > Add-ons > Add-on Store**.
   - Make sure **Enable discovery** is turned on in the MQTT integration options (enabled by default).
2. **Configure MQTT in WLED**:
   - In the WLED web UI, go to **Config > Sync Interfaces > MQTT**.
   - Check **Enable MQTT**.
   - Enter your broker IP or hostname, port (`1883`), username, and password.
   - Leave the default topic (`wled/%mac%`) or enter a custom one.
   - Click **Save**.
3. **Verify discovery**:
   - Home Assistant should automatically detect the controller and display the AudioClassifier entities under the WLED device card.

---

## Configuration & tuning

### 1. AudioClassifier settings
Configure under **Config > Usermods > AudioClassifier**:

| Setting | Default | Description |
| :--- | :--- | :--- |
| **`Enabled`** | `true` | Master toggle for audio classification. |
| **`MQTT Auto-Discovery`** | `true` | Automatically publish Home Assistant MQTT discovery entities. |
| **`Quiet Threshold`** | `6.0 %` | Absolute silence cutoff floor. |
| **`Margin Above Floor`** | `4.0 %` | Margin required above ambient noise floor to trigger sound. |
| **`On Delay`** | `1200 ms` | Continuous sound duration required before triggering presence. |
| **`Off Delay`** | `8000 ms` | Continuous silence duration required before clearing presence. |
| **`Music Hold`** | `30000 ms` | Hold time in music state during quiet bridges and breakdowns. |
| **`Stream Raw`** | `false` | Stream raw audio frames over MQTT (`wled/<device>/audio/raw`) for analysis. |

### 2. AudioReactive settings
Under **Config > Usermods > AudioReactive**, set up the input stage:

| Setting | Recommended | Meaning & Impact |
| :--- | :--- | :--- |
| **`enabled`** | `Checked` | Required for audio input. |
| **`AGC` (Gain Control)** | **`0 (Off)`** | **`0 (Off)`** preserves true room dynamics (recommended for presence).<br>**`1 (Normal)`** or **`3 (Lazy)`**: Adjusts gain gradually for lights while keeping sensing usable.<br>**`2 (Vivid)`**: Aggressively boosts quiet sounds, making volume levels float. |
| **`squelch`** | `4` – `10` | Noise gate. Cuts out microphone electrical noise and background hiss. |
| **`gain`** | `40` – `70` | Digital amplification. Scale so normal conversation registers around 20–35%. |

#### Status messages
The info modal and Home Assistant sensor report coupling health:
- `OK (Coupled with AudioReactive)`: Normal operation.
- `Warning: AudioReactive is disabled in settings`: Enable AudioReactive in Usermods settings.
- `Warning: AudioReactive AGC is enabled (relative levels will float)`: Sound levels adjust dynamically with AGC.
- `Info: AudioReactive is in network receive mode`: Telemetry is streaming from another WLED instance.
- `Warning: No audio signal detected from AudioReactive (>30s)`: Check microphone wiring and I2S configuration.
- `Error: AudioReactive usermod not present in build`: Rebuild firmware with AudioReactive included.

### 3. Calibration walkthrough

Use live status readouts (in the WLED **Info modal** or the Home Assistant `Audio Level RMS` sensor):

#### Step 1: Set squelch (noise gate)
1. Ensure the room is quiet (no speech, no media, only resting background sound).
2. Under **Config > Usermods > AudioReactive**, set **`AGC` to `0 (Off)`**, **`Gain` to `50`**, and **`Squelch` to `0`**. Save.
3. Check `Sound Level (%)`. With squelch at `0`, microphone noise typically shows a fluctuating `2% – 8%` reading.
4. Increase **`Squelch` in steps of +2** (e.g. `2`, `4`, `6`, `8`) and save until the quiet room baseline drops to **`0.0% – 1.0%`**.
   * *Typical values*: `4`–`8` for I2S/PDM digital mics; `8`–`15` for analog mics.

#### Step 2: Set gain (sensitivity)
1. Speak at normal conversational volume from typical seating distance (or play background music at your normal listening level).
2. Check `Sound Level (%)`:
   * Normal conversation should sit around **`20% – 35%`**.
   * Loud claps or hand gestures should peak around **`70% – 90%`** without sustained clipping at `100%`.
3. Adjust **`Gain`**:
   * If conversation stays below `15%`: Increase gain (e.g. `65`–`80`).
   * If conversation regularly hits `80% – 100%`: Decrease gain (e.g. `35`–`45`).
   * *Typical values*: `40`–`70`.

#### Step 3: Tune AudioClassifier thresholds
1. Note the resting baseline with background fans or HVAC running (e.g. `2.5%`).
2. Under **Config > Usermods > AudioClassifier**:
   * Set **`Quiet Threshold`** ~2–3% above the quiet room baseline (e.g. `5.0%` for a `2.5%` baseline).
   * Leave **`Margin Above Floor`** at `4.0%`.
   * If footfalls or closing doors trigger presence, increase **`On Delay`** (`1500`–`2000 ms`).
   * If presence clears during quiet track pauses, increase **`Off Delay`** (`10000`–`15000 ms`).

#### Quick troubleshooting

| Symptom | Cause | Action |
| :--- | :--- | :--- |
| Baseline never drops to `0–1%` in a quiet room | Squelch too low | Increase squelch (+2) |
| Soft speech is not detected | Squelch too high | Decrease squelch (-2) |
| Normal speech stays below `15%` | Gain too low | Increase gain |
| Ambient background noise reads as high volume | Gain too high | Decrease gain |
| Volume drifts or floats over time | AudioReactive AGC is on | Set AGC to `0 (Off)` |

---

## Technical design

### Noise floor tracking
Simple moving averages suffer from baseline creep: continuous music or conversation gradually inflates the average until active sound is classified as room silence.

AudioClassifier prevents this with two controls:
1. **10th-percentile sliding window**: Levels are binned into a 101-bucket histogram across a 300-second circular buffer. The baseline is sampled at the 10th percentile rather than the mean.
2. **Freezing during sound**: When presence is active (`SOUND` state), histogram ingestion pauses. The noise floor remains locked to the quiet room baseline during long listening sessions, resuming only after sound stops and the off-delay timer elapses.

### Beat tracking and tempo
Beat detection analyzes energy in the lowest frequency bands (bands 0 and 1). Timestamps of detected peaks are tracked in a circular buffer to calculate inter-beat intervals, filter out eighth-note sub-harmonics, and lock onto tempo (60–240 BPM). The detected tempo is maintained across breakdowns instead of resetting immediately to zero.

### Song phrase state machine
The phrase engine tracks the structural progression common in electronic tracks:
- **Breakdown**: Requires at least 20 seconds (or 10 bars) without drums before decaying to `Outro`, preventing premature state drops on quiet passages.
- **Buildup**: Detects volume crescendos, rising pitch sweeps, and snare rolls without regular kick hits.
- **Anti-churn hysteresis**: Enforces minimum state dwell times to prevent rapid back-and-forth transitions between quiet breakdowns and outros.

---

## Testing & development

The core classifier logic (`src/classifier.{h,cpp}`) has no hardware or WLED dependencies and compiles cleanly for native host testing:

```bash
pio test -e native
```

### Capturing session fixtures
To record acoustic telemetry from a running controller for regression testing:
```bash
# Record frames over MQTT into a synchronized JSON test fixture:
python3 tools/record_session.py --broker <mqtt-ip> --wled-ip <wled-ip> --duration 60 --label music

# Replay and plot telemetry offline:
python3 tools/replay_session.py test/fixtures/session_*.json --plot
```

Fixtures are replayed directly in the native Unity test suite with `FixtureReplayer`. See [AGENTS.md](AGENTS.md) and [CONTRIBUTING.md](CONTRIBUTING.md) for details.

---

## License

Licensed under the [European Union Public Licence v1.2](LICENSE) (EUPL-1.2).

*This project is an independent community usermod and is not affiliated with or endorsed by Gledopto or the WLED project.*
