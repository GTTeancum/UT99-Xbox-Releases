"""Record only Xemu's native PNG output, preserving capture timestamps."""
import json
import os
import shutil
import subprocess
import time

from xemu_native_screenshot import trigger_native_screenshot


def record(pid, executable, screenshot_dir, output_dir, seconds=60, fps=20):
    os.makedirs(output_dir, exist_ok=True)
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("ffmpeg is required to encode native captures")
    frames = []
    start = time.perf_counter()
    while time.perf_counter() - start < seconds:
        ok, detail, native = trigger_native_screenshot(
            pid, executable, screenshot_dir, 3, poll_interval=0.005)
        if not ok:
            raise RuntimeError("Native recording capture failed: " + detail)
        stamp = time.perf_counter() - start
        name = "frame_%05d.png" % len(frames)
        shutil.copyfile(native, os.path.join(output_dir, name))
        frames.append({"file": name, "seconds": stamp})
        delay = (len(frames) / float(fps)) - (time.perf_counter() - start)
        if delay > 0:
            time.sleep(delay)
    # Use actual acquisition intervals, not an assumed source frame rate.
    origin = frames[0]["seconds"]
    concat = os.path.join(output_dir, "frames.ffconcat")
    with open(concat, "w") as handle:
        handle.write("ffconcat version 1.0\n")
        for index, frame in enumerate(frames):
            # Put the repeated endpoint inside -t, so the muxer retains the
            # last frame's hold instead of ending at its acquisition timestamp.
            following = frames[index + 1]["seconds"] - origin if index + 1 < len(frames) else seconds - 0.001
            duration = max(0.001, following - (frame["seconds"] - origin))
            handle.write("file '%s'\noption framerate 1000\nduration %.6f\n" % (frame["file"], duration))
        handle.write("file '%s'\noption framerate 1000\n" % frames[-1]["file"])
    video = os.path.join(output_dir, "elite_gameplay_60s.mp4")
    subprocess.run([ffmpeg, "-y", "-hide_banner", "-loglevel", "error",
                    "-safe", "0", "-f", "concat", "-i", concat,
                    "-t", str(seconds), "-vf", "tpad=stop_mode=clone:stop_duration=0.1,fps=%s" % fps,
                    "-fps_mode", "cfr", "-c:v", "libx264",
                    "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", video], check=True)
    result = {"video": video, "requestedSeconds": seconds, "nativeFrames": len(frames),
              "captureSpanSeconds": frames[-1]["seconds"] - origin,
              "audio": False, "frames": frames}
    with open(os.path.join(output_dir, "recording.json"), "w") as handle:
        json.dump(result, handle, indent=2)
    return result
