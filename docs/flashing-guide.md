# 🔌 Beginner Flashing Guide for WLED Usermods

> **Who is this guide for?**
> If you have only ever used WLED's automatic over-the-air (OTA) updates and have **never compiled code, installed Python, or used command line tools**, this guide is for you!

You do **not** need to install complex developer tools to use this usermod. Follow one of the three simple methods below depending on your situation.

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
2. Under **Assets**, download the `.bin` file for your hardware:
   - For **Gledopto WLED+MIC** (or standard ESP32 boards): download `wled_audio_classifier_esp32.bin`.
   - Save the file to your computer.

### Step 2: Open WLED in Your Browser
1. In your browser (phone or computer), open your WLED controller's web interface:
   - Usually accessible at `http://wled.local` or directly by typing its IP address (e.g. `http://192.168.1.150`).

### Step 3: Navigate to Manual OTA Update
1. Click the **Config** button (the gear icon ⚙️ in the top bar).
2. Click **Security & Updates**.
3. Scroll down to the section titled **Manual OTA Update**:
   - You will see a file picker button (`Choose File` / `Browse...`) and an **Update!** button.

### Step 4: Upload the Binary
1. Click **Choose File** and select the `wled_audio_classifier_esp32.bin` file you downloaded in Step 1.
2. Click **Update!**.
3. Wait **30 to 45 seconds** while the file uploads and flashes:
   - Do **not** unplug the device or close the browser tab.
   - The device will automatically reboot once finished.

### Step 5: Verify the Usermod is Active
1. Once WLED reboots, refresh the page.
2. Go to **Config (⚙️) > Usermods**.
3. You will now see **`AudioClassifier`** listed with settings for:
   - `enabled` (Checked)
   - `ha-discovery` (Checked to auto-create Home Assistant entities)
   - `pin-sd`, `pin-ws`, `pin-sck` (`26`, `5`, `21` for Gledopto)
   - `db-offset` (`0.0`)
   - `silence-threshold-db` (`45.0`)
4. Done! All your existing WiFi credentials and LED settings are preserved.

---

## ⚡ Method 2: Browser Web Flasher over USB

If your controller is brand new, unconfigured, or cannot connect to WiFi, you can flash it over a USB cable directly from Chrome, Edge, or Opera using **WebSerial**—no software installation required.

### What you need:
- A computer with **Google Chrome**, **Microsoft Edge**, or **Opera**.
- A USB cable that supports **data** (not a charge-only cable).
- The downloaded `wled_audio_classifier_esp32.bin` from [Releases](https://github.com/Steppschuh/wled-usermod-audio-classifier/releases).

### Steps:
1. Plug your ESP32 / WLED controller into your computer's USB port.
2. Open the **[ESP Web Tools](https://web.esphome.io/)** or **[Adafruit ESPTool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/)** website in Chrome or Edge.
3. Click **Connect** (or *Choose Port*):
   - A browser pop-up will ask you to select a serial port (e.g. `CP2102 USB to UART Bridge Controller` or `USB Serial Port (COM3)`).
   - Click your device and click **Connect**.
4. Once connected, select **Install** or upload your local `wled_audio_classifier_esp32.bin` file.
5. Click **Program / Install** and wait ~60 seconds for the progress bar to reach 100%.
6. Once completed, connect to the temporary WiFi hotspot created by WLED (`WLED-AP`, password: `wled1234`) to connect it to your home WiFi.

---

## 💻 Method 3: Compile Locally with PlatformIO (Advanced)

If you are a developer or want to customize the C++ code before flashing:

1. Install **[VS Code](https://code.visualstudio.com/)** and the **[PlatformIO IDE](https://platformio.org/)** extension.
2. Clone the core WLED repository:
   ```bash
   git clone -b main https://github.com/wled/WLED.git
   cd WLED
   ```
3. Copy our example configuration into the WLED root folder:
   ```bash
   # Download or copy platformio_override.ini
   curl -o platformio_override.ini https://raw.githubusercontent.com/Steppschuh/wled-usermod-audio-classifier/main/examples/platformio_override.ini
   ```
4. Open the `WLED` folder in VS Code.
5. In the PlatformIO sidebar, find `esp32_audio_classifier` under **Project Tasks** and click **Build** (or click the checkmark icon in the bottom status bar).
6. Once compiled, either:
   - Click **Upload** to flash directly via USB.
   - Or find the compiled binary at `.pio/build/esp32_audio_classifier/firmware.bin` and upload it using [Method 1](#-method-1-manual-ota-update-in-browser-easiest).

---

## ❓ Troubleshooting & FAQ

#### Q: Did I lose my LED preferences or presets after the update?
**No.** WLED stores configuration and presets in a separate flash partition (`LittleFS`). Uploading a new firmware binary preserves your presets, network settings, and segment configurations.

#### Q: The usermod doesn't show up in Config > Usermods after updating!
1. Ensure the binary you uploaded actually completed (did the screen show "Update successful"?).
2. Power-cycle the controller (unplug power, wait 5 seconds, plug back in). ESP32 I2S hardware initialization requires a full reboot.

#### Q: Home Assistant hasn't discovered the audio entities.
1. Make sure MQTT is configured and connected under **Config > Sync Interfaces > MQTT**.
2. Make sure your MQTT Broker (e.g. Mosquitto) is connected and discovery is enabled in Home Assistant (`Settings > Devices & Services > MQTT`).
3. In WLED, go to **Config > Usermods** and confirm `ha-discovery` is checked, then save.
