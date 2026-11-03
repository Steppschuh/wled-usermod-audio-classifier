// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#pragma once

#ifdef ARDUINO_ARCH_ESP32

#include "wled.h"
#include "audio_frame.h"

enum class AudioSourceStatus : uint8_t {
  OK = 0,
  AR_MISSING,
  AR_DISABLED,
  AR_AGC_ON,
  AR_RECEIVE_MODE,
  NO_SIGNAL
};

class AudioReactiveSource {
public:
  void init();
  void updateConfig(JsonObject& root);
  bool pollFrame(AudioFrame& outFrame);
  AudioSourceStatus getStatus() const { return status; }
  const char* getStatusDescription() const;
  bool isAudioReactiveActive() const;

private:
  AudioSourceStatus status = AudioSourceStatus::OK;
  bool arFound = false;
  bool arEnabled = true;
  bool arAgcOn = false;
  bool arReceiveMode = false;

  uint32_t lastSignalTime = 0;
  uint32_t lastPollTime = 0;
  bool latchedPeak = false;
};

#endif // ARDUINO_ARCH_ESP32
