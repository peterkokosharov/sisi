# Testing Sisi

Everything below can be done by one person alone, with copy-paste commands.

## One command for everything

From the repository root:

    python tools/run_tests.py

It runs every suite that can run on the machine and writes `test-results.txt`
(date UTC, git commit, OS, per suite: command, passed/failed/skipped, overall PASS/FAIL).
A suite that cannot run (for example no compiler) is listed as `not run here: <reason>`.
Add `--require-all` to treat "not run" as a failure (CI does this).

## Prerequisites

| OS | Needed |
|----|--------|
| All | Python 3.12 or newer, git |
| Windows 10/11 | Visual Studio 2022 Build Tools ("Desktop development with C++", includes CMake) - run from the "x64 Native Tools Command Prompt" |
| Linux / Mint | `sudo apt install build-essential cmake git python3` |
| macOS | `xcode-select --install` and `brew install cmake` |

No Qt is needed for the tests. No venv is needed either: the test tooling is Python standard
library only. (If you add pip-based test tools later, create a venv with
`python -m venv .venv`, activate it - `.venv\Scripts\activate` on Windows,
`source .venv/bin/activate` elsewhere - and `pip install` there. Production code stays stdlib only.)

## Suites

### 1. C++ unit tests - `tests/test_core.cpp`

Covers the parts of Sisi without UI: markdown fences and image paths, the INI parser
(`src/ini.c`), secret redaction, config precedence, models-list parsing, permission
handling and the context indicator (`src/core.h`, `src/markdown.h`). Plain C++, no framework.

    cmake -S tests -B build-tests
    cmake --build build-tests --config Release
    ctest --test-dir build-tests -C Release --output-on-failure

Output ends with `N checks, M failed`. Run the binary directly for the full list of failures:
`build-tests/sisi_tests` (Linux/macOS) or `build-tests\Release\sisi_tests.exe` (Windows).

### 2. AI attribution header check - `tools/check_ai_header.py`

Fails if a tracked file with an author/copyright header has no `AI models:` / `LLM:` line.

    python -I tools/check_ai_header.py

## What red means

- C++ suite, `FAIL file:line: expression` - a check in `tests/test_core.cpp` at that line failed;
  the code under test is in `src/`. Open the test line and the function it calls.
- C++ suite, build failed - a compile error in `src/` or the test; read the first error.
- Header check lists files - add the `AI models:` line (the model ids that wrote the file) to the header.
- `not run here` - not a failure; the tool named in the reason is missing on this machine.

## Continuous integration

`.github/workflows/build.yml` runs on every push and pull request. The `tests` job runs on
Ubuntu, macOS and Windows: it builds and runs the C++ suite, runs `tools/run_tests.py --require-all`
and uploads `test-results.txt` as the artifact `test-results-<os>`. Packaging jobs run only after
the tests are green. Tests are never copied into the release zip, tar.gz or .app (the packaging
steps copy an explicit list of files).

## Manual test plans

None exist yet. Before a release, start the built program once on each OS and check by hand:
it starts, a prompt reaches rex and an answer streams back, a diagram renders, and the
permission dialog appears. See the README sections "Quick start" and "Permissions".
