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

  clippedFrames1s = 0;
  for (size_t i = 0; i < CLIP_WINDOW_SECS; i++) clipHistory[i] = false;
  clipHistoryIdx = 0;
  clipHistoryCount = 0;
  clipSecondsCount = 0;
  clippingDetected = false;

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
  beatProbBuffer.reset();
  beatDelayBuffer.reset();
  instBeatProbability = 0.0f;
  beatProbability = 0.0f;
  frameDtMs = 20;

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
  prevBassEnergy2 = 0.0f;
  prevHighEnergy = 0.0f;
  shortBassEnergy = 0.0f;
  longBassEnergy = 0.0f;
  phraseLevel = 0.0f;
  lowVolumeStartMs = 0;

  tentativePrevEnergy = SoundEnergy::SILENCE;
  tentativePrevPhrase = MusicPhrase::UNKNOWN;
  tentativePrevPhraseStartMs = 0;
  tentativeTransitionMs = 0;
  isTentativeTransition = false;
  tentativeBeatsSeen = 0;

  resetBarHistory();
}

void AudioClassifier::resetBarHistory() {
  for (size_t i = 0; i < MAX_BARS; i++) {
    barHistory[i] = BarSummary();
  }
  barIdx = 0;
  barCount = 0;
  currentBarStartMs = 0;
  currentBarFrames = 0;
  barSumLoudness = 0.0f;
  barMaxLoudness = 0.0f;
  barSumSubBass = 0.0f;
  barSumLowMid = 0.0f;
  barSumHighMid = 0.0f;
  barSumHighs = 0.0f;
  barSumBeatProb = 0.0f;
  barSumCentroid = 0.0f;
  barSumHighFlux = 0.0f;
  barNovelty = 0.0f;
  barEnergyDelta = 0.0f;
  trackActiveMedLoud = 0.0f;
  trackActiveHighLoud = 0.0f;
  trackActiveMedBass = 0.0f;
  trackActiveMedHighFlux = 0.0f;
  recentActiveLoud = 0.0f;
  highToBassSlope = 0.0f;
  centroidSlope = 0.0f;
  loudnessSlope = 0.0f;
  highFluxSlope = 0.0f;
}

