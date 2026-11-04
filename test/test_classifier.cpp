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
  TEST_ASSERT_EQUAL_STRING("verse", classifier.getPhraseStateStr());
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
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 20;

    if (t - lastBeat >= 500) {
      frame.isPeak = true;
      frame.bands[0] = 140; // Bass transient
      frame.bands[1] = 120;
      frame.bands[2] = 80;
      lastBeat = t;
    } else {
      frame.isPeak = false;
    }
    frame.bands[8] = 40;  // Mid

    classifier.processFrame(frame);
  }

  TEST_ASSERT_TRUE(classifier.isSoundDetected());
  TEST_ASSERT_EQUAL_STRING("music", classifier.getSemanticStateStr());
  TEST_ASSERT_TRUE(classifier.getMusicConfidence() >= 0.6f);
  TEST_ASSERT_UINT16_WITHIN(5, 120, classifier.getBPM());
  TEST_ASSERT_EQUAL_UINT8(100, classifier.getSoundEnergy()); // Climaxes to Chorus / Drop on initial bass explosion
  TEST_ASSERT_EQUAL_STRING("chorus", classifier.getSoundEnergyStr());
  TEST_ASSERT_EQUAL_STRING("chorus", classifier.getPhraseStateStr());

  // Continue steady groove past the initial 8-bar phrase (> 16s at 120 BPM)
  for (uint32_t t = 8020; t <= 20000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 25.0f;
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 15;

    if (t - lastBeat >= 500) {
      frame.isPeak = true;
      frame.bands[0] = 60; // Moderate bass transient for verse
      frame.bands[1] = 45;
      frame.bands[2] = 30;
      lastBeat = t;
    } else {
      frame.isPeak = false;
    }
    frame.bands[8] = 30;
    classifier.processFrame(frame);
  }

  // Now smoothly stabilizes into VERSE (80%, steady)
  TEST_ASSERT_EQUAL_UINT8(80, classifier.getSoundEnergy());
  TEST_ASSERT_EQUAL_STRING("verse", classifier.getSoundEnergyStr());
  TEST_ASSERT_EQUAL_STRING("verse", classifier.getPhraseStateStr());
}

void test_130_bpm_eighth_note_rejection_and_stability(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  AudioClassifier classifier(cfg);

  AudioFrame frame;
  // 130 BPM: Quarter note beat = 461.5 ms. Eighth note = 230.8 ms.
  uint32_t lastKick = 0;
  uint32_t lastSnare = 230;
  uint16_t bpmSnapshots[5] = {0};
  size_t snapshotIdx = 0;

  for (uint32_t t = 0; t <= 12000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 38.0f;

    // Default spectrum
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 20;

    bool isKick = (t >= lastKick + 460);
    bool isSnare = (t >= lastSnare + 460);

    if (isKick) {
      // Kick: heavy bass transient
      frame.isPeak = true;
      frame.bands[0] = 140;
      frame.bands[1] = 120;
      frame.bands[2] = 90;
      lastKick = t;
    } else if (isSnare) {
      // Snare on eighth note: mid/high transient, low bass
      frame.isPeak = true;
      frame.bands[0] = 15;
      frame.bands[1] = 20;
      frame.bands[6] = 130;
      frame.bands[7] = 110;
      lastSnare = t;
    } else {
      frame.isPeak = false;
    }

    classifier.processFrame(frame);

    // Record BPM once every second during steady state (sec 7 to 11)
    if (t >= 7000 && t % 1000 == 0 && snapshotIdx < 5) {
      bpmSnapshots[snapshotIdx++] = classifier.getBPM();
    }
  }

  // Must detect ~130 BPM, NOT 220-260 BPM
  TEST_ASSERT_UINT16_WITHIN(5, 130, classifier.getBPM());

  // Verify second-by-second stability (no jitter)
  for (size_t i = 1; i < 5; i++) {
    TEST_ASSERT_EQUAL_UINT16(bpmSnapshots[0], bpmSnapshots[i]);
  }
}

