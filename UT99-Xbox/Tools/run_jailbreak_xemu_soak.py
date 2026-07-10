#!/usr/bin/env python
"""Run bot-loaded Jailbreak match-completion soaks in Xemu."""

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

import run_jailbreak_soak as jb_soak
import poll_xemu_ram_log as xemu_poll


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
XBOX_DIR = os.path.dirname(SCRIPT_DIR)
ROOT_DIR = os.path.dirname(XBOX_DIR)

DEFAULT_BUILD = os.path.join(XBOX_DIR, "build_cli", "release")
DEFAULT_RUNTIME_SOURCE = r"C:\Games\Emulators\CXBX\UT99x"
DEFAULT_XEMU_ROOT = r"C:\Games\Emulators\Xemu"
DEFAULT_XEMU_INSTANCE = r"C:\Games\Emulators\Xemu\UT99Soak"
DEFAULT_HDD = r"C:\Games\Emulators\Xemu\UT99Test\HDD\ut99_hdd.qcow2"
DEFAULT_EEPROM_SOURCE = r"C:\Games\Emulators\Xemu\EEPROM\eeprom.bin"
DEFAULT_MONITOR_PORT = 4477
POLL_SCRIPT = os.path.join(SCRIPT_DIR, "poll_xemu_ram_log.py")

XEMU_PROCESS = "xemu.exe"
POLL_SYMBOL_CACHE = {}


def fail(message):
    print("ERROR: " + message)
    sys.exit(1)


def ensure_file(path, label):
    if not os.path.isfile(path):
        fail("%s not found: %s" % (label, path))


def ensure_dir(path):
    if not os.path.isdir(path):
        os.makedirs(path)


def safe_remove_tree(path, allowed_parent):
    full = os.path.abspath(path)
    parent = os.path.abspath(allowed_parent)
    if not full.lower().startswith(parent.lower() + os.sep):
        fail("Refusing to remove path outside %s: %s" % (parent, full))
    if os.path.isdir(full):
        shutil.rmtree(full)


def xemu_exe_from_root(root):
    candidates = (
        os.path.join(root, "xemu.exe"),
        os.path.join(root, "UT99Codex", "xemu.exe"),
    )
    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate
    return candidates[0]


def ensure_instance_xemu(source_exe, instance_dir):
    source_exe = os.path.abspath(source_exe)
    instance_dir = os.path.abspath(instance_dir)
    ensure_file(source_exe, "xemu.exe")
    ensure_dir(instance_dir)
    dest = os.path.join(instance_dir, "xemu.exe")
    if os.path.abspath(source_exe).lower() == os.path.abspath(dest).lower():
        return dest
    if os.path.isfile(dest):
        return dest
    try:
        os.link(source_exe, dest)
    except Exception:
        shutil.copy2(source_exe, dest)
    return dest


def find_xiso_tool(build_root):
    candidates = (
        os.path.join(build_root, "tools", "extract-xiso", "artifacts", "extract-xiso.exe"),
        r"C:\Programming\GitHub\Guitar Hero II\tools\artifacts\extract-xiso.exe",
        os.path.abspath(os.path.join(ROOT_DIR, "..", "Guitar Hero II", "tools", "artifacts", "extract-xiso.exe")),
    )
    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate
    fail("extract-xiso.exe not found. Checked: " + ", ".join(candidates))


def stop_xemu_for_config(config_path):
    full_config = os.path.abspath(config_path)
    script = r"""
$config = [System.IO.Path]::GetFullPath($args[0])
Get-CimInstance Win32_Process -Filter "Name = 'xemu.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -and $_.CommandLine.IndexOf($config, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
"""
    try:
        with open(os.devnull, "w") as devnull:
            subprocess.run(
                ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script, full_config],
                stdout=devnull,
                stderr=devnull,
                timeout=10,
            )
    except subprocess.TimeoutExpired:
        print("WARNING: timed out while clearing old Xemu instance for config: " + full_config)


def terminate_process(proc):
    if not proc:
        return
    if proc.poll() is not None:
        return
    try:
        proc.terminate()
        proc.wait(timeout=8)
    except Exception:
        try:
            proc.kill()
        except Exception:
            pass


