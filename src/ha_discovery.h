// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#pragma once

#ifdef ARDUINO_ARCH_ESP32
#ifndef WLED_DISABLE_MQTT

#include "wled.h"
#include "classifier.h"
#include "ar_source.h"

#ifndef USERMOD_AUDIO_CLASSIFIER_VERSION
#define USERMOD_AUDIO_CLASSIFIER_VERSION "0.2.0"
#endif

class HomeAssistantAudioDiscovery {
public:
  static void publishDiscovery(bool experimentalEnabled = true);
  static void removeDiscovery(bool experimentalEnabled = true);
  static void publishTelemetry(const AudioClassifier& classifier, const AudioReactiveSource& source, bool force = false);
  static String getBaseTopic();
  static String getStatusTopic();

private:
};

#endif // WLED_DISABLE_MQTT
#endif // ARDUINO_ARCH_ESP32
