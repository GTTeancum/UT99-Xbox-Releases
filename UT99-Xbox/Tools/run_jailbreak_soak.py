#!/usr/bin/env python
"""Run bot-loaded Jailbreak match-completion soaks in CXBX-R."""

from __future__ import print_function

import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import time


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
XBOX_DIR = os.path.dirname(SCRIPT_DIR)
ROOT_DIR = os.path.dirname(XBOX_DIR)

DEFAULT_BUILD = os.path.join(XBOX_DIR, "build_cli", "release")
DEFAULT_TARGET = r"C:\Games\Emulators\CXBX\UT99x"
DEFAULT_CXBX = r"C:\Games\Emulators\CXBX\cxbx.exe"
CXBX_PROCESS_NAMES = ("cxbx.exe", "cxbxr-ldr.exe", "cxbxr-debugger.exe")

FATAL_RE = re.compile(
    r"Critical:|appError|Out of memory|FMallocXbox: Out of memory|Assertion failed|General protection fault",
    re.IGNORECASE,
)
MATCH_RE = re.compile(r"SMOKE match-ended .*")
SMOKE_RE = re.compile(r"SMOKE tick=.*")
BOT_RE = re.compile(r"\b(?:bots|priBots)=(\d+)")
AVAIL_RE = re.compile(r"\bavailKB=(\d+)")
TICK_RE = re.compile(r"SMOKE tick=(\d+)")


def fail(message):
    print("ERROR: " + message)
    sys.exit(1)


def ensure_dir(path):
    if not os.path.isdir(path):
        os.makedirs(path)


def stop_cxbx():
    for name in CXBX_PROCESS_NAMES:
        subprocess.call(
            ["taskkill", "/F", "/IM", name],
            stdout=open(os.devnull, "w"),
            stderr=open(os.devnull, "w"),
        )
    deadline = time.time() + 10.0
    while time.time() < deadline:
        live = False
        for name in CXBX_PROCESS_NAMES:
            output = subprocess.check_output(
                ["tasklist", "/FI", "IMAGENAME eq " + name],
                stderr=subprocess.STDOUT,
                text=True,
            )
            if name.lower() in output.lower():
                live = True
                break
        if not live:
            return
        time.sleep(0.25)


def copy_tree_overlay(src, dst):
    if not os.path.isdir(src):
        return 0
    copied = 0
    for current, dirs, files in os.walk(src):
        dirs.sort()
        files.sort()
        rel = os.path.relpath(current, src)
        out_dir = dst if rel == "." else os.path.join(dst, rel)
        ensure_dir(out_dir)
        for name in files:
            shutil.copy2(os.path.join(current, name), os.path.join(out_dir, name))
            copied += 1
    return copied


def deploy_build(build_dir, target_dir):
    xbe = os.path.join(build_dir, "default.xbe")
    if not os.path.isfile(xbe):
        fail("Built XBE not found: " + xbe)
    ensure_dir(target_dir)
    shutil.copy2(xbe, os.path.join(target_dir, "default.xbe"))
    copied = 1
    for folder in ("System", "Maps", "Textures", "Sounds", "MusicXbox", "MenuAssets"):
        copied += copy_tree_overlay(os.path.join(build_dir, folder), os.path.join(target_dir, folder))
    return copied


def largest_jailbreak_maps(target_dir, count):
    maps_dir = os.path.join(target_dir, "Maps")
    if not os.path.isdir(maps_dir):
        fail("Maps directory not found: " + maps_dir)

    maps = []
    for name in os.listdir(maps_dir):
        lower = name.lower()
        if not lower.startswith("jb") or not lower.endswith(".unr"):
            continue
        if lower in ("jbtutorialmap.unr", "jbcredits.unr"):
            continue
        path = os.path.join(maps_dir, name)
        if os.path.isfile(path):
            maps.append((os.path.getsize(path), os.path.splitext(name)[0]))
    maps.sort(reverse=True)
    if not maps:
        fail("No Jailbreak maps found under " + maps_dir)
    return maps[:count]


def read_lines(path):
    if not os.path.isfile(path):
        return []
    with open(path, "r") as f:
        return f.read().splitlines()


def write_lines(path, lines):
    ensure_dir(os.path.dirname(path))
    with open(path, "w") as f:
        f.write("\n".join(lines))
        f.write("\n")


