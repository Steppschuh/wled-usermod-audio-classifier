# MQTT & Home Assistant Integration

`wled-usermod-audio-classifier` integrates with Home Assistant via MQTT Auto-Discovery. Entities are automatically attached to the existing WLED device via MAC address linkage (`connections: [["mac", ...]]`).

---

## 📡 Discovered Entities

| Entity Name | Component | Device Class | Unit / Options | Description |
| :--- | :--- | :--- | :--- | :--- |
| **`Sound Level`** | `sensor` | — | `%` | Relative sound volume (0.0 to 100.0%). Updates at 1 Hz. |
| **`Sound Detected`** | `binary_sensor` | `sound` | `ON` / `OFF` | Debounced presence indicator for active room sound. |
| **`Audio State`** | `sensor` | `enum` | `silence`, `ambient_noise`, `music` | Acoustic context classification. |
| **`Audio Source Status`** | `sensor` (diagnostic) | — | text | Live health and coupling status with AudioReactive. |
| **`Music Phrase`** | `sensor` (diagnostic, optional) | `enum` | `steady`, `buildup`, `breakdown`, `drop` | Structural phrasing changes (enabled via `experimental-music`). |

---

## 📬 MQTT Topic Structure

Topics are derived from WLED's configured MQTT Device Topic (`mqttDeviceTopic`, defaults to `wled/[mac]`):

| Subtopic | Payload Format | Retained | Update Rate | Description |
| :--- | :--- | :--- | :--- | :--- |
| `<device>/audio/level` | Float (e.g. `24.5`) | `false` | 1 Hz + Heartbeat | Relative sound percentage |
| `<device>/audio/sound` | `ON` or `OFF` | `false` | On change + Heartbeat | Acoustic presence binary state |
| `<device>/audio/state` | String (`silence`, `ambient_noise`, `music`) | `false` | On change + Heartbeat | Semantic acoustic context |
| `<device>/audio/status` | String (diagnostic message) | `false` | On change + Heartbeat | Coupling status with AudioReactive |
| `<device>/audio/phrase` | String (`steady`, `buildup`, `breakdown`, `drop`) | `false` | On change + Heartbeat | Phrasing state (if experimental enabled) |

### Availability
Availability is tracked automatically via WLED's LWT status topic:
- **Topic**: `<device>/status`
- **Online**: `online`
- **Offline**: `offline`

When `ha-discovery` is toggled off in settings, the usermod publishes empty retained payloads to all discovery topics to instantly remove them from Home Assistant.