def copy_runtime_tree(source_dir, stage_dir):
    if not os.path.isdir(source_dir):
        fail("Runtime source not found: " + source_dir)

    def ignore_names(_dir, names):
        ignored = set()
        for name in names:
            lower = name.lower()
            if lower in ("logs", "screenshots"):
                ignored.add(name)
            elif lower in ("ut99.log", "krnldebug.txt", "cxbxdebug.txt"):
                ignored.add(name)
        return ignored

    shutil.copytree(source_dir, stage_dir, ignore=ignore_names)


def copy_build_overlay(build_dir, stage_dir):
    ensure_file(os.path.join(build_dir, "default.xbe"), "Built XBE")
    shutil.copy2(os.path.join(build_dir, "default.xbe"), os.path.join(stage_dir, "default.xbe"))
    copied = 1
    for folder in ("System", "Maps", "Textures", "Sounds", "MusicXbox", "MenuAssets"):
        copied += jb_soak.copy_tree_overlay(os.path.join(build_dir, folder), os.path.join(stage_dir, folder))
    return copied


def prepare_stage(args, map_name, run_dir):
    build_root = os.path.abspath(os.path.join(args.build_dir, ".."))
    stage_dir = os.path.abspath(args.stage_dir)
    safe_remove_tree(stage_dir, build_root)
    ensure_dir(os.path.dirname(stage_dir))
    copy_runtime_tree(args.runtime_source, stage_dir)
    copied = copy_build_overlay(args.build_dir, stage_dir)
    jb_soak.remove_old_logs(stage_dir)

    for ini_name in ("Default.ini", "UnrealTournament.ini"):
        jb_soak.patch_runtime_ini(
            os.path.join(stage_dir, "System", ini_name),
            args.min_players,
            args.initial_bots,
            args.goal_team_score,
            args.time_limit,
        )

    url = jb_soak.write_start_url(
        stage_dir,
        map_name,
        args.min_players,
        args.initial_bots,
        args.goal_team_score,
        args.time_limit,
        args.difficulty,
    )
    jb_soak.copy_if_exists(os.path.join(stage_dir, "XboxStartURL.ini"), run_dir)
    jb_soak.copy_if_exists(os.path.join(stage_dir, "System", "Default.ini"), run_dir)
    jb_soak.copy_if_exists(os.path.join(stage_dir, "System", "UnrealTournament.ini"), run_dir)
    return stage_dir, url, copied


def prepare_continuous_stage(args, maps, run_dir):
    build_root = os.path.abspath(os.path.join(args.build_dir, ".."))
    stage_dir = os.path.abspath(args.stage_dir)
    safe_remove_tree(stage_dir, build_root)
    ensure_dir(os.path.dirname(stage_dir))
    copy_runtime_tree(args.runtime_source, stage_dir)
    copied = copy_build_overlay(args.build_dir, stage_dir)
    jb_soak.remove_old_logs(stage_dir)

    for ini_name in ("Default.ini", "UnrealTournament.ini"):
        jb_soak.patch_runtime_ini(
            os.path.join(stage_dir, "System", ini_name),
            args.min_players,
            args.initial_bots,
            args.goal_team_score,
            args.time_limit,
        )

    urls = [
        jb_soak.build_start_url(
            map_name,
            args.min_players,
            args.initial_bots,
            args.goal_team_score,
            args.time_limit,
            args.difficulty,
        )
        for map_name in maps
    ]
    jb_soak.write_lines(
        os.path.join(stage_dir, "XboxStartURL.ini"),
        ["; Jailbreak continuous Xemu soak", "StartURL=" + urls[0]],
    )
    jb_soak.write_lines(
        os.path.join(stage_dir, "XboxSoakMapList.ini"),
        ["; Jailbreak continuous Xemu soak map list"] + ["URL=" + url for url in urls],
    )
    jb_soak.copy_if_exists(os.path.join(stage_dir, "XboxStartURL.ini"), run_dir)
    jb_soak.copy_if_exists(os.path.join(stage_dir, "XboxSoakMapList.ini"), run_dir)
    jb_soak.copy_if_exists(os.path.join(stage_dir, "System", "Default.ini"), run_dir)
    jb_soak.copy_if_exists(os.path.join(stage_dir, "System", "UnrealTournament.ini"), run_dir)
    return stage_dir, urls, copied


