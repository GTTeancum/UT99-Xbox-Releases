#!/usr/bin/env python
"""Run isolated, rendered UT99 Xbox stress cases in Xemu."""

from __future__ import print_function

import argparse
import ctypes
import ctypes.wintypes
import datetime
import json
import os
import re
import shutil
import sys
import time

from PIL import ImageGrab

import poll_xemu_ram_log as xemu_poll
import run_jailbreak_soak as soak_log
import run_jailbreak_xemu_soak as xemu_soak


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
XBOX_DIR = os.path.dirname(SCRIPT_DIR)
ROOT_DIR = os.path.dirname(XBOX_DIR)

DEFAULT_BUILD = os.path.join(ROOT_DIR, "build")
DEFAULT_RUNTIME = r"C:\Games\Emulators\CXBX\UT99x"
DEFAULT_XEMU_ROOT = r"C:\Games\Emulators\Xemu"
DEFAULT_XEMU_INSTANCE = r"C:\Games\Emulators\Xemu\UT99DeepSoak"
DEFAULT_HDD = r"C:\Games\Emulators\Xemu\UT99Test\HDD\ut99_hdd.qcow2"
DEFAULT_EEPROM = r"C:\Games\Emulators\Xemu\EEPROM\eeprom.bin"

ROSTER = [
    ("PS2 Raw Steel", "UTPS2Characters.RawSteelPS2Bot"),
    ("PS2 Male Necris", "UTPS2Characters.NecrisPS2Bot"),
    ("PS2 Male Soldier", "UTPS2Characters.MSoldierPS2Bot"),
    ("PS2 Female Soldier", "UTPS2Characters.FemSoldierPS2Bot"),
    ("PS2 Female Soldier 2", "UTPS2Characters.FemSoldierSPS2Bot"),
    ("PS2 Female Commando", "UTPS2Characters.FemCommandoPS2Bot"),
    ("PS2 Female Mercenary", "UTPS2Characters.FemMercenaryPS2Bot"),
    ("PS2 Male Commando", "UTPS2Characters.MCommandoPS2Bot"),
    ("PS2 Male Mercenary", "UTPS2Characters.MMercenaryPS2Bot"),
    ("PS2 Skaarj Hybrid", "UTPS2Characters.SkaarjHybridPS2Bot"),
    ("PS2 Skaarj Hybrid 2", "UTPS2Characters.SkaarjHybridMPS2Bot"),
    ("PS2 Xan", "UTPS2Characters.XanPS2Bot"),
    ("PS2 Rampage", "UTPS2Characters.WarbossPS2Bot"),
    ("PS2 Dominator", "UTPS2Characters.SkaarjBossPS2Bot"),
    ("PS2 Damien", "UTPS2Characters.DamienPS2Bot"),
    ("PS2 War Machine", "UTPS2Characters.WarMachinePS2Bot"),
    ("Master Chief", "HaloMasterChief.HaloMasterChiefBot"),
]

CASES = [
    {
        "map": "DM-HangEmHigh",
        "game": "Botpack.DeathMatchPlus",
        "player": "HaloMasterChief.HaloMasterChief",
        "bots": 8,
        "roster_offset": 0,
        "seconds": 300,
        "reason": "new 9.6 MB map and the previous 64 MB memory-floor boundary",
    },
    {
        "map": "DM-Halo-Derelict",
        "game": "Botpack.DeathMatchPlus",
        "player": "UTPS2Characters.SkaarjBossPS2",
        "bots": 12,
        "roster_offset": 8,
        "seconds": 180,
        "reason": "new Halo map with expanded mixed custom roster",
    },
    {
        "map": "CTF-Titania",
        "game": "Botpack.CTFGame",
        "player": "UTPS2Characters.DamienPS2",
        "bots": 12,
        "seconds": 180,
        "reason": "new current-build CTF map and team-skin churn",
    },
    {
        "map": "CTF-Darji16",
        "game": "Botpack.CTFGame",
        "player": "UTPS2Characters.WarMachinePS2",
        "bots": 8,
        "seconds": 240,
        "reason": "exact formerly crashing mixed-roster boundary",
    },
    {
        "map": "DOM-Coagulate",
        "game": "Botpack.Domination",
        "player": "UTPS2Characters.XanPS2",
        "bots": 12,
        "seconds": 180,
        "reason": "PS2 map lighting plus bot-heavy control-point combat",
    },
    {
        "map": "AS-HiSpeed",
        "game": "Botpack.Assault",
        "player": "UTPS2Characters.WarbossPS2",
        "bots": 12,
        "seconds": 180,
        "reason": "large moving-brush Assault map and round travel",
    },
    {
        "map": "JB-Alcatraz",
        "game": "JailBreak.JailBreak",
        "player": "UTPS2Characters.SkaarjHybridPS2",
        "bots": 12,
        "seconds": 180,
        "reason": "11.9 MB map, cache pressure, and prior Jailbreak concern",
    },
]

