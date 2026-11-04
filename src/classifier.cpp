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
  phraseState = MusicPhrase::VERSE;
  soundEnergy = SoundEnergy::SILENCE;
  musicConfidence = 0.0f;
  lastPhraseChangeMs = 0;
  lastMusicEvidenceTime = 0;
  lastFrameTimeMs = 0;
  lastEnergyUpdateTime = 0;

  loudnessBuffer.reset();
  bassBuffer.reset();
  spreadBuffer.reset();
  beatFeatureBuffer.reset();
  beatDelayBuffer.reset();

  maxLoudness = 20.0f;
  decayedMaxLoudness = 20.0f;
  lastMaxLoudnessTime = 0;
  averageLoudnessPercent = 0.0f;

  for (size_t i = 0; i < MAX_BEATS; i++) beatTimes[i] = 0;
  beatIdx = 0;
  beatCount = 0;
  beatRegularity = 0.0f;

  lastBeatTimestamp = 0;
  beatsPerMinute = 0;
  rawBpm = 0.0f;
  bpmLockCount = 0;
  durationBetweenBeats = 0;
  durationBetweenBars = 0;
  durationBetweenPhrases = 0;
  lastBpmUpdateTime = 0;
  missedBeats = 0;
  expectedBeatProb = 0.0f;

  lastDropTimestamp = 0;
  lastExpectedPhraseStart = 0;
  expectedBeatIndex = 0;
  expectedBarIndex = 0;
  expectedPhraseIndex = 0;

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
    case MusicPhrase::VERSE:     return "verse";
    case MusicPhrase::INTRO:     return "intro";
    case MusicPhrase::BUILDUP:   return "buildup";
    case MusicPhrase::CHORUS:    return "chorus";
    case MusicPhrase::BREAKDOWN: return "breakdown";
    case MusicPhrase::OUTRO:     return "outro";
    default:                     return "verse";
  }
}

