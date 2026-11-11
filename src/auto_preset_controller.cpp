// Copyright (c) 2026 Stephan Schultz
// SPDX-License-Identifier: EUPL-1.2

#include "auto_preset_controller.h"
#include <ctype.h>
#include <string.h>

void PresetPoolRegistry::clear() {
  for (size_t s = 0; s < static_cast<size_t>(AutomationSlot::COUNT); s++) {
    poolCounts[s] = 0;
    for (size_t i = 0; i < MAX_PRESETS_PER_SLOT; i++) {
      pools[s][i] = 0;
    }
  }
  for (size_t id = 0; id <= MAX_PRESET_ID; id++) {
    validPresets[id] = false;
  }
  scanned = false;
}

bool PresetPoolRegistry::exists(uint8_t id) const {
  if (id == 0 || id > MAX_PRESET_ID) return false;
  return validPresets[id];
}

size_t PresetPoolRegistry::getPoolCount(AutomationSlot slot) const {
  size_t idx = static_cast<size_t>(slot);
  if (idx >= static_cast<size_t>(AutomationSlot::COUNT)) return 0;
  return poolCounts[idx];
}

uint8_t PresetPoolRegistry::getPoolPreset(AutomationSlot slot, size_t index) const {
  size_t s = static_cast<size_t>(slot);
  if (s >= static_cast<size_t>(AutomationSlot::COUNT)) return 0;
  if (index >= poolCounts[s]) return 0;
  return pools[s][index];
}

