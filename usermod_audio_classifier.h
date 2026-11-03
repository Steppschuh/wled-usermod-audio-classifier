#pragma once

#include "wled.h"
#include <driver/i2s.h>
#include <math.h>

#ifndef USERMOD_ID_AUDIO_CLASSIFIER
  #define USERMOD_ID_AUDIO_CLASSIFIER 205
#endif

#ifndef AUDIO_CLASSIFIER_SD_PIN
  #define AUDIO_CLASSIFIER_SD_PIN 26 // Gledopto WLED+MIC default
#endif
#ifndef AUDIO_CLASSIFIER_WS_PIN
  #define AUDIO_CLASSIFIER_WS_PIN 5  // Gledopto WLED+MIC default
#endif
#ifndef AUDIO_CLASSIFIER_SCK_PIN
  #define AUDIO_CLASSIFIER_SCK_PIN 21 // Gledopto WLED+MIC default
#endif

class AudioClassifierUsermod : public Usermod {
private:
  // PROGMEM configuration and JSON keys
  static const char _name[];
  static const char _enabled[];
  static const char _haDiscovery[];
  static const char _pinSD[];
  static const char _pinWS[];
  static const char _pinSCK[];
  static const char _dbOffset[];
  static const char _silenceThreshold[];

  // Settings
  bool enabled = true;
  bool haDiscoveryEnabled = true;
  int8_t i2sSd = AUDIO_CLASSIFIER_SD_PIN;
  int8_t i2sWs = AUDIO_CLASSIFIER_WS_PIN;
  int8_t i2sSck = AUDIO_CLASSIFIER_SCK_PIN;
  float dbOffset = 0.0f;              // Manual calibration offset against a real dB meter
  float silenceThresholdDb = 45.0f;   // Ambient silence cutoff

  // Runtime State
  bool initDone = false;
  bool i2sInitialized = false;
  float currentDbSpl = 35.0f;
  float peakDb = 35.0f;
  float rollingBaseline = 40.0f;
  String audioState = "silence";      // "silence" | "ambient_noise" | "music"
  bool musicActive = false;
  String phraseState = "steady";      // "steady" | "buildup" | "breakdown" | "drop"
  float musicConfidence = 0.0f;

  // Timers
  unsigned long lastSampleTime = 0;
  unsigned long lastTelemetryPublish = 0;
  unsigned long musicStartTime = 0;
  bool discoveryPublished = false;

  // I2S buffer
  static const size_t SAMPLE_BLOCK_SIZE = 256;
  int32_t sampleBuffer[SAMPLE_BLOCK_SIZE];

  // Energy tracking for phrasing
  float energyHistory[8] = {0};
  uint8_t energyHistoryIdx = 0;

  bool initI2S() {
    if (i2sInitialized) return true;
    if (i2sSd < 0 || i2sWs < 0 || i2sSck < 0) return false;

    i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = 16000,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = SAMPLE_BLOCK_SIZE,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
      .bck_io_num = i2sSck,
      .ws_io_num = i2sWs,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = i2sSd
    };

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (err != ESP_OK) return false;

    err = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (err != ESP_OK) {
      i2s_driver_uninstall(I2S_NUM_0);
      return false;
    }

