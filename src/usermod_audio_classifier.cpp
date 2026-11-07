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
const char AudioClassifierUsermod::_musicHold[]      PROGMEM = "music-hold";
const char AudioClassifierUsermod::_streamRaw[]      PROGMEM = "stream-raw";

void AudioClassifierUsermod::setup() {
  arSource.init();
  classifier.setConfig(config);
  initDone = true;
}

void AudioClassifierUsermod::loop() {
  if (!initDone) return;

  if (!enabled) {
    if (classifier.isSoundDetected() || classifier.getSemanticState() != AudioSemanticState::SILENCE || classifier.getSoundEnergy() > 0 || classifier.getAverageLevelPercent() > 0.0f) {
      classifier.reset();
#ifndef WLED_DISABLE_MQTT
      if (WLED_MQTT_CONNECTED && haDiscoveryEnabled) {
        HomeAssistantAudioDiscovery::publishTelemetry(classifier, arSource, true);
      }
#endif
    }
    return;
  }

  AudioFrame frame;
  if (arSource.pollFrame(frame)) {
    classifier.processFrame(frame);

#ifndef WLED_DISABLE_MQTT
    if (rawStreamEnabled && WLED_MQTT_CONNECTED && (millis() - lastRawStreamMs >= 40)) {
      lastRawStreamMs = millis();
      publishRawFrame(frame);
    }
#endif
  } else if (!arSource.isAudioReactiveActive()) {
    // AudioReactive is disabled or missing: reset classifier to 0 / silence / unknown
    if (classifier.isSoundDetected() || classifier.getSemanticState() != AudioSemanticState::SILENCE || classifier.getSoundEnergy() > 0 || classifier.getAverageLevelPercent() > 0.0f) {
      classifier.reset();
#ifndef WLED_DISABLE_MQTT
      if (WLED_MQTT_CONNECTED && haDiscoveryEnabled) {
        HomeAssistantAudioDiscovery::publishTelemetry(classifier, arSource, true);
      }
#endif
    }
  }

#ifndef WLED_DISABLE_MQTT
  if (WLED_MQTT_CONNECTED && haDiscoveryEnabled) {
    if (!discoveryPublished) {
      HomeAssistantAudioDiscovery::publishDiscovery();
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
    HomeAssistantAudioDiscovery::publishDiscovery();
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

  String btnStr = F("<button class=\"btn btn-xs\" onclick=\"requestJson({");
  btnStr += FPSTR(_name);
  btnStr += F(":{");
  btnStr += FPSTR(_enabled);
  btnStr += enabled ? F(":false}});\">") : F(":true}});\">");
  btnStr += F("<i class=\"icons");
  btnStr += enabled ? F(" on") : F(" off");
  btnStr += F("\">&#xe08f;</i>");
  btnStr += F("</button>");

  infoArr.add(btnStr);

  if (!enabled) {
    infoArr.add(F(" Disabled"));
    return;
  }

  String infoStr = " Type: " + String(classifier.getSemanticStateStr()) + ", Energy: " + String(classifier.getSoundEnergy()) + "%";
  infoArr.add(infoStr);
}

void AudioClassifierUsermod::addToJsonState(JsonObject& root) {
  if (!initDone) return;
  JsonObject usermod = root[FPSTR(_name)];
  if (usermod.isNull()) {
    usermod = root.createNestedObject(FPSTR(_name));
  }
  usermod[FPSTR(_enabled)] = enabled;
  usermod["on"] = enabled;
}

void AudioClassifierUsermod::readFromJsonState(JsonObject& root) {
  if (!initDone) return;

  JsonObject usermod = root[FPSTR(_name)];
  if (!usermod.isNull()) {
    bool stateChanged = false;
    if (usermod[FPSTR(_enabled)].is<bool>()) {
      bool newEnabled = usermod[FPSTR(_enabled)].as<bool>();
      if (enabled != newEnabled) {
        enabled = newEnabled;
        stateChanged = true;
      }
    } else if (usermod["on"].is<bool>()) {
      bool newEnabled = usermod["on"].as<bool>();
      if (enabled != newEnabled) {
        enabled = newEnabled;
        stateChanged = true;
      }
    }
    if (stateChanged && !enabled) {
      classifier.reset();
#ifndef WLED_DISABLE_MQTT
      if (WLED_MQTT_CONNECTED && haDiscoveryEnabled) {
        HomeAssistantAudioDiscovery::publishTelemetry(classifier, arSource, true);
      }
#endif
    }
  }

  // Also immediately detect if AudioReactive was toggled in this state update
  JsonObject arObj = root[F("AudioReactive")];
  if (!arObj.isNull()) {
    bool arState = true;
    bool hasArState = false;
    if (arObj[F("enabled")].is<bool>()) {
      arState = arObj[F("enabled")].as<bool>();
      hasArState = true;
    } else if (arObj["on"].is<bool>()) {
      arState = arObj["on"].as<bool>();
      hasArState = true;
    }
    if (hasArState && !arState) {
      classifier.reset();
#ifndef WLED_DISABLE_MQTT
      if (WLED_MQTT_CONNECTED && haDiscoveryEnabled) {
        HomeAssistantAudioDiscovery::publishTelemetry(classifier, arSource, true);
      }
#endif
    }
  }
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
  top[FPSTR(_streamRaw)] = rawStreamEnabled;
}

bool AudioClassifierUsermod::readFromConfig(JsonObject& root) {
  // Update AudioReactive config state (detect AGC, receive mode, enabled)
  arSource.updateConfig(root);

  JsonObject top = root[FPSTR(_name)];
  if (top.isNull()) return false;

  bool prevHa = haDiscoveryEnabled;

  enabled = top[FPSTR(_enabled)] | enabled;
  haDiscoveryEnabled = top[FPSTR(_haDiscovery)] | haDiscoveryEnabled;
  config.quietThresholdPercent = top[FPSTR(_quietThreshold)] | config.quietThresholdPercent;
  config.marginAboveFloorPercent = top[FPSTR(_marginFloor)] | config.marginAboveFloorPercent;
  config.onDelayMs = top[FPSTR(_onDelay)] | config.onDelayMs;
  config.offDelayMs = top[FPSTR(_offDelay)] | config.offDelayMs;
  config.musicHoldMs = top[FPSTR(_musicHold)] | config.musicHoldMs;
  rawStreamEnabled = top[FPSTR(_streamRaw)] | rawStreamEnabled;

  classifier.setConfig(config);

#ifndef WLED_DISABLE_MQTT
  if (initDone && WLED_MQTT_CONNECTED) {
    if (prevHa && !haDiscoveryEnabled) {
      HomeAssistantAudioDiscovery::removeDiscovery();
      discoveryPublished = false;
    } else if (haDiscoveryEnabled && !discoveryPublished) {
      HomeAssistantAudioDiscovery::publishDiscovery();
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
