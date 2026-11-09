# Project files, autosave and settings

This page describes what Ng DAW writes to disk: the `.ngdaw` project file, the autosave and
crash-recovery files, the bridge connection file, and the app's settings. See
[architecture.md](architecture.md) for how the pieces fit together.

## Summary of locations

`<AppData>` below is JUCE's `userApplicationDataDirectory`:

| OS | `<AppData>` |
| --- | --- |
| Windows | `%APPDATA%` (e.g. `C:\Users\<you>\AppData\Roaming`) |
| macOS | `~/Library` |
| Linux | `~/.config` |

| What | Where | Written by |
| --- | --- | --- |
| Projects | wherever the user saves them; new ones default to `Music/Ng DAW` | `Session::saveProject` |
| Recordings | `Recordings/` beside the project file, or `<default folder>/Recordings` before the first save | `Session::getRecordingFolder` |
| Presets (`.ngpreset`) | `Music/Ng DAW/Presets/<plugin name>/` unless `set_library` sets another folder | `src/core/LibraryCommands.cpp` |
| Autosaves | `<AppData>/Ng DAW/Autosave/` (macOS: `~/Library/Application Support/Ng DAW/Autosave/`) | `Autosave::getDefaultFolder` |
| Bridge connection file | `<AppData>/Ng DAW/bridge.json` (macOS: `~/Library/Application Support/Ng DAW/bridge.json`), or `$NG_DAW_BRIDGE_FILE` | `bridge::getConnectionFile` |
| Settings | `<AppData>/Ng DAW/Settings.xml` (macOS: `~/Library/Ng DAW/Settings.xml`, see below) | Tracktion's `PropertyStorage` |

`Music` is JUCE's `userMusicDirectory` (e.g. `C:\Users\<you>\Music`, `~/Music`).

## The `.ngdaw` project file

A `.ngdaw` file is a **Tracktion Edit saved as XML**: the root element is `<EDIT>`, exactly as
Tracktion Engine v3.2.0 writes it. The extension is defined once, as
`ng::projectFileExtension` in `src/core/Session.h`.

### Saving

`Session::saveProject()` (`src/core/Session.cpp`):

1. Uses the given file (Save As) or the project's own file (Save); fails if the project has never
   been saved and no path is given, or if the path is a folder.
2. Creates the parent folder if needed.
3. Writes with `te::EditFileOperations (*edit).writeToFile (...)` into a `juce::TemporaryFile`
   beside the target, then moves it over the target (`overwriteTargetFileWithTemporary`), so a
   failed save never leaves a half-written project.
4. Makes that file the project's file and marks the project saved.

The `save_project` command wraps this; the app's Save and Save As run that command.

### Opening

`Session::openProject()` refuses a file that doesn't exist or whose root element isn't `EDIT`
(Tracktion itself would silently give an empty project for an unreadable file), then loads it
with `te::loadEditFromFile`. The new Edit then goes through `Session::adoptEdit()`, which also
does a few things to the loaded project:

- adds a level meter plugin to the master plugin list if there isn't one;
- writes every built-in effect's settings into the project, defaults included
  (`Session::storeEffectSettings`), so undo always has a value to go back to;
- removes racks no plugin uses (left behind by multi-output instruments that were turned off);
- switches every audio clip to real-time stretching (`Session::useRealTimeStretching`, which sets
  `proxyAllowed="0"` on the clip), including clips in older projects;
- clears the undo history and marks the project saved.

### What's in it

Everything Tracktion keeps in an Edit: tempo and time signature, tracks, clips and MIDI notes,
plugins and their state, automation, sends and aux returns, racks, the transport's state, and so
on. Audio isn't embedded: audio clips and sampler sounds refer to their files (Tracktion's
source file references). Paths may be stored relative to the project, which is why
`Session::recoverProject` resolves them from the original project's folder.

On top of that, Ng DAW stores its own model as extra properties and one extra element, all with
an `ng` / `NG` prefix. They live in the Edit's `ValueTree`, so Tracktion saves and loads them
with everything else. Most are written through the Edit's `UndoManager` and so are part of the
undo history; the ones marked "no" are not.

#### On tracks (`<TRACK>` elements)

