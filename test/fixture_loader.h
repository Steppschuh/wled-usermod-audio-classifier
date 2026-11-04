// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#pragma once

#include <ArduinoJson.h>
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
#include <unity.h>
#include "classifier.h"

struct FixtureAssertion {
  std::string description;
  float timeSec = -1.0f;
  float minTimeSec = -1.0f;
  float maxTimeSec = -1.0f;
  std::string expectedPresence;
  std::string expectedState;
  std::string expectedPhrase;
  bool fired = false;
};

class FixtureReplayer {
public:
  static bool replayAndVerify(const std::string& fixturePath, AudioClassifier& classifier) {
    std::ifstream file(fixturePath);
    if (!file.is_open()) {
      // Try alternate working directory path if running from different build folder
      std::string altPath = "test/fixtures/" + fixturePath;
      file.open(altPath);
      if (!file.is_open()) {
        std::cerr << "Failed to open fixture file: " << fixturePath << std::endl;
        TEST_FAIL_MESSAGE(("Failed to open fixture file: " + fixturePath).c_str());
        return false;
      }
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
      std::string errStr = std::string("JSON parse error: ") + error.c_str();
      TEST_FAIL_MESSAGE(errStr.c_str());
      return false;
    }

    // Parse assertions
    std::vector<FixtureAssertion> assertions;
    JsonArray assertArr = doc["metadata"]["assertions"].as<JsonArray>();
    for (JsonObject a : assertArr) {
      FixtureAssertion fa;
      fa.description = a["description"].as<std::string>();
      if (a["time_sec"].is<float>()) {
        fa.timeSec = a["time_sec"].as<float>();
      }
      if (a["min_time_sec"].is<float>()) {
        fa.minTimeSec = a["min_time_sec"].as<float>();
      }
      if (a["max_time_sec"].is<float>()) {
        fa.maxTimeSec = a["max_time_sec"].as<float>();
      }
      if (a["expected_presence"].is<std::string>()) {
        fa.expectedPresence = a["expected_presence"].as<std::string>();
      }
      if (a["expected_state"].is<std::string>()) {
        fa.expectedState = a["expected_state"].as<std::string>();
      }
      if (a["expected_phrase"].is<std::string>()) {
        fa.expectedPhrase = a["expected_phrase"].as<std::string>();
      }
      assertions.push_back(fa);
    }

    JsonArray frames = doc["frames"].as<JsonArray>();
    if (frames.size() == 0) {
      TEST_FAIL_MESSAGE("Fixture contains no frames!");
      return false;
    }

    uint32_t t0 = frames[0]["t"].as<uint32_t>();

    // Replay frames
    for (JsonObject f : frames) {
      AudioFrame frame;
      uint32_t rawT = f["t"].as<uint32_t>();
      frame.timestampMs = (rawT >= t0) ? (rawT - t0) : rawT;
      float frameTimeSec = (float)frame.timestampMs / 1000.0f;

      frame.rawVolume = f["raw"] | 0.0f;
      frame.smoothedVolume = f["smth"] | 0.0f;
      float effectiveVol = (frame.smoothedVolume > frame.rawVolume) ? frame.smoothedVolume : frame.rawVolume;
      frame.levelPercent = (effectiveVol / 255.0f) * 100.0f;
      frame.isPeak = (f["pk"] | 0) != 0;
      frame.majorPeakHz = f["hz"] | 0.0f;
      frame.peakMagnitude = f["mag"] | 0.0f;

      JsonArray bands = f["bands"];
      for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) {
        frame.bands[b] = (b < bands.size()) ? bands[b].as<uint8_t>() : 0;
      }

      // Process frame through classifier
      classifier.processFrame(frame);

      // Evaluate assertions
      for (auto& a : assertions) {
        // Point assertion
        if (a.timeSec >= 0.0f && !a.fired && frameTimeSec >= a.timeSec) {
          a.fired = true;
          if (!a.expectedPresence.empty()) {
            const char* actualPres = (classifier.getPresence() == SoundPresence::SOUND) ? "sound" : "quiet";
            if (a.expectedPresence != actualPres) {
              std::string msg = "Assertion failed [" + a.description + "] at " + std::to_string(frameTimeSec) + 
                                "s: Expected presence '" + a.expectedPresence + "' but got '" + actualPres + "'";
              TEST_FAIL_MESSAGE(msg.c_str());
            }
          }
          if (!a.expectedState.empty()) {
            const char* actualState = classifier.getSemanticStateStr();
            if (a.expectedState != actualState) {
              std::string msg = "Assertion failed [" + a.description + "] at " + std::to_string(frameTimeSec) + 
                                "s: Expected state '" + a.expectedState + "' but got '" + actualState + "'";
              TEST_FAIL_MESSAGE(msg.c_str());
            }
          }
          if (!a.expectedPhrase.empty()) {
            const char* actualPhrase = classifier.getPhraseStateStr();
            if (a.expectedPhrase != actualPhrase) {
              std::string msg = "Assertion failed [" + a.description + "] at " + std::to_string(frameTimeSec) + 
                                "s: Expected phrase '" + a.expectedPhrase + "' but got '" + actualPhrase + "'";
              TEST_FAIL_MESSAGE(msg.c_str());
            }
          }
        }

        // Window assertion
        if (a.minTimeSec >= 0.0f && a.maxTimeSec >= 0.0f) {
          if (frameTimeSec >= a.minTimeSec && frameTimeSec <= a.maxTimeSec) {
            a.fired = true;
            if (!a.expectedPresence.empty()) {
              const char* actualPres = (classifier.getPresence() == SoundPresence::SOUND) ? "sound" : "quiet";
              if (a.expectedPresence != actualPres) {
                std::string msg = "Window assertion failed [" + a.description + "] at " + std::to_string(frameTimeSec) + 
                                  "s: Expected presence '" + a.expectedPresence + "' but got '" + actualPres + "'";
                TEST_FAIL_MESSAGE(msg.c_str());
              }
            }
            if (!a.expectedState.empty()) {
              const char* actualState = classifier.getSemanticStateStr();
              if (a.expectedState != actualState) {
                std::string msg = "Window assertion failed [" + a.description + "] at " + std::to_string(frameTimeSec) + 
                                  "s: Expected state '" + a.expectedState + "' but got '" + actualState + "'";
                TEST_FAIL_MESSAGE(msg.c_str());
              }
            }
          }
        }
      }
    }

    // Verify all point assertions fired
    for (const auto& a : assertions) {
      if (a.timeSec >= 0.0f && !a.fired) {
        std::string msg = "Assertion never evaluated (time beyond fixture duration?): " + a.description;
        TEST_FAIL_MESSAGE(msg.c_str());
      }
    }

    return true;
  }
};
