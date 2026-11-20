Once your controller is running the Audio Classifier firmware (see **[Getting Started](Getting-Started)**), it can automatically switch [WLED lighting presets](https://kno.wled.ge/features/presets/) to match what is happening in the room—without needing Home Assistant or an external server.

### Jump to Your Goal
- **[I want to learn how the 5 energy slots work](#how-standalone-preset-switching-works)**
- **[I have never created a WLED preset before — walk me through it](#step-1-create-your-presets-in-wled)**
- **[I already have presets and just want to enable auto-switching](#step-3-enable-auto-switch-presets)**

### Table of Contents
- [How Standalone Preset Switching Works](#how-standalone-preset-switching-works)
- [Step 1: Create Your Presets in WLED](#step-1-create-your-presets-in-wled)
- [Step 2: Recommended Starter Presets for Each Slot](#step-2-recommended-starter-presets-for-each-slot)
- [Step 3: Enable Auto Switch Presets](#step-3-enable-auto-switch-presets)
- [Step 4: How Manual Changes & Cooldowns Work](#step-4-how-manual-changes--cooldowns-work)
- [Troubleshooting](#troubleshooting)

---

## How Standalone Preset Switching Works

The Audio Classifier maps the current room sound and song section into one of **5 lighting slots**:

| Energy Slot | Naming Prefix | When the Controller Uses This Slot |
| :--- | :--- | :--- |
| **1. No Music** | `Ambient` | The room is in **Silence** or has non-musical **Ambient Noise** (talking, TV dialogue, household activity). |
| **2. Low Energy** | `AR Low` | **Music** is playing in a calm or quiet section (**Intro**, **Breakdown**, or **Outro**). |
| **3. Mid Energy** | `AR Mid` | **Music** is in a steady, moderate groove (**Verse**). |
| **4. High Energy** | `AR High` | **Music** is rising in tension and intensity (**Buildup**). |
| **5. Max Energy** | `AR Max` | **Music** hits peak energy (**Chorus** or bass drop). |

### Two Ways to Assign Presets to Slots

1. **By Preset Name Prefix (Default):**
   Name your WLED presets starting with `Ambient`, `AR Low`, `AR Mid`, `AR High`, or `AR Max` (for example: `Ambient Warm Glow` or `AR Max Bass Drop`).
   - **Multiple presets per slot:** You can save up to **16 presets** with the same prefix (for example, three different `AR Mid ...` presets). Each time a song enters that slot, the controller picks one at random without repeating the same preset twice in a row.
   - **Smart fallback:** You don't have to populate every slot. If an `AR *` music slot has no matching presets, the controller automatically falls back to the nearest populated `AR *` energy pool (and never drops back to static `Ambient` lighting while music is playing).
   - **Automatic scanning:** WLED notifies the usermod whenever you save, rename, or delete a preset, so your preset pools update automatically.

2. **By Explicit Preset ID (`1–250`):**
   If you prefer not to rename your existing presets, you can assign a specific WLED Preset ID (`1–250`) to any slot under **Config > Usermods > AudioClassifier > Automation**. Any slot left at **`0`** uses name prefix matching.

---

## Step 1: Create Your Presets in WLED

In WLED, a [**preset**](https://kno.wled.ge/features/presets/) is a saved combination of colors, brightness, and an effect.

1. **Choose your colors:**
   - Open your WLED controller in your browser or the WLED app.
   - In the **Colors** tab, pick a color on the color wheel (for static ambient lighting) or scroll down and select a [**Color Palette**](https://kno.wled.ge/features/palettes/) (such as *Party*, *Ocean*, *Sunset*, or *Fire* for dynamic effects).
2. **Choose an effect:**
   - Open the [**Effects**](https://kno.wled.ge/features/effects/) tab.
   - For your **`Ambient`** (no music) preset, pick a calm non-audio effect like **Solid**, **Breathe**, or **Candle**.
   - For your **`AR Low / Mid / High / Max`** (music) presets, pick an [audio-reactive effect](https://kno.wled.ge/advanced/audio-reactive/#effects) marked with a **`♪`** musical note icon (see recommendations in **Step 2** below).
   - Adjust the **Effect speed** and **Effect intensity** sliders at the bottom of the Effects tab.
3. **Save it as a Preset:**
   - Open the **Presets** tab and click **+ Preset**.
   - Enter a **Name** starting with the slot prefix (case-insensitive, followed by a space, colon, hyphen, or underscore)—for example:
     ```text
     Ambient Warm Glow
     ```
   - Leave **Save to ID** at the suggested number (`1–250`) and click **Save preset**.

<!-- Screenshot placeholder: WLED Presets tab with "+ Preset" expanded, showing the Name field ("Ambient Warm Glow"), Include brightness / Save segment bounds checkboxes, Save to ID, and the "Save preset" button -->

> **Tip: Keeping your own colors while effects switch**
> When saving an `AR *` preset, if you want the controller to only switch the *effect* while keeping your current color palette or brightness, uncheck **Include brightness** (or edit the preset's saved segments/colors). If you want each energy slot to have its own color theme and brightness, keep **Include brightness** checked.

---

## Step 2: Recommended Starter Presets for Each Slot

To get started quickly, try creating this 5-preset set:

| Slot | Example Preset Name | Suggested WLED Effect | Suggested Palette & Sliders | Vibe |
| :--- | :--- | :--- | :--- | :--- |
| **No Music** | `Ambient Warm Glow` | **Solid** *(or **Breathe**)* | Warm white / orange, moderate brightness | Calm everyday lighting when no music is playing |
| **Low Energy** | `AR Low Soft Ripple` | **♪ Puddles** *(or **♪ Juggles**)* | *Ocean* or *Sunset* palette, slow speed (`40–80`) | Gentle movement during intros, breakdowns, and outros |
| **Mid Energy** | `AR Mid Smooth Wave` | **♪ Gravimeter** *(or **♪ NoiseMove**)* | *Party* or *Rainbow* palette, medium speed (`110–140`) | Steady, rhythmic bounce during verses |
| **High Energy** | `AR High Rising Pulse` | **♪ GEQ** *(or **♪ Freqwave**)* | *Fire* or *Lava* palette, faster speed (`160–200`) | Active spectrum building up toward a drop |
| **Max Energy** | `AR Max Drop Impact` | **♪ Rocktaves** *(or **♪ Gravcentric**)* | High-contrast palette, high speed & intensity (`200+`) | Full-strip punch during choruses and drops |

*(Create a second or third preset for any slot—such as `AR Mid Color Waves` and `AR Mid Beat Pulse`—and the controller will alternate between them automatically).*

<!-- Screenshot placeholder: WLED Presets list showing saved presets organized with "Ambient", "AR Low", "AR Mid", "AR High", and "AR Max" prefixes -->

---

## Step 3: Enable Auto Switch Presets

1. Go to **Config > Usermods**.
2. Scroll down to **AudioClassifier** and configure the **Automation** settings:

| Setting | Default | Description |
| :--- | :--- | :--- |
| **`Auto Switch Presets`** | `false` | Master toggle for standalone preset switching. Can also be toggled via Home Assistant or the [WLED JSON API](https://kno.wled.ge/interfaces/json-api/) (`/json/state` with `{"AudioClassifier":{"auto_switch_presets":true}}`). |
| **`Manual Override Hold`** | `true` | Pause automatic preset switching after manual WLED changes until audio returns to `Silence`. |
| **`Preset Cooldown`** | `15 s` | Minimum dwell time enforced between automatic preset switches. |
| **`Preset No Music`** | `0` | Explicit Preset ID (`1–250`) for `Silence` / `Ambient Noise`, or `0` to match `Ambient *` presets. |
| **`Preset Low Energy`** | `0` | Explicit Preset ID (`1–250`) for `Intro`, `Breakdown`, and `Outro`, or `0` to match `AR Low *` presets. |
| **`Preset Mid Energy`** | `0` | Explicit Preset ID (`1–250`) for `Verse`, or `0` to match `AR Mid *` presets. |
| **`Preset High Energy`** | `0` | Explicit Preset ID (`1–250`) for `Buildup`, or `0` to match `AR High *` presets. |
| **`Preset Max Energy`** | `0` | Explicit Preset ID (`1–250`) for `Chorus`, or `0` to match `AR Max *` presets. |

3. Check **`Auto Switch Presets`** and click **Save** at the bottom of the page.

<!-- Screenshot placeholder: WLED Config > Usermods > AudioClassifier > Automation section showing Auto Switch Presets checked, Manual Override Hold checked, Preset Cooldown at 15 s, and the five Preset slot ID fields set to 0 -->

---

## Step 4: How Manual Changes & Cooldowns Work

### 1. Preset Cooldown (`15 s` default)
To prevent your lights from flipping back and forth rapidly during fast song transitions, **Preset Cooldown** enforces a minimum wait time (default **15 seconds**) after each automatic preset change before another automatic switch can occur.

### 2. Manual Override Hold
If you manually change a color, brightness, effect, or preset while sound is active:
- When **`Manual Override Hold`** is enabled (default), automatic switching pauses so it doesn't overwrite your manual adjustment.
- While active or paused, the WLED **Info (`i`)** popup shows the current status next to `AudioClassifier`:
  - `Type: Music, Energy: 72% [Auto Switch]` — Automatic preset switching is active.
  - `Type: Music, Energy: 72% [Auto Switch: Hold]` — Paused after a manual lighting change.
- **How it resumes:** Automatic switching resumes automatically once the room returns to **`Silence`**, or if you toggle **Auto Switch Presets** off and back on.

<!-- Screenshot placeholder: WLED Info (i) popup showing the AudioClassifier line with "[Auto Switch]" active vs. "[Auto Switch: Hold]" after a manual adjustment -->

### 3. Strip Power & Nightlight Behavior
If you turn the WLED strip **Off** (power button off / brightness `0`) or activate WLED's **Nightlight** timer, automatic preset switching stays inactive and will not turn your lights back on by itself.

---

## Troubleshooting

| Symptom | Cause | Fix |
| :--- | :--- | :--- |
| Presets don't switch when music plays | `Auto Switch Presets` is off, strip is turned off, or preset names don't start with a valid prefix | Confirm the strip is turned on, **Auto Switch Presets** is checked under **Config > Usermods > AudioClassifier**, and preset names start with `Ambient`, `AR Low`, `AR Mid`, `AR High`, or `AR Max` |
| Info (`i`) popup shows `[Auto Switch: Hold]` | You manually changed a light setting while sound was detected | Stop the music and wait ~10 seconds for `Type: Silence` to clear the hold, or uncheck **Manual Override Hold** under **Config > Usermods > AudioClassifier** |
| Lights switch during music, but stay on an audio effect when music stops | No `Ambient *` preset exists (and `Preset No Music` is `0`) | Create a preset named `Ambient ...` (e.g. `Ambient Warm Glow`) or enter a preset ID in **Preset No Music** |
| Presets change too often or too slowly | `Preset Cooldown` is too short or too long | Adjust **Preset Cooldown** (e.g. `10 s` for faster changes, `25 s` for slower transitions) under **Config > Usermods > AudioClassifier** |

---

## Next Steps

Want to expose your controller's audio classification data to Home Assistant for whole-room automations, dashboard graphs, or occupancy detection? Continue to **[Home Assistant Integration](Home-Assistant-Integration)**.
