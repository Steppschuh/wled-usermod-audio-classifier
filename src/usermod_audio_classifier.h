// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#pragma once

#ifdef ARDUINO_ARCH_ESP32

#include "wled.h"
#include "audio_frame.h"
#include "ar_source.h"
#include "classifier.h"
#include "auto_preset_controller.h"

#ifndef USERMOD_ID_AUDIO_CLASSIFIER
  #define USERMOD_ID_AUDIO_CLASSIFIER 205
#endif

class AudioClassifierUsermod : public Usermod {
private:
  static const char _name[];
  static const char _enabled[];
  static const char _haDiscovery[];
  static const char _quietThreshold[];
  static const char _marginFloor[];
  static const char _onDelay[];
  static const char _offDelay[];
  static const char _musicHold[];
  static const char _streamRaw[];
  static const char _automation[];
  static const char _autoMode[];
  static const char _manualOverrideHold[];
  static const char _presetCooldown[];
  static const char _presetAmbient[];
  static const char _presetArLow[];
  static const char _presetArMid[];
  static const char _presetArHigh[];
  static const char _presetArMax[];

  bool enabled = true;
  bool haDiscoveryEnabled = true;
  bool rawStreamEnabled = false;
  uint32_t lastRawStreamMs = 0;

  ClassifierConfig config;
  AudioReactiveSource arSource;
  AudioClassifier classifier;

  AutoPresetConfig autoConfig;
  PresetPoolRegistry presetRegistry;
  AutoPresetController autoController;
  bool presetsScanned = false;
  unsigned long lastScannedPresetsModTime = 0;
  uint8_t pendingAutoPresetId = 0;
  uint32_t pendingAutoPresetStartMs = 0;
  int16_t activeAutoPlaylistId = -1;

  bool initDone = false;
  bool discoveryPublished = false;

  void scanPresetsIfNeeded();

#ifndef WLED_DISABLE_MQTT
  void publishRawFrame(const AudioFrame& frame);
#endif

public:
  void setup() override;
  void loop() override;
  void onStateChange(uint8_t mode) override;

#ifndef WLED_DISABLE_MQTT
  void onMqttConnect(bool sessionPresent) override;
  bool onMqttMessage(char* topic, char* payload) override;
#endif

  void addToJsonInfo(JsonObject& root) override;
  void addToJsonState(JsonObject& root) override;
  void readFromJsonState(JsonObject& root) override;
  void setEnabled(bool newEnabled);
  void setAutoMode(bool newAutoMode);
  void addToConfig(JsonObject& root) override;
  bool readFromConfig(JsonObject& root) override;
  void appendConfigData() override;

  uint16_t getId() override {
    return USERMOD_ID_AUDIO_CLASSIFIER;
  }
};

#endif // ARDUINO_ARCH_ESP32