bool AudioClassifier::updateBarHistory(const AudioFrame& frame, uint32_t now, bool beatDetected) {
  if (currentBarFrames == 0) {
    currentBarStartMs = now;
  }

  float subBass = ((float)frame.bands[0] + (float)frame.bands[1]) * 0.5f;
  float lowMid = ((float)frame.bands[2] + (float)frame.bands[3] + (float)frame.bands[4]) / 3.0f;
  float highMid = ((float)frame.bands[5] + (float)frame.bands[6] + (float)frame.bands[7] + (float)frame.bands[8] + (float)frame.bands[9]) / 5.0f;
  float highs = ((float)frame.bands[10] + (float)frame.bands[11] + (float)frame.bands[12] + (float)frame.bands[13] + (float)frame.bands[14] + (float)frame.bands[15]) / 6.0f;

  float rawHigh = 0.0f;
  for (size_t i = 6; i <= 14; i++) rawHigh += (float)frame.bands[i];
  float highFlux = (rawHigh > prevHighEnergy) ? (rawHigh - prevHighEnergy) : 0.0f;
  prevHighEnergy = rawHigh;

  float bandSum = 0.0f;
  float weightedSum = 0.0f;
  for (size_t i = 0; i < NUM_AUDIO_BANDS; i++) {
    float v = (float)frame.bands[i];
    bandSum += v;
    weightedSum += v * (float)i;
  }
  float centroid = (bandSum > 1.0f) ? (weightedSum / bandSum) : 0.0f;

  barSumLoudness += frame.levelPercent;
  if (frame.levelPercent > barMaxLoudness) barMaxLoudness = frame.levelPercent;
  barSumSubBass += subBass;
  barSumLowMid += lowMid;
  barSumHighMid += highMid;
  barSumHighs += highs;
  barSumBeatProb += beatProbability;
  barSumCentroid += centroid;
  barSumHighFlux += highFlux;
  currentBarFrames++;

  uint32_t targetBarMs = (durationBetweenBars >= 1000 && durationBetweenBars <= 4000) ? durationBetweenBars : 2000;
  uint32_t barElapsed = (now >= currentBarStartMs) ? (now - currentBarStartMs) : 0;
  uint32_t minBeatSnapMs = (targetBarMs > 150) ? (targetBarMs - 150) : targetBarMs;
  bool shouldClose = (currentBarFrames > 0) && ((beatDetected && barElapsed >= minBeatSnapMs) || (barElapsed >= targetBarMs + 150));

  if (!shouldClose) {
    return false;
  }

  float invN = 1.0f / (float)currentBarFrames;
  BarSummary bar;
  bar.meanLoudness = barSumLoudness * invN;
  bar.peakLoudness = barMaxLoudness;
  bar.subBass = barSumSubBass * invN;
  bar.lowMid = barSumLowMid * invN;
  bar.highMid = barSumHighMid * invN;
  bar.highs = barSumHighs * invN;
  bar.beatProb = barSumBeatProb * invN;
  bar.spectralCentroid = barSumCentroid * invN;
  bar.highToBassRatio = (bar.highMid + bar.highs) / (bar.subBass + 4.0f);
  bar.highFlux = barSumHighFlux * invN;
  bar.durationMs = barElapsed;

  barHistory[barIdx] = bar;
  barIdx = (barIdx + 1) % MAX_BARS;
  if (barCount < MAX_BARS) barCount++;

  currentBarStartMs = now;
  currentBarFrames = 0;
  barSumLoudness = 0.0f;
  barMaxLoudness = 0.0f;
  barSumSubBass = 0.0f;
  barSumLowMid = 0.0f;
  barSumHighMid = 0.0f;
  barSumHighs = 0.0f;
  barSumBeatProb = 0.0f;
  barSumCentroid = 0.0f;
  barSumHighFlux = 0.0f;

  // 1. Track-Relative Active Baselines (median & 85th percentile across active bars)
  float louds[MAX_BARS];
  float basses[MAX_BARS];
  float fluxes[MAX_BARS];
  size_t activeN = 0;
  float recentSum = 0.0f;
  size_t recentN = 0;

  for (size_t i = 0; i < barCount; i++) {
    size_t idx = (barIdx + MAX_BARS - 1 - i) % MAX_BARS;
    const BarSummary& b = barHistory[idx];
    float spec = b.subBass + b.lowMid + b.highMid + b.highs;
    if (b.meanLoudness >= noiseFloor + 5.0f && spec > 10.0f && (b.beatProb >= 0.35f || b.subBass >= 12.0f)) {
      louds[activeN] = b.meanLoudness;
      basses[activeN] = b.subBass;
      fluxes[activeN] = b.highFlux;
      activeN++;
    }
    if (b.beatProb >= 0.45f && recentN < 6) {
      recentSum += b.meanLoudness;
      recentN++;
    }
  }

  if (activeN > 0) {
    for (size_t i = 1; i < activeN; i++) {
      float kl = louds[i], kb = basses[i], kf = fluxes[i];
      size_t j = i;
      while (j > 0 && louds[j - 1] > kl) { louds[j] = louds[j - 1]; j--; }
      louds[j] = kl;
      j = i;
      while (j > 0 && basses[j - 1] > kb) { basses[j] = basses[j - 1]; j--; }
      basses[j] = kb;
      j = i;
      while (j > 0 && fluxes[j - 1] > kf) { fluxes[j] = fluxes[j - 1]; j--; }
      fluxes[j] = kf;
    }
    trackActiveMedLoud = louds[activeN / 2];
    size_t hiIdx = (size_t)((float)activeN * 0.85f);
    if (hiIdx >= activeN) hiIdx = activeN - 1;
    trackActiveHighLoud = louds[hiIdx];
    trackActiveMedBass = basses[activeN / 2];
    trackActiveMedHighFlux = fluxes[activeN / 2];
  }
  if (recentN > 0) {
    recentActiveLoud = recentSum / (float)recentN;
  }

  // 2. Causal Bar-Synchronous Novelty & Multi-Bar Spectral Slopes
  if (barCount >= 4) {
    size_t currN = 2;
    size_t prevN = (barCount - currN >= 4) ? 4 : (barCount - currN);
    float cVec[6] = {0.0f};
    float pVec[6] = {0.0f};
    float cRatio = 0.0f, pRatio = 0.0f;
    float cCent = 0.0f, pCent = 0.0f;
    float cLoud = 0.0f, pLoud = 0.0f;
    float cFlux = 0.0f, pFlux = 0.0f;

    for (size_t i = 0; i < currN; i++) {
      size_t idx = (barIdx + MAX_BARS - 1 - i) % MAX_BARS;
      const BarSummary& b = barHistory[idx];
      cVec[0] += b.subBass / 100.0f;
      cVec[1] += b.lowMid / 100.0f;
      cVec[2] += b.highMid / 100.0f;
      cVec[3] += b.highs / 100.0f;
      cVec[4] += b.beatProb;
      cVec[5] += b.meanLoudness / 50.0f;
      cRatio += b.highToBassRatio;
      cCent += b.spectralCentroid;
      cLoud += b.meanLoudness;
      cFlux += b.highFlux;
    }
    for (size_t i = 0; i < prevN; i++) {
      size_t idx = (barIdx + MAX_BARS - 1 - currN - i) % MAX_BARS;
      const BarSummary& b = barHistory[idx];
      pVec[0] += b.subBass / 100.0f;
      pVec[1] += b.lowMid / 100.0f;
      pVec[2] += b.highMid / 100.0f;
      pVec[3] += b.highs / 100.0f;
      pVec[4] += b.beatProb;
      pVec[5] += b.meanLoudness / 50.0f;
      pRatio += b.highToBassRatio;
      pCent += b.spectralCentroid;
      pLoud += b.meanLoudness;
      pFlux += b.highFlux;
    }

    for (size_t k = 0; k < 6; k++) {
      cVec[k] /= (float)currN;
      pVec[k] /= (float)prevN;
    }
    cRatio /= (float)currN; pRatio /= (float)prevN;
    cCent /= (float)currN;  pCent /= (float)prevN;
    cLoud /= (float)currN;  pLoud /= (float)prevN;
    cFlux /= (float)currN;  pFlux /= (float)prevN;

    float dot = 0.0f, nc2 = 0.0f, np2 = 0.0f, l2 = 0.0f;
    for (size_t k = 0; k < 6; k++) {
      dot += cVec[k] * pVec[k];
      nc2 += cVec[k] * cVec[k];
      np2 += pVec[k] * pVec[k];
      float d = cVec[k] - pVec[k];
      l2 += d * d;
    }
    float nc = sqrtf(nc2);
    float np = sqrtf(np2);
    float cosDist = (nc > 0.01f && np > 0.01f) ? (1.0f - (dot / (nc * np))) : 0.0f;
    if (cosDist < 0.0f) cosDist = 0.0f;
    float relL2 = sqrtf(l2) / (nc + np + 0.1f);
    barNovelty = fminf(1.0f, (cosDist * 2.5f) + (relL2 * 0.6f));
    barEnergyDelta = (cLoud - pLoud) / fmaxf(10.0f, pLoud);

    highToBassSlope = cRatio - pRatio;
    centroidSlope = cCent - pCent;
    loudnessSlope = barEnergyDelta;
    highFluxSlope = (cFlux - pFlux) / fmaxf(4.0f, pFlux);

    if (barNovelty >= 0.50f) {
      lastExpectedPhraseStart = now;
    }
  }
  return true;
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

uint8_t AudioClassifier::getSoundEnergy() const {
  if (semanticState != AudioSemanticState::MUSIC) {
    return 0;
  }

  // Normalized acoustic headroom above noise floor (0.0 to 1.0)
  float headroom = (average1sLevel - (noiseFloor + 2.0f)) / 25.0f;
  if (headroom < 0.0f) headroom = 0.0f;
  if (headroom > 1.0f) headroom = 1.0f;

  float energy = 0.0f;
  switch (phraseState) {
    case MusicPhrase::BREAKDOWN:
      energy = 15.0f + (headroom * 10.0f); // 15% - 25% (center: 20%)
      break;
    case MusicPhrase::OUTRO:
      energy = 30.0f + (headroom * 10.0f); // 30% - 40% (center: 35%)
      break;
    case MusicPhrase::INTRO:
      energy = 45.0f + (headroom * 10.0f); // 45% - 55% (center: 50%)
      break;
    case MusicPhrase::VERSE:
      energy = 65.0f + (headroom * 13.0f); // 65% - 78% (center: 71%)
      break;
    case MusicPhrase::BUILDUP: {
      uint32_t dt = (lastPhraseChangeMs > 0 && lastFrameTimeMs >= lastPhraseChangeMs)
                    ? (lastFrameTimeMs - lastPhraseChangeMs) : 0;
      uint32_t targetDur = (durationBetweenPhrases > 0) ? durationBetweenPhrases : 16000;
      float progress = (float)dt / (float)targetDur;
      if (progress < 0.0f) progress = 0.0f;
      if (progress > 1.0f) progress = 1.0f;

      float buildupFactor = (progress * 0.65f) + (headroom * 0.35f);
      if (buildupFactor > 1.0f) buildupFactor = 1.0f;
      energy = 80.0f + (buildupFactor * 15.0f); // 80% - 95%
      break;
    }
    case MusicPhrase::CHORUS:
      if (peak1sLevel >= 40.0f || headroom >= 0.70f) {
        energy = 100.0f;
      } else {
        energy = 95.0f + (headroom * 5.0f); // 95% - 100%
      }
      break;
    default:
      energy = 0.0f;
      break;
  }

  if (energy < 0.0f) energy = 0.0f;
  if (energy > 100.0f) energy = 100.0f;
  return (uint8_t)roundf(energy);
}

const char* AudioClassifier::getSoundEnergyStr() const {
  if (semanticState != AudioSemanticState::MUSIC) {
    return "silence";
  }
  switch (soundEnergy) {
    case SoundEnergy::SILENCE:   return "silence";
    case SoundEnergy::NOISE:     return "noise";
    case SoundEnergy::BREAKDOWN: return "breakdown";
    case SoundEnergy::OUTRO:     return "outro";
    case SoundEnergy::INTRO:     return "intro";
    case SoundEnergy::VERSE:     return "verse";
    case SoundEnergy::BUILDUP:   return "buildup";
    case SoundEnergy::CHORUS:    return "chorus";
    default:                     return "silence";
  }
}

void AudioClassifier::processFrame(const AudioFrame& frame) {
  uint32_t now = frame.timestampMs;
  if (now == lastFrameTimeMs && lastFrameTimeMs > 0) {
    return; // de-duplicate identical packets
  }
  frameDtMs = (lastFrameTimeMs > 0 && now > lastFrameTimeMs) ? (now - lastFrameTimeMs) : 20;
  lastFrameTimeMs = now;

  // 1. Smooth instantaneous level and track 1s peak
  currentLevel = (currentLevel * 0.7f) + (frame.levelPercent * 0.3f);
  if (frame.levelPercent > current1sPeak) {
    current1sPeak = frame.levelPercent;
  }

  // 2. Accumulate energy and saturation count for 1-second metrics
  sumSquaredEnergy += (frame.levelPercent * frame.levelPercent);
  frameCount1s++;

  if (frame.rawVolume >= 253.0f || frame.smoothedVolume >= 253.0f || frame.levelPercent >= 99.0f) {
    clippedFrames1s++;
  }

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
    instBeatProbability = 0.0f;
    beatProbability = 0.0f;
    beatProbBuffer.reset();
  } else {
    updateMusicAndPhrasing(frame);
    if (presence == SoundPresence::QUIET) {
      semanticState = AudioSemanticState::SILENCE;
      phraseState = MusicPhrase::UNKNOWN;
      soundEnergy = SoundEnergy::SILENCE;
      musicConfidence = 0.0f;
      instBeatProbability = 0.0f;
      beatProbability = 0.0f;
      beatProbBuffer.reset();
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

    // Evaluate per-second clipping ratio over a 10-second sliding window
    float clipRatio = (float)clippedFrames1s / (float)frameCount1s;
    bool isClipSec = (clippedFrames1s >= 3 && clipRatio >= 0.10f);

    if (clipHistoryCount == CLIP_WINDOW_SECS) {
      if (clipHistory[clipHistoryIdx] && clipSecondsCount > 0) {
        clipSecondsCount--;
      }
    } else {
      clipHistoryCount++;
    }

    clipHistory[clipHistoryIdx] = isClipSec;
    if (isClipSec) {
      clipSecondsCount++;
    }
    clipHistoryIdx = (clipHistoryIdx + 1) % CLIP_WINDOW_SECS;

    if (!clippingDetected) {
      if (clipSecondsCount >= 3) {
        clippingDetected = true;
      }
    } else {
      if (clipSecondsCount == 0) {
        clippingDetected = false;
      }
    }
  } else {
    average1sLevel = currentLevel;
    peak1sLevel = currentLevel;
  }
  sumSquaredEnergy = 0.0f;
  current1sPeak = 0.0f;
  frameCount1s = 0;
  clippedFrames1s = 0;

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
    instBeatProbability = 0.0f;
    beatProbability = 0.0f;
    beatProbBuffer.reset();
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

  // Positive Bass Spectral Flux (instantaneous 1-frame rise)
  float rawBass = (float)frame.bands[0] * 1.3f + (float)frame.bands[1] * 0.7f;
  float bassFlux = (rawBass > prevBassEnergy) ? (rawBass - prevBassEnergy) : 0.0f;
  prevBassEnergy2 = prevBassEnergy;
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

  // 3. Track Decayed Maximum Loudness (adapts after 30s of sustained lower-level music)
  float dtNorm = (frameDtMs > 0 && frameDtMs <= 200) ? ((float)frameDtMs / 25.0f) : 1.0f;
  if (dtNorm < 0.5f) dtNorm = 0.5f;
  if (dtNorm > 1.25f) dtNorm = 1.25f;

  if (loudnessBuffer.rms > decayedMaxLoudness) {
    maxLoudness = loudnessBuffer.rms;
    decayedMaxLoudness = maxLoudness;
    lastMaxLoudnessTime = now;
  } else {
    uint32_t dt = now - lastMaxLoudnessTime;
    if (dt > 30000 && loudnessBuffer.rms < decayedMaxLoudness * 0.65f) {
      float targetMax = fmaxf(15.0f, loudnessBuffer.rms * 1.5f);
      float relaxAlpha = 1.0f - powf(0.9992f, dtNorm);
      decayedMaxLoudness = (decayedMaxLoudness * (1.0f - relaxAlpha)) + (targetMax * relaxAlpha);
      if (decayedMaxLoudness < 15.0f) decayedMaxLoudness = 15.0f;
    }
    if (dt > 300000) {
      float decay = 1.0f - ((float)(dt - 300000) / 300000.0f);
      if (decay < 0.0f) decay = 0.0f;
      float minDecayed = (maxLoudness * 0.5f > 15.0f) ? (maxLoudness * 0.5f) : 15.0f;
      if (maxLoudness * decay > minDecayed) {
        decayedMaxLoudness = fminf(decayedMaxLoudness, maxLoudness * decay);
      } else {
        decayedMaxLoudness = fminf(decayedMaxLoudness, minDecayed);
      }
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
  // Accept punchy sub-bass (band 0 >= 35) OR punchy mid-bass (band 1 >= 40) OR combined bass (band 0 + band 1 >= 60),
  // AND bass must make up a reasonable portion of total spectrum (>= 22%) or have strong positive onset flux (>= 20).
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
          float maxDelayStdDev = (rawBpm <= 0.0f || beatsPerMinute == 0) ? 10.0f : 14.0f;
          if (beatDelayBuffer.count >= 4 && beatDelayBuffer.stdDev < maxDelayStdDev) {
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
        } else {
          beatRegularity *= 0.75f;
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

  if (phraseState == MusicPhrase::CHORUS) {
    dropThresh *= 2.0f;
  } else if (phraseState == MusicPhrase::BUILDUP || phraseState == MusicPhrase::INTRO || phraseState == MusicPhrase::BREAKDOWN || phraseState == MusicPhrase::OUTRO || phraseState == MusicPhrase::UNKNOWN) {
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
  } else {
    bool enteringMusic = (semanticState != AudioSemanticState::MUSIC);
    float minBassRatio = enteringMusic ? 1.15f : 1.05f;
    float minLowBass = enteringMusic ? 18.0f : 10.0f;
    float minHighEnergy = enteringMusic ? 25.0f : 15.0f;
    float minHighBass = enteringMusic ? 25.0f : 20.0f;
    if (((b01 >= b23 * minBassRatio && b01 >= minLowBass) || (highEnergy >= minHighEnergy && b01 >= minHighBass)) &&
        !isMidOverpowering &&
        (currentLevel > (noiseFloor + 5.0f) || (frame.levelPercent > (noiseFloor + 4.0f) && b01 >= 30.0f))) {
      hasMusicalEvidence = true;
      instantConfidence = 0.75f;
    }
  }

  if (hasMusicalEvidence) {
    lastMusicEvidenceTime = now;
    musicConfidence = (musicConfidence * 0.85f) + (instantConfidence * 0.15f);
    if (musicConfidence > 1.0f) musicConfidence = 1.0f;
  } else {
    // Decay faster while still in AMBIENT_NOISE so sparse speech/call peaks cannot ratchet up into MUSIC
    musicConfidence *= (semanticState == AudioSemanticState::MUSIC) ? 0.999f : 0.997f;
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
    if (musicConfidence >= 0.60f || (beatRegularity >= 0.72f && beatCount >= 3 && currentLevel > noiseFloor + 5.0f)) {
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

  // 9. Dedicated Music Beat Probability (computed before phrasing so current frame's beat probability is active)
  // Pure signal-driven & sliding-window averaged: independent of phraseState or bpmLockCount
  if (presence == SoundPresence::QUIET || semanticState != AudioSemanticState::MUSIC) {
    instBeatProbability = 0.0f;
    beatProbability = 0.0f;
    beatProbBuffer.reset();
  } else {
    float span = lowFreqBuffer.maxVal - lowFreqBuffer.minVal;
    float m = lowFreqBuffer.mean;
    float stdDev = lowFreqBuffer.stdDev;

    // Time-normalized smoothing step (clamped near nominal 20-25ms so 40ms MQTT frames don't over-decay between beats)
    float dtFactor = (frameDtMs > 0 && frameDtMs <= 200) ? ((float)frameDtMs / 25.0f) : 1.0f;
    if (dtFactor < 0.5f) dtFactor = 0.5f;
    if (dtFactor > 1.25f) dtFactor = 1.25f;

    // 9a. Low-frequency transient modulation (stdDev and span across 1.5s window)
    float varFactor = (stdDev - 2.8f) / 7.5f;
    if (varFactor < 0.0f) varFactor = 0.0f;
    if (varFactor > 1.0f) varFactor = 1.0f;

    float spanFactor = (span - 12.0f) / 34.0f;
    if (spanFactor < 0.0f) spanFactor = 0.0f;
    if (spanFactor > 1.0f) spanFactor = 1.0f;

    // 9b. Sustained bass & relative loudness envelope (prevents quiet breakdown residual noise from scoring high)
    float meanFactor = (m - 12.0f) / 14.0f;
    if (meanFactor < 0.0f) meanFactor = 0.0f;
    if (meanFactor > 1.0f) meanFactor = 1.0f;

    float relLoud = (averageLoudnessPercent - 34.0f) / 30.0f;
    if (relLoud < 0.0f) relLoud = 0.0f;
    if (relLoud > 1.0f) relLoud = 1.0f;

    float absLoud = (loudnessBuffer.rms - (noiseFloor + 3.5f)) / 9.5f;
    if (absLoud < 0.0f) absLoud = 0.0f;
    if (absLoud > 1.0f) absLoud = 1.0f;

    float loudFactor = fminf(absLoud, fmaxf(relLoud, meanFactor * 0.90f));

    float bassEnv = ((varFactor * 0.45f) + (spanFactor * 0.35f) + (meanFactor * 0.20f)) * meanFactor * loudFactor;
    float scaledEnv = bassEnv * 1.60f;
    if (scaledEnv < 0.0f) scaledEnv = 0.0f;
    if (scaledEnv > 1.0f) scaledEnv = 1.0f;
    float invEnv = 1.0f - scaledEnv;
    float modScore = 1.0f - (invEnv * invEnv * invEnv);

    // 9c. Time-weighted beat density over 4.0s & outlier-tolerant fundamental period cluster matching
    float weightedBeats = 0.0f;
    for (size_t i = 0; i < MAX_BEATS; i++) {
      uint32_t bt = beatTimes[i];
      if (bt > 0 && now >= bt) {
        uint32_t age = now - bt;
        if (age <= 2400) {
          weightedBeats += 1.0f;
        } else if (age <= 4000) {
          weightedBeats += 1.0f - ((float)(age - 2400) / 1600.0f);
        }
      }
    }

    float recentIntervals[8];
    size_t numIntervals = 0;
    for (size_t i = 1; i < beatCount && numIntervals < 8; i++) {
      size_t currIdx = (beatIdx + MAX_BEATS - i) % MAX_BEATS;
      size_t prevIdx = (beatIdx + MAX_BEATS - i - 1) % MAX_BEATS;
      uint32_t tCurr = beatTimes[currIdx];
      uint32_t tPrev = beatTimes[prevIdx];
      if (tCurr == 0 || tPrev == 0 || tCurr <= tPrev || (now - tCurr) > 4800) break;
      uint32_t dt = tCurr - tPrev;
      if (dt >= 220 && dt <= 1800) {
        recentIntervals[numIntervals++] = (float)dt;
      }
    }

    float rhythmScore = 0.0f;
    if (weightedBeats < 0.2f) {
      rhythmScore = 0.0f;
    } else if (numIntervals == 0 || weightedBeats <= 1.05f) {
      rhythmScore = 0.15f * fminf(1.0f, weightedBeats);
    } else if (numIntervals == 1) {
      float dt0 = recentIntervals[0];
      float inBand = (dt0 >= 280.0f && dt0 <= 950.0f) ? 1.0f : 0.5f;
      rhythmScore = 0.35f * inBand * fminf(1.0f, weightedBeats * 0.5f);
    } else {
      float bestClusterScore = 0.0f;
      for (size_t i = 0; i < numIntervals; i++) {
        float baseDt = recentIntervals[i];
        float cands[3];
        size_t numCands = 0;
        cands[numCands++] = baseDt;
        if (baseDt >= 520.0f) cands[numCands++] = baseDt * 0.5f;
        if (baseDt >= 780.0f) cands[numCands++] = baseDt / 3.0f;

        for (size_t c = 0; c < numCands; c++) {
          float T = cands[c];
          if (T < 250.0f || T > 850.0f) continue;
          float matched = 0.0f;
          for (size_t j = 0; j < numIntervals; j++) {
            float dt = recentIntervals[j];
            float e1 = fabsf(dt - T) / T;
            float e2 = (fabsf(dt - 2.0f * T) / (2.0f * T)) * 1.18f;
            float e3 = (fabsf(dt - 3.0f * T) / (3.0f * T)) * 1.35f;
            float e = fminf(e1, fminf(e2, e3));
            if (e <= 0.22f) {
              matched += 1.0f - (e / 0.34f);
            }
          }
          float score = matched / (float)numIntervals;
          if (score > bestClusterScore) bestClusterScore = score;
        }
      }

      // Consecutive interval ratio check: penalizes chaotic alternating intervals (e.g. 300, 820, 420, 1140, 340)
      size_t consecMatches = 0;
      for (size_t k = 0; k + 1 < numIntervals; k++) {
        float a = recentIntervals[k];
        float b = recentIntervals[k + 1];
        float r = (a > b) ? (a / fmaxf(1.0f, b)) : (b / fmaxf(1.0f, a));
        if (r <= 1.30f || fabsf(r - 2.0f) <= 0.26f) {
          consecMatches++;
        }
      }
      float consecRatio = (numIntervals > 1) ? ((float)consecMatches / (float)(numIntervals - 1)) : 1.0f;
      float effCluster = bestClusterScore * (0.20f + 0.80f * consecRatio);

      float density = (weightedBeats - 1.0f) / 2.2f;
      if (density < 0.0f) density = 0.0f;
      if (density > 1.0f) density = 1.0f;

      float reg = 0.16f;
      if (effCluster >= 0.54f) {
        reg = 0.85f + 0.15f * ((effCluster - 0.54f) / 0.46f);
      } else if (effCluster > 0.36f) {
        reg = 0.16f + 0.69f * ((effCluster - 0.36f) / 0.18f);
      }
      rhythmScore = reg * (0.45f + 0.55f * density);
    }

    float targetProb = modScore * rhythmScore;
    if (numIntervals == 0 && targetProb > 0.15f) {
      targetProb = 0.15f;
    }
    if (targetProb < 0.0f) targetProb = 0.0f;
    if (targetProb > 1.0f) targetProb = 1.0f;

    float alpha = (modScore < 0.12f)
                  ? (1.0f - powf(0.55f, dtFactor))
                  : ((targetProb < instBeatProbability)
                     ? (1.0f - powf(0.78f, dtFactor))
                     : (1.0f - powf(0.85f, dtFactor)));
    instBeatProbability = (instBeatProbability * (1.0f - alpha)) + (targetProb * alpha);
    if (instBeatProbability < 0.02f) instBeatProbability = 0.0f;

    // 9d. Sliding-window time averaging (75 frames = ~1.5s-2.5s window)
    beatProbBuffer.add(instBeatProbability);
    beatProbBuffer.calculate();

    uint32_t dtBeat = (lastBeatTimestamp > 0) ? (now - lastBeatTimestamp) : 99999;
    float envGate = 1.0f;
    if (dtBeat > 1600 && averageLoudnessPercent < 32.0f) {
      envGate = fminf(1.0f, fmaxf(modScore * 2.5f, instBeatProbability * 1.5f));
    }
    beatProbability = beatProbBuffer.mean * envGate;
    if (beatProbability < 0.02f) beatProbability = 0.0f;
  }

  // 10. Phrasing & SoundEnergy State Machine (Bar-Synchronized Unified 6-State Grammar)
  if (semanticState == AudioSemanticState::MUSIC) {
    bool barClosedThisFrame = updateBarHistory(frame, now, beatDetected);

    float smoothAvg = fmaxf(average1sLevel, loudnessBuffer.rms);
    float smoothPeak = fmaxf(peak1sLevel, loudnessBuffer.maxVal);

    if (soundEnergy == SoundEnergy::SILENCE) {
      phraseLevel = frame.levelPercent;
      lowVolumeStartMs = 0;
      if ((smoothPeak >= 60.0f || currentLevel >= 50.0f) && (smoothAvg >= 40.0f || currentLevel >= 45.0f)) {
        soundEnergy = SoundEnergy::CHORUS;
        phraseState = MusicPhrase::CHORUS;
      } else {
        soundEnergy = SoundEnergy::INTRO;
        phraseState = MusicPhrase::INTRO;
      }
      shortBassEnergy = bassEnergy;
      longBassEnergy = bassEnergy;
      lastEnergyUpdateTime = now;
      lastPhraseChangeMs = now;
    } else {
      phraseLevel = (phraseLevel * 0.98f) + (frame.levelPercent * 0.02f);
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

    // Outlier Detection & Revert Evaluation (Upward outlier protection only;
    // downward transitions use sustained hysteresis/dwell to avoid ping-pong reverts)
    if (isTentativeTransition) {
      uint32_t dtTentative = (now >= tentativeTransitionMs) ? (now - tentativeTransitionMs) : 0;
      if (beatDetected) {
        tentativeBeatsSeen++;
      }

      if (dtTentative > outlierWindowMs) {
        isTentativeTransition = false;
      } else if ((phraseState == MusicPhrase::CHORUS || phraseState == MusicPhrase::VERSE) &&
                 (tentativePrevPhrase != MusicPhrase::CHORUS && tentativePrevPhrase != MusicPhrase::VERSE)) {
        if (tentativeBeatsSeen >= 2 || (smoothPeak >= 48.0f && bassBuffer.mean >= 20.0f && smoothAvg >= 28.0f)) {
          isTentativeTransition = false;
        } else {
          uint32_t gapSinceBeat = (lastBeatTimestamp > 0) ? (now - lastBeatTimestamp) : dtTentative;
          bool noFollowupBeat = (gapSinceBeat >= (uint32_t)(beatDur * 2.2f));
          bool energyDropped = (peak1sLevel < 42.0f && bassBuffer.mean < 15.0f && currentLevel < 24.0f);

          if (noFollowupBeat && energyDropped) {
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
      } else {
        isTentativeTransition = false;
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

    if (currentLevel < 12.0f && !beatDetected) {
      if (lowVolumeStartMs == 0) lowVolumeStartMs = now;
    } else if (currentLevel >= 14.0f || (beatDetected && currentLevel >= 12.0f)) {
      lowVolumeStartMs = 0;
    }
    uint32_t lowVolumeDurMs = (lowVolumeStartMs > 0 && now >= lowVolumeStartMs) ? (now - lowVolumeStartMs) : 0;

    bool isVolumeCollapsed = ((currentLevel < (noiseFloor + 4.0f) && average1sLevel < (noiseFloor + 6.0f)) ||
                              (peak1sLevel < 12.0f && average1sLevel < 8.0f)) &&
                             (lowVolumeDurMs >= 800 || loudnessBuffer.rms < (noiseFloor + 6.0f));
    bool noBeatsRecent = (durationSinceLastBeat > 1200 || barsWithoutBeats >= 1);
    bool hasActiveBeatGroove = (barsWithoutBeats == 0) && (durationSinceLastBeat <= (barDur * 5 / 4)) && (instBeatProbability >= 0.25f || constantBeats);

    // Track-relative normalization (Approach A) + Causal novelty & spectral slopes (Approaches B & C)
    bool hasTrackContext = (barCount >= 4 && trackActiveMedLoud >= 8.0f);
    float relLoud = hasTrackContext ? (smoothAvg / fmaxf(8.0f, trackActiveMedLoud)) : 1.0f;
    float relPeak = hasTrackContext ? (smoothPeak / fmaxf(12.0f, trackActiveHighLoud)) : 1.0f;
    float subBassNow = ((float)frame.bands[0] + (float)frame.bands[1]) * 0.5f;
    float upperNow = 0.0f;
    for (size_t i = 5; i < NUM_AUDIO_BANDS; i++) upperNow += (float)frame.bands[i];
    upperNow /= 11.0f;

    bool isRelativeBreakdown = hasTrackContext && (recentActiveLoud >= 14.0f) &&
                               (smoothAvg < recentActiveLoud * 0.58f) &&
                               (beatProbability < 0.25f) && (barsWithoutBeats >= 2);

    bool absChorusCheck = (smoothPeak >= 48.0f && smoothAvg >= 32.0f && currentLevel >= 24.0f && bassTrend >= 0.85f) ||
                          (dropInLastPhrase && (smoothPeak >= 42.0f || smoothAvg >= 32.0f) && smoothAvg >= 28.0f && currentLevel >= 22.0f && bassTrend >= 0.85f) ||
                          (currentLevel >= 38.0f && smoothPeak >= 48.0f && bassTrend >= 1.35f);
    bool relChorusCheck = hasTrackContext && (smoothPeak >= noiseFloor + 14.0f) &&
                          (beatProbability >= 0.50f || beatDetected) && (
                            (relLoud >= 1.45f && relPeak >= 0.94f && smoothAvg >= trackActiveMedLoud + 8.0f && bassTrend >= 1.05f) ||
                            (smoothPeak >= 38.0f && relPeak >= 0.90f && relLoud >= 0.94f && smoothAvg >= noiseFloor + 14.0f && bassTrend >= 0.85f)
                          );
    bool canEnterChorus = (hasTrackContext ? (relChorusCheck || (absChorusCheck && relLoud >= 0.92f)) : absChorusCheck) &&
                          (beatInLastPhrase || beatDetected) &&
                          (beatProbability >= 0.45f || beatDetected);

    bool canEnterVerseFromLow = hasActiveBeatGroove &&
                                (instBeatProbability >= 0.40f) &&
                                (durationSinceLastBeat <= 1000) &&
                                (smoothAvg >= fmaxf(noiseFloor + 6.0f, 8.5f)) &&
                                (loudnessBuffer.rms >= fmaxf(noiseFloor + 4.5f, 7.5f)) &&
                                (smoothPeak >= noiseFloor + 10.0f) &&
                                (averageLoudnessPercent >= 48.0f);

    bool canSustainVerseOrChorus = hasActiveBeatGroove &&
                                   !isVolumeCollapsed &&
                                   (smoothPeak >= 14.0f && smoothAvg >= 9.0f);

    bool hasSlopeBuildup = (barCount >= 3) &&
                           (centroidSlope >= 0.70f || highToBassSlope >= 0.30f || highFluxSlope >= 0.35f) &&
                           (loudnessSlope >= 0.05f || highFluxSlope >= 0.45f) &&
                           (beatProbability < 0.65f || barsWithoutBeats >= 1) &&
                           (smoothAvg >= noiseFloor + 5.0f);

    bool hasBandBuildup = (peak1sLevel >= 16.0f && smoothAvg >= 12.0f) &&
                          ((frame.bands[10] >= 30 && frame.bands[11] >= 40) ||
                           (barsWithoutBeats >= 1 && ((currentLevel > average1sLevel * 1.15f && peak1sLevel >= 20.0f && (frame.bands[10] >= 18 || frame.bands[11] >= 18 || upperNow > subBassNow * 1.4f)) ||
                                                      (frame.majorPeakHz > 300.0f && (frame.bands[10] >= 20 || frame.bands[11] >= 20 || upperNow > subBassNow * 1.4f)))) ||
                           (soundEnergy == SoundEnergy::VERSE && bassTrend > 1.25f && barsWithoutBeats >= 1 && peak1sLevel >= 22.0f));

    bool hasBuildupFeatures = hasBandBuildup || hasSlopeBuildup;

    // Approach D: Continuous 6-State Likelihood Scoring + Unified Transition Grammar
    float sBreakdown = 0.0f;
    if (isVolumeCollapsed && noBeatsRecent) {
      sBreakdown = 0.95f;
    } else if (isRelativeBreakdown && noBeatsRecent && soundEnergy != SoundEnergy::CHORUS && timeSincePhraseChange >= 4000) {
      sBreakdown = 0.82f;
    }

    bool canRecoverFromOutro = (currentLevel >= noiseFloor + 6.0f) &&
                               (average1sLevel >= fmaxf(noiseFloor + 10.0f, hasTrackContext ? trackActiveMedLoud * 0.70f : 18.0f)) &&
                               !isVolumeCollapsed;
    float sOutro = 0.0f;
    if (soundEnergy == SoundEnergy::OUTRO) {
      sOutro = canRecoverFromOutro ? 0.20f : 1.15f;
    } else if (soundEnergy == SoundEnergy::BREAKDOWN && timeSincePhraseChange >= 20000 && barsSinceEnergyUpdate >= 10) {
      sOutro = 1.15f;
    }

    float sBuildup = 0.0f;
    if (soundEnergy != SoundEnergy::CHORUS) {
      if (hasBuildupFeatures) {
        sBuildup = (hasSlopeBuildup && hasBandBuildup) ? 0.84f : 0.76f;
      } else if (soundEnergy == SoundEnergy::BUILDUP && !isVolumeCollapsed && smoothAvg >= noiseFloor + 4.0f && barsSinceEnergyUpdate < 16) {
        sBuildup = 0.76f;
      }
    }

    float sChorus = 0.0f;
    if (canEnterChorus) {
      float relBoost = hasTrackContext ? fminf(0.18f, fmaxf(0.0f, (relLoud - 1.0f) * 0.40f)) : 0.10f;
      float novBoost = (barNovelty >= 0.45f && barEnergyDelta >= 0.20f) ? 0.08f : 0.0f;
      sChorus = fminf(1.0f, 0.85f + relBoost + novBoost);
    } else if (soundEnergy == SoundEnergy::CHORUS && canSustainVerseOrChorus) {
      if (!isTentativeTransition && timeSincePhraseChange < phraseDur && smoothPeak >= 35.0f) {
        sChorus = 0.88f;
      } else if (!nearPhraseBoundary || dropInLastPhrase || (smoothPeak >= 48.0f && smoothAvg >= 34.0f)) {
        sChorus = 0.84f;
      }
    }

    float sVerse = 0.0f;
    if ((soundEnergy == SoundEnergy::BREAKDOWN || soundEnergy == SoundEnergy::OUTRO) ? canEnterVerseFromLow : canSustainVerseOrChorus) {
      if (soundEnergy != SoundEnergy::CHORUS || (barsWithoutBeats == 0 && currentLevel >= 18.0f && bassEnergy >= 20.0f && bassTrend >= 0.75f)) {
        sVerse = (relLoud < 1.15f) ? 0.78f : 0.70f;
      }
    }

    float sIntro = 0.0f;
    if (soundEnergy == SoundEnergy::INTRO && !canEnterChorus && !hasBuildupFeatures) {
      if (!isVolumeCollapsed && timeSincePhraseChange < phraseDur && !(barsWithoutBeats >= 2 && loudnessBuffer.rms < 10.0f) && !(noBeatsRecent && loudnessBuffer.rms < 8.5f)) {
        sIntro = 0.82f;
      }
    }

    // Incumbency hysteresis (+0.10f) to prevent micro-flips between closely scored states
    if (soundEnergy == SoundEnergy::BREAKDOWN && sBreakdown > 0.0f) sBreakdown += 0.10f;
    if (soundEnergy == SoundEnergy::OUTRO && sOutro > 0.0f)         sOutro += 0.10f;
    if (soundEnergy == SoundEnergy::BUILDUP && sBuildup > 0.0f)     sBuildup += 0.10f;
    if (soundEnergy == SoundEnergy::CHORUS && sChorus > 0.0f)       sChorus += 0.10f;
    if (soundEnergy == SoundEnergy::VERSE && sVerse > 0.0f)         sVerse += 0.10f;
    if (soundEnergy == SoundEnergy::INTRO && sIntro > 0.0f)         sIntro += 0.10f;

    // Select highest-scoring phrase with musical grammar tie-breaking
    SoundEnergy updatedEnergy = SoundEnergy::BREAKDOWN;
    float bestScore = sBreakdown;
    if (sOutro > bestScore)   { bestScore = sOutro;   updatedEnergy = SoundEnergy::OUTRO; }
    if (sIntro > bestScore)   { bestScore = sIntro;   updatedEnergy = SoundEnergy::INTRO; }
    if (sVerse > bestScore)   { bestScore = sVerse;   updatedEnergy = SoundEnergy::VERSE; }
    if (sBuildup > bestScore) { bestScore = sBuildup; updatedEnergy = SoundEnergy::BUILDUP; }
    if (sChorus > bestScore)  { bestScore = sChorus;  updatedEnergy = SoundEnergy::CHORUS; }

    // Preserve explicit Breakdown/Outro fallback when no active score wins
    if (bestScore < 0.30f) {
      if (soundEnergy == SoundEnergy::OUTRO) {
        updatedEnergy = canRecoverFromOutro ? SoundEnergy::BREAKDOWN : SoundEnergy::OUTRO;
      } else if (soundEnergy == SoundEnergy::VERSE && !isVolumeCollapsed && barsWithoutBeats < 2 && !(barsWithoutBeats >= 1 && loudnessBuffer.rms < 10.0f && currentLevel < 9.0f) && !(bassTrend < 0.42f && loudnessBuffer.rms < 10.5f)) {
        updatedEnergy = SoundEnergy::VERSE;
      } else if (soundEnergy == SoundEnergy::CHORUS && !isVolumeCollapsed && !(barsWithoutBeats >= 2 && currentLevel < 16.0f && average1sLevel < 22.0f) && !(barsWithoutBeats >= 1 && currentLevel < 15.0f && loudnessBuffer.rms < 16.0f)) {
        updatedEnergy = SoundEnergy::CHORUS;
      } else if (soundEnergy == SoundEnergy::BREAKDOWN && timeSincePhraseChange >= 20000 && barsSinceEnergyUpdate >= 10) {
        updatedEnergy = SoundEnergy::OUTRO;
      } else {
        updatedEnergy = SoundEnergy::BREAKDOWN;
      }
    }

    // Unified bar-synchronized transition gate + dwell rules (replaces dual > / < tree)
    bool immediateBeatTransition = beatDetected && (timeSincePhraseChange >= 2000) && (updatedEnergy == SoundEnergy::CHORUS || (updatedEnergy == SoundEnergy::VERSE && timeSincePhraseChange >= 3500)) && (updatedEnergy != soundEnergy);
    bool barGate = barClosedThisFrame || isVolumeCollapsed || (timeSincePhraseChange >= 2000 && updatedEnergy == SoundEnergy::OUTRO);
    if ((updatedEnergy != soundEnergy) && (((now - lastEnergyUpdateTime >= 2000) && barGate) || immediateBeatTransition)) {
      bool allowTransition = false;
      if (soundEnergy == SoundEnergy::OUTRO) {
        if (updatedEnergy == SoundEnergy::CHORUS && timeSincePhraseChange >= 2000) allowTransition = true;
        else if (updatedEnergy == SoundEnergy::BUILDUP && timeSincePhraseChange >= 2000) allowTransition = true;
        else if (updatedEnergy == SoundEnergy::VERSE && timeSincePhraseChange >= 3500) allowTransition = true;
        else if (updatedEnergy == SoundEnergy::BREAKDOWN && timeSincePhraseChange >= 3000 && !canEnterVerseFromLow) allowTransition = true;
      } else if (soundEnergy == SoundEnergy::INTRO) {
        if (updatedEnergy == SoundEnergy::CHORUS) allowTransition = true;
        else if (updatedEnergy == SoundEnergy::BUILDUP && timeSincePhraseChange >= 2000) allowTransition = true;
        else if (updatedEnergy == SoundEnergy::VERSE && timeSincePhraseChange >= phraseDur) allowTransition = true;
        else if (updatedEnergy == SoundEnergy::BREAKDOWN && (timeSincePhraseChange >= 3000 || isVolumeCollapsed)) allowTransition = true;
      } else if (soundEnergy == SoundEnergy::BREAKDOWN) {
        if (updatedEnergy == SoundEnergy::CHORUS && timeSincePhraseChange >= 2000) allowTransition = true;
        else if (updatedEnergy == SoundEnergy::BUILDUP && timeSincePhraseChange >= 2000) allowTransition = true;
        else if (updatedEnergy == SoundEnergy::VERSE && timeSincePhraseChange >= 3500) allowTransition = true;
        else if (updatedEnergy == SoundEnergy::OUTRO && timeSincePhraseChange >= 20000 && barsSinceEnergyUpdate >= 10) allowTransition = true;
      } else if (soundEnergy == SoundEnergy::VERSE) {
        if (updatedEnergy == SoundEnergy::CHORUS && (timeSincePhraseChange >= 2000 || smoothPeak >= 52.0f || bassTrend >= 1.40f)) allowTransition = true;
        else if (updatedEnergy == SoundEnergy::BUILDUP && timeSincePhraseChange >= 3000) allowTransition = true;
        else if (updatedEnergy == SoundEnergy::BREAKDOWN && (timeSincePhraseChange >= 3500 || isVolumeCollapsed)) allowTransition = true;
      } else if (soundEnergy == SoundEnergy::BUILDUP) {
        if (updatedEnergy == SoundEnergy::CHORUS || (dropInLastPhrase && (smoothPeak >= 38.0f || smoothAvg >= 28.0f) && smoothAvg >= 26.0f)) {
          updatedEnergy = SoundEnergy::CHORUS;
          allowTransition = true;
        } else if (barsSinceEnergyUpdate >= 16 || isVolumeCollapsed || smoothAvg < noiseFloor + 3.0f) {
          updatedEnergy = SoundEnergy::BREAKDOWN;
          allowTransition = true;
        } else if (updatedEnergy == SoundEnergy::VERSE && hasActiveBeatGroove && timeSincePhraseChange >= 8000) {
          allowTransition = true;
        }
      } else if (soundEnergy == SoundEnergy::CHORUS) {
        if (!isTentativeTransition && timeSincePhraseChange < phraseDur && !isVolumeCollapsed) {
          if ((updatedEnergy == SoundEnergy::BREAKDOWN || updatedEnergy == SoundEnergy::OUTRO) &&
              timeSincePhraseChange >= 2500 &&
              (isVolumeCollapsed || (barsWithoutBeats >= 1 && currentLevel < 15.0f && loudnessBuffer.rms < 16.0f))) {
            updatedEnergy = SoundEnergy::BREAKDOWN;
            allowTransition = true;
          }
        } else if (updatedEnergy == SoundEnergy::VERSE && beatInLastBar && hasActiveBeatGroove) {
          allowTransition = true;
        } else if (updatedEnergy == SoundEnergy::BREAKDOWN && (isVolumeCollapsed || timeSincePhraseChange >= 2500)) {
          allowTransition = true;
        }
      } else {
        allowTransition = true;
      }

      if (allowTransition) {
        soundEnergy = updatedEnergy;
        lastEnergyUpdateTime = now;
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
    phraseLevel = 0.0f;
    lowVolumeStartMs = 0;
    resetBarHistory();
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
