# Configuration Reference

Configure `AudioClassifier` via WLED's web interface under **Config > Usermods**:

---

## 🎛️ Settings Overview

| Setting Key | Type | Default | Unit | Description |
| :--- | :--- | :--- | :--- | :--- |
| **`enabled`** | boolean | `true` | — | Master toggle to enable or disable audio classification. |
| **`ha-discovery`** | boolean | `true` | — | Automatically create and maintain Home Assistant entities via MQTT. |
| **`quiet-threshold-percent`** | float | `6.0` | `%` | Absolute relative volume floor below which acoustic state is classified as `silence`. |
| **`margin-above-floor`** | float | `4.0` | `%` | Dynamic margin above the rolling background noise floor required to trigger `SOUND` state. |
| **`on-delay-ms`** | integer | `1200` | `ms` | Minimum continuous sound duration before switching from `QUIET` to `SOUND`. Prevents transient claps or door slams from tripping automations. |
| **`off-delay-ms`** | integer | `8000` | `ms` | Minimum silence duration before switching from `SOUND` to `QUIET`. Prevents automations from flapping during pauses between tracks or quiet song bridges. |
| **`experimental-music`** | boolean | `false` | — | Enable experimental beat regularity tracking and phrase state detection (`steady`, `buildup`, `breakdown`, `drop`). |

---

## 💡 Calibration Tips

1. **Setting `quiet-threshold-percent`**:
   - Leave the room quiet with HVAC or normal background sounds running.
   - Look at the `AudioClassifier` reading on WLED's `/json/info` page or Home Assistant (e.g., `3.2%`).
   - Set `quiet-threshold-percent` about 2–3% above that value (e.g. `5.5%` or `6.0%`).

2. **Adjusting `off-delay-ms`**:
   - If lights turn off too quickly between music tracks, increase `off-delay-ms` to `10000` or `15000` (10–15 seconds).