ARRAY_RE = re.compile(r"^(BotClasses|BotNames|BotTeams|BotSkins|BotFaces)\[(\d+)\]=", re.IGNORECASE)
SMOKE_MAP_RE = re.compile(r"SMOKE tick=(\d+).*?url=([^?\s]+).*?\bbots=(\d+)")
XCHAR_RE = re.compile(r"XCHAR tick=(\d+) name=(.*?) class=Class (\S+) mesh=(.*?) skin=")
XSKELCAM_RE = re.compile(r"XSKELCAM tick=(\d+) index=(\d+) class=Class (\S+)")
PERF_RE = re.compile(r"\bPERF fps=([0-9.]+)")
PERF_DETAIL_RE = re.compile(
    r"UT99XDBG t=(\d+).*?\bPERF fps=([0-9.]+).*?"
    r"texNew=(\d+) texUp=(\d+).*?liveKB=(\d+) texKB=(\d+) availKB=(\d+)"
)
XSKEL_FLICKER_RE = re.compile(r"[^\r\n]*XSKELFLICKER[^\r\n]*")


def read_lines(path):
    with open(path, "r", errors="replace") as handle:
        return handle.read().splitlines()


def write_lines(path, lines):
    with open(path, "w", newline="") as handle:
        handle.write("\r\n".join(lines) + "\r\n")


def roster_entry(index, roster_offset=0, roster_span=None):
    span = roster_span if roster_span is not None else len(ROSTER)
    span = max(1, min(span, len(ROSTER)))
    return ROSTER[(roster_offset + (index % span)) % len(ROSTER)]


def patch_bot_roster(path, count, roster_offset=0, roster_span=None):
    lines = read_lines(path)
    found = set()
    output = []
    for line in lines:
        match = ARRAY_RE.match(line.strip())
        if not match:
            output.append(line)
            continue
        key = match.group(1)
        index = int(match.group(2))
        found.add((key.lower(), index))
        if index < 32:
            name, bot_class = roster_entry(index, roster_offset, roster_span)
            values = {
                "botclasses": bot_class,
                "botnames": name,
                "botteams": str(index & 1),
                "botskins": "",
                "botfaces": "",
            }
            output.append("%s[%d]=%s" % (key, index, values[key.lower()]))
        else:
            output.append(line)

    if not any(key == "botclasses" for key, _index in found):
        raise RuntimeError("BotClasses array not found in " + path)
    soak_log.set_key(output, "Botpack.ChallengeBotInfo", "bRandomOrder", "False")
    write_lines(path, output)


def patch_match_ini(path, bots):
    lines = soak_log.read_lines(path)
    sections = (
        "Botpack.DeathMatchPlus",
        "Botpack.TeamGamePlus",
        "Botpack.CTFGame",
        "Botpack.Domination",
        "Botpack.Assault",
        "JailBreak.JailBreak",
    )
    for section in sections:
        soak_log.set_key(lines, section, "MinPlayers", bots + 1)
        soak_log.set_key(lines, section, "InitialBots", bots)
        soak_log.set_key(lines, section, "bTournament", "False")
        soak_log.set_key(lines, section, "TimeLimit", 0)
    for section in ("Botpack.CTFGame", "Botpack.Domination", "JailBreak.JailBreak"):
        soak_log.set_key(lines, section, "GoalTeamScore", "0.000000")
    soak_log.set_key(lines, "Botpack.DeathMatchPlus", "FragLimit", 0)
    soak_log.write_lines(path, lines)


def build_url(case):
    return (
        "{map}.unr?Game={game}?Name=SoakP1?Class={player}?Team=0"
        "?MinPlayers={players}?InitialBots={bots}?MaxPlayers=16?Difficulty=3"
        "?FragLimit=0?TimeLimit=0?GoalTeamScore=0"
    ).format(
        map=case["map"],
        game=case["game"],
        player=case["player"],
        players=case["bots"] + 1,
        bots=case["bots"],
    )


