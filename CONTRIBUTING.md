# Contributing to WLED Audio Classifier

Thank you for your interest in improving `wled-usermod-audio-classifier`!

## Code Architecture

This usermod is divided into distinct, decoupled components:

- `src/audio_frame.h`: Lightweight data struct representing an acoustic frame.
- `src/ar_source.{h,cpp}`: Driver reading raw metrics and frequency bands from WLED's AudioReactive usermod.
- `src/classifier.{h,cpp}`: Pure C++ state machines (presence, noise floor histogram, beat regularity, phrasing). **Must remain free of platform/WLED includes** to enable native host testing.
- `src/auto_preset_controller.{h,cpp}`: Pure C++17 streaming `/presets.json` scanner and standalone auto-reactive preset selection controller. **Must remain free of platform/WLED includes**.
- `src/ha_discovery.{h,cpp}`: Home Assistant MQTT discovery and real-time state telemetry generation using ArduinoJson.
- `src/usermod_audio_classifier.{h,cpp}`: WLED Usermod V2 hooks and web configuration.

## Development & Testing Workflow

### 1. Running Native Unit Tests
To test classifier logic, state transitions, and debounce timers on your computer without flashing an ESP32:

```bash
pio test -e native
```

### 2. Building with WLED Core
To build the firmware binary locally:

```bash
git clone https://github.com/wled/WLED.git wled-core
cd wled-core
```

Add to `platformio_override.ini`:

```ini
[platformio]
default_envs = esp32dev

[common]
default_usermods = audioreactive

[env:esp32dev]
custom_usermods =
  ${common.default_usermods}
  symlink:///path/to/wled-usermod-audio-classifier
```

Then build:

```bash
pio run -e esp32dev
```

### 3. Recording Live Audio & Creating Test Fixtures

You can capture live acoustic telemetry from a running WLED controller over MQTT and turn it into a native test case:

1. **Record a session**:
   ```bash
   python3 tools/record_session.py --broker <mqtt-ip> --wled-ip <wled-ip> --duration 60 --label music
   ```
   This generates a synchronized JSON fixture in `test/fixtures/session_YYYYMMDD_HHMMSS.json`.
2. **Add assertions**:
   Add test assertions in the `metadata.assertions` block of the JSON fixture (e.g. `expected_state: "music"`, `expected_phrase: "chorus"`, `min_bpm: 120`).
3. **Inspect with offline simulator**:
   ```bash
   python3 tools/replay_session.py test/fixtures/session_YYYYMMDD_HHMMSS.json --plot
   ```
4. **Add to native test suite**:
   In `test/test_classifier.cpp`, use `FixtureReplayer::replayAndVerify("path/to/fixture.json", classifier)` and register it in `main()`.

See [AGENTS.md](AGENTS.md) for detailed assertion schemas and options.

## Pull Request Guidelines

1. Ensure all native unit tests pass: `pio test -e native`.
2. Adhere to the code formatting guidelines defined in `.editorconfig` and `.clang-format` (2-space indentation).
3. Update documentation and `CHANGELOG.md` when introducing new features or modifying configuration options.
4. For AI-assisted workflows, OTA flashing recipes, and architecture invariants, see [AGENTS.md](AGENTS.md).

## AI Tooling & Issue-to-Release Workflow

### Local AI Assistants
Contributors using local AI coding agents should instruct them to read [AGENTS.md](AGENTS.md), which documents all architectural boundaries, behavioral invariants, and verification commands.

### End-to-End Issue & Pull Request Flow
1. **Open an Issue**: A contributor or user files a bug report or feature request.
2. **Specification (`@gemini-cli /spec`)**: A maintainer comments `@gemini-cli /spec` on the issue to generate a structured functional specification and open questions.
3. **Implementation Plan (`@gemini-cli /plan`)**: Once the specification is agreed upon, a maintainer comments `@gemini-cli /plan` to propose a concrete implementation plan.
4. **Implementation & PR (`@gemini-cli /approve`)**: After approving the plan, a maintainer comments `@gemini-cli /approve` so Gemini CLI implements the changes and opens a pull request.
5. **Automated PR Reviews**: Pull requests targeting `main` or `dev` are built & tested via CI ([`.github/workflows/build-firmware.yml`](.github/workflows/build-firmware.yml)), and PRs are automatically reviewed by two AI reviewers (ignoring `test/fixtures/**`, `media/**`, `.pio/**`, and binaries):
   - **Gemini Code Assist** ([`.gemini/config.yaml`](.gemini/config.yaml), [`.gemini/styleguide.md`](.gemini/styleguide.md)): Posts an automated summary and review on PR open (including drafts). Re-trigger manually with `/gemini review` or `/gemini summary`.
   - **CodeRabbit** ([`.coderabbit.yaml`](.coderabbit.yaml)): Reviews non-draft PRs (`chill` profile) targeting any branch with path-specific rules. Re-trigger manually with `@coderabbitai review` or `@coderabbitai full review`.
6. **Addressing Review Feedback**: Contributors or maintainers can push fixes directly or invoke `@gemini-cli <feedback>` → `@gemini-cli /approve` on the PR thread.
7. **Releases**:
   - **Preview Release (`dev`)**: Merging into `dev` triggers [`.github/workflows/preview-release.yml`](.github/workflows/preview-release.yml), compiling OTA/factory binaries and publishing a GitHub prerelease.
   - **Stable Release (`main`)**: Merging into `main` triggers [`.github/workflows/sync-wled-releases.yml`](.github/workflows/sync-wled-releases.yml), publishing a GitHub release whenever `library.json` version or the latest upstream WLED release introduces a new `wled-<wled_tag>-mod-<usermod_tag>` pair.

> **Maintainer Access & Secret Requirement**: `@gemini-cli` comment invocations are restricted to repository `OWNER`, `MEMBER`, and `COLLABORATOR` accounts via [`.github/workflows/gemini-dispatch.yml`](.github/workflows/gemini-dispatch.yml) and require `GEMINI_API_KEY` to be configured in repository Actions secrets.


