// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#pragma once

#include <ArduinoJson.h>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <unity.h>
#include "classifier.h"

struct TrackSegment {
  float startSec = 0.0f;
  float endSec = 0.0f;
  std::string state;
  std::string phrase;
};

struct BenchmarkScore {
  float overallScore = 0.0f;
  float presenceAccuracy = 0.0f;
  float semanticAccuracy = 0.0f;
  float phraseAlignmentScore = 0.0f;
  float stabilityScore = 0.0f;
  size_t evaluatedSeconds = 0;
  size_t stateFlips = 0;
  size_t phraseFlips = 0;
};

struct FixtureAssertion {
  std::string description;
  float timeSec = -1.0f;
  float minTimeSec = -1.0f;
  float maxTimeSec = -1.0f;
  std::string expectedPresence;
  std::string expectedState;
  std::string expectedPhrase;
  uint16_t minBpm = 0;
  uint16_t maxBpm = 0;
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
      if (a["min_bpm"].is<uint16_t>()) {
        fa.minBpm = a["min_bpm"].as<uint16_t>();
      }
      if (a["max_bpm"].is<uint16_t>()) {
        fa.maxBpm = a["max_bpm"].as<uint16_t>();
      }
      assertions.push_back(fa);
    }

    // Parse segments
    std::vector<TrackSegment> segments;
    std::vector<float> boundaries;
    if (doc["metadata"]["segments"].is<JsonArray>()) {
      for (JsonObject s : doc["metadata"]["segments"].as<JsonArray>()) {
        TrackSegment ts;
        ts.startSec = s["start_sec"] | 0.0f;
        ts.endSec = s["end_sec"] | 0.0f;
        ts.state = s["state"] | "music";
        ts.phrase = s["phrase"] | "verse";
        segments.push_back(ts);
        boundaries.push_back(ts.startSec);
        boundaries.push_back(ts.endSec);
      }
    }

    std::string primaryLabel = doc["metadata"]["primary_label"] | "silence";
    float groundTruthBpm = doc["metadata"]["ground_truth_bpm"] | 0.0f;

    struct SecondSnap {
      std::string presence;
      std::string state;
      std::string phrase;
      uint16_t bpm = 0;
    };
    std::map<uint32_t, SecondSnap> snapshots;
    uint32_t lastSnapSec = 999999;
    size_t stateFlips = 0;
    size_t phraseFlips = 0;
    std::string prevState = "";
    std::string prevPhrase = "";

    JsonArray frames = doc["frames"].as<JsonArray>();
    if (frames.size() == 0) {
      TEST_FAIL_MESSAGE("Fixture contains no frames!");
      return false;
    }

    uint32_t t0 = frames[0]["t"].as<uint32_t>();
    uint32_t lastRawT = 0xFFFFFFFF;

    // Replay frames
    for (JsonObject f : frames) {
      uint32_t rawT = f["t"].as<uint32_t>();
      if (rawT == lastRawT) continue; // skip duplicate packet
      lastRawT = rawT;

      AudioFrame frame;
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

      uint32_t curSec = frame.timestampMs / 1000;
      if (curSec != lastSnapSec) {
        lastSnapSec = curSec;
        SecondSnap snap;
        snap.presence = (classifier.getPresence() == SoundPresence::SOUND) ? "sound" : "quiet";
        snap.state = classifier.getSemanticStateStr();
        snap.phrase = classifier.getPhraseStateStr();
        snap.bpm = classifier.getBPM();
        snapshots[curSec] = snap;

        if (!prevState.empty() && snap.state != prevState) stateFlips++;
        if (!prevPhrase.empty() && snap.phrase != prevPhrase) phraseFlips++;
        prevState = snap.state;
        prevPhrase = snap.phrase;
      }

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
          if (a.minBpm > 0) {
            uint16_t actBpm = classifier.getBPM();
            if (actBpm < a.minBpm) {
              std::string msg = "Assertion failed [" + a.description + "] at " + std::to_string(frameTimeSec) + 
                                "s: Expected BPM >= " + std::to_string(a.minBpm) + " but got " + std::to_string(actBpm);
              TEST_FAIL_MESSAGE(msg.c_str());
            }
          }
          if (a.maxBpm > 0) {
            uint16_t actBpm = classifier.getBPM();
            if (actBpm > a.maxBpm) {
              std::string msg = "Assertion failed [" + a.description + "] at " + std::to_string(frameTimeSec) + 
                                "s: Expected BPM <= " + std::to_string(a.maxBpm) + " but got " + std::to_string(actBpm);
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
            if (!a.expectedPhrase.empty()) {
              const char* actualPhrase = classifier.getPhraseStateStr();
              if (a.expectedPhrase != actualPhrase) {
                std::string msg = "Window assertion failed [" + a.description + "] at " + std::to_string(frameTimeSec) + 
                                  "s: Expected phrase '" + a.expectedPhrase + "' but got '" + actualPhrase + "'";
                TEST_FAIL_MESSAGE(msg.c_str());
              }
            }
            if (a.minBpm > 0) {
              uint16_t actBpm = classifier.getBPM();
              if (actBpm < a.minBpm) {
                std::string msg = "Window assertion failed [" + a.description + "] at " + std::to_string(frameTimeSec) + 
                                  "s: Expected BPM >= " + std::to_string(a.minBpm) + " but got " + std::to_string(actBpm);
                TEST_FAIL_MESSAGE(msg.c_str());
              }
            }
            if (a.maxBpm > 0) {
              uint16_t actBpm = classifier.getBPM();
              if (actBpm > a.maxBpm) {
                std::string msg = "Window assertion failed [" + a.description + "] at " + std::to_string(frameTimeSec) + 
                                  "s: Expected BPM <= " + std::to_string(a.maxBpm) + " but got " + std::to_string(actBpm);
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

    // Evaluate continuous benchmark score if segments are present
    if (!segments.empty() && !snapshots.empty()) {
      float presErrors = 0.0f;
      float semErrors = 0.0f;
      float phrErrors = 0.0f;
      float sumBpmErrors = 0.0f;
      size_t bpmEvalCount = 0;
      size_t phrEvalCount = 0;
      size_t totalSecs = snapshots.size();

      for (const auto& kv : snapshots) {
        uint32_t sec = kv.first;
        const auto& snap = kv.second;

        // Find expected segment
        std::string expState = primaryLabel;
        std::string expPhrase = "steady";
        for (const auto& seg : segments) {
          if (sec >= (uint32_t)seg.startSec && sec < (uint32_t)seg.endSec) {
            expState = seg.state;
            expPhrase = seg.phrase;
            break;
          }
        }
        std::string expPres = (expState == "silence") ? "quiet" : "sound";

        // Boundary slack window (+/- 1.5s)
        bool inSlack = false;
        for (float b : boundaries) {
          if (fabsf((float)sec - b) <= 1.5f) {
            inSlack = true;
            break;
          }
        }

        // Presence penalty
        if (snap.presence != expPres) {
          if (!inSlack) presErrors += 1.0f;
        }

        // Semantic penalty
        if (snap.state != expState) {
          if (!inSlack) {
            if ((expState == "music" && snap.state == "silence") || (expState == "silence" && snap.state == "music")) {
              semErrors += 1.0f;
            } else if ((expState == "music" && snap.state == "ambient_noise") || (expState == "ambient_noise" && snap.state == "music")) {
              semErrors += 0.8f;
            } else if ((expState == "silence" || expState == "ambient_noise") && (snap.state == "silence" || snap.state == "ambient_noise")) {
              semErrors += 0.25f;
            } else {
              semErrors += 0.5f;
            }
          }
        }

        // Phrase penalty
        if (expState == "music") {
          phrEvalCount++;
          if (snap.phrase != expPhrase) {
            if (!inSlack) {
              if ((expPhrase == "verse" && snap.phrase == "chorus") || (expPhrase == "chorus" && snap.phrase == "verse")) {
                phrErrors += 0.25f;
              } else if ((expPhrase == "breakdown" || expPhrase == "outro") && (snap.phrase == "breakdown" || snap.phrase == "outro")) {
                phrErrors += 0.20f;
              } else if (expPhrase == "buildup" || snap.phrase == "buildup") {
                phrErrors += 0.25f;
              } else if ((expPhrase == "chorus" && (snap.phrase == "breakdown" || snap.phrase == "outro")) ||
                         ((expPhrase == "breakdown" || expPhrase == "outro") && snap.phrase == "chorus")) {
                phrErrors += 1.0f;
              } else {
                phrErrors += 0.50f;
              }
            }
          }

          // Continuous BPM evaluation during active music (excluding quiet breakdown/outro)
          if (groundTruthBpm > 0.0f && expPhrase != "breakdown" && expPhrase != "outro") {
            if (snap.bpm > 0) {
              float err = fabsf((float)snap.bpm - groundTruthBpm);
              sumBpmErrors += err;
              bpmEvalCount++;
            }
          }
        }
      }

      float presAcc = (totalSecs > 0) ? fmaxf(0.0f, 100.0f * (1.0f - (presErrors / (float)totalSecs))) : 100.0f;
      float semAcc = (totalSecs > 0) ? fmaxf(0.0f, 100.0f * (1.0f - (semErrors / (float)totalSecs))) : 100.0f;
      float phrAcc = (phrEvalCount > 0) ? fmaxf(0.0f, 100.0f * (1.0f - (phrErrors / (float)phrEvalCount))) : 100.0f;
      float bpmMae = (bpmEvalCount > 0) ? (sumBpmErrors / (float)bpmEvalCount) : 0.0f;
      float bpmAcc = (bpmEvalCount > 0) ? fmaxf(0.0f, 100.0f * (1.0f - (bpmMae / 20.0f))) : 100.0f;

      size_t expectedFlips = segments.size();
      size_t excessFlips = ((stateFlips + phraseFlips) > expectedFlips * 2) ? ((stateFlips + phraseFlips) - expectedFlips * 2) : 0;
      float stabilityScore = fmaxf(0.0f, 100.0f - ((float)excessFlips * 2.5f));

      float overallScore = (0.35f * semAcc) + (0.25f * presAcc) + (0.25f * phrAcc) + (0.15f * stabilityScore);

      std::cout << "\n========================================================================================\n"
                << "📊 Native Fixture Benchmark Score: " << std::fixed << std::setprecision(1) << overallScore << "%\n"
                << "   Presence: " << presAcc << "% | Semantic: " << semAcc << "% | Phrase: " << phrAcc << "% | Stability: " << stabilityScore << "%\n";
      if (groundTruthBpm > 0.0f) {
        std::cout << "   🎵 BPM Accuracy: " << bpmAcc << "% | MAE: " << bpmMae << " BPM (Ground Truth: " << groundTruthBpm << " BPM, Evaluated: " << bpmEvalCount << "s)\n";
      }
      std::cout << "========================================================================================\n";

      TEST_ASSERT_TRUE_MESSAGE(overallScore >= 75.0f, "Continuous benchmark score dropped below 75% quality threshold!");
      if (groundTruthBpm > 0.0f) {
        TEST_ASSERT_TRUE_MESSAGE(bpmAcc >= 80.0f, "Continuous BPM Accuracy dropped below 80%!");
        TEST_ASSERT_TRUE_MESSAGE(bpmMae <= 4.0f, "Continuous BPM Mean Absolute Error exceeds 4.0 BPM!");
      }
    }

    return true;
  }
};
