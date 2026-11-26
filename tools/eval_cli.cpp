// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#include <ArduinoJson.h>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "classifier.h"

namespace {

std::string normStr(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == ' ' || c == '-') out += '_';
    else out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

std::string readAllStream(std::istream& in) {
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool readFileToString(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::in | std::ios::binary);
  if (!f.is_open()) return false;
  out = readAllStream(f);
  return true;
}

void applyConfigOverrides(const JsonObjectConst& obj, ClassifierConfig& cfg) {
  if (!obj["quietThresholdPercent"].isNull()) cfg.quietThresholdPercent = obj["quietThresholdPercent"].as<float>();
  if (!obj["marginAboveFloorPercent"].isNull()) cfg.marginAboveFloorPercent = obj["marginAboveFloorPercent"].as<float>();
  if (!obj["onDelayMs"].isNull()) cfg.onDelayMs = obj["onDelayMs"].as<uint32_t>();
  if (!obj["offDelayMs"].isNull()) cfg.offDelayMs = obj["offDelayMs"].as<uint32_t>();
  if (!obj["experimentalEnabled"].isNull()) cfg.experimentalEnabled = obj["experimentalEnabled"].as<bool>();
  if (!obj["musicHoldMs"].isNull()) cfg.musicHoldMs = obj["musicHoldMs"].as<uint32_t>();
  if (!obj["relChorusLoudRatio"].isNull()) cfg.relChorusLoudRatio = obj["relChorusLoudRatio"].as<float>();
  if (!obj["relChorusPeakRatio"].isNull()) cfg.relChorusPeakRatio = obj["relChorusPeakRatio"].as<float>();
  if (!obj["relChorusDeltaFloor"].isNull()) cfg.relChorusDeltaFloor = obj["relChorusDeltaFloor"].as<float>();
  if (!obj["relChorusBassTrend"].isNull()) cfg.relChorusBassTrend = obj["relChorusBassTrend"].as<float>();
  if (!obj["slopeBuildupCentroid"].isNull()) cfg.slopeBuildupCentroid = obj["slopeBuildupCentroid"].as<float>();
  if (!obj["slopeBuildupHighToBass"].isNull()) cfg.slopeBuildupHighToBass = obj["slopeBuildupHighToBass"].as<float>();
  if (!obj["slopeBuildupHighFlux"].isNull()) cfg.slopeBuildupHighFlux = obj["slopeBuildupHighFlux"].as<float>();
  if (!obj["phraseDwellMinMs"].isNull()) cfg.phraseDwellMinMs = obj["phraseDwellMinMs"].as<uint32_t>();
  if (!obj["outroRecoveryMedRatio"].isNull()) cfg.outroRecoveryMedRatio = obj["outroRecoveryMedRatio"].as<float>();
}

struct AssertionState {
  std::string description;
  bool isPoint = false;
  bool isWindow = false;
  double timeSec = 0.0;
  double minTimeSec = 0.0;
  double maxTimeSec = 0.0;
  std::string expPresence;
  std::string expState;
  std::string expPhrase;
  int minBpm = -1;
  int maxBpm = -1;
  double minBeatProb = -1.0;
  double maxBeatProb = -1.0;
  bool fired = false;
  bool windowChecked = false;
  bool failed = false;
  std::string failureMsg;
};

}  // namespace

