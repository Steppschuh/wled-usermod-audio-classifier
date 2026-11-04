// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "audio_frame.h"

enum class SoundPresence : uint8_t {
  QUIET = 0,
  SOUND = 1
};

enum class AudioSemanticState : uint8_t {
  SILENCE = 0,
  AMBIENT_NOISE = 1,
  MUSIC = 2
};

enum class MusicPhrase : uint8_t {
  STEADY = 0,
  BUILDUP = 1,
  BREAKDOWN = 2,
  DROP = 3
};

struct ClassifierConfig {
  float quietThresholdPercent = 6.0f;     // Absolute minimum % to qualify as sound
  float marginAboveFloorPercent = 4.0f;   // Dynamic margin above background noise floor
  uint32_t onDelayMs = 1200;              // Required sustain to trigger SOUND state
  uint32_t offDelayMs = 8000;             // Required sustain to return to QUIET state
  bool experimentalEnabled = false;       // Enable experimental music & phrase detection
  uint32_t musicHoldMs = 30000;           // Hold music state for up to 30s during breakdowns/quiet sections
};

class AudioClassifier {
public:
  AudioClassifier(const ClassifierConfig& config = ClassifierConfig());

  void setConfig(const ClassifierConfig& newConfig);
  const ClassifierConfig& getConfig() const { return config; }

  void processFrame(const AudioFrame& frame);
  void reset();

  // Primary Telemetry
  float getCurrentLevelPercent() const { return currentLevel; }
  float getAverage1sLevelPercent() const { return average1sLevel; }
  float getNoiseFloorPercent() const { return noiseFloor; }
  SoundPresence getPresence() const { return presence; }
  bool isSoundDetected() const { return presence == SoundPresence::SOUND; }

  // Semantic & Phrasing Telemetry
  AudioSemanticState getSemanticState() const { return semanticState; }
  const char* getSemanticStateStr() const;
  MusicPhrase getPhraseState() const { return phraseState; }
  const char* getPhraseStateStr() const;
  float getMusicConfidence() const { return musicConfidence; }

private:
  void update1sMetrics(uint32_t now);
  void updateNoiseFloor(float new1sSample);
  void updatePresence(uint32_t now);
  void updateMusicAndPhrasing(const AudioFrame& frame);

  ClassifierConfig config;

  // Runtime Level Tracking
  float currentLevel = 0.0f;
  float average1sLevel = 0.0f;
  float noiseFloor = 2.0f;

  // 1-Second Energy Accumulator
  float sumSquaredEnergy = 0.0f;
  size_t frameCount1s = 0;
  uint32_t last1sTickMs = 0;

  // Noise Floor Histogram (10th Percentile over up to 300s / 5min)
  static constexpr size_t HISTOGRAM_BINS = 101; // 0% to 100%
  static constexpr size_t HISTORY_MAX_SECS = 300;
  uint16_t histogram[HISTOGRAM_BINS] = {0};
  uint8_t historyRing[HISTORY_MAX_SECS] = {0};
  size_t historyIdx = 0;
  size_t historyCount = 0;

  // Presence State Machine & Debounce
  SoundPresence presence = SoundPresence::QUIET;
  bool isPendingOn = false;
  uint32_t pendingOnTime = 0;
  bool isPendingOff = false;
  uint32_t pendingOffTime = 0;

  // Experimental Classification & Phrasing
  AudioSemanticState semanticState = AudioSemanticState::SILENCE;
  MusicPhrase phraseState = MusicPhrase::STEADY;
  float musicConfidence = 0.0f;
  uint32_t lastPhraseChangeMs = 0;
  uint32_t lastMusicEvidenceTime = 0;
  uint32_t lastFrameTimeMs = 0;

  // Beat Tracking Memory
  static constexpr size_t MAX_BEATS = 16;
  uint32_t beatTimes[MAX_BEATS] = {0};
  size_t beatIdx = 0;
  size_t beatCount = 0;
  float beatRegularity = 0.0f;

  // Energy & Phrasing Memory
  float prevBandsNorm[NUM_AUDIO_BANDS] = {0};
  float shortBassEnergy = 0.0f;
  float longBassEnergy = 0.0f;
};