def build_xiso(stage_dir, iso_path, xiso_tool):
    ensure_dir(os.path.dirname(iso_path))
    new_iso = iso_path + ".new"
    if os.path.isfile(new_iso):
        os.remove(new_iso)
    if os.path.isfile(iso_path):
        os.remove(iso_path)
    result = subprocess.run([xiso_tool, "-Q", "-m", "-c", stage_dir, new_iso])
    if result.returncode != 0:
        fail("extract-xiso failed with exit code %d" % result.returncode)
    os.replace(new_iso, iso_path)


def write_xemu_config(args, iso_path):
    instance_dir = os.path.abspath(args.xemu_instance)
    ensure_dir(instance_dir)
    config_path = os.path.join(instance_dir, "xemu_soak_runtime_config")
    script_path = os.path.join(SCRIPT_DIR, "update_xemu_soak_config.ps1")
    ensure_file(script_path, "Xemu config writer")
    command = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        script_path,
        "-ConfigPath",
        os.path.join(instance_dir, "xemu.toml"),
        "-RuntimeConfigPath",
        config_path,
        "-InstanceDir",
        instance_dir,
        "-ScreenshotDir",
        os.path.abspath(args.screenshot_dir),
        "-BootRom",
        args.bootrom,
        "-FlashRom",
        args.flashrom,
        "-EepromSource",
        args.eeprom_source,
        "-HddPath",
        args.hdd,
        "-DvdPath",
        iso_path,
    ]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        fail("Xemu config update failed: " + (result.stderr or result.stdout))
    return config_path


def close_process_streams(proc):
    if not proc:
        return
    for name in ("_xemu_stdout_handle", "_xemu_stderr_handle"):
        handle = getattr(proc, name, None)
        if handle:
            try:
                handle.close()
            except Exception:
                pass
            setattr(proc, name, None)


def launch_xemu(args, config_path, run_dir):
    stop_xemu_for_config(config_path)
    command = build_xemu_command(args, config_path)
    stdout_handle = open(os.path.join(run_dir, "xemu_stdout.txt"), "wb")
    stderr_handle = open(os.path.join(run_dir, "xemu_stderr.txt"), "wb")
    startupinfo = None
    creationflags = 0
    if os.name == "nt":
        startupinfo = subprocess.STARTUPINFO()
        startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startupinfo.wShowWindow = 0
        creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    try:
        proc = subprocess.Popen(
            command,
            cwd=args.xemu_instance,
            stdout=stdout_handle,
            stderr=stderr_handle,
            startupinfo=startupinfo,
            creationflags=creationflags,
        )
    except Exception:
        stdout_handle.close()
        stderr_handle.close()
        raise
    proc._xemu_stdout_handle = stdout_handle
    proc._xemu_stderr_handle = stderr_handle
    return proc


def build_xemu_command(args, config_path):
    monitor = "tcp:127.0.0.1:%d,server,nowait" % args.monitor_port
    command = [args.xemu, "-config_path", config_path]
    if args.display_backend:
        command.extend(["-display", args.display_backend])
    command.extend(["-monitor", monitor])
    return command


def read_snapshot_log(snapshot_path):
    if not os.path.isfile(snapshot_path):
        return ""
    with open(snapshot_path, "r", errors="replace") as handle:
        text = handle.read()
    parts = text.split("\n\n", 1)
    return parts[1] if len(parts) == 2 else text


def append_snapshot(accumulated, snapshot, stamp):
    if not snapshot:
        return accumulated
    if snapshot in accumulated[-(len(snapshot) + 4096):]:
        return accumulated

    max_overlap = min(len(accumulated), len(snapshot), 65536)
    for size in range(max_overlap, 0, -1):
        if accumulated.endswith(snapshot[:size]):
            return accumulated + snapshot[size:]

    gap = "\n--- RAM log snapshot gap %s ---\n" % stamp
    return accumulated + gap + snapshot


