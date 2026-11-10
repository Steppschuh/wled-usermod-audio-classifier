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
  TEST_ASSERT_EQUAL_STRING("Silence", classifier.getSemanticStateStr());
  TEST_ASSERT_EQUAL_STRING("Unknown", classifier.getPhraseStateStr());
  TEST_ASSERT_EQUAL_UINT8(MusicPhrase::UNKNOWN, classifier.getPhraseState());
  TEST_ASSERT_EQUAL_UINT8(0, classifier.getSoundEnergy());
  TEST_ASSERT_EQUAL_STRING("silence", classifier.getSoundEnergyStr());
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
  TEST_ASSERT_EQUAL_STRING("Music", classifier.getSemanticStateStr());
  TEST_ASSERT_TRUE(classifier.getMusicConfidence() >= 0.6f);
  TEST_ASSERT_UINT16_WITHIN(5, 120, classifier.getBPM());
  TEST_ASSERT_EQUAL_UINT8(100, classifier.getSoundEnergy()); // Climaxes to Chorus / Drop on initial bass explosion
  TEST_ASSERT_EQUAL_STRING("chorus", classifier.getSoundEnergyStr());
  TEST_ASSERT_EQUAL_STRING("Chorus", classifier.getPhraseStateStr());

  // Continue steady groove past the initial 8-bar phrase (> 16s at 120 BPM)
  for (uint32_t t = 8020; t <= 26000; t += 20) {
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

  // Now smoothly stabilizes into VERSE (65% - 78% window, steady)
  TEST_ASSERT_TRUE(classifier.getSoundEnergy() >= 65 && classifier.getSoundEnergy() <= 78);
  TEST_ASSERT_EQUAL_STRING("verse", classifier.getSoundEnergyStr());
  TEST_ASSERT_EQUAL_STRING("Verse", classifier.getPhraseStateStr());
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
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 20;
    if (t - lastBeat >= 500) {
      frame.isPeak = true;
      frame.bands[0] = 140;
      frame.bands[1] = 120;
      frame.bands[2] = 80;
      lastBeat = t;
    } else {
      frame.isPeak = false;
    }
    frame.bands[8] = 40;
    classifier.processFrame(frame);
  }
  TEST_ASSERT_EQUAL_STRING("Music", classifier.getSemanticStateStr());
  TEST_ASSERT_UINT16_WITHIN(5, 120, classifier.getBPM());

  // 2. 25-second breakdown into outro: bass drops completely out, no beat peaks, quiet ambient synth/vocal
  // Even with low volume and zero beat peaks, semanticState MUST NOT flap to ambient_noise,
  // and BPM must be held at the last known track tempo (~120 BPM) rather than dropping to 0!
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
  TEST_ASSERT_EQUAL_STRING("Music", classifier.getSemanticStateStr());
  TEST_ASSERT_TRUE(sawBreakdownPhrase);
  TEST_ASSERT_TRUE(sawOutroPhrase);
  TEST_ASSERT_TRUE(classifier.getSoundEnergy() >= 30 && classifier.getSoundEnergy() <= 40);
  TEST_ASSERT_EQUAL_STRING("Outro", classifier.getPhraseStateStr());
  // BPM must remain held throughout breakdown and outro
  TEST_ASSERT_UINT16_WITHIN(5, 120, classifier.getBPM());

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
  TEST_ASSERT_EQUAL_STRING("Ambient Noise", classifier.getSemanticStateStr());
  // When leaving music state, BPM must reset to 0
  TEST_ASSERT_EQUAL_UINT16(0, classifier.getBPM());
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

void test_replay_recorded_adjacent_room_music_session(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  cfg.musicHoldMs = 30000;
  AudioClassifier classifier(cfg);

  bool ok = FixtureReplayer::replayAndVerify("test/fixtures/session_20261104_201823.json", classifier);
  TEST_ASSERT_TRUE(ok);
}

void test_replay_recorded_speech_session(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  cfg.musicHoldMs = 30000;
  AudioClassifier classifier(cfg);

  bool ok = FixtureReplayer::replayAndVerify("test/fixtures/session_20261104_210347.json", classifier);
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

  // 4. Mids appearing without bass (e.g. vocals/guitars in bridge/intro/outro)
  // Low frequencies bands 0 & 1 are 0, while mids are loud
  for (uint32_t t = 10000; t < 15000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 50.0f;
    frame.rawVolume = 120.0f;
    frame.bands[0] = 5;
    frame.bands[1] = 5;
    for (size_t b = 4; b < 10; b++) {
      frame.bands[b] = 160; // Loud mids
    }
    classifier.processFrame(frame);
  }
  // Mids without bass -> beat probability MUST be low (<= 0.15)
  TEST_ASSERT_TRUE(classifier.getBeatProbability() <= 0.15f);
}

