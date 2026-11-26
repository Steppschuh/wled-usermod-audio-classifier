// WLED Audio Classifier Studio — Frontend Controller

const COLORS = {
  silence: "#2b313c",
  ambient_noise: "#5c667a",
  intro: "#2f6feb",
  verse: "#2da44e",
  buildup: "#d29922",
  chorus: "#cf222e",
  breakdown: "#8250df",
  outro: "#1b7c83",
  unknown: "#30363d",
  steady: "#2da44e",
};

const CONFIG_PARAMS = [
  { key: "relChorusLoudRatio", label: "Rel Chorus Loud Ratio", min: 1.10, max: 1.80, step: 0.05, def: 1.45 },
  { key: "relChorusPeakRatio", label: "Rel Chorus Peak Ratio", min: 0.80, max: 1.00, step: 0.02, def: 0.94 },
  { key: "relChorusDeltaFloor", label: "Rel Chorus Delta (%)", min: 2.0, max: 16.0, step: 1.0, def: 8.0 },
  { key: "relChorusBassTrend", label: "Rel Chorus Bass Trend", min: 0.75, max: 1.40, step: 0.05, def: 1.05 },
  { key: "slopeBuildupCentroid", label: "Buildup Centroid Slope", min: 0.30, max: 1.20, step: 0.05, def: 0.70 },
  { key: "slopeBuildupHighToBass", label: "Buildup High/Bass Slope", min: 0.10, max: 0.60, step: 0.05, def: 0.30 },
  { key: "slopeBuildupHighFlux", label: "Buildup High-Flux Slope", min: 0.15, max: 0.80, step: 0.05, def: 0.35 },
  { key: "phraseDwellMinMs", label: "Phrase Min Dwell (ms)", min: 1000, max: 4000, step: 250, def: 2000 },
  { key: "outroRecoveryMedRatio", label: "Outro Recovery Ratio", min: 0.45, max: 0.95, step: 0.05, def: 0.70 },
  { key: "musicHoldMs", label: "Music Hangover (ms)", min: 15000, max: 90000, step: 5000, def: 65000 },
  { key: "quietThresholdPercent", label: "Quiet Threshold (%)", min: 2.0, max: 15.0, step: 0.5, def: 6.0 },
  { key: "marginAboveFloorPercent", label: "Floor Margin (%)", min: 1.0, max: 10.0, step: 0.5, def: 4.0 },
  { key: "onDelayMs", label: "Presence On-Delay (ms)", min: 400, max: 2500, step: 100, def: 1200 },
];

const state = {
  suite: null,
  fixture: null,
  baselineSnapshots: null,
  config: {},
  editMode: false,
  playheadSec: 0,
  viewStartSec: 0,
  viewEndSec: 60,
  dragTarget: null,
  recPollTimer: null,
};

const $ = (id) => document.getElementById(id);
const audio = $("audio-player");
const canvas = $("timeline-canvas");
const ctx = canvas.getContext("2d");

function fmtTime(sec) {
  const s = Math.max(0, sec);
  const m = Math.floor(s / 60);
  const rem = (s % 60).toFixed(1).padStart(4, "0");
  return `${String(m).padStart(2, "0")}:${rem}`;
}

function colorFor(stateName, phraseName) {
  if (stateName === "silence" || stateName === "ambient_noise") return COLORS[stateName] || COLORS.unknown;
  return COLORS[phraseName] || COLORS[stateName] || COLORS.unknown;
}

function labelFor(stateName, phraseName) {
  if (stateName !== "music") return stateName === "ambient_noise" ? "Ambient" : "Silence";
  return phraseName && phraseName !== "unknown" ? phraseName.charAt(0).toUpperCase() + phraseName.slice(1) : "Music";
}

function isConfigModified() {
  return CONFIG_PARAMS.some((p) => Number(state.config[p.key]) !== p.def);
}

function resetConfigDefaults() {
  state.config = {};
  CONFIG_PARAMS.forEach((p) => { state.config[p.key] = p.def; });
}

async function apiGet(url) {
  const res = await fetch(url);
  const data = await res.json();
  if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
  return data;
}

async function apiPost(url, body) {
  const res = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  const data = await res.json();
  if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
  return data;
}