def prepare_base(args):
    stage = os.path.abspath(args.stage_dir)
    xemu_soak.safe_remove_tree(stage, os.path.join(XBOX_DIR, "build_cli"))
    xemu_soak.ensure_dir(os.path.dirname(stage))
    xemu_soak.copy_runtime_tree(args.runtime_source, stage)
    copied = xemu_soak.copy_build_overlay(args.build_dir, stage)
    soak_log.remove_old_logs(stage)
    for marker in ("XboxSoakMapList.ini", "XboxStartURL.ini", "XboxCharacterSoak.ini"):
        path = os.path.join(stage, marker)
        if os.path.isfile(path):
            os.remove(path)
    with open(os.path.join(stage, "XboxCharacterSoak.ini"), "w") as handle:
        handle.write("; Log exact bot class, mesh, and skin during Xemu stress\n")
    return stage, copied


def prepare_case(args, stage, case, run_dir):
    for ini_name in ("Default.ini", "UnrealTournament.ini"):
        patch_match_ini(os.path.join(stage, "System", ini_name), case["bots"])
    for ini_name in ("User.ini", "DefUser.ini"):
        path = os.path.join(stage, "System", ini_name)
        if os.path.isfile(path):
            patch_bot_roster(
                path,
                case["bots"],
                case.get("roster_offset", 0),
                case.get("roster_span"),
            )

    url = build_url(case)
    soak_log.write_lines(
        os.path.join(stage, "XboxStartURL.ini"),
        ["; Isolated rendered Xemu stress case", "StartURL=" + url],
    )
    for name in ("XboxStartURL.ini", "XboxCharacterSoak.ini"):
        shutil.copy2(os.path.join(stage, name), os.path.join(run_dir, name))
    for name in ("Default.ini", "UnrealTournament.ini", "User.ini"):
        shutil.copy2(os.path.join(stage, "System", name), os.path.join(run_dir, name))
    return url


def capture_screen(pid, source_dir, output_path):
    if os.name != "nt":
        return "built-in screenshot trigger requires Windows"

    user32 = ctypes.windll.user32
    handles = []
    enum_proc_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def collect_window(hwnd, _lparam):
        process_id = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(process_id))
        if process_id.value == pid:
            handles.append(hwnd)
        return True

    callback = enum_proc_type(collect_window)
    user32.EnumWindows(callback, 0)
    for parent in list(handles):
        user32.EnumChildWindows(parent, callback, 0)

    top_level = []
    for hwnd in set(handles):
        if user32.GetParent(hwnd) or not user32.IsWindowVisible(hwnd):
            continue
        rect = ctypes.wintypes.RECT()
        if user32.GetWindowRect(hwnd, ctypes.byref(rect)):
            width = rect.right - rect.left
            height = rect.bottom - rect.top
            if width > 0 and height > 0:
                top_level.append((width * height, hwnd, width, height))
    if top_level:
        _area, hwnd, _width, _height = max(top_level)
        try:
            image = ImageGrab.grab(window=int(hwnd))
            image.save(output_path, "PNG")
            return "captured from Xemu window without focus via PrintWindow"
        except Exception:
            pass

    before = set(os.listdir(source_dir)) if os.path.isdir(source_dir) else set()
    vk_f12 = 0x7B
    scan_code = user32.MapVirtualKeyW(vk_f12, 0)
    key_down = 1 | (scan_code << 16)
    key_up = key_down | (1 << 30) | (1 << 31)
    for hwnd in set(handles):
        user32.PostMessageW(hwnd, 0x0100, vk_f12, key_down)
        user32.PostMessageW(hwnd, 0x0101, vk_f12, key_up)

    deadline = time.time() + 4.0
    while time.time() < deadline:
        current = set(os.listdir(source_dir)) if os.path.isdir(source_dir) else set()
        created = [
            name for name in current.difference(before)
            if name.lower().endswith(".png")
        ]
        if created:
            newest = max(
                (os.path.join(source_dir, name) for name in created),
                key=os.path.getmtime,
            )
            shutil.copy2(newest, output_path)
            return "captured from Xemu built-in writer: " + newest
        time.sleep(0.1)

    if not top_level:
        return "Xemu screenshot did not fire and no visible top-level window was found"

    _area, hwnd, _width, _height = max(top_level)
    try:
        image = ImageGrab.grab(window=int(hwnd))
        image.save(output_path, "PNG")
        return "captured from Xemu window without focus via PrintWindow"
    except Exception as exc:
        return "Xemu built-in screenshot did not fire and PrintWindow failed: %s" % exc


