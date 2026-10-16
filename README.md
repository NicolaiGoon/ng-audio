# Ng DAW

A desktop DAW built on [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) and JUCE.
Tracktion does the hard engine work (playback, plugin hosting, the edit model); this project
builds the workflow on top: FL Studio-style flexibility with REAPER/Cubase-style mixer mapping,
and every action available to AI tools through one command layer.

![First window](docs/first-window.png)

## Installing

Download `NgDaw-<version>-Windows-Setup.exe` from the
[releases page](https://github.com/nicolaigoon/ng-audio/releases) and run it. It installs Ng DAW
and the `NgDawMcp` server (for Claude) into Program Files, adds a Start menu entry, and can make
`.ngdaw` projects open in Ng DAW when double-clicked. Nothing else needs installing. On macOS and
Linux, build it from source (see *Building*).

## What's here (v1.0)

- **A window that plays audio.** It opens with one synth (Tracktion's built-in 4OSC) playing a
  four-on-the-floor pattern. *Add instrument*, *Play*, *Undo* and *Redo* all go through the command layer.
  In the channel rack and mixer, double-click a name to rename it, click the colour chip to
  recolour, pick an instrument, or press × to delete.
- **Project files.** *New*, *Open*, *Save* and *Save as* (Ctrl+N, Ctrl+O, Ctrl+S,
  Ctrl+Shift+S) work with `.ngdaw` files, which hold the Tracktion Edit as XML. The title bar
  shows the project name with a `*` while there are unsaved changes, and New, Open and closing
  the app ask whether to save them first.
- **A timeline.** One lane per channel shows its clips (with a small picture of their notes)
  against a bar ruler, with the loop region and the playhead. Click in the ruler to move the
  playhead, or drag in it to draw a new loop. Ctrl + mouse wheel (or the wheel over the ruler)
  zooms in time, Alt + wheel makes the lanes taller or shorter, Shift + wheel scrolls through
  time, and the zoom buttons in the timeline's header do the same. Ctrl + left click fits the whole song
  in view, as in FL Studio. Drag a lane's bottom edge (in
  the names on the left) to size just that lane; double-click the edge to reset it. The view
  glides along with the playhead while playing, and an automated value's dot rides its line.
- **Clip editing, FL Studio style.** Click a clip to pick it, then click anywhere in its lane to
  place a copy there, or drag along the lane to paint copies one after another. Right-click a
  clip to delete it, or right-drag across clips to delete them all (one undo step). Shift+click
  an empty spot makes a new one-bar clip; the ▾ on a clip, or Shift+right-click, opens its menu
  (split, duplicate, delete, open in the piano roll). Drag a clip to move it (onto another
  channel's lane too) and drag its edges to trim it. Del deletes the selected clip, Ctrl+D
  duplicates it and S splits it at the playhead. Drags snap to the beat (16ths when zoomed in); hold Alt to place freely. A step
  pattern can be copied along the timeline, and every copy follows the channel's steps; drag a
  copy's edge to resize it: making it longer adds empty steps to the channel's step row, as in
  FL Studio, and making it shorter cuts off the steps past its end.
- **A piano roll.** Double-click a clip (or select it and press Enter) to edit its notes in
  place of the rack and mixer. Click to draw a note, or hold and drag to paint more along the
  way, as on the timeline; each is as long as the last note drawn or resized. Drag a note to
  move it and its right edge to resize it. Right-click deletes a note and right-dragging deletes
  every note it passes over (one undo step); Del deletes the selection. Ctrl+click and Ctrl+drag
  select several, Ctrl+A selects all, and the arrow keys move the selection (Shift + up/down
  for an octave). Drawing, clicking or moving a note, or clicking the keys on the left, plays
  it so you hear what you're placing. Ctrl + wheel zooms in time and Alt + wheel sizes the rows.
  Notes snap to the grid picked in Snap (1/4 to 1/32); hold Alt to place freely.
  Under the notes, the velocity lane shows how hard each one plays: click or drag across it to
  set the velocity of the notes there (only the selected ones, if any are). Quantize (or Q)
  moves the selected notes, or all of them, onto the grid. Each edit is one undo step.
- **A step sequencer.** Each channel has a row of 16 steps (16th notes) under its name; click a
  step to turn it on or off, or drag across steps to paint them (right-click clears). The − and + beside the row take away
  or add a bar of steps (4 to 64). The step under the playhead is outlined while playing.
- **A transport** with Play, back to the start (|< or Home), tempo (type it, click the arrows
  or drag), time signature, the song position (bar.beat.sixteenth), a Loop switch and a
  Metronome that clicks on every beat, louder on the bar. New projects loop the whole project,
  rounded up to whole bars. The loop region shows in the timeline ruler: drag in the ruler to draw a
  new one, drag its edges to change it, or right-click to loop a bar or the whole project.
- **MIDI input.** Arm a channel with its ● button in the channel rack, and any MIDI keyboard
  plays it (one channel is armed at a time). No keyboard? The typing keyboard (the keys button in
  the top bar, or Ctrl+K) plays the armed channel from the computer keyboard as in FL Studio: the
  bottom row Z S X D C V G B H N J M is one octave from C4 and Q 2 W 3 E R 5 T... the next; it
  arms the first instrument if none is. While it's on, those letters play notes instead of their
  shortcuts. The piano button (Ctrl+Shift+K) shows an on-screen keyboard to play with the mouse. Rec records what you play into
  the armed channel from the playhead; Rec again or Stop finishes, and the recording becomes a
  clip in one undo step.
- **Keyboard shortcuts.** Space plays and stops, Ctrl+Z undoes, Ctrl+Y (or Ctrl+Shift+Z)
  redoes, R records, C turns the metronome on or off, Ctrl+L looping, Ctrl+T adds an
  instrument, Ctrl+K turns the typing keyboard on or off and Ctrl+Shift+K shows the piano. On the timeline, Ctrl+C and Ctrl+V copy a clip and
  paste it at the playhead, the left and right arrows move it by a beat (Shift: a bar), up and
  down move it to the next channel, and Esc deselects. In the piano roll, Ctrl+C, Ctrl+X and
  Ctrl+V copy, cut and paste notes at the playhead, Ctrl+D duplicates them, and Esc closes it.
  F1 or the ? button lists them all.
- **A layout you can arrange.** The top bar holds the menus (File, Edit, View, Options, Help),
  the transport and the buttons for settings, export, history and the browser. The timeline,
  channel rack, mixer and piano roll are panels: drag the bars between them to give each more
  room (double-click a bar to reset it), and click the window button in a panel's header (or
  double-click the header) to detach it into a window of its own, e.g. the mixer on a second
  screen. Closing that window puts the panel back. The layout is remembered; *View > Reset the
  layout* puts everything back. Knobs and faders follow automation live while the song plays,
  and an animated splash shows the app's progress while it starts.
- **Plugins.** *Plugins* opens the list of VST3 plugins found on this computer. *Scan for VST3
  plugins...* asks which folders to look in (the usual places are filled in) and adds what it
  finds; right-click a plugin to remove it. The list is saved with the app's settings, and a
  plugin that crashes a scan is skipped the next time. Scanned instruments appear in every
  channel's instrument menu; *Edit* next to it opens the plugin's own window. The plugin and its
  settings are saved with the project. A project that uses a plugin this computer doesn't have
  still opens, with the plugin marked missing.
- **A mixer.** Each strip has a fader, pan, mute, solo and a level meter, with a master strip
  on the right. A whole fader drag is one undo step; mute and solo aren't undo steps, as in
  most DAWs.
- **Insert effects.** *+ FX* on any strip, master included, adds an EQ, compressor, reverb or
  delay, or a scanned VST3 effect. Effects run top to bottom, before the fader. Click one to open
  its window (the plugin's own, or sliders for a built-in effect); right-click it to bypass, move
  or remove it. Effects and their settings are saved with the project.
- **Buses and sends.** *Add bus* (above the mixer) adds a bus: a strip other strips send to, such
  as a shared reverb. *+ Send* on a strip sends it to a bus (or makes a new one); drag the send's
  bar to set its level, and right-click it to make it pre-fader or remove it. Sends that would
  loop back into themselves are refused. Channels can also be routed straight to a bus.
- **Multi-output instruments.** For a plugin with more than one stereo output (a drum plugin with
  separate kick, snare and hats outputs, say), *Outs* in the channel rack gives each output its own
  mixer strip, shown right after the instrument's strip, which sums them. Click *Outs* again to go
  back to one stereo output. When the mixer is short (with the console open), it hides the sends
  and effects to keep room for the faders.
- **A sampler.** Pick *Sampler* as a channel's instrument and click *Edit* to open it. Drop audio
  files on it (or use *Add samples*) and each becomes a one-shot on the next free key from C1, as a
  drum kit wants; drop a folder (or use *Load kit*) to load a whole kit, with samples named kick,
  snare, clap, hat, open hat, tom, crash or ride put on their General MIDI keys (kick C1, snare D1,
  closed hat F#1...). Each sample has its key, level and one-shot switch (off stops it when the
  note ends); `set_sample` can also spread a sample across a range of keys to play it pitched.
  The project saves where each sample file is, and marks one that has gone missing.
- **A browser.** The panel on the left (*Browser*, or Ctrl+B) lists instruments, effects,
  presets and your sample folders (*+ Folder* adds one), with a search box that finds samples by
  name. Drag an instrument, instrument preset, sample or kit folder onto the channel rack for a new
  channel (or double-click it); drop an instrument or preset on a channel to swap its instrument,
  and samples or a folder on a sampler channel to add them. Drag an effect or effect preset onto a
  mixer strip. Audio files and folders can also be dropped in from the desktop. Right-click an
  effect, or a channel's name, to save it as a preset; presets work in every project.
- **Audio channels.** Drop WAV, AIFF, FLAC, Ogg or MP3 files on the timeline (from the browser
  or the desktop) and each gets an audio channel of its own, named after the file, with a mixer
  strip like any channel; drop them on an audio channel's lane to add them there, one after
  another. *+ Audio* above the timeline (or Ctrl+I) imports files at the playhead or adds an
  empty audio channel, and double-clicking an empty spot on an audio lane picks files for it.
  Audio clips show their waveform and move, trim, split and copy like MIDI clips, but can't be
  trimmed past the end of their file. Audio and MIDI clips each stay on their own kind of channel.
  The project saves where each file is, and marks one that has gone missing.
- **Recording audio.** Pick an audio channel's input in the channel rack (the inputs are the
  ones the device in *Settings* has), arm it with its ● button (several audio channels can
  be armed at once, alongside one instrument channel for MIDI) and press *Rec* (or R). *Stop* turns
  each take into a clip on its channel, as one undo step. *Mon auto* plays the input through the
  channel while it's armed; *Mon on* always and *Mon off* never. Takes are saved as WAV files in a
  *Recordings* folder beside the project (or in Music/Ng DAW/Recordings before it's saved).
- **Editing audio clips.** Select an audio clip and drag the small handles in its top corners to
  fade it in or out. Right-click it for its *Gain* (+6 to -12 dB; the waveform grows or shrinks
  with it), *Follow tempo*, which time-stretches the clip so it keeps its length in beats when the
  tempo changes, and *Remove fades*.
- **Exporting.** *Export* above the timeline (or Ctrl+E) saves the whole song (to the end of the
  last clip, plus two seconds for tails) or the loop region as a WAV (16-bit, 24-bit or 32-bit
  float) or an MP3, at 44.1 to 96 kHz. Tick *stems* for one file per mixer strip instead, all the
  same length so they line up in another app: each strip's channels through its own effects and
  fader, and each bus with what is sent to it, so together they add up to the mix (before the
  master). MP3 needs LAME or FFmpeg installed (on the PATH, or next to Ng DAW); without one,
  the MP3 choice says so.
- **Automation.** Click the **A** in a channel's header on the timeline to show an automation
  lane under it, for its strip's volume to start with. Click in the lane to add a point, drag a
  point to move it, and double-click or right-click it to remove it. Right-click the lane to
  automate pan or any parameter of the strip's effects instead, to clear the points, or to hide
  the lane. Automated parameters follow their points when the song plays and when it's exported.
- **Autosave and crash recovery.** While a project has unsaved changes, a copy of them is saved
  every minute (*Settings* > *General* sets how often, or turns it off). If Ng DAW closes
  without saving (a crash, a power cut), the next start offers to recover them; the recovered
  project opens unsaved, so you can look before you save over the original.
- **Settings** (Ctrl+,): the audio and MIDI devices, sample rate and buffer size; the colour
  theme; the project folder new projects, recordings and exports go to (Music/Ng DAW unless
  you choose another); and how often to autosave.
- **History** (Ctrl+H): every change since the project was opened, as a list. Click a step to
  undo (or redo) back to just after it.
- **CPU meter.** *CPU* above the timeline shows how hard the audio engine is working. It turns
  amber, then red, when it's near the limit or the audio drops out, and its tooltip shows the
  device, buffer size and latency, and which buffer size to try instead.
- **Linked-by-default mixer strips.** Adding an instrument creates a channel *and* a mixer strip
  with the same name and colour, already routed. Pick a different strip in the channel rack to
  detach it; several channels can share one strip; pick its own strip again to re-link it.
- **A command layer.** Every change is a named command with JSON parameters:

  | Command | What it does |
  | --- | --- |
  | `create_track` | Adds an instrument channel (or, with instrument `audio`, an audio channel) plus its linked mixer strip |
  | `import_audio` | Puts an audio file on an audio channel as a clip, or on a new audio channel named after it |
  | `route` | Sends a channel to a strip (or `master`); detaches it if it isn't its own strip |
  | `rename_track` | Renames a channel or strip; a linked pair is renamed together |
  | `set_track_colour` | Recolours a channel or strip; a linked pair changes together |
  | `delete_track` | Deletes a channel (and its own strip, unless shared) or a strip (its inputs go to master) |
  | `set_instrument` | Swaps a channel's instrument: `4osc`, `sampler`, `none`, or a scanned plugin by id or name |
  | `set_mixer_strip` | Sets a strip's (or `master`'s) fader, pan, mute and solo |
  | `set_tempo` / `set_time_signature` | Project tempo (clips keep their beats) and time signature |
  | `set_loop` | Loop on/off and the loop region in beats (default: the whole project, in whole bars) |
  | `set_steps` | Sets or toggles a channel's step pattern (`steps`, `step`, `length`, `pitch`) |
  | `add_midi_clip` | Adds a MIDI clip with notes to a channel |
  | `move_clip` / `resize_clip` / `split_clip` / `duplicate_clip` / `delete_clip` | Clip editing, by clip id from `get_state` |
  | `get_notes` / `set_notes` | Reads or replaces a MIDI clip's notes (beats from the clip's start) |
  | `quantize_notes` | Moves a clip's notes (or some of them) onto a grid, optionally with their ends |
  | `play` / `stop` / `set_position` | Transport; `set_position` moves the playhead to a beat |
  | `set_metronome` | Turns the metronome on or off |
  | `set_audio_clip` | Sets an audio clip's gain, fade in and out, and whether it follows the tempo (time-stretched from its source tempo) |
  | `export_audio` | Exports the song or the loop region as a WAV or MP3 file, or as one file per mixer strip (stems) |
  | `set_audio_input` | Sets the audio input an audio channel records from, and how it's monitored |
  | `arm_channel` / `record` | Arms a channel for MIDI input (one at a time) or audio input (any number), and records into them until `stop` |
  | `new_project` / `open_project` / `save_project` | Project files; New and Open refuse to drop unsaved changes unless `discard_changes` is true |
  | `scan_plugins` / `list_plugins` | Scans folders for VST3 plugins, and lists the ones found with the ids used to load them |
  | `show_plugin_window` | Opens the editor window of a channel's plugin instrument or sampler |
  | `add_effect` / `remove_effect` / `move_effect` | Insert effects on a strip or `master`: `eq`, `compressor`, `reverb`, `delay`, or a scanned plugin |
  | `set_effect` / `get_effect` | Bypass an effect and set its parameters; list them with their ranges |
  | `show_effect_window` | Opens an effect's window |
  | `create_bus` / `set_send` / `remove_send` | Adds a bus; sends a strip to it with a level, pre- or post-fader |
  | `route_strip` | Plays a mixer strip into another strip (a group or bus) or into `master` |
  | `tag_channel` | Tags a channel `drums`, `bass`, `vox` or anything else; the tag's bus ("Drums Bus") is created the first time, and the channel's strip plays into it. `create_track` takes a `tag` too |
  | `set_multi_output` | Gives each stereo output of a channel's plugin instrument its own strip, or goes back to one |
  | `add_sample` / `set_sample` / `remove_sample` | A sampler's samples: the file, its key (or range of keys), level, pan and one-shot |
  | `load_drum_kit` | Loads a folder of samples into a sampler as a drum kit, on General MIDI keys where the names say what they are |
  | `set_automation` / `get_automation` | Automates a strip's volume (dB) or pan, or an effect parameter, with points in beats; `[]` removes it |
  | `get_library` / `set_library` | The browser's sample folders and preset folder (app settings, not part of the project) |
  | `find_samples` | Finds audio files in the sample folders by words in their names and folders |
  | `save_preset` / `list_presets` / `load_preset` | Saves an instrument's or effect's settings as a preset, lists presets, and loads one onto a channel or strip |
  | `get_state` | The whole project as JSON (channels with their clips in beats, strips, transport), so an AI can "see" the session. Or less: one `channel`, `strip` or `clip` (with its notes), or a `view` (`summary`, `mixer`, `transport`) |

  Each editing command is one undo step, a batch of commands is one undo step, and a failing command
  or batch is rolled back. In a batch, a param like `"$0.channel"` is that field of the first
  command's result, so one batch can create a track and then fill it. The command console (tick
  *Console* in the toolbar) takes the same JSON the MCP server sends, for example:

  ```json
  {"command": "route", "params": {"channel": "Synth 2", "strip": "Synth 1"}}
  {"batch": "Two synths", "commands": [
    {"command": "create_track", "params": {"name": "Bass"}},
    {"command": "create_track", "params": {"name": "Lead"}}]}
  ```

