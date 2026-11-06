// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#include "classifier.h"
#include <math.h>
#include <stdio.h>

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
  phraseState = MusicPhrase::UNKNOWN;
  soundEnergy = SoundEnergy::SILENCE;
  musicConfidence = 0.0f;
  lastPhraseChangeMs = 0;
  lastMusicEvidenceTime = 0;
  lastFrameTimeMs = 0;
  lastEnergyUpdateTime = 0;

  loudnessBuffer.reset();
  bassBuffer.reset();
  lowFreqBuffer.reset();
  spreadBuffer.reset();
  beatFeatureBuffer.reset();
  beatDelayBuffer.reset();
  beatProbability = 0.0f;

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
  prevBassEnergy = 0.0f;
  shortBassEnergy = 0.0f;
  longBassEnergy = 0.0f;

  tentativePrevEnergy = SoundEnergy::SILENCE;
  tentativePrevPhrase = MusicPhrase::UNKNOWN;
  tentativePrevPhraseStartMs = 0;
  tentativeTransitionMs = 0;
  isTentativeTransition = false;
  tentativeBeatsSeen = 0;
}

void AudioClassifier::setConfig(const ClassifierConfig& newConfig) {
  config = newConfig;
}

const char* AudioClassifier::getSemanticStateStr() const {
  switch (semanticState) {
    case AudioSemanticState::SILENCE:       return "Silence";
    case AudioSemanticState::AMBIENT_NOISE: return "Ambient Noise";
    case AudioSemanticState::MUSIC:         return "Music";
    default:                                return "Unknown";
  }
}

const char* AudioClassifier::getPhraseStateStr() const {
  if (semanticState != AudioSemanticState::MUSIC) {
    return "Unknown";
  }
  switch (phraseState) {
    case MusicPhrase::VERSE:     return "Verse";
    case MusicPhrase::INTRO:     return "Intro";
    case MusicPhrase::BUILDUP:   return "Buildup";
    case MusicPhrase::CHORUS:    return "Chorus";
    case MusicPhrase::BREAKDOWN: return "Breakdown";
    case MusicPhrase::OUTRO:     return "Outro";
    case MusicPhrase::UNKNOWN:
    default:                     return "Unknown";
  }
}