def parse_case_evidence(text, case):
    summary = soak_log.summarize_log(text)
    map_samples = []
    for match in SMOKE_MAP_RE.finditer(text):
        if os.path.basename(match.group(2)).lower() == (case["map"] + ".unr").lower():
            map_samples.append((int(match.group(1)), int(match.group(3))))
    chars_by_tick = {}
    for match in XCHAR_RE.finditer(text):
        tick = int(match.group(1))
        chars_by_tick.setdefault(tick, []).append({
            "name": match.group(2),
            "class": match.group(3),
            "mesh": match.group(4),
            "tick": tick,
        })
    complete_ticks = [tick for tick, items in chars_by_tick.items() if len(items) >= case["bots"]]
    latest_char_tick = max(complete_ticks) if complete_ticks else (max(chars_by_tick.keys()) if chars_by_tick else 0)
    chars = chars_by_tick.get(latest_char_tick, [])
    class_counts = {}
    for item in chars:
        class_counts[item["class"]] = class_counts.get(item["class"], 0) + 1
    fps = [float(value) for value in PERF_RE.findall(text)]
    perf_by_time = {}
    for match in PERF_DETAIL_RE.finditer(text):
        perf_by_time[int(match.group(1))] = {
            "time": int(match.group(1)),
            "fps": float(match.group(2)),
            "textureCreates": int(match.group(3)),
            "textureUploads": int(match.group(4)),
            "textureLiveKB": int(match.group(5)),
            "textureUploadedKB": int(match.group(6)),
            "availKB": int(match.group(7)),
        }
    ordered_perf = [perf_by_time[key] for key in sorted(perf_by_time)]
    steady_perf = ordered_perf[-min(8, len(ordered_perf)):]
    skeletal_flicker_lines = XSKEL_FLICKER_RE.findall(text)
    summary.update({
        "activeMapSamples": len(map_samples),
        "activeMapLastTick": max([item[0] for item in map_samples] or [0]),
        "activeMapMaxBots": max([item[1] for item in map_samples] or [0]),
        "characterCount": len(chars),
        "characters": chars,
        "characterAuditTick": latest_char_tick,
        "characterClassCounts": class_counts,
        "fpsSamples": len(fps),
        "minFps": min(fps) if fps else None,
        "averageFps": (sum(fps) / len(fps)) if fps else None,
        "steadyPerfSamples": steady_perf,
        "steadyAverageFps": (
            sum(item["fps"] for item in steady_perf) / len(steady_perf)
            if steady_perf else None
        ),
        "steadyMinFps": min((item["fps"] for item in steady_perf), default=None),
        "steadyMaxTextureCreates": max(
            (item["textureCreates"] for item in steady_perf), default=None
        ),
        "steadyMaxTextureUploads": max(
            (item["textureUploads"] for item in steady_perf), default=None
        ),
        "skeletalFlickerAlertCount": len(skeletal_flicker_lines),
        "skeletalFlickerAlerts": skeletal_flicker_lines,
    })
    return summary


