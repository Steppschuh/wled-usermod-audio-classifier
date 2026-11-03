#include "usermod_audio_classifier.h"

// PROGMEM String definitions
const char AudioClassifierUsermod::_name[]             PROGMEM = "AudioClassifier";
const char AudioClassifierUsermod::_enabled[]          PROGMEM = "enabled";
const char AudioClassifierUsermod::_haDiscovery[]      PROGMEM = "ha-discovery";
const char AudioClassifierUsermod::_pinSD[]            PROGMEM = "pin-sd";
const char AudioClassifierUsermod::_pinWS[]            PROGMEM = "pin-ws";
const char AudioClassifierUsermod::_pinSCK[]           PROGMEM = "pin-sck";
const char AudioClassifierUsermod::_dbOffset[]         PROGMEM = "db-offset";
const char AudioClassifierUsermod::_silenceThreshold[] PROGMEM = "silence-threshold-db";

// Register usermod dynamically in WLED
static AudioClassifierUsermod audioClassifier;
REGISTER_USERMOD(audioClassifier);