    i2sInitialized = true;
    return true;
  }

  void deinitI2S() {
    if (i2sInitialized) {
      i2s_driver_uninstall(I2S_NUM_0);
      i2sInitialized = false;
    }
  }

  void processAudioBlock() {
    if (!i2sInitialized) return;

    size_t bytesRead = 0;
    esp_err_t result = i2s_read(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesRead, 10 / portTICK_PERIOD_MS);
    if (result != ESP_OK || bytesRead == 0) return;

    size_t samples = bytesRead / sizeof(int32_t);
    if (samples == 0) return;

    // Calculate RMS & Peak
    double sumSquares = 0.0;
    int32_t maxVal = 0;

    for (size_t i = 0; i < samples; i++) {
      int32_t val = sampleBuffer[i] >> 14; // Normalize 24-bit MEMS in 32-bit slot
      sumSquares += (double)val * val;
      if (abs(val) > maxVal) maxVal = abs(val);
    }

    double rms = sqrt(sumSquares / samples);

    // Approximate dBA conversion (calibrated relative to digital full scale + user offset)
    float rawDb = 20.0f * log10f((float)rms + 1.0f) + dbOffset;
    if (rawDb < 20.0f) rawDb = 20.0f; // Ambient noise floor clamp
    if (rawDb > 120.0f) rawDb = 120.0f;

    // Smooth dBA reading
    currentDbSpl = (currentDbSpl * 0.7f) + (rawDb * 0.3f);
    peakDb = 20.0f * log10f((float)maxVal + 1.0f) + dbOffset;

    // Update rolling baseline
    if (currentDbSpl < rollingBaseline) {
      rollingBaseline = (rollingBaseline * 0.95f) + (currentDbSpl * 0.05f);
    } else {
      rollingBaseline = (rollingBaseline * 0.999f) + (currentDbSpl * 0.001f);
    }

    // Classify Acoustic State (Silence vs Ambient Noise vs Music)
    if (currentDbSpl < silenceThresholdDb) {
      audioState = "silence";
      musicActive = false;
      phraseState = "steady";
      musicConfidence = 0.0f;
    } else {
      // Evaluate energy dynamics across blocks
      float currentEnergy = (float)rms;
      float prevAvgEnergy = 0.0f;
      for (int i = 0; i < 8; i++) prevAvgEnergy += energyHistory[i];
      prevAvgEnergy /= 8.0f;

      energyHistory[energyHistoryIdx] = currentEnergy;
      energyHistoryIdx = (energyHistoryIdx + 1) % 8;

      float energyRatio = (prevAvgEnergy > 1.0f) ? (currentEnergy / prevAvgEnergy) : 1.0f;

      // Music detection heuristic: sustained rhythmic energy above ambient baseline
      if (currentDbSpl > (rollingBaseline + 6.0f)) {
        if (musicStartTime == 0) musicStartTime = millis();
        if (millis() - musicStartTime > 1500) { // 1.5s sustain debounce
          audioState = "music";
          musicActive = true;
          musicConfidence = 0.85f;

          // Phrasing state machine
          if (energyRatio > 1.8f) {
            phraseState = "drop";
          } else if (energyRatio > 1.25f) {
            phraseState = "buildup";
          } else if (energyRatio < 0.6f) {
            phraseState = "breakdown";
          } else {
            phraseState = "steady";
          }
        }
      } else {
        musicStartTime = 0;
        audioState = "ambient_noise";
        musicActive = false;
        phraseState = "steady";
        musicConfidence = 0.2f;
      }
    }
  }

  void publishHomeAssistantDiscovery() {
    if (!WLED_MQTT_CONNECTED() || !haDiscoveryEnabled) return;

    String chipId = String((uint32_t)ESP.getEfuseMac(), HEX);
    String devName = (serverDescription[0] != '\0') ? String(serverDescription) : "WLED Audio Classifier";

    // 1. Sound Pressure Level (dB)
    {
      String topic = "homeassistant/sensor/" + chipId + "_spl/config";
      String payload = "{\"name\":\"" + devName + " Sound Level\","
        "\"stat_t\":\"wled/" + String(mqttClientID) + "/audio/spl\","
        "\"unit_of_meas\":\"dB\","
        "\"dev_cla\":\"sound_pressure\","
        "\"stat_cla\":\"measurement\","
        "\"uniq_id\":\"" + chipId + "_spl\","
        "\"dev\":{\"ids\":[\"" + chipId + "\"],\"name\":\"" + devName + "\",\"mf\":\"Steppschuh\",\"mdl\":\"WLED Audio Classifier\"}}";
      mqtt->publish(topic.c_str(), 0, true, payload.c_str());
    }

    // 2. Audio State (silence / ambient_noise / music)
    {
      String topic = "homeassistant/sensor/" + chipId + "_state/config";
      String payload = "{\"name\":\"" + devName + " Audio State\","
        "\"stat_t\":\"wled/" + String(mqttClientID) + "/audio/state\","
        "\"icon\":\"mdi:waveform\","
        "\"uniq_id\":\"" + chipId + "_state\","
        "\"dev\":{\"ids\":[\"" + chipId + "\"]}}";
      mqtt->publish(topic.c_str(), 0, true, payload.c_str());
    }

    // 3. Music Active Binary Sensor
    {
      String topic = "homeassistant/binary_sensor/" + chipId + "_music/config";
      String payload = "{\"name\":\"" + devName + " Music Active\","
        "\"stat_t\":\"wled/" + String(mqttClientID) + "/audio/music\","
        "\"dev_cla\":\"occupancy\","
        "\"pl_on\":\"ON\",\"pl_off\":\"OFF\","
        "\"uniq_id\":\"" + chipId + "_music\","
        "\"dev\":{\"ids\":[\"" + chipId + "\"]}}";
      mqtt->publish(topic.c_str(), 0, true, payload.c_str());
    }

    // 4. Music Phrase State (steady / buildup / breakdown / drop)
    {
      String topic = "homeassistant/sensor/" + chipId + "_phrase/config";
      String payload = "{\"name\":\"" + devName + " Music Phrase\","
        "\"stat_t\":\"wled/" + String(mqttClientID) + "/audio/phrase\","
        "\"icon\":\"mdi:music-note-eighth\","
        "\"uniq_id\":\"" + chipId + "_phrase\","
        "\"dev\":{\"ids\":[\"" + chipId + "\"]}}";
      mqtt->publish(topic.c_str(), 0, true, payload.c_str());
    }

    discoveryPublished = true;
  }

  void publishTelemetry() {
    if (!WLED_MQTT_CONNECTED()) return;

    String baseTopic = "wled/" + String(mqttClientID) + "/audio/";

    mqtt->publish((baseTopic + "spl").c_str(), 0, false, String(currentDbSpl, 1).c_str());
    mqtt->publish((baseTopic + "state").c_str(), 0, false, audioState.c_str());
    mqtt->publish((baseTopic + "music").c_str(), 0, false, musicActive ? "ON" : "OFF");
    mqtt->publish((baseTopic + "phrase").c_str(), 0, false, phraseState.c_str());
  }