- **AI chat** (tick *AI chat* above the timeline, or Ctrl+J): a chat down the right of the window.
  Ask for a change in plain words ("add a bass line that follows the chords and put it on a bus
  with compression") and the AI makes it with the same commands as everything else, so each
  change goes in the command log and the undo history. Its replies, and each command it ran, show
  in the chat; *Stop* stops it and *New chat* starts over. Pick the provider in *Settings* > *AI*:
  - **Claude** (Anthropic), **OpenAI**, **Google Gemini** or **OpenRouter**, with your own API key
    for that service (kept in this computer's Ng DAW settings, or read from `ANTHROPIC_API_KEY`,
    `OPENAI_API_KEY`, `GEMINI_API_KEY` or `OPENROUTER_API_KEY`). The service's charges apply to
    your account.
  - **Ollama** or **LM Studio** on this computer, with a model that supports tools; nothing leaves
    the computer.
  - **Other**: any service with an OpenAI-style chat completions API, at the address you give.

  *Load models* lists the provider's models. Each provider keeps its own key and model.
- **Claude Desktop and Claude Code:** `NgDawMcp` is an MCP server, so Claude (Claude Code, Claude Desktop or
  any MCP client) can use every command above on the project that's open in Ng DAW, plus `batch`,
  `undo` and `redo`. Ask it, say, to "add a bass line following the chords and put it on a bass bus
  with compression", and it happens in the open project as one undo step.
  - What Claude did shows in the **AI command log** beside the console (it opens by itself the
    first time). Each entry has **Undo**, which takes back that whole command or batch.
  - The app only listens on 127.0.0.1. It writes the port and a secret token to `bridge.json` in
    the user's app data folder (`~/.config/Ng DAW` on Linux, `~/Library/Application Support/Ng DAW`
    on macOS, `%APPDATA%\Ng DAW` on Windows), readable only by you, and refuses requests without
    the token. `NG_DAW_BRIDGE_FILE` sets another path for both ends.

  *Settings* > *AI* shows the exact command and settings for this computer, with Copy buttons.
  To add it to Claude Code (with Ng DAW open):

  ```sh
  claude mcp add ng-daw -- /path/to/build/NgDawMcp_artefacts/Debug/NgDawMcp
  ```

  With the Windows installer, the server is `C:\Program Files\Ng DAW\NgDawMcp.exe`.

  Or to Claude Desktop, in its `claude_desktop_config.json`:

  ```json
  {"mcpServers": {"ng-daw": {"command": "/path/to/build/NgDawMcp_artefacts/Debug/NgDawMcp"}}}
  ```

