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
const char AudioClassifierUsermod::_musicHold[]          PROGMEM = "music-hold";
const char AudioClassifierUsermod::_streamRaw[]          PROGMEM = "stream-raw";
const char AudioClassifierUsermod::_automation[]         PROGMEM = "automation";
const char AudioClassifierUsermod::_autoSwitchPresets[]  PROGMEM = "auto-switch-presets";
const char AudioClassifierUsermod::_manualOverrideHold[] PROGMEM = "manual-override-hold";
const char AudioClassifierUsermod::_presetCooldown[]     PROGMEM = "preset-cooldown";
const char AudioClassifierUsermod::_presetNoMusic[]      PROGMEM = "preset-no-music";
const char AudioClassifierUsermod::_presetLowEnergy[]    PROGMEM = "preset-low-energy";
const char AudioClassifierUsermod::_presetMidEnergy[]    PROGMEM = "preset-mid-energy";
const char AudioClassifierUsermod::_presetHighEnergy[]   PROGMEM = "preset-high-energy";
const char AudioClassifierUsermod::_presetMaxEnergy[]    PROGMEM = "preset-max-energy";

void AudioClassifierUsermod::setup() {
  arSource.init();
  classifier.setConfig(config);
  autoController.setConfig(autoConfig);
  initDone = true;
}

void AudioClassifierUsermod::scanPresetsIfNeeded() {
  if (!autoConfig.enabled) return;
  if (presetsScanned && presetsModifiedTime == lastScannedPresetsModTime) return;
  if (strip.isUpdating()) return;

  File f = WLED_FS.open(FPSTR(getPresetsFileName()), "r");
  if (!f) {
    presetRegistry.clear();
    presetRegistry.markScanned();
    lastScannedPresetsModTime = presetsModifiedTime;
    presetsScanned = true;
    return;
  }

  PresetScanParser parser;
  parser.reset(presetRegistry);
  char buf[256];
  while (f.available()) {
    size_t n = f.readBytes(buf, sizeof(buf));
    if (n == 0) break;
    parser.feed(buf, n, presetRegistry);
  }
  f.close();
  parser.finish(presetRegistry);

  lastScannedPresetsModTime = presetsModifiedTime;
  presetsScanned = true;
}

void AudioClassifierUsermod::loop() {
  if (!initDone) return;

  if (pendingAutoPresetId != 0 && (millis() - pendingAutoPresetStartMs > 2000)) {
    pendingAutoPresetId = 0;
  }

  if (!enabled) {
    if (classifier.isSoundDetected() || classifier.getSemanticState() != AudioSemanticState::SILENCE || classifier.getSoundEnergy() > 0 || classifier.getAverageLevelPercent() > 0.0f || classifier.isClipping()) {
      classifier.reset();
      arSource.setClipping(false);
#ifndef WLED_DISABLE_MQTT
      if (WLED_MQTT_CONNECTED && haDiscoveryEnabled) {
        HomeAssistantAudioDiscovery::publishTelemetry(classifier, arSource, enabled, autoConfig.enabled, true);
      }
#endif
    }
    return;
  }

  bool arActive = arSource.isAudioReactiveActive();
  AudioFrame frame;
  if (arSource.pollFrame(frame)) {
    classifier.processFrame(frame);
    arSource.setClipping(classifier.isClipping());

#ifndef WLED_DISABLE_MQTT
    if (rawStreamEnabled && WLED_MQTT_CONNECTED && (millis() - lastRawStreamMs >= 40)) {
      lastRawStreamMs = millis();
      publishRawFrame(frame);
    }
#endif
  } else if (!arActive) {
    // AudioReactive is disabled or missing: reset classifier to 0 / silence / unknown
    if (classifier.isSoundDetected() || classifier.getSemanticState() != AudioSemanticState::SILENCE || classifier.getSoundEnergy() > 0 || classifier.getAverageLevelPercent() > 0.0f || classifier.isClipping()) {
      classifier.reset();
      arSource.setClipping(false);
#ifndef WLED_DISABLE_MQTT
      if (WLED_MQTT_CONNECTED && haDiscoveryEnabled) {
        HomeAssistantAudioDiscovery::publishTelemetry(classifier, arSource, enabled, autoConfig.enabled, true);
      }
#endif
    }
  }

  if (autoConfig.enabled && arActive) {
    scanPresetsIfNeeded();
    if (presetsScanned) {
      bool stripOffOrNl = (bri == 0) || nightlightActive;
      uint8_t targetPreset = autoController.update(
          millis(),
          enabled,
          classifier.getSemanticState(),
          classifier.getPhraseState(),
          presetRegistry,
          currentPreset,
          currentPlaylist,
          stripOffOrNl,
          static_cast<uint32_t>(random(0x7FFFFFFF)));
      if (targetPreset > 0 && targetPreset <= PresetPoolRegistry::MAX_PRESET_ID) {
        pendingAutoPresetId = targetPreset;
        pendingAutoPresetStartMs = millis();
        activeAutoPlaylistId = -1;
        applyPreset(targetPreset, CALL_MODE_DIRECT_CHANGE);
      }
    }
  }

#ifndef WLED_DISABLE_MQTT
  if (WLED_MQTT_CONNECTED && haDiscoveryEnabled) {
    if (!discoveryPublished) {
      HomeAssistantAudioDiscovery::publishDiscovery();
      discoveryPublished = true;
    }
    HomeAssistantAudioDiscovery::publishTelemetry(classifier, arSource, enabled, autoConfig.enabled);
  }
#endif
}

