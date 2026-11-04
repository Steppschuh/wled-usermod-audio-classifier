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
const char AudioClassifierUsermod::_haDiscovery[]    PROGMEM = "mqtt-auto-discovery";
const char AudioClassifierUsermod::_quietThreshold[] PROGMEM = "quiet-threshold";
const char AudioClassifierUsermod::_marginFloor[]    PROGMEM = "margin-above-floor";
const char AudioClassifierUsermod::_onDelay[]        PROGMEM = "on-delay";
const char AudioClassifierUsermod::_offDelay[]       PROGMEM = "off-delay";
const char AudioClassifierUsermod::_experimental[]   PROGMEM = "experimental-music";
const char AudioClassifierUsermod::_musicHold[]      PROGMEM = "music-hold";
const char AudioClassifierUsermod::_streamRaw[]      PROGMEM = "stream-raw";

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

#ifndef WLED_DISABLE_MQTT
    if (rawStreamEnabled && WLED_MQTT_CONNECTED && (millis() - lastRawStreamMs >= 40)) {
      lastRawStreamMs = millis();
      publishRawFrame(frame);
    }
#endif
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

  // Subscribe to raw stream command topic: wled/<device>/audio/stream
  if (mqtt != nullptr) {
    String subTopic = HomeAssistantAudioDiscovery::getBaseTopic() + "stream";
    mqtt->subscribe(subTopic.c_str(), 0);
  }
}

bool AudioClassifierUsermod::onMqttMessage(char* topic, char* payload) {
  if (strstr(topic, "/audio/stream") != nullptr || strstr(topic, "audio/stream") != nullptr) {
    if (strcmp(payload, "ON") == 0 || strcmp(payload, "1") == 0 || strcmp(payload, "true") == 0) {
      rawStreamEnabled = true;
    } else {
      rawStreamEnabled = false;
    }
    return true;
  }
  return false;
}

void AudioClassifierUsermod::publishRawFrame(const AudioFrame& frame) {
  String topic = HomeAssistantAudioDiscovery::getBaseTopic() + "raw";
  String payload = "{\"t\":" + String(frame.timestampMs) +
                   ",\"lvl\":" + String(frame.levelPercent, 1) +
                   ",\"raw\":" + String((int)frame.rawVolume) +
                   ",\"smth\":" + String(frame.smoothedVolume, 1) +
                   ",\"pk\":" + String(frame.isPeak ? 1 : 0) +
                   ",\"hz\":" + String(frame.majorPeakHz, 1) +
                   ",\"mag\":" + String(frame.peakMagnitude, 1) +
                   ",\"bands\":[";
  for (size_t i = 0; i < NUM_AUDIO_BANDS; i++) {
    payload += String(frame.bands[i]);
    if (i < NUM_AUDIO_BANDS - 1) payload += ",";
  }
  payload += "]}";

  mqtt->publish(topic.c_str(), 0, false, payload.c_str());
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
  String levelStr = String(classifier.getAverageLevelPercent(), 1) + "% avg / " + String(classifier.getPeakLevelPercent(), 1) + "% peak (" + classifier.getSemanticStateStr() + ")";
  infoArr.add(levelStr);

  // Row 2: Phrase State, Energy & BPM (if experimental enabled)
  if (experimentalEnabled) {
    String phraseStr = "Phrase: " + String(classifier.getPhraseStateStr()) + " (Energy: " + String(classifier.getSoundEnergy()) + "%";
    if (classifier.getBPM() > 0) {
      phraseStr += ", " + String(classifier.getBPM()) + " BPM";
    }
    phraseStr += ")";
    infoArr.add(phraseStr);
  }

  // Row 3: Status / Warnings
  infoArr.add(arSource.getStatusDescription());
}

void AudioClassifierUsermod::addToConfig(JsonObject& root) {
  JsonObject top = root.createNestedObject(FPSTR(_name));
  top[FPSTR(_enabled)] = enabled;
  top[FPSTR(_haDiscovery)] = haDiscoveryEnabled;
  top[FPSTR(_quietThreshold)] = config.quietThresholdPercent;
  top[FPSTR(_marginFloor)] = config.marginAboveFloorPercent;
  top[FPSTR(_onDelay)] = config.onDelayMs;
  top[FPSTR(_offDelay)] = config.offDelayMs;
  top[FPSTR(_musicHold)] = config.musicHoldMs;
  top[FPSTR(_experimental)] = experimentalEnabled;
  top[FPSTR(_streamRaw)] = rawStreamEnabled;
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
  config.musicHoldMs = top[FPSTR(_musicHold)] | config.musicHoldMs;
  rawStreamEnabled = top[FPSTR(_streamRaw)] | rawStreamEnabled;
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
  oappend(SET_F("addInfo('AudioClassifier:quiet-threshold', 1, ' %');"));
  oappend(SET_F("addInfo('AudioClassifier:margin-above-floor', 1, ' %');"));
  oappend(SET_F("addInfo('AudioClassifier:on-delay', 1, ' ms');"));
  oappend(SET_F("addInfo('AudioClassifier:off-delay', 1, ' ms');"));
  oappend(SET_F("addInfo('AudioClassifier:music-hold', 1, ' ms');"));
}

// Register usermod dynamically in WLED
static AudioClassifierUsermod audioClassifier;
REGISTER_USERMOD(audioClassifier);

#endif // ARDUINO_ARCH_ESP32
