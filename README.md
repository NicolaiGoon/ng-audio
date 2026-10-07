# Ng DAW

A desktop DAW built on [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) and JUCE.
Tracktion does the hard engine work (playback, plugin hosting, the edit model); this project
builds the workflow on top: FL Studio-style flexibility with REAPER/Cubase-style mixer mapping,
and every action available to AI tools through one command layer.

![First window](docs/first-window.png)

## What's here (v0.1)

- **A window that plays audio.** It opens with one synth (Tracktion's built-in 4OSC) playing a
  short arpeggio. *Add instrument*, *Play*, *Undo* and *Redo* all go through the command layer.
- **Linked-by-default mixer strips.** Adding an instrument creates a channel *and* a mixer strip
  with the same name and colour, already routed. Pick a different strip in the channel rack to
  detach it; several channels can share one strip; pick its own strip again to re-link it.
- **A command layer.** Every change is a named command with JSON parameters:

  | Command | What it does |
  | --- | --- |
  | `create_track` | Adds an instrument channel plus its linked mixer strip |
  | `route` | Sends a channel to a strip (or `master`); detaches it if it isn't its own strip |
  | `add_midi_clip` | Adds a MIDI clip with notes to a channel |
  | `play` / `stop` | Transport |
  | `get_state` | The whole project as JSON, so an AI can "see" the session |

  Each command is one undo step, a batch of commands is one undo step, and a failing command
  or batch is rolled back. The command console at the bottom of the window takes the same JSON
  an MCP server will send, for example:

  ```json
  {"command": "route", "params": {"channel": "Synth 2", "strip": "Synth 1"}}
  {"batch": "Two synths", "commands": [
    {"command": "create_track", "params": {"name": "Bass"}},
    {"command": "create_track", "params": {"name": "Lead"}}]}
  ```

## Layout

```
src/core/Session.*            the open project: Tracktion Edit + channel/strip model
src/core/CommandRegistry.*    command specs (name, description, JSON schema, handler)
src/core/CommandDispatcher.*  runs commands, batches, JSON requests, undo/redo
src/core/ProjectCommands.cpp  create_track, route, add_midi_clip, play, stop, get_state
src/app/                      the JUCE window (channel rack, mixer, transport, console)
tests/CommandTests.cpp        headless tests, including an offline render that checks for sound
```

`ng_core` is a static library holding JUCE, Tracktion and the command layer, so the app, the
tests and a future MCP server all share it. `CommandRegistry::describe()` already returns the
commands in MCP `tools/list` shape (name, description, inputSchema), and
`CommandDispatcher::executeJson()` is the call an MCP `tools/call` handler would make.

## Building

Needs CMake 3.22+, a C++20 compiler, and git. CMake downloads Tracktion Engine v3.2.0 and the
JUCE commit it pins on first configure (a few hundred MB; the first build takes a while).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The app ends up in `build/NgDaw_artefacts/Debug/`.

- **macOS:** Xcode command line tools. `-G Xcode` works too.
- **Windows:** Visual Studio 2022 with the C++ workload. `-G "Visual Studio 17 2022"`.
- **Linux (Debian/Ubuntu):**
  `sudo apt install libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libcurl4-openssl-dev`

To build against existing checkouts instead of downloading, pass
`-DFETCHCONTENT_SOURCE_DIR_TRACKTION=/path/to/tracktion_engine -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`.

## Licensing

Tracktion Engine and JUCE are each available under the GPLv3 or a commercial licence.
Releasing this app closed-source needs commercial licences for both.

## Next steps

- An MCP server executable that wraps `CommandRegistry::describe()` and `executeJson()`
- `rename_and_color`, `unlink`, and keeping name and colour in sync while linked
- Multi-output instruments getting a folder of strips that sums to one bus
- Tags (`drums`, `bass`, `vox`) that create and route buses automatically
- Plugin scanning and VST3/CLAP instruments, project save/load