void test_recorded_session_beat_probability(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  cfg.musicHoldMs = 30000;
  AudioClassifier classifier(cfg);

  // Replay 3-minute recording with beat probability assertions
  bool ok = FixtureReplayer::replayAndVerify("test/fixtures/session_20261104_113800.json", classifier);
  TEST_ASSERT_TRUE(ok);
}

void test_outlier_soft_bass_hit_before_drop_reverts(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  AudioClassifier classifier(cfg);
  AudioFrame frame;

  // 1. Establish music presence with steady groove for 3 seconds
  uint32_t lastBeat = 0;
  for (uint32_t t = 0; t <= 3000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 28.0f;
    frame.rawVolume = 70.0f;
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 20;
    if (t - lastBeat >= 500) {
      frame.isPeak = true;
      frame.bands[0] = 55;
      frame.bands[1] = 40;
      lastBeat = t;
    } else {
      frame.isPeak = false;
    }
    classifier.processFrame(frame);
  }
  TEST_ASSERT_EQUAL_STRING("Music", classifier.getSemanticStateStr());

  // 2. Transition into a buildup riser for 4 seconds (mids/highs rise, kick stops)
  for (uint32_t t = 3020; t <= 7000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 25.0f;
    frame.rawVolume = 65.0f;
    frame.isPeak = false;
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = (b >= 4) ? 80 : 10;
    classifier.processFrame(frame);
  }
  TEST_ASSERT_EQUAL_STRING("Music", classifier.getSemanticStateStr());

  // 3. Simulate 1 premature soft bass hit (outlier 1 bar before drop)
  frame.timestampMs = 7020;
  frame.levelPercent = 35.0f;
  frame.rawVolume = 90.0f;
  frame.isPeak = true;
  frame.bands[0] = 70;
  frame.bands[1] = 60;
  classifier.processFrame(frame);

  // 4. For the next 1400ms (~2-3 beats at 120 BPM), no second beat arrives and energy remains at buildup level
  for (uint32_t t = 7040; t <= 8500; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 20.0f;
    frame.rawVolume = 50.0f;
    frame.isPeak = false;
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = (b >= 4) ? 80 : 10;
    classifier.processFrame(frame);
  }

  // The isolated hit must be recognized as an outlier and reverted back to non-chorus (buildup/intro/breakdown)
  // and the 8-bar chorus lock must NOT keep it in chorus!
  TEST_ASSERT_NOT_EQUAL_MESSAGE(100, classifier.getSoundEnergy(), "Should not be locked in chorus after single outlier hit");
}

void test_outlier_missing_beat_in_phrase_reverts(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  AudioClassifier classifier(cfg);
  AudioFrame frame;

  // 1. Establish strong driving 120 BPM chorus groove for 6 seconds
  uint32_t lastBeat = 0;
  for (uint32_t t = 0; t <= 6000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 55.0f;
    frame.rawVolume = 140.0f;
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 30;

    if (t - lastBeat >= 500) {
      frame.isPeak = true;
      frame.bands[0] = 160;
      frame.bands[1] = 140;
      lastBeat = t;
    } else {
      frame.isPeak = false;
    }
    classifier.processFrame(frame);
  }

  TEST_ASSERT_EQUAL_STRING("Chorus", classifier.getPhraseStateStr());
  TEST_ASSERT_EQUAL_UINT8(100, classifier.getSoundEnergy());

  // 2. Simulate 2 missing beats (tension gap in bar 8, kick drum muted for 800ms)
  for (uint32_t t = 6020; t <= 6800; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 25.0f;
    frame.rawVolume = 60.0f;
    frame.isPeak = false;
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = (b >= 4) ? 50 : 10;
    classifier.processFrame(frame);
  }

  // 3. Beat resumes immediately on beat 1 with full bass
  for (uint32_t t = 6820; t <= 7500; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 55.0f;
    frame.rawVolume = 140.0f;
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 30;

    if (t == 6820 || t == 7320) {
      frame.isPeak = true;
      frame.bands[0] = 160;
      frame.bands[1] = 140;
    } else {
      frame.isPeak = false;
    }
    classifier.processFrame(frame);
  }

  // The 2 missing beats must be recognized as an outlier gap and reverted back to chorus!
  TEST_ASSERT_EQUAL_STRING("Chorus", classifier.getPhraseStateStr());
  TEST_ASSERT_EQUAL_UINT8(100, classifier.getSoundEnergy());
}