def poll_ram_log(args, run_dir, history_handle):
    out_dir = os.path.join(run_dir, "ram_snapshots")
    ensure_dir(out_dir)
    stamp = datetime.datetime.now().isoformat()
    map_path = os.path.join(args.build_dir, "UnrealTournament.map")
    xbe_path = os.path.join(args.build_dir, "default.xbe")
    cache_key = (map_path, xbe_path)
    if cache_key not in POLL_SYMBOL_CACHE:
        POLL_SYMBOL_CACHE[cache_key] = xemu_poll.resolve_symbols(map_path, xbe_path)
    poll_args = argparse.Namespace(timeout=args.poll_timeout, phys_delta=args.phys_delta)
    try:
        phys_delta, values, text = xemu_poll.poll_port(args.monitor_port, POLL_SYMBOL_CACHE[cache_key], poll_args)
        snapshot_path = os.path.join(out_dir, "port%d_ut99_ram_log.txt" % args.monitor_port)
        with open(snapshot_path, "w", errors="replace") as handle:
            handle.write("port=%d\n" % args.monitor_port)
            handle.write("map=%s\n" % os.path.abspath(map_path))
            handle.write("xbe=%s\n" % os.path.abspath(xbe_path))
            handle.write("phys_delta=%s\n" % ("virtual" if phys_delta is None else "0x%08X" % phys_delta))
            for key in ("boot_phase", "writes", "heartbeats", "last_tick", "offset", "wrapped", "magic0", "magic1"):
                value = values.get(key)
                if value is None:
                    handle.write("%s=None\n" % key)
                elif key.startswith("magic") or key == "boot_phase":
                    handle.write("%s=0x%08X\n" % (key, value))
                else:
                    handle.write("%s=%u\n" % (key, value))
            handle.write("\n")
            handle.write(text)
            if text and not text.endswith("\n"):
                handle.write("\n")
        history_handle.write("[%s] rc=0\n" % stamp)
        history_handle.write("port %d: phase=0x%08X writes=%s hb=%s mirror=%s\n" % (
            args.monitor_port,
            values["boot_phase"] or 0,
            values["writes"],
            values["heartbeats"],
            snapshot_path,
        ))
        history_handle.flush()
        return text, 0
    except Exception as exc:
        history_handle.write("[%s] rc=1\n" % stamp)
        history_handle.write("port %d: ERROR: %s\n" % (args.monitor_port, exc))
    history_handle.flush()
    snapshot_path = os.path.join(out_dir, "port%d_ut99_ram_log.txt" % args.monitor_port)
    return read_snapshot_log(snapshot_path), 1


def write_partial_summary(run_dir, text, marker, stalled):
    summary = jb_soak.summarize_log(text)
    summary.update({
        "ok": False,
        "stalled": stalled,
        "marker": marker,
        "partial": True,
        "updated": datetime.datetime.now().isoformat(),
    })
    with open(os.path.join(run_dir, "partial_summary.json"), "w") as handle:
        json.dump(summary, handle, indent=2, sort_keys=True)


def wait_for_xemu_match(args, proc, run_dir, expected_matches=1):
    deadline = time.time() + args.timeout_seconds
    last_change = time.time()
    last_snapshot = ""
    accumulated = ""
    stalled = False
    marker = ""
    history_path = os.path.join(run_dir, "poll_history.log")
    aggregate_path = os.path.join(run_dir, "xemu_ram_log_accumulated.txt")

    with open(history_path, "w") as history:
        while time.time() < deadline:
            snapshot, rc = poll_ram_log(args, run_dir, history)
            stamp = datetime.datetime.now().isoformat()
            if snapshot and snapshot != last_snapshot:
                last_snapshot = snapshot
                last_change = time.time()
                accumulated = append_snapshot(accumulated, snapshot, stamp)
                with open(aggregate_path, "w", errors="replace") as out:
                    out.write(accumulated)

            text = accumulated if accumulated else snapshot
            summary = jb_soak.summarize_log(text)
            write_partial_summary(run_dir, text, marker, False)
            if summary["fatalCount"]:
                marker = summary["fatalLines"][0]
                write_partial_summary(run_dir, text, marker, False)
                return False, marker, text, False
            if summary["matchEndedCount"] >= expected_matches and (expected_matches <= 1 or summary["mapListComplete"]):
                marker = summary["lastMatchEnded"]
                write_partial_summary(run_dir, text, marker, False)
                return True, marker, text, False
            if proc.poll() is not None:
                marker = "xemu exited with code %s" % proc.returncode
                write_partial_summary(run_dir, text, marker, False)
                return False, marker, text, False
            if time.time() - last_change >= args.stall_seconds:
                stalled = True
                marker = "stalled; no RAM log change for %.1fs" % (time.time() - last_change)
                write_partial_summary(run_dir, text, marker, stalled)
                return False, marker, text, stalled
            time.sleep(args.poll_interval)

    marker = "timeout; no RAM log change for %.1fs" % (time.time() - last_change)
    text = accumulated if accumulated else last_snapshot
    write_partial_summary(run_dir, text, marker, stalled)
    return False, marker, text, stalled