def find_section(lines, section):
    wanted = "[" + section.lower() + "]"
    start = None
    for i, line in enumerate(lines):
        if line.strip().lower() == wanted:
            start = i
            break
    if start is None:
        if lines and lines[-1].strip():
            lines.append("")
        lines.append("[" + section + "]")
        return len(lines) - 1, len(lines)
    end = len(lines)
    for i in range(start + 1, len(lines)):
        stripped = lines[i].strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            end = i
            break
    return start, end


def set_key(lines, section, key, value):
    start, end = find_section(lines, section)
    lower_key = key.lower()
    for i in range(start + 1, end):
        stripped = lines[i].strip()
        if not stripped or stripped.startswith(";") or "=" not in stripped:
            continue
        lhs = stripped.split("=", 1)[0].strip().lower()
        if lhs == lower_key:
            lines[i] = key + "=" + str(value)
            return
    lines.insert(end, key + "=" + str(value))


def patch_runtime_ini(path, min_players, initial_bots, goal_team_score, time_limit):
    lines = read_lines(path)
    for section in ("Botpack.DeathMatchPlus", "JailBreak.JailBreak"):
        set_key(lines, section, "MinPlayers", min_players)
        set_key(lines, section, "InitialBots", initial_bots)
        set_key(lines, section, "bTournament", "False")
    set_key(lines, "JailBreak.JailBreak", "GoalTeamScore", "%.6f" % float(goal_team_score))
    set_key(lines, "JailBreak.JailBreak", "TimeLimit", int(time_limit))
    set_key(lines, "JailBreak.JailBreak", "MaxTeams", 2)
    set_key(lines, "JailBreak.JailBreak", "MaxTeamSize", 16)
    set_key(lines, "JailBreak.JailBreak", "bForceRespawn", "True")
    write_lines(path, lines)


def build_start_url(map_name, min_players, initial_bots, goal_team_score, time_limit, difficulty):
    return (
        "{map}.unr?game=JailBreak.JailBreak?Name=SoakP1?Class=Botpack.TMale2"
        "?team=0?skin=SoldierSkins.blkt?Face=SoldierSkins.Othello"
        "?Voice=BotPack.VoiceMaleTwo?MinPlayers={min_players}?InitialBots={initial_bots}"
        "?GoalTeamScore={goal}?TimeLimit={time_limit}?MaxPlayers=16?Difficulty={difficulty}"
    ).format(
        map=map_name,
        min_players=min_players,
        initial_bots=initial_bots,
        goal=goal_team_score,
        time_limit=time_limit,
        difficulty=difficulty,
    )


def write_start_url(target_dir, map_name, min_players, initial_bots, goal_team_score, time_limit, difficulty):
    url = build_start_url(
        map_name,
        min_players,
        initial_bots,
        goal_team_score,
        time_limit,
        difficulty,
    )
    path = os.path.join(target_dir, "XboxStartURL.ini")
    write_lines(path, ["; Jailbreak bot-loaded match-completion soak", "StartURL=" + url])
    return url


def remove_old_logs(target_dir):
    for name in ("ut99.log", "KrnlDebug.txt", "CxbxDebug.txt"):
        path = os.path.join(target_dir, name)
        if os.path.isfile(path):
            remove_with_retry(path)
    for name in (
        "XboxAutoFireSmoke.ini",
        "XboxAudioToneSmoke.ini",
        "XboxMenuSmoke.ini",
        "XboxMenuSmokeJailbreak.ini",
        "XboxSoakSmoke.ini",
        # Camera/state proofs make actors invulnerable or override their input;
        # never carry those markers into a match-completion/travel soak.
        "XboxCharacterSoak.ini",
        "XboxGameplayRecording.ini",
        "XboxLightingProof.ini",
        "XboxFlickerTraversal.ini",
        "XboxSkeletalStateProof.ini",
        "XboxAllAnimationProof.ini",
        "XboxSkaarjSkinProof.ini",
        "XboxSplitSmoke.ini",
        "XboxSplitControlsProofSmoke.ini",
        "XboxSplitBenchmark.ini",
        "XboxSplitCombatBenchmark.ini",
        "XboxSplitLayout03.ini",
        "XboxSplitLayout07.ini",
        "XboxSystemLinkSmoke.ini",
        "XboxSystemLink4PStress.ini",
        "XboxTournamentSmoke.ini",
    ):
        path = os.path.join(target_dir, name)
        if os.path.isfile(path):
            remove_with_retry(path)