void test_moderate_volume_with_band1_kick_and_vocal_mids_detected(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  AudioClassifier classifier(cfg);
  AudioFrame frame;

  // 5 seconds of 120 BPM kicks, moderate volume (lvl 30%, raw 45), kick in band 1 (b0=25, b1=75), vocals in mids
  for (uint32_t t = 0; t <= 5000; t += 20) {
    frame.timestampMs = t;
    bool isKick = (t % 500) < 40;
    frame.levelPercent = isKick ? 30.0f : 20.0f;
    frame.rawVolume = isKick ? 45.0f : 35.0f;
    frame.smoothedVolume = 40.0f;
    frame.isPeak = isKick;
    frame.majorPeakHz = 80.0f;
    frame.peakMagnitude = 100.0f;

    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 0;
    if (isKick) {
      frame.bands[0] = 25;
      frame.bands[1] = 75;
      frame.bands[2] = 40;
      frame.bands[3] = 45;
      frame.bands[4] = 40;
    } else {
      frame.bands[0] = 10;
      frame.bands[1] = 20;
      frame.bands[2] = 35;
      frame.bands[3] = 40;
      frame.bands[4] = 35;
    }
    classifier.processFrame(frame);
  }

  // Must not have false negatives: beats must be detected, BPM locked, beat probability high
  TEST_ASSERT_EQUAL_STRING("Music", classifier.getSemanticStateStr());
  TEST_ASSERT_TRUE_MESSAGE(classifier.getBeatProbability() >= 0.70f, "Beat probability should be high during active groove");
  TEST_ASSERT_INT_WITHIN_MESSAGE(10, 120, classifier.getBPM(), "BPM should lock onto 120 BPM groove");
}

void test_isolated_single_bass_hit_does_not_yield_high_beat_probability(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  AudioClassifier classifier(cfg);
  AudioFrame frame;

  // 1. Initial 2 seconds of silence
  for (uint32_t t = 0; t < 2000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 2.0f;
    frame.rawVolume = 5.0f;
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 0;
    classifier.processFrame(frame);
  }

  // 2. One single isolated bass boom (sound effect or door slam)
  frame.timestampMs = 2000;
  frame.levelPercent = 35.0f;
  frame.rawVolume = 70.0f;
  frame.isPeak = true;
  frame.bands[0] = 60;
  frame.bands[1] = 80;
  classifier.processFrame(frame);

  // 3. Followed by 1500ms of quiet audio
  for (uint32_t t = 2020; t <= 3500; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 2.0f;
    frame.rawVolume = 5.0f;
    frame.isPeak = false;
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 0;
    classifier.processFrame(frame);
  }

  // 1 isolated hit cannot establish a groove -> beat probability must be low (<= 0.20)
  TEST_ASSERT_TRUE_MESSAGE(classifier.getBeatProbability() <= 0.20f, "Single isolated bass hit must not yield high beat probability");
}

