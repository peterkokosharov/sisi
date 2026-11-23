# Sisi Valkodav

A small Qt 6 desktop chat window for rex (a Python agent). Sisi was a friend's
little bolonka who once fought a big dog over a bone; "Valkodav" (roughly "wolfhound") is the joke.

Hobby project, MIT, use it anywhere.

## Using it

- Fill in **Endpoint** (an OpenAI-style server, e.g. LiteLLM), pick a **Model** (the list is read from
  `<endpoint>/v1/models`, you can also type one) and a **Workspace** folder, type a prompt, press
  Ctrl+Enter or Send. Stop interrupts the current turn. The `+` button shows a second pane with every
  tool/shell call rex makes. Menu Sisi > Reset conversation starts a fresh context.
- Changing endpoint, model or workspace restarts rex on the next Send (the conversation starts fresh).

## Running with rex

Sisi starts `<python> -m runtime --model <model> --jsonl --local-tools` with
`RUNTIME_LITELLM__BASE_URL=<endpoint>`, `PYTHONPATH=<rex_dir>` and the workspace as working directory.
Put the rex checkout in a folder `rex` next to the program or set `rex_dir`.

## Config

Order: environment `SISI_ENDPOINT`, `SISI_MODEL`, `SISI_WORKSPACE`, `SISI_PYTHON`, `SISI_REX_DIR`,
`SISI_LOG`, `SISI_CONFIG` (path of the ini) > `sisi.ini` (section `[sisi]`, next to the program; on
Linux/macOS also `~/.config/sisi/sisi.ini`) > defaults (`http://127.0.0.1:4000`, `haiku`, current
directory, `python` on Windows / `python3` elsewhere, `rex` next to the program). See `sisi.ini.example`.
The optional log masks keys, tokens, passwords and URL credentials.
If your endpoint needs an API key, set `api_key` (or `SISI_API_KEY`); otherwise an existing
`RUNTIME_LITELLM__API_KEY` from the environment is passed on, or the dummy value `dummy`.

## Downloads

Built by GitHub Actions: Linux tar.gz (run `./sisi`; Qt 6 libraries are included, you need the usual
system X11/OpenGL libraries, e.g. `libxcb-cursor0`), macOS zip (unsigned: right-click > Open the first
time; put `sisi.ini` in `~/.config/sisi/`), Windows zip (run `sisi.exe`; if it still does not start, run `vc_redist.x64.exe` from the zip once).

## Build

Needs Qt 6 (Widgets, Network, Svg), CMake and a C++17 compiler:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests

## License

MIT (`LICENSE`). Qt 6 is used under the LGPLv3, dynamically linked; the INI parser keeps its Wine
license (`LICENSE-ini`). See `THIRD-PARTY.txt`.
