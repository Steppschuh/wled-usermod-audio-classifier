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
import threading
import time
import urllib.request
import wave
from datetime import datetime

try:
    import sounddevice as sd
except ImportError:
    sd = None


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
        description="Record synchronized WLED AudioFrames and microphone audio."
    )
    parser.add_argument("--transport", choices=["http", "mqtt"], default="http", help="Telemetry transport: direct HTTP SSE (default) or MQTT")
    parser.add_argument("--broker", default="192.168.0.11", help="MQTT broker host (only for --transport mqtt)")
    parser.add_argument("--port", type=int, default=1883, help="MQTT broker port (only for --transport mqtt)")
    parser.add_argument("--user", default=None, help="MQTT username (only for --transport mqtt)")
    parser.add_argument("--password", default=None, help="MQTT password (only for --transport mqtt)")
    parser.add_argument("--device-topic", default="wled/audio", help="WLED base MQTT topic prefix (only for --transport mqtt)")
    parser.add_argument("--wled-ip", default="192.168.0.26", help="WLED controller IP address (default: 192.168.0.26)")
    parser.add_argument("--output-dir", default="test/fixtures", help="Directory to save session fixtures")
    parser.add_argument("--duration", type=float, default=None, help="Stop recording automatically after N seconds")
    parser.add_argument("--label", choices=["silence", "ambient_noise", "music"], default=None, help="Pre-set classification label (skips prompt)")
    parser.add_argument("--description", default="", help="Optional description/title (skips prompt)")
    parser.add_argument("--non-interactive", action="store_true", help="Do not prompt for interactive labeling")
    parser.add_argument("--no-mic", action="store_true", help="Disable simultaneous MacBook microphone recording")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)
    session_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    base_name = f"session_{session_id}"
    json_path = os.path.join(args.output_dir, f"{base_name}.json")
    wav_path = os.path.join(args.output_dir, f"{base_name}.wav")

    use_sounddevice = (sd is not None) and (not args.no_mic)
    ffmpeg_bin = find_ffmpeg() if (not args.no_mic and not use_sounddevice) else None
    mic_available = use_sounddevice or (ffmpeg_bin is not None)
    mic_backend = "Cross-platform (sounddevice)" if use_sounddevice else ("ffmpeg fallback" if ffmpeg_bin else "Disabled / Not found")

    print("\n" + "=" * 65)
    print("🎙️  WLED Audio Classifier - Session Recorder")
    print("=" * 65)
    if args.transport == "http":
        print(f"📡 Direct WLED:  http://{args.wled_ip}/audio-classifier/stream")
    else:
        print(f"📡 MQTT Broker:  {args.broker}:{args.port}")
    print(f"📁 Output Dir:   {args.output_dir}")
    print(f"🎤 Microphone:   {mic_backend}")
    print("-" * 65)

    captured_frames = []
    is_recording = True
    active_device_topic = None
    stream_topic = None
    client = None
    sse_resp = None

    # Auto-detect device topic / MAC from WLED IP if provided
    device_mac = None
    if args.wled_ip:
        try:
            req = urllib.request.Request(f"http://{args.wled_ip}/json/info", headers={"Accept": "application/json"})
            with urllib.request.urlopen(req, timeout=2.0) as resp:
                info_data = json.loads(resp.read().decode())
                device_mac = info_data.get("mac", "")
                if device_mac and len(device_mac) >= 6:
                    short_mac = device_mac[-6:].lower()
                    if args.device_topic == "wled/audio":
                        args.device_topic = f"wled/{short_mac}"
                    print(f"💡 Detected WLED MAC: {device_mac} (http://{args.wled_ip})")
        except Exception as e:
            print(f"⚠️  Could not query WLED info: {e}")

    # Ensure WLED live override is active and enable raw stream via HTTP
    if args.wled_ip:
        try:
            req = urllib.request.Request(
                f"http://{args.wled_ip}/json/state",
                data=b'{"lor":2,"AudioClassifier":{"stream-raw":true}}',
                headers={"Content-Type": "application/json"}
            )
            urllib.request.urlopen(req, timeout=2.0)
        except Exception:
            pass

    if args.transport == "http":
        def _sse_reader():
            nonlocal sse_resp
            url = f"http://{args.wled_ip}/audio-classifier/stream"
            try:
                req = urllib.request.Request(url, headers={"Accept": "text/event-stream"})
                sse_resp = urllib.request.urlopen(req, timeout=10.0)
                print("✅ Connected to WLED direct audio stream.")
                for raw_line in sse_resp:
                    if not is_recording:
                        break
                    line = raw_line.decode("utf-8", errors="ignore").strip()
                    if line.startswith("data:"):
                        json_str = line[5:].strip()
                        if json_str:
                            try:
                                payload = json.loads(json_str)
                                payload["_local_rx_time"] = time.time()
                                captured_frames.append(payload)
                            except Exception:
                                pass
            except Exception as e:
                if is_recording:
                    print(f"❌ Direct stream error ({url}): {e}")

        sse_thread = threading.Thread(target=_sse_reader, daemon=True)
        sse_thread.start()
    else:
        try:
            import paho.mqtt.client as mqtt
        except ImportError:
            print("❌ Missing dependency: paho-mqtt (only required for --transport mqtt)")
            sys.exit(1)

        def on_connect(client_obj, userdata, flags, rc):
            if rc == 0:
                print("✅ Connected to MQTT broker.")
                client_obj.subscribe("wled/+/audio/raw")
                client_obj.subscribe(f"{args.device_topic}/raw")
                client_obj.subscribe(f"{args.device_topic}/audio/raw")
                print("📡 Subscribed to raw audio stream topics. Waiting for frames...")
            else:
                print(f"❌ Failed to connect to MQTT broker, return code: {rc}")

        def on_message(client_obj, userdata, msg):
            nonlocal active_device_topic, stream_topic
            topic = msg.topic
            try:
                payload = json.loads(msg.payload.decode("utf-8"))
                payload["_local_rx_time"] = time.time()
                captured_frames.append(payload)
                if active_device_topic is None:
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
        time.sleep(0.5)
        client.publish("wled/audio/stream", "ON")
        client.publish("wled/all/audio/stream", "ON")
        client.publish(f"{args.device_topic}/audio/stream", "ON")
        client.publish(f"{args.device_topic}/stream", "ON")

    # Start microphone recording (cross-platform sounddevice, ffmpeg fallback)
    ffmpeg_proc = None
    sd_stream = None
    sd_wav = None
    sd_lock = threading.Lock()
    if mic_available:
        try:
            if use_sounddevice:
                sd_wav = wave.open(wav_path, "wb")
                sd_wav.setnchannels(1)
                sd_wav.setsampwidth(2)
                sd_wav.setframerate(44100)

                def _sd_callback(indata, frames_count, time_info, status):
                    with sd_lock:
                        if sd_wav is not None:
                            sd_wav.writeframes(bytes(indata))

                sd_stream = sd.RawInputStream(
                    samplerate=44100,
                    channels=1,
                    dtype="int16",
                    callback=_sd_callback,
                )
                sd_stream.start()
            else:
                if sys.platform == "darwin":
                    in_fmt, in_dev = "avfoundation", ":0"
                elif sys.platform.startswith("win"):
                    in_fmt, in_dev = "dshow", "audio=Microphone"
                else:
                    in_fmt, in_dev = "pulse", "default"
                cmd = [
                    ffmpeg_bin,
                    "-y",
                    "-thread_queue_size", "2048",
                    "-f", in_fmt,
                    "-i", in_dev,
                    "-af", "aresample=async=1000",
                    "-ar", "44100",
                    "-ac", "1",
                    "-loglevel", "error",
                    wav_path,
                ]
                ffmpeg_proc = subprocess.Popen(
                    cmd,
                    stdin=subprocess.PIPE,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE,
                    start_new_session=True,
                )
        except Exception as e:
            print(f"⚠️  Could not start microphone recording: {e}")
            ffmpeg_proc = None
            if sd_stream is not None:
                try:
                    sd_stream.close()
                except Exception:
                    pass
                sd_stream = None
            if sd_wav is not None:
                try:
                    sd_wav.close()
                except Exception:
                    pass
                sd_wav = None

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

            # Check if duration limit reached
            if args.duration and elapsed >= args.duration:
                break

            # Check if user pressed Enter on stdin
            if sys.stdin.isatty() and sys.stdin in select.select([sys.stdin], [], [], 0)[0]:
                sys.stdin.readline()
                break

            time.sleep(0.05)
    except KeyboardInterrupt:
        pass

    # Stop recording
    is_recording = False
    total_duration = time.time() - start_time
    print(f"\n\n🛑 Recording stopped after {format_duration(total_duration)} ({len(captured_frames)} frames captured).")

    if sse_resp is not None:
        try:
            sse_resp.close()
        except Exception:
            pass

    # Stop WLED stream and restore live override
    if client is not None:
        if stream_topic:
            client.publish(stream_topic, "OFF")
        client.publish("wled/audio/stream", "OFF")
        client.publish("wled/all/audio/stream", "OFF")
        client.publish(f"{args.device_topic}/audio/stream", "OFF")
        client.publish(f"{args.device_topic}/stream", "OFF")
        client.loop_stop()
        client.disconnect()
    if args.wled_ip:
        try:
            req = urllib.request.Request(
                f"http://{args.wled_ip}/json/state",
                data=b'{"lor":0,"AudioClassifier":{"stream-raw":false}}',
                headers={"Content-Type": "application/json"}
            )
            urllib.request.urlopen(req, timeout=1.5)
        except Exception:
            pass

    # Stop microphone recorder cleanly
    if sd_stream is not None:
        try:
            sd_stream.stop()
            sd_stream.close()
        except Exception:
            pass
    if sd_wav is not None:
        with sd_lock:
            try:
                sd_wav.close()
            except Exception:
                pass
            sd_wav = None
        if os.path.exists(wav_path):
            size_kb = os.path.getsize(wav_path) / 1024
            print(f"🎵 Ambient audio saved: {wav_path} ({size_kb:.1f} KB)")

    if ffmpeg_proc:
        try:
            ffmpeg_proc.stdin.write(b"q\n")
            ffmpeg_proc.stdin.flush()
            ffmpeg_proc.wait(timeout=3.0)
        except Exception:
            ffmpeg_proc.terminate()
            try:
                ffmpeg_proc.wait(timeout=2.0)
            except Exception:
                pass
        if os.path.exists(wav_path):
            size_kb = os.path.getsize(wav_path) / 1024
            print(f"🎵 Ambient audio saved: {wav_path} ({size_kb:.1f} KB)")

    if not captured_frames:
        print("\n⚠️  Warning: No frames were received. Ensure WLED is powered on, connected to MQTT, and raw streaming is enabled.")
        return

    primary_label = args.label if args.label else "music"
    track_name = args.description
    segments = []

    # Save initial fixture immediately so data is never lost
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

    # =========================================================================
    # Post-Recording Interactive Labeler (if interactive)
    # =========================================================================
    if not args.non_interactive and not args.label and sys.stdin.isatty():
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

        while True:
            try:
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
            except (EOFError, KeyboardInterrupt):
                break

        if not track_name:
            try:
                track_name = input("\nTrack title / sample description (optional): ").strip()
            except (EOFError, KeyboardInterrupt):
                pass

        # Phrase labeling for music
        if primary_label == "music":
            print("\nAdd Phrase Timeline Segments (Breakdown, Drop, Buildup, Steady)?")
            print("Example: '0-30:steady, 30-55:buildup, 55-90:drop, 90-120:breakdown'")
            try:
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
            except (EOFError, KeyboardInterrupt):
                pass

        # Update saved JSON with new labels
        session_data["metadata"]["primary_label"] = primary_label
        session_data["metadata"]["description"] = track_name
        session_data["metadata"]["segments"] = segments

        with open(json_path, "w") as f:
            json.dump(session_data, f, indent=2)
        print(f"\n💾 Session Fixture updated with user labels: {json_path}")

    print("\n✅ You can now replay this fixture in automated tests with:")
    print(f"   pio test -e native\n")


if __name__ == "__main__":
    main()

