// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#ifdef ARDUINO_ARCH_ESP32
#ifndef WLED_DISABLE_MQTT

#include "ha_discovery.h"

String HomeAssistantAudioDiscovery::getBaseTopic() {
  String base = (mqttDeviceTopic[0] != '\0') ? String(mqttDeviceTopic) : ("wled/" + String(escapedMac));
  if (!base.endsWith("/")) base += "/";
  base += "audio/";
  return base;
}

String HomeAssistantAudioDiscovery::getStatusTopic() {
  String status = (mqttDeviceTopic[0] != '\0') ? String(mqttDeviceTopic) : ("wled/" + String(escapedMac));
  if (!status.endsWith("/")) status += "/";
  status += "status";
  return status;
}

void HomeAssistantAudioDiscovery::publishDiscovery(bool experimentalEnabled) {
  if (!WLED_MQTT_CONNECTED) return;

  String chipId = String(escapedMac);
  String devName = (serverDescription[0] != '\0') ? String(serverDescription) : "WLED Audio";
  String baseTopic = getBaseTopic();
  String statusTopic = getStatusTopic();

  auto buildDevice = [&](JsonObject& dev) {
    dev["name"] = devName;
    dev["sw"] = "0.1.0";
    dev["mdl"] = "WLED";
    JsonArray ids = dev.createNestedArray("ids");
    ids.add(chipId);
    JsonArray conns = dev.createNestedArray("connections");
    JsonArray macConn = conns.createNestedArray();
    macConn.add("mac");
    macConn.add(chipId);
  };

  // 1. Relative Audio Level Sensor (%)
  {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "Audio Level";
    doc["stat_t"] = baseTopic + "level";
    doc["unit_of_meas"] = "%";
    doc["stat_cla"] = "measurement";
    doc["icon"] = "mdi:volume-high";
    doc["uniq_id"] = chipId + "_audio_level";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";
    doc["expire_after"] = 90;

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/sensor/" + chipId + "_audio_level/config";
    String payload;
    serializeJson(doc, payload);
    mqtt->publish(topic.c_str(), 0, true, payload.c_str());
  }

  // 1b. Audio Peak Level Sensor (%)
  {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "Audio Peak";
    doc["stat_t"] = baseTopic + "peak";
    doc["unit_of_meas"] = "%";
    doc["stat_cla"] = "measurement";
    doc["icon"] = "mdi:volume-vibrate";
    doc["uniq_id"] = chipId + "_audio_peak";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";
    doc["expire_after"] = 90;

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/sensor/" + chipId + "_audio_peak/config";
    String payload;
    serializeJson(doc, payload);
    mqtt->publish(topic.c_str(), 0, true, payload.c_str());
  }

  // 2. Audio Detected Binary Sensor
  {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "Audio Detected";
    doc["stat_t"] = baseTopic + "detected";
    doc["dev_cla"] = "sound";
    doc["pl_on"] = "ON";
    doc["pl_off"] = "OFF";
    doc["uniq_id"] = chipId + "_audio_detected";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/binary_sensor/" + chipId + "_audio_detected/config";
    String payload;
    serializeJson(doc, payload);
    mqtt->publish(topic.c_str(), 0, true, payload.c_str());
  }

  // 3. Audio State Sensor (silence / ambient_noise / music)
  {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "Audio State";
    doc["stat_t"] = baseTopic + "state";
    doc["dev_cla"] = "enum";
    JsonArray opts = doc.createNestedArray("options");
    opts.add("silence");
    opts.add("ambient_noise");
    opts.add("music");
    doc["icon"] = "mdi:waveform";
    doc["uniq_id"] = chipId + "_audio_state";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/sensor/" + chipId + "_audio_state/config";
    String payload;
    serializeJson(doc, payload);
    mqtt->publish(topic.c_str(), 0, true, payload.c_str());
  }

  // 4. Audio Source Status (Diagnostic)
  {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "Audio Source Status";
    doc["stat_t"] = baseTopic + "status";
    doc["icon"] = "mdi:information-outline";
    doc["entity_category"] = "diagnostic";
    doc["uniq_id"] = chipId + "_audio_status";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/sensor/" + chipId + "_audio_status/config";
    String payload;
    serializeJson(doc, payload);
    mqtt->publish(topic.c_str(), 0, true, payload.c_str());
  }

  // 5. Audio Phrase Sensor (Optional / Experimental)
  if (experimentalEnabled) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "Audio Phrase";
    doc["stat_t"] = baseTopic + "phrase";
    doc["dev_cla"] = "enum";
    JsonArray opts = doc.createNestedArray("options");
    opts.add("intro");
    opts.add("verse");
    opts.add("buildup");
    opts.add("breakdown");
    opts.add("drop");
    doc["icon"] = "mdi:music-note-eighth";
    doc["entity_category"] = "diagnostic";
    doc["uniq_id"] = chipId + "_audio_phrase";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/sensor/" + chipId + "_audio_phrase/config";
    String payload;
    serializeJson(doc, payload);
    mqtt->publish(topic.c_str(), 0, true, payload.c_str());
  }

  // 6. Audio Energy Sensor (0-100%)
  if (experimentalEnabled) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "Audio Energy";
    doc["stat_t"] = baseTopic + "energy";
    doc["unit_of_meas"] = "%";
    doc["stat_cla"] = "measurement";
    doc["icon"] = "mdi:lightning-bolt";
    doc["uniq_id"] = chipId + "_audio_energy";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";
    doc["expire_after"] = 90;

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/sensor/" + chipId + "_audio_energy/config";
    String payload;
    serializeJson(doc, payload);
    mqtt->publish(topic.c_str(), 0, true, payload.c_str());
  }

  // 7. Audio BPM Sensor
  if (experimentalEnabled) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "Audio BPM";
    doc["stat_t"] = baseTopic + "bpm";
    doc["unit_of_meas"] = "bpm";
    doc["stat_cla"] = "measurement";
    doc["icon"] = "mdi:metronome";
    doc["uniq_id"] = chipId + "_audio_bpm";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";
    doc["expire_after"] = 90;

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/sensor/" + chipId + "_audio_bpm/config";
    String payload;
    serializeJson(doc, payload);
    mqtt->publish(topic.c_str(), 0, true, payload.c_str());
  }
}

