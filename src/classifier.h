// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <math.h>
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
  UNKNOWN = 0,
  VERSE = 1,
  INTRO = 2,
  BUILDUP = 3,
  CHORUS = 4,
  BREAKDOWN = 5,
  OUTRO = 6
};

enum class SoundEnergy : uint8_t {
  SILENCE   = 0,
  NOISE     = 10,
  BREAKDOWN = 20,
  OUTRO     = 35,
  INTRO     = 50,
  VERSE     = 70,
  BUILDUP   = 85,
  CHORUS    = 100
};

template<size_t N>
struct RollingBuffer {
  float values[N] = {0.0f};
  size_t index = 0;
  size_t count = 0;

  float minVal = 0.0f;
  float maxVal = 0.0f;
  float mean = 0.0f;
  float rms = 0.0f;
  float variance = 0.0f;
  float stdDev = 0.0f;

  void reset() {
    for (size_t i = 0; i < N; i++) values[i] = 0.0f;
    index = 0;
    count = 0;
    minVal = maxVal = mean = rms = variance = stdDev = 0.0f;
  }

  void add(float val) {
    values[index] = val;
    index = (index + 1) % N;
    if (count < N) count++;
  }

  float getLatest() const {
    if (count == 0) return 0.0f;
    size_t lastIdx = (index + N - 1) % N;
    return values[lastIdx];
  }

  void calculate() {
    if (count == 0) {
      minVal = maxVal = mean = rms = variance = stdDev = 0.0f;
      return;
    }
    minVal = values[0];
    maxVal = values[0];
    float sum = 0.0f;
    float sumSq = 0.0f;

    for (size_t i = 0; i < count; i++) {
      float v = values[i];
      if (v < minVal) minVal = v;
      if (v > maxVal) maxVal = v;
      sum += v;
      sumSq += v * v;
    }

    mean = sum / (float)count;
    rms = sqrtf(sumSq / (float)count);

    float diffSqSum = 0.0f;
    for (size_t i = 0; i < count; i++) {
      float diff = values[i] - mean;
      diffSqSum += diff * diff;
    }
    variance = diffSqSum / (float)count;
    stdDev = sqrtf(variance);
  }
};

struct ClassifierConfig {
  float quietThresholdPercent = 6.0f;     // Absolute minimum % to qualify as sound
  float marginAboveFloorPercent = 4.0f;   // Dynamic margin above background noise floor
  uint32_t onDelayMs = 1200;              // Required sustain to trigger SOUND state
  uint32_t offDelayMs = 8000;             // Required sustain to return to QUIET state
  bool experimentalEnabled = true;        // Enable music & phrase detection (defaults to true)
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
  float getAverageLevelPercent() const { return average1sLevel; }
  float getPeakLevelPercent() const { return peak1sLevel; }
  float getNoiseFloorPercent() const { return noiseFloor; }
  SoundPresence getPresence() const { return presence; }
  bool isSoundDetected() const { return presence == SoundPresence::SOUND; }

  // Semantic, Phrasing, Energy & BPM Telemetry
  AudioSemanticState getSemanticState() const { return semanticState; }
  const char* getSemanticStateStr() const;
  MusicPhrase getPhraseState() const {
    if (semanticState != AudioSemanticState::MUSIC) return MusicPhrase::UNKNOWN;
    return phraseState;
  }
  const char* getPhraseStateStr() const;
  float getMusicConfidence() const { return musicConfidence; }
  uint16_t getBPM() const {
    if (semanticState != AudioSemanticState::MUSIC) return 0;
    return beatsPerMinute;
  }
  float getBeatProbability() const { return beatProbability; }
  float getBeatProbabilityPercent() const { return beatProbability * 100.0f; }
  uint8_t getSoundEnergy() const;
  const char* getSoundEnergyStr() const;
  SoundEnergy getSoundEnergyState() const {
    if (semanticState != AudioSemanticState::MUSIC) return SoundEnergy::SILENCE;
    return soundEnergy;
  }
  uint8_t getExpectedBeatIndex() const { return expectedBeatIndex; }
  uint8_t getExpectedBarIndex() const { return expectedBarIndex; }

private:
  void update1sMetrics(uint32_t now);
  void updateNoiseFloor(float new1sSample);
  void updatePresence(uint32_t now);
  void updateMusicAndPhrasing(const AudioFrame& frame);

