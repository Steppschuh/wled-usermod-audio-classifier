# 📋 Repository Improvement Suggestions & OSS Best Practices Audit

**Repository:** `Steppschuh/wled-usermod-audio-classifier`  
**Audit Date:** 2026-11-13  
**Auditor:** `repo-oss-init` benchmark against SOTA open-source repositories (`astral-sh/uv`, `biomejs/biome`, `woodruffw/zizmor`, `sveltejs/svelte`).

---

## 🎯 Executive Summary

`wled-usermod-audio-classifier` exhibits strong domain engineering:
- Comprehensive `AGENTS.md` (281 lines) with strict behavioral invariants and non-negotiable architectural boundaries.
- Targeted `.coderabbit.yaml` tailored to C++17 native tests, ArduinoJson compatibility, and ESP32 flash-wear safety.
- `EUPL-1.2` licensing, `SECURITY.md`, `CONTRIBUTING.md`, `ROADMAP.md`, and clean native unit test suites.
- Clear separation between beginner OTA flashing instructions and developer compilation workflows.

To align with modern open-source repository governance, CI security hardening, and cross-platform agent tooling, the following improvements are recommended.

---

## 🚦 Prioritized Action Items

| Priority | Category | Recommendation | Effort | Impact |
| :--- | :--- | :--- | :--- | :--- |
| **High** | **CI/CD** | Add concurrency cancellation & top-level `permissions: {}` to `build-firmware.yml` | Low | High (runner efficiency, least privilege) |
| **High** | **Governance** | Add structured GitHub Issue Forms (`.github/ISSUE_TEMPLATE/`) & PR template | Low | High (triage quality, hardware bug reports) |
| **Medium** | **Security** | Add Dependabot cooldown (`default-days: 7`) & workflow static analysis (`zizmor`) | Low | High (supply-chain attack mitigation) |
| **Medium** | **Supply Chain**| Add cryptographic build provenance (`actions/attest@v4`) to release binaries | Low | Medium (verifiable firmware releases) |
| **Low** | **Tooling** | Add `CLAUDE.md` (`@AGENTS.md`), `.gitattributes`, and `CODE_OF_CONDUCT.md` | Low | Medium (cross-platform DX & community) |
| **Low** | **Review Bot** | Add `knowledge_base` references to `.coderabbit.yaml` | Low | Low (higher context for PR reviews) |

---

## 1. CI/CD Hardening (`.github/workflows/build-firmware.yml`)

### 1.1 Add Concurrency Cancellation
Currently, pushing new commits to an open PR spawns parallel PlatformIO builds without canceling outdated runs, consuming unnecessary GitHub Actions minutes.

**Proposed Change:**
Add top-level `concurrency`:
```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true
```

### 1.2 Zero-Trust Workflow Permissions & Token Hygiene
Currently, top-level permissions default to `contents: read`, and `actions/checkout` persists the default repository token on the runner disk.

**Proposed Changes:**
1. Set top-level `permissions: {}` (least privilege), granting permissions explicitly on individual jobs:
   - `test-native`: `permissions: { contents: read }`
   - `build-firmware`: `permissions: { contents: read }`
   - `release`: `permissions: { contents: write, id-token: write, attestations: write }`
2. Add `persist-credentials: false` to all `actions/checkout` invocations:
```yaml
- name: Checkout Repo
  uses: actions/checkout@v4
  with:
    persist-credentials: false
```

### 1.3 Cryptographic Build Provenance for Release Assets
When publishing official firmware releases, generate SLSA build provenance attestations so users and automated installers can verify binaries were built on GitHub Actions from trusted source commits.

**Proposed Change:**
In the `release` job of `.github/workflows/build-firmware.yml`:
```yaml
jobs:
  release:
    name: Publish GitHub Release
    if: startsWith(github.ref, 'refs/tags/v') || startsWith(github.ref, 'refs/tags/wled-') || (inputs.release_tag != '' && inputs.release_tag != null)
    needs: build-firmware
    runs-on: ubuntu-latest
    permissions:
      contents: write
      id-token: write
      attestations: write
    steps:
      - name: Download all build artifacts
        uses: actions/download-artifact@v4
        with:
          path: release_artifacts
          merge-multiple: true

      - name: Attest Build Provenance
        uses: actions/attest@v4
        with:
          subject-path: "release_artifacts/*"

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          tag_name: ${{ inputs.release_tag || github.ref_name }}
          target_commitish: ${{ inputs.target_commitish || github.sha }}
          name: ${{ inputs.release_title || inputs.release_tag || github.ref_name }}
          body: ${{ inputs.release_body || '' }}
          prerelease: ${{ inputs.prerelease || false }}
          files: release_artifacts/*
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

---

## 2. GitHub Issue Forms & PR Templates

The repository currently allows unstructured markdown issues, leading to missing hardware specs, missing audio logs, and untracked WLED versions.

### 2.1 Issue Form Config (`.github/ISSUE_TEMPLATE/config.yml`)
Disable blank issues and route security concerns to private reporting:
```yaml
blank_issues_enabled: false
contact_links:
  - name: 🛡️ Report a Security Vulnerability
    url: https://github.com/Steppschuh/wled-usermod-audio-classifier/security/advisories/new
    about: Please report security vulnerabilities privately via GitHub Security Advisories.
  - name: 💬 Community Discussions & Questions
    url: https://github.com/Steppschuh/wled-usermod-audio-classifier/discussions
    about: Ask questions or discuss custom hardware setups.
