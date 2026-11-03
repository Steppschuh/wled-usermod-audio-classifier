// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#ifdef ARDUINO_ARCH_ESP32

#include "ar_source.h"

void AudioReactiveSource::init() {
  Usermod* um = UsermodManager::lookup(USERMOD_ID_AUDIOREACTIVE);
  arFound = (um != nullptr);
  if (!arFound) {
    status = AudioSourceStatus::AR_MISSING;
  } else {
    status = AudioSourceStatus::OK;
  }
  lastSignalTime = millis();
}

void AudioReactiveSource::updateConfig(JsonObject& root) {
  JsonObject arTop = root[F("AudioReactive")];
  if (!arTop.isNull()) {
    arEnabled = arTop[F("enabled")] | true;
    JsonObject cfg = arTop[F("config")];
    if (!cfg.isNull()) {
      arAgcOn = (cfg[F("AGC")] | 0) != 0;
    }
    JsonObject sync = arTop[F("sync")];
    if (!sync.isNull()) {
      arReceiveMode = ((sync[F("mode")] | 0) & 0x02) != 0;
    }
  }
}

bool AudioReactiveSource::isAudioReactiveActive() const {
  return arFound && arEnabled;
}

const char* AudioReactiveSource::getStatusDescription() const {
  switch (status) {
    case AudioSourceStatus::OK:              return "OK (Coupled with AudioReactive)";
    case AudioSourceStatus::AR_MISSING:     return "Error: AudioReactive usermod not present in build";
    case AudioSourceStatus::AR_DISABLED:    return "Warning: AudioReactive is disabled in settings";
    case AudioSourceStatus::AR_AGC_ON:      return "Warning: AudioReactive AGC is enabled (relative levels will float)";
    case AudioSourceStatus::AR_RECEIVE_MODE:return "Info: AudioReactive is in network receive mode";
    case AudioSourceStatus::NO_SIGNAL:      return "Warning: No audio signal detected from AudioReactive (>30s)";
    default:                                return "Unknown";
  }
}

bool AudioReactiveSource::pollFrame(AudioFrame& outFrame) {
  uint32_t now = millis();
  
  um_data_t* umData = nullptr;
  if (!UsermodManager::getUMData(&umData, USERMOD_ID_AUDIOREACTIVE) || !umData) {
    if (!arFound) {
      status = AudioSourceStatus::AR_MISSING;
    } else {
      status = AudioSourceStatus::AR_DISABLED;
    }
    return false;
  }

  if (umData->u_size < 6 || !umData->u_data) {
    return false;
  }

  // Slot 0: volumeSmth (float)
  float smth = (umData->u_data[0]) ? *(reinterpret_cast<float*>(umData->u_data[0])) : 0.0f;
  
  // Slot 1: volumeRaw (int16_t or uint16_t)
  float raw = 0.0f;
  if (umData->u_data[1]) {
    raw = (float)(*(reinterpret_cast<int16_t*>(umData->u_data[1])));
    if (raw < 0.0f) raw = 0.0f;
    if (raw > 255.0f) raw = 255.0f;
  }

  // Slot 2: fftResult (uint8_t[16])
  uint8_t* bands = (umData->u_data[2]) ? reinterpret_cast<uint8_t*>(umData->u_data[2]) : nullptr;

  // Slot 3: samplePeak (bool)
  bool peak = (umData->u_data[3]) ? (*(reinterpret_cast<bool*>(umData->u_data[3])) != 0) : false;
  if (peak) {
    latchedPeak = true;
  }

  // Slot 4: FFT_MajorPeak (float)
  float majorPeak = (umData->u_data[4]) ? *(reinterpret_cast<float*>(umData->u_data[4])) : 0.0f;

  // Slot 5: my_magnitude (float)
  float magnitude = (umData->u_data[5]) ? *(reinterpret_cast<float*>(umData->u_data[5])) : 0.0f;

  // Rate limit frame emission to ~20ms
  if (now - lastPollTime < 20) {
    return false;
  }
  lastPollTime = now;

  // Signal activity check
  if (raw > 0.0f || smth > 0.0f) {
    lastSignalTime = now;
  }

  if (now - lastSignalTime > 30000) {
    status = AudioSourceStatus::NO_SIGNAL;
  } else if (arReceiveMode) {
    status = AudioSourceStatus::AR_RECEIVE_MODE;
  } else if (arAgcOn) {
    status = AudioSourceStatus::AR_AGC_ON;
  } else {
    status = AudioSourceStatus::OK;
  }

  outFrame.timestampMs = now;
  outFrame.rawVolume = raw;
  outFrame.smoothedVolume = smth;
  outFrame.levelPercent = (raw / 255.0f) * 100.0f;
  if (outFrame.levelPercent > 100.0f) outFrame.levelPercent = 100.0f;
  if (outFrame.levelPercent < 0.0f) outFrame.levelPercent = 0.0f;

  if (bands) {
    for (size_t i = 0; i < NUM_AUDIO_BANDS; i++) {
      outFrame.bands[i] = bands[i];
    }
  } else {
    for (size_t i = 0; i < NUM_AUDIO_BANDS; i++) {
      outFrame.bands[i] = 0;
    }
  }

  outFrame.isPeak = latchedPeak;
  latchedPeak = false; // Reset latch
  outFrame.majorPeakHz = majorPeak;
  outFrame.peakMagnitude = magnitude;

  return true;
}

#endif // ARDUINO_ARCH_ESP32