| Property | On | Meaning | Undoable | Where |
| --- | --- | --- | --- | --- |
| `ngRole` | every Ng track | `"channel"` (instrument or audio channel) or `"strip"` (mixer strip). Tracks without it are ignored by Ng's model. | yes | `src/core/Session.cpp` |
| `ngOwnStrip` | channel | item id of the strip created with the channel (the link) | yes | `Session::setOwnStrip` |
| `ngAudio` | channel | `true` for an audio channel (audio clips, no instrument) | yes | `Session::setAudioChannel` |
| `ngInput` | audio channel | the audio input it records from (`""` for none) | yes | `Session::setAudioInput` |
| `ngMonitor` | audio channel | monitoring mode: `auto`, `on` or `off` | yes | `Session::setMonitorMode` |
| `ngSteps` | channel | pattern 1's step row: one character per 16th, `x` on, `.` off | yes | `Session::setSteps` |
| `ngStepPitch` | channel | the MIDI note pattern 1's steps play (default 60) | yes | `Session::setSteps` |
| `ngTag` | channel | lower-case tag such as `drums` | yes | `Session::setTag` |
| `ngTagBus` | strip | the tag whose bus this strip is | yes | `Session::setTagOfBus` |
| `ngOutputOf` | strip | for a multi-output instrument's output strip: the channel's item id | yes | `Session::setOutputOf` |
| `ngOutputPair` | strip | which stereo pair of outputs it plays (0 = outputs 1-2) | yes | `Session::setOutputOf` |
| `ngFolded` | strip | the mixer folds away the strips that play into it | no | `Session::setFolded` |

Which strip a channel plays into is Tracktion's own track output; buses are strips holding a
Tracktion aux return, and sends are Tracktion aux send plugins.

#### On clips

| Property | Meaning | Undoable | Where |
| --- | --- | --- | --- |
| `ngColour` | the clip's own colour (`#rrggbb`); absent means "use the channel's colour" | yes | `Session::setClipColour` |
| `ngPattern` | for a pattern clip: the pattern it plays (its notes are rebuilt from the step rows) | yes | `src/core/ClipCommands.cpp` |
| `ngPatternBeats` | for a pattern clip: its pattern length in beats | yes | `src/core/ClipCommands.cpp` |
| `ngResample` | an audio clip in "resample" stretch mode (speed and pitch change together) | yes | `src/core/AudioCommands.cpp` |

#### On the transport (`<TRANSPORT>`)

| Property | Meaning | Undoable |
| --- | --- | --- |
| `ngLoopStartBeat`, `ngLoopEndBeat` | a custom loop region in beats. Absent: the loop covers the whole project, rounded up to whole bars (`Session::getLoopBeats`). Tracktion's own loop points (in seconds) are moved to match after every command (`Session::updateLoopRange`). | no |

#### Patterns (`<NGPATTERNS>`, a child of `<EDIT>`)

```xml
<NGPATTERNS current="2">
  <PATTERN id="1" name="Pattern 1"/>
  <PATTERN id="2" name="Fill" colour="#e0a03c">
    <ROW channel="1005" steps="x.x.x.x.x.x.xxxx" pitch="38"/>
  </PATTERN>
</NGPATTERNS>
```

- `current` is the pattern the channel rack shows; it is saved but not undoable
  (`Session::setCurrentPattern`).
- `PATTERN` holds `id`, `name` and optionally `colour` (absent: clips show in their channels'
  colours). A missing `name` reads as `Pattern <id>`. Pattern 1 always exists: when it has no
  `PATTERN` element (nothing has renamed it yet) `Session::getPatterns()` adds it as
  "Pattern 1". The whole `NGPATTERNS` element may be absent; it's created the first time a
  pattern is added, named or picked.
- `ROW` holds a channel's steps and pitch for that pattern, keyed by the channel's item id.
  Pattern 1 has no rows here: its rows are the `ngSteps` / `ngStepPitch` properties on the
  channels, where the single step row always lived, so older projects open unchanged.

(The example is illustrative; see `Session.cpp` around `patternsType` for the exact code.)

### What isn't saved

Not part of the project (`src/core/Session.h`):

- the armed MIDI channel and armed audio channels (setup, not the song);
- the app's preferences, plugin list and library folders (see *App settings*);
- the undo history.

The playhead position and the input devices Tracktion records under `<INPUTDEVICES>` *are* in the
file, but `Session::getComparableState()` ignores them (and the Edit's `lastSignificantChange`)
when deciding whether there are unsaved changes.

## Presets (`.ngpreset`)

`save_preset` (`src/core/LibraryCommands.cpp`) writes an XML file whose root is `<NGPRESET>`
with attributes `name`, `plugin` (the plugin id, as `list_plugins` and `get_state` name it),
`pluginName` and `kind` (`instrument` or `effect`), holding the plugin's state element (with its
`id` removed). Files go in `<preset folder>/<plugin name>/<preset name>.ngpreset`.

## Autosave and crash recovery