def remove_with_retry(path):
    deadline = time.time() + 12.0
    last_error = None
    while time.time() < deadline:
        try:
            os.remove(path)
            return
        except OSError as exc:
            last_error = exc
            time.sleep(0.25)
    raise last_error


def copy_if_exists(src, dst_dir, name=None):
    if os.path.isfile(src):
        shutil.copy2(src, os.path.join(dst_dir, name or os.path.basename(src)))


def wait_for_match(log_path, timeout_seconds, stall_seconds):
    deadline = time.time() + timeout_seconds
    last_size = -1
    last_change = time.time()
    last_match = None
    while time.time() < deadline:
        if os.path.isfile(log_path):
            size = os.path.getsize(log_path)
            if size != last_size:
                last_size = size
                last_change = time.time()
            with open(log_path, "r", errors="replace") as f:
                text = f.read()
            matches = MATCH_RE.findall(text)
            if matches:
                last_match = matches[-1]
                return True, last_match, text, False
            if last_size > 0 and time.time() - last_change >= stall_seconds:
                return False, "stalled; no log change for %.1fs" % (time.time() - last_change), text, True
        time.sleep(2)
    if os.path.isfile(log_path):
        with open(log_path, "r", errors="replace") as f:
            text = f.read()
    else:
        text = ""
    stalled = time.time() - last_change
    return False, "timeout; no log change for %.1fs" % stalled, text, stalled >= stall_seconds


def summarize_log(text):
    fatal_lines = [line for line in text.splitlines() if FATAL_RE.search(line)]
    smoke_lines = SMOKE_RE.findall(text)
    match_lines = []
    seen_match_lines = set()
    for line in MATCH_RE.findall(text):
        if line in seen_match_lines:
            continue
        seen_match_lines.add(line)
        match_lines.append(line)
    bots = [int(m.group(1)) for m in BOT_RE.finditer(text)]
    avail = []
    for line in smoke_lines:
        if "heapLiveKB=" not in line or "largestTag=" not in line or "comment=" not in line:
            continue
        match = AVAIL_RE.search(line)
        if match:
            avail.append(int(match.group(1)))
    ticks = [int(m.group(1)) for m in TICK_RE.finditer(text)]
    return {
        "fatalCount": len(fatal_lines),
        "fatalLines": fatal_lines[:20],
        "smokeSamples": len(smoke_lines),
        "lastSmoke": smoke_lines[-1] if smoke_lines else "",
        "matchEnded": bool(match_lines),
        "matchEndedCount": len(match_lines),
        "lastMatchEnded": match_lines[-1] if match_lines else "",
        "mapListComplete": "SMOKE map-list complete" in text,
        "maxBots": max(bots) if bots else 0,
        "minAvailKB": min(avail) if avail else None,
        "lastTick": max(ticks) if ticks else 0,
    }


