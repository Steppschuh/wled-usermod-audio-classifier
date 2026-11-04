// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#include "classifier.h"
#include <math.h>

AudioClassifier::AudioClassifier(const ClassifierConfig& initialConfig)
  : config(initialConfig) {
  reset();
}

void AudioClassifier::reset() {
  currentLevel = 0.0f;
  average1sLevel = 0.0f;
  peak1sLevel = 0.0f;
  current1sPeak = 0.0f;
  noiseFloor = 2.0f;

  sumSquaredEnergy = 0.0f;
  frameCount1s = 0;
  last1sTickMs = 0;
  has1sTickStart = false;

  for (size_t i = 0; i < HISTOGRAM_BINS; i++) histogram[i] = 0;
  for (size_t i = 0; i < HISTORY_MAX_SECS; i++) historyRing[i] = 0;
  historyIdx = 0;
  historyCount = 0;

  presence = SoundPresence::QUIET;
  isPendingOn = false;
  pendingOnTime = 0;
  isPendingOff = false;
  pendingOffTime = 0;

  semanticState = AudioSemanticState::SILENCE;
  phraseState = MusicPhrase::STEADY;
  musicConfidence = 0.0f;
  lastPhraseChangeMs = 0;
  lastMusicEvidenceTime = 0;
  lastFrameTimeMs = 0;

  for (size_t i = 0; i < MAX_BEATS; i++) beatTimes[i] = 0;
  beatIdx = 0;
  beatCount = 0;
  beatRegularity = 0.0f;

  for (size_t i = 0; i < NUM_AUDIO_BANDS; i++) prevBandsNorm[i] = 0.0f;
  shortBassEnergy = 0.0f;
  longBassEnergy = 0.0f;
}

void AudioClassifier::setConfig(const ClassifierConfig& newConfig) {
  config = newConfig;
}

const char* AudioClassifier::getSemanticStateStr() const {
  switch (semanticState) {
    case AudioSemanticState::SILENCE:       return "silence";
    case AudioSemanticState::AMBIENT_NOISE: return "ambient_noise";
    case AudioSemanticState::MUSIC:         return "music";
    default:                                return "unknown";
  }
}

const char* AudioClassifier::getPhraseStateStr() const {
  switch (phraseState) {
    case MusicPhrase::STEADY:    return "steady";
    case MusicPhrase::BUILDUP:   return "buildup";
    case MusicPhrase::BREAKDOWN: return "breakdown";
    case MusicPhrase::DROP:      return "drop";
    default:                     return "steady";
  }
}

void AudioClassifier::processFrame(const AudioFrame& frame) {
  uint32_t now = frame.timestampMs;

  // 1. Smooth instantaneous level and track 1s peak
  currentLevel = (currentLevel * 0.7f) + (frame.levelPercent * 0.3f);
  if (frame.levelPercent > current1sPeak) {
    current1sPeak = frame.levelPercent;
  }

  // 2. Accumulate energy for 1-second metric
  sumSquaredEnergy += (frame.levelPercent * frame.levelPercent);
  frameCount1s++;

  if (!has1sTickStart) {
    last1sTickMs = now;
    has1sTickStart = true;
  }

  // 3. Periodic 1-second calculations & noise floor update
  if (now - last1sTickMs >= 1000) {
    update1sMetrics(now);
    last1sTickMs = now;
  }

  // 4. Presence State Machine
  updatePresence(now);

  // 5. Experimental semantic classification & phrasing
  if (presence == SoundPresence::QUIET) {
    bool hangoverActive = (lastMusicEvidenceTime > 0 && (now - lastMusicEvidenceTime <= config.musicHoldMs));
    if (!hangoverActive) {
      semanticState = AudioSemanticState::SILENCE;
      phraseState = MusicPhrase::STEADY;
      musicConfidence = 0.0f;
    } else {
      updateMusicAndPhrasing(frame);
    }
  } else {
    updateMusicAndPhrasing(frame);
  }
}

void AudioClassifier::update1sMetrics(uint32_t now) {
  if (frameCount1s > 0) {
    average1sLevel = sqrtf(sumSquaredEnergy / (float)frameCount1s);
    peak1sLevel = current1sPeak;
  } else {
    average1sLevel = currentLevel;
    peak1sLevel = currentLevel;
  }
  sumSquaredEnergy = 0.0f;
  current1sPeak = 0.0f;
  frameCount1s = 0;

  // Only adapt baseline background noise floor while the room is in QUIET state
  // and sound is not actively pending to turn on.
  // This completely prevents sustained music from contaminating and elevating the baseline noise floor!
  if (presence == SoundPresence::QUIET && !isPendingOn) {
    updateNoiseFloor(average1sLevel);
  }
}

