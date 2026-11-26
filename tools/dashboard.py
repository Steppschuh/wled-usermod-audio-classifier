#!/usr/bin/env python3
# Copyright (c) 2026 Stephan Schultz
# SPDX-License-Identifier: EUPL-1.2
"""
WLED Audio Classifier - Local Testing & Recording Dashboard Server

Serves a DAW-style timeline inspector, suite evaluator, live ClassifierConfig
what-if tuner, visual label editor, and live WLED/MQTT session recorder.
"""

import argparse
import gzip
import json
import mimetypes
import os
import signal
import subprocess
import sys
import threading
import time
import urllib.parse
import webbrowser
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import evaluate  # noqa: E402

REPO_ROOT = evaluate.REPO_ROOT
FIXTURES_DIR = evaluate.DEFAULT_FIXTURES_DIR.resolve()
BASELINE_PATH = evaluate.DEFAULT_BASELINE_PATH.resolve()
UI_DIR = Path(__file__).resolve().parent / "ui"
RECORD_SCRIPT = Path(__file__).resolve().parent / "record_session.py"


def resolve_fixture_path(rel_or_name: str) -> Path:
    if not rel_or_name:
        raise ValueError("Missing fixture path")
    candidate = Path(rel_or_name)
    if candidate.is_absolute():
        resolved = candidate.resolve()
    elif str(candidate).startswith("test/fixtures/"):
        resolved = (REPO_ROOT / candidate).resolve()
    else:
        resolved = (FIXTURES_DIR / candidate).resolve()
    if not resolved.is_relative_to(FIXTURES_DIR):
        raise ValueError("Fixture path outside test/fixtures")
    if not resolved.is_file():
        raise FileNotFoundError(f"Fixture not found: {rel_or_name}")
    return resolved


def companion_wav_path(fixture_path: Path) -> Path:
    name = fixture_path.name
    if name.endswith(".json.gz"):
        stem = name[:-8]
    elif name.endswith(".json"):
        stem = name[:-5]
    else:
        stem = fixture_path.stem
    return fixture_path.with_name(f"{stem}.wav")


def load_fixture_json(fixture_path: Path) -> dict:
    opener = gzip.open if fixture_path.name.endswith(".gz") else open
    with opener(fixture_path, "rt", encoding="utf-8") as f:
        return json.load(f)