void test_non_music_state_has_zero_energy_and_unknown_phrase_and_silence_clear_coupling(void) {
  ClassifierConfig cfg;
  cfg.onDelayMs = 500;
  cfg.offDelayMs = 2000;
  cfg.quietThresholdPercent = 5.0f;
  cfg.marginAboveFloorPercent = 4.0f;
  cfg.experimentalEnabled = true;
  AudioClassifier classifier(cfg);

  // 1. Initial State: Silence <==> Clear
  TEST_ASSERT_EQUAL_UINT8(SoundPresence::QUIET, classifier.getPresence());
  TEST_ASSERT_FALSE(classifier.isSoundDetected());
  TEST_ASSERT_EQUAL_STRING("Silence", classifier.getSemanticStateStr());
  TEST_ASSERT_EQUAL_UINT8(AudioSemanticState::SILENCE, classifier.getSemanticState());
  TEST_ASSERT_EQUAL_STRING("Unknown", classifier.getPhraseStateStr());
  TEST_ASSERT_EQUAL_UINT8(MusicPhrase::UNKNOWN, classifier.getPhraseState());
  TEST_ASSERT_EQUAL_UINT8(0, classifier.getSoundEnergy());
  TEST_ASSERT_EQUAL_STRING("silence", classifier.getSoundEnergyStr());

  // 2. Feed 1.5s of non-music ambient sound (e.g. talking / clatter on mid-bands, no beat regularity)
  AudioFrame frame;
  for (uint32_t t = 20; t <= 1500; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 25.0f;
    frame.rawVolume = 30.0f;
    frame.smoothedVolume = 25.0f;
    frame.isPeak = false;
    frame.majorPeakHz = 800.0f;
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 0;
    frame.bands[4] = 40; // Mid frequencies only
    classifier.processFrame(frame);
  }

  // After 1.5s (> onDelayMs of 500ms), presence is SOUND, semantic state is AMBIENT_NOISE
  TEST_ASSERT_EQUAL_UINT8(SoundPresence::SOUND, classifier.getPresence());
  TEST_ASSERT_TRUE(classifier.isSoundDetected());
  TEST_ASSERT_EQUAL_STRING("Ambient Noise", classifier.getSemanticStateStr());
  TEST_ASSERT_EQUAL_UINT8(AudioSemanticState::AMBIENT_NOISE, classifier.getSemanticState());
  // Because Audio Type is not Music: Music Energy must be 0 and Music Phrase must be Unknown
  TEST_ASSERT_EQUAL_STRING("Unknown", classifier.getPhraseStateStr());
  TEST_ASSERT_EQUAL_UINT8(MusicPhrase::UNKNOWN, classifier.getPhraseState());
  TEST_ASSERT_EQUAL_UINT8(0, classifier.getSoundEnergy());
  TEST_ASSERT_EQUAL_STRING("silence", classifier.getSoundEnergyStr());

  // 3. Audio drops to silence for 3s (> offDelayMs of 2000ms)
  for (uint32_t t = 1520; t <= 4500; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 1.0f;
    frame.rawVolume = 2.0f;
    frame.smoothedVolume = 1.0f;
    frame.isPeak = false;
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 0;
    classifier.processFrame(frame);
  }

  // Audio Type is Silence <==> Audio Detected is Clear
  TEST_ASSERT_EQUAL_UINT8(SoundPresence::QUIET, classifier.getPresence());
  TEST_ASSERT_FALSE(classifier.isSoundDetected());
  TEST_ASSERT_EQUAL_STRING("Silence", classifier.getSemanticStateStr());
  TEST_ASSERT_EQUAL_UINT8(AudioSemanticState::SILENCE, classifier.getSemanticState());
  TEST_ASSERT_EQUAL_STRING("Unknown", classifier.getPhraseStateStr());
  TEST_ASSERT_EQUAL_UINT8(MusicPhrase::UNKNOWN, classifier.getPhraseState());
  TEST_ASSERT_EQUAL_UINT8(0, classifier.getSoundEnergy());
  TEST_ASSERT_EQUAL_STRING("silence", classifier.getSoundEnergyStr());
}

void test_phrase_penalty_calculator_penalizes_rapid_phrase_churn(void) {
  PhrasePenaltyCalculator calc;

  // Case 1: Ideal macro structure progression:
  // Verse (0 to 30s) -> Chorus (30s to 60s) -> Breakdown (60s to 90s) -> Chorus (90s to 120s) -> Outro (120s to 150s)
  calc.recordFrame(0.0f, "verse");
  calc.recordFrame(15.0f, "verse");
  calc.recordFrame(30.0f, "chorus");
  calc.recordFrame(45.0f, "chorus");
  calc.recordFrame(60.0f, "breakdown");
  calc.recordFrame(75.0f, "breakdown");
  calc.recordFrame(90.0f, "chorus");
  calc.recordFrame(105.0f, "chorus");
  calc.recordFrame(120.0f, "outro");
  calc.recordFrame(150.0f, "outro");

  PhrasePenaltyMetrics cleanMetrics = calc.getMetrics();
  TEST_ASSERT_EQUAL_UINT32(0, cleanMetrics.microFlips);
  TEST_ASSERT_EQUAL_UINT32(0, cleanMetrics.rapidFlips);
  TEST_ASSERT_EQUAL_UINT32(0, cleanMetrics.pingPongs);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, cleanMetrics.penaltyScore);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, cleanMetrics.stabilityScore);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 30.0f, cleanMetrics.minDwellSec);

  // Case 2: Frequent rapid phrase churn (jumping every second between Chorus and Verse):
  calc.reset();
  calc.recordFrame(0.0f, "chorus");
  calc.recordFrame(1.0f, "verse");    // dwell 1.0s -> micro-flip!
  calc.recordFrame(2.0f, "chorus");   // dwell 1.0s -> micro-flip + ping-pong!
  calc.recordFrame(3.0f, "verse");    // dwell 1.0s -> micro-flip + ping-pong!
  calc.recordFrame(4.0f, "chorus");   // dwell 1.0s -> micro-flip + ping-pong!
  calc.recordFrame(5.0f, "verse");    // dwell 1.0s -> micro-flip + ping-pong!
  calc.recordFrame(6.0f, "chorus");   // dwell 1.0s -> micro-flip + ping-pong!

  PhrasePenaltyMetrics churnMetrics = calc.getMetrics();
  TEST_ASSERT_TRUE_MESSAGE(churnMetrics.microFlips >= 5, "Micro-flips must be detected for 1-second jumps");
  TEST_ASSERT_TRUE_MESSAGE(churnMetrics.pingPongs >= 4, "Ping-pong pattern must be detected");
  TEST_ASSERT_TRUE_MESSAGE(churnMetrics.penaltyScore >= 70.0f, "Penalty score must be heavy for rapid churn");
  TEST_ASSERT_TRUE_MESSAGE(churnMetrics.stabilityScore <= 30.0f, "Stability score must drop heavily for rapid churn");
}