const char* AudioClassifier::getSoundEnergyStr() const {
  switch (soundEnergy) {
    case SoundEnergy::SILENCE:   return "silence";
    case SoundEnergy::NOISE:     return "noise";
    case SoundEnergy::OUTRO:     return "outro";
    case SoundEnergy::BREAKDOWN: return "breakdown";
    case SoundEnergy::BUILDUP:   return "buildup";
    case SoundEnergy::INTRO:     return "intro";
    case SoundEnergy::VERSE:     return "verse";
    case SoundEnergy::CHORUS:    return "chorus";
    default:                     return "silence";
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
      phraseState = MusicPhrase::VERSE;
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
    phraseState = MusicPhrase::VERSE;
    soundEnergy = SoundEnergy::NOISE;
    musicConfidence = 0.0f;
    beatsPerMinute = 0;
    return;
  }

  uint32_t now = frame.timestampMs;

  // 1. Band Energy Distribution & Spectral Flux
  float totalBandEnergy = 0.0f;
  float bassEnergy = 0.0f;
  for (size_t i = 0; i < NUM_AUDIO_BANDS; i++) {
    totalBandEnergy += frame.bands[i];
    if (i < 4) bassEnergy += frame.bands[i];
  }

  float bassRatio = (totalBandEnergy > 5.0f) ? (bassEnergy / totalBandEnergy) : 0.0f;
  float bassPercent = (bassEnergy / 4.0f) * (100.0f / 255.0f);
  float spread = fabsf(frame.rawVolume - frame.smoothedVolume) / 2.55f;

  // 2. Feed 1.5s Rolling Feature Buffers (Rave-Totem architecture)
  loudnessBuffer.add(frame.levelPercent);
  loudnessBuffer.calculate();
  bassBuffer.add(bassPercent);
  bassBuffer.calculate();
  spreadBuffer.add(spread);
  spreadBuffer.calculate();

  // 3. Track Decayed Maximum Loudness (5-minute peak hold with decay)
  if (loudnessBuffer.rms > decayedMaxLoudness) {
    maxLoudness = loudnessBuffer.rms;
    decayedMaxLoudness = maxLoudness;
    lastMaxLoudnessTime = now;
  } else {
    uint32_t dt = now - lastMaxLoudnessTime;
    if (dt > 300000) {
      float decay = 1.0f - ((float)(dt - 300000) / 300000.0f);
      if (decay < 0.0f) decay = 0.0f;
      float minDecayed = (maxLoudness * 0.5f > 15.0f) ? (maxLoudness * 0.5f) : 15.0f;
      decayedMaxLoudness = (maxLoudness * decay > minDecayed) ? (maxLoudness * decay) : minDecayed;
    }
  }
  averageLoudnessPercent = (decayedMaxLoudness > 0.1f) ? (100.0f * loudnessBuffer.rms / decayedMaxLoudness) : 0.0f;

  // 4. Z-Score Onset Feature Extraction (beatFeature)
  float bf1 = spreadBuffer.getLatest() - spreadBuffer.mean;
  bf1 -= (1.0f * spreadBuffer.stdDev);
  if (bf1 < 0.0f) bf1 = 0.0f;
  bf1 = bf1 * bf1 - (3.0f * spreadBuffer.stdDev);
  if (bf1 < 0.0f) bf1 = 0.0f;
  bf1 = sqrtf(bf1);

  float bf2 = bassBuffer.getLatest() - bassBuffer.mean;
  bf2 -= (0.5f * bassBuffer.stdDev);
  if (bf2 < 0.0f) bf2 = 0.0f;
  bf2 = bf2 * bf2 - (3.0f * bassBuffer.stdDev);
  if (bf2 < 0.0f) bf2 = 0.0f;
  bf2 = sqrtf(bf2);

  float beatFeature = (bf2 > bf1) ? (bf2 * 0.7f + bf1 * 0.3f) : bf1;
  if (bf2 > beatFeature) beatFeature = bf2;
  // Only boost on frame.isPeak if bass is significant (avoiding snare/hi-hat double triggers)
  if (frame.isPeak && bassRatio >= 0.20f) {
    if (beatFeature < 20.0f) beatFeature = 20.0f;
    else beatFeature *= 1.3f;
  }
  if (beatFeature > 100.0f) beatFeature = 100.0f;

  beatFeatureBuffer.add(beatFeature);
  beatFeatureBuffer.calculate();

  // 5. Phase-Locked Beat Expectation & Dynamic Adaptive Thresholding
  uint32_t durationSinceLastBeat = (lastBeatTimestamp > 0) ? (now - lastBeatTimestamp) : 10000;

  if (durationBetweenBeats > 0) {
    uint32_t lastBeatRef = ((now - lastBpmUpdateTime < 10 * durationBetweenBeats))
                           ? lastBpmUpdateTime : lastBeatTimestamp;
    uint32_t nextExpectedBeat = lastBeatRef + durationBetweenBeats;
    int32_t deltaNext = (int32_t)(nextExpectedBeat - now);
    missedBeats = 0;

    if (deltaNext < 0) {
      missedBeats = 2 + (uint32_t)((now - nextExpectedBeat) / durationBetweenBeats);
      nextExpectedBeat = lastBeatRef + (missedBeats * durationBetweenBeats);
      deltaNext = (int32_t)(nextExpectedBeat - now);
    }
    if (deltaNext > (int32_t)(durationBetweenBeats / 2)) {
      deltaNext = (int32_t)durationBetweenBeats - deltaNext;
    }
    if (deltaNext < 0) deltaNext = -deltaNext;

    expectedBeatProb = (float)(durationBetweenBeats - deltaNext) / (float)durationBetweenBeats;
    expectedBeatProb = powf(expectedBeatProb, 5.0f);
    if (expectedBeatProb > 1.0f) expectedBeatProb = 1.0f;
    if (expectedBeatProb < 0.0f) expectedBeatProb = 0.0f;
  } else {
    expectedBeatProb = 0.0f;
    missedBeats = 0;
  }

  const float BEAT_THRESH_MAX = 40.0f;
  const float BEAT_THRESH_DEF = 16.0f;
  const float BEAT_THRESH_MIN = 8.0f;
  const uint32_t MIN_BEAT_DELAY = 300; // Max 200 BPM (300ms) - rejects 8th-note double-triggers for <= 150 BPM

  float beatThresh = BEAT_THRESH_MAX;
  bool beatProbActive = (beatFeatureBuffer.rms > 2.0f);
  bool isOffbeat = (durationBetweenBeats > 0 && durationSinceLastBeat < (uint32_t)(durationBetweenBeats * 0.72f));
  bool enoughDelay = (durationSinceLastBeat > MIN_BEAT_DELAY && !isOffbeat);

  if (beatProbActive && enoughDelay) {
    float recency = 1.0f - ((float)MIN_BEAT_DELAY / (float)durationSinceLastBeat);
    if (recency < 0.0f) recency = 0.0f;
    if (recency > 1.0f) recency = 1.0f;
    beatThresh -= 2.0f * (BEAT_THRESH_MAX - BEAT_THRESH_DEF) * recency;
    if (beatThresh < BEAT_THRESH_DEF) beatThresh = BEAT_THRESH_DEF;
    beatThresh -= 2.0f * (BEAT_THRESH_DEF - BEAT_THRESH_MIN) * expectedBeatProb;
    if (beatThresh < BEAT_THRESH_MIN) beatThresh = BEAT_THRESH_MIN;
  }

  bool beatDetected = ((beatFeature > beatThresh || (frame.isPeak && bassRatio >= 0.20f && beatFeature > BEAT_THRESH_MIN)) && enoughDelay);

  if (beatDetected) {
    beatTimes[beatIdx] = now;
    beatIdx = (beatIdx + 1) % MAX_BEATS;
    if (beatCount < MAX_BEATS) beatCount++;

    if (lastBeatTimestamp > 0 && durationSinceLastBeat <= 1500) {
      beatDelayBuffer.add((float)(durationSinceLastBeat / 10)); // stored in 10ms units
      beatDelayBuffer.calculate();
      if (beatDelayBuffer.count >= 4 && beatDelayBuffer.stdDev < 14.0f) {
        float avgIntervalMs = beatDelayBuffer.mean * 10.0f;
        if (avgIntervalMs >= 250.0f && avgIntervalMs <= 1000.0f) {
          uint16_t curBpm = (uint16_t)roundf(60000.0f / avgIntervalMs);
          // Fold into primary human musical range (80 - 160 BPM)
          while (curBpm > 160) curBpm /= 2;
          while (curBpm < 80) curBpm *= 2;

          if (curBpm >= 60 && curBpm <= 200) {
            if (beatsPerMinute == 0) {
              rawBpm = (float)curBpm;
              beatsPerMinute = curBpm;
              bpmLockCount = 1;
            } else {
              float diff = fabsf((float)curBpm - rawBpm);
              if (diff <= 8.0f) {
                // Exponential smoothing to eliminate discrete 20ms frame jitter
                rawBpm = (rawBpm * 0.90f) + ((float)curBpm * 0.10f);
                if (bpmLockCount < 30) bpmLockCount++;
              } else {
                // Divergence: slowly reduce lock confidence
                if (bpmLockCount > 2) bpmLockCount -= 2;
                else {
                  rawBpm = (float)curBpm;
                  bpmLockCount = 1;
                }
              }

              // Only update public integer BPM once smoothed and locked
              if (bpmLockCount >= 6) {
                uint16_t targetBpm = (uint16_t)roundf(rawBpm);
                if (targetBpm != beatsPerMinute) {
                  beatsPerMinute = targetBpm;
                }
              } else {
                beatsPerMinute = (uint16_t)roundf(rawBpm);
              }
            }

            durationBetweenBeats = (uint32_t)roundf(60000.0f / (float)beatsPerMinute);
            durationBetweenBars = durationBetweenBeats * 4;
            durationBetweenPhrases = durationBetweenBars * 8; // 8 bars per phrase (32 beats)
            lastBpmUpdateTime = now;
            beatRegularity = 0.90f;
          }
        }
      }
    }

    // Inter-beat regularity from circular history
    if (beatCount >= 4) {
      float intervals[MAX_BEATS - 1];
      size_t validIntervals = 0;
      float sumIntervals = 0.0f;

      for (size_t i = 1; i < beatCount; i++) {
        size_t currIdx = (beatIdx + MAX_BEATS - i) % MAX_BEATS;
        size_t prevIdx = (beatIdx + MAX_BEATS - i - 1) % MAX_BEATS;
        uint32_t dt = beatTimes[currIdx] - beatTimes[prevIdx];
        if (dt >= 250 && dt <= 1200) {
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
        float cv = stddev / (mean + 1.0f);

        if (cv < 0.25f) {
          if (beatRegularity < 0.85f) beatRegularity = 0.85f;
          if (beatsPerMinute == 0 && mean > 0.0f) {
            uint16_t b = (uint16_t)roundf(60000.0f / mean);
            while (b > 160) b /= 2;
            while (b < 80) b *= 2;
            beatsPerMinute = b;
            rawBpm = (float)b;
            bpmLockCount = 1;
            durationBetweenBeats = (uint32_t)roundf(60000.0f / (float)b);
            durationBetweenBars = durationBetweenBeats * 4;
            durationBetweenPhrases = durationBetweenBars * 8; // 8 bars per phrase (32 beats)
            lastBpmUpdateTime = now;
          }
        } else if (cv < 0.40f) {
          if (beatRegularity < 0.55f) beatRegularity = 0.55f;
        }
      }
    }

    lastBeatTimestamp = now;
    durationSinceLastBeat = 0;
  } else {
    beatRegularity *= 0.995f;
    if (durationSinceLastBeat > 3000) {
      beatCount = 0;
      beatDelayBuffer.reset();
      rawBpm = 0.0f;
      bpmLockCount = 0;
    }
    if (durationSinceLastBeat > 10000 || presence == SoundPresence::QUIET) {
      beatsPerMinute = 0;
      rawBpm = 0.0f;
      bpmLockCount = 0;
      durationBetweenBeats = 0;
      durationBetweenBars = 0;
      durationBetweenPhrases = 0;
    }
  }

  // 6. Drop Detection & 4/4 Phrase Grid Tracking
  float dropFeature = bassBuffer.getLatest() - bassBuffer.mean;
  dropFeature -= (1.25f * bassBuffer.stdDev);
  if (dropFeature < 0.0f) dropFeature = 0.0f;
  dropFeature = dropFeature * dropFeature - (2.0f * bassBuffer.stdDev);
  if (dropFeature < 0.0f) dropFeature = 0.0f;
  dropFeature = sqrtf(dropFeature);
  dropFeature += (spreadBuffer.getLatest() - spreadBuffer.mean) * 0.5f;

  uint32_t durationSinceLastDrop = (lastDropTimestamp > 0) ? (now - lastDropTimestamp) : 999999;
  float dropThresh = 4.0f * bassBuffer.stdDev;

  if (lastPhraseChangeMs > 0 && durationBetweenPhrases > 0) {
    uint32_t timeSinceChange = (now >= lastPhraseChangeMs) ? (now - lastPhraseChangeMs) : 0;
    uint32_t phraseOffset = timeSinceChange % durationBetweenPhrases;
    uint32_t distToBoundary = (phraseOffset < durationBetweenPhrases / 2)
                              ? phraseOffset
                              : (durationBetweenPhrases - phraseOffset);
    if (distToBoundary <= durationBetweenBars) {
      // Near expected 8-bar phrase boundary: lower drop threshold (increase transition sensitivity)
      float factor = 1.0f - ((float)distToBoundary / (float)durationBetweenBars);
      dropThresh *= (1.0f - 0.35f * factor);
    } else {
      // Mid-phrase: increase threshold to avoid mid-phrase spurious drops
      dropThresh *= 1.35f;
    }
  } else if (lastExpectedPhraseStart > 0 && durationBetweenPhrases > 0) {
    int32_t deltaPhrase = (int32_t)(now - (lastExpectedPhraseStart + durationBetweenPhrases));
    float phraseFactor = (500.0f - fabsf((float)deltaPhrase)) / 500.0f;
    if (phraseFactor < 0.0f) phraseFactor = 0.0f;
    if (phraseFactor > 1.0f) phraseFactor = 1.0f;
    dropThresh *= (1.0f - 0.25f * phraseFactor);
  }

  if (soundEnergy == SoundEnergy::CHORUS) {
    dropThresh *= 2.0f;
  } else if (soundEnergy < SoundEnergy::VERSE) {
    dropThresh *= 0.75f;
  }

  if (dropThresh < 15.0f) dropThresh = 15.0f;
  if (dropFeature > dropThresh && durationSinceLastDrop > 4000) {
    lastDropTimestamp = now;
    durationSinceLastDrop = 0;
  }

  if (lastDropTimestamp > 0 && durationBetweenBeats > 0) {
    if (lastExpectedPhraseStart == 0 || lastDropTimestamp == now) {
      lastExpectedPhraseStart = lastDropTimestamp;
    }
    uint32_t durSincePhrase = now - lastExpectedPhraseStart;
    if (durSincePhrase >= durationBetweenPhrases) {
      durSincePhrase %= durationBetweenPhrases;
    }
    lastExpectedPhraseStart = now - durSincePhrase;

    uint32_t beatsSinceDrop = (now - lastDropTimestamp) / durationBetweenBeats;
    expectedBeatIndex = beatsSinceDrop % 4;
    expectedBarIndex = (beatsSinceDrop / 4) % 8; // 8 bars per phrase
    expectedPhraseIndex = (beatsSinceDrop / 32);  // 32 beats per phrase
  }

  // 7. Musical Evidence & Confidence Filter
  bool hasMusicalEvidence = false;
  float instantConfidence = 0.0f;

  if (beatRegularity > 0.50f && (now - lastBeatTimestamp <= 3000)) {
    hasMusicalEvidence = true;
    instantConfidence = beatRegularity;
  } else if (bassRatio > 0.35f && currentLevel > (noiseFloor + 5.0f)) {
    hasMusicalEvidence = true;
    instantConfidence = 0.75f;
  }

  if (hasMusicalEvidence) {
    lastMusicEvidenceTime = now;
    musicConfidence = (musicConfidence * 0.85f) + (instantConfidence * 0.15f);
    if (musicConfidence > 1.0f) musicConfidence = 1.0f;
  } else {
    musicConfidence *= 0.999f;
  }

  // 8. Acoustic State Machine Transitions with Hangover Protection
  if (semanticState == AudioSemanticState::MUSIC) {
    bool hangoverActive = (lastMusicEvidenceTime > 0 && (now - lastMusicEvidenceTime <= config.musicHoldMs));
    if (!hangoverActive) {
      if (presence == SoundPresence::SOUND) {
        semanticState = AudioSemanticState::AMBIENT_NOISE;
        soundEnergy = SoundEnergy::NOISE;
      } else {
        semanticState = AudioSemanticState::SILENCE;
        soundEnergy = SoundEnergy::SILENCE;
      }
      phraseState = MusicPhrase::VERSE;
    }
  } else {
    if (musicConfidence >= 0.60f || (beatRegularity >= 0.70f && currentLevel > noiseFloor + 5.0f) || (beatsPerMinute > 0 && currentLevel > noiseFloor + 4.0f)) {
      semanticState = AudioSemanticState::MUSIC;
      lastMusicEvidenceTime = now;
      if (musicConfidence < 0.70f) musicConfidence = 0.70f;
    } else if (presence == SoundPresence::SOUND) {
      semanticState = AudioSemanticState::AMBIENT_NOISE;
      soundEnergy = SoundEnergy::NOISE;
    } else {
      semanticState = AudioSemanticState::SILENCE;
      soundEnergy = SoundEnergy::SILENCE;
    }
  }

  // 9. Phrasing & SoundEnergy State Machine (Rave-Totem Algorithm)
  if (semanticState == AudioSemanticState::MUSIC) {
    shortBassEnergy = (shortBassEnergy * 0.92f) + (bassEnergy * 0.08f);
    longBassEnergy = (longBassEnergy * 0.99f) + (bassEnergy * 0.01f);
    float bassTrend = (longBassEnergy > 2.0f) ? (shortBassEnergy / longBassEnergy) : 1.0f;

    uint32_t phraseDur = (durationBetweenPhrases > 0) ? durationBetweenPhrases : 16000;
    uint32_t barDur = (durationBetweenBars > 0) ? durationBetweenBars : 2000;

    uint32_t timeSincePhraseChange = (lastPhraseChangeMs > 0 && now >= lastPhraseChangeMs)
                                     ? (now - lastPhraseChangeMs)
                                     : phraseDur;
    uint32_t phraseOffset = (durationBetweenPhrases > 0) ? (timeSincePhraseChange % durationBetweenPhrases) : 0;
    uint32_t distToBoundary = (phraseOffset < phraseDur / 2)
                              ? phraseOffset
                              : (phraseDur - phraseOffset);
    bool nearPhraseBoundary = (durationBetweenBars > 0) && (distToBoundary <= durationBetweenBars);

    bool dropInLastPhrase = (now - lastDropTimestamp <= phraseDur);
    bool beatInLastPhrase = (now - lastBeatTimestamp <= phraseDur);
    bool beatInLastBar = (now - lastBeatTimestamp <= barDur);
    bool constantBeats = (beatsPerMinute > 0) && (now - lastBpmUpdateTime <= 2 * barDur);
    uint32_t barsWithoutBeats = (durationBetweenBars > 0) ? (durationSinceLastBeat / durationBetweenBars) : (durationSinceLastBeat / 2000);
    uint32_t barsSinceEnergyUpdate = (barDur > 0) ? ((now - lastEnergyUpdateTime) / barDur) : 0;

    SoundEnergy updatedEnergy = soundEnergy;

    if (dropInLastPhrase && (beatInLastPhrase || beatDetected)) {
      updatedEnergy = SoundEnergy::CHORUS;
    } else if (barsWithoutBeats <= 2 && (constantBeats || beatInLastBar || bassRatio > 0.25f)) {
      if (soundEnergy == SoundEnergy::CHORUS) {
        // User rule: If switched to chorus, do NOT change back to verse for at least 8 bars (durationBetweenPhrases / 32 beats)
        if (timeSincePhraseChange < phraseDur) {
          updatedEnergy = SoundEnergy::CHORUS;
        } else if (nearPhraseBoundary && (bassTrend < 0.90f || !dropInLastPhrase)) {
          updatedEnergy = SoundEnergy::VERSE;
        } else {
          updatedEnergy = SoundEnergy::CHORUS;
        }
      } else {
        updatedEnergy = SoundEnergy::VERSE;
      }
    } else {
      if (soundEnergy == SoundEnergy::CHORUS || soundEnergy == SoundEnergy::BREAKDOWN || soundEnergy == SoundEnergy::OUTRO || bassTrend < 0.50f || (bassRatio < 0.15f && currentLevel > noiseFloor + 2.0f) || currentLevel < noiseFloor + 3.0f) {
        if ((soundEnergy == SoundEnergy::BREAKDOWN || soundEnergy == SoundEnergy::OUTRO) && (barsSinceEnergyUpdate >= 8 || currentLevel < noiseFloor + 4.0f)) {
          updatedEnergy = SoundEnergy::OUTRO;
        } else {
          updatedEnergy = SoundEnergy::BREAKDOWN;
        }
      } else if (bassTrend > 1.25f || (now - lastDropTimestamp > phraseDur && barsWithoutBeats <= 4)) {
        updatedEnergy = SoundEnergy::BUILDUP;
      } else {
        updatedEnergy = SoundEnergy::INTRO;
      }
    }

    if (now - lastEnergyUpdateTime >= 1000) {
      if (updatedEnergy > soundEnergy) {
        if (soundEnergy == SoundEnergy::OUTRO) {
          if (dropInLastPhrase) soundEnergy = SoundEnergy::CHORUS;
          else if (barsSinceEnergyUpdate >= 2) soundEnergy = SoundEnergy::VERSE;
        } else if (soundEnergy == SoundEnergy::INTRO) {
          if (dropInLastPhrase) soundEnergy = SoundEnergy::CHORUS;
          else if (barsSinceEnergyUpdate >= 2) soundEnergy = SoundEnergy::VERSE;
        } else if (soundEnergy == SoundEnergy::VERSE) {
          if (dropInLastPhrase) soundEnergy = SoundEnergy::CHORUS;
          else if (bassTrend > 1.25f) soundEnergy = SoundEnergy::BUILDUP;
        } else if (soundEnergy == SoundEnergy::BUILDUP) {
          if (dropInLastPhrase || (bassTrend > 1.4f && (beatInLastBar || beatDetected))) {
            soundEnergy = SoundEnergy::CHORUS;
          }
        } else {
          soundEnergy = updatedEnergy;
        }
        lastEnergyUpdateTime = now;
      } else if (updatedEnergy < soundEnergy) {
        if (soundEnergy == SoundEnergy::CHORUS) {
          if (timeSincePhraseChange < phraseDur) {
            // Chorus locked for at least 8 bars (durationBetweenPhrases).
            // Only allow transition down if audio dropped to breakdown level!
            if (updatedEnergy == SoundEnergy::BREAKDOWN || updatedEnergy == SoundEnergy::OUTRO) {
              soundEnergy = SoundEnergy::BREAKDOWN;
              lastEnergyUpdateTime = now;
            }
          } else {
            if (beatInLastBar) soundEnergy = SoundEnergy::VERSE;
            else soundEnergy = SoundEnergy::BREAKDOWN;
            lastEnergyUpdateTime = now;
          }
        } else if (soundEnergy == SoundEnergy::VERSE) {
          if (bassTrend < 0.50f || barsWithoutBeats >= 2 || currentLevel < noiseFloor + 3.0f) {
            soundEnergy = SoundEnergy::BREAKDOWN;
            lastEnergyUpdateTime = now;
          }
        } else if (soundEnergy == SoundEnergy::BUILDUP) {
          if (barsSinceEnergyUpdate >= 4 || currentLevel < noiseFloor + 3.0f) {
            soundEnergy = SoundEnergy::BREAKDOWN;
            lastEnergyUpdateTime = now;
          }
        } else if (soundEnergy == SoundEnergy::BREAKDOWN) {
          if (barsSinceEnergyUpdate >= 8 || currentLevel < noiseFloor + 4.0f) {
            soundEnergy = SoundEnergy::OUTRO;
            lastEnergyUpdateTime = now;
          } else if (barsSinceEnergyUpdate >= 4) {
            soundEnergy = SoundEnergy::INTRO;
            lastEnergyUpdateTime = now;
          }
        } else {
          soundEnergy = updatedEnergy;
          lastEnergyUpdateTime = now;
        }
      }
    }

    MusicPhrase prevPhrase = phraseState;
    switch (soundEnergy) {
      case SoundEnergy::CHORUS:    phraseState = MusicPhrase::CHORUS; break;
      case SoundEnergy::BUILDUP:   phraseState = MusicPhrase::BUILDUP; break;
      case SoundEnergy::BREAKDOWN: phraseState = MusicPhrase::BREAKDOWN; break;
      case SoundEnergy::OUTRO:     phraseState = MusicPhrase::OUTRO; break;
      case SoundEnergy::INTRO:     phraseState = MusicPhrase::INTRO; break;
      case SoundEnergy::VERSE:     phraseState = MusicPhrase::VERSE; break;
      default:                     phraseState = MusicPhrase::VERSE; break;
    }
    if (phraseState != prevPhrase) {
      lastPhraseChangeMs = now;
      lastExpectedPhraseStart = now;
    }
  } else {
    phraseState = MusicPhrase::VERSE;
  }
}
