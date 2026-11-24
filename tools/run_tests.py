# SPDX-License-Identifier: MIT
# Author: Peter Kokosharov <peter.kokosharov@gmail.com>
# AI models: claude-sonnet-5-5
"""Run every test suite that can run on this machine and write test-results.txt.

Usage: python -I tools/run_tests.py [--require-all] [--no-write]
  --require-all  a suite that cannot run (e.g. no compiler) counts as a failure (used in CI)
  --no-write     print the report only

Stdlib only. Exit code 0 = every suite that ran passed (and, with --require-all, all ran).
"""
import datetime
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build-tests"


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace", **kw)


def suite_headers():
    cmd = [sys.executable, "-I", "tools/check_ai_header.py"]
    r = run(cmd)
    ok = r.returncode == 0
    return {"name": "AI attribution headers", "command": "python -I tools/check_ai_header.py",
            "passed": 1 if ok else 0, "failed": 0 if ok else 1, "skipped": 0,
            "detail": "" if ok else (r.stdout + r.stderr).strip()}


def suite_cpp():
    name = "C++ unit tests (tests/test_core.cpp)"
    cmdtxt = "cmake -S tests -B build-tests; cmake --build build-tests --config Release; ctest --test-dir build-tests -C Release"
    if not shutil.which("cmake") or not shutil.which("ctest"):
        return {"name": name, "command": cmdtxt, "not_run": "cmake/ctest not installed"}
    steps = [["cmake", "-S", "tests", "-B", "build-tests"],
             ["cmake", "--build", "build-tests", "--config", "Release"]]
    for s in steps:
        r = run(s)
        if r.returncode != 0:
            out = (r.stdout + r.stderr).strip()
            if re.search(r"No CMAKE_(C|CXX)_COMPILER|compiler.*not (found|able)", out, re.I):
                return {"name": name, "command": cmdtxt, "not_run": "no C/C++ compiler found"}
            return {"name": name, "command": cmdtxt, "passed": 0, "failed": 1, "skipped": 0,
                    "detail": "build failed: " + " ".join(s) + "\n" + out[-2000:]}
    r = run(["ctest", "--test-dir", "build-tests", "-C", "Release", "--output-on-failure", "-V"])
    out = r.stdout + r.stderr
    m = re.search(r"(\d+) checks, (\d+) failed", out)
    if m:
        total, failed = int(m.group(1)), int(m.group(2))
        res = {"passed": total - failed, "failed": failed}
    else:
        res = {"passed": 0, "failed": 1}
    if r.returncode != 0 and res["failed"] == 0:
        res["failed"] = 1
    res.update(name=name, command=cmdtxt, skipped=0, detail="" if r.returncode == 0 else out[-2000:])
    return res


def git_commit():
    r = run(["git", "rev-parse", "--short", "HEAD"])
    c = r.stdout.strip() or "unknown"
    if run(["git", "status", "--porcelain", "--untracked-files=no"]).stdout.strip():
        c += " (+uncommitted changes)"
    return c


def main():
    require_all = "--require-all" in sys.argv
    results = [suite_headers(), suite_cpp()]
    bad = False
    lines = ["Sisi test results",
             "date (UTC): " + datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S"),
             "git commit: " + git_commit(),
             "os: " + platform.platform(), ""]
    for r in results:
        lines.append("suite: " + r["name"])
        lines.append("  command: " + r["command"])
        if "not_run" in r:
            lines.append("  not run here: " + r["not_run"])
            bad = bad or require_all
        else:
            lines.append("  passed: %d  failed: %d  skipped: %d" % (r["passed"], r["failed"], r["skipped"]))
            if r["failed"]:
                bad = True
                lines += ["  " + l for l in r["detail"].splitlines()]
        lines.append("")
    lines.append("overall: " + ("FAIL" if bad else "PASS"))
    text = "\n".join(lines) + "\n"
    print(text, end="")
    if "--no-write" not in sys.argv:
        (ROOT / "test-results.txt").write_text(text, encoding="utf-8", newline="\n")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