void test_edm_macro_structure_phrase_progression_and_anti_churn(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  cfg.musicHoldMs = 35000;
  AudioClassifier classifier(cfg);
  PhrasePenaltyCalculator phraseCalc;
  AudioFrame frame;

  // 128 BPM: 1 beat = 469ms. 1 bar = 1875ms.
  const uint32_t beatMs = 469;
  uint32_t lastBeatTime = 0;
  uint32_t lastSnareTime = 0;

  bool sawIntroOrVerse = false;
  bool sawBreakdown1 = false;
  bool sawBuildup1 = false;
  bool sawChorus1 = false;
  bool sawBreakdown2 = false;
  bool sawBuildup2 = false;
  bool sawChorus2 = false;
  bool sawOutro = false;

  // Total 210 seconds:
  // 00:00 - 00:30 (16 bars): Intro
  // 00:30 - 01:00 (16 bars): Breakdown 1
  // 01:00 - 01:15 ( 8 bars): Build-up 1
  // 01:15 - 01:45 (16 bars): Drop 1 / Chorus
  // 01:45 - 02:15 (16 bars): Breakdown 2 / Bridge
  // 02:15 - 02:30 ( 8 bars): Build-up 2
  // 02:30 - 03:00 (16 bars): Drop 2 / Climax Chorus
  // 03:00 - 03:30 (16 bars): Outro

  for (uint32_t t = 0; t <= 210000; t += 20) {
    frame.timestampMs = t;
    float timeSec = (float)t / 1000.0f;

    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 5;
    frame.isPeak = false;

    // 1. Intro (0 - 30s): Stripped-back steady 128 BPM groove, moderate 30% volume
    if (t < 30000) {
      frame.levelPercent = 30.0f;
      frame.rawVolume = 75.0f;
      frame.smoothedVolume = 75.0f;
      frame.majorPeakHz = 80.0f;
      for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 15;
      frame.bands[8] = 30; // light mids

      if (t - lastBeatTime >= beatMs) {
        lastBeatTime = t;
        frame.isPeak = true;
        frame.bands[0] = 130; // 128 BPM steady kick
        frame.bands[1] = 100;
        frame.bands[2] = 60;
      }
    }
    // 2. Breakdown 1 (30s - 60s): Drums drop out, atmospheric pad, low level (14%)
    else if (t < 60000) {
      frame.levelPercent = 14.0f;
      frame.rawVolume = 35.0f;
      frame.smoothedVolume = 35.0f;
      frame.isPeak = false; // Drums cut out!
      frame.majorPeakHz = 440.0f;
      frame.bands[0] = 5;   // bass silent
      frame.bands[1] = 5;
      frame.bands[7] = 20;  // warm synth pad
      frame.bands[8] = 25;
    }
    // 3. Build-up 1 (60s - 75s): Accelerating snare rolls, rising volume (15% -> 45%), rising pitch sweep
    else if (t < 75000) {
      float p = (float)(t - 60000) / 15000.0f; // 0.0 -> 1.0
      frame.levelPercent = 15.0f + (30.0f * p);
      frame.rawVolume = 38.0f + (80.0f * p);
      frame.smoothedVolume = frame.rawVolume;
      frame.majorPeakHz = 300.0f + (900.0f * p);
      frame.bands[0] = (uint8_t)(10.0f + 25.0f * p);
      frame.bands[1] = (uint8_t)(10.0f + 25.0f * p);
      frame.bands[10] = (uint8_t)(25.0f + 35.0f * p); // rising snare

      uint32_t snareRate = (p < 0.5f) ? beatMs : ((p < 0.8f) ? (beatMs / 2) : (beatMs / 4));
      if (t - lastSnareTime >= snareRate) {
        lastSnareTime = t;
        frame.isPeak = true;
        frame.bands[11] = 70;
      }
    }
    // 4. Drop 1 / Chorus (75s - 105s): Massive bass drop, driving 128 BPM kicks, peak 55% volume
    else if (t < 105000) {
      frame.levelPercent = 55.0f;
      frame.rawVolume = 140.0f;
      frame.smoothedVolume = 140.0f;
      frame.majorPeakHz = 60.0f;
      frame.bands[8] = 40;
      frame.bands[12] = 35; // hi-hats

      if (t - lastBeatTime >= beatMs) {
        lastBeatTime = t;
        frame.isPeak = true;
        frame.bands[0] = 150; // Heavy four-on-the-floor kick
        frame.bands[1] = 130;
        frame.bands[2] = 80;
      } else {
        frame.bands[0] = 30; // heavy sub-bass drone
        frame.bands[1] = 30;
      }
    }
    // 5. Breakdown 2 / Bridge (105s - 135s): Kick cuts out abruptly, atmospheric breather, 12% volume
    else if (t < 135000) {
      frame.levelPercent = 12.0f;
      frame.rawVolume = 30.0f;
      frame.smoothedVolume = 30.0f;
      frame.isPeak = false;
      frame.majorPeakHz = 520.0f;
      frame.bands[0] = 4;
      frame.bands[1] = 4;
      frame.bands[8] = 22;
    }
    // 6. Build-up 2 (135s - 150s): Second build-up riser with accelerating snare rolls
    else if (t < 150000) {
      float p = (float)(t - 135000) / 15000.0f;
      frame.levelPercent = 15.0f + (30.0f * p);
      frame.rawVolume = 38.0f + (80.0f * p);
      frame.smoothedVolume = frame.rawVolume;
      frame.majorPeakHz = 350.0f + (1000.0f * p);
      frame.bands[0] = (uint8_t)(10.0f + 25.0f * p);
      frame.bands[1] = (uint8_t)(10.0f + 25.0f * p);
      frame.bands[10] = (uint8_t)(25.0f + 35.0f * p);

      uint32_t snareRate = (p < 0.5f) ? beatMs : ((p < 0.8f) ? (beatMs / 2) : (beatMs / 4));
      if (t - lastSnareTime >= snareRate) {
        lastSnareTime = t;
        frame.isPeak = true;
        frame.bands[11] = 80;
      }
    }
    // 7. Drop 2 / Climax Chorus (150s - 180s): Climax drop, highest energy
    else if (t < 180000) {
      frame.levelPercent = 60.0f;
      frame.rawVolume = 155.0f;
      frame.smoothedVolume = 155.0f;
      frame.majorPeakHz = 55.0f;
      frame.bands[8] = 45;
      frame.bands[12] = 40;

      if (t - lastBeatTime >= beatMs) {
        lastBeatTime = t;
        frame.isPeak = true;
        frame.bands[0] = 160;
        frame.bands[1] = 140;
        frame.bands[2] = 90;
      } else {
        frame.bands[0] = 35;
        frame.bands[1] = 35;
      }
    }
    // 8. Outro (180s - 210s): Stripped-back mix-out decaying to silence
    else {
      float p = (float)(t - 180000) / 30000.0f; // 0.0 -> 1.0
      frame.levelPercent = fmaxf(1.0f, 15.0f * (1.0f - p));
      frame.rawVolume = frame.levelPercent * 2.55f;
      frame.smoothedVolume = frame.rawVolume;
      frame.majorPeakHz = 200.0f;
      frame.bands[0] = (uint8_t)(10.0f * (1.0f - p));
      frame.bands[8] = (uint8_t)(15.0f * (1.0f - p));
    }

    classifier.processFrame(frame);

    MusicPhrase phr = classifier.getPhraseState();
    if (t >= 5000 && t < 30000 && (phr == MusicPhrase::INTRO || phr == MusicPhrase::VERSE)) sawIntroOrVerse = true;
    if (t >= 35000 && t < 60000 && phr == MusicPhrase::BREAKDOWN) sawBreakdown1 = true;
    if (t >= 62000 && t < 75000 && phr == MusicPhrase::BUILDUP) sawBuildup1 = true;
    if (t >= 80000 && t < 105000 && phr == MusicPhrase::CHORUS) sawChorus1 = true;
    if (t >= 110000 && t < 135000 && phr == MusicPhrase::BREAKDOWN) sawBreakdown2 = true;
    if (t >= 137000 && t < 150000 && phr == MusicPhrase::BUILDUP) sawBuildup2 = true;
    if (t >= 155000 && t < 180000 && phr == MusicPhrase::CHORUS) sawChorus2 = true;
    if (t >= 195000 && t <= 210000 && phr == MusicPhrase::OUTRO) sawOutro = true;

    // Record into phrase penalty calculator
    phraseCalc.recordFrame(timeSec, classifier.getPhraseStateStr());
  }

  // 1. Verify that all 8 macro structure sections were reached
  TEST_ASSERT_TRUE_MESSAGE(sawIntroOrVerse, "EDM Intro must be detected as Intro or Verse");
  TEST_ASSERT_TRUE_MESSAGE(sawBreakdown1, "Breakdown 1 must be detected as Breakdown");
  TEST_ASSERT_TRUE_MESSAGE(sawBuildup1, "Build-up 1 must be detected as Buildup");
  TEST_ASSERT_TRUE_MESSAGE(sawChorus1, "Drop 1 must be detected as Chorus");
  TEST_ASSERT_TRUE_MESSAGE(sawBreakdown2, "Breakdown 2 / Bridge must be detected as Breakdown");
  TEST_ASSERT_TRUE_MESSAGE(sawBuildup2, "Build-up 2 must be detected as Buildup");
  TEST_ASSERT_TRUE_MESSAGE(sawChorus2, "Drop 2 must be detected as Chorus");
  TEST_ASSERT_TRUE_MESSAGE(sawOutro, "Outro must be detected as Outro");

  // 2. Verify phrase stability and absence of rapid churn
  PhrasePenaltyMetrics m = phraseCalc.getMetrics();
  std::cout << "\n========================================================================================\n"
            << "🎧 EDM Macro Structure Phrase Stability Report:\n"
            << "   Total transitions: " << m.totalTransitions << " across 210s track\n"
            << "   Micro-Flips (< 2s): " << m.microFlips << "\n"
            << "   Rapid-Flips (< 5s): " << m.rapidFlips << "\n"
            << "   Ping-Pongs (A->B->A in <= 4s): " << m.pingPongs << "\n"
            << "   Shortest phrase dwell: " << m.minDwellSec << "s | Avg dwell: " << m.avgDwellSec << "s\n"
            << "   Phrase Anti-Churn Score: " << m.stabilityScore << "% (Penalty: -" << m.penaltyScore << ")\n";
  phraseCalc.printHistory();
  std::cout << "========================================================================================\n";

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, m.microFlips, "No micro-flips (< 2s) allowed in EDM macro structure!");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, m.pingPongs, "No ping-pong oscillations allowed in EDM macro structure!");
  TEST_ASSERT_TRUE_MESSAGE(m.stabilityScore >= 85.0f, "EDM Macro phrase stability score must be >= 85%!");
}

