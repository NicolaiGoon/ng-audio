# Building Ng DAW

Ng DAW is one CMake project (`CMakeLists.txt` at the repo root). It downloads its dependencies
itself, so all a fresh checkout needs is a compiler, CMake and git.

- [Prerequisites](#prerequisites)
- [Quick start](#quick-start)
- [How dependencies are fetched](#how-dependencies-are-fetched)
- [Configuring and building](#configuring-and-building)
- [Targets](#targets)
- [Options](#options)
- [Output locations](#output-locations)
- [Build caching](#build-caching)
- [Running the app and the MCP server from a build tree](#running-the-app-and-the-mcp-server-from-a-build-tree)
- [Common problems](#common-problems)

## Prerequisites

All platforms need:

- **CMake 3.22 or newer** (`cmake_minimum_required(VERSION 3.22)`).
- **A C++20 compiler** (`CMAKE_CXX_STANDARD 20`, required).
- **git**, used by CMake's `FetchContent` to download Tracktion Engine, JUCE and Rubber Band.
- Network access on the first configure, and a few GB of disk: the sources are a few hundred MB,
  and a build tree holds JUCE and Tracktion compiled.

| Platform | Toolchain | Notes |
| --- | --- | --- |
| Windows | Visual Studio 2022 with the *Desktop development with C++* workload (MSVC) | Generator `-G "Visual Studio 17 2022"`, or Ninja from a *x64 Native Tools Command Prompt for VS 2022*. CI uses the `windows-2022` runner. |
| macOS | Xcode command line tools (`xcode-select --install`) | `-G Xcode` works too. AU plugin hosting is turned on for Apple builds (`JUCE_PLUGINHOST_AU=1`). macOS is **not** built in CI. |
| Linux | GCC (CI uses the default GCC on `ubuntu-24.04`) | Needs the system packages below. `libatomic` is linked. |

### Linux system packages (Debian/Ubuntu)

The headers JUCE needs, as listed in the README:

```sh
sudo apt install libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev
```

CI installs these plus Ninja, ccache and Xvfb (Xvfb is only needed to run the tests without a
display; see [testing.md](testing.md)):

```sh
sudo apt-get update
sudo apt-get install -y ninja-build ccache libasound2-dev libjack-jackd2-dev libfreetype-dev \
  libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev xvfb
```

You also need the usual toolchain basics (`build-essential`, `cmake`, `git`, `pkg-config`), which
the GitHub runner already has.

## Quick start

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The app ends up in `build/NgDaw_artefacts/Debug/`, and the MCP server in
`build/NgDawMcp_artefacts/Debug/`.

## How dependencies are fetched

`CMakeLists.txt` uses `FetchContent` to download three repositories during the **first configure**:

| Dependency | Source | Version |
| --- | --- | --- |
| Tracktion Engine | `https://github.com/Tracktion/tracktion_engine.git` | tag `v3.2.0`, shallow clone, **no submodules** |
| JUCE | `https://github.com/juce-framework/JUCE.git` | commit `19edd538429c93d277bf95b55aaa7e3eb545f951` (the JUCE that Tracktion v3.2.0 pins) |
| Rubber Band | `https://github.com/breakfastquay/rubberband.git` | tag `v4.0.0`, shallow clone |

Some details:

- **Why JUCE is fetched separately.** Tracktion's own JUCE submodule URL uses SSH, which fails
  without a GitHub SSH key, so Tracktion is cloned with `GIT_SUBMODULES ""` and JUCE is cloned
  over HTTPS on its own.
- **How they're added.** JUCE and Tracktion's `modules/` folder are added with
  `add_subdirectory(... EXCLUDE_FROM_ALL)`, so only what Ng DAW's targets use gets built.
- **Rubber Band** isn't built as a library. Tracktion compiles it in as a single file
  (`TRACKTION_BUILD_RUBBERBAND=1`) and includes `<rubberband/single/RubberBandSingle.cpp>`.
  CMake writes a small forwarding file to `build/rubberband_include/rubberband/single/RubberBandSingle.cpp`
  that points at the download.
- **Tracktion is patched while configuring.** CMake edits
  `modules/tracktion_engine/timestretch/tracktion_TimeStretch.cpp` in the fetched Tracktion
  source, in place:
  1. It switches Rubber Band from its R2 engine (`OptionWindowShort`) to R3 (`OptionEngineFiner`).
  2. It caps how much input the stretcher asks for at `getMaxFramesNeeded() / 2`, so R3 can't
     overrun the real-time reader's buffers.

  Both patches are idempotent. If the source no longer matches (for example after a Tracktion
  upgrade), configure prints a warning and goes on:
  `Couldn't switch Tracktion's Rubber Band stretcher to the R3 engine; clips will use R2` or
  `Couldn't cap Rubber Band's input request; real-time stretching may overrun`.

By default the downloads go to `build/_deps/`. Set `FETCHCONTENT_BASE_DIR` to keep them somewhere
else, so deleting `build/` doesn't download them again (CI does this, see [Build caching](#build-caching)).

### Using existing checkouts

To build against local checkouts instead of downloading:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DFETCHCONTENT_SOURCE_DIR_TRACKTION=/path/to/tracktion_engine \
  -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
```

`-DFETCHCONTENT_SOURCE_DIR_RUBBERBAND=/path/to/rubberband` works the same way. Use the versions
in the table above. **Note** that the Tracktion patches above are written into your checkout's
`tracktion_TimeStretch.cpp`.

## Configuring and building

### Single-config generators (Ninja, Unix Makefiles)

The build type is picked when configuring:

```sh
# Debug
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# Release
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
```

Always set `CMAKE_BUILD_TYPE`: JUCE puts its outputs in a folder named after the configuration
(`NgDaw_artefacts/Debug/`), so the paths in this document assume one is set.

With Ninja (as CI does):

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
```

### Multi-config generators (Visual Studio, Xcode)

One build tree holds every configuration; pick one with `--config` when building:

```sh
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug --parallel
cmake --build build --config Release --parallel
```

The release workflow builds just the two shipped programs:

```sh
cmake -B build -G "Visual Studio 17 2022" -DFETCHCONTENT_BASE_DIR="<repo>/.deps"
cmake --build build --config Release --parallel --target NgDaw NgDawMcp
```

`build/` and `build-*/` (and `cmake-build-*/`) are in `.gitignore`, so several build trees can sit
side by side, e.g. `build/` and `build-linux/`.

### Compiler settings worth knowing

- **Static C++ runtime on Windows.** `CMAKE_MSVC_RUNTIME_LIBRARY` is
  `MultiThreaded$<$<CONFIG:Debug>:Debug>`, so the executables don't need the Visual C++
  redistributable. This is why the installer ships just two `.exe` files.
- **Compile definitions** on `ng_core` (and everything that links it) include
  `JUCE_PLUGINHOST_VST3=1`, `JUCE_USE_CURL=0`, `JUCE_WEB_BROWSER=0`, `JUCE_USE_MP3AUDIOFORMAT=1`,
  `TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1`, `TRACKTION_ENABLE_TIMESTRETCH_RUBBERBAND=1`,
  `TRACKTION_BUILD_RUBBERBAND=1` and `NG_VERSION="${PROJECT_VERSION}"`.
- **GCC**: `-Wno-unknown-pragmas`, and on GCC 15+ `-Wno-template-body` (GCC 15 errors on a
  broken but unused template in Tracktion's bundled nanorange).
- **The version** comes from `project(NgDaw VERSION 1.0.0 ...)`. It's the app's version
  (`NG_VERSION`) and the test plugins' version, and the release workflow reads it (see
  [release.md](release.md)).

## Targets

| Target | Kind | What it is |
| --- | --- | --- |
| `ng_core` | static library | JUCE and Tracktion compiled once, plus the project model and the command layer (`src/core/`). The app, the MCP server and the tests all link it. |
| `NgDaw` | JUCE GUI app (`juce_add_gui_app`) | The desktop app, product name **Ng DAW** (`src/app/`). Bundle id `audio.ng.daw`, company *Ng Audio*, microphone permission text "Audio recording". |
| `NgDawMcp` | JUCE console app (`juce_add_console_app`) | The MCP server (`src/mcp/McpMain.cpp`): JSON-RPC over stdio, passed to the running app through the bridge. |
| `NgDawTests` | JUCE console app | Headless tests (`tests/CommandTests.cpp`). Only with `BUILD_TESTING`. Depends on the three test plugins. |
| `NgTestSynth`, `NgTestSynth_VST3` | JUCE plugin | *Ng Test Synth*, a one-voice sine VST3 instrument (`tests/TestSynth/`). Only with `BUILD_TESTING`. |
| `NgTestGain`, `NgTestGain_VST3` | JUCE plugin | *Ng Test Gain*, a gain VST3 effect (`tests/TestGain/`). Only with `BUILD_TESTING`. |
| `NgTestDrums`, `NgTestDrums_VST3` | JUCE plugin | *Ng Test Drums*, a two-output VST3 instrument (`tests/TestDrums/`). Only with `BUILD_TESTING`. |

`juce_add_plugin` makes two targets per plugin: `<Name>` (the shared code) and `<Name>_VST3` (the
bundle). The test plugins are built with `VST3_AUTO_MANIFEST FALSE` and aren't copied into the
system plugin folders.

Also present, from the tools CMake and JUCE bring in:

- `juceaide`, the helper JUCE builds while configuring (for generating plist/resource files).
- `include(CTest)` adds the CTest dashboard targets (`Continuous`, `Experimental`, `Nightly`, ...),
  which Ng DAW doesn't use.

Build one target with `--target`:

```sh
cmake --build build --target NgDaw        # just the app
cmake --build build --target NgDawMcp     # just the MCP server
cmake --build build --target NgDawTests   # the tests (and the test plugins)
```

## Options

| Option | Default | What it does |
| --- | --- | --- |
| `CMAKE_BUILD_TYPE` | (none) | `Debug` or `Release` with single-config generators. |
| `BUILD_TESTING` | `ON` (from `include(CTest)`) | Builds `NgDawTests` and the three test plugins and registers the `command_layer` ctest test. `-DBUILD_TESTING=OFF` skips them all. |
| `NG_COMPILER_LAUNCHER` | (unset) | A compiler cache such as `ccache` or `sccache` for Ng DAW's targets (which compile JUCE's and Tracktion's modules too). See [Build caching](#build-caching). |
| `FETCHCONTENT_BASE_DIR` | `build/_deps` | Where the dependencies are downloaded. |
| `FETCHCONTENT_SOURCE_DIR_TRACKTION` / `_JUCE` / `_RUBBERBAND` | (unset) | Use a local checkout instead of downloading. |
| `FETCHCONTENT_FULLY_DISCONNECTED` | `OFF` | Standard FetchContent switch: `ON` skips every download/update step (offline rebuilds once `_deps` is populated). |
| `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT` | (CMake default) | CI passes `Embedded` (with `-DCMAKE_POLICY_DEFAULT_CMP0141=NEW`) so MSVC writes `/Z7` debug info that sccache can cache. |

## Output locations

JUCE puts every target's output under `<build>/<Target>_artefacts/<Config>/`:

| Target | Windows | macOS | Linux |
| --- | --- | --- | --- |
| App | `build/NgDaw_artefacts/Debug/Ng DAW.exe` | `build/NgDaw_artefacts/Debug/Ng DAW.app` | `build/NgDaw_artefacts/Debug/Ng DAW` |
| MCP server | `build/NgDawMcp_artefacts/Debug/NgDawMcp.exe` | `build/NgDawMcp_artefacts/Debug/NgDawMcp` | `build/NgDawMcp_artefacts/Debug/NgDawMcp` |
| Tests | `build/NgDawTests_artefacts/Debug/NgDawTests.exe` | `build/NgDawTests_artefacts/Debug/NgDawTests` | `build/NgDawTests_artefacts/Debug/NgDawTests` |
| Test plugins | `build/NgTestSynth_artefacts/Debug/VST3/Ng Test Synth.vst3` (likewise `NgTestGain_artefacts/.../Ng Test Gain.vst3`, `NgTestDrums_artefacts/.../Ng Test Drums.vst3`) | same | same |

Replace `Debug` with `Release` for a Release build. The installer from the release workflow lands
in `build/installer/` (see [release.md](release.md)).

## Build caching

The first build compiles all of JUCE and Tracktion, which is most of the build time. Two things
avoid paying for that again:

**1. Keep the downloads outside the build tree.** Pass `-DFETCHCONTENT_BASE_DIR=<somewhere>`
(CI uses `<workspace>/.deps`), so a fresh `build/` doesn't download a few hundred MB again.

**2. Use a compiler cache through `NG_COMPILER_LAUNCHER`**, not `CMAKE_CXX_COMPILER_LAUNCHER`:

```sh
# Linux / macOS with ccache
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNG_COMPILER_LAUNCHER=ccache

# Windows with sccache, from a "x64 Native Tools Command Prompt for VS 2022"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNG_COMPILER_LAUNCHER=sccache ^
  -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
```

Why it's done this way (from the commits that set it up):

- `CMakeLists.txt` sets `CMAKE_C_COMPILER_LAUNCHER`/`CMAKE_CXX_COMPILER_LAUNCHER` from
  `NG_COMPILER_LAUNCHER` **after** adding JUCE. JUCE passes the launcher variables on to
  `juceaide`, which it builds while configuring, but not the embedded debug-info setting, so with
  sccache that build hit MSVC's shared PDB and configure failed with `C1041`. Setting the launcher
  later keeps the cache away from `juceaide`.
- sccache can't cache MSVC's shared PDB (`/Zi`), so debug info has to be embedded (`/Z7`):
  `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded` (needs policy `CMP0141` set to `NEW`).
- `juce_recommended_config_flags` adds `/Zi` in Debug, which overrides that. When
  `NG_COMPILER_LAUNCHER` is set and the compiler is MSVC, `CMakeLists.txt` rewrites that target's
  `INTERFACE_COMPILE_OPTIONS`, replacing `/Zi` with `/Z7`.
- Use the Ninja generator for caching. The Visual Studio generator ignores compiler launchers.

In CI (`.github/workflows/ci.yml`), `hendrikmuhs/ccache-action@v1.2` keeps a 2 GB cache keyed
`<sccache|ccache>-<OS>`, and `actions/cache@v4` keeps `.deps` keyed
`deps-<OS>-<hash of CMakeLists.txt>`, so changing `CMakeLists.txt` downloads the dependencies again.

## Running the app and the MCP server from a build tree

### The app

```sh
# Linux
"./build/NgDaw_artefacts/Debug/Ng DAW"
# macOS
open "build/NgDaw_artefacts/Debug/Ng DAW.app"
```

```powershell
# Windows
& ".\build\NgDaw_artefacts\Debug\Ng DAW.exe"
```

An absolute path to a `.ngdaw` file on the command line opens that project (this is how double-
clicking a project in Explorer or Finder works):

```powershell
& ".\build\NgDaw_artefacts\Debug\Ng DAW.exe" "C:\Users\me\Music\Ng DAW\Song.ngdaw"
```

### The MCP server

`NgDawMcp` reads JSON-RPC messages from stdin, one per line, and replies on stdout. It passes each
tool call to the running app through the bridge, so **start Ng DAW first**. The app listens on
127.0.0.1 and writes its port and a secret token to `bridge.json` in the user's app data folder
(`%APPDATA%\Ng DAW` on Windows, `~/Library/Application Support/Ng DAW` on macOS,
`~/.config/Ng DAW` on Linux). Set `NG_DAW_BRIDGE_FILE` to use another path, for both the app and
the server.

Add it to Claude Code:

```sh
claude mcp add ng-daw -- /path/to/build/NgDawMcp_artefacts/Debug/NgDawMcp
```

or to Claude Desktop's `claude_desktop_config.json`:

```json
{"mcpServers": {"ng-daw": {"command": "/path/to/build/NgDawMcp_artefacts/Debug/NgDawMcp"}}}
```

Quick check that the server starts (it answers `initialize` without the app, and exits when its
input closes; this is the check the release workflow runs):

```sh
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' | ./build/NgDawMcp_artefacts/Debug/NgDawMcp
```

The reply contains `"serverInfo"`. See [mcp.md](mcp.md) for the protocol and tools.

## Common problems

**Configure takes ages / fails on the download.** The first configure clones Tracktion, JUCE and
Rubber Band over HTTPS. Check network/proxy access to github.com. Once `_deps` (or your
`FETCHCONTENT_BASE_DIR`) is populated, `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` reconfigures offline.

**Git asks for an SSH key while fetching Tracktion.** You're probably fetching Tracktion's
submodules some other way. `CMakeLists.txt` clones Tracktion with `GIT_SUBMODULES ""` and gets JUCE
separately over HTTPS for exactly this reason.

**`fatal error C1041: cannot open program database` (Windows, with sccache).** sccache met MSVC's
shared PDB. Pass the cache as `-DNG_COMPILER_LAUNCHER=sccache` (not `CMAKE_CXX_COMPILER_LAUNCHER`),
together with `-DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded`,
and use Ninja. If you changed these on an existing tree, delete `build/` and configure again.

**`cl` or `link` not found with Ninja on Windows.** Ninja needs the MSVC environment. Run CMake
from a *x64 Native Tools Command Prompt for VS 2022* (CI uses `ilammy/msvc-dev-cmd@v1`), or use
the Visual Studio generator.

**"Does not match the generator used previously".** A build tree is tied to one generator.
Delete it, or configure into another folder (`build-ninja/`, say).

**Outputs aren't where this document says.** With a single-config generator, set
`CMAKE_BUILD_TYPE`. With Visual Studio or Xcode, the folder is whatever `--config` you built.

**Missing `alsa/asoundlib.h`, `ft2build.h`, `X11/...` on Linux.** Install the
[Linux packages](#linux-system-packages-debianubuntu).

**Undefined references to `__atomic_*` on Linux.** `ng_core` links `atomic` on Linux; make sure
libatomic is installed (it comes with GCC on Debian/Ubuntu).

**GCC 15 errors inside Tracktion's nanorange.** `CMakeLists.txt` adds `-Wno-template-body` for
GCC 15+; if you see it, check that `CMAKE_CXX_COMPILER_VERSION` is detected properly (a stale cache
from an older GCC can cause it). Delete `CMakeCache.txt` and reconfigure.

**Configure warns "Couldn't switch Tracktion's Rubber Band stretcher to the R3 engine" or
"Couldn't cap Rubber Band's input request".** The Tracktion source doesn't match the text the
patches look for, usually because a different Tracktion version is in use (a local checkout via
`FETCHCONTENT_SOURCE_DIR_TRACKTION`, or a changed tag). The build still works, but audio clips fall
back to Rubber Band's R2 engine or may overrun while stretching in real time. Use Tracktion v3.2.0,
or update the `string(REPLACE ...)` patterns in `CMakeLists.txt`.

**FetchContent deprecation warnings on new CMake.** `CMakeLists.txt` calls
`FetchContent_Populate(<name>)` directly, which newer CMake versions (3.30+) warn about
(policy `CMP0169`). The warning is harmless.

**Tests can't find the test plugins.** `NgDawTests` looks for them in
`build/NgTest*_artefacts/<Config>/VST3`, baked in at compile time. Build `NgDawTests` (which builds
the plugins first), and run ctest with the same configuration (`-C Debug`). See
[testing.md](testing.md).
