#!/usr/bin/env python3
"""TEST ONLY: explicit process delay / B process failure around a real Worker.

Never install this as a platform executable. It permits a real browser to
observe queued runs, navigation independence and failed-B result handling.
Successful A/B executions are forwarded unmodified to the tested binary.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import time

worker = os.environ["PLATFORM_BROWSER_REAL_WORKER"]
if len(sys.argv) > 1 and sys.argv[1].startswith("--"):
    os.execv(worker, [worker, *sys.argv[1:]])
line = sys.stdin.buffer.readline()
command = json.loads(line)
time.sleep(2)
if (command.get("execution", {}).get("physical_layer") == "Channel2Waveform"
        and Path(os.environ["PLATFORM_BROWSER_FAIL_B_SENTINEL"]).exists()):
    sys.stderr.write("Explicit browser acceptance fault injection: B process exit.\n")
    raise SystemExit(73)
result = subprocess.run([worker, *sys.argv[1:]], input=line)
raise SystemExit(result.returncode)