void test_chorus_to_breakdown_does_not_flip_to_outro_one_second_later(void) {
  ClassifierConfig cfg;
  cfg.experimentalEnabled = true;
  cfg.onDelayMs = 500;
  cfg.offDelayMs = 8000;
  cfg.musicHoldMs = 30000;
  AudioClassifier classifier(cfg);

  AudioFrame frame;
  // 1. Establish strong Chorus for 8 seconds (128 BPM = ~468ms interval)
  uint32_t lastBeat = 0;
  for (uint32_t t = 0; t <= 8000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 50.0f;
    frame.rawVolume = 120.0f;
    frame.smoothedVolume = 120.0f;
    frame.majorPeakHz = 60.0f;
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 30;

    if (t - lastBeat >= 468) {
      frame.isPeak = true;
      frame.bands[0] = 180;
      frame.bands[1] = 160;
      frame.bands[2] = 120;
      lastBeat = t;
    } else {
      frame.isPeak = false;
    }
    classifier.processFrame(frame);
  }

  TEST_ASSERT_EQUAL_STRING("Chorus", classifier.getPhraseStateStr());
  TEST_ASSERT_EQUAL_UINT8(100, classifier.getSoundEnergy());

  // 2. Sudden transition to Breakdown: kicks drop out, volume drops to 4.5% (synth pad / room reverb).
  // Noise floor is typically ~2.0%, so 4.5% is quiet and previously was < noiseFloor + 3.0f,
  // which triggered a 1-second flip to Outro.
  // Verify that for the next 15 seconds, the classifier stays firmly in Breakdown.
  for (uint32_t t = 8020; t <= 23000; t += 20) {
    frame.timestampMs = t;
    frame.levelPercent = 4.5f;
    frame.rawVolume = 10.0f;
    frame.smoothedVolume = 10.0f;
    frame.isPeak = false;
    for (size_t b = 0; b < NUM_AUDIO_BANDS; b++) frame.bands[b] = 5;
    frame.bands[8] = 12; // subtle synth pad mid

    classifier.processFrame(frame);

    // After ~1.2 seconds (t = 9200), phrase should be Breakdown, NOT Outro!
    if (t == 9200) {
      TEST_ASSERT_EQUAL_STRING("Breakdown", classifier.getPhraseStateStr());
    }
    // At 5 seconds into breakdown (t = 13000):
    if (t == 13000) {
      TEST_ASSERT_EQUAL_STRING("Breakdown", classifier.getPhraseStateStr());
    }
    // At 10 seconds into breakdown (t = 18000):
    if (t == 18000) {
      TEST_ASSERT_EQUAL_STRING("Breakdown", classifier.getPhraseStateStr());
    }
    // Throughout this 15-second breakdown dwell, phrase must never be prematurely flipped to Outro
    TEST_ASSERT_NOT_EQUAL_MESSAGE(MusicPhrase::OUTRO, classifier.getPhraseState(),
                                  "Classifier flipped to Outro during breakdown dwell (< 20s)!");
  }

  TEST_ASSERT_EQUAL_STRING("Breakdown", classifier.getPhraseStateStr());
}