```

### 2.2 Bug Report Form (`.github/ISSUE_TEMPLATE/bug_report.yml`)
Require essential embedded hardware and acoustic diagnostic fields:
- **Controller Hardware / Model:** (e.g., Gledopto GL-C-015WL-M, ESP32-WROOM-32, ESP32-S3).
- **Microphone Hardware:** (e.g., analog MAX9814 / MAX4466, I2S INMP441 / ICS-43434, onboard digital mic).
- **WLED Version / Build Ref:** (e.g., WLED 0.14.4, 0.15.0-b2, commit hash).
- **Audio Classifier Usermod Version:** (e.g., v0.1.0).
- **Acoustic Behavior Observed:** (e.g., false sound triggers, baseline inflation, phrase misclassification).
- **Serial Console / MQTT Telemetry Logs:** (reproducible log dump).

### 2.3 Feature Request Form (`.github/ISSUE_TEMPLATE/feature_request.yml`)
Standardized template capturing problem description, proposed solution, and willingness to test on physical hardware.

### 2.4 Pull Request Template (`.github/pull_request_template.md`)
Standardized checklist verifying:
- [ ] Code strictly follows architectural boundaries in `AGENTS.md`.
- [ ] Core classifier logic changes are mirrored in `tools/replay_session.py`.
- [ ] Native unit tests pass locally (`pio test -e native`).
- [ ] Firmware compiles without warnings for target boards.

---

## 3. Supply-Chain Security & Static Analysis

### 3.1 Dependabot Cooldown (`.github/dependabot.yml`)
Add a 7-day cooldown on dependency updates to protect against compromised packages / actions:
```yaml
version: 2
updates:
  - package-ecosystem: "github-actions"
    directory: "/"
    cooldown:
      default-days: 7
    schedule:
      interval: "weekly"
    groups:
      github-actions:
        patterns:
          - "*"

  - package-ecosystem: "pip"
    directory: "/"
    cooldown:
      default-days: 7
    schedule:
      interval: "weekly"
    open-pull-requests-limit: 5
    groups:
      python-tools:
        patterns:
          - "*"
```

### 3.2 Automated Workflow Security Analysis (`.github/workflows/zizmor.yml`)
Run `zizmor` on push and PR to `.github/**` to automatically detect unsafe script injection, credential leakage, or overly permissive tokens:
```yaml
name: GitHub Actions Security Analysis (zizmor)

on:
  push:
    branches: [main]
    paths: [".github/**"]
  pull_request:
    branches: [main]
    paths: [".github/**"]

permissions: {}

concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true

jobs:
  zizmor:
    name: Audit workflows with zizmor
    runs-on: ubuntu-latest
    permissions:
      contents: read
      security-events: write
    steps:
      - name: Checkout repository
        uses: actions/checkout@v4
        with:
          persist-credentials: false

      - name: Run zizmor
        uses: zizmorcore/zizmor-action@v0.8.1
        with:
          inputs: ./.github/
```

---

## 4. Cross-Platform Developer Ergonomics & Community Governance

### 4.1 Cross-Platform Agent Context Pointer (`CLAUDE.md`)
`AGENTS.md` is complete and robust. To ensure tools like Claude Code automatically pick it up without depending on OS-specific symlinks (which fail on Windows if `core.symlinks` is disabled), create `CLAUDE.md` in the repo root containing:
```markdown
@AGENTS.md
```

### 4.2 Line Ending Normalization (`.gitattributes`)
Create `.gitattributes` to guarantee consistent LF line endings across developers cloning on Windows, macOS, and Linux:
```gitattributes
* text=auto eol=lf
*.bin binary
*.wav binary
*.png binary
```

### 4.3 Community Code of Conduct (`CODE_OF_CONDUCT.md`)
Add standard Contributor Covenant v2.1 referencing private contact information / security advisories.

### 4.4 CodeRabbit Knowledge Base Enrichment (`.coderabbit.yaml`)
Enrich `.coderabbit.yaml` to explicitly register project guideline documents:
```yaml
knowledge_base:
  code_guidelines:
    enabled: true
    filePatterns:
      - "**/AGENTS.md"
      - "**/CLAUDE.md"
      - "**/CONTRIBUTING.md"
```

---

## 5. Verification Checklist

When applying these suggestions:
1. Verify `pio test -e native` still passes cleanly.
2. Run `act` or push a draft branch to verify `build-firmware.yml` and `zizmor.yml` execute cleanly on GitHub Actions.
3. Validate issue templates via GitHub web preview (`.github/ISSUE_TEMPLATE/*`).