void test_150_bpm_stability_and_no_octave_flapping(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  AudioClassifier classifier(cfg);

  AudioFrame frame;
  // 150 BPM: Quarter note beat = 400 ms.
  uint32_t lastBeat = 0;

  for (uint32_t t = 0; t <= 10000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 45.0f;
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 20;

    if (t - lastBeat >= 400) {
      frame.isPeak = true;
      frame.bands[0] = 140;
      frame.bands[1] = 120;
      frame.bands[2] = 90;
      lastBeat = t;
    } else {
      frame.isPeak = false;
    }

    classifier.processFrame(frame);
  }

  TEST_ASSERT_UINT16_WITHIN(5, 150, classifier.getBPM());
}

void test_music_hangover_persists_across_breakdown(void) {
  // Test case for user requirement:
  // When music transitions into a low-volume breakdown/buildup without bass/percussion,
  // it must stay in the music state for the 30s hangover duration and not flap to ambient_noise.
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  cfg.onDelayMs = 500;
  cfg.offDelayMs = 8000;
  cfg.quietThresholdPercent = 6.0f;
  cfg.musicHoldMs = 30000; // 30 seconds
  AudioClassifier classifier(cfg);

  AudioFrame frame;
  // 1. Initial 10s of steady music with strong bass and 120 BPM beat
  uint32_t lastBeat = 0;
  for (uint32_t t = 0; t <= 10000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 40.0f;
    frame.bands[0] = 130;
    frame.bands[1] = 110;
    frame.bands[8] = 40;
    if (t - lastBeat >= 500) {
      frame.isPeak = true;
      lastBeat = t;
    } else {
      frame.isPeak = false;
    }
    classifier.processFrame(frame);
  }
  TEST_ASSERT_EQUAL_STRING("music", classifier.getSemanticStateStr());

  // 2. 25-second breakdown into outro: bass drops completely out, no beat peaks, quiet ambient synth/vocal
  // Even with low volume and zero beat peaks, semanticState MUST NOT flap to ambient_noise!
  bool sawBreakdownPhrase = false;
  bool sawOutroPhrase = false;
  for (uint32_t t = 10020; t <= 35000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 14.0f; // low energy breakdown
    frame.bands[0] = 5;         // no bass
    frame.bands[1] = 5;
    frame.bands[8] = 20;        // subtle mids
    frame.isPeak = false;       // no beat peak
    classifier.processFrame(frame);

    if (classifier.getPhraseState() == MusicPhrase::BREAKDOWN) {
      sawBreakdownPhrase = true;
    }
    if (classifier.getPhraseState() == MusicPhrase::OUTRO) {
      sawOutroPhrase = true;
    }

    // At every frame within the 30s window, state must strictly remain "music"!
    if (classifier.getSemanticState() != AudioSemanticState::MUSIC) {
      TEST_FAIL_MESSAGE("Classifier flapped away from music during a 25s breakdown!");
    }
  }

  // Confirm state is still music at second 35 (25s into breakdown/outro)
  TEST_ASSERT_EQUAL_STRING("music", classifier.getSemanticStateStr());
  TEST_ASSERT_TRUE(sawBreakdownPhrase);
  TEST_ASSERT_TRUE(sawOutroPhrase);
  TEST_ASSERT_EQUAL_UINT8(30, classifier.getSoundEnergy());
  TEST_ASSERT_EQUAL_STRING("outro", classifier.getPhraseStateStr());

  // 3. After full 30s hold expires (> 10000 + 30000 = 40000ms), and continued ambient non-music:
  // Ambient noise has flat spectrum across 16 bands (bassRatio = 4/16 = 0.25)
  for (size_t i = 0; i < NUM_AUDIO_BANDS; i++) frame.bands[i] = 8;
  frame.isPeak = false;

  for (uint32_t t = 35020; t <= 45000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 14.0f; // still some sound (so presence is sound)
    classifier.processFrame(frame);
  }

  // Now, since 35 seconds of non-musical sound passed (> 30s hold), it cleanly decays to ambient_noise
  TEST_ASSERT_EQUAL_STRING("ambient_noise", classifier.getSemanticStateStr());
}

#include "fixture_loader.h"

