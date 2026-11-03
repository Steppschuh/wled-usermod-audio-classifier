// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#include <unity.h>
#include "classifier.h"

void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}

void test_initial_state_is_quiet_and_silence(void) {
  AudioClassifier classifier;
  TEST_ASSERT_EQUAL_UINT8(SoundPresence::QUIET, classifier.getPresence());
  TEST_ASSERT_FALSE(classifier.isSoundDetected());
  TEST_ASSERT_EQUAL_STRING("silence", classifier.getSemanticStateStr());
  TEST_ASSERT_EQUAL_STRING("steady", classifier.getPhraseStateStr());
}

void test_transient_noise_ignored(void) {
  ClassifierConfig cfg;
  cfg.onDelayMs = 1000;
  cfg.quietThresholdPercent = 10.0f;
  AudioClassifier classifier(cfg);

  AudioFrame frame;
  // A brief 300ms burst of loud sound (e.g., door slam or clap)
  for (uint32_t t = 0; t <= 300; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 85.0f;
    classifier.processFrame(frame);
  }

  // Returns to quiet
  for (uint32_t t = 320; t <= 1500; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 2.0f;
    classifier.processFrame(frame);
  }

  // Must not have triggered presence
  TEST_ASSERT_EQUAL_UINT8(SoundPresence::QUIET, classifier.getPresence());
  TEST_ASSERT_FALSE(classifier.isSoundDetected());
}

void test_sustained_sound_triggers_presence(void) {
  ClassifierConfig cfg;
  cfg.onDelayMs = 1000;
  cfg.quietThresholdPercent = 10.0f;
  AudioClassifier classifier(cfg);

  AudioFrame frame;
  // Sustained sound for 1500ms
  for (uint32_t t = 0; t <= 1500; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 35.0f;
    classifier.processFrame(frame);
  }

  TEST_ASSERT_EQUAL_UINT8(SoundPresence::SOUND, classifier.getPresence());
  TEST_ASSERT_TRUE(classifier.isSoundDetected());
}

void test_continuous_music_does_not_swallow_itself(void) {
  // Regression test for finding D1:
  // When music plays continuously for 10 minutes, the noise floor must NOT rise
  // to match the music level and cause the music presence to drop out.
  ClassifierConfig cfg;
  cfg.onDelayMs = 1000;
  cfg.offDelayMs = 5000;
  cfg.quietThresholdPercent = 8.0f;
  cfg.marginAboveFloorPercent = 4.0f;
  AudioClassifier classifier(cfg);

  AudioFrame frame;
  // First 60s of quiet baseline (2%)
  for (uint32_t t = 0; t <= 60000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 2.0f;
    classifier.processFrame(frame);
  }
  TEST_ASSERT_EQUAL_UINT8(SoundPresence::QUIET, classifier.getPresence());

  // 10 minutes (600,000 ms) of continuous music (average 30% volume)
  uint32_t startMusic = 60000;
  for (uint32_t t = startMusic; t <= startMusic + 600000; t += 20) {
    frame.timestampMs = t;
    // Vary between 20% and 40% with simulated dynamic peaks
    frame.levelPercent = 25.0f + 10.0f * (float)((t / 20) % 2);
    classifier.processFrame(frame);

    // At no point during continuous playback should presence be lost!
    if (t > startMusic + 2000) {
      if (!classifier.isSoundDetected()) {
        TEST_FAIL_MESSAGE("Presence dropped out during continuous music playback!");
      }
    }
  }

  TEST_ASSERT_TRUE(classifier.isSoundDetected());
}

void test_short_pause_does_not_flap_presence(void) {
  // Off-debounce test: a 3-second quiet gap between songs
  // should not flap presence when offDelayMs = 8000.
  ClassifierConfig cfg;
  cfg.onDelayMs = 1000;
  cfg.offDelayMs = 8000;
  cfg.quietThresholdPercent = 8.0f;
  AudioClassifier classifier(cfg);

  AudioFrame frame;
  // 5s of sound
  for (uint32_t t = 0; t <= 5000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 30.0f;
    classifier.processFrame(frame);
  }
  TEST_ASSERT_TRUE(classifier.isSoundDetected());

  // 3s pause (silence between tracks)
  for (uint32_t t = 5020; t <= 8000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 2.0f;
    classifier.processFrame(frame);
  }
  // Must still be SOUND because offDelay is 8000ms
  TEST_ASSERT_TRUE(classifier.isSoundDetected());

  // Prolonged silence > 8s
  for (uint32_t t = 8020; t <= 15000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 2.0f;
    classifier.processFrame(frame);
  }
  // Now should transition to QUIET
  TEST_ASSERT_FALSE(classifier.isSoundDetected());
}

void test_experimental_beat_and_phrasing(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  cfg.onDelayMs = 500;
  cfg.quietThresholdPercent = 6.0f;
  AudioClassifier classifier(cfg);

  AudioFrame frame;
  // Simulate 120 BPM steady beat (500ms intervals between beats) with strong bass
  uint32_t lastBeat = 0;
  for (uint32_t t = 0; t <= 8000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 35.0f;
    frame.bands[0] = 120; // Bass
    frame.bands[1] = 100;
    frame.bands[2] = 80;
    frame.bands[8] = 40;  // Mid

    if (t - lastBeat >= 500) {
      frame.isPeak = true;
      lastBeat = t;
    } else {
      frame.isPeak = false;
    }

    classifier.processFrame(frame);
  }

  TEST_ASSERT_TRUE(classifier.isSoundDetected());
  TEST_ASSERT_EQUAL_STRING("music", classifier.getSemanticStateStr());
  TEST_ASSERT_TRUE(classifier.getMusicConfidence() >= 0.6f);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_initial_state_is_quiet_and_silence);
  RUN_TEST(test_transient_noise_ignored);
  RUN_TEST(test_sustained_sound_triggers_presence);
  RUN_TEST(test_continuous_music_does_not_swallow_itself);
  RUN_TEST(test_short_pause_does_not_flap_presence);
  RUN_TEST(test_experimental_beat_and_phrasing);
  return UNITY_END();
}