int main(int argc, char** argv) {
  std::string fixturePath;
  std::string configArg;
  uint32_t stepMs = 1000;
  bool trace = false;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--fixture" && i + 1 < argc) {
      fixturePath = argv[++i];
    } else if (arg == "--config" && i + 1 < argc) {
      configArg = argv[++i];
    } else if (arg == "--step-ms" && i + 1 < argc) {
      stepMs = static_cast<uint32_t>(std::stoul(argv[++i]));
    } else if (arg == "--trace") {
      trace = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: eval_cli --fixture <path|-> [--config <json|path>] [--step-ms <ms>] [--trace]\n";
      return 0;
    }
  }

  if (fixturePath.empty()) {
    std::cerr << "Missing --fixture <path|->\n";
    return 2;
  }

  std::string inputJson;
  if (fixturePath == "-") {
    inputJson = readAllStream(std::cin);
  } else if (!readFileToString(fixturePath, inputJson)) {
    std::cerr << "Failed to open fixture: " << fixturePath << "\n";
    return 2;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, inputJson);
  if (err) {
    std::cerr << "JSON parse error: " << err.c_str() << "\n";
    return 2;
  }

  ClassifierConfig cfg;
  if (!configArg.empty()) {
    std::string cfgStr = configArg;
    if (!cfgStr.empty() && cfgStr[0] != '{') {
      if (!readFileToString(configArg, cfgStr)) {
        std::cerr << "Failed to open config file: " << configArg << "\n";
        return 2;
      }
    }
    JsonDocument cfgDoc;
    if (deserializeJson(cfgDoc, cfgStr) == DeserializationError::Ok && cfgDoc.is<JsonObject>()) {
      applyConfigOverrides(cfgDoc.as<JsonObjectConst>(), cfg);
    } else {
      std::cerr << "Invalid --config JSON\n";
      return 2;
    }
  }

  JsonObjectConst meta = doc["metadata"].as<JsonObjectConst>();
  JsonArrayConst frames = doc["frames"].as<JsonArrayConst>();
  if (frames.isNull() || frames.size() == 0) {
    std::cerr << "Fixture contains no frames\n";
    return 2;
  }

  std::vector<AssertionState> assertions;
  if (!meta["assertions"].isNull()) {
    for (JsonObjectConst a : meta["assertions"].as<JsonArrayConst>()) {
      AssertionState st;
      st.description = a["description"] | "";
      if (!a["time_sec"].isNull()) {
        st.isPoint = true;
        st.timeSec = a["time_sec"].as<double>();
      }
      if (!a["min_time_sec"].isNull() && !a["max_time_sec"].isNull()) {
        st.isWindow = true;
        st.minTimeSec = a["min_time_sec"].as<double>();
        st.maxTimeSec = a["max_time_sec"].as<double>();
      }
      st.expPresence = a["expected_presence"] | "";
      st.expState = a["expected_state"] | "";
      st.expPhrase = a["expected_phrase"] | "";
      if (!a["min_bpm"].isNull()) st.minBpm = a["min_bpm"].as<int>();
      if (!a["max_bpm"].isNull()) st.maxBpm = a["max_bpm"].as<int>();
      if (!a["min_beat_prob"].isNull()) st.minBeatProb = a["min_beat_prob"].as<double>();
      if (!a["max_beat_prob"].isNull()) st.maxBeatProb = a["max_beat_prob"].as<double>();
      assertions.push_back(st);
    }
  }

  AudioClassifier classifier(cfg);
  uint32_t t0 = frames[0]["t"] | 0u;
  uint32_t lastRawT = 0;
  bool hasLastRawT = false;
  int64_t lastSnapBucket = -1;

  JsonDocument outDoc;
  outDoc["metadata"] = meta;
  JsonArray snapshots = outDoc["snapshots"].to<JsonArray>();
  JsonArray phraseTransitions = outDoc["phrase_transitions"].to<JsonArray>();
  std::string lastPhraseRecorded;

  uint32_t lastTimeMs = 0;
  for (JsonObjectConst f : frames) {
    uint32_t rawT = f["t"] | 0u;
    if (hasLastRawT && rawT == lastRawT) continue;
    hasLastRawT = true;
    lastRawT = rawT;

    uint32_t t = (rawT >= t0) ? (rawT - t0) : rawT;
    double tSec = static_cast<double>(t) / 1000.0;
    lastTimeMs = t;

    float raw = f["raw"] | 0.0f;
    float smth = f["smth"] | 0.0f;
    float effVol = (raw > smth) ? raw : smth;
    float lvl = (effVol / 255.0f) * 100.0f;

    AudioFrame frame = {};
    frame.timestampMs = t;
    frame.levelPercent = lvl;
    frame.rawVolume = raw;
    frame.smoothedVolume = smth;
    frame.isPeak = (f["pk"] | 0) != 0;
    frame.majorPeakHz = f["hz"] | 0.0f;

    JsonArrayConst bands = f["bands"].as<JsonArrayConst>();
    if (!bands.isNull()) {
      size_t n = bands.size() < NUM_AUDIO_BANDS ? bands.size() : NUM_AUDIO_BANDS;
      for (size_t i = 0; i < n; i++) {
        frame.bands[i] = static_cast<uint8_t>(bands[i] | 0);
      }
    }

    classifier.processFrame(frame);

    std::string curPresence = (classifier.getPresence() == SoundPresence::SOUND) ? "sound" : "quiet";
    std::string curState = normStr(classifier.getSemanticStateStr());
    std::string curPhrase = normStr(classifier.getPhraseStateStr());
    uint16_t curBpm = classifier.getBPM();
    float curBeatProb = classifier.getBeatProbability();

    if (curPhrase != "unknown" && curPhrase != lastPhraseRecorded) {
      JsonObject tr = phraseTransitions.add<JsonObject>();
      tr["t_sec"] = tSec;
      tr["phrase"] = curPhrase;
      lastPhraseRecorded = curPhrase;
    }

    int64_t bucket = (stepMs == 0) ? static_cast<int64_t>(t) : static_cast<int64_t>(t / stepMs);
    if (stepMs == 0 || bucket != lastSnapBucket) {
      lastSnapBucket = bucket;
      JsonObject snap = snapshots.add<JsonObject>();
      snap["t_ms"] = t;
      snap["t_sec"] = (stepMs == 0) ? tSec : static_cast<double>(bucket * stepMs) / 1000.0;
      snap["level"] = classifier.getCurrentLevelPercent();
      snap["floor"] = classifier.getNoiseFloorPercent();
      snap["presence"] = curPresence;
      snap["state"] = curState;
      snap["phrase"] = curPhrase;
      snap["conf"] = classifier.getMusicConfidence();
      snap["bpm"] = curBpm;
      snap["beat_prob"] = curBeatProb;
      snap["beats"] = static_cast<uint32_t>(classifier.getBeatCount());
      if (trace) {
        snap["decayed_max"] = classifier.getDecayedMaxLoudness();
        snap["inst_beat_prob"] = classifier.getInstBeatProbability();
        snap["bar_novelty"] = classifier.getBarNovelty();
        snap["high_flux_slope"] = classifier.getHighFluxSlope();
        snap["centroid_slope"] = classifier.getCentroidSlope();
        snap["loudness_slope"] = classifier.getLoudnessSlope();
        snap["track_med_loud"] = classifier.getTrackActiveMedLoud();
        snap["sound_energy"] = classifier.getSoundEnergyStr();
        snap["bar_count"] = classifier.getBarCount();
      }
    }

    for (auto& a : assertions) {
      if (a.isPoint && !a.fired && tSec >= a.timeSec) {
        a.fired = true;
        std::vector<std::string> errs;
        if (!a.expPresence.empty() && normStr(a.expPresence) != curPresence) {
          errs.push_back("presence expected '" + a.expPresence + "', got '" + curPresence + "'");
        }
        if (!a.expState.empty() && normStr(a.expState) != curState) {
          errs.push_back("state expected '" + a.expState + "', got '" + curState + "'");
        }
        if (!a.expPhrase.empty() && normStr(a.expPhrase) != curPhrase) {
          errs.push_back("phrase expected '" + a.expPhrase + "', got '" + curPhrase + "'");
        }
        if (a.minBpm >= 0 && curBpm < static_cast<uint16_t>(a.minBpm)) {
          errs.push_back("bpm expected >= " + std::to_string(a.minBpm) + ", got " + std::to_string(curBpm));
        }
        if (a.maxBpm >= 0 && curBpm > static_cast<uint16_t>(a.maxBpm)) {
          errs.push_back("bpm expected <= " + std::to_string(a.maxBpm) + ", got " + std::to_string(curBpm));
        }
        if (a.minBeatProb >= 0.0 && curBeatProb < a.minBeatProb) {
          errs.push_back("beat_prob expected >= " + std::to_string(a.minBeatProb) + ", got " + std::to_string(curBeatProb));
        }
        if (a.maxBeatProb >= 0.0 && curBeatProb > a.maxBeatProb) {
          errs.push_back("beat_prob expected <= " + std::to_string(a.maxBeatProb) + ", got " + std::to_string(curBeatProb));
        }
        if (!errs.empty()) {
          a.failed = true;
          for (size_t k = 0; k < errs.size(); k++) {
            if (k > 0) a.failureMsg += ", ";
            a.failureMsg += errs[k];
          }
          a.failureMsg = "[" + a.description + "] at " + std::to_string(tSec) + "s: " + a.failureMsg;
        }
      }

      if (a.isWindow && tSec >= a.minTimeSec && tSec <= a.maxTimeSec) {
        a.windowChecked = true;
        if (!a.failed) {
          if (!a.expPresence.empty() && normStr(a.expPresence) != curPresence) {
            a.failed = true;
            a.failureMsg = "[Window: " + a.description + "] at " + std::to_string(tSec) + "s: presence '" + curPresence + "' != '" + a.expPresence + "'";
          } else if (!a.expState.empty() && normStr(a.expState) != curState) {
            a.failed = true;
            a.failureMsg = "[Window: " + a.description + "] at " + std::to_string(tSec) + "s: state '" + curState + "' != '" + a.expState + "'";
          } else if (!a.expPhrase.empty() && normStr(a.expPhrase) != curPhrase) {
            a.failed = true;
            a.failureMsg = "[Window: " + a.description + "] at " + std::to_string(tSec) + "s: phrase '" + curPhrase + "' != '" + a.expPhrase + "'";
          } else if (a.minBpm >= 0 && curBpm < static_cast<uint16_t>(a.minBpm)) {
            a.failed = true;
            a.failureMsg = "[Window: " + a.description + "] at " + std::to_string(tSec) + "s: bpm " + std::to_string(curBpm) + " < " + std::to_string(a.minBpm);
          } else if (a.maxBpm >= 0 && curBpm > static_cast<uint16_t>(a.maxBpm)) {
            a.failed = true;
            a.failureMsg = "[Window: " + a.description + "] at " + std::to_string(tSec) + "s: bpm " + std::to_string(curBpm) + " > " + std::to_string(a.maxBpm);
          } else if (a.minBeatProb >= 0.0 && curBeatProb < a.minBeatProb) {
            a.failed = true;
            a.failureMsg = "[Window: " + a.description + "] at " + std::to_string(tSec) + "s: beat_prob " + std::to_string(curBeatProb) + " < " + std::to_string(a.minBeatProb);
          } else if (a.maxBeatProb >= 0.0 && curBeatProb > a.maxBeatProb) {
            a.failed = true;
            a.failureMsg = "[Window: " + a.description + "] at " + std::to_string(tSec) + "s: beat_prob " + std::to_string(curBeatProb) + " > " + std::to_string(a.maxBeatProb);
          }
        }
      }
    }
  }

  JsonArray assertOut = outDoc["assertions"].to<JsonArray>();
  int passedCount = 0;
  int failedCount = 0;
  for (const auto& a : assertions) {
    if (a.isPoint || (a.isWindow && a.windowChecked)) {
      JsonObject ao = assertOut.add<JsonObject>();
      ao["description"] = a.description;
      ao["passed"] = !a.failed;
      if (a.failed) {
        ao["error"] = a.failureMsg;
        failedCount++;
      } else {
        passedCount++;
      }
    }
  }

  outDoc["duration_sec"] = static_cast<double>(lastTimeMs) / 1000.0;
  outDoc["frame_count"] = frames.size();
  outDoc["passed_assertions"] = passedCount;
  outDoc["failed_assertions"] = failedCount;

  serializeJson(outDoc, std::cout);
  std::cout << "\n";
  return 0;
}