void test_replay_recorded_music_session(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  cfg.musicHoldMs = 30000;
  AudioClassifier classifier(cfg);

  bool ok = FixtureReplayer::replayAndVerify("test/fixtures/session_20261104_030037.json", classifier);
  TEST_ASSERT_TRUE(ok);
}

void test_replay_recorded_ambient_noise_session(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  cfg.musicHoldMs = 30000;
  AudioClassifier classifier(cfg);

  bool ok = FixtureReplayer::replayAndVerify("test/fixtures/session_20261104_081914.json", classifier);
  TEST_ASSERT_TRUE(ok);
}

void test_replay_recorded_3min_phrase_changes_session(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  cfg.musicHoldMs = 30000;
  AudioClassifier classifier(cfg);

  bool ok = FixtureReplayer::replayAndVerify("test/fixtures/session_20261104_113800.json", classifier);
  TEST_ASSERT_TRUE(ok);
}

void test_1s_average_and_peak_tracking(void) {
  AudioClassifier classifier;
  AudioFrame frame;

  // Feed frames over 1000ms: 49 frames at 10% and 1 spike at 80%
  for (uint32_t t = 0; t < 1000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = (t == 500) ? 80.0f : 10.0f;
    classifier.processFrame(frame);
  }

  // Trigger the 1000ms tick
  frame.timestampMs = 1000;
  frame.levelPercent = 10.0f;
  classifier.processFrame(frame);

  // Peak should be 80.0%
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 80.0f, classifier.getPeakLevelPercent());
  // RMS Average: sqrt((49*10^2 + 80^2)/50) = sqrt((4900 + 6400)/50) = sqrt(226) ≈ 15.03%
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 15.03f, classifier.getAverageLevelPercent());
}

void test_music_beat_probability(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  AudioClassifier classifier(cfg);
  AudioFrame frame;

  // 1. Initial silence
  TEST_ASSERT_EQUAL_FLOAT(0.0f, classifier.getBeatProbability());

  // 2. Constant ambient noise with low frequency rumble (no variance / beat)
  for (uint32_t t = 0; t < 3000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 25.0f;
    frame.bands[0] = 50; // Constant flat rumble
    frame.bands[1] = 50;
    classifier.processFrame(frame);
  }
  // No rhythmic variance -> beat probability must be 0
  TEST_ASSERT_EQUAL_FLOAT(0.0f, classifier.getBeatProbability());

  // 3. Rhythmic beats at 128 BPM (interval ≈ 468ms) with high kick variance
  for (uint32_t t = 3000; t < 10000; t += 20) {
    frame.timestampMs = t;
    bool isKick = ((t - 3000) % 468) < 40;
    frame.levelPercent = isKick ? 60.0f : 15.0f;
    frame.rawVolume = isKick ? 150.0f : 20.0f;
    frame.bands[0] = isKick ? 180 : 10;
    frame.bands[1] = isKick ? 140 : 10;
    for (size_t b = 2; b < NUM_AUDIO_BANDS; b++) {
      frame.bands[b] = isKick ? 20 : 5;
    }
    classifier.processFrame(frame);
  }

  // Active kicks with variance -> beat probability must be high (> 0.8)
  TEST_ASSERT_TRUE(classifier.getBeatProbability() >= 0.80f);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_initial_state_is_quiet_and_silence);
  RUN_TEST(test_transient_noise_ignored);
  RUN_TEST(test_sustained_sound_triggers_presence);
  RUN_TEST(test_continuous_music_does_not_swallow_itself);
  RUN_TEST(test_short_pause_does_not_flap_presence);
  RUN_TEST(test_experimental_beat_and_phrasing);
  RUN_TEST(test_130_bpm_eighth_note_rejection_and_stability);
  RUN_TEST(test_150_bpm_stability_and_no_octave_flapping);
  RUN_TEST(test_music_hangover_persists_across_breakdown);
  RUN_TEST(test_replay_recorded_music_session);
  RUN_TEST(test_replay_recorded_ambient_noise_session);
  RUN_TEST(test_replay_recorded_3min_phrase_changes_session);
  RUN_TEST(test_1s_average_and_peak_tracking);
  RUN_TEST(test_music_beat_probability);
  return UNITY_END();
}