def save_fixture_json(fixture_path: Path, data: dict):
    opener = gzip.open if fixture_path.name.endswith(".gz") else open
    with opener(fixture_path, "wt", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def downsample_bands(frames: list, step_ms: int = 250) -> list:
    if not frames:
        return []
    t0 = frames[0].get("t", 0)
    out = []
    last_bucket = -1
    last_raw_t = None
    for f in frames:
        raw_t = f.get("t", 0)
        if raw_t == last_raw_t:
            continue
        last_raw_t = raw_t
        t = (raw_t - t0) if raw_t >= t0 else raw_t
        bucket = int(t // step_ms)
        if bucket != last_bucket:
            last_bucket = bucket
            bands = f.get("bands", [0] * 16)
            out.append([int(b) for b in bands[:16]])
    return out


class RecordingManager:
    def __init__(self):
        self.lock = threading.Lock()
        self.proc = None
        self.running = False
        self.started_at = 0.0
        self.logs = deque(maxlen=120)
        self.output_dir = FIXTURES_DIR
        self.last_fixture = None
        self.before_files = set()

    def _reader_thread(self, proc):
        try:
            for line in iter(proc.stdout.readline, ""):
                if not line:
                    break
                with self.lock:
                    self.logs.append(line.rstrip("\n"))
        except Exception:
            pass
        finally:
            proc.wait()
            with self.lock:
                self.running = False
                after_files = set(self.output_dir.glob("session_*.json"))
                created = sorted(after_files - self.before_files, key=lambda p: p.stat().st_mtime)
                if created:
                    self.last_fixture = str(created[-1].relative_to(FIXTURES_DIR))
                self.proc = None

    def start(self, params: dict) -> dict:
        with self.lock:
            if self.running and self.proc is not None:
                raise RuntimeError("A recording is already in progress")

            category = str(params.get("category", "")).strip()
            out_dir = (FIXTURES_DIR / category).resolve() if category else FIXTURES_DIR
            if not out_dir.is_relative_to(FIXTURES_DIR):
                raise ValueError("Invalid category path")
            out_dir.mkdir(parents=True, exist_ok=True)

            self.output_dir = out_dir
            self.before_files = set(out_dir.glob("session_*.json"))
            self.last_fixture = None
            self.logs.clear()

            cmd = [
                sys.executable,
                str(RECORD_SCRIPT),
                "--transport", "http",
                "--wled-ip", str(params.get("wled_ip", "192.168.0.26")),
                "--output-dir", str(out_dir),
                "--label", str(params.get("primary_label", "music")),
                "--description", str(params.get("description", "")),
                "--non-interactive",
            ]
            duration = params.get("duration")
            if duration is not None and str(duration).strip() != "":
                cmd.extend(["--duration", str(float(duration))])
            if params.get("no_mic"):
                cmd.append("--no-mic")

            self.proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
            self.running = True
            self.started_at = time.time()
            t = threading.Thread(target=self._reader_thread, args=(self.proc,), daemon=True)
            t.start()
            return self.status_unlocked()

    def stop(self) -> dict:
        proc_to_stop = None
        with self.lock:
            if self.running and self.proc is not None:
                proc_to_stop = self.proc
        if proc_to_stop is not None:
            proc_to_stop.send_signal(signal.SIGINT)
            deadline = time.time() + 8.0
            while time.time() < deadline:
                with self.lock:
                    if not self.running:
                        break
                time.sleep(0.1)
        with self.lock:
            return self.status_unlocked()

    def status_unlocked(self) -> dict:
        elapsed = round(time.time() - self.started_at, 1) if self.running else 0.0
        return {
            "running": self.running,
            "elapsed_sec": elapsed,
            "logs": list(self.logs),
            "last_fixture": self.last_fixture,
        }

    def status(self) -> dict:
        with self.lock:
            return self.status_unlocked()


RECORDER = RecordingManager()


class DashboardHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def _send_json(self, payload: dict, status: int = 200):
        raw = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(raw)

    def _read_json_body(self) -> dict:
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length > 0 else b"{}"
        return json.loads(raw.decode("utf-8"))

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        qs = urllib.parse.parse_qs(parsed.query)

        try:
            if path == "/api/suite":
                split = qs.get("split", ["all"])[0]
                category = qs.get("category", ["all"])[0]
                report = evaluate.evaluate_suite(fixtures_root=FIXTURES_DIR, split=split, category=category)
                baseline = None
                if BASELINE_PATH.is_file():
                    with open(BASELINE_PATH, "r", encoding="utf-8") as bf:
                        baseline = json.load(bf)
                report["baseline"] = baseline
                self._send_json(report)
                return

            if path == "/api/fixture":
                rel = qs.get("path", [""])[0]
                cfg = qs.get("config", [None])[0]
                step_ms = int(qs.get("step_ms", ["250"])[0])
                fpath = resolve_fixture_path(rel)
                bin_path = evaluate.ensure_eval_binary()
                cli_hi = evaluate.run_eval_cli(bin_path, fpath, config_json=cfg, step_ms=step_ms, trace=True)
                cli_1hz = cli_hi if step_ms == 1000 else evaluate.run_eval_cli(bin_path, fpath, config_json=cfg, step_ms=1000, trace=True)
                meta = cli_hi.get("metadata", {})
                cat = evaluate.infer_category(fpath, meta, FIXTURES_DIR)
                sp = str(meta.get("split", "train")).lower()
                score = evaluate.score_cli_output(cli_1hz, fpath, cat, sp)
                raw_doc = load_fixture_json(fpath)
                bands_hi = downsample_bands(raw_doc.get("frames", []), step_ms=step_ms)
                wav = companion_wav_path(fpath)
                self._send_json({
                    "path": str(fpath.relative_to(FIXTURES_DIR)),
                    "category": cat,
                    "split": sp,
                    "has_audio": wav.is_file(),
                    "duration_sec": cli_hi.get("duration_sec", 0.0),
                    "frame_count": cli_hi.get("frame_count", 0),
                    "metadata": meta,
                    "snapshots": cli_hi.get("snapshots", []),
                    "bands": bands_hi,
                    "step_ms": step_ms,
                    "assertions": cli_1hz.get("assertions", []),
                    "score": score,
                })
                return

            if path == "/api/audio":
                rel = qs.get("path", [""])[0]
                fpath = resolve_fixture_path(rel)
                wav = companion_wav_path(fpath)
                if not wav.is_file():
                    self._send_json({"error": "No companion WAV file"}, status=404)
                    return
                size = wav.stat().st_size
                range_hdr = self.headers.get("Range")
                start = 0
                end = size - 1
                status = 200
                if range_hdr and range_hdr.startswith("bytes="):
                    parts = range_hdr.replace("bytes=", "").split("-")
                    if parts[0]:
                        start = int(parts[0])
                    if len(parts) > 1 and parts[1]:
                        end = min(size - 1, int(parts[1]))
                    status = 206
                length = max(0, end - start + 1)
                self.send_response(status)
                self.send_header("Content-Type", "audio/wav")
                self.send_header("Accept-Ranges", "bytes")
                self.send_header("Content-Length", str(length))
                if status == 206:
                    self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
                self.end_headers()
                with open(wav, "rb") as wf:
                    wf.seek(start)
                    remaining = length
                    while remaining > 0:
                        chunk = wf.read(min(65536, remaining))
                        if not chunk:
                            break
                        self.wfile.write(chunk)
                        remaining -= len(chunk)
                return

            if path == "/api/record/status":
                self._send_json(RECORDER.status())
                return

            # Static UI files
            rel_ui = "index.html" if path in ("/", "") else path.lstrip("/")
            target = (UI_DIR / rel_ui).resolve()
            if not target.is_relative_to(UI_DIR.resolve()) or not target.is_file():
                self.send_error(404, "Not found")
                return
            ctype, _ = mimetypes.guess_type(str(target))
            data = target.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", ctype or "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except Exception as e:
            self._send_json({"error": str(e)}, status=500)

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        try:
            body = self._read_json_body()
            if path == "/api/fixture/save":
                fpath = resolve_fixture_path(body.get("path", ""))
                doc = load_fixture_json(fpath)
                new_meta = body.get("metadata", {})
                if not isinstance(new_meta, dict):
                    raise ValueError("Invalid metadata")
                doc["metadata"] = new_meta
                save_fixture_json(fpath, doc)
                self._send_json({"ok": True, "path": str(fpath.relative_to(FIXTURES_DIR))})
                return

            if path == "/api/baseline/save":
                report = evaluate.evaluate_suite(fixtures_root=FIXTURES_DIR)
                baseline_payload = {
                    "overall_score": report["overall_score"],
                    "semantic_acc": report["semantic_acc"],
                    "presence_acc": report["presence_acc"],
                    "phrase_acc": report["phrase_acc"],
                    "stability_score": report["stability_score"],
                    "categories": report["categories"],
                    "fixtures": {
                        r["fixture"]: {
                            "overall_score": r["overall_score"],
                            "semantic_acc": r["semantic_acc"],
                            "presence_acc": r["presence_acc"],
                            "phrase_acc": r["phrase_acc"],
                            "stability_score": r["stability_score"],
                        }
                        for r in report["fixtures"]
                    },
                }
                with open(BASELINE_PATH, "w", encoding="utf-8") as bf:
                    json.dump(baseline_payload, bf, indent=2)
                self._send_json({"ok": True, "overall_score": report["overall_score"]})
                return

            if path == "/api/record/start":
                self._send_json(RECORDER.start(body))
                return

            if path == "/api/record/stop":
                self._send_json(RECORDER.stop())
                return

            self._send_json({"error": "Unknown endpoint"}, status=404)
        except Exception as e:
            self._send_json({"error": str(e)}, status=500)


def main():
    parser = argparse.ArgumentParser(description="WLED Audio Classifier Testing & Recording Dashboard")
    parser.add_argument("--host", default="127.0.0.1", help="Bind host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8765, help="Bind port (default: 8765)")
    parser.add_argument("--no-open", action="store_true", help="Do not automatically open browser")
    args = parser.parse_args()

    evaluate.ensure_eval_binary()
    server = ThreadingHTTPServer((args.host, args.port), DashboardHandler)
    url = f"http://{args.host}:{args.port}"
    print(f"🎛️  WLED Audio Classifier Dashboard running at {url}")
    print("Press Ctrl+C to stop.")
    if not args.no_open:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down dashboard...")
        RECORDER.stop()
        server.server_close()


if __name__ == "__main__":
    main()