void AudioClassifierUsermod::onStateChange(uint8_t mode) {
  if (!initDone || !enabled || !autoConfig.enabled || mode == CALL_MODE_INIT) return;

  if (pendingAutoPresetId != 0 && (millis() - pendingAutoPresetStartMs <= 2000)) {
    if (mode != CALL_MODE_NO_NOTIFY) {
      if (currentPlaylist > 0 && currentPlaylist == pendingAutoPresetId) {
        activeAutoPlaylistId = currentPlaylist;
      } else {
        activeAutoPlaylistId = -1;
      }
      pendingAutoPresetId = 0;
    }
    return;
  }

  if (activeAutoPlaylistId > 0 && currentPlaylist == activeAutoPlaylistId) {
    return;
  }

  activeAutoPlaylistId = -1;
  autoController.onManualStateChange();
}

#ifndef WLED_DISABLE_MQTT
void AudioClassifierUsermod::onMqttConnect(bool sessionPresent) {
  discoveryPublished = false;
  if (haDiscoveryEnabled) {
    HomeAssistantAudioDiscovery::publishDiscovery();
    HomeAssistantAudioDiscovery::publishTelemetry(classifier, arSource, enabled, autoConfig.enabled, true);
    discoveryPublished = true;
  }

  // Subscribe to command topics: raw stream, enable toggle, and auto switch presets
  if (mqtt != nullptr) {
    String streamTopic = HomeAssistantAudioDiscovery::getBaseTopic() + "stream";
    mqtt->subscribe(streamTopic.c_str(), 0);
    String enableTopic = HomeAssistantAudioDiscovery::getBaseTopic() + "enabled/set";
    mqtt->subscribe(enableTopic.c_str(), 0);
    String autoSwitchTopic = HomeAssistantAudioDiscovery::getBaseTopic() + "auto_switch_presets/set";
    mqtt->subscribe(autoSwitchTopic.c_str(), 0);
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
  if (strstr(topic, "/audio/enabled/set") != nullptr || strstr(topic, "audio/enabled/set") != nullptr) {
    if (strcmp(payload, "ON") == 0 || strcmp(payload, "1") == 0 || strcmp(payload, "true") == 0) {
      setEnabled(true);
    } else if (strcmp(payload, "OFF") == 0 || strcmp(payload, "0") == 0 || strcmp(payload, "false") == 0) {
      setEnabled(false);
    }
    return true;
  }
  if (strstr(topic, "/audio/auto_switch_presets/set") != nullptr || strstr(topic, "audio/auto_switch_presets/set") != nullptr ||
      strstr(topic, "/audio/auto_mode/set") != nullptr || strstr(topic, "audio/auto_mode/set") != nullptr) {
    if (strcmp(payload, "ON") == 0 || strcmp(payload, "1") == 0 || strcmp(payload, "true") == 0) {
      setAutoMode(true);
    } else if (strcmp(payload, "OFF") == 0 || strcmp(payload, "0") == 0 || strcmp(payload, "false") == 0) {
      setAutoMode(false);
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

void AudioClassifierUsermod::setEnabled(bool newEnabled) {
  if (enabled != newEnabled) {
    enabled = newEnabled;
    if (!enabled) {
      classifier.reset();
      arSource.setClipping(false);
      autoController.reset();
      pendingAutoPresetId = 0;
      activeAutoPlaylistId = -1;
    }
#ifndef WLED_DISABLE_MQTT
    if (WLED_MQTT_CONNECTED && haDiscoveryEnabled) {
      HomeAssistantAudioDiscovery::publishTelemetry(classifier, arSource, enabled, autoConfig.enabled, true);
    }
#endif
  }
}

void AudioClassifierUsermod::setAutoMode(bool newAutoMode) {
  if (autoConfig.enabled != newAutoMode) {
    autoConfig.enabled = newAutoMode;
    autoController.setConfig(autoConfig);
    pendingAutoPresetId = 0;
    activeAutoPlaylistId = -1;
    if (!newAutoMode) {
      autoController.reset();
    }
#ifndef WLED_DISABLE_MQTT
    if (WLED_MQTT_CONNECTED && haDiscoveryEnabled) {
      HomeAssistantAudioDiscovery::publishTelemetry(classifier, arSource, enabled, autoConfig.enabled, true);
    }
#endif
  }
}

void AudioClassifierUsermod::addToJsonInfo(JsonObject& root) {
  JsonObject user = root["u"];
  if (user.isNull()) user = root.createNestedObject("u");

  JsonArray infoArr = user.createNestedArray(FPSTR(_name));

  String btnStr = F(" <button class=\"btn btn-xs\" onclick=\"requestJson({");
  btnStr += FPSTR(_name);
  btnStr += F(":{");
  btnStr += FPSTR(_enabled);
  btnStr += enabled ? F(":false}});\">") : F(":true}});\">");
  btnStr += F("<i class=\"icons");
  btnStr += enabled ? F(" on") : F(" off");
  btnStr += F("\">&#xe08f;</i>");
  btnStr += F("</button>");

  if (!enabled) {
    infoArr.add(F("Disabled"));
    infoArr.add(btnStr);
    return;
  }

  String infoStr = "Type: " + String(classifier.getSemanticStateStr()) + ", Energy: " + String(classifier.getSoundEnergy()) + "%";
  if (autoConfig.enabled) {
    infoStr += autoController.isManualOverrideActive() ? F(" [Auto Switch: Hold]") : F(" [Auto Switch]");
  }
  infoArr.add(infoStr);
  infoArr.add(btnStr);
}

void AudioClassifierUsermod::addToJsonState(JsonObject& root) {
  if (!initDone) return;
  JsonObject usermod = root[FPSTR(_name)];
  if (usermod.isNull()) {
    usermod = root.createNestedObject(FPSTR(_name));
  }
  usermod[FPSTR(_enabled)] = enabled;
  usermod["on"] = enabled;
  usermod["auto_switch_presets"] = autoConfig.enabled;
}

void AudioClassifierUsermod::readFromJsonState(JsonObject& root) {
  if (!initDone) return;

  JsonObject usermod = root[FPSTR(_name)];
  if (!usermod.isNull()) {
    if (usermod[FPSTR(_enabled)].is<bool>()) {
      setEnabled(usermod[FPSTR(_enabled)].as<bool>());
    } else if (usermod["on"].is<bool>()) {
      setEnabled(usermod["on"].as<bool>());
    }
    if (usermod["auto_switch_presets"].is<bool>()) {
      setAutoMode(usermod["auto_switch_presets"].as<bool>());
    } else if (usermod["auto"].is<bool>()) {
      setAutoMode(usermod["auto"].as<bool>());
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
      arSource.setClipping(false);
#ifndef WLED_DISABLE_MQTT
      if (WLED_MQTT_CONNECTED && haDiscoveryEnabled) {
        HomeAssistantAudioDiscovery::publishTelemetry(classifier, arSource, enabled, autoConfig.enabled, true);
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

  JsonObject autoObj = top.createNestedObject(FPSTR(_automation));
  autoObj[FPSTR(_autoSwitchPresets)] = autoConfig.enabled;
  autoObj[FPSTR(_manualOverrideHold)] = autoConfig.manualOverrideHold;
  autoObj[FPSTR(_presetCooldown)] = autoConfig.cooldownSec;
  autoObj[FPSTR(_presetNoMusic)]    = autoConfig.presetOverrides[static_cast<size_t>(AutomationSlot::AMBIENT)];
  autoObj[FPSTR(_presetLowEnergy)]  = autoConfig.presetOverrides[static_cast<size_t>(AutomationSlot::AR_LOW)];
  autoObj[FPSTR(_presetMidEnergy)]  = autoConfig.presetOverrides[static_cast<size_t>(AutomationSlot::AR_MID)];
  autoObj[FPSTR(_presetHighEnergy)] = autoConfig.presetOverrides[static_cast<size_t>(AutomationSlot::AR_HIGH)];
  autoObj[FPSTR(_presetMaxEnergy)]  = autoConfig.presetOverrides[static_cast<size_t>(AutomationSlot::AR_MAX)];
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

  JsonObject autoObj = top[FPSTR(_automation)];
  bool hasCurrentAutomationKeys = !autoObj.isNull() && !autoObj[FPSTR(_autoSwitchPresets)].isNull();
  if (!autoObj.isNull()) {
    if (!autoObj[FPSTR(_autoSwitchPresets)].isNull()) {
      autoConfig.enabled = autoObj[FPSTR(_autoSwitchPresets)] | autoConfig.enabled;
    } else if (!autoObj[F("auto-mode")].isNull()) {
      autoConfig.enabled = autoObj[F("auto-mode")] | autoConfig.enabled;
    }
    autoConfig.manualOverrideHold = autoObj[FPSTR(_manualOverrideHold)] | autoConfig.manualOverrideHold;
    autoConfig.cooldownSec = autoObj[FPSTR(_presetCooldown)] | autoConfig.cooldownSec;

    auto readOverride = [&](const char* key, const __FlashStringHelper* legacyKey, AutomationSlot slot) {
      int cur = static_cast<int>(autoConfig.presetOverrides[static_cast<size_t>(slot)]);
      int val = cur;
      if (!autoObj[FPSTR(key)].isNull()) {
        val = autoObj[FPSTR(key)] | cur;
      } else if (!autoObj[legacyKey].isNull()) {
        val = autoObj[legacyKey] | cur;
      }
      if (val < 0 || val > PresetPoolRegistry::MAX_PRESET_ID) val = 0;
      autoConfig.presetOverrides[static_cast<size_t>(slot)] = static_cast<uint8_t>(val);
    };
    readOverride(_presetNoMusic,    F("preset-ambient"), AutomationSlot::AMBIENT);
    readOverride(_presetLowEnergy,  F("preset-ar-low"),  AutomationSlot::AR_LOW);
    readOverride(_presetMidEnergy,  F("preset-ar-mid"),  AutomationSlot::AR_MID);
    readOverride(_presetHighEnergy, F("preset-ar-high"), AutomationSlot::AR_HIGH);
    readOverride(_presetMaxEnergy,  F("preset-ar-max"),  AutomationSlot::AR_MAX);
  }

  classifier.setConfig(config);
  autoController.setConfig(autoConfig);
  presetsScanned = false;

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

  return hasCurrentAutomationKeys;
}

void AudioClassifierUsermod::appendConfigData() {
  oappend(SET_F("addInfo('AudioClassifier:quiet-threshold', 1, ' %');"));
  oappend(SET_F("addInfo('AudioClassifier:margin-above-floor', 1, ' %');"));
  oappend(SET_F("addInfo('AudioClassifier:on-delay', 1, ' ms');"));
  oappend(SET_F("addInfo('AudioClassifier:off-delay', 1, ' ms');"));
  oappend(SET_F("addInfo('AudioClassifier:music-hold', 1, ' ms');"));
  oappend(SET_F("addInfo('AudioClassifier:automation:preset-cooldown', 1, ' s');"));
  oappend(SET_F("addInfo('AudioClassifier:automation:preset-no-music', 1, ' (0 = auto: \"Ambient *\")');"));
  oappend(SET_F("addInfo('AudioClassifier:automation:preset-low-energy', 1, ' (0 = auto: \"AR Low *\")');"));
  oappend(SET_F("addInfo('AudioClassifier:automation:preset-mid-energy', 1, ' (0 = auto: \"AR Mid *\")');"));
  oappend(SET_F("addInfo('AudioClassifier:automation:preset-high-energy', 1, ' (0 = auto: \"AR High *\")');"));
  oappend(SET_F("addInfo('AudioClassifier:automation:preset-max-energy', 1, ' (0 = auto: \"AR Max *\")');"));
}

// Register usermod dynamically in WLED
static AudioClassifierUsermod audioClassifier;
REGISTER_USERMOD(audioClassifier);

#endif // ARDUINO_ARCH_ESP32