def copy_xemu_logs(args, run_dir):
    jb_soak.copy_if_exists(os.path.join(args.xemu_instance, "xemu.log"), run_dir)


def map_size_for_name(map_name, build_dir, runtime_source):
    for root in (build_dir, runtime_source):
        path = os.path.join(root, "Maps", map_name + ".unr")
        if os.path.isfile(path):
            return os.path.getsize(path)
    return 0


def run_one(args, evidence_dir, map_name, run_index, xiso_tool):
    run_name = "%02d_%s" % (run_index, map_name)
    run_dir = os.path.join(evidence_dir, run_name)
    ensure_dir(run_dir)

    stage_dir, url, copied = prepare_stage(args, map_name, run_dir)
    build_xiso(stage_dir, args.iso_path, xiso_tool)
    config_path = write_xemu_config(args, args.iso_path)

    proc = None
    started = datetime.datetime.now().isoformat()
    text = ""
    ok = False
    marker = ""
    stalled = False
    try:
        proc = launch_xemu(args, config_path, run_dir)
        ok, marker, text, stalled = wait_for_xemu_match(args, proc, run_dir)
        if ok and args.post_end_seconds > 0:
            time.sleep(args.post_end_seconds)
    finally:
        terminate_process(proc)
        close_process_streams(proc)
        stop_xemu_for_config(config_path)
        copy_xemu_logs(args, run_dir)

    ended = datetime.datetime.now().isoformat()
    summary = jb_soak.summarize_log(text)
    summary.update({
        "map": map_name,
        "run": run_index,
        "ok": bool(ok and summary["fatalCount"] == 0 and summary["maxBots"] >= args.initial_bots),
        "stalled": stalled,
        "marker": marker,
        "url": url,
        "started": started,
        "ended": ended,
        "xemuPid": proc.pid if proc else None,
        "xemuConfig": config_path,
        "isoPath": args.iso_path,
        "stageDir": stage_dir,
        "stageOverlayFiles": copied,
    })
    with open(os.path.join(run_dir, "summary.json"), "w") as handle:
        json.dump(summary, handle, indent=2, sort_keys=True)
    return summary


def run_continuous(args, evidence_dir, maps, xiso_tool):
    run_dir = os.path.join(evidence_dir, "continuous")
    ensure_dir(run_dir)

    stage_dir, urls, copied = prepare_continuous_stage(args, maps, run_dir)
    build_xiso(stage_dir, args.iso_path, xiso_tool)
    config_path = write_xemu_config(args, args.iso_path)
    jb_soak.copy_if_exists(config_path, run_dir, "xemu_soak_runtime_config")

    setup = {
        "displayBackend": args.display_backend or "xemu default",
        "expectedMatches": len(maps),
        "isoPath": args.iso_path,
        "launchCommand": build_xemu_command(args, config_path),
        "launchCommandStyle": "instance-local xemu.exe with -config_path and TCP monitor",
        "maps": maps,
        "monitorPort": args.monitor_port,
        "stageDir": stage_dir,
        "stageOverlayFiles": copied,
        "urls": urls,
        "xemuConfig": config_path,
    }
    with open(os.path.join(run_dir, "setup.json"), "w") as handle:
        json.dump(setup, handle, indent=2, sort_keys=True)

    proc = None
    started = datetime.datetime.now().isoformat()
    text = ""
    ok = False
    marker = ""
    stalled = False
    try:
        proc = launch_xemu(args, config_path, run_dir)
        ok, marker, text, stalled = wait_for_xemu_match(args, proc, run_dir, expected_matches=len(maps))
        if ok and args.post_end_seconds > 0:
            time.sleep(args.post_end_seconds)
    finally:
        terminate_process(proc)
        close_process_streams(proc)
        stop_xemu_for_config(config_path)
        copy_xemu_logs(args, run_dir)

    ended = datetime.datetime.now().isoformat()
    summary = jb_soak.summarize_log(text)
    summary.update({
        "continuousInstance": True,
        "expectedMatches": len(maps),
        "maps": maps,
        "ok": bool(
            ok
            and summary["fatalCount"] == 0
            and summary["maxBots"] >= args.initial_bots
            and summary["matchEndedCount"] >= len(maps)
            and (len(maps) <= 1 or summary["mapListComplete"])
        ),
        "stalled": stalled,
        "marker": marker,
        "urls": urls,
        "started": started,
        "ended": ended,
        "xemuPid": proc.pid if proc else None,
        "xemuConfig": config_path,
        "isoPath": args.iso_path,
        "stageDir": stage_dir,
        "stageOverlayFiles": copied,
    })
    with open(os.path.join(run_dir, "summary.json"), "w") as handle:
        json.dump(summary, handle, indent=2, sort_keys=True)
    return summary