async function loadSuite(selectPath = null) {
  const cat = $("filter-category").value;
  const split = $("filter-split").value;
  const data = await apiGet(`/api/suite?category=${encodeURIComponent(cat)}&split=${encodeURIComponent(split)}`);
  state.suite = data;

  $("suite-overall").textContent = `${data.overall_score.toFixed(1)}%`;
  $("suite-sem").textContent = `${data.semantic_acc.toFixed(1)}%`;
  $("suite-pres").textContent = `${data.presence_acc.toFixed(1)}%`;
  $("suite-phr").textContent = `${data.phrase_acc.toFixed(1)}%`;
  $("suite-stab").textContent = `${data.stability_score.toFixed(1)}%`;
  $("suite-micro").textContent = data.total_micro_flips;
  $("suite-ping").textContent = data.total_ping_pongs;

  if (data.baseline && typeof data.baseline.overall_score === "number") {
    const d = data.overall_score - data.baseline.overall_score;
    const sign = d >= 0 ? "+" : "";
    $("suite-delta").textContent = `${sign}${d.toFixed(2)}% vs base`;
    $("suite-delta").style.color = d >= -0.05 ? "#56d364" : "#ff7b72";
  } else {
    $("suite-delta").textContent = "no baseline";
  }

  // Populate category filter if viewing all
  if (cat === "all" && data.categories) {
    const sel = $("filter-category");
    const prev = sel.value;
    sel.innerHTML = '<option value="all">All Categories</option>';
    Object.keys(data.categories).sort().forEach((c) => {
      const opt = document.createElement("option");
      opt.value = c;
      opt.textContent = `${c} (${data.categories[c].count})`;
      sel.appendChild(opt);
    });
    sel.value = prev;
  }

  const sorted = [...data.fixtures].sort((a, b) => a.overall_score - b.overall_score);
  const list = $("fixture-list");
  list.innerHTML = "";
  const activeRel = selectPath || (state.fixture && state.fixture.path);

  sorted.forEach((f) => {
    const rel = f.path.replace(/^test\/fixtures\//, "");
    const div = document.createElement("div");
    div.className = "fixture-item" + (activeRel === rel ? " active" : "");
    const scoreColor = f.overall_score >= 95 ? "#56d364" : f.overall_score >= 85 ? "#e3b341" : "#ff7b72";
    div.innerHTML = `
      <div class="fixture-item-top">
        <span>${f.fixture}</span>
        <span style="color:${scoreColor}">${f.overall_score.toFixed(1)}%</span>
      </div>
      <div class="fixture-item-sub">
        <span>${f.category} · ${f.split}</span>
        <span>${f.duration_secs}s · ${f.passed_assertions}/${f.passed_assertions + f.failed_assertions}</span>
      </div>`;
    div.onclick = () => loadFixture(rel);
    list.appendChild(div);
  });

  if (!activeRel && sorted.length > 0) {
    loadFixture(sorted[0].path.replace(/^test\/fixtures\//, ""));
  }
}

async function loadFixture(relPath, preserveView = false) {
  const cfgParam = isConfigModified() ? `&config=${encodeURIComponent(JSON.stringify(state.config))}` : "";
  const data = await apiGet(`/api/fixture?path=${encodeURIComponent(relPath)}&step_ms=250${cfgParam}`);
  if (!isConfigModified()) {
    state.baselineSnapshots = data.snapshots;
  }
  state.fixture = data;

  $("active-fixture-name").textContent = data.path;
  const sc = data.score.overall_score;
  const sb = $("fixture-score-badge");
  sb.textContent = `${sc.toFixed(1)}%`;
  sb.className = "badge " + (sc >= 95 ? "badge-good" : sc >= 85 ? "badge-warn" : "badge-bad");

  const pa = data.score.passed_assertions;
  const fa = data.score.failed_assertions;
  const ab = $("fixture-assert-badge");
  ab.textContent = `${pa}/${pa + fa} asserts`;
  ab.className = "badge " + (fa === 0 ? "badge-good" : "badge-bad");

  if (!preserveView) {
    state.playheadSec = 0;
    state.viewStartSec = 0;
    state.viewEndSec = Math.max(5, data.duration_sec || 60);
    if (data.has_audio) {
      audio.src = `/api/audio?path=${encodeURIComponent(data.path)}`;
      $("btn-play").disabled = false;
    } else {
      audio.removeAttribute("src");
      $("btn-play").disabled = true;
    }
    $("btn-play").textContent = "▶ Play";
  }

  // Highlight active item in sidebar
  document.querySelectorAll(".fixture-item").forEach((el) => {
    const name = el.querySelector("span").textContent;
    el.classList.toggle("active", data.path.endsWith(name));
  });

  renderHotspots();
  renderTraceInspector();
  renderLabelEditor();
  syncPanSlider();
  drawTimeline();
}

function expectedAtSec(sec) {
  const meta = state.fixture?.metadata || {};
  const segs = meta.segments || [];
  for (const s of segs) {
    if (sec >= (s.start_sec ?? 0) && sec < (s.end_sec ?? 0)) {
      return { state: (s.state || "music").toLowerCase(), phrase: (s.phrase || "verse").toLowerCase() };
    }
  }
  return { state: (meta.primary_label || "silence").toLowerCase(), phrase: "steady" };
}

function snapshotAtSec(sec) {
  const snaps = state.fixture?.snapshots || [];
  if (snaps.length === 0) return null;
  const stepSec = (state.fixture.step_ms || 250) / 1000;
  const idx = Math.max(0, Math.min(snaps.length - 1, Math.floor(sec / stepSec)));
  return snaps[idx];
}

function renderHotspots() {
  const list = $("hotspots-list");
  list.innerHTML = "";
  const assertions = state.fixture?.assertions || [];
  const hotspots = state.fixture?.score?.hotspots || [];
  $("hotspot-count").textContent = `${hotspots.length} hotspots · ${assertions.length} asserts`;

  assertions.filter((a) => !a.passed).forEach((a) => {
    const row = document.createElement("div");
    row.className = "hotspot-row";
    row.textContent = `❌ ${a.error || a.description}`;
    list.appendChild(row);
  });

  hotspots.forEach((h) => {
    const row = document.createElement("div");
    row.className = "hotspot-row warn";
    row.textContent = `[${fmtTime(h.t_sec)}] ${h.kind}: expected '${h.expected}', got '${h.actual}'`;
    row.onclick = () => seekTo(h.t_sec);
    list.appendChild(row);
  });

  if (list.children.length === 0) {
    list.innerHTML = '<div class="muted" style="padding:8px;">No divergence hotspots or assertion failures.</div>';
  }
}

function renderTraceInspector() {
  const snap = snapshotAtSec(state.playheadSec);
  $("trace-time").textContent = `@ ${fmtTime(state.playheadSec)}`;
  $("time-readout").textContent = `${fmtTime(state.playheadSec)} / ${fmtTime(state.fixture?.duration_sec || 0)}`;
  const grid = $("trace-grid");
  if (!snap) {
    grid.innerHTML = "";
    return;
  }
  const fields = [
    ["State", snap.state],
    ["Phrase", snap.phrase],
    ["SoundEnergy", snap.sound_energy ?? "--"],
    ["Presence", snap.presence],
    ["Level", `${Number(snap.level).toFixed(1)}%`],
    ["Noise Floor", `${Number(snap.floor).toFixed(1)}%`],
    ["Decayed Max", snap.decayed_max !== undefined ? `${Number(snap.decayed_max).toFixed(1)}%` : "--"],
    ["Track Med Loud", snap.track_med_loud !== undefined ? `${Number(snap.track_med_loud).toFixed(1)}%` : "--"],
    ["BPM", snap.bpm || "--"],
    ["Beat Prob", `${(Number(snap.beat_prob) * 100).toFixed(1)}%`],
    ["Bar Novelty", snap.bar_novelty !== undefined ? Number(snap.bar_novelty).toFixed(2) : "--"],
    ["HighFlux Slope", snap.high_flux_slope !== undefined ? Number(snap.high_flux_slope).toFixed(2) : "--"],
    ["Centroid Slope", snap.centroid_slope !== undefined ? Number(snap.centroid_slope).toFixed(2) : "--"],
    ["Bar Count", snap.bar_count ?? "--"],
  ];
  grid.innerHTML = fields.map(([k, v]) => `<div class="trace-cell"><span class="muted">${k}</span><b>${v}</b></div>`).join("");
}

function renderLabelEditor() {
  const meta = state.fixture?.metadata || {};
  $("ed-category").value = state.fixture?.category || "music";
  $("ed-split").value = state.fixture?.split || "train";
  $("ed-primary").value = meta.primary_label || "music";
  $("ed-bpm").value = meta.ground_truth_bpm || "";

  const cont = $("ed-segments");
  cont.innerHTML = "";
  const segs = meta.segments || [];
  segs.forEach((s, idx) => {
    const row = document.createElement("div");
    row.className = "ed-seg-row";
    row.innerHTML = `
      <input type="number" step="0.5" value="${s.start_sec ?? 0}" data-idx="${idx}" data-f="start_sec" />
      <input type="number" step="0.5" value="${s.end_sec ?? 0}" data-idx="${idx}" data-f="end_sec" />
      <select data-idx="${idx}" data-f="state">
        <option value="music" ${s.state === "music" ? "selected" : ""}>music</option>
        <option value="ambient_noise" ${s.state === "ambient_noise" ? "selected" : ""}>ambient_noise</option>
        <option value="silence" ${s.state === "silence" ? "selected" : ""}>silence</option>
      </select>
      <select data-idx="${idx}" data-f="phrase">
        ${["intro", "verse", "buildup", "chorus", "breakdown", "outro", "steady"].map((p) =>
          `<option value="${p}" ${(s.phrase || "verse") === p ? "selected" : ""}>${p}</option>`
        ).join("")}
      </select>
      <button class="btn btn-sm" data-del="${idx}">✕</button>`;
    row.querySelectorAll("input,select").forEach((el) => {
      el.onchange = () => {
        const f = el.dataset.f;
        segs[idx][f] = (f === "start_sec" || f === "end_sec") ? parseFloat(el.value) : el.value;
        drawTimeline();
      };
    });
    row.querySelector("[data-del]").onclick = () => {
      segs.splice(idx, 1);
      renderLabelEditor();
      drawTimeline();
    };
    cont.appendChild(row);
  });
}

function drawTimeline() {
  const wrap = $("canvas-container");
  const dpr = window.devicePixelRatio || 1;
  const w = wrap.clientWidth;
  const h = wrap.clientHeight;
  if (canvas.width !== w * dpr || canvas.height !== h * dpr) {
    canvas.width = w * dpr;
    canvas.height = h * dpr;
  }
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);

  if (!state.fixture) return;
  const snaps = state.fixture.snapshots || [];
  const bands = state.fixture.bands || [];
  const stepSec = (state.fixture.step_ms || 250) / 1000;
  const t0 = state.viewStartSec;
  const t1 = Math.max(t0 + 1, state.viewEndSec);
  const span = t1 - t0;
  const xOf = (t) => ((t - t0) / span) * w;

  // Layout Y bands
  const yRuler = 0, hRuler = 22;
  const ySpec = 24, hSpec = 115;
  const yExp = 146, hExp = 34;
  const yDiff = 184, hDiff = 12;
  const yAct = 200, hAct = 34;
  const yGhost = 238, hGhost = isConfigModified() && $("chk-ghost").checked ? 14 : 0;
  const yFeat = yGhost + hGhost + 8;
  const hFeat = Math.max(40, h - yFeat - 8);

  // 1. Time Ruler
  ctx.fillStyle = "#12161d";
  ctx.fillRect(0, yRuler, w, hRuler);
  const tickStep = span > 120 ? 15 : span > 45 ? 5 : span > 15 ? 2 : 1;
  ctx.fillStyle = "#8b949e";
  ctx.font = "11px ui-monospace, monospace";
  for (let t = Math.ceil(t0 / tickStep) * tickStep; t <= t1; t += tickStep) {
    const x = xOf(t);
    ctx.strokeStyle = "#262c36";
    ctx.beginPath(); ctx.moveTo(x, hRuler); ctx.lineTo(x, h); ctx.stroke();
    ctx.fillText(fmtTime(t), x + 3, 15);
  }

  // 2. 16-Band FFT Heatmap + Level/Floor/DecayedMax
  ctx.fillStyle = "#080b10";
  ctx.fillRect(0, ySpec, w, hSpec);
  const rowH = hSpec / 16;
  const cellW = Math.max(2, (stepSec / span) * w + 0.6);
  for (let i = 0; i < bands.length; i++) {
    const t = i * stepSec;
    if (t + stepSec < t0 || t > t1) continue;
    const x = xOf(t);
    const bArr = bands[i];
    for (let b = 0; b < 16; b++) {
      const val = Math.min(1, (bArr[b] || 0) / 180);
      if (val < 0.04) continue;
      ctx.fillStyle = `rgba(${Math.round(40 + val * 215)}, ${Math.round(80 + val * 110)}, ${Math.round(220 - val * 160)}, ${0.15 + val * 0.8})`;
      ctx.fillRect(x, ySpec + (15 - b) * rowH, cellW, rowH + 0.5);
    }
  }

  // Level / Floor / DecayedMax curves
  const drawCurve = (getter, color, width = 1.5) => {
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.beginPath();
    let started = false;
    for (let i = 0; i < snaps.length; i++) {
      const t = snaps[i].t_sec;
      if (t < t0 - stepSec || t > t1 + stepSec) continue;
      const x = xOf(t);
      const y = ySpec + hSpec - Math.min(1, Math.max(0, getter(snaps[i]) / 100)) * hSpec;
      if (!started) { ctx.moveTo(x, y); started = true; } else ctx.lineTo(x, y);
    }
    ctx.stroke();
  };
  drawCurve((s) => s.decayed_max || 0, "rgba(255, 166, 87, 0.55)", 1.2);
  drawCurve((s) => s.floor || 0, "#58a6ff", 1.2);
  drawCurve((s) => s.level || 0, "#e6edf3", 1.6);

  // 3. Expected Segments + Assertion Pins
  const meta = state.fixture.metadata || {};
  const segs = meta.segments || [];
  if (segs.length === 0) {
    ctx.fillStyle = colorFor(meta.primary_label || "silence", "steady");
    ctx.fillRect(0, yExp, w, hExp);
    ctx.fillStyle = "#fff";
    ctx.fillText(`Expected: ${meta.primary_label || "silence"}`, 8, yExp + 21);
  } else {
    segs.forEach((s) => {
      const xA = Math.max(0, xOf(s.start_sec));
      const xB = Math.min(w, xOf(s.end_sec));
      if (xB <= 0 || xA >= w) return;
      ctx.fillStyle = colorFor((s.state || "music").toLowerCase(), (s.phrase || "verse").toLowerCase());
      ctx.fillRect(xA, yExp, Math.max(1, xB - xA - 1), hExp);
      if (xB - xA > 36) {
        ctx.fillStyle = "#fff";
        ctx.fillText(labelFor((s.state || "music").toLowerCase(), (s.phrase || "").toLowerCase()), xA + 5, yExp + 21);
      }
    });
  }

  // Assertion pins
  (meta.assertions || []).forEach((a, idx) => {
    const res = (state.fixture.assertions || [])[idx];
    const ok = res ? res.passed : true;
    ctx.fillStyle = ok ? "#56d364" : "#ff7b72";
    if (a.time_sec !== undefined && a.time_sec >= t0 && a.time_sec <= t1) {
      const x = xOf(a.time_sec);
      ctx.beginPath(); ctx.moveTo(x, yExp - 2); ctx.lineTo(x + 5, yExp + 6); ctx.lineTo(x, yExp + 14); ctx.lineTo(x - 5, yExp + 6); ctx.closePath(); ctx.fill();
    }
  });

  // 4. Actual Output + Diff Strip
  for (let i = 0; i < snaps.length; i++) {
    const s = snaps[i];
    const t = s.t_sec;
    if (t + stepSec < t0 || t > t1) continue;
    const x = xOf(t);
    ctx.fillStyle = colorFor(s.state, s.phrase);
    ctx.fillRect(x, yAct, cellW, hAct);

    const exp = expectedAtSec(t);
    let diffCol = "#2da44e";
    if (s.state !== exp.state) {
      diffCol = "#f85149";
    } else if (exp.state === "music" && exp.phrase !== "steady" && s.phrase !== exp.phrase) {
      diffCol = (exp.phrase === "chorus" && (s.phrase === "breakdown" || s.phrase === "outro")) ||
                (s.phrase === "chorus" && (exp.phrase === "breakdown" || exp.phrase === "outro"))
                ? "#f85149" : "#d29922";
    }
    ctx.fillStyle = diffCol;
    ctx.fillRect(x, yDiff, cellW, hDiff);
  }

  // 5. Ghost Baseline Strip (when Config modified)
  if (hGhost > 0 && state.baselineSnapshots) {
    for (let i = 0; i < state.baselineSnapshots.length; i++) {
      const s = state.baselineSnapshots[i];
      if (s.t_sec + stepSec < t0 || s.t_sec > t1) continue;
      ctx.fillStyle = colorFor(s.state, s.phrase);
      ctx.globalAlpha = 0.65;
      ctx.fillRect(xOf(s.t_sec), yGhost, cellW, hGhost);
      ctx.globalAlpha = 1.0;
    }
  }

  // 6. Rhythm & Slopes Track
  if ($("chk-features").checked) {
    ctx.fillStyle = "#0b0f15";
    ctx.fillRect(0, yFeat, w, hFeat);
    // Bar novelty bars
    ctx.fillStyle = "rgba(88, 166, 255, 0.35)";
    for (let i = 0; i < snaps.length; i++) {
      const s = snaps[i];
      if (s.t_sec < t0 || s.t_sec > t1) continue;
      const nov = s.bar_novelty || 0;
      if (nov > 0.05) {
        const bh = Math.min(1, nov) * hFeat;
        ctx.fillRect(xOf(s.t_sec), yFeat + hFeat - bh, Math.max(2, cellW * 0.7), bh);
      }
    }
    // Beat Probability curve
    ctx.strokeStyle = "#56d364";
    ctx.lineWidth = 1.6;
    ctx.beginPath();
    let started = false;
    for (let i = 0; i < snaps.length; i++) {
      const s = snaps[i];
      if (s.t_sec < t0 - stepSec || s.t_sec > t1 + stepSec) continue;
      const x = xOf(s.t_sec);
      const y = yFeat + hFeat - Math.min(1, Math.max(0, s.beat_prob || 0)) * hFeat;
      if (!started) { ctx.moveTo(x, y); started = true; } else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }

  // Track Labels
  ctx.fillStyle = "rgba(13,17,23,0.75)";
  [[yExp + 2, "EXPECTED"], [yAct + 2, "ACTUAL"], [yFeat + 2, "BEAT PROB & NOVELTY"]].forEach(([yy, txt]) => {
    ctx.fillRect(4, yy, ctx.measureText(txt).width + 8, 14);
    ctx.fillStyle = "#c9d1d9";
    ctx.fillText(txt, 8, yy + 11);
    ctx.fillStyle = "rgba(13,17,23,0.75)";
  });

  // Playhead
  if (state.playheadSec >= t0 && state.playheadSec <= t1) {
    const px = xOf(state.playheadSec);
    ctx.strokeStyle = "#ffffff";
    ctx.lineWidth = 1.5;
    ctx.beginPath(); ctx.moveTo(px, 0); ctx.lineTo(px, h); ctx.stroke();
  }
}

function seekTo(sec) {
  const dur = state.fixture?.duration_sec || 60;
  state.playheadSec = Math.max(0, Math.min(dur, sec));
  if (state.fixture?.has_audio && Number.isFinite(audio.duration)) {
    audio.currentTime = state.playheadSec;
  }
  if (state.playheadSec < state.viewStartSec || state.playheadSec > state.viewEndSec) {
    const span = state.viewEndSec - state.viewStartSec;
    state.viewStartSec = Math.max(0, Math.min(dur - span, state.playheadSec - span * 0.25));
    state.viewEndSec = Math.min(dur, state.viewStartSec + span);
    syncPanSlider();
  }
  renderTraceInspector();
  drawTimeline();
}

function syncPanSlider() {
  const dur = Math.max(1, state.fixture?.duration_sec || 60);
  const span = state.viewEndSec - state.viewStartSec;
  const maxStart = Math.max(0.001, dur - span);
  $("pan-slider").value = Math.round((state.viewStartSec / maxStart) * 1000);
}

// Canvas interactions: scrub + drag segment edges + zoom
canvas.addEventListener("mousedown", (e) => {
  if (!state.fixture) return;
  const rect = canvas.getBoundingClientRect();
  const x = e.clientX - rect.left;
  const y = e.clientY - rect.top;
  const t = state.viewStartSec + (x / rect.width) * (state.viewEndSec - state.viewStartSec);

  if (state.editMode && y >= 146 && y <= 180) {
    const segs = state.fixture.metadata?.segments || [];
    const tol = (state.viewEndSec - state.viewStartSec) * 0.015;
    for (let i = 0; i < segs.length; i++) {
      if (Math.abs(t - segs[i].start_sec) <= tol) { state.dragTarget = { idx: i, edge: "start_sec" }; return; }
      if (Math.abs(t - segs[i].end_sec) <= tol) { state.dragTarget = { idx: i, edge: "end_sec" }; return; }
    }
  }
  seekTo(t);
});

window.addEventListener("mousemove", (e) => {
  if (!state.dragTarget || !state.fixture) return;
  const rect = canvas.getBoundingClientRect();
  const x = Math.max(0, Math.min(rect.width, e.clientX - rect.left));
  const t = Number((state.viewStartSec + (x / rect.width) * (state.viewEndSec - state.viewStartSec)).toFixed(1));
  const seg = state.fixture.metadata.segments[state.dragTarget.idx];
  seg[state.dragTarget.edge] = t;
  renderLabelEditor();
  drawTimeline();
});

window.addEventListener("mouseup", () => { state.dragTarget = null; });

canvas.addEventListener("wheel", (e) => {
  if (!state.fixture) return;
  e.preventDefault();
  const dur = Math.max(5, state.fixture.duration_sec || 60);
  const rect = canvas.getBoundingClientRect();
  const ratio = (e.clientX - rect.left) / rect.width;
  const span = state.viewEndSec - state.viewStartSec;
  const anchor = state.viewStartSec + ratio * span;
  const factor = e.deltaY > 0 ? 1.25 : 0.8;
  const newSpan = Math.max(5, Math.min(dur, span * factor));
  state.viewStartSec = Math.max(0, Math.min(dur - newSpan, anchor - ratio * newSpan));
  state.viewEndSec = state.viewStartSec + newSpan;
  syncPanSlider();
  drawTimeline();
}, { passive: false });

$("pan-slider").oninput = () => {
  if (!state.fixture) return;
  const dur = Math.max(1, state.fixture.duration_sec || 60);
  const span = state.viewEndSec - state.viewStartSec;
  const maxStart = Math.max(0, dur - span);
  state.viewStartSec = (Number($("pan-slider").value) / 1000) * maxStart;
  state.viewEndSec = state.viewStartSec + span;
  drawTimeline();
};

$("btn-zoom-in").onclick = () => {
  if (!state.fixture) return;
  const span = state.viewEndSec - state.viewStartSec;
  const mid = (state.viewStartSec + state.viewEndSec) / 2;
  const ns = Math.max(5, span * 0.65);
  state.viewStartSec = Math.max(0, mid - ns / 2);
  state.viewEndSec = state.viewStartSec + ns;
  syncPanSlider();
  drawTimeline();
};

$("btn-zoom-out").onclick = () => {
  if (!state.fixture) return;
  const dur = Math.max(5, state.fixture.duration_sec || 60);
  const span = state.viewEndSec - state.viewStartSec;
  const mid = (state.viewStartSec + state.viewEndSec) / 2;
  const ns = Math.min(dur, span * 1.5);
  state.viewStartSec = Math.max(0, Math.min(dur - ns, mid - ns / 2));
  state.viewEndSec = state.viewStartSec + ns;
  syncPanSlider();
  drawTimeline();
};

$("btn-zoom-fit").onclick = () => {
  if (!state.fixture) return;
  state.viewStartSec = 0;
  state.viewEndSec = Math.max(5, state.fixture.duration_sec || 60);
  syncPanSlider();
  drawTimeline();
};

// Audio playback loop
function togglePlay() {
  if (!state.fixture?.has_audio) return;
  if (audio.paused) {
    audio.currentTime = state.playheadSec;
    audio.play();
    $("btn-play").textContent = "❚❚ Pause";
    requestAnimationFrame(tickPlayback);
  } else {
    audio.pause();
    $("btn-play").textContent = "▶ Play";
  }
}

function tickPlayback() {
  if (audio.paused) return;
  state.playheadSec = audio.currentTime;
  renderTraceInspector();
  drawTimeline();
  requestAnimationFrame(tickPlayback);
}

audio.onended = () => { $("btn-play").textContent = "▶ Play"; };
$("btn-play").onclick = togglePlay;
window.addEventListener("keydown", (e) => {
  if (e.code === "Space" && !["INPUT", "SELECT", "TEXTAREA"].includes(document.activeElement.tagName)) {
    e.preventDefault();
    togglePlay();
  }
});

// Toggles & Actions
$("btn-toggle-edit").onclick = () => {
  state.editMode = !state.editMode;
  $("btn-toggle-edit").classList.toggle("btn-active", state.editMode);
  $("label-editor-panel").classList.toggle("hidden", !state.editMode);
};

$("btn-toggle-config").onclick = () => {
  $("config-drawer").classList.toggle("hidden");
  $("btn-toggle-config").classList.toggle("btn-active", !$("config-drawer").classList.contains("hidden"));
};

$("chk-ghost").onchange = drawTimeline;
$("chk-features").onchange = drawTimeline;
$("filter-category").onchange = () => loadSuite();
$("filter-split").onchange = () => loadSuite();
$("btn-rerun-suite").onclick = () => loadSuite();

$("btn-save-baseline").onclick = async () => {
  const res = await apiPost("/api/baseline/save", {});
  await loadSuite();
  alert(`Baseline updated (${res.overall_score.toFixed(1)}%)`);
};

$("btn-add-segment").onclick = () => {
  if (!state.fixture) return;
  state.fixture.metadata.segments = state.fixture.metadata.segments || [];
  const start = Number(state.playheadSec.toFixed(1));
  const end = Number(Math.min(state.fixture.duration_sec || start + 15, start + 15).toFixed(1));
  state.fixture.metadata.segments.push({ start_sec: start, end_sec: end, state: "music", phrase: "verse" });
  renderLabelEditor();
  drawTimeline();
};

$("btn-save-labels").onclick = async () => {
  if (!state.fixture) return;
  const meta = state.fixture.metadata;
  meta.category = $("ed-category").value;
  meta.split = $("ed-split").value;
  meta.primary_label = $("ed-primary").value;
  const bpmVal = parseFloat($("ed-bpm").value);
  if (!Number.isNaN(bpmVal) && bpmVal > 0) meta.ground_truth_bpm = bpmVal;
  await apiPost("/api/fixture/save", { path: state.fixture.path, metadata: meta });
  await loadFixture(state.fixture.path, true);
  await loadSuite(state.fixture.path);
};

// What-If Config Sliders
function buildConfigSliders() {
  resetConfigDefaults();
  const wrap = $("config-sliders");
  wrap.innerHTML = "";
  let timer = null;

  CONFIG_PARAMS.forEach((p) => {
    const row = document.createElement("div");
    row.className = "slider-row";
    row.innerHTML = `
      <div class="slider-row-top">
        <span>${p.label}</span>
        <b class="mono" id="val-${p.key}">${p.def}</b>
      </div>
      <input type="range" id="sl-${p.key}" min="${p.min}" max="${p.max}" step="${p.step}" value="${p.def}" />`;
    const sl = row.querySelector("input");
    sl.oninput = () => {
      state.config[p.key] = Number(sl.value);
      $(`val-${p.key}`).textContent = sl.value;
      clearTimeout(timer);
      timer = setTimeout(() => {
        if (state.fixture) loadFixture(state.fixture.path, true);
      }, 60);
    };
    wrap.appendChild(row);
  });
}

$("btn-reset-config").onclick = () => {
  resetConfigDefaults();
  CONFIG_PARAMS.forEach((p) => {
    $(`sl-${p.key}`).value = p.def;
    $(`val-${p.key}`).textContent = p.def;
  });
  if (state.fixture) loadFixture(state.fixture.path, true);
};

$("btn-copy-config").onclick = () => {
  navigator.clipboard.writeText(JSON.stringify(state.config, null, 2));
  $("btn-copy-config").textContent = "Copied!";
  setTimeout(() => { $("btn-copy-config").textContent = "Copy JSON"; }, 1200);
};

// Live Recording Modal
$("btn-open-record").onclick = () => $("record-modal").classList.remove("hidden");
$("btn-close-record").onclick = () => $("record-modal").classList.add("hidden");

async function pollRecordStatus() {
  const st = await apiGet("/api/record/status");
  $("rec-timer").textContent = `${st.elapsed_sec.toFixed(1)}s`;
  if (st.logs.length > 0) {
    $("rec-logs").textContent = st.logs.join("\n");
    $("rec-logs").scrollTop = $("rec-logs").scrollHeight;
  }
  if (!st.running) {
    clearInterval(state.recPollTimer);
    state.recPollTimer = null;
    $("btn-start-record").disabled = false;
    $("btn-stop-record").disabled = true;
    if (st.last_fixture) {
      $("record-modal").classList.add("hidden");
      await loadSuite(st.last_fixture);
      await loadFixture(st.last_fixture);
    }
  }
}

$("btn-start-record").onclick = async () => {
  $("btn-start-record").disabled = true;
  $("btn-stop-record").disabled = false;
  $("rec-logs").textContent = "Starting recorder...";
  await apiPost("/api/record/start", {
    wled_ip: $("rec-wled-ip").value,
    category: $("rec-category").value,
    primary_label: $("rec-label").value,
    description: $("rec-desc").value,
    duration: $("rec-duration").value,
    no_mic: $("rec-no-mic").checked,
  });
  state.recPollTimer = setInterval(pollRecordStatus, 500);
};

$("btn-stop-record").onclick = async () => {
  $("btn-stop-record").disabled = true;
  const st = await apiPost("/api/record/stop", {});
  await pollRecordStatus();
  if (st.last_fixture) {
    $("record-modal").classList.add("hidden");
    await loadSuite(st.last_fixture);
    await loadFixture(st.last_fixture);
  }
};

window.addEventListener("resize", drawTimeline);
buildConfigSliders();
loadSuite();
