# 🧭 Documentation & Project Overview: WLED Audio Classifier

> **One-Line Pitch**: Custom WLED Usermod V2 that repurposes commercial ESP32 WLED controllers with built-in microphones (e.g. Gledopto WLED+MIC) into edge audio sensors that measure dB SPL, classify silence vs. music, track musical phrasing, and report directly to Home Assistant via MQTT Auto-Discovery.

---

## 📌 Project Metadata

- **Slug**: `wled-usermod-audio-classifier` (formerly `esp32-audio-classifier`)
- **Status / Stage**: `🎓 Active Development` <!-- Options: 🌱 Seed | 🔍 Discovery | 📐 Specification | 🏗️ Architecture | ⏸️ On Hold | 🎓 Graduated -->
- **Repository**: [Steppschuh/wled-usermod-audio-classifier](https://github.com/Steppschuh/wled-usermod-audio-classifier)
- **Target Audience**: Smart home enthusiasts, Home Assistant users wanting acoustic context, WLED controller owners
- **Primary Tech Stack**: ESP32 (Gledopto WLED+MIC / DIY) + WLED Usermod V2 (`wled-usermod-example`) + I2S Digital Mic (GPIO 26/5/21) + Home Assistant MQTT Auto-Discovery & JSON API

---

## 🧭 Navigation & Documentation Sections

| Document | Focus | Status |
| :--- | :--- | :--- |
| **[Vision & Problem](vision.md)** | Problem statement, target audience, personas, UVP, success metrics | 🟢 Complete |
| **[Idea Evaluation](evaluation.md)** | Competitor landscape, feasibility, risks, devil's advocate stress test | 🟢 Complete |
| **[Feature Spec & Scope](spec.md)** | Core user flows, MoSCoW scope matrix, edge cases | 🟢 Complete |
| **[Architecture & Design](architecture.md)** | Usermod V2 class design, DSP data flow diagram, MQTT discovery schema | 🟢 Complete |
| **[Beginner Flashing Guide](flashing-guide.md)** | Step-by-step browser OTA, WebSerial, and PlatformIO flashing instructions | 🟢 Complete |
| **[Braindumps](braindumps/)** | Raw thought dumps, conversation logs, voice notes | 📁 Directory |

---

## 🎯 Current Focus & Next Steps

- **Current Status**: Usermod V2 core implemented with I2S DMA streaming, RMS sound level calculation, dynamic baseline tracking, heuristic music/silence/ambient classifier, phrasing state machine, and Home Assistant MQTT Auto-Discovery.
- **Next Milestone**: Hardware testing on Gledopto GL-C-010WL / GL-C-015WL-D and calibration against a reference SPL meter.

---

## ✅ Maturity Checklist

- [x] **Seed**: Raw braindump captured and core idea articulated.
- [x] **Discovery**: Problem verified, target audience identified, competitors/alternatives researched.
- [x] **Evaluation**: Technical feasibility evaluated, key risks and dependencies flagged.
- [x] **Specification**: Must-have MVP scope locked (WLED Usermod V2 + HA MQTT Auto-Discovery), peer-to-peer sync pruned as non-goal.
- [x] **Architecture**: Tech stack chosen, Usermod V2 class design drafted, MQTT discovery schema specified.
- [x] **Standalone Implementation**: Graduated to standalone repository [Steppschuh/wled-usermod-audio-classifier](https://github.com/Steppschuh/wled-usermod-audio-classifier) with Usermod V2 C++ implementation and build configurations.