  ClassifierConfig config;

  // Runtime Level Tracking
  float currentLevel = 0.0f;
  float average1sLevel = 0.0f;
  float peak1sLevel = 0.0f;
  float current1sPeak = 0.0f;
  float noiseFloor = 2.0f;

  // 1-Second Energy Accumulator
  float sumSquaredEnergy = 0.0f;
  size_t frameCount1s = 0;
  uint32_t last1sTickMs = 0;
  bool has1sTickStart = false;

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
  MusicPhrase phraseState = MusicPhrase::UNKNOWN;
  SoundEnergy soundEnergy = SoundEnergy::SILENCE;
  float musicConfidence = 0.0f;
  uint32_t lastPhraseChangeMs = 0;
  uint32_t lastMusicEvidenceTime = 0;
  uint32_t lastFrameTimeMs = 0;
  uint32_t lastEnergyUpdateTime = 0;

  // Rolling Statistical Buffers (Rave-Totem architecture)
  static constexpr size_t FEATURE_BUFFER_SIZE = 75; // 1.5 seconds at 50Hz (20ms)
  RollingBuffer<FEATURE_BUFFER_SIZE> loudnessBuffer;
  RollingBuffer<FEATURE_BUFFER_SIZE> bassBuffer;
  RollingBuffer<FEATURE_BUFFER_SIZE> lowFreqBuffer; // Low frequency variance buffer (bands 0 & 1)
  RollingBuffer<FEATURE_BUFFER_SIZE> spreadBuffer;
  RollingBuffer<FEATURE_BUFFER_SIZE> beatFeatureBuffer;
  RollingBuffer<8> beatDelayBuffer; // Last 8 beat intervals (2 bars)

  float beatProbability = 0.0f;

  float maxLoudness = 20.0f;
  float decayedMaxLoudness = 20.0f;
  uint32_t lastMaxLoudnessTime = 0;
  float averageLoudnessPercent = 0.0f;

  // Beat & Tempo (BPM) Tracking
  static constexpr size_t MAX_BEATS = 16;
  uint32_t beatTimes[MAX_BEATS] = {0};
  size_t beatIdx = 0;
  size_t beatCount = 0;
  float beatRegularity = 0.0f;

  uint32_t lastBeatTimestamp = 0;
  uint16_t beatsPerMinute = 0;
  float rawBpm = 0.0f;
  uint8_t bpmLockCount = 0;
  uint32_t durationBetweenBeats = 0;
  uint32_t durationBetweenBars = 0;
  uint32_t durationBetweenPhrases = 0;
  uint32_t lastBpmUpdateTime = 0;
  uint32_t missedBeats = 0;
  float expectedBeatProb = 0.0f;

  // Drop Detection & 4/4 Phrase Grid Tracking
  uint32_t lastDropTimestamp = 0;
  uint32_t lastExpectedPhraseStart = 0;
  uint8_t expectedBeatIndex = 0;
  uint8_t expectedBarIndex = 0;
  uint8_t expectedPhraseIndex = 0;

  // Energy & Phrasing Memory
  float prevBandsNorm[NUM_AUDIO_BANDS] = {0};
  float prevBassEnergy = 0.0f;
  float shortBassEnergy = 0.0f;
  float longBassEnergy = 0.0f;

  // Outlier Detection & Revert Tracking
  SoundEnergy tentativePrevEnergy = SoundEnergy::SILENCE;
  MusicPhrase tentativePrevPhrase = MusicPhrase::UNKNOWN;
  uint32_t tentativePrevPhraseStartMs = 0;
  uint32_t tentativeTransitionMs = 0;
  bool isTentativeTransition = false;
  uint8_t tentativeBeatsSeen = 0;
};
