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

from PIL import Image, ImageGrab

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

SKELETAL_PROOF_STATES = (
    "idle",
    "walk",
    "run",
    "jump",
    "attack",
    "hit",
    "dodge",
    "swim",
    "crouch",
    "death",
)

CASES = [
    {
        "map": "DM-Deck16][",
        "game": "Botpack.DeathMatchPlus",
        "player": "Botpack.TMale1",
        "bots": 1,
        "roster_offset": 2,
        "roster_span": 1,
        "seconds": 20,
        "reason": "well-lit stock arena for close skeletal, winding, material, and weapon inspection",
    },
    {
        "map": "DM-Morpheus",
        "game": "Botpack.DeathMatchPlus",
        "player": "Botpack.TMale1",
        "bots": 1,
        "roster_offset": 2,
        "roster_span": 1,
        "seconds": 20,
        "reason": "open stock arena for unobstructed skeletal, weapon, and chase-camera fidelity",
    },
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
XSKELCAM_RE = re.compile(
    r"XSKELCAM tick=(\d+) index=(\d+) class=Class "
    r"([A-Za-z0-9_]+\.[A-Za-z0-9_]+)"
)
XLIGHTCAM_RE = re.compile(
    r"XLIGHTCAM draw=(\d+) slot=(\d+) count=(\d+) map=([^\s]+).*?wouldClear=(\d+)"
)
PERF_RE = re.compile(r"\bPERF fps=([0-9.]+)")
PERF_DETAIL_RE = re.compile(
    r"UT99XDBG t=(\d+).*?\bPERF fps=([0-9.]+).*?"
    r"texNew=(\d+) texUp=(\d+).*?liveKB=(\d+) texKB=(\d+) availKB=(\d+)"
)
XSKEL_FLICKER_RE = re.compile(r"[^\r\n]*XSKELFLICKER[^\r\n]*")
XSKEL_STATE_RE = re.compile(
    r"XSKELSTATE tick=(\d+) class=Class (\S+) mesh=(.+?) "
    r"state=([a-z]+) seq=(\S+) frame=([0-9.]+) "
    r"numframes=(\d+) rate=([0-9.]+) found=([01])"
)


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


def expected_case_classes(case):
    if case.get("bot_class"):
        return set([case["bot_class"]])
    return set(
        roster_entry(index, case.get("roster_offset", 0), case.get("roster_span"))[1]
        for index in range(case["bots"])
    )


def patch_bot_roster(path, count, roster_offset=0, roster_span=None, bot_class_override=None):
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
            if bot_class_override:
                name = bot_class_override.split(".")[-1]
                bot_class = bot_class_override
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
    url = (
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
    if case.get("mutator"):
        url += "?Mutator=" + case["mutator"]
    return url


def prepare_base(args):
    stage = os.path.abspath(args.stage_dir)
    if not os.path.isdir(stage):
        xemu_soak.ensure_dir(os.path.dirname(stage))
        xemu_soak.copy_runtime_tree(args.runtime_source, stage)
    copied = xemu_soak.copy_build_overlay(args.build_dir, stage)
    retired_bake = os.path.join(stage, "System", "UTPS2Baked.u")
    if os.path.isfile(retired_bake):
        os.remove(retired_bake)
    soak_log.remove_old_logs(stage)
    for marker in (
        "XboxSoakMapList.ini",
        "XboxStartURL.ini",
        "XboxCharacterSoak.ini",
        "XboxLightingProof.ini",
        "XboxSkeletalStateProof.ini",
        "XboxSkaarjSkinProof.ini",
        "XboxIssueMapSmoke.ini",
        "XboxSoakSmoke.ini",
        "XboxInstantMenuProofSmoke.ini",
        "XboxInstantMenuProof_DM.ini",
        "XboxInstantMenuProof_CTF.ini",
        "XboxInstantMenuProof_DOM.ini",
        "XboxInstantMenuProof_AS.ini",
        "XboxInstantMenuProof_JB.ini",
    ):
        path = os.path.join(stage, marker)
        if os.path.isfile(path):
            os.remove(path)
    if args.lighting_proof:
        with open(os.path.join(stage, "XboxLightingProof.ini"), "w") as handle:
            handle.write("; Deterministic map-lighting viewpoint qualification\n")
    else:
        with open(os.path.join(stage, "XboxCharacterSoak.ini"), "w") as handle:
            handle.write("; Log exact bot class, mesh, and skin during Xemu stress\n")
    if args.skeletal_state_proof:
        with open(os.path.join(stage, "XboxSkeletalStateProof.ini"), "w") as handle:
            handle.write("; Deterministic post-simulation animation-state qualification\n")
    if args.skaarj_skin_proof:
        with open(os.path.join(stage, "XboxSkaarjSkinProof.ini"), "w") as handle:
            handle.write("; Repeated harmless hits for Skaarj blood/material qualification\n")
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
                case.get("bot_class"),
            )

    if args.frontend_loading_proof:
        marker_by_mode = {
            "issue": "XboxIssueMapSmoke.ini",
            "soak": "XboxSoakSmoke.ini",
            "instant-dm": "XboxInstantMenuProof_DM.ini",
        }
        marker_name = marker_by_mode[args.frontend_loading_proof]
        start_url_path = os.path.join(stage, "XboxStartURL.ini")
        if os.path.isfile(start_url_path):
            os.remove(start_url_path)
        with open(os.path.join(stage, marker_name), "w") as handle:
            handle.write("; Frontend-driven loading-spinner proof\n")
        url = "frontend-proof:" + marker_name
        proof_markers = [marker_name]
        if args.lighting_proof:
            proof_markers.append("XboxLightingProof.ini")
        else:
            proof_markers.append("XboxCharacterSoak.ini")
    else:
        url = build_url(case)
        soak_log.write_lines(
            os.path.join(stage, "XboxStartURL.ini"),
            ["; Isolated rendered Xemu stress case", "StartURL=" + url],
        )
        proof_markers = ["XboxStartURL.ini"]
        if args.lighting_proof:
            proof_markers.append("XboxLightingProof.ini")
        else:
            proof_markers.append("XboxCharacterSoak.ini")
    if args.skeletal_state_proof:
        proof_markers.append("XboxSkeletalStateProof.ini")
    if args.skaarj_skin_proof:
        proof_markers.append("XboxSkaarjSkinProof.ini")
    for name in proof_markers:
        shutil.copy2(os.path.join(stage, name), os.path.join(run_dir, name))
    for name in ("Default.ini", "UnrealTournament.ini", "User.ini"):
        shutil.copy2(os.path.join(stage, "System", name), os.path.join(run_dir, name))
    return url


def capture_screen(pid, source_dir, output_path, monitor_port=None):
    if os.name != "nt":
        return "built-in screenshot trigger requires Windows"

    user32 = ctypes.windll.user32
    handles = []
    enum_proc_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def collect_fast_window(hwnd, _lparam):
        process_id = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(process_id))
        if process_id.value == pid:
            handles.append(hwnd)
        return True

    callback = enum_proc_type(collect_fast_window)
    user32.EnumWindows(callback, 0)
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

    hmp_error = None
    if monitor_port is not None:
        ppm_path = os.path.splitext(output_path)[0] + ".ppm"
        try:
            if os.path.isfile(ppm_path):
                os.remove(ppm_path)
            sock = xemu_poll.connect_monitor(monitor_port, 5.0)
            try:
                monitor_path = os.path.abspath(ppm_path).replace("\\", "/")
                reply = xemu_poll.monitor_cmd(
                    sock,
                    'screendump "%s"' % monitor_path,
                    0.5,
                )
            finally:
                sock.close()
            deadline = time.time() + 5.0
            while time.time() < deadline and not os.path.isfile(ppm_path):
                time.sleep(0.05)
            if os.path.isfile(ppm_path) and os.path.getsize(ppm_path) > 0:
                with Image.open(ppm_path) as image:
                    image.save(output_path, "PNG")
                os.remove(ppm_path)
                return "captured from Xemu HMP screendump"
            if os.path.isfile(ppm_path):
                os.remove(ppm_path)
            hmp_error = "Xemu HMP screendump produced no image: " + reply.strip()
        except Exception as exc:
            hmp_error = "Xemu HMP screendump failed: %s" % exc

    handles = []

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
        return hmp_error or "Xemu screenshot did not fire and no visible top-level window was found"

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
    expected_classes = expected_case_classes(case)
    frontend_loading_proof = bool(args.frontend_loading_proof)
    lighting_proof = bool(args.lighting_proof)
    required_camera_bursts = 0 if (frontend_loading_proof or lighting_proof) else min(
        max(1, case.get("camera_burst_limit", args.camera_burst_limit)),
        len(expected_classes),
    )
    camera_frames_per_burst = max(
        1,
        case.get("camera_frames", args.camera_frames),
    )
    required_state_keys = set(
        (bot_class, state_name)
        for bot_class in expected_classes
        for state_name in SKELETAL_PROOF_STATES
    ) if args.skeletal_state_proof and not frontend_loading_proof else set()

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
    lighting_screenshots = []
    loading_screenshots = []
    last_loading_frame = 0
    captured_lighting_slots = set()
    lighting_capture_failures = []
    last_camera_tick = 0
    active_camera_tick = 0
    active_camera_class = ""
    camera_burst_tick = 0
    camera_burst_index = ""
    camera_burst_class = ""
    camera_burst_class_full = ""
    camera_burst_frame = 0
    camera_burst_remaining = 0
    completed_camera_bursts = 0
    captured_camera_classes = set()
    camera_capture_failures = []
    state_screenshots = []
    captured_state_keys = set()
    state_capture_failures = []
    state_capture_retries = []
    marker = ""
    ok = False
    poll_failures = 0
    first_poll_failure = None
    diagnostic_screen_captured = False
    symbols = xemu_poll.resolve_symbols(
        os.path.join(args.build_dir, "UnrealTournament.map"),
        os.path.join(args.build_dir, "default.xbe"),
    )
    poll_args = argparse.Namespace(
        timeout=args.poll_timeout,
        phys_delta="auto",
        tail_bytes=4096,
    )
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
                poll_args.phys_delta = "0" if _delta is None else hex(_delta)
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
                    reply = capture_screen(proc.pid, args.screenshot_dir, path, args.monitor_port)
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
            camera_events = [
                event
                for event in XSKELCAM_RE.findall(accumulated or snapshot)
                if event[2] in expected_classes
            ]
            latest_camera_tick = 0
            if camera_events:
                camera_tick, camera_index, camera_class = max(
                    camera_events, key=lambda item: int(item[0])
                )
                camera_tick = int(camera_tick)
                latest_camera_tick = camera_tick
                if camera_tick >= active_camera_tick:
                    # XSKELCAM is repeated as a heartbeat while the same bot
                    # remains targeted. Only a real handoff starts a new epoch;
                    # otherwise early state events get discarded every 60 ticks.
                    if camera_class != active_camera_class:
                        active_camera_tick = camera_tick
                    active_camera_class = camera_class
                if (
                    camera_tick > last_camera_tick
                    and camera_burst_remaining == 0
                ):
                    last_camera_tick = camera_tick
                    if (
                        camera_class not in captured_camera_classes
                        and completed_camera_bursts < required_camera_bursts
                    ):
                        new_camera_event = True
                        safe_class = re.sub(
                            r"[^A-Za-z0-9_-]+",
                            "_",
                            camera_class.split(".")[-1],
                        )
                        path = os.path.join(
                            screenshot_dir,
                            "camera_%05d_%s_slot%s_frame1.png"
                            % (camera_tick, safe_class, camera_index),
                        )
                        time.sleep(0.40)
                        reply = capture_screen(
                            proc.pid,
                            args.screenshot_dir,
                            path,
                            args.monitor_port,
                        )
                        if os.path.isfile(path):
                            camera_screenshots.append(path)
                            camera_burst_tick = camera_tick
                            camera_burst_index = camera_index
                            camera_burst_class = safe_class
                            camera_burst_class_full = camera_class
                            camera_burst_frame = 1
                            camera_burst_remaining = camera_frames_per_burst - 1
                            while camera_burst_remaining > 0:
                                time.sleep(0.20)
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
                                reply = capture_screen(
                                    proc.pid,
                                    args.screenshot_dir,
                                    path,
                                    args.monitor_port,
                                )
                                if not os.path.isfile(path):
                                    camera_capture_failures.append(
                                        "%s tick=%d frame=%d: %s"
                                        % (
                                            camera_burst_class_full,
                                            camera_burst_tick,
                                            camera_burst_frame,
                                            reply,
                                        )
                                    )
                                    with open(
                                        os.path.join(run_dir, "screendump_error.txt"),
                                        "a",
                                    ) as handle:
                                        handle.write(reply + "\n")
                                    break
                                camera_screenshots.append(path)
                                camera_burst_remaining -= 1
                            if camera_burst_remaining == 0:
                                captured_camera_classes.add(camera_burst_class_full)
                                completed_camera_bursts += 1
                            camera_burst_remaining = 0
                        else:
                            camera_capture_failures.append(
                                "%s tick=%d: %s" % (camera_class, camera_tick, reply)
                            )
                            with open(os.path.join(run_dir, "screendump_error.txt"), "a") as handle:
                                handle.write(reply + "\n")

            if not new_camera_event and camera_burst_remaining > 0:
                if latest_camera_tick > camera_burst_tick:
                    camera_capture_failures.append(
                        "%s tick=%d interrupted at frame %d"
                        % (
                            camera_burst_class_full,
                            camera_burst_tick,
                            camera_burst_frame + 1,
                        )
                    )
                    camera_burst_remaining = 0
                else:
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
                    reply = capture_screen(
                        proc.pid,
                        args.screenshot_dir,
                        path,
                        args.monitor_port,
                    )
                    if os.path.isfile(path):
                        camera_screenshots.append(path)
                        camera_burst_remaining -= 1
                        if camera_burst_remaining == 0:
                            captured_camera_classes.add(camera_burst_class_full)
                            completed_camera_bursts += 1
                    else:
                        camera_capture_failures.append(
                            "%s tick=%d frame=%d: %s"
                            % (
                                camera_burst_class_full,
                                camera_burst_tick,
                                camera_burst_frame,
                                reply,
                            )
                        )
                        camera_burst_remaining = 0
                        with open(os.path.join(run_dir, "screendump_error.txt"), "a") as handle:
                            handle.write(reply + "\n")

            if args.skeletal_state_proof and not new_camera_event:
                state_events = [
                    event
                    for event in XSKEL_STATE_RE.findall(accumulated or snapshot)
                    if active_camera_class
                    and event[1] == active_camera_class
                    and int(event[0]) >= active_camera_tick
                    and event[3] in SKELETAL_PROOF_STATES
                ]
                if state_events:
                    state_event = max(state_events, key=lambda item: int(item[0]))
                    (
                        state_tick,
                        state_class,
                        _state_mesh,
                        state_name,
                        state_sequence,
                        state_frame,
                        _state_numframes,
                        _state_rate,
                        state_found,
                    ) = state_event
                    state_key = (state_class, state_name)
                    if state_found != "1":
                        marker = (
                            "missing skeletal proof sequence class=%s state=%s"
                            % state_key
                        )
                        break
                    # Leave enough time for every requested screendump and the
                    # post-capture verification to finish before the next state.
                    state_capture_window_open = float(state_frame) <= 0.40
                    if (
                        state_key not in captured_state_keys
                        and state_capture_window_open
                    ):
                        safe_class = re.sub(
                            r"[^A-Za-z0-9_-]+",
                            "_",
                            state_class.split(".")[-1],
                        )
                        captured_paths = []
                        for state_frame_index in range(1, args.state_frames + 1):
                            if state_frame_index == 1:
                                time.sleep(0.10)
                            else:
                                time.sleep(0.16)
                            path = os.path.join(
                                screenshot_dir,
                                "state_%05d_%s_%s_%s_frame%d.png"
                                % (
                                    int(state_tick),
                                    safe_class,
                                    state_name,
                                    state_sequence,
                                    state_frame_index,
                                ),
                            )
                            reply = capture_screen(
                                proc.pid,
                                args.screenshot_dir,
                                path,
                                args.monitor_port,
                            )
                            if not os.path.isfile(path):
                                state_capture_failures.append(
                                    "%s %s tick=%s frame=%d: %s"
                                    % (
                                        state_class,
                                        state_name,
                                        state_tick,
                                        state_frame_index,
                                        reply,
                                    )
                                )
                                with open(
                                    os.path.join(run_dir, "screendump_error.txt"),
                                    "a",
                                ) as handle:
                                    handle.write(reply + "\n")
                                break
                            captured_paths.append(path)
                        if len(captured_paths) == args.state_frames:
                            capture_still_current = False
                            retry_reason = "post-capture state unavailable"
                            try:
                                post_delta, _post_values, post_snapshot = xemu_poll.poll_port(
                                    args.monitor_port,
                                    symbols,
                                    poll_args,
                                )
                                poll_args.phys_delta = (
                                    "0" if post_delta is None else hex(post_delta)
                                )
                                post_camera_events = [
                                    event
                                    for event in XSKELCAM_RE.findall(post_snapshot)
                                    if event[2] in expected_classes
                                ]
                                post_state_events = [
                                    event
                                    for event in XSKEL_STATE_RE.findall(post_snapshot)
                                    if int(event[0]) >= active_camera_tick
                                    and event[3] in SKELETAL_PROOF_STATES
                                ]
                                post_camera = (
                                    max(post_camera_events, key=lambda item: int(item[0]))
                                    if post_camera_events else None
                                )
                                post_state = (
                                    max(post_state_events, key=lambda item: int(item[0]))
                                    if post_state_events else None
                                )
                                capture_still_current = bool(
                                    post_state
                                    and post_state[1] == state_class
                                    and post_state[3] == state_name
                                    and post_state[8] == "1"
                                )
                                if not capture_still_current:
                                    retry_reason = (
                                        "transition camera=%s state=%s"
                                        % (
                                            (
                                                post_camera[2]
                                                if post_camera
                                                else (
                                                    post_state[1]
                                                    if post_state
                                                    else "none"
                                                )
                                            ),
                                            post_state[3] if post_state else "none",
                                        )
                                    )
                            except Exception as exc:
                                retry_reason = "post-capture poll failed: %s" % exc

                            if capture_still_current:
                                state_screenshots.extend(captured_paths)
                                captured_state_keys.add(state_key)
                            else:
                                state_capture_retries.append(
                                    "%s:%s tick=%s %s"
                                    % (
                                        state_class,
                                        state_name,
                                        state_tick,
                                        retry_reason,
                                    )
                                )
                                for captured_path in captured_paths:
                                    if os.path.isfile(captured_path):
                                        os.remove(captured_path)

            if (
                lighting_proof
                and current["activeMapSamples"] > 0
                and len(lighting_screenshots) < args.lighting_proof_screenshots
            ):
                lighting_events = XLIGHTCAM_RE.findall(accumulated or snapshot)
                for draw, slot, count, map_name, would_clear in lighting_events:
                    if int(draw) < 120:
                        continue
                    slot_index = int(slot)
                    if slot_index in captured_lighting_slots:
                        continue
                    safe_map = re.sub(r"[^A-Za-z0-9_-]+", "_", map_name)
                    path = os.path.join(
                        screenshot_dir,
                        "lighting_%02d_slot%d_draw%s_clear%s_%s.png"
                        % (
                            len(lighting_screenshots) + 1,
                            slot_index,
                            draw,
                            would_clear,
                            safe_map,
                        ),
                    )
                    time.sleep(0.12)
                    reply = capture_screen(
                        proc.pid,
                        args.screenshot_dir,
                        path,
                        args.monitor_port,
                    )
                    if os.path.isfile(path):
                        lighting_screenshots.append(path)
                        captured_lighting_slots.add(slot_index)
                    else:
                        lighting_capture_failures.append(
                            "slot=%s draw=%s map=%s: %s"
                            % (slot, draw, map_name, reply)
                        )
                        with open(os.path.join(run_dir, "screendump_error.txt"), "a") as handle:
                            handle.write(reply + "\n")
                    break

            loading_target = args.loading_proof_screenshots if frontend_loading_proof else 4
            if args.capture_loading_animation and len(loading_screenshots) < loading_target:
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
                        reply = capture_screen(proc.pid, args.screenshot_dir, path, args.monitor_port)
                        if os.path.isfile(path):
                            loading_screenshots.append(path)
                        else:
                            with open(os.path.join(run_dir, "screendump_error.txt"), "a") as handle:
                                handle.write(reply + "\n")

            if frontend_loading_proof:
                text = accumulated or snapshot
                if (
                    len(loading_screenshots) >= args.loading_proof_screenshots
                    and re.search(r"XLOADANIM end frames=(\d+)", text)
                ):
                    marker = "captured frontend loading animation"
                    ok = True
                    break

            map_live_enough = (
                current["activeMapSamples"] > 0
                if lighting_proof
                else current["activeMapMaxBots"] >= case["bots"]
            )
            if map_live_enough and live_started is None:
                live_started = time.time()
                if not args.skip_general_screenshots:
                    path = os.path.join(screenshot_dir, "01_live_start.png")
                    reply = capture_screen(
                        proc.pid,
                        args.screenshot_dir,
                        path,
                        args.monitor_port,
                    )
                    if os.path.isfile(path):
                        screenshots.append(path)
                    else:
                        with open(os.path.join(run_dir, "screendump_error.txt"), "a") as handle:
                            handle.write(reply + "\n")

            if live_started is not None:
                elapsed = time.time() - live_started
                halfway = case["seconds"] / 2.0
                if (
                    not args.skip_general_screenshots
                    and elapsed >= halfway
                    and len(screenshots) < 2
                ):
                    path = os.path.join(screenshot_dir, "02_mid_soak.png")
                    reply = capture_screen(proc.pid, args.screenshot_dir, path, args.monitor_port)
                    if os.path.isfile(path):
                        screenshots.append(path)
                    else:
                        with open(os.path.join(run_dir, "screendump_error.txt"), "a") as handle:
                            handle.write(reply + "\n")
                if (
                    elapsed >= case["seconds"]
                    and current["activeMapLastTick"] >= required_tick
                    and (
                        not lighting_proof
                        or len(captured_lighting_slots) >= args.lighting_proof_screenshots
                    )
                    and completed_camera_bursts >= required_camera_bursts
                    and camera_burst_remaining == 0
                    and required_state_keys.issubset(captured_state_keys)
                ):
                    if not args.skip_general_screenshots:
                        path = os.path.join(screenshot_dir, "03_final.png")
                        reply = capture_screen(
                            proc.pid,
                            args.screenshot_dir,
                            path,
                            args.monitor_port,
                        )
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
    allowed_classes.update(expected_classes)
    seen_classes = set(item["class"] for item in summary["characters"])
    unexpected_classes = seen_classes.difference(allowed_classes)
    missing_classes = expected_classes.difference(seen_classes)
    missing_camera_classes = expected_classes.difference(captured_camera_classes)
    camera_coverage_complete = (
        len(captured_camera_classes.intersection(expected_classes))
        >= required_camera_bursts
    )
    missing_state_keys = required_state_keys.difference(captured_state_keys)
    state_coverage_complete = not missing_state_keys
    loading_proof_complete = (
        not frontend_loading_proof
        or len(loading_screenshots) >= args.loading_proof_screenshots
    )
    lighting_proof_complete = (
        not lighting_proof
        or len(captured_lighting_slots) >= args.lighting_proof_screenshots
    )
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
        "lightingProofEnabled": lighting_proof,
        "lightingProofComplete": lighting_proof_complete,
        "lightingProofRequiredScreenshots": args.lighting_proof_screenshots if lighting_proof else 0,
        "lightingScreenshots": lighting_screenshots,
        "capturedLightingSlots": sorted(captured_lighting_slots),
        "lightingCaptureFailures": lighting_capture_failures,
        "completedCameraBursts": completed_camera_bursts,
        "requiredCameraBursts": required_camera_bursts,
        "cameraFramesPerBurst": camera_frames_per_burst,
        "capturedCameraClasses": sorted(captured_camera_classes),
        "missingCameraClasses": sorted(missing_camera_classes),
        "cameraCaptureFailures": camera_capture_failures,
        "cameraCoverageComplete": camera_coverage_complete,
        "skeletalStateProofEnabled": args.skeletal_state_proof,
        "stateScreenshots": state_screenshots,
        "capturedStateKeys": [
            "%s:%s" % key for key in sorted(captured_state_keys)
        ],
        "missingStateKeys": [
            "%s:%s" % key for key in sorted(missing_state_keys)
        ],
        "stateCaptureFailures": state_capture_failures,
        "stateCaptureRetries": state_capture_retries,
        "stateCoverageComplete": state_coverage_complete,
        "frontendLoadingProof": frontend_loading_proof,
        "frontendLoadingProofMode": args.frontend_loading_proof or "",
        "loadingProofComplete": loading_proof_complete,
        "loadingProofRequiredScreenshots": args.loading_proof_screenshots if frontend_loading_proof else 0,
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
            (
                ok
                and summary["fatalCount"] == 0
                and loading_proof_complete
            )
            if frontend_loading_proof else
            (
                ok
                and summary["fatalCount"] == 0
                and summary["activeMapLastTick"] >= required_tick
                and lighting_proof_complete
                and not lighting_capture_failures
                and summary["steadyAverageFps"] is not None
                and summary["steadyAverageFps"] >= args.min_steady_fps
                and summary["steadyMaxTextureUploads"] is not None
                and summary["steadyMaxTextureUploads"] <= args.max_steady_texture_uploads
            )
            if lighting_proof else
            (
                ok
                and summary["fatalCount"] == 0
                and summary["activeMapMaxBots"] >= case["bots"]
                and summary["activeMapLastTick"] >= required_tick
                and summary["characterCount"] >= case["bots"]
                and camera_coverage_complete
                and state_coverage_complete
                and not state_capture_failures
                and summary["steadyAverageFps"] is not None
                and summary["steadyAverageFps"] >= args.min_steady_fps
                and summary["steadyMaxTextureUploads"] is not None
                and summary["steadyMaxTextureUploads"] <= args.max_steady_texture_uploads
                and summary["skeletalFlickerAlertCount"] == 0
                and not unexpected_classes
                and not missing_classes
            )
        ),
    })
    with open(os.path.join(run_dir, "summary.json"), "w") as handle:
        json.dump(summary, handle, indent=2, sort_keys=True)
    return summary, config_path


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default=DEFAULT_BUILD)
    parser.add_argument("--runtime-source", default=DEFAULT_RUNTIME)
    parser.add_argument(
        "--stage-dir",
        default=os.path.join(XBOX_DIR, "build_cli", "xemu_deep_soak_stage"),
    )
    parser.add_argument("--xemu-root", default=DEFAULT_XEMU_ROOT)
    parser.add_argument("--xemu-instance", default=DEFAULT_XEMU_INSTANCE)
    parser.add_argument("--hdd", default=DEFAULT_HDD)
    parser.add_argument("--eeprom-source", default=DEFAULT_EEPROM)
    parser.add_argument("--monitor-port", type=int, default=4477)
    parser.add_argument("--poll-interval", type=float, default=8.0)
    parser.add_argument("--poll-timeout", type=float, default=8.0)
    parser.add_argument("--stall-seconds", type=float, default=150.0)
    parser.add_argument("--boot-timeout", type=float, default=240.0)
    parser.add_argument("--mute-audio", action="store_true", help="Launch Xemu with QEMU audio disabled.")
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
    parser.add_argument(
        "--map-override",
        help="Run the selected matrix case on this staged map while retaining its game type",
    )
    parser.add_argument("--seconds-override", type=int, help="Override each selected live-soak duration")
    parser.add_argument("--bots-override", type=int, help="Override bot count for each selected case")
    parser.add_argument(
        "--mutator",
        help="Append one mutator class to each rendered case URL",
    )
    parser.add_argument(
        "--player-class",
        help="Override the local observer class (useful when isolating a skeletal bot under test)",
    )
    parser.add_argument(
        "--bot-class",
        help="Override every staged bot slot with one exact bot class",
    )
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
        "--isolate-roster-count",
        type=int,
        help=(
            "Repeat one selected map as one-bot cases for this many consecutive "
            "roster entries, starting at --roster-offset"
        ),
    )
    parser.add_argument(
        "--capture-loading-animation",
        action="store_true",
        help="Capture up to four loading-wheel frames when XLOADANIM advances",
    )
    parser.add_argument(
        "--frontend-loading-proof",
        choices=("issue", "soak", "instant-dm"),
        help="Boot the frontend, use a menu smoke marker to launch a map, and pass/fail on loading-wheel captures.",
    )
    parser.add_argument(
        "--loading-proof-screenshots",
        type=int,
        default=4,
        help="Number of loading animation screenshots required in --frontend-loading-proof mode.",
    )
    parser.add_argument(
        "--camera-burst-limit",
        type=int,
        default=1,
        help="Require in-game camera evidence for this many distinct bot classes per case",
    )
    parser.add_argument(
        "--camera-frames",
        type=int,
        default=3,
        help="Capture this many consecutive in-game frames for each distinct camera target",
    )
    parser.add_argument(
        "--skip-general-screenshots",
        action="store_true",
        help="Skip redundant live-start, midpoint, and final overview captures",
    )
    parser.add_argument(
        "--lighting-proof",
        action="store_true",
        help="Use deterministic first-person map-lighting viewpoints instead of character camera proof",
    )
    parser.add_argument(
        "--lighting-proof-screenshots",
        type=int,
        default=8,
        help="Number of distinct lighting-proof viewpoints required per map",
    )
    parser.add_argument(
        "--skeletal-state-proof",
        action="store_true",
        help="Require deterministic rendered coverage for all ten skeletal animation states",
    )
    parser.add_argument(
        "--skaarj-skin-proof",
        action="store_true",
        help="Apply repeated harmless hits to the viewed Skaarj for blood/material proof",
    )
    parser.add_argument(
        "--state-frames",
        type=int,
        default=2,
        help="Capture this many consecutive in-game frames for each deterministic state",
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
    args.stage_dir = os.path.abspath(args.stage_dir)
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    args.evidence_dir = os.path.abspath(
        args.evidence_dir or os.path.join(XBOX_DIR, "build_cli", "xemu_deep_soak_" + stamp)
    )
    args.screenshot_dir = os.path.join(args.evidence_dir, "xemu_internal_screenshots")
    args.display_backend = ""
    if args.frontend_loading_proof:
        args.capture_loading_animation = True
        if args.loading_proof_screenshots <= 0 or args.loading_proof_screenshots > 12:
            raise RuntimeError("--loading-proof-screenshots must be between 1 and 12")
    if args.lighting_proof_screenshots <= 0 or args.lighting_proof_screenshots > 16:
        raise RuntimeError("--lighting-proof-screenshots must be between 1 and 16")
    if args.camera_burst_limit <= 0 or args.camera_burst_limit > len(ROSTER):
        raise RuntimeError("--camera-burst-limit must be between 1 and %d" % len(ROSTER))
    if args.camera_frames <= 0 or args.camera_frames > 12:
        raise RuntimeError("--camera-frames must be between 1 and 12")
    if args.state_frames <= 0 or args.state_frames > 6:
        raise RuntimeError("--state-frames must be between 1 and 6")

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
    if args.map_override:
        for case in selected:
            case["map"] = args.map_override
    if args.bots_override is not None:
        min_bots = 0 if args.lighting_proof else 1
        if args.bots_override < min_bots or args.bots_override > 15:
            if args.lighting_proof:
                raise RuntimeError("--bots-override must be between 0 and 15 in --lighting-proof mode")
            raise RuntimeError("--bots-override must be between 1 and 15")
        for case in selected:
            case["bots"] = args.bots_override
    if args.player_class:
        for case in selected:
            case["player"] = args.player_class
    if args.bot_class:
        for case in selected:
            case["bot_class"] = args.bot_class
    if args.mutator:
        for case in selected:
            case["mutator"] = args.mutator
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
    if args.isolate_roster_count is not None:
        if len(selected) != 1:
            raise RuntimeError("--isolate-roster-count requires exactly one selected map")
        start = args.roster_offset if args.roster_offset is not None else 0
        if args.isolate_roster_count <= 0 or start + args.isolate_roster_count > len(ROSTER):
            raise RuntimeError(
                "--isolate-roster-count must cover roster indices %d through %d"
                % (start, len(ROSTER) - 1)
            )
        base_case = selected[0]
        selected = []
        for roster_index in range(start, start + args.isolate_roster_count):
            case = dict(base_case)
            case["bots"] = 1
            case["roster_offset"] = roster_index
            case["roster_span"] = 1
            case["reason"] = "isolated rendered fidelity: %s" % ROSTER[roster_index][0]
            selected.append(case)

    stage, copied = prepare_base(args)
    xiso_tool = xemu_soak.find_xiso_tool(os.path.dirname(args.build_dir))
    config_path = os.path.join(args.xemu_instance, "xemu_soak_runtime_config")
    summaries = []
    try:
        for index, case in enumerate(selected, 1):
            print("[%d/%d] %s: %s" % (index, len(selected), case["map"], case["reason"]))
            summary, config_path = run_case(args, stage, case, index, xiso_tool, config_path)
            summaries.append(summary)
            if args.lighting_proof:
                print(
                    "  ok=%s lightSlots=%d/%d minAvailKB=%s lastTick=%s fpsMin=%s steadyFps=%s steadyTexUp=%s marker=%s"
                    % (
                        summary["ok"],
                        len(summary["capturedLightingSlots"]),
                        summary["lightingProofRequiredScreenshots"],
                        summary["minAvailKB"],
                        summary["activeMapLastTick"],
                        summary["minFps"],
                        summary["steadyAverageFps"],
                        summary["steadyMaxTextureUploads"],
                        summary["marker"],
                    )
                )
            else:
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