def setup_one(args, evidence_dir, map_name, run_index, xiso_tool):
    safe_map = re.sub(r"[^A-Za-z0-9_.-]+", "_", map_name)
    run_dir = os.path.join(evidence_dir, "setup_%02d_%s" % (run_index, safe_map))
    ensure_dir(run_dir)

    original_stage_dir = args.stage_dir
    original_iso_path = args.iso_path
    args.stage_dir = os.path.abspath(os.path.join(
        XBOX_DIR,
        "build_cli",
        "xemu_jailbreak_soak_stage_%02d_%s" % (run_index, safe_map),
    ))
    args.iso_path = os.path.abspath(os.path.join(
        evidence_dir,
        "isos",
        "ut99_xemu_soak_%02d_%s.iso" % (run_index, safe_map),
    ))

    try:
        stage_dir, url, copied = prepare_stage(args, map_name, run_dir)
        build_xiso(stage_dir, args.iso_path, xiso_tool)
        config_path = write_xemu_config(args, args.iso_path)
        stable_config = os.path.join(run_dir, "xemu_soak_runtime_config")
        shutil.copy2(config_path, stable_config)
        setup = {
            "displayBackend": args.display_backend or "xemu default",
            "isoPath": args.iso_path,
            "launchCommand": build_xemu_command(args, stable_config),
            "map": map_name,
            "monitorPort": args.monitor_port,
            "stageDir": stage_dir,
            "stageOverlayFiles": copied,
            "url": url,
            "xemuConfig": stable_config,
        }
        with open(os.path.join(run_dir, "setup.json"), "w") as handle:
            json.dump(setup, handle, indent=2, sort_keys=True)
        return setup
    finally:
        args.stage_dir = original_stage_dir
        args.iso_path = original_iso_path


