# 🔌 Beginner Flashing Guide for WLED Audio Classifier

> **Who is this guide for?**
> If you have only ever used WLED's automatic updates and have **never compiled code, installed Python, or used command line tools**, this guide is for you!

You do **not** need to install developer tools to use this usermod. Follow one of the methods below depending on your situation.

---

## 🧭 Which Method Should I Use?

| Your Situation | Recommended Method | Difficulty |
| :--- | :--- | :---: |
| **You already have WLED running on your controller** and it is connected to your WiFi. | **[Method 1: Manual OTA Update (Web UI)](#-method-1-manual-ota-update-in-browser-easiest)** | 🟢 Very Easy (2 min) |
| **You have a brand new controller** or existing firmware won't boot. | **[Method 2: Browser USB Web Flasher](#-method-2-browser-web-flasher-over-usb)** | 🟢 Very Easy (3 min) |
| **You want to build custom binaries from source**. | **[Method 3: Compile via VS Code & PlatformIO](#-method-3-compile-locally-with-platformio-advanced)** | 🟡 Intermediate |

---

## 🌐 Method 1: Manual OTA Update in Browser (Easiest)

If your WLED controller is already running on your home WiFi network, you can upgrade it to this usermod directly in your web browser—**no cables or developer tools needed**.

### Step 1: Download the Pre-compiled Firmware Binary
1. Go to the **[Releases](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases)** page of this repository.
2. Under **Assets**, download the OTA binary (`*_ota.bin`, e.g. `wled_audio_classifier_esp32dev_ota.bin`).

### Step 2: Open WLED in Your Browser
1. In your browser (phone or computer), open your WLED controller's web interface (`http://<wled-ip>` or `http://wled.local`).

### Step 3: Navigate to Manual OTA Update
1. Click the **Config** button (the gear icon ⚙️ in the top bar).
2. Click **Security & Updates**.
3. Under **Manual OTA Update**, click **Choose File** and select the downloaded `*_ota.bin` file.
4. Click **Update!** and wait ~30–45 seconds. The controller reboots automatically, preserving your existing WiFi credentials and LED settings.

### Step 4: Verify in Usermods
1. Open **Config (⚙️) > Usermods**.
2. Ensure both **`AudioReactive`** and **`AudioClassifier`** are present and checked.
3. Configure your MQTT broker under **Config > Sync Interfaces > MQTT** to enable Home Assistant auto-discovery.

---

## ⚡ Method 2: Browser Web Flasher over USB

If your controller is brand new, unconfigured, or cannot connect to WiFi, you can flash it over a USB cable directly from Chrome, Edge, or Opera using **WebSerial**—no software installation required.

### What you need:
- A computer running Chrome, Edge, or Opera.
- A USB data cable.
- The downloaded **factory** binary (`*_factory.bin`, e.g. `wled_audio_classifier_esp32dev_factory.bin`) from [Releases](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases). Note: The factory image contains the complete bootloader, partition table, and application merged for flashing directly at offset `0x0`.

### Steps:
1. Connect your ESP32 controller to your computer via USB.
2. Open the **[ESP Web Tools](https://web.esphome.io/)** or **[Adafruit ESPTool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/)** in your browser.
3. Click **Connect** and select your serial port.
4. Select the downloaded `*_factory.bin` file and flash at address `0x0000`.
5. Once completed, connect to the temporary WiFi hotspot created by WLED (`WLED-AP`, password: `wled1234`) to set up your home WiFi.

---

## 💻 Method 3: Compile Locally with PlatformIO (Advanced)

1. Clone WLED core:
   ```bash
   git clone https://github.com/wled/WLED.git wled-core
   cd wled-core
   ```
2. In `platformio_override.ini`, add:
   ```ini
   [platformio]
   default_envs = esp32dev

   [env:esp32dev]
   custom_usermods =
     ${common.default_usermods}
     https://github.com/Steppschuh/wled-usermod-audio-classifier.git
   ```
3. Compile and upload:
   ```bash
   pio run -e esp32dev -t upload
   ```
