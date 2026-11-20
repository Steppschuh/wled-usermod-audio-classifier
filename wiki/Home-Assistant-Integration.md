Connecting your WLED Audio Classifier controller to [**Home Assistant**](https://www.home-assistant.io/) exposes **live sensors, switches, and diagnostic entities**—allowing you to build automations around sound presence, audio type (`Silence`, `Ambient Noise`, `Music`), song energy (`0–100%`), song sections (`Chorus`, `Breakdown`, etc.), and tempo (`BPM`).

All entities are discovered automatically over [**MQTT**](https://www.home-assistant.io/integrations/mqtt/) without manual sensor YAML.

### Jump to Your Starting Point
- **[I don't have MQTT set up in Home Assistant yet](#step-1-set-up-an-mqtt-broker-in-home-assistant)** — Install the official Mosquitto broker add-on.
- **[I already have an MQTT broker running in Home Assistant](#step-2-connect-wled-to-your-mqtt-broker)** — Enter your broker details in WLED to trigger auto-discovery.
- **[My entities are already in Home Assistant — show me what they do & example automations](#exposed-entities-reference)** — Jump to the entity guide, calibration steps, and automation YAML examples.

### Table of Contents
- [How the Integration Works](#how-the-integration-works)
- [Step 1: Set Up an MQTT Broker in Home Assistant](#step-1-set-up-an-mqtt-broker-in-home-assistant)
- [Step 2: Connect WLED to Your MQTT Broker](#step-2-connect-wled-to-your-mqtt-broker)
- [Step 3: View Your Entities in Home Assistant](#step-3-view-your-entities-in-home-assistant)
- [Exposed Entities Reference](#exposed-entities-reference)
- [Precision Calibration Using Home Assistant Diagnostics](#precision-calibration-using-home-assistant-diagnostics)
- [Example Automations](#example-automations)
- [Troubleshooting](#troubleshooting)

---

## How the Integration Works

Home Assistant's built-in [**WLED integration**](https://www.home-assistant.io/integrations/wled/) controls your LEDs (power, brightness, colors, and presets). To push real-time audio classification updates without polling lag, the **Audio Classifier** uses [**MQTT Auto-Discovery**](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery).

Once both Home Assistant and your WLED controller connect to the same MQTT broker:
1. The controller announces its audio sensors and switches automatically.
2. Home Assistant attaches them **directly to your existing WLED device page** (matched by the controller's hardware MAC address), so your LED controls and audio entities appear under a single device.

---

## Step 1: Set Up an MQTT Broker in Home Assistant

> **Already using MQTT** (for example with Zigbee2MQTT or Tasmota)? **Skip to [Step 2](#step-2-connect-wled-to-your-mqtt-broker).**

If you don't have an MQTT broker running yet, install the official [Mosquitto broker add-on](https://github.com/home-assistant/addons/tree/master/mosquitto):

1. **Install the Mosquitto Broker Add-on:**
   - In Home Assistant, go to **Settings > Add-ons > Add-on Store** (bottom right).
   - Search for **Mosquitto broker**, select it, and click **Install**.
   - Turn on **Start on boot** and **Watchdog**, then click **Start**.
2. **Create an MQTT User Account (Recommended):**
   - Go to **Settings > People > Users** tab (at the top).
   - Click **Add user**, name it `mqtt` (you can check *Can only log in from the local network*), set a password, and click **Create**.
   - *(The Mosquitto add-on automatically accepts credentials from any Home Assistant user account).*
3. **Enable the [MQTT Integration](https://www.home-assistant.io/integrations/mqtt/):**
   - Go to **Settings > Devices & Services**.
   - Find **Mosquitto broker** under **Discovered** and click **Configure > Submit**.

<!-- Screenshot placeholder: Home Assistant Settings > Devices & Services showing the discovered Mosquitto broker / MQTT integration card -->

---

## Step 2: Connect WLED to Your MQTT Broker

1. Open your WLED controller's web interface (`http://<wled-ip>` or `http://wled-livingroom.local`).
2. Go to **Config > Sync Interfaces** and scroll to [**MQTT**](https://kno.wled.ge/interfaces/mqtt/):
   - Check **Enable MQTT**.
   - **Broker:** Enter your Home Assistant IP address (for example, `192.168.1.100`).
   - **Port:** Leave at `1883`.
   - **Username:** Enter your MQTT username (for example, `mqtt`).
   - **Password:** Enter your MQTT password.
   - **Device Topic:** Leave at the default `wled/%mac%` (or enter a custom topic like `wled/livingroom`).
3. Click **Save** at the bottom of the page.

<!-- Screenshot placeholder: WLED Config > Sync Interfaces > MQTT section showing Enable MQTT checked, Broker IP address, Port 1883, Username, Password, and Device Topic set to wled/%mac% -->

4. Go to **Config > Usermods > AudioClassifier** and confirm **MQTT Auto-Discovery** is checked (enabled by default).
5. If MQTT does not connect right away, go to **Config > Security & Updates** and click **Reboot WLED**.

---

## Step 3: View Your Entities in Home Assistant

1. In Home Assistant, go to **Settings > Devices & Services**.
2. Open the **MQTT** integration (or the **WLED** integration if you have both configured).
3. Select your WLED controller device to view the **Audio Classifier entities** under **Sensors**, **Controls**, and **Diagnostics**.

<!-- Screenshot placeholder: Home Assistant Device page for the WLED controller showing the auto-discovered Audio Classifier sensors, controls, and diagnostic entities merged alongside WLED lighting controls -->

---

## Exposed Entities Reference

### Primary Sensors
| Entity Name | Type | Values / Unit | Description |
| :--- | :--- | :--- | :--- |
| **`Audio Detected`** | Binary Sensor (`sound`) | `On` (Detected) / `Off` (Clear) | Turns `On` after sustained sound (default `1.2 s`) and clears after sustained silence (default `8.0 s`). Ignores brief noises like claps or door slams. |
| **`Audio Type`** | Sensor | `Silence`<br>`Ambient Noise`<br>`Music` | Current acoustic state of the room. Always reads `Silence` when `Audio Detected` is `Off`. |
| **`Music Energy`** | Sensor | `0 %` – `100 %` | Dynamic intensity of the current song (`15–25%` Breakdown, `30–40%` Outro, `45–55%` Intro, `65–78%` Verse, `80–95%` Buildup, `95–100%` Chorus). Resets to `0%` when `Audio Type` is not `Music`. |
| **`Music Phrase`** | Sensor | `Unknown`, `Intro`, `Verse`, `Buildup`, `Chorus`, `Breakdown`, `Outro` | Current structural section of the song. Reads `Unknown` when `Audio Type` is not `Music`. |
| **`BPM`** | Sensor | `60` – `240 bpm` (or `0 bpm`) | Estimated tempo in beats per minute with octave-jump rejection. Holds steady across quiet breakdowns and resets to `0` when music ends. |
| **`Music Beat Probability`** | Sensor | `0.0 %` – `100.0 %` | Rhythm regularity score based on low-frequency beat consistency (`0%` = non-rhythmic sound or silence; `100%` = steady bass beat). |

### Controls (Switches)
| Entity Name | Type | Description |
| :--- | :--- | :--- |
| **`Audio Classifier`** | Switch | Master on/off toggle for the classifier. Turning it off resets all audio sensors to `Silence` / `0%`. |
| **`Auto Switch Presets`** | Switch | Toggles [Standalone Auto-Reactive Preset Switching](Standalone-Preset-Switching) on or off from Home Assistant. |

### Diagnostics
| Entity Name | Type | Values / Unit | Description |
| :--- | :--- | :--- | :--- |
| **`Audio Level RMS`** | Sensor | `0.0 %` – `100.0 %` | Continuous 1-second rolling RMS sound level. Useful for tuning `Squelch`, `Gain`, and `Quiet Threshold`. |
| **`Audio Level Peak`** | Sensor | `0.0 %` – `100.0 %` | Highest sound spike recorded during the last 1-second window. |
| **`Audio Source Status`** | Sensor | `OK`<br>`Clipping, reduce gain`<br>`AudioReactive mod disabled`<br>`AudioReactive mod missing` | Reports microphone input health and alerts if the signal is clipping repeatedly. |

---

## Precision Calibration Using Home Assistant Diagnostics

Watching **`Audio Level RMS`**, **`Audio Level Peak`**, and **`Audio Source Status`** in Home Assistant makes [microphone tuning](https://kno.wled.ge/advanced/audio-reactive/#settings) much more exact:

1. **Squelch (Noise Gate):**
   - Keep the room quiet.
   - In WLED (**Config > Usermods > AudioReactive**), set **AGC** to **`0 (Off)`** and **Gain** to **`50`**.
   - Watch **`Audio Level RMS`** in Home Assistant. If it sits above `1.0%` in silence, increase **Squelch** in steps of **`+2`** until **`Audio Level RMS`** rests at **`0.0% – 1.0%`**.
2. **Gain (Sensitivity):**
   - Speak at normal conversational volume from your typical seating distance.
   - Adjust **Gain** so normal speech reads **`20% – 35%`** on **`Audio Level RMS`**.
   - Play loud music and verify **`Audio Level Peak`** reaches **`70% – 90%`** while **`Audio Source Status`** stays **`OK`**. If `Audio Source Status` switches to `Clipping, reduce gain`, lower **Gain** by `-5` to `-10`.
3. **Classifier Thresholds (Optional):**
   - If your room has constant background noise (such as a fan or HVAC) where **`Audio Level RMS`** rests around `3%`, go to **Config > Usermods > AudioClassifier** and set **`Quiet Threshold`** to **`2–3%` above** that baseline (e.g. `5.5%` or `6.0%`).

<!-- Screenshot placeholder: Home Assistant Diagnostics card or history graph showing Audio Level RMS (0.0% in silence, ~25% in speech, ~75% in music), Audio Level Peak, and Audio Source Status ("OK") -->

---

## Example Automations

If you use **[Standalone Auto-Reactive Preset Switching](Standalone-Preset-Switching)**, the controller already switches effects by song energy on its own. [Home Assistant Automations](https://www.home-assistant.io/docs/automation/) let you go further in two ways:
1. **Controlling WLED & AudioClassifier** using context from other devices in your home.
2. **Controlling other smart home devices** using the audio classification sensors.

*(In the YAML examples below, replace `wled_livingroom` with your WLED device's entity prefix in Home Assistant. To paste YAML into Home Assistant, go to **Settings > Automations & scenes > Create Automation > Create new automation**, click the **⋮** menu in the top right, and select **Edit in YAML**).*

---

### Part 1: Controlling WLED & AudioClassifier from Home Assistant

#### 1. Sync WLED Colors with Other Room Lights (While Standalone Mode Picks the Effect)
Let standalone mode switch between your `AR Low / Mid / High / Max` audio-reactive effects automatically, while Home Assistant keeps the WLED strip's colors matched to another smart lamp or TV backlight:

```yaml
alias: "WLED: Sync RGB Color with Living Room Lamp"
description: "Copies color from main room lamp to WLED while Audio Classifier handles effects"
triggers:
  - trigger: state
    entity_id: light.living_room_lamp
    attribute: rgb_color
conditions:
  - condition: state
    entity_id: light.wled_livingroom
    state: "on"
actions:
  - action: light.turn_on
    target:
      entity_id: light.wled_livingroom
    data:
      rgb_color: "{{ state_attr('light.living_room_lamp', 'rgb_color') }}"
mode: restart
```

#### 2. Pause `Auto Switch Presets` Automatically During Movie Mode
When your TV starts playing a movie, pause `Auto Switch Presets` and select a calm bias-lighting preset so movie soundtracks don't trigger party effects—then re-enable auto-switching when the TV turns off:

```yaml
alias: "WLED: Pause Audio Preset Switching During Movies"
triggers:
  - trigger: state
    entity_id: media_player.living_room_tv
    to: "playing"
    id: tv_playing
  - trigger: state
    entity_id: media_player.living_room_tv
    to:
      - "off"
      - "idle"
    for: "00:01:00"
    id: tv_stopped
actions:
  - choose:
      - conditions:
          - condition: trigger
            id: tv_playing
        sequence:
          - action: switch.turn_off
            target:
              entity_id: switch.wled_livingroom_auto_switch_presets
          - action: select.select_option
            target:
              entity_id: select.wled_livingroom_preset
            data:
              option: "Ambient Warm Glow"
      - conditions:
          - condition: trigger
            id: tv_stopped
        sequence:
          - action: switch.turn_on
            target:
              entity_id: switch.wled_livingroom_auto_switch_presets
mode: single
```

---

### Part 2: Controlling Other Devices with Audio Classification Data

#### 3. Motion + Audio Room Occupancy (Keep Lights On While People Are Talking)
Motion sensors often turn lights off when people sit still talking, reading, or watching TV. Combining a PIR motion sensor with **`Audio Detected`** keeps the room lights on until both motion *and* sound have been clear for 5 minutes:

```yaml
alias: "Living Room: Turn Off Lights Only When Both Motion and Audio Clear"
triggers:
  - trigger: state
    entity_id:
      - binary_sensor.living_room_motion
      - binary_sensor.wled_livingroom_audio_detected
    to: "off"
    for: "00:05:00"
conditions:
  - condition: state
    entity_id: binary_sensor.living_room_motion
    state: "off"
    for: "00:05:00"
  - condition: state
    entity_id: binary_sensor.wled_livingroom_audio_detected
    state: "off"
    for: "00:05:00"
actions:
  - action: light.turn_off
    target:
      entity_id: light.living_room_ceiling
mode: single
```

#### 4. Dim Overhead Room Lights When Music Hits the Chorus
Use **`Audio Type`** and **`Music Phrase`** to dim overhead smart bulbs during high-energy song choruses so your audio-reactive WLED strip stands out, then restore brightness when the music ends:

```yaml
alias: "Living Room: Dim Overhead Bulbs on Chorus Drop"
triggers:
  - trigger: state
    entity_id: sensor.wled_livingroom_music_phrase
    to: "Chorus"
    id: chorus_drop
  - trigger: state
    entity_id: sensor.wled_livingroom_audio_type
    to: "Silence"
    for: "00:00:15"
    id: music_stopped
conditions:
  - condition: state
    entity_id: light.living_room_ceiling
    state: "on"
actions:
  - choose:
      - conditions:
          - condition: trigger
            id: chorus_drop
        sequence:
          - action: light.turn_on
            target:
              entity_id: light.living_room_ceiling
            data:
              brightness_pct: 20
              transition: 2
      - conditions:
          - condition: trigger
            id: music_stopped
        sequence:
          - action: light.turn_on
            target:
              entity_id: light.living_room_ceiling
            data:
              brightness_pct: 75
              transition: 5
mode: restart
```

#### 5. Unexpected Loud Sound Alert While Away
Send a mobile notification if **`Audio Detected`** turns on and **`Audio Level Peak`** exceeds `60%` while nobody is home:

```yaml
alias: "Security: Unexpected Loud Sound While Away"
triggers:
  - trigger: numeric_state
    entity_id: sensor.wled_livingroom_audio_level_peak
    above: 60
conditions:
  - condition: state
    entity_id: binary_sensor.wled_livingroom_audio_detected
    state: "on"
  - condition: state
    entity_id: zone.home
    state: "0"
actions:
  - action: notify.notify
    data:
      title: "Loud Sound Detected at Home"
      message: >
        WLED detected sustained sound ({{ states('sensor.wled_livingroom_audio_type') }},
        peak {{ states('sensor.wled_livingroom_audio_level_peak') }}%) while the house is empty.
mode: single
```

---

## Troubleshooting

| Symptom | Cause | Fix |
| :--- | :--- | :--- |
| Audio Classifier entities don't appear in Home Assistant | MQTT is not connected in WLED, or broker IP/credentials are wrong | Open **Config > Sync Interfaces > MQTT** in WLED, check your Home Assistant IP and MQTT username/password, click **Save**, and reboot WLED |
| Entities appear in Home Assistant as `Unavailable` | WLED lost connection to the MQTT broker | Verify the **Mosquitto broker** add-on is running in Home Assistant and check the controller's Wi-Fi signal |
| `Audio Source Status` shows `Clipping, reduce gain` | Microphone `Gain` is too high | Lower **Gain** by `-5` to `-10` under **Config > Usermods > AudioReactive** |
| `Music Energy`, `Music Phrase`, and `BPM` stay at `0%` / `Unknown` | `Audio Type` is currently `Silence` or `Ambient Noise` | Normal behavior—music metrics are only active while `Audio Type` is `Music` |