public:
  void setup() override {
    if (!enabled) return;
    initI2S();
    initDone = true;
  }

  void loop() override {
    if (!enabled || strip.isUpdating()) return;

    unsigned long now = millis();

    // Sample audio every ~25ms
    if (now - lastSampleTime >= 25) {
      lastSampleTime = now;
      processAudioBlock();
    }

    // Publish telemetry to MQTT / Home Assistant every 1 second (or on phrase state changes)
    if (now - lastTelemetryPublish >= 1000) {
      lastTelemetryPublish = now;
      if (WLED_MQTT_CONNECTED()) {
        if (!discoveryPublished && haDiscoveryEnabled) {
          publishHomeAssistantDiscovery();
        }
        publishTelemetry();
      }
    }
  }

  void onMqttConnect(bool sessionPresent) override {
    discoveryPublished = false;
    if (haDiscoveryEnabled) {
      publishHomeAssistantDiscovery();
    }
  }

  void addToJsonInfo(JsonObject& root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");

    JsonArray infoArr = user.createNestedArray(FPSTR(_name));
    infoArr.add(String(currentDbSpl, 1) + " dB (" + audioState + ")");
    infoArr.add("Phrase: " + phraseState);
  }

  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject(FPSTR(_name));
    top[FPSTR(_enabled)] = enabled;
    top[FPSTR(_haDiscovery)] = haDiscoveryEnabled;
    top[FPSTR(_pinSD)] = i2sSd;
    top[FPSTR(_pinWS)] = i2sWs;
    top[FPSTR(_pinSCK)] = i2sSck;
    top[FPSTR(_dbOffset)] = dbOffset;
    top[FPSTR(_silenceThreshold)] = silenceThresholdDb;
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root[FPSTR(_name)];
    if (top.isNull()) return false;

    int8_t oldSd = i2sSd, oldWs = i2sWs, oldSck = i2sSck;
    bool oldEnabled = enabled;

    enabled = top[FPSTR(_enabled)] | enabled;
    haDiscoveryEnabled = top[FPSTR(_haDiscovery)] | haDiscoveryEnabled;
    i2sSd = top[FPSTR(_pinSD)] | i2sSd;
    i2sWs = top[FPSTR(_pinWS)] | i2sWs;
    i2sSck = top[FPSTR(_pinSCK)] | i2sSck;
    dbOffset = top[FPSTR(_dbOffset)] | dbOffset;
    silenceThresholdDb = top[FPSTR(_silenceThreshold)] | silenceThresholdDb;

    if (initDone && (oldEnabled != enabled || oldSd != i2sSd || oldWs != i2sWs || oldSck != i2sSck)) {
      deinitI2S();
      if (enabled) initI2S();
    }

    return true;
  }

  uint16_t getId() override {
    return USERMOD_ID_AUDIO_CLASSIFIER;
  }
};

// PROGMEM String definitions
const char AudioClassifierUsermod::_name[]             PROGMEM = "AudioClassifier";
const char AudioClassifierUsermod::_enabled[]          PROGMEM = "enabled";
const char AudioClassifierUsermod::_haDiscovery[]      PROGMEM = "ha-discovery";
const char AudioClassifierUsermod::_pinSD[]            PROGMEM = "pin-sd";
const char AudioClassifierUsermod::_pinWS[]            PROGMEM = "pin-ws";
const char AudioClassifierUsermod::_pinSCK[]           PROGMEM = "pin-sck";
const char AudioClassifierUsermod::_dbOffset[]         PROGMEM = "db-offset";
const char AudioClassifierUsermod::_silenceThreshold[] PROGMEM = "silence-threshold-db";

// Register usermod dynamically
static AudioClassifierUsermod audioClassifier;
REGISTER_USERMOD(audioClassifier);