def run_case(args, stage, case, index, xiso_tool, config_path):
    safe_name = "%02d_%s" % (index, case["map"])
    run_dir = os.path.join(args.evidence_dir, safe_name)
    xemu_soak.ensure_dir(run_dir)
    screenshot_dir = os.path.join(run_dir, "screenshots")
    xemu_soak.ensure_dir(screenshot_dir)
    url = prepare_case(args, stage, case, run_dir)

    xemu_soak.build_xiso(stage, args.iso_path, xiso_tool)
    config_path = xemu_soak.write_xemu_config(args, args.iso_path)
    shutil.copy2(config_path, os.path.join(run_dir, "xemu_soak_runtime_config"))

    started = datetime.datetime.now().isoformat()
    proc = None
    accumulated = ""
    last_snapshot = ""
    live_started = None
    last_change = time.time()
    screenshots = []
    camera_screenshots = []
    loading_screenshots = []
    last_loading_frame = 0
    last_camera_tick = 0
    camera_burst_tick = 0
    camera_burst_index = ""
    camera_burst_class = ""
    camera_burst_frame = 0
    camera_burst_remaining = 0
    completed_camera_bursts = 0
    marker = ""
    ok = False
    poll_failures = 0
    first_poll_failure = None
    diagnostic_screen_captured = False
    symbols = xemu_poll.resolve_symbols(
        os.path.join(args.build_dir, "UnrealTournament.map"),
        os.path.join(args.build_dir, "default.xbe"),
    )
    poll_args = argparse.Namespace(timeout=args.poll_timeout, phys_delta="auto")
    required_tick = case.get("minimum_tick", args.minimum_tick)
    deadline = time.time() + args.boot_timeout + case["seconds"] + args.tick_timeout

    try:
        proc = xemu_soak.launch_xemu(args, config_path, run_dir)
        while time.time() < deadline:
            if proc.poll() is not None:
                marker = "xemu exited with code %s" % proc.returncode
                break
            try:
                _delta, values, snapshot = xemu_poll.poll_port(args.monitor_port, symbols, poll_args)
                poll_failures = 0
                first_poll_failure = None
                marker = ""
            except Exception as exc:
                poll_failures += 1
                if first_poll_failure is None:
                    first_poll_failure = time.time()
                # The HMP monitor becomes reachable before the title installs
                # its page tables. Preserve the visible state after 30 seconds,
                # then keep waiting for the configured boot timeout.
                if (
                    not diagnostic_screen_captured
                    and time.time() - first_poll_failure >= 30.0
                ):
                    path = os.path.join(screenshot_dir, "00_ram_log_wait.png")
                    reply = capture_screen(proc.pid, args.screenshot_dir, path)
                    diagnostic_screen_captured = os.path.isfile(path)
                    if not diagnostic_screen_captured:
                        with open(os.path.join(run_dir, "screendump_error.txt"), "a") as handle:
                            handle.write(reply + "\n")
                marker = "waiting for RAM log after %d polls: %s" % (poll_failures, exc)
                time.sleep(3)
                continue

            stamp = datetime.datetime.now().isoformat()
            with open(os.path.join(run_dir, "xemu_ram_log_latest.txt"), "w", errors="replace") as handle:
                handle.write(snapshot)
            if snapshot and snapshot != last_snapshot:
                last_snapshot = snapshot
                last_change = time.time()
                accumulated = xemu_soak.append_snapshot(accumulated, snapshot, stamp)
                with open(os.path.join(run_dir, "xemu_ram_log_accumulated.txt"), "w", errors="replace") as handle:
                    handle.write(accumulated)

            current = parse_case_evidence(accumulated or snapshot, case)
            if current["fatalCount"]:
                marker = current["fatalLines"][0]
                break
            if current["skeletalFlickerAlertCount"]:
                marker = current["skeletalFlickerAlerts"][-1]
                break

            new_camera_event = False
            camera_events = XSKELCAM_RE.findall(accumulated or snapshot)
            if camera_events:
                camera_tick, camera_index, camera_class = max(
                    camera_events, key=lambda item: int(item[0])
                )
                camera_tick = int(camera_tick)
                if (
                    camera_tick > last_camera_tick
                    and camera_burst_remaining == 0
                    and completed_camera_bursts == 0
                ):
                    new_camera_event = True
                    last_camera_tick = camera_tick
                    safe_class = re.sub(r"[^A-Za-z0-9_-]+", "_", camera_class.split(".")[-1])
                    path = os.path.join(
                        screenshot_dir,
                        "camera_%05d_%s_slot%s_frame1.png"
                        % (camera_tick, safe_class, camera_index),
                    )
                    reply = capture_screen(proc.pid, args.screenshot_dir, path)
                    if os.path.isfile(path):
                        camera_screenshots.append(path)
                        camera_burst_tick = camera_tick
                        camera_burst_index = camera_index
                        camera_burst_class = safe_class
                        camera_burst_frame = 1
                        camera_burst_remaining = 2
                    else:
                        with open(os.path.join(run_dir, "screendump_error.txt"), "a") as handle:
                            handle.write(reply + "\n")

            if not new_camera_event and camera_burst_remaining > 0:
                camera_burst_frame += 1
                path = os.path.join(
                    screenshot_dir,
                    "camera_%05d_%s_slot%s_frame%d.png"
                    % (
                        camera_burst_tick,
                        camera_burst_class,
                        camera_burst_index,
                        camera_burst_frame,
                    ),
                )
                reply = capture_screen(proc.pid, args.screenshot_dir, path)
                if os.path.isfile(path):
                    camera_screenshots.append(path)
                    camera_burst_remaining -= 1
                    if camera_burst_remaining == 0:
                        completed_camera_bursts += 1
                else:
                    camera_burst_remaining = 0
                    with open(os.path.join(run_dir, "screendump_error.txt"), "a") as handle:
                        handle.write(reply + "\n")

            if args.capture_loading_animation and len(loading_screenshots) < 4:
                loading_frames = re.findall(
                    r"XLOADANIM frame=(\d+) step=(\d+)", accumulated or snapshot
                )
                if loading_frames:
                    frame_number, step_number = [int(value) for value in loading_frames[-1]]
                    if frame_number > last_loading_frame:
                        last_loading_frame = frame_number
                        path = os.path.join(
                            screenshot_dir,
                            "loading_%02d_frame_%d_step_%d.png"
                            % (len(loading_screenshots) + 1, frame_number, step_number),
                        )
                        reply = capture_screen(proc.pid, args.screenshot_dir, path)
                        if os.path.isfile(path):
                            loading_screenshots.append(path)
                        else:
                            with open(os.path.join(run_dir, "screendump_error.txt"), "a") as handle:
                                handle.write(reply + "\n")

            if current["activeMapMaxBots"] >= case["bots"] and live_started is None:
                live_started = time.time()
                path = os.path.join(screenshot_dir, "01_live_start.png")
                reply = capture_screen(proc.pid, args.screenshot_dir, path)
                if os.path.isfile(path):
                    screenshots.append(path)
                else:
                    with open(os.path.join(run_dir, "screendump_error.txt"), "a") as handle:
                        handle.write(reply + "\n")

            if live_started is not None:
                elapsed = time.time() - live_started
                halfway = case["seconds"] / 2.0
                if elapsed >= halfway and len(screenshots) < 2:
                    path = os.path.join(screenshot_dir, "02_mid_soak.png")
                    reply = capture_screen(proc.pid, args.screenshot_dir, path)
                    if os.path.isfile(path):
                        screenshots.append(path)
                    else:
                        with open(os.path.join(run_dir, "screendump_error.txt"), "a") as handle:
                            handle.write(reply + "\n")
                if (
                    elapsed >= case["seconds"]
                    and current["activeMapLastTick"] >= required_tick
                    and completed_camera_bursts >= 1
                    and camera_burst_remaining == 0
                ):
                    path = os.path.join(screenshot_dir, "03_final.png")
                    reply = capture_screen(proc.pid, args.screenshot_dir, path)
                    if os.path.isfile(path):
                        screenshots.append(path)
                    else:
                        with open(os.path.join(run_dir, "screendump_error.txt"), "a") as handle:
                            handle.write(reply + "\n")
                    marker = "completed %.1fs live soak" % elapsed
                    ok = True
                    break

            if time.time() - last_change >= args.stall_seconds:
                marker = "RAM log stalled for %.1fs" % (time.time() - last_change)
                break
            time.sleep(args.poll_interval)

        if not marker:
            marker = "timeout before requested live soak completed"
    finally:
        xemu_soak.terminate_process(proc)
        xemu_soak.close_process_streams(proc)
        xemu_soak.stop_xemu_for_config(config_path)
        xemu_soak.copy_xemu_logs(args, run_dir)

    ended = datetime.datetime.now().isoformat()
    summary = parse_case_evidence(accumulated or last_snapshot, case)
    allowed_classes = set(item[1] for item in ROSTER)
    expected_classes = set(
        roster_entry(index, case.get("roster_offset", 0), case.get("roster_span"))[1]
        for index in range(case["bots"])
    )
    seen_classes = set(item["class"] for item in summary["characters"])
    unexpected_classes = seen_classes.difference(allowed_classes)
    missing_classes = expected_classes.difference(seen_classes)
    ps2_classes = set(item for item in seen_classes if item.startswith("UTPS2Characters."))
    master_chief_count = summary["characterClassCounts"].get(
        "HaloMasterChief.HaloMasterChiefBot", 0
    )
    summary.update({
        "map": case["map"],
        "game": case["game"],
        "playerClass": case["player"],
        "requestedBots": case["bots"],
        "requestedSeconds": case["seconds"],
        "requiredTick": required_tick,
        "reason": case["reason"],
        "url": url,
        "started": started,
        "ended": ended,
        "marker": marker,
        "xemuPid": proc.pid if proc else None,
        "screenshots": screenshots,
        "cameraScreenshots": camera_screenshots,
        "completedCameraBursts": completed_camera_bursts,
        "loadingAnimationScreenshots": loading_screenshots,
        "loadingAnimationLastFrame": last_loading_frame,
        "allowedBotClasses": sorted(allowed_classes),
        "expectedBotClasses": sorted(expected_classes),
        "seenBotClasses": sorted(seen_classes),
        "missingBotClasses": sorted(missing_classes),
        "unexpectedBotClasses": sorted(unexpected_classes),
        "distinctPs2BotClasses": len(ps2_classes),
        "masterChiefBotCount": master_chief_count,
        "customRosterOnly": not unexpected_classes,
        "ok": bool(
            ok
            and summary["fatalCount"] == 0
            and summary["activeMapMaxBots"] >= case["bots"]
            and summary["activeMapLastTick"] >= required_tick
            and summary["characterCount"] >= case["bots"]
            and completed_camera_bursts >= 1
            and summary["steadyAverageFps"] is not None
            and summary["steadyAverageFps"] >= args.min_steady_fps
            and summary["steadyMaxTextureUploads"] is not None
            and summary["steadyMaxTextureUploads"] <= args.max_steady_texture_uploads
            and summary["skeletalFlickerAlertCount"] == 0
            and not unexpected_classes
            and not missing_classes
        ),
    })
    with open(os.path.join(run_dir, "summary.json"), "w") as handle:
        json.dump(summary, handle, indent=2, sort_keys=True)
    return summary, config_path


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default=DEFAULT_BUILD)
    parser.add_argument("--runtime-source", default=DEFAULT_RUNTIME)
    parser.add_argument("--xemu-root", default=DEFAULT_XEMU_ROOT)
    parser.add_argument("--xemu-instance", default=DEFAULT_XEMU_INSTANCE)
    parser.add_argument("--hdd", default=DEFAULT_HDD)
    parser.add_argument("--eeprom-source", default=DEFAULT_EEPROM)
    parser.add_argument("--monitor-port", type=int, default=4477)
    parser.add_argument("--poll-interval", type=float, default=8.0)
    parser.add_argument("--poll-timeout", type=float, default=8.0)
    parser.add_argument("--stall-seconds", type=float, default=150.0)
    parser.add_argument("--boot-timeout", type=float, default=240.0)
    parser.add_argument("--minimum-tick", type=int, default=300)
    parser.add_argument("--min-steady-fps", type=float, default=15.0)
    parser.add_argument("--max-steady-texture-uploads", type=int, default=32)
    parser.add_argument(
        "--tick-timeout",
        type=float,
        default=600.0,
        help="Additional wall time allowed for rendered cases to reach --minimum-tick",
    )
    parser.add_argument("--cases", nargs="*", help="Optional map names from the built-in matrix")
    parser.add_argument("--seconds-override", type=int, help="Override each selected live-soak duration")
    parser.add_argument("--bots-override", type=int, help="Override bot count for each selected case")
    parser.add_argument(
        "--roster-span",
        type=int,
        help="Cycle only this many adjacent custom classes while retaining the requested bot count",
    )
    parser.add_argument(
        "--roster-offset",
        type=int,
        help="Start the controlled custom roster at this zero-based class index",
    )
    parser.add_argument(
        "--capture-loading-animation",
        action="store_true",
        help="Capture up to four loading-wheel frames when XLOADANIM advances",
    )
    parser.add_argument("--evidence-dir")
    args = parser.parse_args(argv)

    args.build_dir = os.path.abspath(args.build_dir)
    args.runtime_source = os.path.abspath(args.runtime_source)
    args.xemu_root = os.path.abspath(args.xemu_root)
    args.xemu_instance = os.path.abspath(args.xemu_instance)
    args.xemu = xemu_soak.ensure_instance_xemu(
        xemu_soak.xemu_exe_from_root(args.xemu_root), args.xemu_instance
    )
    args.bootrom = os.path.join(args.xemu_root, "MCPX", "mcpx_1.0.bin")
    args.flashrom = os.path.join(args.xemu_root, "BIOS", "xbox-4627_debug.bin")
    args.iso_path = os.path.join(args.xemu_instance, "ut99_deep_soak_current.iso")
    args.stage_dir = os.path.join(XBOX_DIR, "build_cli", "xemu_deep_soak_stage")
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    args.evidence_dir = os.path.abspath(
        args.evidence_dir or os.path.join(XBOX_DIR, "build_cli", "xemu_deep_soak_" + stamp)
    )
    args.screenshot_dir = os.path.join(args.evidence_dir, "xemu_internal_screenshots")
    args.display_backend = ""

    for path, label in (
        (os.path.join(args.build_dir, "default.xbe"), "default.xbe"),
        (os.path.join(args.build_dir, "UnrealTournament.map"), "linker map"),
        (args.hdd, "Xemu HDD"),
        (args.eeprom_source, "EEPROM"),
        (args.bootrom, "MCPX ROM"),
        (args.flashrom, "BIOS"),
    ):
        xemu_soak.ensure_file(path, label)
    xemu_soak.ensure_dir(args.evidence_dir)
    xemu_soak.ensure_dir(args.screenshot_dir)

    selected = [dict(case) for case in CASES]
    if args.cases:
        wanted = set(item.lower() for item in args.cases)
        selected = [case for case in CASES if case["map"].lower() in wanted]
        missing = wanted.difference(case["map"].lower() for case in selected)
        if missing:
            raise RuntimeError("Unknown matrix case(s): " + ", ".join(sorted(missing)))
    if args.seconds_override is not None:
        if args.seconds_override <= 0:
            raise RuntimeError("--seconds-override must be positive")
        for case in selected:
            case["seconds"] = args.seconds_override
    if args.bots_override is not None:
        if args.bots_override <= 0 or args.bots_override > 15:
            raise RuntimeError("--bots-override must be between 1 and 15")
        for case in selected:
            case["bots"] = args.bots_override
    if args.roster_span is not None:
        if args.roster_span <= 0 or args.roster_span > len(ROSTER):
            raise RuntimeError("--roster-span must be between 1 and %d" % len(ROSTER))
        for case in selected:
            case["roster_span"] = args.roster_span
    if args.roster_offset is not None:
        if args.roster_offset < 0 or args.roster_offset >= len(ROSTER):
            raise RuntimeError("--roster-offset must be between 0 and %d" % (len(ROSTER) - 1))
        for case in selected:
            case["roster_offset"] = args.roster_offset

    stage, copied = prepare_base(args)
    xiso_tool = xemu_soak.find_xiso_tool(os.path.dirname(args.build_dir))
    config_path = os.path.join(args.xemu_instance, "xemu_soak_runtime_config")
    summaries = []
    try:
        for index, case in enumerate(selected, 1):
            print("[%d/%d] %s: %s" % (index, len(selected), case["map"], case["reason"]))
            summary, config_path = run_case(args, stage, case, index, xiso_tool, config_path)
            summaries.append(summary)
            print(
                "  ok=%s bots=%s minAvailKB=%s lastTick=%s fpsMin=%s steadyFps=%s steadyTexUp=%s marker=%s"
                % (
                    summary["ok"],
                    summary["activeMapMaxBots"],
                    summary["minAvailKB"],
                    summary["activeMapLastTick"],
                    summary["minFps"],
                    summary["steadyAverageFps"],
                    summary["steadyMaxTextureUploads"],
                    summary["marker"],
                )
            )
    finally:
        xemu_soak.stop_xemu_for_config(config_path)

    aggregate = {
        "buildDir": args.build_dir,
        "runtimeSource": args.runtime_source,
        "stageDir": stage,
        "stageOverlayFiles": copied,
        "allPassed": bool(summaries and all(item["ok"] for item in summaries)),
        "cases": summaries,
    }
    with open(os.path.join(args.evidence_dir, "aggregate_summary.json"), "w") as handle:
        json.dump(aggregate, handle, indent=2, sort_keys=True)
    print("Evidence: " + args.evidence_dir)
    print("All passed: %s" % aggregate["allPassed"])
    return 0 if aggregate["allPassed"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
