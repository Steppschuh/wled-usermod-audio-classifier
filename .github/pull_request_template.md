## Description
<!-- Briefly describe the changes introduced by this pull request -->

## Type of Change
- [ ] Bug fix (non-breaking fix for acoustic classification, MQTT telemetry, or state machine)
- [ ] New feature (non-breaking addition of a new entity, preset behavior, or DSP capability)
- [ ] Breaking change (modification that changes existing MQTT topic schema, JSON keys, or configuration fields)
- [ ] Documentation / CI / tooling update

## Verification & Architecture Checklist
- [ ] Code strictly adheres to architectural boundaries in `AGENTS.md` (pure C++17 core in `src/classifier.*` and `src/auto_preset_controller.*` with zero hardware/WLED headers).
- [ ] Core classifier logic changes are mirrored in `tools/replay_session.py`.
- [ ] Native unit tests pass locally: `pio test -e native` (0 failures).
- [ ] Behavioral invariants are preserved (non-music state lock, silence coupling, noise floor freeze during sound).
- [ ] Tested on target hardware (ESP32) or verified via test fixtures.