def run_one(args, evidence_dir, map_name, run_index):
    run_name = "%02d_%s" % (run_index, map_name)
    run_dir = os.path.join(evidence_dir, run_name)
    ensure_dir(run_dir)

    stop_cxbx()
    remove_old_logs(args.target)
    url = write_start_url(
        args.target,
        map_name,
        args.min_players,
        args.initial_bots,
        args.goal_team_score,
        args.time_limit,
        args.difficulty,
    )

    start_path = os.path.join(args.target, "XboxStartURL.ini")
    copy_if_exists(start_path, run_dir)
    copy_if_exists(os.path.join(args.target, "System", "UnrealTournament.ini"), run_dir)

    xbe = os.path.join(args.target, "default.xbe")
    proc = subprocess.Popen([args.cxbx, xbe], cwd=os.path.dirname(args.cxbx))
    started = datetime.datetime.now().isoformat()
    ok, marker, text, stalled = wait_for_match(os.path.join(args.target, "ut99.log"), args.timeout_seconds, args.stall_seconds)
    if ok and args.post_end_seconds > 0:
        time.sleep(args.post_end_seconds)
    stop_cxbx()
    ended = datetime.datetime.now().isoformat()

    copy_if_exists(os.path.join(args.target, "ut99.log"), run_dir, "ut99.log")
    copy_if_exists(os.path.join(args.target, "KrnlDebug.txt"), run_dir, "KrnlDebug.txt")
    copy_if_exists(os.path.join(args.target, "CxbxDebug.txt"), run_dir, "CxbxDebug.txt")

    if not text and os.path.isfile(os.path.join(args.target, "ut99.log")):
        with open(os.path.join(args.target, "ut99.log"), "r", errors="replace") as f:
            text = f.read()
    summary = summarize_log(text)
    summary.update({
        "map": map_name,
        "run": run_index,
        "ok": bool(ok and summary["fatalCount"] == 0 and summary["maxBots"] >= args.initial_bots),
        "stalled": stalled,
        "marker": marker,
        "url": url,
        "started": started,
        "ended": ended,
        "pid": proc.pid,
    })
    with open(os.path.join(run_dir, "summary.json"), "w") as f:
        json.dump(summary, f, indent=2, sort_keys=True)
    return summary


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default=DEFAULT_BUILD)
    parser.add_argument("--target", default=DEFAULT_TARGET)
    parser.add_argument("--cxbx", default=DEFAULT_CXBX)
    parser.add_argument("--maps", nargs="+")
    parser.add_argument("--largest-maps", type=int, default=4)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--min-players", type=int, default=8)
    parser.add_argument("--initial-bots", type=int, default=7)
    parser.add_argument("--goal-team-score", type=float, default=5.0)
    parser.add_argument("--time-limit", type=int, default=15)
    parser.add_argument("--difficulty", type=int, default=2)
    parser.add_argument("--timeout-seconds", type=int, default=1500)
    parser.add_argument("--stall-seconds", type=int, default=120)
    parser.add_argument("--post-end-seconds", type=int, default=15)
    parser.add_argument("--continue-on-failure", action="store_true")
    parser.add_argument("--skip-deploy", action="store_true")
    parser.add_argument("--evidence-dir")
    args = parser.parse_args(argv)

    if not os.path.isfile(args.cxbx):
        fail("CXBX executable not found: " + args.cxbx)
    if not args.skip_deploy:
        copied = deploy_build(args.build_dir, args.target)
        print("Deployed %d files to %s" % (copied, args.target))

    for ini_name in ("Default.ini", "UnrealTournament.ini"):
        patch_runtime_ini(
            os.path.join(args.target, "System", ini_name),
            args.min_players,
            args.initial_bots,
            args.goal_team_score,
            args.time_limit,
        )

    if args.maps:
        selected_maps = [(os.path.getsize(os.path.join(args.target, "Maps", item + ".unr")), item) for item in args.maps]
    else:
        selected_maps = largest_jailbreak_maps(args.target, args.largest_maps)
        args.maps = [item[1] for item in selected_maps]
    print("Selected Jailbreak maps by file size:")
    for size, map_name in selected_maps:
        print("  %s (%d bytes)" % (map_name, size))

    if args.evidence_dir:
        evidence_dir = os.path.abspath(args.evidence_dir)
    else:
        stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        evidence_dir = os.path.join(XBOX_DIR, "build_cli", "jailbreak_soak_" + stamp)
    ensure_dir(evidence_dir)

    summaries = []
    run_index = 1
    try:
        for repeat in range(args.repeats):
            for map_name in args.maps:
                print("Running %s repeat %d/%d" % (map_name, repeat + 1, args.repeats))
                summary = run_one(args, evidence_dir, map_name, run_index)
                summaries.append(summary)
                print("  ok={ok} maxBots={maxBots} minAvailKB={minAvailKB} lastTick={lastTick}".format(**summary))
                if not summary["ok"] and not args.continue_on_failure:
                    break
                run_index += 1
            if summaries and not summaries[-1]["ok"] and not args.continue_on_failure:
                break
    finally:
        stop_cxbx()

    aggregate = {
        "evidenceDir": evidence_dir,
        "maps": args.maps,
        "repeats": args.repeats,
        "settings": {
            "minPlayers": args.min_players,
            "initialBots": args.initial_bots,
            "goalTeamScore": args.goal_team_score,
            "timeLimit": args.time_limit,
            "difficulty": args.difficulty,
        },
        "runs": summaries,
        "allOk": bool(summaries) and all(item["ok"] for item in summaries),
    }
    with open(os.path.join(evidence_dir, "summary.json"), "w") as f:
        json.dump(aggregate, f, indent=2, sort_keys=True)
    print("Evidence: " + evidence_dir)
    print("ALL_OK=%s" % ("1" if aggregate["allOk"] else "0"))
    return 0 if aggregate["allOk"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
