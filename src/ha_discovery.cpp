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
    dev["sw"] = versionString;
    dev["mdl"] = "WLED";
    JsonArray ids = dev.createNestedArray("ids");
    ids.add(chipId);
    JsonArray conns = dev.createNestedArray("connections");
    JsonArray macConn = conns.createNestedArray();
    macConn.add("mac");
    macConn.add(chipId);
  };

  // Clear legacy discovery topics to avoid orphaned entities in HA
  mqtt->publish(("homeassistant/binary_sensor/" + chipId + "_audio_sound/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/binary_sensor/" + chipId + "_sound/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_level/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_peak_level/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_peak/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_state/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_phrase/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_energy/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_bpm/config").c_str(), 0, true, "");

  // 1. Relative Audio Level Sensor (%)
  {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "Audio Level RMS";
    doc["stat_t"] = baseTopic + "level";
    doc["unit_of_meas"] = "%";
    doc["stat_cla"] = "measurement";
    doc["icon"] = "mdi:volume-medium";
    doc["uniq_id"] = chipId + "_audio_level_rms";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";
    doc["expire_after"] = 90;

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/sensor/" + chipId + "_audio_level_rms/config";
    String payload;
    serializeJson(doc, payload);
    mqtt->publish(topic.c_str(), 0, true, payload.c_str());
  }

  // 1b. Peak Level Sensor (%)
  {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "Audio Level Peak";
    doc["stat_t"] = baseTopic + "peak";
    doc["unit_of_meas"] = "%";
    doc["stat_cla"] = "measurement";
    doc["icon"] = "mdi:volume-maximum";
    doc["uniq_id"] = chipId + "_audio_level_peak";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";
    doc["expire_after"] = 90;

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/sensor/" + chipId + "_audio_level_peak/config";
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
    doc["icon"] = "mdi:ear-hearing";
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

  // 3. Audio Type Sensor (silence / ambient_noise / music)
  {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "Audio Type";
    doc["stat_t"] = baseTopic + "type";
    doc["dev_cla"] = "enum";
    JsonArray opts = doc.createNestedArray("options");
    opts.add("silence");
    opts.add("ambient_noise");
    opts.add("music");
    doc["icon"] = "mdi:waveform";
    doc["uniq_id"] = chipId + "_audio_type";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/sensor/" + chipId + "_audio_type/config";
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
    doc["icon"] = "mdi:microphone-outline";
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

  // 5. Music Phrase Sensor (Optional / Experimental)
  if (experimentalEnabled) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "Music Phrase";
    doc["stat_t"] = baseTopic + "phrase";
    doc["dev_cla"] = "enum";
    JsonArray opts = doc.createNestedArray("options");
    opts.add("intro");
    opts.add("verse");
    opts.add("buildup");
    opts.add("chorus");
    opts.add("breakdown");
    opts.add("outro");
    doc["icon"] = "mdi:playlist-music";
    doc["uniq_id"] = chipId + "_music_phrase";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/sensor/" + chipId + "_music_phrase/config";
    String payload;
    serializeJson(doc, payload);
    mqtt->publish(topic.c_str(), 0, true, payload.c_str());
  }

  // 6. Music Energy Sensor (0-100%)
  if (experimentalEnabled) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "Music Energy";
    doc["stat_t"] = baseTopic + "energy";
    doc["unit_of_meas"] = "%";
    doc["stat_cla"] = "measurement";
    doc["icon"] = "mdi:lightning-bolt";
    doc["uniq_id"] = chipId + "_music_energy";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";
    doc["expire_after"] = 90;

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/sensor/" + chipId + "_music_energy/config";
    String payload;
    serializeJson(doc, payload);
    mqtt->publish(topic.c_str(), 0, true, payload.c_str());
  }

  // 7. BPM Sensor
  if (experimentalEnabled) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "BPM";
    doc["stat_t"] = baseTopic + "bpm";
    doc["unit_of_meas"] = "bpm";
    doc["stat_cla"] = "measurement";
    doc["icon"] = "mdi:metronome";
    doc["uniq_id"] = chipId + "_bpm";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";
    doc["expire_after"] = 90;

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/sensor/" + chipId + "_bpm/config";
    String payload;
    serializeJson(doc, payload);
    mqtt->publish(topic.c_str(), 0, true, payload.c_str());
  }

  // 8. Music Beat Probability Sensor (0.00 - 1.00)
  if (experimentalEnabled) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<640> doc;
#endif
    doc["name"] = "Music Beat Probability";
    doc["stat_t"] = baseTopic + "beat_probability";
    doc["unit_of_meas"] = "%";
    doc["stat_cla"] = "measurement";
    doc["icon"] = "mdi:drum";
    doc["uniq_id"] = chipId + "_music_beat_probability";
    doc["avty_t"] = statusTopic;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";
    doc["expire_after"] = 90;

    JsonObject dev = doc.createNestedObject("dev");
    buildDevice(dev);

    String topic = "homeassistant/sensor/" + chipId + "_music_beat_probability/config";
    String payload;
    serializeJson(doc, payload);
    mqtt->publish(topic.c_str(), 0, true, payload.c_str());
  }
}

void HomeAssistantAudioDiscovery::removeDiscovery(bool experimentalEnabled) {
  if (!WLED_MQTT_CONNECTED) return;
  String chipId = String(escapedMac);

  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_level_rms/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_level_peak/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_level/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_peak_level/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_peak/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/binary_sensor/" + chipId + "_audio_sound/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/binary_sensor/" + chipId + "_audio_detected/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_type/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_state/config").c_str(), 0, true, "");
  mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_status/config").c_str(), 0, true, "");
  if (experimentalEnabled) {
    mqtt->publish(("homeassistant/sensor/" + chipId + "_music_phrase/config").c_str(), 0, true, "");
    mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_phrase/config").c_str(), 0, true, "");
    mqtt->publish(("homeassistant/sensor/" + chipId + "_music_energy/config").c_str(), 0, true, "");
    mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_energy/config").c_str(), 0, true, "");
    mqtt->publish(("homeassistant/sensor/" + chipId + "_bpm/config").c_str(), 0, true, "");
    mqtt->publish(("homeassistant/sensor/" + chipId + "_audio_bpm/config").c_str(), 0, true, "");
    mqtt->publish(("homeassistant/sensor/" + chipId + "_music_beat_probability/config").c_str(), 0, true, "");
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

    char probBuf[16];
    snprintf(probBuf, sizeof(probBuf), "%.1f", classifier.getBeatProbabilityPercent());
    mqtt->publish((base + "beat_probability").c_str(), 0, false, probBuf);

    lastPubTime = now;
  }

  if (force || stateChanged || timeForHeartbeat) {
    mqtt->publish((base + "detected").c_str(), 0, false, soundState ? "ON" : "OFF");
    mqtt->publish((base + "type").c_str(), 0, false, classifier.getSemanticStateStr());
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
