#!/usr/bin/env python3
"""Crash report check for OpenTech2: does a fatal error or a crash leave a report?

Starts the dedicated server (and once the hidden client) on base1 with
developer 1 and runs "crashtest <kind>", then reads <home>/crashes/:
  fatal   Com_Error(ERR_FATAL): report with the message, FATAL ERROR in the log
  segv    null write (SIGSEGV): report with the signal and a backtrace that
          addr2line resolves to Crash_Test_f
  abort   abort() (SIGABRT): report with the signal
  drop    Com_Error(ERR_DROP): no report (not fatal), server keeps running
  client  fatal error in the hidden client: report has the renderer
Every report must also hold the full command line and the console tail.

Each run gets its own temporary HOME. Logs and reports land in
build/crash-check/.
Usage: crash_check.py   (exit 1 on failure)
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CHECK_DIR = Path(__file__).resolve().parent
REPO_DIR = CHECK_DIR.parent.parent
SERVER_BINARY = REPO_DIR / "build" / "release" / "OpenTech2-server"
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
OUTPUT_DIR = REPO_DIR / "build" / "crash-check"
DATA_DIR = Path(os.environ.get("OPTECH2_DATA_DIR",
                               Path.home() / ".local/share/Steam/steamapps/common/Quake 2"))
TIMEOUT_SECONDS = 60

CLIENT_SETTINGS = ["+set", "vid_renderer", "soft", "+set", "r_vsync", "0", "+set", "cl_maxfps", "60",
                   "+set", "s_initsound", "0"]

results = []


def report(passed, text):
    results.append(passed)
    print(f"{'ok' if passed else 'FAIL':<6}{text}")


def run_crash(name, binary, kind, extra=()):
    """Runs one crashtest; returns (exit code, log text, report text or None, report count)."""
    with tempfile.TemporaryDirectory(prefix=f"optech2-crash-{name}-") as home:
        # the old-style hidden dir is used when it exists: no XDG lookup
        (Path(home) / ".opentech2").mkdir()
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        command = [str(binary), "-datadir", str(DATA_DIR), *extra, "+set", "developer", "1",
                   "+map", "base1", "+wait", "5", "+crashtest", kind]
        if kind == "drop":
            command += ["+wait", "5", "+echo", "still running", "+quit"]
        log_path = OUTPUT_DIR / f"{name}.log"
        with open(log_path, "w") as log_file:
            try:
                process = subprocess.run(command, env=environment, stdin=subprocess.DEVNULL, stdout=log_file,
                                         stderr=subprocess.STDOUT, timeout=TIMEOUT_SECONDS, check=False)
                exit_code = process.returncode
            except subprocess.TimeoutExpired:
                exit_code = "timeout"
        reports = sorted((Path(home) / ".opentech2" / "crashes").glob("crash-*.txt"))
        text = None
        if reports:
            shutil.copy(reports[0], OUTPUT_DIR / f"{name}-report.txt")
            text = reports[0].read_text(errors="replace")
        return exit_code, log_path.read_text(errors="replace"), text, len(reports)


def backtrace_names(report_text):
    """Function names for the backtrace lines, through addr2line (empty if it's missing)."""
    if not shutil.which("addr2line"):
        return None
    names = []
    for module, offset in re.findall(r"^\s+#\d+\s+(\S+)\+(0x[0-9a-f]+)", report_text, re.M):
        if "OpenTech2" not in module:
            continue
        module_path = Path(module) if Path(module).is_absolute() else REPO_DIR / module
        output = subprocess.run(["addr2line", "-f", "-e", str(module_path), offset],
                                capture_output=True, text=True).stdout.split("\n")
        names.append(output[0])
    return names


def check_common(name, report_text):
    report("Command line:" in report_text and "+crashtest" in report_text, f"{name}: full command line")
    tail = report_text.split("Last console output:", 1)[-1]
    report("OpenTech2 Initialized" in tail, f"{name}: console tail")
    report("Map:          base1" in report_text, f"{name}: map name")


def main():
    for binary in (SERVER_BINARY, CLIENT_BINARY):
        if not binary.exists():
            sys.exit(f"{binary} missing; build first (cmake --workflow --preset default)")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)

    exit_code, log, text, _ = run_crash("fatal", SERVER_BINARY, "fatal")
    report(exit_code == 1, f"fatal: exit {exit_code}")
    report("FATAL ERROR: crashtest: fatal error" in log, "fatal: message in the log")
    report(text is not None and "Reason:       Fatal error" in text
           and "Message:      crashtest: fatal error" in text, "fatal: report with reason + message")
    if text:
        check_common("fatal", text)

    exit_code, log, text, _ = run_crash("segv", SERVER_BINARY, "segv")
    report(exit_code == -11, f"segv: killed by signal 11 (exit {exit_code})")
    report(text is not None and "Reason:       Signal 11" in text, "segv: report with the signal")
    if text:
        names = backtrace_names(text)
        if names is None:
            print("skip  segv: addr2line missing, backtrace names not checked")
        else:
            report("Crash_Test_f" in names, f"segv: backtrace reaches Crash_Test_f ({len(names)} frames)")
        check_common("segv", text)

    exit_code, log, text, _ = run_crash("abort", SERVER_BINARY, "abort")
    report(exit_code == -6, f"abort: killed by signal 6 (exit {exit_code})")
    report(text is not None and "Reason:       Signal 6" in text, "abort: report with the signal")

    exit_code, log, text, count = run_crash("drop", SERVER_BINARY, "drop")
    report(count == 0, f"drop: no report ({count})")
    report("still running" in log, "drop: server kept running")

    exit_code, log, text, _ = run_crash("client", CLIENT_BINARY, "fatal", CLIENT_SETTINGS)
    report(exit_code == 1, f"client: exit {exit_code}")
    report(text is not None and "Renderer:     soft" in text, "client: report with the renderer")

    passed = sum(results)
    print(f"\n{'PASS' if all(results) else 'FAIL'}: {passed}/{len(results)} crash report checks")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
