# AI Agent & Developer Guide: WLED Audio Classifier

This document provides architectural context, development workflows, testing procedures, and deployment recipes for AI agents and human contributors working on `wled-usermod-audio-classifier`.

---

## 🏛️ Architecture & Component Boundaries

The codebase is strictly separated into decoupled layers to enable native host testing without ESP32 cross-compilation:

| File | Purpose | Dependencies / Constraints |
| :--- | :--- | :--- |
| `src/classifier.{h,cpp}` | Core acoustic state machine, 10th-percentile noise floor histogram, beat regularity tracker, and musical phrase classifier. | **Pure C++17 only.** Must NOT include Arduino, ESP32, FreeRTOS, or WLED headers. Must compile cleanly in `pio test -e native`. |
| `src/audio_frame.h` | Shared audio frame structure (`levelPercent`, `rawVolume`, `smoothedVolume`, `bands[16]`, `majorPeakHz`, `isPeak`, `timestampMs`). | Header-only, zero dependencies. |
| `src/ar_source.{h,cpp}` | Interface bridge between WLED's `AudioReactive` usermod and `AudioClassifier`. Reads FFT and volume arrays using `getUMData()`. | Depends on WLED and AudioReactive types. Only compiled on ESP32 target. |
| `src/ha_discovery.{h,cpp}` | Home Assistant MQTT Auto-Discovery configuration payloads and telemetry publishing. | Uses `ArduinoJson` (v6 or v7) and WLED MQTT client. |
| `src/usermod_audio_classifier.{h,cpp}` | WLED Usermod V2 hooks (`setup`, `loop`, `addToJsonState`, `readFromJsonState`, `addToConfig`, `readFromConfig`). | WLED framework integration. |
| `tools/replay_session.py` | Python simulation of `classifier.cpp` for rapid offline playback, edge-case debugging, and visualization of recorded audio sessions. | **Keep in parity with `src/classifier.cpp`!** |
| `tools/record_session.py` | MQTT subscriber script to record live audio frames from a physical controller for regression test fixtures. | Requires `paho-mqtt`. |
| `test/` | Unity unit test suite running natively on macOS/Linux. | 25+ comprehensive test cases. |

---

## 🔒 Behavioral Invariants & Contracts

When modifying `src/classifier.cpp` or related logic, maintain these non-negotiable behavioral invariants:

1. **Non-Music Audio State Lock**:
   - If `Audio Type` is NOT `Music`, `Music Energy` MUST be `0%` and `Music Phrase` MUST be `Unknown`.
2. **Silence / Presence Coupling**:
   - If `Audio Type` is `Silence`, `Audio Detected` MUST be `OFF`.
   - If `Audio Detected` is `OFF`, `Audio Type` MUST be `Silence`.
3. **Noise Floor Freezing**:
   - During active sound presence (`SOUND` state), the 10th-percentile noise floor histogram ingestion MUST freeze. This prevents continuous music or speech from inflating the baseline and causing "baseline creep".
4. **Breakdown Persistence**:
   - Transitions into `Breakdown` must dwell for at least 20 seconds (or 10 bars without beats) before decaying into `Outro`. Low acoustic levels (3%–5%) during quiet reverb or pads must NEVER cause a premature 1-second flip to `Outro`.
5. **Outro Recovery Hysteresis**:
   - Returning from `Outro` to `Breakdown` requires sustained musical energy ($\ge 18\%$ 1-second average and $\ge \text{noiseFloor} + 6\%$) to prevent oscillation.
6. **Tempo (BPM) Stability**:
   - BPM estimates must reject octave-jumping (e.g. jumping from 130 to 260 BPM) and eighth-note jitter. BPM is held across quiet song breakdowns and clears cleanly to 0 when music state ends.

---

## 🧪 Testing & Verification

Native tests run directly on the development machine using PlatformIO's `native` environment:

### Running Unit Tests
```bash
# Using standard PlatformIO CLI
pio test -e native

# Or using explicit path on macOS
/Users/steppschuh/.platformio/penv/bin/pio test -e native
```