## Layout

```
src/core/Session.*            the open project: Tracktion Edit + channel/strip model
src/core/CommandRegistry.*    command specs (name, description, JSON schema, handler)
src/core/CommandDispatcher.*  runs commands, batches, JSON requests, undo/redo
src/core/ProjectCommands.cpp  create_track, route, set_steps, add_midi_clip, play, stop, projects, get_state
src/core/PluginCommands.cpp   scan_plugins, list_plugins
src/core/EffectCommands.cpp   insert effects on mixer strips
src/core/BusCommands.cpp      buses and sends
src/core/OutputCommands.cpp   multi-output instruments
src/core/SamplerCommands.cpp  the sampler's samples and drum kits
src/core/LibraryCommands.cpp  sample folders, finding samples, and presets
src/core/AudioCommands.cpp    importing audio files onto audio channels, editing audio clips, and audio inputs
src/core/ExportCommands.cpp   exporting the song, the loop region and stems to WAV or MP3
src/core/AutomationCommands.cpp  automating volume, pan and effect parameters
src/core/AiChat.*             the AI chat's engine: Claude and OpenAI-style APIs, and running the tools the model asks for
src/core/Autosave.*           autosaving unsaved changes, and finding and recovering them after a crash
src/core/Bridge.*             the local connection other programs (the MCP server) use to run commands in the app
src/core/McpServer.*          the MCP protocol: tools/list and tools/call, passed to the app through the bridge
src/mcp/McpMain.cpp           NgDawMcp, the MCP server program (stdio)
src/app/                      the JUCE window (timeline, channel rack, mixer, transport, console)
src/app/MixerView.*           mixer strips: fader, pan, effects, sends, mute/solo, meters, master
src/app/PluginWindow.*        plugin editor windows, and sliders for built-in effects
src/app/SamplerEditor.*       the sampler's window: its samples, and dropping files and kits on it
src/app/BrowserPanel.*        the browser, and what dropping its items on the rack, channels and strips does
src/app/ExportPanel.*         the Export dialog
src/app/PreferencesPanel.*    the Settings window, and the CPU meter's buffer-size advice
src/app/HistoryPanel.*        the undo history
src/app/ChatPanel.*           the AI chat panel
src/app/AppSettings.h         the app's preferences: theme, project folder, autosave
src/app/CommandLogPanel.*     the AI command log, with undo for each entry
src/app/TimelineView.*        the arrangement timeline: lanes, clips and waveforms, ruler, playhead, zoom, audio drops
src/app/PianoRoll.*           the piano roll: keyboard, note grid, drawing and editing notes
tests/CommandTests.cpp        headless tests, including an offline render that checks for sound
tests/TestSynth, TestGain,    tiny VST3 plugins the tests load: an instrument, an effect
  TestDrums                   and a two-output instrument
```

