// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "classifier.h"

enum class AutomationSlot : uint8_t {
  AMBIENT = 0,
  AR_LOW  = 1,
  AR_MID  = 2,
  AR_HIGH = 3,
  AR_MAX  = 4,
  COUNT   = 5
};

struct AutoPresetConfig {
  bool enabled = false;
  bool manualOverrideHold = true;
  uint16_t cooldownSec = 15;
  uint8_t presetOverrides[static_cast<size_t>(AutomationSlot::COUNT)] = {0, 0, 0, 0, 0};
};

class PresetPoolRegistry {
public:
  static constexpr size_t MAX_PRESETS_PER_SLOT = 16;
  static constexpr uint8_t MAX_PRESET_ID = 250;

  void clear();
  void markScanned() { scanned = true; }
  bool isScanned() const { return scanned; }

  void registerPreset(uint8_t id, const char* name);
  bool exists(uint8_t id) const;

  size_t getPoolCount(AutomationSlot slot) const;
  uint8_t getPoolPreset(AutomationSlot slot, size_t index) const;

  static bool matchSlotPrefix(const char* name, AutomationSlot& outSlot);
  static bool matchPrefixToken(const char* name, const char* prefix);

private:
  uint8_t pools[static_cast<size_t>(AutomationSlot::COUNT)][MAX_PRESETS_PER_SLOT] = {{0}};
  uint8_t poolCounts[static_cast<size_t>(AutomationSlot::COUNT)] = {0};
  bool validPresets[MAX_PRESET_ID + 1] = {false};
  bool scanned = false;
};

// Streaming single-pass JSON parser for WLED's /presets.json.
// Captures valid preset IDs (1..250) and top-level preset names ("n" at depth 2),
// while ignoring segment names ("seg":[{"n":...}] at depth >= 3) and dummy "0":{}.
class PresetScanParser {
public:
  void reset(PresetPoolRegistry& registry);
  void feed(const char* data, size_t len, PresetPoolRegistry& registry);
  void finish(PresetPoolRegistry& registry);

private:
  static constexpr size_t KEY_BUF_SIZE = 16;
  static constexpr size_t NAME_BUF_SIZE = 65;

  enum class CaptureTarget : uint8_t {
    NONE = 0,
    PRESET_ID_KEY,
    ENTRY_KEY,
    PRESET_NAME_VAL
  };

  void commitCurrentPreset(PresetPoolRegistry& registry);

  int depth = 0;
  bool inString = false;
  bool escaped = false;
  bool expectValueAtDepth2 = false;
  bool expectPresetObject = false;

  CaptureTarget capture = CaptureTarget::NONE;
  char keyBuf[KEY_BUF_SIZE] = {0};
  size_t keyLen = 0;

  int currentPresetId = -1;
  bool currentPresetHasContent = false;
  bool hasCurrentName = false;
  char nameBuf[NAME_BUF_SIZE] = {0};
  size_t nameLen = 0;
};

class AutoPresetController {
public:
  AutoPresetController(const AutoPresetConfig& config = AutoPresetConfig());

  void setConfig(const AutoPresetConfig& newConfig);
  const AutoPresetConfig& getConfig() const { return config; }

  void reset();
  void onManualStateChange();

  bool isManualOverrideActive() const { return manualOverride; }
  bool hasActiveSlot() const { return hasAppliedSlot; }
  AutomationSlot getActiveSlot() const { return lastResolvedSlot; }

  static AutomationSlot mapClassifierToSlot(AudioSemanticState semanticState, MusicPhrase phraseState);
  static const char* getSlotName(AutomationSlot slot);

  // Resolves a target preset ID for the desired slot (applying overrides, pool selection, and AR fallbacks).
  uint8_t resolvePresetForSlot(
      AutomationSlot desiredSlot,
      const PresetPoolRegistry& registry,
      uint8_t currentWledPresetId,
      uint32_t randomVal,
      AutomationSlot* outResolvedSlot = nullptr) const;

  // Evaluates whether an automated preset switch should happen now.
  // Returns 1..250 to switch preset, or 0 if no switch should occur.
  uint8_t update(
      uint32_t nowMs,
      bool classifierEnabled,
      AudioSemanticState semanticState,
      MusicPhrase phraseState,
      const PresetPoolRegistry& registry,
      uint8_t currentWledPresetId,
      int16_t currentWledPlaylistId,
      bool isStripOffOrNightlight,
      uint32_t randomVal);

private:
  AutoPresetConfig config;
  bool manualOverride = false;
  bool hasSwitchedOnce = false;
  uint32_t lastSwitchMs = 0;
  bool hasAppliedSlot = false;
  AutomationSlot lastResolvedSlot = AutomationSlot::AMBIENT;
  uint8_t lastPresetPerSlot[static_cast<size_t>(AutomationSlot::COUNT)] = {0, 0, 0, 0, 0};
};