void HomeAssistantAudioDiscovery::removeDiscovery(bool experimentalEnabled) {
  if (!WLED_MQTT_CONNECTED) return;
  String chipId = String(escapedMac);

  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_level/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_peak/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/binary_sensor/" + chipId + "_audio_sound/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/binary_sensor/" + chipId + "_audio_detected/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_state/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_status/config").c_str(), 0, true, "");
  if (experimentalEnabled) {
    mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_phrase/config").c_str(), 0, true, "");
    mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_energy/config").c_str(), 0, true, "");
    mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_bpm/config").c_str(), 0, true, "");
  }
}

void HomeAssistantAudioDiscovery::publishTelemetry(const AudioClassifier& classifier, const AudioReactiveSource& source, bool force) {
  if (!WLED_MQTT_CONNECTED) return;

  static uint32_t lastPubTime = 0;
  static uint32_t lastHeartbeatTime = 0;
  static bool lastSoundState = false;
  static AudioSemanticState lastSemanticState = AudioSemanticState::SILENCE;
  static MusicPhrase lastPhraseState = MusicPhrase::VERSE;
  static uint8_t lastSoundEnergy = 0;
  static uint16_t lastBpm = 0;
  static AudioSourceStatus lastSourceStatus = AudioSourceStatus::OK;

  uint32_t now = millis();
  bool soundState = classifier.isSoundDetected();
  AudioSemanticState semanticState = classifier.getSemanticState();
  MusicPhrase phraseState = classifier.getPhraseState();
  uint8_t soundEnergy = classifier.getSoundEnergy();
  uint16_t bpm = classifier.getBPM();
  AudioSourceStatus srcStatus = source.getStatus();

  bool stateChanged = (soundState != lastSoundState) ||
                      (semanticState != lastSemanticState) ||
                      (phraseState != lastPhraseState) ||
                      (soundEnergy != lastSoundEnergy) ||
                      (bpm != lastBpm) ||
                      (srcStatus != lastSourceStatus);

  bool timeForHeartbeat = (now - lastHeartbeatTime >= 30000);
  bool timeForLevel = (now - lastPubTime >= 1000);

  // Rate limit event emissions to at least 250ms
  if (!force && !stateChanged && !timeForHeartbeat && !timeForLevel) {
    return;
  }
  if (!force && stateChanged && (now - lastPubTime < 250)) {
    return;
  }

  String base = getBaseTopic();

  // Publish periodic / on-change telemetry
  if (force || timeForLevel || timeForHeartbeat) {
    char lvlBuf[16];
    snprintf(lvlBuf, sizeof(lvlBuf), "%.1f", classifier.getAverageLevelPercent());
    mqtt->publish((base + "level").c_str(), 0, false, lvlBuf);

    char pkBuf[16];
    snprintf(pkBuf, sizeof(pkBuf), "%.1f", classifier.getPeakLevelPercent());
    mqtt->publish((base + "peak").c_str(), 0, false, pkBuf);

    char nrgBuf[8];
    snprintf(nrgBuf, sizeof(nrgBuf), "%u", soundEnergy);
    mqtt->publish((base + "energy").c_str(), 0, false, nrgBuf);

    char bpmBuf[8];
    snprintf(bpmBuf, sizeof(bpmBuf), "%u", bpm);
    mqtt->publish((base + "bpm").c_str(), 0, false, bpmBuf);

    lastPubTime = now;
  }

  if (force || stateChanged || timeForHeartbeat) {
    mqtt->publish((base + "detected").c_str(), 0, false, soundState ? "ON" : "OFF");
    mqtt->publish((base + "sound").c_str(), 0, false, soundState ? "ON" : "OFF");
    mqtt->publish((base + "state").c_str(), 0, false, classifier.getSemanticStateStr());
    mqtt->publish((base + "phrase").c_str(), 0, false, classifier.getPhraseStateStr());
    mqtt->publish((base + "status").c_str(), 0, false, source.getStatusDescription());

    lastSoundState = soundState;
    lastSemanticState = semanticState;
    lastPhraseState = phraseState;
    lastSoundEnergy = soundEnergy;
    lastBpm = bpm;
    lastSourceStatus = srcStatus;
  }

  if (timeForHeartbeat || force) {
    lastHeartbeatTime = now;
  }
}

#endif // WLED_DISABLE_MQTT
#endif // ARDUINO_ARCH_ESP32
