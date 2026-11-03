# 🌟 Vision & Problem: WLED Audio Classifier

---

## 🎯 The Problem

### What is the core friction or unmet need?
Most smart home presence and context systems rely exclusively on PIR motion sensors, mmWave radar, or door contacts. However, sound—and specifically music or ambient volume—is a major contextual indicator of room presence, activity, and mood:
- When music is playing, people are typically active, entertaining, working, or cooking, even if sitting still out of mmWave range.
- Motion sensors often turn off lights if someone is listening to music on the couch without moving.
- Existing smart home audio sensors are either dumb analog sound triggers (binary high/low comparator without nuance) or cloud-connected smart speakers that don't expose granular local state (dB levels, silence vs music, musical phrasing) to Home Assistant.
- WLED audio-reactive setups require synchronized beat and phrasing cues to deliver intelligent room illumination transitions without harsh flashing.

### Why does it happen?
1. Off-the-shelf sound sensors (like cheap LM393 microphone modules) only detect basic voltage spikes, creating false positives from doors closing or background HVAC hum.
2. Voice satellites (like ESP32 Wyoming/Micro Wake Word) are optimized strictly for waking up and streaming audio to Whisper, not for ambient room acoustic telemetry or continuous classification.
3. DSP and machine learning on microcontrollers have historically been difficult to configure, but modern ESP32 chips provide enough SRAM/PSRAM, vector instructions, and efficient I2S DMA streaming to perform continuous acoustic processing locally.

---

## 👥 Target Audience & Personas

### Primary User
- **Who they are**: Home automation enthusiasts running Home Assistant who want ambient context-aware automations (party modes, focus modes, quiet-hours noise alerts).
- **Key traits & habits**: Already has Home Assistant running, comfortable with ESP32s (ESPHome or PlatformIO/WLED), values 100% local processing and privacy (no raw audio sent to the cloud).
- **Their immediate goal**: Plug an inexpensive (\$10-\$15) commercial controller or DIY sensor into a room, configure WiFi, and immediately get entities in Home Assistant for dB sound level, audio classification (silence vs music vs noise), and musical cues.

### Secondary Users / Beneficiaries
- **WLED LED Strip Enthusiasts**: Looking for responsive audio-reactive effects that react not just to raw volume, but to the structure/phrasing of music tracks.
- **Parents / Apartment Dwellers**: Wanting automated alerts when room sound pressure exceeds city or quiet-hour dB thresholds.

---

## 💡 Unique Value Proposition (UVP)

> **"For smart home enthusiasts who want real-time acoustic context without cloud microphones, WLED Audio Classifier is a custom WLED Usermod that repurposes commercial, pre-packaged WLED controllers with built-in microphones into edge audio sensors that measure dB sound levels, classify silence vs. music, and track musical phrasing, reporting directly to Home Assistant via MQTT Auto-Discovery."**

### Core Differentiators
1. **Leverages Off-the-Shelf Hardware**: Runs directly on inexpensive, pre-packaged commercial WLED controllers with built-in microphones (e.g. Gledopto WLED+MIC) with zero custom soldering required.
2. **Home Assistant as Single Source of Truth**: Does not implement fragile peer-to-peer device meshes; reports clean state to Home Assistant so central automations orchestrate lights and scenes.
3. **100% Local & Privacy-Preserving**: Audio is processed directly in ESP32 RAM buffers via DMA and discarded immediately—zero raw audio leaves the device.
4. **Contextual Classification Beyond Volume**: Distinguishes between ambient noise (fan, vacuum, traffic), silence, and actual music playback.
5. **Musical Phrasing Awareness**: Detects drops, breakdowns, and buildups to drive dynamic scene and lighting changes.

---

## 🏁 Success Criteria

### Qualitative Outcomes
- Zero privacy concerns: microphone acts purely as an edge telemetry sensor, never recording or streaming conversations.
- Reliable music detection without triggering on HVAC hum or conversation murmur.
- Seamless Home Assistant discovery and entity updates with sub-second responsiveness.

### Key Metrics
- **End-to-End Latency**: Classification updates delivered to Home Assistant within < 500ms; dB sound level updates at 1–2 Hz.
- **Resource Footprint**: Runs comfortably on ESP32 / ESP32-S3 without memory leaks or WiFi dropouts.
- **False Positive Rate**: < 5% false music triggers during everyday household activities.