### Key Test Cases & Coverage
- **`test_initial_state_is_quiet_and_silence`**: Baseline initialization.
- **`test_non_music_state_has_zero_energy_and_unknown_phrase_and_silence_clear_coupling`**: Enforces energy/phrase reset and silence/presence coupling.
- **`test_transient_noise_ignored`**: Verifies that claps and short sounds (< 1.2s on-delay) do not trigger presence.
- **`test_130_bpm_eighth_note_rejection_and_stability`**: Verifies BPM stability against syncopated snare hits.
- **`test_music_hangover_persists_across_breakdown`**: Verifies 30s music hangover across quiet breakdowns.
- **`test_edm_macro_structure_phrase_progression_and_anti_churn`**: Comprehensive 210-second EDM track progression. Checks macro structure (`Intro -> Verse -> Buildup -> Chorus -> Breakdown -> Buildup -> Chorus -> Outro`) and calculates anti-churn metrics:
  - 0 micro-flips (< 2s) allowed.
  - 0 ping-pong oscillations allowed.
  - Minimum stability score $\ge 85\%$.
- **`test_chorus_to_breakdown_does_not_flip_to_outro_one_second_later`**: Verifies that quiet breakdowns (4.5% level) do not eject into Outro after 1 second.

> **Rule for Agents**: ALWAYS run `pio test -e native` before concluding a task or committing code changes. All tests must pass with 0 failures.

---

## ⚡ Firmware Compilation & OTA Deployment

### Directory Setup
WLED builds require the core WLED repository (typically located at `/tmp/wled-core` or cloned alongside the usermod):

```bash
git clone https://github.com/wled/WLED.git /tmp/wled-core
```

### Usermod Integration via `platformio_override.ini`
In `/tmp/wled-core/platformio_override.ini`:

```ini
[platformio]
default_envs = esp32dev

[common]
default_usermods = audioreactive

[env:esp32dev]
custom_usermods =
  ${common.default_usermods}
  wled-usermod-audio-classifier = symlink:///Users/steppschuh/Documents/Projects/Steppschuh/wled-usermod-audio-classifier
```

*(Using `symlink://` ensures local edits in the usermod workspace are instantly reflected in the compilation without copying files).*

### Compilation Command
```bash
/Users/steppschuh/.platformio/penv/bin/pio run -d /tmp/wled-core -e esp32dev
```
The compiled binary will be placed at:
`/tmp/wled-core/.pio/build/esp32dev/firmware.bin`

### Over-The-Air (OTA) Flash Command
Flash the compiled binary to the controller over the local network via HTTP POST:

```bash
curl -F "file=@/tmp/wled-core/.pio/build/esp32dev/firmware.bin" http://<controller-ip>/update
```

A successful update returns:
```html
<h2>Update successful!</h2>Rebooting now...
```

### Verification After Flash
Allow 10–15 seconds for the controller to reboot, then query WLED's JSON API:

```bash
curl -s http://<controller-ip>/json/info | python3 -m json.tool | grep -A 10 '"AudioClassifier"'
```

Expected output confirms the usermod is running:
```json
"AudioClassifier": [
    "3.9% avg / 11.4% peak (Music)",
    "Phrase: Verse (Energy: 80%, 128 BPM)",
    "OK (Coupled with AudioReactive)"
]
```

---

## 🛠️ Offline Simulation & Session Replay

When debugging real-world recordings:
1. **Record a session**:
   ```bash
   python3 tools/record_session.py --broker <mqtt-ip> --topic "wled/<mac>/audio/raw" --output test/fixtures/my_session.csv --duration 120
   ```
2. **Replay and inspect with Python simulator**:
   ```bash
   python3 tools/replay_session.py test/fixtures/my_session.csv --plot
   ```
3. When adjustments are made to `src/classifier.cpp`, replicate identical algorithmic updates in `tools/replay_session.py` to maintain offline simulation parity.

---

## 💡 Best Practices for AI Agents

- **Preserve Documentation Integrity**: Do not remove existing comments, docstrings, or test assertions.
- **Decoupled Architecture**: Do not introduce hardware or WLED dependencies into `src/classifier.h` or `src/classifier.cpp`.
- **Sandbox Permissions**: PlatformIO compilation, network calls (`curl` to hardware IPs), and `git push` require appropriate user permission / sandbox bypass.
- **Zero Regressions**: Any modification to phrasing or energy thresholds must maintain passing status for the entire 25-test suite.