`ng_core` is a static library holding JUCE, Tracktion and the command layer, so the app, the
tests and the MCP server all share it. `CommandRegistry::describe()` returns the commands in MCP
`tools/list` shape (name, description, inputSchema). The MCP server sends each tool call to the
running app over the bridge, where the app's own `CommandDispatcher` runs it.

## Building

Needs CMake 3.22+, a C++20 compiler, and git. CMake downloads Tracktion Engine v3.2.0 and the
JUCE commit it pins on first configure (a few hundred MB; the first build takes a while).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The app ends up in `build/NgDaw_artefacts/Debug/`, and the MCP server in `build/NgDawMcp_artefacts/Debug/`.

GitHub Actions runs the same build and tests on Windows (MSVC) and Linux (GCC) for every pull
request and every push to `main` (`.github/workflows/ci.yml`).

### Releases

`.github/workflows/release.yml` builds a Release copy of the app and the MCP server on Windows
and packs them into an installer with Inno Setup (`packaging/windows/NgDaw.iss`), then installs
it silently, checks it runs and uninstalls it. Pull requests that change the packaging get the
installer as a build artifact. To publish a release, set the version in `CMakeLists.txt`
(`project(NgDaw VERSION ...)`) and push a matching tag:

```sh
git tag v1.0.0 && git push origin v1.0.0
```

The workflow then creates the GitHub release with the installer attached.

- **macOS:** Xcode command line tools. `-G Xcode` works too.
- **Windows:** Visual Studio 2022 with the C++ workload. `-G "Visual Studio 17 2022"`.
- **Linux (Debian/Ubuntu):** (libcurl is for the AI chat's HTTPS requests)
  `sudo apt install libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libcurl4-openssl-dev`

To build against existing checkouts instead of downloading, pass
`-DFETCHCONTENT_SOURCE_DIR_TRACKTION=/path/to/tracktion_engine -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`.

## Licensing

Tracktion Engine and JUCE are each available under the GPLv3 or a commercial licence.
Releasing this app closed-source needs commercial licences for both.

## Next steps

- CLAP support