void test_clipping_detection_ignores_single_spike_and_triggers_on_repeated_or_constant(void) {
  AudioClassifier classifier;
  AudioFrame frame;

  // 1. Single loud spike (1.5 seconds of saturation): should NOT trigger clipping
  for (uint32_t t = 0; t <= 1500; t += 20) {
    frame.timestampMs = t;
    frame.rawVolume = 254.0f;
    frame.smoothedVolume = 255.0f;
    frame.levelPercent = 99.6f;
    classifier.processFrame(frame);
  }
  TEST_ASSERT_FALSE_MESSAGE(classifier.isClipping(), "Single spike should not trigger clipping");

  // Followed by 10 seconds of normal non-clipping audio (50% level)
  for (uint32_t t = 1520; t <= 12000; t += 20) {
    frame.timestampMs = t;
    frame.rawVolume = 128.0f;
    frame.smoothedVolume = 128.0f;
    frame.levelPercent = 50.2f;
    classifier.processFrame(frame);
  }
  TEST_ASSERT_FALSE(classifier.isClipping());

  // 2. Repeated beat-level clipping: 3 separate seconds with >= 15% clipped frames inside 10s window
  for (uint32_t sec = 0; sec < 5; sec++) {
    uint32_t baseT = 12020 + sec * 1000;
    bool clipThisSec = (sec == 0 || sec == 2 || sec == 4);
    for (uint32_t i = 0; i < 50; i++) {
      frame.timestampMs = baseT + i * 20;
      if (clipThisSec && i < 10) { // 20% clipped frames in this second
        frame.rawVolume = 254.0f;
        frame.smoothedVolume = 230.0f;
        frame.levelPercent = 99.6f;
      } else {
        frame.rawVolume = 180.0f;
        frame.smoothedVolume = 180.0f;
        frame.levelPercent = 70.6f;
      }
      classifier.processFrame(frame);
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(classifier.isClipping(), "Repeated clipping across 3 seconds should trigger clipping warning");

  // 3. Recovery: 10+ clean seconds should clear clipping warning
  for (uint32_t t = 17020; t <= 28500; t += 20) {
    frame.timestampMs = t;
    frame.rawVolume = 100.0f;
    frame.smoothedVolume = 100.0f;
    frame.levelPercent = 39.2f;
    classifier.processFrame(frame);
  }
  TEST_ASSERT_FALSE_MESSAGE(classifier.isClipping(), "Clipping warning should clear after 10 clean seconds");

  // 4. Constant clipping (>= 3.5 seconds continuously pinned) triggers and reset() clears it
  for (uint32_t t = 28520; t <= 32500; t += 20) {
    frame.timestampMs = t;
    frame.rawVolume = 255.0f;
    frame.smoothedVolume = 255.0f;
    frame.levelPercent = 100.0f;
    classifier.processFrame(frame);
  }
  TEST_ASSERT_TRUE_MESSAGE(classifier.isClipping(), "Constant clipping should trigger warning");

  classifier.reset();
  TEST_ASSERT_FALSE(classifier.isClipping());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_initial_state_is_quiet_and_silence);
  RUN_TEST(test_non_music_state_has_zero_energy_and_unknown_phrase_and_silence_clear_coupling);
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
  RUN_TEST(test_replay_recorded_adjacent_room_music_session);
  RUN_TEST(test_replay_recorded_speech_session);
  RUN_TEST(test_1s_average_and_peak_tracking);
  RUN_TEST(test_music_beat_probability);
  RUN_TEST(test_recorded_session_beat_probability);
  RUN_TEST(test_outlier_soft_bass_hit_before_drop_reverts);
  RUN_TEST(test_outlier_missing_beat_in_phrase_reverts);
  RUN_TEST(test_moderate_volume_with_band1_kick_and_vocal_mids_detected);
  RUN_TEST(test_isolated_single_bass_hit_does_not_yield_high_beat_probability);
  RUN_TEST(test_phrase_penalty_calculator_penalizes_rapid_phrase_churn);
  RUN_TEST(test_edm_macro_structure_phrase_progression_and_anti_churn);
  RUN_TEST(test_chorus_to_breakdown_does_not_flip_to_outro_one_second_later);
  RUN_TEST(test_clipping_detection_ignores_single_spike_and_triggers_on_repeated_or_constant);
  return UNITY_END();
}