See `src/core/Autosave.h` / `src/core/Autosave.cpp`, and [architecture.md](architecture.md#autosave)
for when it runs.

Folder: `Autosave/` under the Ng DAW app-data folder (`Autosave::getDefaultFolder()`):

- Windows: `%APPDATA%\Ng DAW\Autosave\`
- macOS: `~/Library/Application Support/Ng DAW/Autosave/`
- Linux: `~/.config/Ng DAW/Autosave/`

Each running copy of the app has a random 16-hex-digit id and writes two files there:

| File | Contents |
| --- | --- |
| `<id>.ngdaw` | the project, written by `Session::writeCopy()`: `edit->flushState()`, then the Edit's state as XML, via a temporary file. Same format as a saved project, but it doesn't count as a save and doesn't change the project's file. |
| `<id>.json` | `{"id": "<id>", "project": "<full path of the project, or empty>", "name": "<project name>", "time": "<ISO 8601>"}` |

While it runs, the copy holds an inter-process lock named `NgDawAutosave_<id>`.

- The files are written only while the project has unsaved changes that haven't been written yet,
  at the interval set in *Settings > General* (`ngAutosaveMinutes`: 0 = off, 1, 2, 5 or 10;
  default 1). Autosave is skipped while recording or exporting.
- They're deleted when the project is saved, a new project is made or another is opened, when the
  changes are gone (e.g. undone back to the saved state), and when the app closes normally.
- On startup, `Autosave::findRecoverable()` looks for `*.json` files whose `.ngdaw` exists and
  whose lock nobody holds (so it wasn't written by a copy that's still running). The newest is
  offered (*Recover*, *Discard* or *Later*); older ones are deleted. A recovered project opens as
  the original project file (or as Untitled if it had never been saved), with unsaved changes.

## Bridge connection file

`bridge.json` (`src/core/Bridge.cpp`) tells `NgDawMcp` how to reach the running app:

```json
{"port": 52345, "token": "<64 hex digits>", "pid": 12345}
```

- Location: `bridge::getConnectionFile()`, i.e. `NG_DAW_BRIDGE_FILE` (resolved against the current
  directory) if set, otherwise `bridge.json` in the Ng DAW app-data folder:
  `%APPDATA%\Ng DAW\bridge.json`, `~/Library/Application Support/Ng DAW/bridge.json` or
  `~/.config/Ng DAW/bridge.json`.
- Written when the app starts listening, with a new token each time. On macOS and Linux the file is
  created empty and set to mode `0600` before the token is written.
- Deleted when the app stops listening, but only if it still holds this copy's token. With several
  copies of the app running, the file points at the one that started last.

See [mcp.md](mcp.md) for the protocol on top of it.

## App settings

App-wide settings are kept in **Tracktion's settings file**, `engine.getPropertyStorage()
.getPropertiesFile()`. With the default `te::PropertyStorage` that is `Settings.xml` (a
`juce::PropertiesFile` stored as XML, saved two seconds after a change) in
`userApplicationDataDirectory/<application name>`. The app creates its engine with the name
`"Ng DAW"` (`src/app/Main.cpp`), so:

- Windows: `%APPDATA%\Ng DAW\Settings.xml`
- Linux: `~/.config/Ng DAW/Settings.xml`
- macOS: `~/Library/Ng DAW/Settings.xml`. Tracktion doesn't add `Application Support` the way
  `Bridge.cpp` and `Autosave.cpp` do, so on macOS the settings live in a different folder from
  the autosaves and the bridge file.

The tests create their engine as `"NgDawTests"` (`tests/CommandTests.cpp`), so they use a
separate settings file.

The same file holds Tracktion's and JUCE's own settings (the audio device setup under
`audiosettings_<device type>`, the scanned plugin list under `knownPluginList` /
`knownPluginList64`, and the folders last scanned under `lastPluginScanPath_<format>`, which
`scan_plugins` and the Plugins window share). Ng DAW's own keys all start with `ng`:

### Preferences (`src/app/AppSettings.h`, namespace `ng::settings`)

| Key | Values | Default |
| --- | --- | --- |
| `ngTheme` | `Dark`, `Midnight`, `Grey` | `Dark` |
| `ngProjectFolder` | absolute path of the default project folder; empty means `Music/Ng DAW` | empty |
| `ngAutosaveMinutes` | `0` (off), `1`, `2`, `5`, `10` | `1` |

### Library (`src/core/LibraryCommands.cpp`, changed with `set_library`)

| Key | Values |
| --- | --- |
| `ngSampleFolders` | the browser's sample folders, one absolute path per line |
| `ngPresetFolder` | the preset folder; empty means `Music/Ng DAW/Presets` |

### Window and layout (`src/app/Main.cpp`, `src/app/MainComponent.cpp`, `src/app/DockPanel.cpp`)

| Key | What |
| --- | --- |
| `ngMainWindow` | the main window's position and maximised state |
| `ngBrowserVisible`, `ngBrowserWidth` | whether the browser is shown, and its width (160-600) |
| `ngTimelineProportion`, `ngRackProportion` | where the splitters are (0.1-0.9) |
| `ngPanelDetached_<id>` | whether a panel is in its own window (`timeline`, `rack`, `mixer`, `pianoroll`, `sampleeditor`) |
| `ngPanelWindow_<id>` | that window's position |
| `ngTypingKeyboard` | the typing keyboard is on (default true) |
| `ngTimelineSnap` | the timeline's snap choice |
| `ngMixerDockBuses` | the mixer's *Dock buses* toggle |
| `ngRollGhosts`, `ngRollKey`, `ngRollScale` | piano roll: ghost notes, key and scale |

These are per-user preferences, not part of any project: they apply to every project and aren't
undone.
