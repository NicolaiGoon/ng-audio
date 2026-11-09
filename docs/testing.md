# Testing Ng DAW

Ng DAW's tests are headless checks of the command layer and of audio rendering. They run with no
audio device and no window, so they work on CI machines.

- [How the tests are structured](#how-the-tests-are-structured)
- [Running the tests](#running-the-tests)
- [What CommandTests covers](#what-commandtests-covers)
- [The test plugins](#the-test-plugins)
- [Adding a test](#adding-a-test)
- [Adding a test plugin](#adding-a-test-plugin)

## How the tests are structured

Everything lives in `tests/`:

```
tests/CommandTests.cpp        every test, the helpers they share, and main()
tests/TestSynth/TestSynth.cpp a tiny VST3 instrument
tests/TestGain/TestGain.cpp   a tiny VST3 effect
tests/TestDrums/TestDrums.cpp a tiny two-output VST3 instrument
```

`CMakeLists.txt` builds them when `BUILD_TESTING` is on (the default, from `include(CTest)`):

- `NgDawTests` is a JUCE console app built from `tests/CommandTests.cpp` and linked to `ng_core`,
  the same library the app and the MCP server use.
- It depends on `NgTestSynth_VST3`, `NgTestGain_VST3` and `NgTestDrums_VST3`, so building it builds
  the three plugins first.
- The folders of the built plugins are compiled into the tests as `NG_TEST_PLUGIN_FOLDER`,
  `NG_TEST_EFFECT_FOLDER` and `NG_TEST_DRUMS_FOLDER`
  (`<build>/NgTest{Synth,Gain,Drums}_artefacts/$<CONFIG>/VST3`).
- One ctest test is registered: `add_test(NAME command_layer COMMAND NgDawTests)`.

**It's one process, not one ctest test per function, on purpose.** Tearing down the engine after a
VST3 plugin has run sometimes crashes in JUCE's Linux run loop, and 45+ teardowns instead of one
turned that into a flaky CI. So `NgDawTests` creates one `tracktion::Engine` (named `NgDawTests`,
with a headless `EngineBehaviour` that doesn't open the audio device) and runs every test function
against it.

Inside `CommandTests.cpp`:

- `CHECK(x)` prints `  ok   x  (line N)` or `  FAIL x  (line N)` and counts failures. It doesn't
  stop the test, so one run shows every failed check.
- Each test starts by printing a one-line description, then makes a `Fixture`: a fresh
  `ng::Session`, a `CommandRegistry` with every command registered (`ng::registerProjectCommands`)
  and a `CommandDispatcher`. `f.dispatcher.execute ("name", obj ({ ... }))` runs a command, and
  `f.state()` returns `get_state`.
- Audio checks render the edit offline at 44.1 kHz: `renderBuffer (f)` returns the audio and
  `renderRms (f)` its left-channel RMS (0 for an empty project, -1 if the render failed);
  `rmsBetween (buffer, from, to)` measures a stretch in seconds. These render as the song would
  play, with mute and solo applied.
- `main()` runs each test with `RUN (testName);`, then prints `All checks passed` and exits 0, or
  `Some checks failed` and exits 1.

Tests write their files to the system temp folder (`juce::TemporaryFile`, and folders such as
`NgDawSamplerTest`, `NgDawExportTest`, `NgDawLibraryTest`). The bridge test writes its connection
file to a temporary file, not to the real `bridge.json`, and the library test puts the library
settings back when it's done.

## Running the tests

After building (see [building.md](building.md)):

```sh
ctest --test-dir build --output-on-failure
```

With a multi-config generator (Visual Studio, Xcode), or to match CI, name the configuration:

```sh
ctest --test-dir build -C Debug --output-on-failure
```

On Linux without a display (a server, a container, CI), run them under Xvfb, since JUCE's GUI
initialisation needs an X display:

```sh
xvfb-run -a ctest --test-dir build -C Debug --output-on-failure
```

These are the exact commands CI runs (`Test (Windows)` and `Test (Linux)` in
`.github/workflows/ci.yml`).

### Running the test program directly

ctest only shows the output when the test fails (with `--output-on-failure`). Run `NgDawTests`
itself to see every check, and to pick tests:

```sh
./build/NgDawTests_artefacts/Debug/NgDawTests                 # every test
./build/NgDawTests_artefacts/Debug/NgDawTests Bridge          # every test with "Bridge" in its name (case-insensitive)
./build/NgDawTests_artefacts/Debug/NgDawTests =testEffects    # exactly that test
```

On Windows it's `build\NgDawTests_artefacts\Debug\NgDawTests.exe`. A filter that matches no test
prints `No test is called '...'` and fails, so a typo doesn't pass silently.

### Things that change what's checked

- **MP3 export.** `testExport` exports an MP3 only if `ng::findMp3Encoder()` finds LAME or FFmpeg
  (on the PATH or next to the program). Without one it checks for the error that mentions LAME
  instead. Either way the test passes; the output says which happened.
- **POSIX file permissions.** On macOS and Linux, `testBridgeAndMcpServer` also checks that the
  bridge's connection file is created with mode `0600`.

## What CommandTests covers

51 test functions, run in the order below (the order in `main()`). Each line is the description the
test prints.

**Tracks, routing and undo**

| Test | Checks |
| --- | --- |
| `testCreateTrackLinksAStrip` | `create_track` makes a linked channel and strip |
| `testUndoAfterTracktionTidiesUp` | undo still undoes the last command after Tracktion's delayed tidy-up |
| `testRouteDetachesAndShares` | `route` detaches a channel and lets strips be shared |
| `testUndoIsOneStepPerCommand` | each command and each batch is one undo step |
| `testFailuresRollBack` | failed commands and batches leave the project unchanged |
| `testJsonBridgeAndToolList` | JSON requests and the MCP tool list |
| `testRenameAndRecolourStayInSyncWhileLinked` | `rename_track` and `set_track_colour` keep linked channels and strips in sync |
| `testFindByIdOrName` | tracks and clips are found by id, and tracks by a name that looks like an id |
| `testDeleteTrack` | `delete_track` removes channels and strips and fixes up routing |
| `testSetInstrument` | `set_instrument` swaps a channel's instrument |

**Mixer and transport**

| Test | Checks |
| --- | --- |
| `testMixerStripSettings` | `set_mixer_strip` sets fader, pan, mute and solo |
| `testGestureIsOneUndoStep` | a fader drag (gesture) is one undo step |
| `testMuteAndSoloSilence` | mute and solo change what renders |
| `testTempoAndTimeSignature` | `set_tempo` and `set_time_signature` |
| `testLoopRegion` | `set_loop` sets the loop region in beats |

**Steps, patterns, projects and clips**

| Test | Checks |
| --- | --- |
| `testStepSequencer` | `set_steps` builds a channel's step pattern |
| `testPatternRenders` | a step pattern makes sound |
| `testSaveAndOpen` | save, open and unsaved changes |
| `testClipsAndPlayheadInState` | `get_state` lists clips in beats and `set_position` moves the playhead |
| `testClipEditing` | clips move, resize, split, duplicate and delete |
| `testClipNamesAndColours` | clips can be renamed and given a colour of their own |
| `testPatternCopiesFollowSteps` | copies of a step pattern follow the channel's steps |
| `testPatterns` | patterns hold a row per channel and are placed as blocks |
| `testMoveAndFoldStrips` | mixer strips can be reordered, and a strip's inputs folded away |
| `testEditingNotes` | notes are read and replaced, timed from the clip's start |
| `testQuantizeAndVelocity` | notes quantize to a grid and keep their velocities |
| `testArmingForMidiInput` | one channel at a time is armed for MIDI input |

**Plugins, effects, buses and the sampler** (these load the test plugins)

| Test | Checks |
| --- | --- |
| `testPluginScanning` | scanning finds a VST3 and lists it (Ng Test Synth); bad folders and formats are refused |
| `testPluginInstrument` | a scanned plugin plays as a channel's instrument, and is saved with the project |
| `testEffects` | insert effects on mixer strips (built-in ones and Ng Test Gain) |
| `testBusesAndSends` | buses and sends |
| `testMultiOutputInstrument` | a multi-output instrument (Ng Test Drums) gets a strip per output |
| `testSampler` | the sampler plays one-shots and drum kits |
| `testLibrary` | the library: sample folders, finding samples, and presets |

**Audio channels and export**

| Test | Checks |
| --- | --- |
| `testAudioTracks` | audio channels play imported files |
| `testAudioClipEditing` | audio clips have gain and fades, and can follow the tempo |
| `testSampleEditing` | audio clips change pitch, stretch, resample, reverse and normalize, and their tempo is detected |
| `testExport` | `export_audio` writes the song, the loop, MP3s and stems |

**Notes, sounds, analysis and automation**

| Test | Checks |
| --- | --- |
| `testNoteShorthand` | note shorthand |
| `testSynthSounds` | synth sounds |
| `testLoudnessAndHistory` | loudness, `analyze_mix` and history |
| `testInstrumentParameters` | instrument parameters |
| `testAutomation` | automation |
| `testAutosaveAndRecovery` | autosave and crash recovery |

**Batches, state, tags and AI access**

| Test | Checks |
| --- | --- |
| `testBatchReferences` | a batch can use earlier results (`"$0.channel"`) |
| `testStateViews` | `get_state` can return less |
| `testTagsAndStripRouting` | tags create buses and route to them |
| `testCommandLog` | the command log |
| `testBridgeAndMcpServer` | the bridge and the MCP server: the connection file and token, refusing requests without the token, and calls from another thread |

**End to end**

| Test | Checks |
| --- | --- |
| `testRendersAudio` | a synth channel renders sound through its strip, and pulling the strip's fader down silences it |
| `testRecordingAudio` | audio channels record from an audio input |

The test functions are defined in a different order in the file than they run; search for
`void testName` to find one.

## The test plugins

Three tiny VST3 plugins give the tests something real to scan, load, save and render, without
depending on whatever plugins a machine has installed. All three are built with JUCE's
`juce_add_plugin` (`FORMATS VST3`, manufacturer code `NgAu`, company *Ng Audio*, version
`${PROJECT_VERSION}`, `VST3_AUTO_MANIFEST FALSE`) and only when `BUILD_TESTING` is on. They aren't
installed into the system's plugin folders; the tests scan their build folders directly.

| Plugin | Target | Code | What it does | Used by |
| --- | --- | --- | --- | --- |
| **Ng Test Synth** (`tests/TestSynth/TestSynth.cpp`) | `NgTestSynth_VST3` | `NgTs` | Instrument, one stereo output. A single sine voice at the played note's pitch, level = velocity x 0.25, with a **Volume** parameter (0-1) saved in its state. Has a generic editor. | plugin scanning, plugin instruments, effects, multi-output, library/presets |
| **Ng Test Gain** (`tests/TestGain/TestGain.cpp`) | `NgTestGain_VST3` | `NgTg` | Effect, stereo in and out. Multiplies its input by a **Gain** parameter (0-1) saved in its state. Has a generic editor. | effects on strips, library/presets |
| **Ng Test Drums** (`tests/TestDrums/TestDrums.cpp`) | `NgTestDrums_VST3` | `NgTd` | Instrument with **two stereo outputs**, "Kick" and "Snare". C1 (MIDI note 36) plays a 110 Hz sine on the first, D1 (38) a 220 Hz sine on the second. No parameters or editor. | multi-output instruments: each output reaches its own mixer strip |

Because the plugins' state is a single float, the tests can check that a plugin's settings survive
saving and reopening a project.

The tests find them through compile definitions, e.g.:

```cpp
juce::File folder (NG_TEST_PLUGIN_FOLDER);
CHECK (folder.getChildFile ("Ng Test Synth.vst3").exists());
f.dispatcher.execute ("scan_plugins", obj ({ { "folders", juce::Array<juce::var> { NG_TEST_PLUGIN_FOLDER } } }));
```

To try one in the app by hand, *Scan for VST3 plugins...* and add
`build/NgTestSynth_artefacts/Debug/VST3` (or the Gain/Drums folders).

## Adding a test

1. **Write the function** in `tests/CommandTests.cpp`, inside the anonymous namespace (before
   `main()`), following the existing ones:

   ```cpp
   void testMyFeature (tracktion::Engine& engine)
   {
       std::cout << "my_command does the thing\n";
       Fixture f (engine);

       auto r = f.dispatcher.execute ("create_track", obj ({ { "name", "Bass" } }));
       CHECK (r.ok);

       r = f.dispatcher.execute ("my_command", obj ({ { "channel", "Bass" }, { "amount", 0.5 } }));
       CHECK (r.ok);
       CHECK (f.state()["channels"][0]["name"].toString() == "Bass");

       // Bad input fails and changes nothing.
       CHECK (! f.dispatcher.execute ("my_command", obj ({ { "channel", "Nope" } })).ok);

       // Sound, if the feature should make (or stop) some.
       CHECK (renderRms (f) > 0.001f);
   }
   ```

   - Make a new `Fixture` in each test; don't share state between tests.
   - Prefer going through `f.dispatcher.execute` (the command layer) over reaching into
     `f.session`, since that's what the app, the console and the MCP server use.
   - Check undo for editing commands (`f.dispatcher.undo()` and the state afterwards), and that a
     failing command leaves the project unchanged.
   - Put files in the temp folder (`juce::TemporaryFile`, or a named folder under
     `juce::File::tempDirectory` that the test deletes first), and put back any app settings the
     test changes, as `testLibrary` does.
   - If the test needs a plugin, `scan_plugins` with `NG_TEST_PLUGIN_FOLDER`,
     `NG_TEST_EFFECT_FOLDER` or `NG_TEST_DRUMS_FOLDER` (clear `engine.getPluginManager().knownPluginList`
     first if the test counts plugins).

2. **Register it** in `main()` with the others:

   ```cpp
   RUN (testMyFeature);
   ```

3. **Build and run it on its own**, then the whole suite:

   ```sh
   cmake --build build --target NgDawTests
   ./build/NgDawTests_artefacts/Debug/NgDawTests =testMyFeature
   ctest --test-dir build -C Debug --output-on-failure
   ```

No CMake change is needed for a new test function: they're all in the one `command_layer` ctest
test.

## Adding a test plugin

If a feature needs a plugin the three above don't cover:

1. Add `tests/TestFoo/TestFoo.cpp` with a small `juce::AudioProcessor` and a
   `createPluginFilter()`, like the existing ones.
2. In `CMakeLists.txt`, inside `if (BUILD_TESTING)`, copy one of the `juce_add_plugin` blocks with
   a new target name, `PRODUCT_NAME`, `BUNDLE_ID` and a **unique** `PLUGIN_CODE`, then the matching
   `target_sources`, `target_compile_definitions` and `target_link_libraries` lines.
3. Add `NgTestFoo_VST3` to `add_dependencies(NgDawTests ...)`.
4. Add a folder definition to `target_compile_definitions(NgDawTests PRIVATE ...)`:
   `NG_TEST_FOO_FOLDER="${CMAKE_CURRENT_BINARY_DIR}/NgTestFoo_artefacts/$<CONFIG>/VST3"`.
5. Scan that folder in your test.

Note that `CMakeLists.txt` changes also trigger the release workflow on pull requests (it builds
the installer), and change CI's dependency cache key; see [release.md](release.md).
