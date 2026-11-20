This guide walks you through setting up a [WLED](https://kno.wled.ge/) controller from scratch, installing the **WLED Audio Classifier** firmware, and tuning the microphone so it reliably detects silence, room noise, and music.

No prior experience with WLED, GitHub, or programming is required.

### Jump to Your Starting Point
Pick the option that matches your controller to skip the steps you don't need:
- **[I have an ESP32 that is not running WLED yet](#step-0-flash-a-blank-esp32-via-usb)** — Flash the factory firmware over a USB cable first, then connect to Wi-Fi.
- **[I have a new WLED controller that has not been setup yet](#step-1-connect-to-your-controller-for-the-first-time)** — Connect to the `WLED-AP` hotspot, join your home Wi-Fi, and install the Audio Classifier update.
- **[I have an existing WLED controller that is already setup](#step-4-download-the-audio-classifier-firmware)** — Skip initial Wi-Fi setup and install the Audio Classifier firmware through the WLED web interface.

### Table of Contents
- [Before You Start](#before-you-start)
- [Step 0: Flash a Blank ESP32 via USB](#step-0-flash-a-blank-esp32-via-usb) *(Only for bare ESP32 boards without WLED)*
- [Step 1: Connect to Your Controller for the First Time](#step-1-connect-to-your-controller-for-the-first-time)
- [Step 2: Connect the Controller to Your Home Wi-Fi](#step-2-connect-the-controller-to-your-home-wi-fi)
- [Step 3: Open WLED on Your Home Wi-Fi & Check Your LED Strip](#step-3-open-wled-on-your-home-wi-fi--check-your-led-strip)
- [Step 4: Download the Audio Classifier Firmware](#step-4-download-the-audio-classifier-firmware)
- [Step 5: Install the Firmware on Your Controller](#step-5-install-the-firmware-on-your-controller)
- [Step 6: Verify Your Microphone Hardware Settings (`AudioReactive`)](#step-6-verify-your-microphone-hardware-settings-audioreactive)
- [Step 7: Tune Microphone Gain, Squelch & AGC](#step-7-tune-microphone-gain-squelch--agc)
- [Step 8: Enable `AudioClassifier` & Test Live Detection](#step-8-enable-audioclassifier--test-live-detection)
- [Next Steps](#next-steps)

---

## Before You Start

### What You Need
1. **An ESP32-based WLED controller with a microphone** (for example: Gledopto `GL-C-015WL-M`, `GL-C-310WL`, a QuinLED board with a mic, or a DIY ESP32 + [supported digital/analog microphone](https://kno.wled.ge/advanced/audio-reactive/#supported-microphones)).
2. **An addressable LED strip** wired to the controller and connected to a power supply.
3. **A computer or smartphone** connected to your home Wi-Fi (ESP32 controllers require a 2.4 GHz Wi-Fi network).

### Why Install Custom Firmware?
Most controllers ship from the factory with standard WLED software. Standard WLED can blink lights to sound, but it does not classify what it is hearing.

In WLED, optional features are compiled in as [**usermods**](https://kno.wled.ge/advanced/usermods/) (add-on modules). The firmware from this repository is standard WLED with two usermods included:
- **[`AudioReactive`](https://kno.wled.ge/advanced/audio-reactive/)**: Samples the microphone to measure volume and frequency bands.
- **`AudioClassifier`**: Analyzes that audio stream in memory to detect **Silence**, **Ambient Noise** (talking, TV, household activity), and **Music** (including song energy, tempo/BPM, and sections like verse, buildup, chorus, and breakdown).

---

## Step 0: Flash a Blank ESP32 via USB

> **Skip to [Step 1](#step-1-connect-to-your-controller-for-the-first-time)** if you bought a pre-made WLED controller (such as Gledopto or QuinLED) that already comes with WLED installed.

If you are starting with a bare ESP32 development board without WLED (see also the [official WLED binary installation guide](https://kno.wled.ge/basics/install-binary/)):

1. Open the [**Releases page**](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases) in your browser and find the top release marked **Latest**.
2. Under **Assets** at the bottom of that release, click the **`*_factory.bin`** file (`WLED_*_MOD_*_ESP32_factory.bin`) to download it.
   *(The `_factory.bin` file includes the bootloader required when flashing a blank chip over USB).*
3. Connect the ESP32 board to your computer with a **USB data cable** (make sure it is not a charge-only cable).
4. Open [ESPHome Web Flasher](https://web.esphome.io/) in **Google Chrome** or **Microsoft Edge**.
5. Click **Connect**, choose your ESP32's USB serial port from the browser popup (often named `CP2102`, `CH340`, or `USB Serial`), and click **Connect**.
6. Click **Install**, select the **`WLED_..._ESP32_factory.bin`** file you downloaded, and click **Install**.
7. Wait 1–2 minutes for flashing to finish, then unplug and replug the board.

<!-- Screenshot placeholder: ESPHome Web Flasher dialog showing a connected USB serial port and the selected WLED_*_ESP32_factory.bin firmware file -->

Your board now has WLED + Audio Classifier installed. Continue to **Step 1** below to connect it to your Wi-Fi.

---

## Step 1: Connect to Your Controller for the First Time

When a new WLED controller is powered on for the first time, it creates a temporary Wi-Fi hotspot so you can enter your home Wi-Fi credentials (see the [official WLED Getting Started guide](https://kno.wled.ge/basics/getting-started/) for additional details).

1. Plug your controller into power.
2. Open the **Wi-Fi settings** on your phone or computer.
3. Connect to the Wi-Fi network named **`WLED-AP`**.
4. Enter the default password:
   ```text
   wled1234
   ```
5. A setup screen usually opens automatically. If it doesn't, open your web browser and go to:
   ```text
   http://4.3.2.1
   ```

---

## Step 2: Connect the Controller to Your Home Wi-Fi

1. On the WLED welcome page, click **WIFI SETTINGS** (or **Config > WiFi Setup**).
2. Under **Network name (SSID)**, click **Scan** and select your home Wi-Fi network, or type the network name manually.
   > **Note:** ESP32 controllers only support **2.4 GHz** Wi-Fi. If your router uses separate names for `2.4G` and `5G`, pick the `2.4G` network.
3. Enter your Wi-Fi password into **Network password**.
4. Scroll down to **mDNS address**. Change the default `wled-xxxxxx` name to something easy to remember without spaces, such as:
   ```text
   wled-livingroom
   ```
   This lets you open the controller later at `http://wled-livingroom.local`.
5. Click **Save & Connect** at the bottom.

<!-- Screenshot placeholder: WLED Config > WiFi Setup screen highlighting Network name (SSID), Network password, mDNS address ("wled-livingroom"), and the "Save & Connect" button -->

The controller will restart and join your home Wi-Fi, and your phone or computer will reconnect to your normal Wi-Fi network.

---

## Step 3: Open WLED on Your Home Wi-Fi & Check Your LED Strip

Once your phone or computer is back on your home Wi-Fi, open the controller using any of these methods:

- **Option A — WLED Native App (Phone/Tablet):** Install the free **WLED Native** app ([iOS App Store](https://apps.apple.com/app/wled-native/id6446207239) / [Google Play](https://play.google.com/store/apps/details?id=ca.cgagnier.wlednativeandroid)). Tap **`+`** to scan your network and discover the controller automatically.
- **Option B — Browser Address (`mDNS`):** Open your browser and go to `http://wled-livingroom.local` (using the mDNS name you set in Step 2).
- **Option C — IP Address:** Find the controller's IP address in your router's connected devices list (for example, `192.168.1.50`) and open `http://192.168.1.50` in your browser.

### Set Your LED Strip Length
By default, WLED only lights up the first **30 LEDs**. To match your strip (see [WLED LED Preferences documentation](https://kno.wled.ge/features/settings/#led-preferences) for advanced strip types and power limits):
1. Go to **Config > LED Preferences**.
2. Under **Hardware setup**, change **Length** from `30` to the total number of LEDs on your strip (and verify **GPIO** if your data line uses a different pin).
3. Click **Save** at the bottom, then click **Back** to verify the full strip lights up.

<!-- Screenshot placeholder: WLED Config > LED Preferences > Hardware setup showing LED strip type, Color Order, Length field, and Data GPIO pin -->

> **Flashed via USB in [Step 0](#step-0-flash-a-blank-esp32-via-usb)?**
> Your board already has the Audio Classifier firmware installed. Skip Steps 4 & 5 and continue at **[Step 6: Verify Your Microphone Hardware Settings](#step-6-verify-your-microphone-hardware-settings-audioreactive)**.

---

## Step 4: Download the Audio Classifier Firmware

Download the pre-built firmware update file from GitHub:

1. Open the [**Releases page**](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases) in your browser.
2. Find the top release marked with the green **Latest** badge.
3. Under **Assets** at the bottom of that release (click **Assets** if collapsed), download the standard `.bin` file (**without** `_factory` in the filename):
   - ✅ **`WLED_*_MOD_*_ESP32.bin`** *(Download this file)*
   - ❌ `WLED_*_MOD_*_ESP32_factory.bin` *(Skip — only for blank chips over USB)*
   - ❌ `SHA256SUMS` *(Skip — checksum text file)*

<!-- Screenshot placeholder: GitHub Releases page showing the "Latest" release block with Assets expanded and an arrow pointing to WLED_*_ESP32.bin -->

Keep the downloaded `.bin` file in your **Downloads** folder without opening it.

---

## Step 5: Install the Firmware on Your Controller

1. In your WLED browser tab, go to **Config > Security & Updates**.
2. Scroll down to **Software Update** and click **Manual OTA Update**.
3. Click **Choose File** (or **Browse**), select the **`WLED_..._ESP32.bin`** file you downloaded, and click **Update!**.

<!-- Screenshot placeholder: WLED Config > Security & Updates > Manual OTA Update dialog showing the selected WLED_*_ESP32.bin file and the "Update!" button -->

4. After ~30–45 seconds you will see:
   > **Update successful! Rebooting now...**
5. Wait another 15 seconds for the reboot to finish, then return to the main WLED page and refresh.

Your Wi-Fi and LED settings are preserved automatically.

---

## Step 6: Verify Your Microphone Hardware Settings (`AudioReactive`)

Make sure WLED's [`AudioReactive` usermod](https://kno.wled.ge/advanced/audio-reactive/) is configured to read from your controller's microphone:

1. Go to **Config > Usermods**.
2. Under **AudioReactive** at the top, confirm **Enabled** is checked.
3. **Check the Microphone Type & Pins:**
   - **Pre-built controllers (Gledopto, etc.):** If your controller shipped with WLED pre-installed, the microphone pins are typically already set. If they were reset, check the label printed on the controller housing or manual.
   - **Common pinouts** (see the [WLED AudioReactive microphone wiring guide](https://kno.wled.ge/advanced/audio-reactive/#wiring) for additional microphones):

     | Controller / Mic Hardware | Digitalmic Type | I2S SD Pin (DOUT) | I2S WS Pin (LRCLK / CLK) | I2S SCK Pin (BCLK) |
     | :--- | :--- | :--- | :--- | :--- |
     | **Gledopto GL-C-015WL-M / GL-C-310WL** *(PDM Mic)* | `3 (Generic I2S PDM)` *(or per label on case)* | `33` *(or per case label)* | `15` *(or per case label)* | `-1 (Unused)` |
     | **DIY ESP32 + INMP441 / ICS-43434** *(Standard I2S)* | `1 (Generic I2S)` | `32` | `15` | `14` |

4. **Important:** If you changed **Digitalmic type** or any **Pin** numbers, click **Save** at the bottom, then go to **Config > Security & Updates** and click **Reboot WLED**. Microphone hardware pins only initialize on reboot.

---

## Step 7: Tune Microphone Gain, Squelch & AGC

For the classifier to separate silence, conversation, and music accurately, configure these settings under **Config > Usermods > AudioReactive** (see [official AudioReactive settings docs](https://kno.wled.ge/advanced/audio-reactive/#settings)):

| Setting | Recommended | Purpose |
| :--- | :--- | :--- |
| **`Enabled`** | `Checked` | Required for microphone sampling. |
| **`AGC`** | `0 (Off)` | Preserves true room volume dynamics (`1 (Normal)` or `3 (Lazy)` can be used for visuals; avoid `2 (Vivid)`). |
| **`Squelch`** | `6` *(typical `4–10`)* | Noise gate to cut out microphone electrical static in a silent room. |
| **`Gain`** | `50` *(typical `40–70`)* | Microphone amplification (tune so normal conversation reads ~20–35%). |

### 1. Set AGC (Automatic Gain Control) to `0 (Off)`
- **Why:** AGC automatically boosts microphone sensitivity when a room is quiet and lowers it when loud. If left on (especially `2 (Vivid)`), it amplifies quiet room background noise until it looks as loud as music, preventing the classifier from detecting silence.
- *(If you prefer automatic volume leveling for visual effects, `1 (Normal)` or `3 (Lazy)` can be used, though `0 (Off)` provides the most consistent classification).*

### 2. Set Squelch (Noise Gate) to `6`
- **What it does:** Cuts out low-level microphone electrical static so a silent room reads as `0%` instead of flickering. Typical range is `4–10` for digital mics and `8–15` for analog mics.

### 3. Set Gain (Sensitivity) to `50`
- **What it does:** Sets overall microphone amplification. Typical range is `40–70`.

<!-- Screenshot placeholder: WLED Config > Usermods > AudioReactive section showing Enabled checked, Digitalmic type and pins, Squelch set to 6, Gain set to 50, and AGC set to 0 (Off) -->

Click **Save** at the bottom of the **Usermods** page.

---

## Step 8: Enable `AudioClassifier` & Test Live Detection

1. Still under **Config > Usermods**, scroll to the **AudioClassifier** section.
2. Confirm **Enabled** is checked (enabled by default). The core detection settings are pre-tuned for typical rooms:

| Setting | Default | Description |
| :--- | :--- | :--- |
| **`Enabled`** | `true` | Master toggle for audio classification. |
| **`MQTT Auto-Discovery`** | `true` | Automatically publish Home Assistant MQTT discovery entities when MQTT is connected. |
| **`Quiet Threshold`** | `6.0 %` | Absolute silence cutoff floor. Sound below this level is always treated as quiet. |
| **`Margin Above Floor`** | `4.0 %` | Margin required above the adaptive ambient noise floor to trigger sound presence. |
| **`On Delay`** | `1200 ms` | Continuous sound duration required before triggering presence (filters out claps and door slams). |
| **`Off Delay`** | `8000 ms` | Continuous silence duration required before clearing presence (bridges brief speech/track pauses). |
| **`Music Hold`** | `30000 ms` | Hold time in `Music` state across quiet song bridges and breakdowns. |

<!-- Screenshot placeholder: WLED Config > Usermods > AudioClassifier section showing Enabled checked, MQTT Auto-Discovery checked, and the default threshold/delay settings -->

3. Click **Save** if you changed anything, then click **Back** to return to the main WLED view.

### Test Live Classification in WLED
1. Click **Info** in the top menu bar (the **`i`** icon).
2. Find the **`AudioClassifier`** line:
   - **Silence:** Keep the room quiet for ~10 seconds and click **Refresh** at the bottom of the Info popup:
     ```text
     Type: Silence, Energy: 0%
     ```
   - **Ambient Noise:** Talk normally or tap a table for 2–3 seconds and click **Refresh**:
     ```text
     Type: Ambient Noise, Energy: 0%
     ```
   - **Music:** Play a song with a beat at normal volume for 5–10 seconds and click **Refresh**:
     ```text
     Type: Music, Energy: 48%
     ```

<!-- Screenshot placeholder: WLED Info (i) popup highlighting the AudioClassifier row ("Type: Music, Energy: 48%") and the Refresh button -->

### Fine-Tune Squelch and Gain with a Visual Effect
1. Close the Info popup and select an [audio-reactive effect](https://kno.wled.ge/advanced/audio-reactive/#effects) (marked with **`♪`**) under the **Effects** tab, such as **GEQ** or **Gravimeter**.
2. **In a quiet room (Squelch):**
   - The LEDs should remain still or dark.
   - If the LEDs twitch in silence, go to **Config > Usermods > AudioReactive**, increase **Squelch** by **`+2`** (e.g. `6` → `8`), and click **Save**.
   - If quiet music doesn't register at all, lower **Squelch** by **`-2`**.
3. **During normal music playback (Gain):**
   - The effect should move across the middle and upper portion of the strip without staying maxed out at 100%.
   - If the LEDs barely react, increase **Gain** by **`+10`** (e.g. `50` → `60`).
   - If quiet talking or low music maxes out the entire strip, lower **Gain** by **`-10`**.

### Quick Troubleshooting

| Symptom | Cause | Fix |
| :--- | :--- | :--- |
| Info popup stays at `Type: Silence` even during loud music | Microphone pins don't match hardware, or board wasn't rebooted after pin changes | Check pin numbers in **Step 6**, click **Save**, and **Reboot WLED** under **Config > Security & Updates** |
| LEDs twitch in a quiet room, or Info stays stuck on `Ambient Noise` | **Squelch** is too low | Increase **Squelch** by `+2` under **Config > Usermods > AudioReactive** |
| Soft music or quiet song sections aren't detected | **Squelch** is too high or **Gain** is too low | Lower **Squelch** by `-2`, or increase **Gain** by `+10` |
| Normal talking or low music maxes out `Energy: 100%` | **Gain** is too high | Decrease **Gain** by `-10` under **Config > Usermods > AudioReactive** |
| Classification works at first, then triggers on silence after a few minutes | **AGC** is enabled | Set **AGC** to `0 (Off)` under **Config > Usermods > AudioReactive** |

---

## Next Steps

1. **[Standalone Auto-Reactive Preset Switching](Standalone-Preset-Switching)** — Create presets for each energy slot (`Ambient`, `AR Low`, `AR Mid`, `AR High`, `AR Max`) so your controller switches effects automatically.
2. **[Home Assistant Integration](Home-Assistant-Integration)** — Connect your controller to Home Assistant over MQTT to use live audio sensors, BPM, and song energy in smart home automations.