const char* AudioClassifier::getSoundEnergyStr() const {
  if (semanticState != AudioSemanticState::MUSIC) {
    return "silence";
  }
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
  if (now == lastFrameTimeMs && lastFrameTimeMs > 0) {
    return; // de-duplicate identical packets
  }
  lastFrameTimeMs = now;

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
  // Enforce biconditional rule: Audio Type == Silence <===> Audio Detected == Clear
  if (presence == SoundPresence::QUIET) {
    semanticState = AudioSemanticState::SILENCE;
    phraseState = MusicPhrase::UNKNOWN;
    soundEnergy = SoundEnergy::SILENCE;
    musicConfidence = 0.0f;
    beatProbability = 0.0f;
  } else {
    updateMusicAndPhrasing(frame);
    if (presence == SoundPresence::QUIET) {
      semanticState = AudioSemanticState::SILENCE;
      phraseState = MusicPhrase::UNKNOWN;
      soundEnergy = SoundEnergy::SILENCE;
      musicConfidence = 0.0f;
      beatProbability = 0.0f;
    } else if (semanticState == AudioSemanticState::SILENCE) {
      semanticState = AudioSemanticState::AMBIENT_NOISE;
      phraseState = MusicPhrase::UNKNOWN;
      soundEnergy = SoundEnergy::SILENCE;
    }
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
      bool hangoverActive = (semanticState == AudioSemanticState::MUSIC &&
                             lastMusicEvidenceTime > 0 &&
                             (now - lastMusicEvidenceTime <= config.musicHoldMs));
      if (hangoverActive) {
        isPendingOff = false;
      } else {
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
}

void AudioClassifier::updateMusicAndPhrasing(const AudioFrame& frame) {
  if (!config.experimentalEnabled) {
    semanticState = (presence == SoundPresence::SOUND) ? AudioSemanticState::AMBIENT_NOISE : AudioSemanticState::SILENCE;
    phraseState = MusicPhrase::UNKNOWN;
    soundEnergy = SoundEnergy::SILENCE;
    musicConfidence = 0.0f;
    beatsPerMinute = 0;
    beatProbability = 0.0f;
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

  // Positive Bass Spectral Flux (kick drum transient onset)
  float rawBass = (float)frame.bands[0] * 1.3f + (float)frame.bands[1] * 0.7f;
  float bassFlux = 0.0f;
  if (prevBassEnergy > 0.0f && rawBass > prevBassEnergy) {
    bassFlux = rawBass - prevBassEnergy;
  }
  prevBassEnergy = rawBass;

  // 2. Feed 1.5s Rolling Feature Buffers (Rave-Totem architecture)
  loudnessBuffer.add(frame.levelPercent);
  loudnessBuffer.calculate();
  bassBuffer.add(bassPercent);
  bassBuffer.calculate();
  float lowFreqVal = fmaxf((float)frame.bands[0], (float)frame.bands[1]);
  lowFreqBuffer.add(lowFreqVal);
  lowFreqBuffer.calculate();
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
  float bf2 = bassBuffer.getLatest() - bassBuffer.mean;
  bf2 -= (0.5f * bassBuffer.stdDev);
  if (bf2 < 0.0f) bf2 = 0.0f;
  bf2 = bf2 * bf2 - (2.0f * bassBuffer.stdDev);
  if (bf2 < 0.0f) bf2 = 0.0f;
  bf2 = sqrtf(bf2);

  // Combine bass statistical acceleration with instantaneous bass flux
  float beatFeature = (bf2 * 0.3f) + (bassFlux * 0.7f);

  // Snare/hi-hat suppression: if volume is low or bass is absent, reject non-beat transients
  float effRawVol = fmaxf(frame.rawVolume, fmaxf(frame.smoothedVolume, frame.levelPercent * 2.55f));
  
  // Middleground bass onset requirement:
  // Accept punchy sub-bass (band 0 >= 35) OR punchy mid-bass (band 1 >= 40) OR combined bass (band 0 + band 1 >= 60)
  bool hasBass = (frame.bands[0] >= 35 || frame.bands[1] >= 40 || (frame.bands[0] + frame.bands[1]) >= 60) && 
                 (bassRatio >= 0.22f || (bassFlux >= 20.0f && bassRatio >= 0.18f));

  // Mid-heavy check: compare average mid band against average bass band
  float midAvg = (float)(frame.bands[2] + frame.bands[3] + frame.bands[4]) / 3.0f;
  float bassAvg = (float)(frame.bands[0] + frame.bands[1]) / 2.0f;
  bool isMidOverpowering = (midAvg > bassAvg * 1.20f);

  if (!hasBass || isMidOverpowering) {
    beatFeature = 0.0f;
  } else if (effRawVol < 25.0f) {
    beatFeature *= 0.30f;
  } else if (spread > bassPercent * 1.2f && bassRatio < 0.35f) {
    beatFeature *= 0.20f;
  } else if (frame.isPeak && (frame.majorPeakHz <= 140.0f || frame.majorPeakHz == 0.0f)) {
    // Only boost if there is already an active bass transient onset; do NOT synthesize a beat from generic peaks
    if (beatFeature > 8.0f) beatFeature *= 1.25f;
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
  const float BEAT_THRESH_DEF = 18.0f;
  const float BEAT_THRESH_MIN = 12.0f;
  const uint32_t MIN_BEAT_DELAY = 280;

  float beatThresh = BEAT_THRESH_MAX;
  bool isOffbeat = (durationBetweenBeats > 0 && durationSinceLastBeat < (uint32_t)(durationBetweenBeats * 0.72f));
  bool enoughDelay = (durationSinceLastBeat >= MIN_BEAT_DELAY && !isOffbeat);

  if (enoughDelay) {
    float recency = (durationSinceLastBeat > MIN_BEAT_DELAY)
                    ? ((float)(durationSinceLastBeat - MIN_BEAT_DELAY) / 120.0f)
                    : 0.0f;
    if (recency > 1.0f) recency = 1.0f;
    beatThresh -= (BEAT_THRESH_MAX - BEAT_THRESH_DEF) * recency;
    if (beatThresh < BEAT_THRESH_DEF) beatThresh = BEAT_THRESH_DEF;
    beatThresh -= (BEAT_THRESH_DEF - BEAT_THRESH_MIN) * expectedBeatProb;
    if (beatThresh < BEAT_THRESH_MIN) beatThresh = BEAT_THRESH_MIN;
  }

  // Require low-frequency variance in buffer to avoid false beats in static pad/drone sections
  float lowSpan = lowFreqBuffer.maxVal - lowFreqBuffer.minVal;
  bool hasLowFreqVariance = (lowFreqBuffer.count < 8) || 
                            ((lowFreqBuffer.stdDev >= 3.0f && lowSpan >= 12.0f) || (lowFreqBuffer.stdDev >= 4.5f));

  bool beatDetected = hasBass && hasLowFreqVariance && (beatFeature > beatThresh && enoughDelay);

  if (beatDetected) {
    beatTimes[beatIdx] = now;
    beatIdx = (beatIdx + 1) % MAX_BEATS;
    if (beatCount < MAX_BEATS) beatCount++;

    bool inBeatPhrase = (semanticState == AudioSemanticState::MUSIC || beatProbability >= 0.15f || beatRegularity >= 0.50f) &&
                        (phraseState == MusicPhrase::VERSE || phraseState == MusicPhrase::CHORUS || phraseState == MusicPhrase::BUILDUP || phraseState == MusicPhrase::INTRO);
    bool hasRecentBpm = (beatsPerMinute > 0) && (durationBetweenBeats > 0) && (now - lastBpmUpdateTime <= 8 * durationBetweenBeats);
    if ((beatProbability >= 0.15f || hasRecentBpm || (beatsPerMinute == 0 && beatCount >= 2)) && inBeatPhrase) {
      if (lastBeatTimestamp > 0 && durationSinceLastBeat <= 2000) {
        // Skipped-beat normalization:
        // If beats were skipped (breaks, syncopation), divide duration by elapsed beats
        uint32_t numBeats = (durationBetweenBeats > 0) ? (uint32_t)roundf((float)durationSinceLastBeat / (float)durationBetweenBeats) : 1;
        if (numBeats == 0) numBeats = 1;
        uint32_t normIntervalMs = durationSinceLastBeat / numBeats;

        if (normIntervalMs >= 250 && normIntervalMs <= 750) {
          beatDelayBuffer.add((float)(normIntervalMs / 10)); // stored in 10ms units
          beatDelayBuffer.calculate();
          if (beatDelayBuffer.count >= 4 && beatDelayBuffer.stdDev < 14.0f) {
            float avgIntervalMs = beatDelayBuffer.mean * 10.0f;
            if (avgIntervalMs >= 250.0f && avgIntervalMs <= 1000.0f) {
              uint16_t curBpm = (uint16_t)roundf(60000.0f / avgIntervalMs);

              if (curBpm >= 40 && curBpm <= 400) {
                if (rawBpm <= 0.0f || beatsPerMinute == 0) {
                  // Initial folding into standard human musical tempo octave [85 - 175 BPM]
                  while (curBpm > 175) curBpm /= 2;
                  while (curBpm < 85) curBpm *= 2;
                  rawBpm = (float)curBpm;
                  beatsPerMinute = curBpm;
                  bpmLockCount = 3;
                } else {
                  // Harmonic matching against established tempo (rawBpm)
                  // Test fundamental (1x), octave above (2x), and octave below (0.5x)
                  float cand1 = (float)curBpm;
                  float cand2 = (float)curBpm * 2.0f;
                  float candHalf = (float)curBpm * 0.5f;

                  float diff1 = fabsf(cand1 - rawBpm);
                  float diff2 = fabsf(cand2 - rawBpm);
                  float diffHalf = fabsf(candHalf - rawBpm);

                  float bestCand = cand1;
                  float minDiff = diff1;

                  if (diff2 < minDiff) {
                    minDiff = diff2;
                    bestCand = cand2;
                  }
                  if (diffHalf < minDiff) {
                    minDiff = diffHalf;
                    bestCand = candHalf;
                  }

                  // Permissible tempo tracking tolerance: up to 12% or 10 BPM
                  float maxDiff = fmaxf(10.0f, rawBpm * 0.12f);

                  if (minDiff <= maxDiff) {
                    // Harmonic match: smooth rawBpm towards the matched harmonic (faster during initial lock, slower when settled)
                    float alpha = (bpmLockCount < 8) ? 0.35f : 0.10f;
                    rawBpm = (rawBpm * (1.0f - alpha)) + (bestCand * alpha);
                    if (bpmLockCount < 30) bpmLockCount++;
                  } else {
                    // Divergence: slowly reduce lock confidence
                    if (bpmLockCount > 1) {
                      bpmLockCount -= 1;
                    } else {
                      // Lock lost: fold bestCand into [85 - 175] and reset
                      while (bestCand > 175.0f) bestCand *= 0.5f;
                      while (bestCand < 85.0f) bestCand *= 2.0f;
                      rawBpm = bestCand;
                      bpmLockCount = 1;
                    }
                  }

                  // Stable integer BPM output
                  if (bpmLockCount >= 3) {
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
      }
    } else {
      beatDelayBuffer.reset();
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
    }
    if (semanticState != AudioSemanticState::MUSIC) {
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

  float highEnergy = 0.0f;
  for (size_t i = 8; i < NUM_AUDIO_BANDS; i++) {
    highEnergy += frame.bands[i];
  }
  float b01 = (float)(frame.bands[0] + frame.bands[1]);
  float b23 = (float)(frame.bands[2] + frame.bands[3]);

  if (beatRegularity > 0.50f && (now - lastBeatTimestamp <= 3000)) {
    hasMusicalEvidence = true;
    instantConfidence = beatRegularity;
  } else if (((b01 >= b23 * 1.05f && b01 >= 10.0f) || (highEnergy >= 15.0f && b01 >= 20.0f)) && !isMidOverpowering && (currentLevel > (noiseFloor + 5.0f) || (frame.levelPercent > (noiseFloor + 4.0f) && b01 >= 30.0f))) {
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
      float threshold = config.quietThresholdPercent;
      float dynamicThreshold = noiseFloor + config.marginAboveFloorPercent;
      if (dynamicThreshold > threshold) threshold = dynamicThreshold;

      if (currentLevel > threshold) {
        semanticState = AudioSemanticState::AMBIENT_NOISE;
      } else {
        semanticState = AudioSemanticState::SILENCE;
        presence = SoundPresence::QUIET;
        isPendingOff = false;
      }
      soundEnergy = SoundEnergy::SILENCE;
      phraseState = MusicPhrase::UNKNOWN;
    }
  } else {
    if (musicConfidence >= 0.60f || (beatRegularity >= 0.70f && currentLevel > noiseFloor + 5.0f)) {
      semanticState = AudioSemanticState::MUSIC;
      lastMusicEvidenceTime = now;
      if (musicConfidence < 0.70f) musicConfidence = 0.70f;
    } else if (presence == SoundPresence::SOUND) {
      semanticState = AudioSemanticState::AMBIENT_NOISE;
      soundEnergy = SoundEnergy::SILENCE;
      phraseState = MusicPhrase::UNKNOWN;
    } else {
      semanticState = AudioSemanticState::SILENCE;
      soundEnergy = SoundEnergy::SILENCE;
      phraseState = MusicPhrase::UNKNOWN;
    }
  }

  // 9. Phrasing & SoundEnergy State Machine (Rave-Totem Algorithm)
  if (semanticState == AudioSemanticState::MUSIC) {
    if (soundEnergy == SoundEnergy::SILENCE) {
      soundEnergy = SoundEnergy::INTRO;
      phraseState = MusicPhrase::INTRO;
      shortBassEnergy = bassEnergy;
      longBassEnergy = bassEnergy;
      lastEnergyUpdateTime = now;
      lastPhraseChangeMs = now;
    }

    SoundEnergy energyBeforeUpdate = soundEnergy;
    MusicPhrase phraseBeforeUpdate = phraseState;

    shortBassEnergy = (shortBassEnergy * 0.92f) + (bassEnergy * 0.08f);
    longBassEnergy = (longBassEnergy * 0.99f) + (bassEnergy * 0.01f);
    float bassTrend = (longBassEnergy > 2.0f) ? (shortBassEnergy / longBassEnergy) : 1.0f;

    uint32_t phraseDur = (durationBetweenPhrases > 0) ? durationBetweenPhrases : 16000;
    uint32_t barDur = (durationBetweenBars > 0) ? durationBetweenBars : 2000;
    uint32_t beatDur = (durationBetweenBeats > 0) ? durationBetweenBeats : 500;
    uint32_t outlierWindowMs = (barDur > 0) ? (barDur + beatDur) : 2500;
    if (outlierWindowMs < 1500) outlierWindowMs = 1500;
    if (outlierWindowMs > 3500) outlierWindowMs = 3500;

    // Outlier Detection & Revert Evaluation:
    // If we just recently transitioned into a new phrase within 1-2 beats / ~1 bar,
    // evaluate whether the switch was caused by a momentary outlier (e.g. 1 soft bass hit
    // before the actual drop, or 1-2 beats missing in bar 8 for tension).
    if (isTentativeTransition) {
      uint32_t dtTentative = (now >= tentativeTransitionMs) ? (now - tentativeTransitionMs) : 0;
      if (beatDetected) {
        tentativeBeatsSeen++;
      }

      if (dtTentative > outlierWindowMs) {
        // Tentative observation window elapsed without being an outlier -> confirmed!
        isTentativeTransition = false;
      } else {
        // Case 1: Premature Upward Bump (e.g. switched to CHORUS or VERSE on 1 isolated hit)
        if (soundEnergy >= SoundEnergy::VERSE && tentativePrevEnergy < SoundEnergy::VERSE) {
          if (tentativeBeatsSeen >= 2 || (peak1sLevel >= 48.0f && bassBuffer.mean >= 20.0f)) {
            // Sustained consecutive beats or strong drop energy confirmed the transition!
            isTentativeTransition = false;
          } else {
            // Check if no follow-up beat arrived after 2 beats, and sustained energy collapsed back
            uint32_t gapSinceBeat = (lastBeatTimestamp > 0) ? (now - lastBeatTimestamp) : dtTentative;
            bool noFollowupBeat = (gapSinceBeat >= (uint32_t)(beatDur * 2.2f));
            bool energyDropped = (peak1sLevel < 42.0f && bassBuffer.mean < 15.0f);

            if (noFollowupBeat && energyDropped) {
              // Revert to previous phrase (e.g. BUILDUP) without applying the 8-bar chorus constraint!
              soundEnergy = tentativePrevEnergy;
              phraseState = tentativePrevPhrase;
              lastPhraseChangeMs = tentativePrevPhraseStartMs;
              lastExpectedPhraseStart = tentativePrevPhraseStartMs;
              lastEnergyUpdateTime = now;
              isTentativeTransition = false;
              energyBeforeUpdate = soundEnergy;
              phraseBeforeUpdate = phraseState;
            }
          }
        }
        // Case 2: Premature Downward Drop (e.g. dropped from CHORUS or VERSE due to 1-2 missing beats in bar 8)
        else if (soundEnergy < SoundEnergy::VERSE && tentativePrevEnergy >= SoundEnergy::VERSE) {
          // If a beat with bass resumes within the tentative window, it was just 1-2 missing beats in bar 8!
          bool bassResumed = (hasBass || bassRatio >= 0.25f || peak1sLevel >= 40.0f);
          if (beatDetected && bassResumed) {
            // Revert back to previous phrase (e.g. CHORUS or VERSE) and preserve phrase continuity!
            soundEnergy = tentativePrevEnergy;
            phraseState = tentativePrevPhrase;
            lastPhraseChangeMs = tentativePrevPhraseStartMs;
            lastExpectedPhraseStart = tentativePrevPhraseStartMs;
            lastEnergyUpdateTime = now;
            isTentativeTransition = false;
            energyBeforeUpdate = soundEnergy;
            phraseBeforeUpdate = phraseState;
          }
        }
      }
    }

    uint32_t timeSincePhraseChange = (lastPhraseChangeMs > 0 && now >= lastPhraseChangeMs)
                                     ? (now - lastPhraseChangeMs)
                                     : phraseDur;
    uint32_t phraseOffset = (durationBetweenPhrases > 0) ? (timeSincePhraseChange % durationBetweenPhrases) : 0;
    uint32_t distToBoundary = (phraseOffset < phraseDur / 2)
                              ? phraseOffset
                              : (phraseDur - phraseOffset);
    bool nearPhraseBoundary = (durationBetweenBars > 0) && (distToBoundary <= durationBetweenBars);

    bool dropInLastPhrase = (lastDropTimestamp > 0) && (now - lastDropTimestamp <= phraseDur);
    bool beatInLastPhrase = (lastBeatTimestamp > 0) && (now - lastBeatTimestamp <= phraseDur);
    bool beatInLastBar = (lastBeatTimestamp > 0) && (now - lastBeatTimestamp <= barDur);
    bool constantBeats = (beatsPerMinute > 0) && (now - lastBpmUpdateTime <= 2 * barDur);
    uint32_t barsWithoutBeats = (durationBetweenBars > 0) ? (durationSinceLastBeat / durationBetweenBars) : (durationSinceLastBeat / 2000);
    uint32_t barsSinceEnergyUpdate = (barDur > 0) ? ((now - lastEnergyUpdateTime) / barDur) : 0;

    bool isVolumeCollapsed = (currentLevel < (noiseFloor + 4.0f) && average1sLevel < (noiseFloor + 6.0f)) ||
                             (peak1sLevel < 12.0f && average1sLevel < 8.0f);
    bool noBeatsRecent = (durationSinceLastBeat > 1200 || barsWithoutBeats >= 1);
    bool hasActiveBeatGroove = (barsWithoutBeats == 0) && (durationSinceLastBeat <= (barDur * 5 / 4)) && (beatProbability >= 0.20f || constantBeats);

    SoundEnergy updatedEnergy = soundEnergy;

    if (isVolumeCollapsed && noBeatsRecent) {
      if (soundEnergy == SoundEnergy::OUTRO) {
        updatedEnergy = SoundEnergy::OUTRO;
      } else if (soundEnergy == SoundEnergy::BREAKDOWN && (barsSinceEnergyUpdate >= 8 || currentLevel < noiseFloor + 3.0f)) {
        updatedEnergy = SoundEnergy::OUTRO;
      } else {
        updatedEnergy = SoundEnergy::BREAKDOWN;
      }
    } else if (((peak1sLevel >= 48.0f || average1sLevel >= 36.0f) || (dropInLastPhrase && (peak1sLevel >= 42.0f || average1sLevel >= 32.0f))) && (beatInLastPhrase || beatDetected)) {
      updatedEnergy = SoundEnergy::CHORUS;
    } else if (hasActiveBeatGroove && (peak1sLevel >= 14.0f || average1sLevel >= 10.0f)) {
      if (soundEnergy == SoundEnergy::CHORUS) {
        // User rule: If switched to chorus, do NOT change back to verse for at least 8 bars (durationBetweenPhrases / 32 beats)
        // If this transition is tentative (evaluating outlier), do not lock in 8 bars yet!
        if (!isTentativeTransition && timeSincePhraseChange < phraseDur && peak1sLevel >= 38.0f) {
          updatedEnergy = SoundEnergy::CHORUS;
        } else if (nearPhraseBoundary && (!dropInLastPhrase || bassTrend < 0.90f) && (peak1sLevel < 52.0f)) {
          if (barsWithoutBeats == 0 && currentLevel >= 18.0f && bassEnergy >= 20.0f) {
            updatedEnergy = SoundEnergy::VERSE;
          } else {
            updatedEnergy = SoundEnergy::BREAKDOWN;
          }
        } else {
          updatedEnergy = SoundEnergy::CHORUS;
        }
      } else if (soundEnergy == SoundEnergy::INTRO && timeSincePhraseChange < phraseDur) {
        updatedEnergy = SoundEnergy::INTRO;
      } else {
        updatedEnergy = SoundEnergy::VERSE;
      }
    } else if ((soundEnergy == SoundEnergy::BREAKDOWN || soundEnergy == SoundEnergy::OUTRO || soundEnergy == SoundEnergy::VERSE) &&
               (peak1sLevel >= 16.0f) &&
               ((frame.bands[10] >= 35 && frame.bands[11] >= 45) || (bassTrend > 1.25f && barsWithoutBeats >= 1 && peak1sLevel >= 22.0f))) {
      updatedEnergy = SoundEnergy::BUILDUP;
    } else {
      if (soundEnergy == SoundEnergy::OUTRO) {
        updatedEnergy = SoundEnergy::OUTRO;
      } else if (soundEnergy == SoundEnergy::CHORUS || soundEnergy == SoundEnergy::BREAKDOWN || isVolumeCollapsed || noBeatsRecent || bassTrend < 0.50f || (bassRatio < 0.15f && currentLevel > noiseFloor + 2.0f)) {
        if (soundEnergy == SoundEnergy::BREAKDOWN && (barsSinceEnergyUpdate >= 8 || currentLevel < noiseFloor + 3.0f)) {
          updatedEnergy = SoundEnergy::OUTRO;
        } else {
          updatedEnergy = SoundEnergy::BREAKDOWN;
        }
      } else {
        updatedEnergy = SoundEnergy::INTRO;
      }
    }

    bool immediateBeatTransition = beatDetected && (updatedEnergy != soundEnergy);
    if ((now - lastEnergyUpdateTime >= 1000) || immediateBeatTransition) {
      if (updatedEnergy > soundEnergy) {
        SoundEnergy prevEnergy = soundEnergy;
        if (soundEnergy == SoundEnergy::OUTRO) {
          if (updatedEnergy == SoundEnergy::CHORUS || peak1sLevel >= 48.0f) soundEnergy = SoundEnergy::CHORUS;
          else if (updatedEnergy >= SoundEnergy::VERSE) soundEnergy = SoundEnergy::VERSE;
          else soundEnergy = updatedEnergy;
        } else if (soundEnergy == SoundEnergy::INTRO) {
          if (updatedEnergy == SoundEnergy::CHORUS || peak1sLevel >= 48.0f) soundEnergy = SoundEnergy::CHORUS;
          else if (updatedEnergy >= SoundEnergy::VERSE) soundEnergy = SoundEnergy::VERSE;
          else soundEnergy = updatedEnergy;
        } else if (soundEnergy == SoundEnergy::BREAKDOWN) {
          if (updatedEnergy == SoundEnergy::CHORUS || peak1sLevel >= 48.0f) soundEnergy = SoundEnergy::CHORUS;
          else if (updatedEnergy >= SoundEnergy::VERSE) soundEnergy = SoundEnergy::VERSE;
          else soundEnergy = updatedEnergy;
        } else if (soundEnergy == SoundEnergy::VERSE) {
          if (updatedEnergy == SoundEnergy::CHORUS || peak1sLevel >= 48.0f) soundEnergy = SoundEnergy::CHORUS;
          else if (bassTrend > 1.25f) soundEnergy = SoundEnergy::BUILDUP;
        } else if (soundEnergy == SoundEnergy::BUILDUP) {
          if (updatedEnergy == SoundEnergy::CHORUS || (dropInLastPhrase && (peak1sLevel >= 38.0f || average1sLevel >= 28.0f)) || peak1sLevel >= 48.0f) {
            soundEnergy = SoundEnergy::CHORUS;
          }
        } else {
          soundEnergy = updatedEnergy;
        }
        if (soundEnergy != prevEnergy) {
          lastEnergyUpdateTime = now;
        }
      } else if (updatedEnergy < soundEnergy) {
        if (soundEnergy == SoundEnergy::CHORUS) {
          if (!isTentativeTransition && timeSincePhraseChange < phraseDur && !isVolumeCollapsed) {
            // Chorus locked for at least 8 bars (durationBetweenPhrases).
            // Only allow transition down if audio dropped to breakdown level!
            if ((updatedEnergy == SoundEnergy::BREAKDOWN || updatedEnergy == SoundEnergy::OUTRO) && (isVolumeCollapsed || barsWithoutBeats >= 1)) {
              soundEnergy = SoundEnergy::BREAKDOWN;
              lastEnergyUpdateTime = now;
            }
          } else {
            if (updatedEnergy == SoundEnergy::VERSE && beatInLastBar && hasActiveBeatGroove) soundEnergy = SoundEnergy::VERSE;
            else soundEnergy = SoundEnergy::BREAKDOWN;
            lastEnergyUpdateTime = now;
          }
        } else if (soundEnergy == SoundEnergy::VERSE) {
          if (updatedEnergy == SoundEnergy::BUILDUP) {
            soundEnergy = SoundEnergy::BUILDUP;
            lastEnergyUpdateTime = now;
          } else if (updatedEnergy == SoundEnergy::BREAKDOWN || updatedEnergy == SoundEnergy::OUTRO ||
                     bassTrend < 0.50f || barsWithoutBeats >= 1 || isVolumeCollapsed) {
            soundEnergy = updatedEnergy;
            lastEnergyUpdateTime = now;
          }
        } else if (soundEnergy == SoundEnergy::BUILDUP) {
          if (barsSinceEnergyUpdate >= 16 || currentLevel < noiseFloor + 3.0f || isVolumeCollapsed) {
            soundEnergy = SoundEnergy::BREAKDOWN;
            lastEnergyUpdateTime = now;
          }
        } else if (soundEnergy == SoundEnergy::BREAKDOWN) {
          if (barsSinceEnergyUpdate >= 8 || currentLevel < noiseFloor + 3.0f) {
            soundEnergy = SoundEnergy::OUTRO;
            lastEnergyUpdateTime = now;
          }
        } else {
          soundEnergy = updatedEnergy;
          lastEnergyUpdateTime = now;
        }
      }
    }

    switch (soundEnergy) {
      case SoundEnergy::CHORUS:    phraseState = MusicPhrase::CHORUS; break;
      case SoundEnergy::BUILDUP:   phraseState = MusicPhrase::BUILDUP; break;
      case SoundEnergy::BREAKDOWN: phraseState = MusicPhrase::BREAKDOWN; break;
      case SoundEnergy::OUTRO:     phraseState = MusicPhrase::OUTRO; break;
      case SoundEnergy::INTRO:     phraseState = MusicPhrase::INTRO; break;
      case SoundEnergy::VERSE:     phraseState = MusicPhrase::VERSE; break;
      default:                     phraseState = MusicPhrase::VERSE; break;
    }
    if (phraseState != phraseBeforeUpdate) {
      if (!isTentativeTransition) {
        tentativePrevEnergy = energyBeforeUpdate;
        tentativePrevPhrase = phraseBeforeUpdate;
        tentativePrevPhraseStartMs = (lastPhraseChangeMs > 0) ? lastPhraseChangeMs : now;
        tentativeTransitionMs = now;
        isTentativeTransition = true;
        tentativeBeatsSeen = beatDetected ? 1 : 0;
      }
      lastPhraseChangeMs = now;
      lastExpectedPhraseStart = now;
    }
  } else {
    phraseState = MusicPhrase::UNKNOWN;
    soundEnergy = SoundEnergy::SILENCE;
    isTentativeTransition = false;
  }

  // 10. Dedicated Music Beat Probability
  if (presence == SoundPresence::QUIET || semanticState != AudioSemanticState::MUSIC) {
    beatProbability = 0.0f;
  } else {
    // Music state: must have active low-frequency transients and variance
    float span = lowFreqBuffer.maxVal - lowFreqBuffer.minVal;
    float m = lowFreqBuffer.mean;
    float stdDev = lowFreqBuffer.stdDev;
    bool hasBassEnergy = (frame.bands[0] >= 28 || frame.bands[1] >= 32 || m >= 18.0f || span >= 20.0f);
    uint32_t dtBeat = (lastBeatTimestamp > 0) ? (now - lastBeatTimestamp) : 99999;

    if (!hasBassEnergy || stdDev < 2.5f || span < 10.0f || dtBeat > 2500) {
      beatProbability *= 0.75f;
      if (beatProbability < 0.02f) beatProbability = 0.0f;
    } else {
      float varFactor = (stdDev - 2.5f) / 5.0f;
      if (varFactor < 0.0f) varFactor = 0.0f;
      if (varFactor > 1.0f) varFactor = 1.0f;

      float spanFactor = (span - 12.0f) / 25.0f;
      if (spanFactor < 0.0f) spanFactor = 0.0f;
      if (spanFactor > 1.0f) spanFactor = 1.0f;

      float levelFactor = (m - 12.0f) / 28.0f;
      if (levelFactor < 0.0f) levelFactor = 0.0f;
      if (levelFactor > 1.0f) levelFactor = 1.0f;

      float recency = 0.0f;
      if (dtBeat <= 1200) {
        recency = 1.0f;
      } else if (dtBeat <= 2200) {
        recency = 1.0f - ((float)(dtBeat - 1200) / 1000.0f);
      }

      float targetProb = varFactor * (spanFactor * 0.40f + levelFactor * 0.25f + recency * 0.35f);

      // Require at least 2 beats within 3 seconds to establish a true rhythmic groove
      uint8_t recentBeatsCount = 0;
      for (size_t i = 0; i < MAX_BEATS; i++) {
        if (beatTimes[i] > 0 && (now - beatTimes[i]) <= 3000) {
          recentBeatsCount++;
        }
      }

      if (recentBeatsCount == 0) {
        targetProb = 0.0f;
      } else if (recentBeatsCount == 1) {
        if (targetProb > 0.15f) targetProb = 0.15f;
      } else {
        if (varFactor > 0.35f && spanFactor > 0.35f && recency > 0.5f) {
          float boost = 0.70f + 0.30f * varFactor;
          if (boost > targetProb) targetProb = boost;
        }
      }
      if (targetProb < 0.0f) targetProb = 0.0f;
      if (targetProb > 1.0f) targetProb = 1.0f;

      if (targetProb < beatProbability) {
        beatProbability = (beatProbability * 0.75f) + (targetProb * 0.25f);
      } else {
        beatProbability = (beatProbability * 0.85f) + (targetProb * 0.15f);
      }
      if (beatProbability < 0.02f) beatProbability = 0.0f;
    }
  }

  // 11. BPM Gating: BPM is held during music state and cleared when leaving music
  if (semanticState != AudioSemanticState::MUSIC) {
    beatsPerMinute = 0;
    rawBpm = 0.0f;
    bpmLockCount = 0;
    beatDelayBuffer.reset();
    durationBetweenBeats = 0;
    durationBetweenBars = 0;
    durationBetweenPhrases = 0;
  }
}