void AudioClassifier::updateNoiseFloor(float new1sSample) {
  uint8_t bin = (uint8_t)roundf(new1sSample);
  if (bin >= HISTOGRAM_BINS) bin = HISTOGRAM_BINS - 1;

  // Evict oldest sample if histogram history ring is saturated
  if (historyCount == HISTORY_MAX_SECS) {
    uint8_t oldBin = historyRing[historyIdx];
    if (histogram[oldBin] > 0) {
      histogram[oldBin]--;
    }
  } else {
    historyCount++;
  }

  // Insert new sample
  historyRing[historyIdx] = bin;
  histogram[bin]++;
  historyIdx = (historyIdx + 1) % HISTORY_MAX_SECS;

  // Compute 10th percentile rank
  size_t targetRank = (historyCount * 10) / 100;
  if (targetRank == 0) targetRank = 1;

  size_t cumSum = 0;
  uint8_t p10Bin = 0;
  for (size_t b = 0; b < HISTOGRAM_BINS; b++) {
    cumSum += histogram[b];
    if (cumSum >= targetRank) {
      p10Bin = (uint8_t)b;
      break;
    }
  }

  noiseFloor = (float)p10Bin;
}

void AudioClassifier::updatePresence(uint32_t now) {
  float threshold = config.quietThresholdPercent;
  float dynamicThreshold = noiseFloor + config.marginAboveFloorPercent;
  if (dynamicThreshold > threshold) {
    threshold = dynamicThreshold;
  }

  if (currentLevel > threshold) {
    isPendingOff = false;
    if (presence == SoundPresence::QUIET) {
      if (!isPendingOn) {
        isPendingOn = true;
        pendingOnTime = now;
      }
      if (now - pendingOnTime >= config.onDelayMs) {
        presence = SoundPresence::SOUND;
        isPendingOn = false;
      }
    }
  } else {
    isPendingOn = false;
    if (presence == SoundPresence::SOUND) {
      if (!isPendingOff) {
        isPendingOff = true;
        pendingOffTime = now;
      }
      if (now - pendingOffTime >= config.offDelayMs) {
        presence = SoundPresence::QUIET;
        isPendingOff = false;
      }
    }
  }
}

