#!/usr/bin/env python3
# Copyright (c) 2026 Stephan Schultz
# SPDX-License-Identifier: EUPL-1.2
"""
WLED Audio Classifier - Telemetry & Microphone Synchronized Session Recorder

Captures high-frequency AudioFrame telemetry from WLED via MQTT and simultaneously
records real ambient room audio from your MacBook microphone using ffmpeg.
Includes an interactive post-recording labeler to generate test fixtures for native testing.
"""

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from datetime import datetime

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("\n❌ Missing dependency: paho-mqtt")
    print("Please install it using:")
    print("    pip install paho-mqtt\n")
    sys.exit(1)


def find_ffmpeg():
    paths = [
        shutil.which("ffmpeg"),
        "/opt/homebrew/bin/ffmpeg",
        "/usr/local/bin/ffmpeg",
    ]
    for p in paths:
        if p and os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    return None


def format_duration(seconds: float) -> str:
    m = int(seconds // 60)
    s = int(seconds % 60)
    ms = int((seconds - int(seconds)) * 10)
    return f"{m:02d}:{s:02d}.{ms}"


def main():
    parser = argparse.ArgumentParser(
        description="Record synchronized WLED AudioFrames and MacBook microphone audio."
    )
    parser.add_argument("--broker", default="192.168.0.11", help="MQTT broker host (default: 192.168.0.11)")
    parser.add_argument("--port", type=int, default=1883, help="MQTT broker port (default: 1883)")
    parser.add_argument("--user", default=None, help="MQTT username (if configured)")
    parser.add_argument("--password", default=None, help="MQTT password (if configured)")
    parser.add_argument("--device-topic", default="wled/audio", help="WLED base MQTT topic prefix (default: wled/audio or wled/%%mac%%)")
    parser.add_argument("--output-dir", default="test/fixtures", help="Directory to save session fixtures")
    parser.add_argument("--no-mic", action="store_true", help="Disable simultaneous MacBook microphone recording")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)
    session_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    base_name = f"session_{session_id}"
    json_path = os.path.join(args.output_dir, f"{base_name}.json")
    wav_path = os.path.join(args.output_dir, f"{base_name}.wav")

    ffmpeg_bin = find_ffmpeg() if not args.no_mic else None
    mic_available = ffmpeg_bin is not None

    print("\n" + "=" * 65)
    print("🎙️  WLED Audio Classifier - Session Recorder")
    print("=" * 65)
    print(f"📡 MQTT Broker:  {args.broker}:{args.port}")
    print(f"📁 Output Dir:   {args.output_dir}")
    print(f"🎤 MacBook Mic:  {'Enabled (ffmpeg)' if mic_available else 'Disabled / Not found'}")
    print("-" * 65)

    captured_frames = []
    is_recording = True
    active_device_topic = None
    stream_topic = None

    # MQTT Callbacks
    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            print("✅ Connected to MQTT broker.")
            # Subscribe to raw stream topics
            client.subscribe("wled/+/audio/raw")
            client.subscribe(f"{args.device_topic}/raw")
            client.subscribe(f"{args.device_topic}/audio/raw")
            print("📡 Subscribed to raw audio stream topics. Waiting for frames...")
        else:
            print(f"❌ Failed to connect to MQTT broker, return code: {rc}")

    def on_message(client, userdata, msg):
        nonlocal active_device_topic, stream_topic
        topic = msg.topic
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
            payload["_local_rx_time"] = time.time()
            captured_frames.append(payload)

            if active_device_topic is None:
                # Deduce stream control topic
                if "/audio/raw" in topic:
                    base = topic.replace("/audio/raw", "")
                    active_device_topic = base
                    stream_topic = f"{base}/audio/stream"
                elif "/raw" in topic:
                    base = topic.replace("/raw", "")
                    active_device_topic = base
                    stream_topic = f"{base}/stream"

        except Exception:
            pass

    # Initialize MQTT client
    client = mqtt.Client()
    if args.user and args.password:
        client.username_pw_set(args.user, args.password)
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(args.broker, args.port, keepalive=60)
        client.loop_start()
    except Exception as e:
        print(f"❌ Could not connect to MQTT broker at {args.broker}:{args.port} - {e}")
        sys.exit(1)

    # Send stream ON command
    time.sleep(0.5)
    client.publish("wled/audio/stream", "ON")
    client.publish("wled/all/audio/stream", "ON")

    # Start MacBook microphone recording via ffmpeg (AVFoundation on macOS)
    ffmpeg_proc = None
    if mic_available:
        try:
            # -f avfoundation -i ":0" captures the default audio input device (MacBook Pro Microphone)
            cmd = [
                ffmpeg_bin,
                "-y",
                "-f", "avfoundation",
                "-i", ":0",
                "-ar", "16000",
                "-ac", "1",
                "-loglevel", "error",
                wav_path,
            ]
            ffmpeg_proc = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except Exception as e:
            print(f"⚠️  Could not start microphone recording: {e}")
            ffmpeg_proc = None

    start_time = time.time()
    print("\n🔴 RECORDING IN PROGRESS...")
    print("Press [ENTER] or [Ctrl+C] to STOP recording and start labeling.\n")

    def handle_stop(signum, frame):
        nonlocal is_recording
        is_recording = False

    signal.signal(signal.SIGINT, handle_stop)

    # Live monitoring loop
    import select
    try:
        while is_recording:
            elapsed = time.time() - start_time
            count = len(captured_frames)
            fps = count / elapsed if elapsed > 0 else 0.0

            last_lvl = captured_frames[-1].get("lvl", 0.0) if captured_frames else 0.0
            last_pk = captured_frames[-1].get("pk", 0) if captured_frames else 0
            peak_str = "💥 [BEAT]" if last_pk else "        "

            status_line = (
                f"\r⏱️  [{format_duration(elapsed)}] "
                f"| Frames: {count:4d} ({fps:4.1f} fps) "
                f"| Volume: {last_lvl:4.1f}% {peak_str} "
                f"| Mic: {'🎙️  REC' if ffmpeg_proc else 'OFF'}"
            )
            sys.stdout.write(status_line)
            sys.stdout.flush()

            # Check if user pressed Enter on stdin
            if sys.stdin in select.select([sys.stdin], [], [], 0)[0]:
                sys.stdin.readline()
                break

            time.sleep(0.05)
    except KeyboardInterrupt:
        pass

    # Stop recording
    total_duration = time.time() - start_time
    print(f"\n\n🛑 Recording stopped after {format_duration(total_duration)} ({len(captured_frames)} frames captured).")

    # Stop WLED stream
    if stream_topic:
        client.publish(stream_topic, "OFF")
    client.publish("wled/audio/stream", "OFF")
    client.publish("wled/all/audio/stream", "OFF")
    client.loop_stop()
    client.disconnect()

    # Stop ffmpeg cleanly
    if ffmpeg_proc:
        try:
            ffmpeg_proc.stdin.write(b"q\n")
            ffmpeg_proc.stdin.flush()
            ffmpeg_proc.wait(timeout=2.0)
        except Exception:
            ffmpeg_proc.terminate()
        if os.path.exists(wav_path):
            size_kb = os.path.getsize(wav_path) / 1024
            print(f"🎵 Ambient audio saved: {wav_path} ({size_kb:.1f} KB)")

    if not captured_frames:
        print("\n⚠️  Warning: No frames were received. Ensure WLED is powered on, connected to MQTT, and raw streaming is enabled.")
        return

    # =========================================================================
    # Post-Recording Interactive Labeler
    # =========================================================================
    print("\n" + "=" * 65)
    print("🏷️  POST-RECORDING INTERACTIVE LABELER")
    print("=" * 65)
    print(f"Total Duration:  {format_duration(total_duration)} ({total_duration:.1f}s)")
    print(f"Total Frames:    {len(captured_frames)}")

    avg_lvl = sum(f.get("lvl", 0.0) for f in captured_frames) / len(captured_frames)
    max_lvl = max(f.get("lvl", 0.0) for f in captured_frames)
    print(f"Average Volume:  {avg_lvl:.1f}%")
    print(f"Max Peak Volume: {max_lvl:.1f}%\n")

    print("Select Primary Classification:")
    print("  [1] silence       - Room is quiet / background noise floor")
    print("  [2] ambient_noise - Continuous non-musical noise (fan, speaking, TV background)")
    print("  [3] music         - Music playback (rhythmic or melodic tracks)")

    primary_label = "music"
    while True:
        choice = input("\nEnter choice [1/2/3] (default: 3): ").strip()
        if choice in ("", "3"):
            primary_label = "music"
            break
        elif choice == "1":
            primary_label = "silence"
            break
        elif choice == "2":
            primary_label = "ambient_noise"
            break
        else:
            print("Invalid selection. Enter 1, 2, or 3.")

    track_name = input("\nTrack title / sample description (optional): ").strip()

    # Phrase labeling for music
    segments = []
    if primary_label == "music":
        print("\nAdd Phrase Timeline Segments (Breakdown, Drop, Buildup, Steady)?")
        print("Example: '0-30:steady, 30-55:buildup, 55-90:drop, 90-120:breakdown'")
        seg_input = input("Enter segments (or leave blank for whole-track steady): ").strip()

        if seg_input:
            for item in seg_input.split(","):
                try:
                    time_part, phrase_part = item.strip().split(":")
                    start_str, end_str = time_part.split("-")
                    segments.append({
                        "start_sec": float(start_str.strip()),
                        "end_sec": float(end_str.strip()),
                        "phrase": phrase_part.strip().lower()
                    })
                except Exception:
                    pass

    # Save fixture JSON bundle
    session_data = {
        "metadata": {
            "session_id": session_id,
            "created_at": datetime.now().isoformat(),
            "duration_seconds": round(total_duration, 2),
            "frame_count": len(captured_frames),
            "primary_label": primary_label,
            "description": track_name,
            "audio_file": os.path.basename(wav_path) if os.path.exists(wav_path) else None,
            "segments": segments,
        },
        "frames": captured_frames,
    }

    with open(json_path, "w") as f:
        json.dump(session_data, f, indent=2)

    print(f"\n💾 Session Fixture saved successfully:")
    print(f"   📄 JSON: {json_path}")
    if os.path.exists(wav_path):
        print(f"   🎵 WAV:  {wav_path}")

    print("\n✅ You can now replay this fixture in automated tests with:")
    print(f"   pio test -e native\n")


if __name__ == "__main__":
    main()