bool PresetPoolRegistry::matchPrefixToken(const char* name, const char* prefix) {
  if (!name || !prefix) return false;

  while (*name != '\0' && isspace(static_cast<unsigned char>(*name))) {
    name++;
  }

  size_t i = 0;
  while (prefix[i] != '\0') {
    if (name[i] == '\0') return false;
    if (tolower(static_cast<unsigned char>(name[i])) != tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
    i++;
  }

  // Ensure token boundary so e.g. "AR High" matches "AR High: Strobe",
  // "AR High Strobe", "AR High - Strobe", or "AR High", but not "AR Higher".
  char next = name[i];
  if (next == '\0') return true;
  return !isalnum(static_cast<unsigned char>(next));
}

bool PresetPoolRegistry::matchSlotPrefix(const char* name, AutomationSlot& outSlot) {
  if (!name) return false;

  if (matchPrefixToken(name, "Ambient")) {
    outSlot = AutomationSlot::AMBIENT;
    return true;
  }
  if (matchPrefixToken(name, "AR Low")) {
    outSlot = AutomationSlot::AR_LOW;
    return true;
  }
  if (matchPrefixToken(name, "AR Mid")) {
    outSlot = AutomationSlot::AR_MID;
    return true;
  }
  if (matchPrefixToken(name, "AR High")) {
    outSlot = AutomationSlot::AR_HIGH;
    return true;
  }
  if (matchPrefixToken(name, "AR Max")) {
    outSlot = AutomationSlot::AR_MAX;
    return true;
  }
  return false;
}

void PresetPoolRegistry::registerPreset(uint8_t id, const char* name) {
  if (id == 0 || id > MAX_PRESET_ID) return;
  validPresets[id] = true;

  AutomationSlot slot;
  if (!matchSlotPrefix(name, slot)) return;

  size_t s = static_cast<size_t>(slot);
  for (size_t i = 0; i < poolCounts[s]; i++) {
    if (pools[s][i] == id) return;
  }
  if (poolCounts[s] < MAX_PRESETS_PER_SLOT) {
    pools[s][poolCounts[s]++] = id;
  }
}

void PresetScanParser::reset(PresetPoolRegistry& registry) {
  registry.clear();
  depth = 0;
  inString = false;
  escaped = false;
  expectValueAtDepth2 = false;
  expectPresetObject = false;
  capture = CaptureTarget::NONE;
  keyLen = 0;
  keyBuf[0] = '\0';
  currentPresetId = -1;
  currentPresetHasContent = false;
  hasCurrentName = false;
  nameLen = 0;
  nameBuf[0] = '\0';
}

void PresetScanParser::commitCurrentPreset(PresetPoolRegistry& registry) {
  if (currentPresetId >= 1 && currentPresetId <= PresetPoolRegistry::MAX_PRESET_ID && currentPresetHasContent) {
    registry.registerPreset(static_cast<uint8_t>(currentPresetId), hasCurrentName ? nameBuf : nullptr);
  }
  currentPresetId = -1;
  currentPresetHasContent = false;
  hasCurrentName = false;
  nameLen = 0;
  nameBuf[0] = '\0';
}

void PresetScanParser::feed(const char* data, size_t len, PresetPoolRegistry& registry) {
  if (!data || len == 0) return;

  for (size_t idx = 0; idx < len; idx++) {
    char ch = data[idx];

    if (inString) {
      if (escaped) {
        escaped = false;
        char decoded = ch;
        if (ch == 'n') decoded = '\n';
        else if (ch == 'r') decoded = '\r';
        else if (ch == 't') decoded = '\t';

        if (capture == CaptureTarget::ENTRY_KEY || capture == CaptureTarget::PRESET_ID_KEY) {
          if (keyLen < KEY_BUF_SIZE - 1) {
            keyBuf[keyLen++] = decoded;
          }
        } else if (capture == CaptureTarget::PRESET_NAME_VAL) {
          if (nameLen < NAME_BUF_SIZE - 1) {
            nameBuf[nameLen++] = decoded;
          }
        }
        continue;
      }

      if (ch == '\\') {
        escaped = true;
        continue;
      }

      if (ch == '"') {
        inString = false;
        if (capture == CaptureTarget::PRESET_ID_KEY) {
          keyBuf[keyLen] = '\0';
          bool allDigits = (keyLen > 0);
          long parsed = 0;
          for (size_t k = 0; k < keyLen; k++) {
            if (!isdigit(static_cast<unsigned char>(keyBuf[k]))) {
              allDigits = false;
              break;
            }
            parsed = parsed * 10 + (keyBuf[k] - '0');
            if (parsed > PresetPoolRegistry::MAX_PRESET_ID) {
              allDigits = false;
              break;
            }
          }
          currentPresetId = (allDigits && parsed >= 1 && parsed <= PresetPoolRegistry::MAX_PRESET_ID)
                              ? static_cast<int>(parsed)
                              : -1;
        } else if (capture == CaptureTarget::ENTRY_KEY) {
          keyBuf[keyLen] = '\0';
        } else if (capture == CaptureTarget::PRESET_NAME_VAL) {
          nameBuf[nameLen] = '\0';
          expectValueAtDepth2 = false;
        } else if (depth == 2 && expectValueAtDepth2) {
          expectValueAtDepth2 = false;
        }
        capture = CaptureTarget::NONE;
        continue;
      }

      if (capture == CaptureTarget::ENTRY_KEY || capture == CaptureTarget::PRESET_ID_KEY) {
        if (keyLen < KEY_BUF_SIZE - 1) {
          keyBuf[keyLen++] = ch;
        }
      } else if (capture == CaptureTarget::PRESET_NAME_VAL) {
        if (nameLen < NAME_BUF_SIZE - 1) {
          nameBuf[nameLen++] = ch;
        }
      }
      continue;
    }

    // Outside string literals
    if (isspace(static_cast<unsigned char>(ch))) {
      continue;
    }

    if (depth == 2 && ch != '}') {
      currentPresetHasContent = true;
    }

    if (ch == '"') {
      inString = true;
      escaped = false;
      if (depth == 1 && !expectPresetObject) {
        capture = CaptureTarget::PRESET_ID_KEY;
        keyLen = 0;
        keyBuf[0] = '\0';
      } else if (depth == 2 && !expectValueAtDepth2) {
        capture = CaptureTarget::ENTRY_KEY;
        keyLen = 0;
        keyBuf[0] = '\0';
      } else if (depth == 2 && expectValueAtDepth2 && strcmp(keyBuf, "n") == 0) {
        capture = CaptureTarget::PRESET_NAME_VAL;
        nameLen = 0;
        nameBuf[0] = '\0';
        hasCurrentName = true;
      } else {
        capture = CaptureTarget::NONE;
      }
      continue;
    }

    if (ch == '{') {
      if (depth == 1) {
        if (expectPresetObject) {
          expectPresetObject = false;
          expectValueAtDepth2 = false;
          currentPresetHasContent = false;
          hasCurrentName = false;
          nameLen = 0;
          nameBuf[0] = '\0';
        } else {
          currentPresetId = -1;
        }
      } else if (depth == 2) {
        expectValueAtDepth2 = false;
      }
      depth++;
      continue;
    }

    if (ch == '[') {
      if (depth == 1) {
        expectPresetObject = false;
        currentPresetId = -1;
      } else if (depth == 2) {
        expectValueAtDepth2 = false;
      }
      depth++;
      continue;
    }

    if (ch == '}') {
      if (depth == 2) {
        commitCurrentPreset(registry);
      }
      if (depth > 0) depth--;
      continue;
    }

    if (ch == ']') {
      if (depth > 0) depth--;
      continue;
    }

    if (depth == 1) {
      if (ch == ':') {
        expectPresetObject = true;
      } else if (ch == ',') {
        expectPresetObject = false;
        currentPresetId = -1;
      }
      continue;
    }

    if (depth == 2) {
      if (ch == ':') {
        expectValueAtDepth2 = true;
      } else if (ch == ',') {
        expectValueAtDepth2 = false;
        keyLen = 0;
        keyBuf[0] = '\0';
      }
    }
  }
}

void PresetScanParser::finish(PresetPoolRegistry& registry) {
  registry.markScanned();
}

AutoPresetController::AutoPresetController(const AutoPresetConfig& cfg)
  : config(cfg) {
}

void AutoPresetController::setConfig(const AutoPresetConfig& newConfig) {
  bool justEnabled = (!config.enabled && newConfig.enabled);
  config = newConfig;
  if (!config.enabled || justEnabled) {
    manualOverride = false;
    hasAppliedSlot = false;
    if (justEnabled) {
      hasSwitchedOnce = false;
    }
  }
  if (!config.manualOverrideHold) {
    manualOverride = false;
  }
}

void AutoPresetController::reset() {
  manualOverride = false;
  hasSwitchedOnce = false;
  lastSwitchMs = 0;
  hasAppliedSlot = false;
  lastResolvedSlot = AutomationSlot::AMBIENT;
  for (size_t i = 0; i < static_cast<size_t>(AutomationSlot::COUNT); i++) {
    lastPresetPerSlot[i] = 0;
  }
}

void AutoPresetController::onManualStateChange() {
  if (!config.enabled || !config.manualOverrideHold) return;
  manualOverride = true;
  hasAppliedSlot = false;
}

AutomationSlot AutoPresetController::mapClassifierToSlot(AudioSemanticState semanticState, MusicPhrase phraseState) {
  if (semanticState != AudioSemanticState::MUSIC) {
    return AutomationSlot::AMBIENT;
  }

  switch (phraseState) {
    case MusicPhrase::BREAKDOWN:
    case MusicPhrase::INTRO:
    case MusicPhrase::OUTRO:
    case MusicPhrase::UNKNOWN:
      return AutomationSlot::AR_LOW;
    case MusicPhrase::VERSE:
      return AutomationSlot::AR_MID;
    case MusicPhrase::BUILDUP:
      return AutomationSlot::AR_HIGH;
    case MusicPhrase::CHORUS:
      return AutomationSlot::AR_MAX;
    default:
      return AutomationSlot::AR_LOW;
  }
}

const char* AutoPresetController::getSlotName(AutomationSlot slot) {
  switch (slot) {
    case AutomationSlot::AMBIENT: return "Ambient";
    case AutomationSlot::AR_LOW:  return "AR Low";
    case AutomationSlot::AR_MID:  return "AR Mid";
    case AutomationSlot::AR_HIGH: return "AR High";
    case AutomationSlot::AR_MAX:  return "AR Max";
    default:                      return "Unknown";
  }
}

uint8_t AutoPresetController::resolvePresetForSlot(
    AutomationSlot desiredSlot,
    const PresetPoolRegistry& registry,
    uint8_t currentWledPresetId,
    uint32_t randomVal,
    AutomationSlot* outResolvedSlot) const {

  static const AutomationSlot kFallbackAmbient[] = { AutomationSlot::AMBIENT };
  static const AutomationSlot kFallbackLow[]     = { AutomationSlot::AR_LOW, AutomationSlot::AR_MID, AutomationSlot::AR_HIGH, AutomationSlot::AR_MAX };
  static const AutomationSlot kFallbackMid[]     = { AutomationSlot::AR_MID, AutomationSlot::AR_LOW, AutomationSlot::AR_HIGH, AutomationSlot::AR_MAX };
  static const AutomationSlot kFallbackHigh[]    = { AutomationSlot::AR_HIGH, AutomationSlot::AR_MID, AutomationSlot::AR_MAX, AutomationSlot::AR_LOW };
  static const AutomationSlot kFallbackMax[]     = { AutomationSlot::AR_MAX, AutomationSlot::AR_HIGH, AutomationSlot::AR_MID, AutomationSlot::AR_LOW };

  const AutomationSlot* chain = kFallbackAmbient;
  size_t chainLen = 1;

  switch (desiredSlot) {
    case AutomationSlot::AMBIENT:
      chain = kFallbackAmbient;
      chainLen = 1;
      break;
    case AutomationSlot::AR_LOW:
      chain = kFallbackLow;
      chainLen = 4;
      break;
    case AutomationSlot::AR_MID:
      chain = kFallbackMid;
      chainLen = 4;
      break;
    case AutomationSlot::AR_HIGH:
      chain = kFallbackHigh;
      chainLen = 4;
      break;
    case AutomationSlot::AR_MAX:
      chain = kFallbackMax;
      chainLen = 4;
      break;
    default:
      return 0;
  }

  for (size_t c = 0; c < chainLen; c++) {
    AutomationSlot candidateSlot = chain[c];
    size_t slotIdx = static_cast<size_t>(candidateSlot);

    // 1. Explicit ID override takes precedence if valid (or if filesystem hasn't been scanned)
    uint8_t overrideId = config.presetOverrides[slotIdx];
    if (overrideId > 0 && overrideId <= PresetPoolRegistry::MAX_PRESET_ID) {
      if (!registry.isScanned() || registry.exists(overrideId)) {
        if (outResolvedSlot) *outResolvedSlot = candidateSlot;
        return overrideId;
      }
    }

    // 2. Auto-discovered pool matching preset prefix
    size_t count = registry.getPoolCount(candidateSlot);
    if (count > 0) {
      if (outResolvedSlot) *outResolvedSlot = candidateSlot;
      if (count == 1) {
        return registry.getPoolPreset(candidateSlot, 0);
      }

      uint8_t avoidLast = lastPresetPerSlot[slotIdx];
      size_t startIdx = static_cast<size_t>(randomVal % count);
      for (size_t step = 0; step < count; step++) {
        uint8_t id = registry.getPoolPreset(candidateSlot, (startIdx + step) % count);
        if (id != 0 && id != currentWledPresetId && id != avoidLast) {
          return id;
        }
      }
      // If all candidates happen to match currentWledPresetId or avoidLast (e.g. pool of size 2), avoid at least current
      for (size_t step = 0; step < count; step++) {
        uint8_t id = registry.getPoolPreset(candidateSlot, (startIdx + step) % count);
        if (id != 0 && id != currentWledPresetId) {
          return id;
        }
      }
      return registry.getPoolPreset(candidateSlot, startIdx);
    }
  }

  return 0;
}

uint8_t AutoPresetController::update(
    uint32_t nowMs,
    bool classifierEnabled,
    AudioSemanticState semanticState,
    MusicPhrase phraseState,
    const PresetPoolRegistry& registry,
    uint8_t currentWledPresetId,
    int16_t currentWledPlaylistId,
    bool isStripOffOrNightlight,
    uint32_t randomVal) {

  if (!classifierEnabled || !config.enabled) {
    manualOverride = false;
    return 0;
  }

  if (config.manualOverrideHold && manualOverride) {
    if (semanticState == AudioSemanticState::SILENCE) {
      manualOverride = false;
    } else {
      return 0;
    }
  }

  if (isStripOffOrNightlight) {
    return 0;
  }

  AutomationSlot desiredSlot = mapClassifierToSlot(semanticState, phraseState);

  AutomationSlot resolvedSlot = desiredSlot;
  uint8_t targetPresetId = resolvePresetForSlot(desiredSlot, registry, currentWledPresetId, randomVal, &resolvedSlot);
  if (targetPresetId == 0) {
    return 0;
  }

  // Already in the resolved slot: keep current preset without churn
  if (hasAppliedSlot && lastResolvedSlot == resolvedSlot) {
    return 0;
  }

  // Already running this exact preset (or automated playlist): sync slot state without reloading from flash
  bool alreadyActive = (targetPresetId == currentWledPresetId) ||
                       (currentWledPlaylistId > 0 && static_cast<int32_t>(targetPresetId) == currentWledPlaylistId);
  if (alreadyActive) {
    hasAppliedSlot = true;
    lastResolvedSlot = resolvedSlot;
    lastPresetPerSlot[static_cast<size_t>(resolvedSlot)] = targetPresetId;
    return 0;
  }

  // Strict cooldown check on all preset transitions
  uint32_t cooldownMs = static_cast<uint32_t>(config.cooldownSec) * 1000UL;
  if (hasSwitchedOnce && (nowMs - lastSwitchMs) < cooldownMs) {
    return 0;
  }

  hasSwitchedOnce = true;
  lastSwitchMs = nowMs;
  hasAppliedSlot = true;
  lastResolvedSlot = resolvedSlot;
  lastPresetPerSlot[static_cast<size_t>(resolvedSlot)] = targetPresetId;
  return targetPresetId;
}

