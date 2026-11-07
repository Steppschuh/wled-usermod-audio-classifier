// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#pragma once

#ifdef ARDUINO_ARCH_ESP32

#include "wled.h"
#include "audio_frame.h"
#include "ar_source.h"
#include "classifier.h"

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

  bool enabled = true;
  bool haDiscoveryEnabled = true;
  bool rawStreamEnabled = false;
  uint32_t lastRawStreamMs = 0;

  ClassifierConfig config;
  AudioReactiveSource arSource;
  AudioClassifier classifier;

  bool initDone = false;
  bool discoveryPublished = false;

#ifndef WLED_DISABLE_MQTT
  void publishRawFrame(const AudioFrame& frame);
#endif

public:
  void setup() override;
  void loop() override;

#ifndef WLED_DISABLE_MQTT
  void onMqttConnect(bool sessionPresent) override;
  bool onMqttMessage(char* topic, char* payload) override;
#endif

  void addToJsonInfo(JsonObject& root) override;
  void addToJsonState(JsonObject& root) override;
  void readFromJsonState(JsonObject& root) override;
  void addToConfig(JsonObject& root) override;
  bool readFromConfig(JsonObject& root) override;
  void appendConfigData() override;

  uint16_t getId() override {
    return USERMOD_ID_AUDIO_CLASSIFIER;
  }
};

#endif // ARDUINO_ARCH_ESP32