void AudioClassifier::updateMusicAndPhrasing(const AudioFrame& frame) {
  if (!config.experimentalEnabled) {
    semanticState = AudioSemanticState::AMBIENT_NOISE;
    phraseState = MusicPhrase::STEADY;
    musicConfidence = 0.0f;
    return;
  }

  uint32_t now = frame.timestampMs;

  // 1. Beat regularity tracking from AR isPeak events
  if (frame.isPeak) {
    beatTimes[beatIdx] = now;
    beatIdx = (beatIdx + 1) % MAX_BEATS;
    if (beatCount < MAX_BEATS) beatCount++;

    // Calculate regularity of inter-beat intervals in the last 6 seconds
    if (beatCount >= 4) {
      float intervals[MAX_BEATS - 1];
      size_t validIntervals = 0;
      float sumIntervals = 0.0f;

      for (size_t i = 1; i < beatCount; i++) {
        size_t currIdx = (beatIdx + MAX_BEATS - i) % MAX_BEATS;
        size_t prevIdx = (beatIdx + MAX_BEATS - i - 1) % MAX_BEATS;
        uint32_t dt = beatTimes[currIdx] - beatTimes[prevIdx];
        if (dt >= 280 && dt <= 1200) { // 50 to 214 BPM
          intervals[validIntervals++] = (float)dt;
          sumIntervals += dt;
        }
      }

      if (validIntervals >= 3) {
        float mean = sumIntervals / validIntervals;
        float var = 0.0f;
        for (size_t i = 0; i < validIntervals; i++) {
          float diff = intervals[i] - mean;
          var += diff * diff;
        }
        float stddev = sqrtf(var / validIntervals);
        float cv = stddev / (mean + 1.0f); // Coefficient of variation

        // CV < 0.25 indicates steady rhythmic metronome/beat
        if (cv < 0.25f) {
          beatRegularity = 0.85f;
        } else if (cv < 0.40f) {
          beatRegularity = 0.55f;
        } else {
          beatRegularity = 0.20f;
        }
      } else {
        beatRegularity *= 0.95f;
      }
    }
  } else {
    beatRegularity *= 0.995f;
  }

  // 2. Band Energy Distribution & Spectral Flux
  float totalBandEnergy = 0.0f;
  float bassEnergy = 0.0f;
  for (size_t i = 0; i < NUM_AUDIO_BANDS; i++) {
    totalBandEnergy += frame.bands[i];
    if (i < 4) bassEnergy += frame.bands[i];
  }

  float bassRatio = (totalBandEnergy > 5.0f) ? (bassEnergy / totalBandEnergy) : 0.0f;

  // 3. Classify Music vs Ambient Noise with Leaky Integrator, Hangover Timer & Hysteresis
  bool hasMusicalEvidence = false;
  float instantConfidence = 0.0f;

  if (beatRegularity > 0.50f) {
    hasMusicalEvidence = true;
    instantConfidence = beatRegularity;
  } else if (bassRatio > 0.35f && currentLevel > (noiseFloor + 5.0f)) {
    hasMusicalEvidence = true;
    instantConfidence = 0.75f;
  }

  // Update confidence score with asymmetrical attack/release
  if (hasMusicalEvidence) {
    lastMusicEvidenceTime = now;
    // Fast attack: smoothly elevate confidence towards target
    musicConfidence = (musicConfidence * 0.85f) + (instantConfidence * 0.15f);
    if (musicConfidence > 1.0f) musicConfidence = 1.0f;
  } else {
    // Slow release: confidence decays gradually over ~30s
    musicConfidence *= 0.999f;
  }

  // State machine transition with configurable hangover protection
  if (semanticState == AudioSemanticState::MUSIC) {
    bool hangoverActive = (lastMusicEvidenceTime > 0 && (now - lastMusicEvidenceTime <= config.musicHoldMs));
    if (!hangoverActive) {
      // Hold period expired: transition back down
      if (presence == SoundPresence::SOUND) {
        semanticState = AudioSemanticState::AMBIENT_NOISE;
      } else {
        semanticState = AudioSemanticState::SILENCE;
      }
      phraseState = MusicPhrase::STEADY;
    }
    // Otherwise, remain locked in MUSIC state (even during low-volume breakdowns/buildups)
  } else {
    // In SILENCE or AMBIENT_NOISE: require confidence > 0.60 to enter MUSIC
    if (musicConfidence >= 0.60f || (beatRegularity >= 0.70f && currentLevel > noiseFloor + 5.0f)) {
      semanticState = AudioSemanticState::MUSIC;
      lastMusicEvidenceTime = now;
      if (musicConfidence < 0.70f) musicConfidence = 0.70f;
    } else if (presence == SoundPresence::SOUND) {
      semanticState = AudioSemanticState::AMBIENT_NOISE;
    } else {
      semanticState = AudioSemanticState::SILENCE;
    }
  }

  // 4. Phrasing State Machine (only evaluated when music is active)
  if (semanticState == AudioSemanticState::MUSIC) {
    // Short-term bass (2s filter) vs Long-term bass (16s filter)
    shortBassEnergy = (shortBassEnergy * 0.92f) + (bassEnergy * 0.08f);
    longBassEnergy = (longBassEnergy * 0.99f) + (bassEnergy * 0.01f);

    float bassTrend = (longBassEnergy > 2.0f) ? (shortBassEnergy / longBassEnergy) : 1.0f;

    // Minimum phrase dwell time: 4000ms
    if (now - lastPhraseChangeMs >= 4000) {
      MusicPhrase prevPhrase = phraseState;

      if (bassTrend < 0.45f || (bassRatio < 0.15f && currentLevel > noiseFloor + 2.0f)) {
        phraseState = MusicPhrase::BREAKDOWN;
      } else if (prevPhrase == MusicPhrase::BREAKDOWN && bassTrend > 1.5f) {
        phraseState = MusicPhrase::DROP;
      } else if (bassTrend > 1.25f) {
        phraseState = MusicPhrase::BUILDUP;
      } else {
        phraseState = MusicPhrase::STEADY;
      }

      if (phraseState != prevPhrase) {
        lastPhraseChangeMs = now;
      }
    }
  } else {
    phraseState = MusicPhrase::STEADY;
  }
}
