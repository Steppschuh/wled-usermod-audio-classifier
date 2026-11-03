// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#ifdef ARDUINO_ARCH_ESP32

#include "usermod_audio_classifier.h"
#ifndef WLED_DISABLE_MQTT
#include "ha_discovery.h"
#endif

// PROGMEM Key Definitions
const char AudioClassifierUsermod::_name[]           PROGMEM = "AudioClassifier";
const char AudioClassifierUsermod::_enabled[]        PROGMEM = "enabled";
const char AudioClassifierUsermod::_haDiscovery[]    PROGMEM = "ha-discovery";
const char AudioClassifierUsermod::_quietThreshold[] PROGMEM = "quiet-threshold-percent";
const char AudioClassifierUsermod::_marginFloor[]    PROGMEM = "margin-above-floor";
const char AudioClassifierUsermod::_onDelay[]        PROGMEM = "on-delay-ms";
const char AudioClassifierUsermod::_offDelay[]       PROGMEM = "off-delay-ms";
const char AudioClassifierUsermod::_experimental[]   PROGMEM = "experimental-music";

void AudioClassifierUsermod::setup() {
  arSource.init();
  classifier.setConfig(config);
  initDone = true;
}

void AudioClassifierUsermod::loop() {
  if (!enabled || !initDone) return;

  AudioFrame frame;
  if (arSource.pollFrame(frame)) {
    classifier.processFrame(frame);
  }

#ifndef WLED_DISABLE_MQTT
  if (WLED_MQTT_CONNECTED && haDiscoveryEnabled) {
    if (!discoveryPublished) {
      HomeAssistantAudioDiscovery::publishDiscovery(experimentalEnabled);
      discoveryPublished = true;
    }
    HomeAssistantAudioDiscovery::publishTelemetry(classifier, arSource);
  }
#endif
}

#ifndef WLED_DISABLE_MQTT
void AudioClassifierUsermod::onMqttConnect(bool sessionPresent) {
  discoveryPublished = false;
  if (enabled && haDiscoveryEnabled) {
    HomeAssistantAudioDiscovery::publishDiscovery(experimentalEnabled);
    HomeAssistantAudioDiscovery::publishTelemetry(classifier, arSource, true);
    discoveryPublished = true;
  }
}
#endif

void AudioClassifierUsermod::addToJsonInfo(JsonObject& root) {
  JsonObject user = root["u"];
  if (user.isNull()) user = root.createNestedObject("u");

  JsonArray infoArr = user.createNestedArray(FPSTR(_name));
  
  if (!enabled) {
    infoArr.add("Disabled");
    return;
  }

  // Row 1: Relative Level & State
  String levelStr = String(classifier.getCurrentLevelPercent(), 1) + "% (" + classifier.getSemanticStateStr() + ")";
  infoArr.add(levelStr);

  // Row 2: Phrase State (if experimental enabled)
  if (experimentalEnabled) {
    infoArr.add("Phrase: " + String(classifier.getPhraseStateStr()));
  }

  // Row 3: Status / Warnings
  infoArr.add(arSource.getStatusDescription());
}

void AudioClassifierUsermod::addToConfig(JsonObject& root) {
  JsonObject top = root.createNestedObject(FPSTR(_name));
  top[FPSTR(_enabled)] = enabled;
  top[FPSTR(_haDiscovery)] = haDiscoveryEnabled;
  top[FPSTR(_experimental)] = experimentalEnabled;
  top[FPSTR(_quietThreshold)] = config.quietThresholdPercent;
  top[FPSTR(_marginFloor)] = config.marginAboveFloorPercent;
  top[FPSTR(_onDelay)] = config.onDelayMs;
  top[FPSTR(_offDelay)] = config.offDelayMs;
}

bool AudioClassifierUsermod::readFromConfig(JsonObject& root) {
  // Update AudioReactive config state (detect AGC, receive mode, enabled)
  arSource.updateConfig(root);

  JsonObject top = root[FPSTR(_name)];
  if (top.isNull()) return false;

  bool prevHa = haDiscoveryEnabled;
  bool prevExp = experimentalEnabled;

  enabled = top[FPSTR(_enabled)] | enabled;
  haDiscoveryEnabled = top[FPSTR(_haDiscovery)] | haDiscoveryEnabled;
  experimentalEnabled = top[FPSTR(_experimental)] | experimentalEnabled;
  config.quietThresholdPercent = top[FPSTR(_quietThreshold)] | config.quietThresholdPercent;
  config.marginAboveFloorPercent = top[FPSTR(_marginFloor)] | config.marginAboveFloorPercent;
  config.onDelayMs = top[FPSTR(_onDelay)] | config.onDelayMs;
  config.offDelayMs = top[FPSTR(_offDelay)] | config.offDelayMs;
  config.experimentalEnabled = experimentalEnabled;

  classifier.setConfig(config);

#ifndef WLED_DISABLE_MQTT
  if (initDone && WLED_MQTT_CONNECTED) {
    if (prevHa && !haDiscoveryEnabled) {
      HomeAssistantAudioDiscovery::removeDiscovery(prevExp);
      discoveryPublished = false;
    } else if (haDiscoveryEnabled && (!discoveryPublished || prevExp != experimentalEnabled)) {
      HomeAssistantAudioDiscovery::publishDiscovery(experimentalEnabled);
      discoveryPublished = true;
    }
  }
#endif

  return true;
}

void AudioClassifierUsermod::appendConfigData() {
  oappend(SET_F("addInfo('AudioClassifier:quiet-threshold-percent', 1, 'Relative noise floor % cutoff');"));
  oappend(SET_F("addInfo('AudioClassifier:on-delay-ms', 1, 'Debounce time to detect sound (ms)');"));
  oappend(SET_F("addInfo('AudioClassifier:off-delay-ms', 1, 'Debounce time to return to quiet (ms)');"));
  oappend(SET_F("addInfo('AudioClassifier:experimental-music', 1, 'Enable experimental music & phrase detection');"));
}

// Register usermod dynamically in WLED
static AudioClassifierUsermod audioClassifier;
REGISTER_USERMOD(audioClassifier);

#endif // ARDUINO_ARCH_ESP32
