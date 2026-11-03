// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#pragma once

#include <stdint.h>
#include <stddef.h>

static constexpr size_t NUM_AUDIO_BANDS = 16;

struct AudioFrame {
  uint32_t timestampMs = 0;
  float levelPercent = 0.0f;             // 0.0 to 100.0% relative volume
  float rawVolume = 0.0f;                // 0.0 to 255.0 from AudioReactive volumeRaw
  float smoothedVolume = 0.0f;           // Smoothed volume from AudioReactive
  uint8_t bands[NUM_AUDIO_BANDS] = {0};  // 16 GEQ frequency channel intensities
  bool isPeak = false;                   // Beat / sample peak flag
  float majorPeakHz = 0.0f;              // Strongest frequency detected by FFT
  float peakMagnitude = 0.0f;            // Magnitude of major peak
};