def setup_continuous(args, evidence_dir, maps, xiso_tool):
    run_dir = os.path.join(evidence_dir, "setup_continuous")
    ensure_dir(run_dir)
    stage_dir, urls, copied = prepare_continuous_stage(args, maps, run_dir)
    build_xiso(stage_dir, args.iso_path, xiso_tool)
    config_path = write_xemu_config(args, args.iso_path)
    stable_config = os.path.join(run_dir, "xemu_soak_runtime_config")
    shutil.copy2(config_path, stable_config)
    setup = {
        "continuousInstance": True,
        "displayBackend": args.display_backend or "xemu default",
        "expectedMatches": len(maps),
        "isoPath": args.iso_path,
        "launchCommand": build_xemu_command(args, stable_config),
        "launchCommandStyle": "instance-local xemu.exe with -config_path and TCP monitor",
        "maps": maps,
        "monitorPort": args.monitor_port,
        "stageDir": stage_dir,
        "stageOverlayFiles": copied,
        "urls": urls,
        "xemuConfig": stable_config,
    }
    with open(os.path.join(run_dir, "setup.json"), "w") as handle:
        json.dump(setup, handle, indent=2, sort_keys=True)
    return setup


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default=DEFAULT_BUILD)
    parser.add_argument("--runtime-source", default=DEFAULT_RUNTIME_SOURCE)
    parser.add_argument("--xemu-root", default=DEFAULT_XEMU_ROOT)
    parser.add_argument("--xemu")
    parser.add_argument("--xemu-instance", default=DEFAULT_XEMU_INSTANCE)
    parser.add_argument("--hdd", default=DEFAULT_HDD)
    parser.add_argument("--eeprom-source", default=DEFAULT_EEPROM_SOURCE)
    parser.add_argument("--bootrom")
    parser.add_argument("--flashrom")
    parser.add_argument("--iso-path")
    parser.add_argument("--stage-dir")
    parser.add_argument("--screenshot-dir")
    parser.add_argument("--monitor-port", type=int, default=DEFAULT_MONITOR_PORT)
    parser.add_argument("--maps", nargs="+")
    parser.add_argument("--largest-maps", type=int, default=4)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--min-players", type=int, default=8)
    parser.add_argument("--initial-bots", type=int, default=7)
    parser.add_argument("--goal-team-score", type=float, default=5.0)
    parser.add_argument("--time-limit", type=int, default=15)
    parser.add_argument("--difficulty", type=int, default=2)
    parser.add_argument("--timeout-seconds", type=int, default=2100)
    parser.add_argument("--stall-seconds", type=int, default=180)
    parser.add_argument("--post-end-seconds", type=int, default=15)
    parser.add_argument("--poll-interval", type=float, default=5.0)
    parser.add_argument("--poll-timeout", type=float, default=5.0)
    parser.add_argument("--phys-delta", default="auto")
    parser.add_argument("--display-backend", default="", help="Optional QEMU display backend. Empty uses Xemu's default display path.")
    parser.add_argument("--setup-only", action="store_true", help="Stage the selected map list, write the Xemu config, and build the ISO without launching Xemu.")
    parser.add_argument("--launch", action="store_true", help="Actually launch Xemu. Omit this to only stage configs/ISOs.")
    parser.add_argument("--separate-instances", action="store_true", help="Run each map in a fresh Xemu process. Default launch mode is one continuous Xemu process.")
    parser.add_argument("--continue-on-failure", action="store_true")
    parser.add_argument("--evidence-dir")
    args = parser.parse_args(argv)

    args.build_dir = os.path.abspath(args.build_dir)
    args.runtime_source = os.path.abspath(args.runtime_source)
    args.xemu_root = os.path.abspath(args.xemu_root)
    args.xemu_instance = os.path.abspath(args.xemu_instance)
    args.xemu = ensure_instance_xemu(args.xemu or xemu_exe_from_root(args.xemu_root), args.xemu_instance)
    args.bootrom = os.path.abspath(args.bootrom or os.path.join(args.xemu_root, "MCPX", "mcpx_1.0.bin"))
    args.flashrom = os.path.abspath(args.flashrom or os.path.join(args.xemu_root, "BIOS", "xbox-4627_debug.bin"))
    args.iso_path = os.path.abspath(args.iso_path or os.path.join(args.xemu_instance, "ut99_xemu_soak_current.iso"))
    args.stage_dir = os.path.abspath(args.stage_dir or os.path.join(XBOX_DIR, "build_cli", "xemu_jailbreak_soak_stage"))
    args.screenshot_dir = os.path.abspath(args.screenshot_dir or os.path.join(XBOX_DIR, "build_cli", "xemu_jailbreak_soak_screenshots"))

    ensure_file(args.xemu, "xemu.exe")
    ensure_file(args.bootrom, "MCPX boot ROM")
    ensure_file(args.flashrom, "Xbox BIOS")
    ensure_file(args.hdd, "Xemu HDD")
    ensure_file(args.eeprom_source, "EEPROM source")
    ensure_file(os.path.join(args.build_dir, "default.xbe"), "Built XBE")
    ensure_file(os.path.join(args.build_dir, "UnrealTournament.map"), "Linker map")
    ensure_file(POLL_SCRIPT, "Xemu RAM log poller")
    if not os.path.isdir(args.runtime_source):
        fail("Runtime source not found: " + args.runtime_source)

    build_root = os.path.abspath(os.path.join(args.build_dir, ".."))
    xiso_tool = find_xiso_tool(build_root)

    if args.maps:
        selected_maps = [(map_size_for_name(item, args.build_dir, args.runtime_source), item) for item in args.maps]
    else:
        map_source = args.build_dir if os.path.isdir(os.path.join(args.build_dir, "Maps")) else args.runtime_source
        selected_maps = jb_soak.largest_jailbreak_maps(map_source, args.largest_maps)
        args.maps = [item[1] for item in selected_maps]

    print("Selected Jailbreak maps by file size:")
    for size, map_name in selected_maps:
        print("  %s (%d bytes)" % (map_name, size))
    print("Xemu: " + args.xemu)
    print("Instance: " + args.xemu_instance)
    print("Monitor: 127.0.0.1:%d" % args.monitor_port)

    if args.evidence_dir:
        evidence_dir = os.path.abspath(args.evidence_dir)
    else:
        stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        evidence_dir = os.path.join(XBOX_DIR, "build_cli", "jailbreak_xemu_soak_" + stamp)
    ensure_dir(evidence_dir)

    if args.setup_only or not args.launch:
        if args.separate_instances:
            setups = []
            for index, map_name in enumerate(args.maps, 1):
                setups.append(setup_one(args, evidence_dir, map_name, index, xiso_tool))
            setup = {
                "continuousInstance": False,
                "displayBackend": args.display_backend or "xemu default",
                "launchCommandStyle": "instance-local xemu.exe with -config_path and TCP monitor",
                "launchRequired": bool(args.launch),
                "maps": args.maps,
                "setups": setups,
            }
        else:
            maps = []
            for _repeat in range(args.repeats):
                maps.extend(args.maps)
            setup = {
                "continuousInstance": True,
                "displayBackend": args.display_backend or "xemu default",
                "launchCommandStyle": "instance-local xemu.exe with -config_path and TCP monitor",
                "launchRequired": bool(args.launch),
                "maps": maps,
                "setup": setup_continuous(args, evidence_dir, maps, xiso_tool),
            }
        with open(os.path.join(evidence_dir, "setup.json"), "w") as handle:
            json.dump(setup, handle, indent=2, sort_keys=True)
        print("Setup-only complete.")
        print("Maps staged: %d" % len(setup["maps"]))
        print("Display backend: " + (args.display_backend or "xemu default"))
        if not args.launch:
            print("Launch skipped. Pass --launch to run Xemu.")
        return 0

    if not args.separate_instances:
        continuous_maps = []
        for _repeat in range(args.repeats):
            continuous_maps.extend(args.maps)
        print("Running one continuous Xemu instance across %d map entries" % len(continuous_maps))
        summary = run_continuous(args, evidence_dir, continuous_maps, xiso_tool)
        print("  ok={ok} matches={matchEndedCount}/{expectedMatches} maxBots={maxBots} minAvailKB={minAvailKB} lastTick={lastTick} marker={marker}".format(**summary))
        aggregate = {
            "continuousInstance": True,
            "evidenceDir": evidence_dir,
            "maps": continuous_maps,
            "repeats": args.repeats,
            "settings": {
                "minPlayers": args.min_players,
                "initialBots": args.initial_bots,
                "goalTeamScore": args.goal_team_score,
                "timeLimit": args.time_limit,
                "difficulty": args.difficulty,
                "monitorPort": args.monitor_port,
            },
            "runs": [summary],
            "allOk": bool(summary["ok"]),
        }
        with open(os.path.join(evidence_dir, "summary.json"), "w") as handle:
            json.dump(aggregate, handle, indent=2, sort_keys=True)
        print("Evidence: " + evidence_dir)
        print("ALL_OK=%s" % ("1" if aggregate["allOk"] else "0"))
        return 0 if aggregate["allOk"] else 1

    summaries = []
    run_index = 1
    for repeat in range(args.repeats):
        for map_name in args.maps:
            print("Running %s in Xemu repeat %d/%d" % (map_name, repeat + 1, args.repeats))
            summary = run_one(args, evidence_dir, map_name, run_index, xiso_tool)
            summaries.append(summary)
            print("  ok={ok} maxBots={maxBots} minAvailKB={minAvailKB} lastTick={lastTick} marker={marker}".format(**summary))
            if not summary["ok"] and not args.continue_on_failure:
                break
            run_index += 1
        if summaries and not summaries[-1]["ok"] and not args.continue_on_failure:
            break

    aggregate = {
        "continuousInstance": False,
        "evidenceDir": evidence_dir,
        "maps": args.maps,
        "repeats": args.repeats,
        "settings": {
            "minPlayers": args.min_players,
            "initialBots": args.initial_bots,
            "goalTeamScore": args.goal_team_score,
            "timeLimit": args.time_limit,
            "difficulty": args.difficulty,
            "monitorPort": args.monitor_port,
        },
        "runs": summaries,
        "allOk": bool(summaries) and all(item["ok"] for item in summaries),
    }
    with open(os.path.join(evidence_dir, "summary.json"), "w") as handle:
        json.dump(aggregate, handle, indent=2, sort_keys=True)
    print("Evidence: " + evidence_dir)
    print("ALL_OK=%s" % ("1" if aggregate["allOk"] else "0"))
    return 0 if aggregate["allOk"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
