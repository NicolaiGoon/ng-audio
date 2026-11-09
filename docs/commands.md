# Command reference

Every change Ng DAW makes to a project goes through one command layer: the buttons, the
keyboard shortcuts, the command console, and AI clients connected through the MCP server
([mcp.md](mcp.md)) all run the same named commands with JSON parameters. This page lists every
command the app registers, as of this version: **75 commands** in 12 groups. The MCP server
adds three tools of its own (`batch`, `undo` and `redo`), described under
[Batches, undo and redo](#batches-undo-and-redo).

The registrations live in `src/core/*Commands.cpp`. `registerProjectCommands()` in
`ProjectCommands.cpp` registers its own commands and then calls every other group's register
function. Each command is a `CommandSpec` (`src/core/CommandRegistry.h`) with a name, a
description, a JSON Schema for its parameters, an `undoable` flag and a handler.

## Contents

- [How commands work](#how-commands-work)
  - [Request format](#request-format)
  - [Response format](#response-format)
  - [Errors](#errors)
  - [Naming things: ids and names](#naming-things-ids-and-names)
  - [Units and conventions](#units-and-conventions)
  - [Undo semantics](#undo-semantics)
  - [Batches, undo and redo](#batches-undo-and-redo)
  - [The command log](#the-command-log)
- [Project, channels and the mixer strip](#project-channels-and-the-mixer-strip) (14)
- [Clips, notes and step patterns](#clips-notes-and-step-patterns) (18)
- [Transport and recording](#transport-and-recording) (9)
- [Plugins](#plugins) (2)
- [Insert effects and instrument parameters](#insert-effects-and-instrument-parameters) (6)
- [Buses, sends and routing](#buses-sends-and-routing) (7)
- [Automation](#automation) (2)
- [Multi-output instruments](#multi-output-instruments) (1)
- [Sampler](#sampler) (4)
- [Library: sample folders and presets](#library-sample-folders-and-presets) (6)
- [Audio clips and inputs](#audio-clips-and-inputs) (4)
- [Export and mix analysis](#export-and-mix-analysis) (2)
- [Index of all commands](#index-of-all-commands)

---

## How commands work

### Request format

A single command is a JSON object with the command's name and an optional `params` object:

```json
{"command": "set_tempo", "params": {"bpm": 128}}
```

Several commands run as one undo step are a batch, with a label for the undo history:

```json
{"batch": "Add a bass line", "commands": [
  {"command": "create_track", "params": {"name": "Bass", "sound": "Round Bass"}},
  {"command": "add_midi_clip", "params": {"channel": "$0.channel", "length_beats": 16,
     "chords": [{"chord": "Am", "start": 0}, {"chord": "F", "start": 4},
                {"chord": "C", "start": 8}, {"chord": "G", "start": 12}],
     "chord_style": "bass_root_octave", "chord_octave": 2}}
]}
```

These two shapes are what `CommandDispatcher::executeJson()` accepts, and what the in-app
command console (tick *Console* in the toolbar) takes. Over MCP, each command is a tool whose
arguments are the `params` object, and `batch` is a tool taking `label` and `commands` (see
[mcp.md](mcp.md)).

- `params` may be left out or be an object. Anything else (an array, a string) fails with
  `<command>: params must be an object`.
- Parameter names are case sensitive and use `snake_case`.
- Unknown parameters are ignored.
- The published JSON Schemas give each parameter a type and a description, and list the
  required ones. They carry no `enum`, `minimum` or `maximum`: ranges and allowed values are
  checked by the handlers and given here and in each parameter's description.
- Types are read leniently in places. Some parameters typed `string` in the schema also take a
  number (an effect's or sample's position, for example), and integers are read from numbers.
  Where a handler insists on a JSON number or boolean, this page says so.

### Response format

Every command returns either a result or an error:

```json
{"ok": true, "result": {"bpm": 128.0}}
{"ok": false, "error": "set_tempo: bpm must be a number from 20 to 300"}
```

Commands with nothing to report (`play`, `stop`, `record`) succeed with no result value
(`"result"` is `null`; the MCP server shows it as the text `Done.`). A batch's result is an
array holding each command's result, in order.

### Errors

- Errors are plain sentences meant for a person (or a model) to read, and most start with the
  command's name, e.g. `move_clip: no clip with id '1234'`. Most say what would work instead.
- An unknown command fails with `Unknown command: <name>`.
- While an export or `analyze_mix` is rendering, every command except `get_state` fails with
  `<command>: wait until the export is finished`.
- Handlers check their parameters before they change anything wherever they can, so a bad value
  usually leaves the project untouched. When a handler does fail part-way, the dispatcher rolls
  back what it changed (see below).

### Naming things: ids and names

- **Channels** (the rows of the channel rack, which hold the instrument and the clips) and
  **mixer strips** (faders and effects) are named by their id from `get_state` or by their
  name. Names are matched exactly (case sensitive); if two share a name, the first wins, so
  prefer ids once you have them. Ids are numbers written as strings, e.g. `"1012"`.
- `"master"` names the master strip wherever a strip is asked for and the description says so.
- **Clips** are named by id only.
- **Patterns** are named by their integer id (pattern 1 always exists).
- **Effects** on a strip are named by id, by position from 0, or by name or type (`eq`,
  `compressor`, `reverb`, `delay`, or a plugin's name) when only one effect on the strip
  matches.
- **Samples** in a sampler are named by position from 0 or by name.
- **Plugins** are named by the `id` from `list_plugins`, or by name when only one scanned
  plugin has it.

### Units and conventions

- **Times are in beats** (quarter notes at the project tempo) from the start of the project,
  except note times, which are beats from the start of their clip. A bar of 4/4 is 4 beats.
- **Levels are in dB**: faders and sends from -100 to 6 (0 is unity).
- **Pan** runs from -1 (left) to 1 (right).
- **Pitches** are MIDI note numbers 0 to 127 (60 is middle C, C4). `add_midi_clip` and
  `set_notes` also take names such as `"C4"`, `"F#3"` or `"Bb5"`.
- **Colours** are hex strings like `"#3c8ce0"`.
- **Paths** should be absolute. Some commands insist on it; the others resolve a relative path
  against the app's working directory.
- Beat values in results are rounded to a millionth of a beat.

### Undo semantics

The dispatcher (`src/core/CommandDispatcher.cpp`) decides what goes in the undo history:

- **Each successful call of an undoable command is one undo step**, labelled with the
  command's name. The table at the top of each command says whether it is undoable (the
  `undoable` flag of its registration).
- **A failing command is rolled back**: whatever it changed before failing is undone, and no
  undo step is left behind.
- **A batch is one undo step**, labelled with the batch's label, whatever commands it holds.
  If any command in it fails, every command before it is undone too and the batch's error is
  `Batch '<label>' rolled back. <the command's error>`.
- **Commands that aren't undoable** (`undoable: false`) don't open an undo step of their own.
  They are the queries (`get_*`, `list_*`, `find_*`, `analyze_*`) and actions outside the
  edit: the transport (`play`, `stop`, `record`, `set_position`, `set_loop`, `set_metronome`,
  `arm_channel`), windows (`show_plugin_window`, `show_effect_window`), files (`new_project`,
  `open_project`, `save_project`, `export_audio`, `save_preset`) and app settings
  (`scan_plugins`, `list_plugins`, `set_library`).
- Some undoable commands change things that aren't undo steps by design: mute and solo in
  `set_mixer_strip`, and `fold_strip`. These changes stay when the step around them is undone,
  and a batch that fails doesn't put them back either. The same goes for anything a
  non-undoable command did inside a failed batch (starting playback, say).
- `stop` while recording turns the recording into clips as an undo step of its own, labelled
  `Record`.
- `new_project` and `open_project` clear the undo history.
- Undo and redo take back the newest step in the project, whoever made it: a command, a batch,
  or an edit made by hand in the app.
- After a successful undoable command the dispatcher re-sorts the tracks' contents, and after
  every command it refreshes the loop range (a loop over the whole project follows the
  project's length).

### Batches, undo and redo

`CommandDispatcher::executeBatch()` runs a batch. Inside it, a string parameter of the form
`"$<n>.<field>"` is replaced by that field of the result of command `n`, counting from 0, so
later commands can use what earlier ones created:

| Reference | Means |
| --- | --- |
| `"$0"` | The whole result of command 0 |
| `"$0.channel"` | The `channel` field of command 0's result |
| `"$1.clips.0.id"` | Fields go deeper; a number picks from an array |
| `"$$5"` | A literal string starting with `$`: the text `$5` |

References are resolved in nested objects and arrays too. Referring to a command that hasn't
run yet, or to a field its result doesn't have, fails the batch (and rolls it back) with an
error naming the reference.

Over MCP these are three extra tools, which aren't app commands:

| Tool | Arguments | What it does |
| --- | --- | --- |
| `batch` | `label` (string, required), `commands` (array of `{command, params}`, required) | Runs the commands as one undo step, as above. The label shows in the undo history and the command log. If it's empty the server uses `AI changes`. Returns the array of results. |
| `undo` | none | Undoes the newest change in the project. Fails with `There's nothing to undo`. |
| `redo` | none | Redoes the change undo last took back. Fails with `There's nothing to redo`. |

The bridge also accepts `{"undo": true}` and `{"redo": true}` requests directly (see
[mcp.md](mcp.md)).

### The command log

Commands sent from outside the UI (by an AI client through the bridge, or typed in the
console) are logged with who sent them (`Claude`, `Console`, or the MCP client's name). Each
log entry records the command or batch label, the commands a batch ran, whether it succeeded,
and whether it made an undo step. The log's *Undo* button takes back an entry while it is
still the newest step. Queries whose names start with `get_`, `list_` or `find_` aren't
logged.

---

## Project, channels and the mixer strip

Registered in `src/core/ProjectCommands.cpp`.

### create_track

Adds an instrument channel and a mixer strip with the same name and colour, linked to it. With
`instrument: "audio"` it adds an audio channel instead (for audio clips). The new strip plays
into master.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{channel, strip}` (ids), plus `bus` (the tag's bus id) when `tag` was given and isn't empty |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `name` | string | no | Name for the channel and its strip. Default `Instrument <n>` (or `Audio <n>` for an audio channel), where n is the number of channels plus one |
| `colour` | string | no | Hex colour. Default: the next colour in the palette |
| `instrument` | string | no | `"4osc"` (the built-in synth, default), `"sampler"`, `"none"`, `"audio"` (an audio channel), or a scanned plugin instrument's id or name from `list_plugins` |
| `sound` | string | no | Only with `4osc`: a starter sound from `list_sounds` (case-insensitive), e.g. `"Round Bass"`. Default: the synth's plain sound |
| `tag` | string | no | A tag such as `"drums"`, `"bass"` or `"vox"`: the channel's strip then plays into that tag's bus, created if needed (see `tag_channel`) |

Fails if the colour isn't a hex colour, the instrument is unknown or is an effect, several
plugins share the name given, `sound` is given with anything but `4osc`, or a plugin won't load.

```json
{"command": "create_track", "params": {"name": "Lead", "sound": "Square Lead", "tag": "synths"}}
→ {"ok": true, "result": {"channel": "1043", "strip": "1044", "bus": "1046"}}
```

### route

Sends a channel to a mixer strip, or to master. Routing a channel away from its own strip
detaches it; several channels can share one strip. Routing it back to its own strip links it
again.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{channel, output, linked}`: `output` is the strip's id or `"master"`; `linked` is true while the channel plays into its own strip |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `channel` | string | yes | Channel id or name |
| `strip` | string | yes | Mixer strip id or name, or `"master"` |

```json
{"command": "route", "params": {"channel": "Synth 2", "strip": "Synth 1"}}
```

### rename_track

Renames a channel or a mixer strip. While a channel is linked to its strip, both get the new
name.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{id, role, name, colour, linked_to?}`: `role` is `"channel"` or `"strip"`; `linked_to` is the linked partner's id, if any |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `track` | string | yes | Channel or mixer strip id or name (channels are looked up first) |
| `name` | string | yes | The new name; can't be empty or only spaces |

### set_track_colour

Recolours a channel or mixer strip. While a channel is linked to its strip, both change.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{id, role, name, colour, linked_to?}`, as `rename_track` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `track` | string | yes | Channel or mixer strip id or name |
| `colour` | string | yes | Hex colour such as `#3c8ce0` |

### delete_track

Deletes a channel or a mixer strip. A channel takes its own strip with it unless other channels
still play into that strip, and a multi-output channel takes its output strips. Anything that
played into a deleted strip is sent to master, and other strips' sends to a deleted bus are
removed.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{deleted: [ids], rerouted_to_master: [ids]}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `track` | string | yes | Channel or mixer strip id or name |

### set_instrument

Replaces a channel's instrument. Fails if the plugin won't load, leaving the old instrument in
place.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{channel, instrument}`: the instrument's type or plugin id |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `channel` | string | yes | Channel id or name |
| `instrument` | string | yes | `"4osc"`, `"sampler"`, `"none"`, or a scanned plugin instrument's id or name |
| `sound` | string | no | Only with `4osc`: a starter sound from `list_sounds` |

Fails on an audio channel, for `"audio"` (an instrument channel can't become an audio
channel), and while the channel has multiple outputs on (turn them off with
`set_multi_output` first).

```json
{"command": "set_instrument", "params": {"channel": "Bass", "instrument": "4osc", "sound": "Sub Bass"}}
```

### list_sounds

Lists the starter sounds for the built-in 4osc synth, with what each is for. Give one as
`sound` to `create_track` or `set_instrument`, then shape it further with `set_effect` and
`channel`.

| | |
| --- | --- |
| Undoable | No (changes nothing) |
| Parameters | None |
| Returns | `[{name, description}]` |

The sounds are: `Soft Bell Lead`, `Square Lead`, `Round Bass`, `Sub Bass`, `Pluck`,
`Warm Pad`, `Electric Keys` and `Organ`.

### show_plugin_window

Opens the editor window of a channel's plugin instrument or sampler, or brings it to the front.

| | |
| --- | --- |
| Undoable | No |
| Returns | `{channel, shown}`: `shown` is false when there's no screen to show it on |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `channel` | string | yes | Channel id or name |

Fails when the channel has no plugin instrument or sampler (the 4osc synth has no window of its
own; see `show_effect_window` with `channel`), or the plugin isn't loaded.

### set_mixer_strip

Sets a mixer strip's fader, pan, mute and solo. Only the values given change; every value is
checked before anything changes.

| | |
| --- | --- |
| Undoable | Yes, for fader and pan. Mute and solo aren't undo steps, as in most DAWs |
| Returns | `{strip, volume_db, pan, mute, solo}` (`mute` and `solo` are left out for master); `strip` is the id or `"master"` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `strip` | string | yes | Mixer strip id or name, or `"master"` |
| `volume_db` | number | no | Fader level in dB, -100 to 6 (0 is unity) |
| `pan` | number | no | -1 (left) to 1 (right), 0 is centre |
| `mute` | boolean | no | Mute the strip. Not for master |
| `solo` | boolean | no | Solo the strip; other strips go silent while any strip is soloed. Not for master |

```json
{"command": "set_mixer_strip", "params": {"strip": "Drums", "volume_db": -6, "pan": -0.2}}
```

### new_project

Closes the open project and starts an empty one.

| | |
| --- | --- |
| Undoable | No: it clears the undo history |
| Returns | `{name, path}` of the new project (`path` is empty until it's saved) |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `discard_changes` | boolean | no | Lose unsaved changes. Without it, the command refuses while there are unsaved changes |

### open_project

Opens a project file (`.ngdaw`), replacing the open project.

| | |
| --- | --- |
| Undoable | No: it clears the undo history |
| Returns | `{name, path}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `path` | string | yes | Path of the project file |
| `discard_changes` | boolean | no | Lose unsaved changes. Without it, the command refuses while there are unsaved changes |

### save_project

Saves the project. Without `path` it saves to the project's own file (Save); with `path` it
saves there and that becomes the project's file (Save As). The file is written beside the
target first, so a failed save never leaves a half-written project.

| | |
| --- | --- |
| Undoable | No |
| Returns | `{name, path}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `path` | string | no | Where to save. Required for a project that hasn't been saved yet. Can't be a folder |

```json
{"command": "save_project", "params": {"path": "C:\\Users\\me\\Music\\Ng DAW\\beat.ngdaw"}}
```

### get_state

Returns the project as data: its channels (with their clips), mixer strips, routing, patterns
and transport. That can be long, so it can return less.

| | |
| --- | --- |
| Undoable | No (changes nothing; works even during an export) |
| Returns | See below |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `channel` | string | no | Just this channel (id or name), with a `strip` field holding the strip it plays into |
| `strip` | string | no | Just this mixer strip (id or name), or `"master"` |
| `clip` | string | no | Just this clip (id), with `channel` and, for a MIDI clip, its `notes` |
| `view` | string | no | `"full"` (default), `"summary"`, `"mixer"` or `"transport"` |

`channel`, `strip` and `clip` are tried in that order; the first one given wins and `view` is
then ignored.

The full state has these top-level fields:

| Field | Contents |
| --- | --- |
| `project` | `{name, path, unsaved_changes}` |
| `tempo_bpm`, `time_signature` | The tempo, and `{numerator, denominator}` |
| `midi_inputs`, `audio_inputs` | The device's input names (`audio_inputs` are what `set_audio_input` takes) |
| `patterns`, `current_pattern` | Each pattern's `{id, name, colour?, channels}` (the channels with steps in it), and the current pattern's id |
| `channels` | Each channel: `id`, `name`, `colour`, `output`, `linked`, `tag?`, `kind` (`instrument` or `audio`), `instrument`, `instrument_name`, `instrument_missing?`, `plugins`, `automation?` (its instrument's automated parameters), `outputs`, `multi_output`, `samples?` (a sampler's), `pattern` (`{id, steps, pitch}` in the current pattern), `clips`, `armed`, and for audio channels `input` and `monitor` |
| `channels[].clips` | Each clip: `id`, `name`, `start_beat`, `length_beats`, `pattern`, `pattern_id?`, `muted?`, `colour?`, `type` (`midi` or `audio`), `notes` (a MIDI clip's note count), and an audio clip's settings (`file`, `missing?`, `max_end_beat`, `gain_db`, `fade_in_beats`, `fade_out_beats`, `follow_tempo`, `source_bpm?`, `stretch_mode`, `pitch_semitones`, `speed`, `reversed`) |
| `strips` | Each strip: `id`, `name`, `colour`, `inputs`, `strip_inputs`, `tag?`, `output`, `volume_db`, `pan`, `mute`, `solo`, `effects` (`{id, type, name, enabled, missing?}`), `automation`, `bus`, `folded?`, `output_of?` and `output_pair?` (for a multi-output strip), `sends` (`{bus, bus_name, level_db, pre_fader}`) |
| `master` | `{volume_db, pan, effects, automation}` |
| `transport` | `{playing, position_seconds, position_beats, position, looping, metronome, recording, loop_start_beat, loop_end_beat, loop_is_whole_project}`; `position` is bar.beat.sixteenth text |

The views pick fields from it:

- `summary`: `project`, `tempo_bpm`, `time_signature`, a short `transport`
  (`playing, position, position_beats, looping, loop_start_beat, loop_end_beat, recording`),
  `channels` (`id, name, kind, instrument, output, tag, armed`, and `clips` with
  `id, type, start_beat, length_beats, notes`) and `strips`
  (`id, name, output, bus, tag, volume_db, mute, solo`, and `effects` as a list of types).
- `mixer`: `strips` and `master`.
- `transport`: `transport`, `tempo_bpm`, `time_signature` and `project`.

```json
{"command": "get_state", "params": {"view": "summary"}}
{"command": "get_state", "params": {"clip": "1077"}}
```

### get_history

Lists the undo history: the steps undo would take back, newest first (each a command's name or
a batch's label), and the steps redo would put back. Use it to check what changed, or how many
undo steps to go back.

| | |
| --- | --- |
| Undoable | No (changes nothing) |
| Returns | `{undo: [labels], undo_count, redo: [labels], redo_count}`; an unnamed step shows as `(unnamed)` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `limit` | integer | no | At most this many of each list, 1 to 500 (default 20; values outside are clamped) |

---

## Clips, notes and step patterns

Registered in `src/core/ClipCommands.cpp`. Clip ids come from `get_state`. Several clip
commands return a clip description, `{clip, channel, start_beat, length_beats, muted}`,
referred to below as *a clip description*.

**Step patterns** work as in FL Studio. A pattern holds a step row (16th-note steps playing one
pitch) for each channel. Placing a pattern puts a *block* on the timeline: a pattern clip on
every channel with steps in it, all starting together. Moving, copying, resizing or deleting
any clip of a block does the same to the whole block, and the clips are rebuilt from the rows
whenever the steps change. Pattern 1 always exists and starts at the beginning of the project.

### add_midi_clip

Adds a MIDI clip with notes to an instrument channel. Instead of writing every note, chords can
be written out as block chords, arpeggios or a bass line, and `repeat` plays the part again, so
a whole part fits in a few lines. Every note, chord and repeat is checked before the clip is
made.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{clip, notes}`: the new clip's id and how many notes it has |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `channel` | string | yes | Instrument channel id or name (not an audio channel) |
| `start_beat` | number | no | Where the clip starts, in beats (default 0) |
| `length_beats` | number | no | Clip length in beats, above 0 (default 4) |
| `notes` | array | no | Notes as `{pitch, start, length, velocity}`, see [Note shorthand](#note-shorthand) |
| `chords` | array | no | Chords as `{chord, start, length, velocity}` |
| `chord_style` | string | no | How chords play (default `"block"`) |
| `chord_octave` | integer | no | Octave of the chords' roots, -1 to 8 (default 4) |
| `chord_rate` | number | no | Beats between arpeggio or bass notes (default 0.5) |
| `repeat` | object | no | `{times, every_beats}`: play the part again |

In `add_midi_clip` a note with no `pitch` plays middle C (60). Note starts must be 0 or later
and lengths above 0. Notes past the clip's end are kept but don't play.

```json
{"command": "add_midi_clip", "params": {
  "channel": "Keys", "start_beat": 0, "length_beats": 16,
  "chords": [{"chord": "C", "start": 0}, {"chord": "Am", "start": 4},
             {"chord": "F", "start": 8}, {"chord": "G7", "start": 12}],
  "chord_style": "arp_up", "chord_rate": 0.25}}
```

#### Note shorthand

`add_midi_clip` and `set_notes` share this shorthand (`src/core/NoteBuilder.cpp`). Plain
notes and chords can be combined; together they make the part, which `repeat` then copies.
All times are in beats from the clip's start.

- **`notes`**: `[{pitch, start, length, velocity}]`. `pitch` is a MIDI number 0 to 127 or a
  note name: a letter A to G, any number of `#` or `b`, and an octave from -1 (`"C4"` is 60,
  `"F#3"`, `"Bb5"`). `start` defaults to 0, `length` to 1, `velocity` to 100.
- **`chords`**: `[{chord, start, length, velocity}]`. `start` defaults to 0, `length` to 4,
  `velocity` to 100. A chord symbol is a root (`C`, `F#`, `Bb`...) and a quality, optionally
  with a slash bass (`"C/E"`, played below the root). The qualities are:

  | Quality | Intervals | Quality | Intervals |
  | --- | --- | --- | --- |
  | (none), `maj` | major triad | `m`, `min`, `-` | minor triad |
  | `7` | dominant 7th | `m7`, `min7` | minor 7th |
  | `maj7`, `M7` | major 7th | `m7b5` | half-diminished |
  | `dim` | diminished triad | `dim7` | diminished 7th |
  | `aug`, `+` | augmented triad | `sus`, `sus4` | suspended 4th |
  | `sus2` | suspended 2nd | `add9` | major add 9 |
  | `6` | major 6th | `m6` | minor 6th |
  | `9` | dominant 9th | `m9` | minor 9th |
  | `maj9` | major 9th | `5` | power chord (root and fifth) |

- **`chord_style`**: `"block"` (default; the chord held for its length), `"arp_up"`,
  `"arp_down"`, `"arp_updown"`, `"alberti"` (low, high, middle, high), or a bass line:
  `"bass_root"` (the root held), `"bass_root_octave"` (root, octave up, repeating) or
  `"bass_root_fifth"` (root, fifth). Arpeggio and bass notes play one every `chord_rate` beats,
  each 95% of a step long, the last cut short at the chord's end.
- **`chord_octave`**: the octave of the chords' roots, -1 to 8 (default 4, so a C chord starts
  on middle C). 2 suits a bass line.
- **`chord_rate`**: beats between arpeggio or bass notes, at least 1/32 (default 0.5, an 8th
  note).
- **`repeat`**: `{times, every_beats}`. `times` (1 to 256) is how often the part plays in all,
  `every_beats` (above 0) how far apart. `{"times": 4, "every_beats": 8}` plays a two-bar
  phrase four times.

### set_steps

Sets a channel's row in a step pattern. Give the whole row, a new length, or one step (they can
be combined and apply in that order), and optionally the pitch. A row with no steps on has no
clips; one with steps gets a clip in each of the pattern's blocks.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{channel, pattern, steps, pitch}`: `steps` is the row as `x` and `.` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `channel` | string | yes | Instrument channel id or name (not an audio channel) |
| `pattern` | integer | no | Pattern id from `get_state` (default: the current pattern) |
| `steps` | string or array | no | The whole row: `x`/`X`/`1` for on and `.`/`0`/`-` for off (spaces and `\|` are ignored), e.g. `"x...x...x...x..."`; or an array of booleans. 1 to 64 steps |
| `length` | integer | no | Number of steps, 1 to 64 (rows start at 16), keeping the steps that still fit |
| `step` | integer | no | One step to change, from 0 |
| `on` | boolean | no | With `step`: on or off (toggles if left out) |
| `pitch` | integer | no | MIDI note the steps play, 0 to 127 (rows start at 60) |

```json
{"command": "set_steps", "params": {"channel": "Kick", "steps": "x...x...x...x..."}}
{"command": "set_steps", "params": {"channel": "Hat", "step": 6, "on": true}}
```

### move_clip

Moves a clip to a new start beat, and optionally to another channel. A step pattern's clip
only moves in time, and takes its whole block along. Audio clips stay on audio channels and
MIDI clips on instrument channels.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | A clip description |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `clip` | string | yes | Clip id |
| `start_beat` | number | no | New start in beats, 0 or later (default: where it is) |
| `channel` | string | no | Channel id or name to move it to |

### resize_clip

Moves a clip's start and/or end; its notes stay where they are in the song. The start can't go
back past the start of the clip's content, an audio clip can't run past the end of its file
(unless it loops), and a clip can't be shorter than 1/16 beat. A step pattern's block can be
resized too: longer adds empty steps to its rows (up to 64), shorter cuts off the steps past its
end; it can't be shorter than one step.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | A clip description |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `clip` | string | yes | Clip id |
| `start_beat` | number | one of these | New start, in beats |
| `end_beat` | number | one of these | New end, in beats |

### split_clip

Splits a clip in two at a beat inside it (at least 1/16 beat from either end). Step pattern
clips can't be split.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | An array of two clip descriptions: the first half, then the second |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `clip` | string | yes | Clip id |
| `beat` | number | yes | Where to split, in beats from the start of the project |

### mute_clip

Mutes or unmutes a clip: a muted clip stays where it is but plays nothing.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | A clip description |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `clip` | string | yes | Clip id |
| `muted` | boolean | no | True to mute, false to unmute; leave out to toggle. Must be a JSON boolean |

### set_clip

Renames a clip or gives it a colour of its own. A clip shows in its channel's colour unless it
has one.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | A clip description plus `name`, and `colour` when one was set |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `clip` | string | yes | Clip id |
| `name` | string | one of these | The clip's new name; can't be empty |
| `colour` | string | one of these | Hex colour, or `""` to go back to the channel's colour |

### duplicate_clip

Copies a clip, by default to just after itself. A step pattern's clip copies its whole block,
which keeps playing the same pattern (a block already at that place stays as it is).

| | |
| --- | --- |
| Undoable | Yes |
| Returns | A clip description of the copy (for a block, the copy of the clip named) |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `clip` | string | yes | Clip id |
| `start_beat` | number | no | Where the copy starts, 0 or later (default: the clip's end) |

### delete_clip

Deletes a clip. A step pattern's clip deletes its whole block (the pattern's clips on every
channel that start there).

| | |
| --- | --- |
| Undoable | Yes |
| Returns | A clip description of the deleted clip |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `clip` | string | yes | Clip id |

### create_pattern

Adds a step pattern. It becomes the current pattern, which `set_steps` changes by default and
the channel rack shows.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{pattern, name, colour?}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `name` | string | no | Its name (default `Pattern <id>`) |
| `colour` | string | no | Hex colour for its clips (default: the channels' colours) |
| `copy_of` | integer | no | A pattern whose rows it starts with, to make a variation |

### set_pattern

Renames a step pattern or changes its colour; its clips follow.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{pattern, name, colour?}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `pattern` | integer | yes | Pattern id (a JSON number) |
| `name` | string | one of these | New name; can't be empty |
| `colour` | string | one of these | Hex colour, or `""` for the channels' colours |

### delete_pattern

Deletes a step pattern, its rows and every block of it on the timeline. Pattern 1 can't be
deleted. If it was the current pattern, pattern 1 becomes current.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{pattern, name, colour?}` of the deleted pattern |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `pattern` | integer | yes | Pattern id |

### select_pattern

Makes a pattern the current one: the one the channel rack shows and `set_steps` changes by
default.

| | |
| --- | --- |
| Undoable | Yes (registered as undoable) |
| Returns | `{pattern, name, colour?}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `pattern` | integer | yes | Pattern id |

### place_pattern

Places a step pattern on the timeline as a block: a clip on every instrument channel with steps
in it, all starting at `start_beat`. Fails if the pattern has no steps yet.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{pattern, name, colour?, start_beat, clips: [clip descriptions]}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `pattern` | integer | yes | Pattern id |
| `start_beat` | number | no | Where the block starts, 0 or later (default 0) |

```json
{"batch": "Verse drums", "commands": [
  {"command": "create_pattern", "params": {"name": "Verse", "copy_of": 1}},
  {"command": "set_steps", "params": {"channel": "Snare", "pattern": "$0.pattern", "steps": "....x.......x..."}},
  {"command": "place_pattern", "params": {"pattern": "$0.pattern", "start_beat": 16}}]}
```

### make_unique

Gives a step pattern's block a pattern of its own, a copy of the one it played (named
`<name> (copy)`), so its steps can change without changing the pattern's other blocks. The copy
becomes the current pattern.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{pattern, name, colour?, clip}`: the new pattern, and the clip named |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `clip` | string | yes | Id of any clip of the block |

### get_notes

Lists a MIDI clip's notes.

| | |
| --- | --- |
| Undoable | No (changes nothing) |
| Returns | A clip description plus `pattern` (true for a step pattern's clip) and `notes: [{pitch, start, length, velocity}]`, times in beats from the clip's start |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `clip` | string | yes | Clip id of a MIDI clip |

The note indexes `quantize_notes` takes are positions in this list.

### set_notes

Replaces all of a MIDI clip's notes. Takes the same [note shorthand](#note-shorthand) as
`add_midi_clip`. Notes past the clip's end are kept but don't play. A step pattern's notes
follow its steps, so this fails on a pattern clip (use `set_steps`).

| | |
| --- | --- |
| Undoable | Yes |
| Returns | A clip description plus `notes` (the clip's notes after the change) |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `clip` | string | yes | Clip id of a MIDI clip |
| `notes` | array | `notes` or `chords` | Notes as `{pitch, start, length, velocity}`; `[]` clears the clip |
| `chords` | array | `notes` or `chords` | As `add_midi_clip` |
| `chord_style`, `chord_octave`, `chord_rate`, `repeat` | | no | As `add_midi_clip` |

Unlike `add_midi_clip`, every note needs a `pitch` (0 to 127 or a name), a length of at least
1/64 beat and a velocity from 1 to 127, and can't start before the clip's content.

```json
{"command": "set_notes", "params": {"clip": "1077", "notes": [
  {"pitch": "C4", "start": 0, "length": 1, "velocity": 110},
  {"pitch": "E4", "start": 1, "length": 1},
  {"pitch": "G4", "start": 2, "length": 2}]}}
```

### quantize_notes

Moves a MIDI clip's notes (or some of them) onto a grid. Note lengths stay the same unless
`ends` is true. Step pattern clips are always on their steps, so this fails on them.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | A clip description plus `moved` (how many notes moved), `quantized` (the indexes of the quantized notes in the new order) and `notes` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `clip` | string | yes | Clip id of a MIDI clip |
| `grid` | number | no | Grid in beats, 1/64 to 16 (default 0.25, a 16th; 1 is a quarter note) |
| `notes` | array | no | Indexes of the notes to quantize, as listed by `get_notes` (default: all) |
| `strength` | number | no | How far towards the grid, above 0 up to 1 (default 1) |
| `ends` | boolean | no | Also snap where notes end (default false) |

---

## Transport and recording

Registered in `src/core/TransportCommands.cpp`. Only tempo and time signature are undo steps;
the rest, like the play position, aren't.

### set_tempo

Sets the project tempo. Clips are placed in beats, so they keep their place in the music.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{bpm}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `bpm` | number | yes | Beats per minute, 20 to 300 (a JSON number) |

### set_time_signature

Sets the project time signature, e.g. 3/4 or 6/8.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{numerator, denominator}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `numerator` | integer | yes | Beats per bar, a whole number from 1 to 32 |
| `denominator` | integer | yes | Note value of a beat: 1, 2, 4, 8, 16 or 32 |

### set_loop

Turns looping on or off and sets the loop region. Without a region the loop covers the whole
project, rounded up to whole bars.

| | |
| --- | --- |
| Undoable | No |
| Returns | `{enabled, start_beat, end_beat, whole_project}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `enabled` | boolean | no | Loop playback |
| `start_beat` | number | with `end_beat` | Loop start in beats, 0 or later |
| `end_beat` | number | with `start_beat` | Loop end in beats, after the start |
| `whole_project` | boolean | no | True to go back to looping the whole project (ignored when a region is given) |

```json
{"command": "set_loop", "params": {"enabled": true, "start_beat": 16, "end_beat": 32}}
```

### play

Starts playback from the current position. Loops when looping is on.

| | |
| --- | --- |
| Undoable | No |
| Returns | Nothing |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `loop` | boolean | no | Turn looping on or off before playing |

### stop

Stops playback. When recording, what was recorded becomes clips on the armed channels, as one
undo step labelled `Record`.

| | |
| --- | --- |
| Undoable | No (but finishing a recording makes its own undo step) |
| Parameters | None |
| Returns | Nothing |

### arm_channel

Arms a channel for recording. An instrument channel arms for MIDI input: MIDI keyboards (and the
on-screen keyboard) play it, and arming one disarms the other instrument channels. An audio
channel arms for its audio input, and any number can be armed together. Leave out `channel`
with `armed: false` to disarm everything.

| | |
| --- | --- |
| Undoable | No |
| Returns | `{armed_channel, armed_audio, midi_inputs}`: the armed instrument channel's id (or null), the armed audio channels' ids, and the MIDI input names |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `channel` | string | no | Channel id or name (required to arm) |
| `armed` | boolean | no | False to disarm (default true) |

An audio channel can only be armed once it has an input (`set_audio_input`).

### record

Starts recording from the playhead: MIDI into the armed instrument channel and audio into each
armed audio channel. `stop` finishes the recording. Audio takes are saved as WAV files in a
*Recordings* folder beside the project.

| | |
| --- | --- |
| Undoable | No (`stop` makes the undo step) |
| Parameters | None |
| Returns | Nothing |

Fails when nothing is armed, an armed audio channel's input isn't on the current audio device,
or it is already recording.

### set_metronome

Turns the metronome (a click on every beat, louder on the bar) on or off.

| | |
| --- | --- |
| Undoable | No |
| Returns | `{enabled}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `enabled` | boolean | no | On or off (toggles if left out) |

### set_position

Moves the playhead.

| | |
| --- | --- |
| Undoable | No |
| Returns | `{beat}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `beat` | number | yes | Where to move it, in beats from the start, 0 or more (below 100000) |

---

## Plugins

Registered in `src/core/PluginCommands.cpp`. The plugin list belongs to the app, so it's the
same in every project.

### list_plugins

Lists the plugins found by scanning (not the built-in ones), sorted by name, with the id used to
load them.

| | |
| --- | --- |
| Undoable | No (changes nothing) |
| Returns | `[{id, name, vendor, format, category, instrument, outputs, file}]`; `outputs` is the number of output channels |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `kind` | string | no | `"all"` (default), `"instrument"` or `"effect"` |

### scan_plugins

Scans folders for plugins and adds any new ones to the plugin list. Without `folders` it scans
the folders used last time (shared with the Plugins window), or the format's usual places, such
as `C:\Program Files\Common Files\VST3` on Windows. Folders given are remembered. A plugin that
crashes the scan is skipped the next time. This runs in the app and can take a while.

| | |
| --- | --- |
| Undoable | No |
| Returns | `{format, folders, added, total, failed}`: the folders searched, how many plugins were added, how many are known now, and the files that failed to load |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `folders` | array | no | Absolute paths of existing folders to scan |
| `format` | string | no | Plugin format to scan for: `"VST3"` (default) |

---

## Insert effects and instrument parameters

Registered in `src/core/EffectCommands.cpp`. Effects sit on mixer strips (master included),
before the fader, and run first to last. The built-in effects are `eq`, `compressor`, `reverb`
and `delay`; scanned VST3 effects work the same way.

Where a command takes `strip` and `effect`, `set_effect`, `get_effect` and
`show_effect_window` (and the automation commands) can take `channel` instead, meaning that
channel's instrument: the built-in 4osc synth or a plugin. Its parameters can then be read, set
and automated like an effect's. Giving both `channel` and `strip` is an error.

The delay's time isn't a Tracktion parameter, so it is offered as an extra parameter with id
`length`, in milliseconds from 1 to 2000.

### add_effect

Adds an insert effect to a mixer strip (or the master), before the fader. Fails if the plugin
won't load, or is an instrument.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{strip, index, effect: {id, type, name, enabled, missing?}}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `strip` | string | yes | Mixer strip id or name, or `"master"` |
| `effect` | string | yes | `eq`, `compressor`, `reverb`, `delay` (or their names, `EQ` etc.), or a scanned plugin effect's id or name |
| `index` | integer | no | Position among the strip's effects, from 0 (default: last; larger values mean last) |

```json
{"command": "add_effect", "params": {"strip": "Bass", "effect": "compressor"}}
```

### remove_effect

Removes an effect from a mixer strip. A channel's instrument can't be removed this way (use
`set_instrument`).

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{strip, removed}`: the removed effect's id |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `strip` | string | yes | Mixer strip id or name, or `"master"` |
| `effect` | string | yes | The effect's id, position from 0, or name |

### move_effect

Moves an effect to another position on its strip.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{strip, effect, index}`: the effect's id and its new position |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `strip` | string | yes | Mixer strip id or name, or `"master"` |
| `effect` | string | yes | The effect's id, position from 0, or name |
| `index` | integer | yes | New position, from 0 (larger values mean last) |

### set_effect

Turns an effect on or off (bypass) and sets its parameters, by the ids or names `get_effect`
lists. With `channel` instead of `strip` and `effect`, sets the parameters of that channel's
instrument, to shape its sound. Values are in each parameter's own range, and every value is
checked before anything changes. Changes made in a plugin's own window aren't undo steps.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | The effect as `get_effect` returns it, but with `params` holding only the parameters that changed (all of them when none were given) |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `strip` | string | with `effect` | Mixer strip id or name, or `"master"` |
| `effect` | string | with `strip` | The effect's id, position from 0, or name |
| `channel` | string | instead of `strip`/`effect` | A channel (id or name), for its instrument |
| `enabled` | boolean | no | False bypasses the effect (a JSON boolean) |
| `params` | object | no | Parameter id or name (case-insensitive name match) to a number, e.g. `{"wet level": 0.5}` |

```json
{"command": "set_effect", "params": {"strip": "Vox", "effect": "reverb", "params": {"room size": 0.7, "wet level": 0.3}}}
{"command": "set_effect", "params": {"channel": "Lead", "params": {"filterFreq": 70}}}
```

### get_effect

Returns an effect with its parameters. With `channel` instead of `strip` and `effect`, returns
that channel's instrument and its parameters. A big plugin has hundreds, so `filter` narrows
them.

| | |
| --- | --- |
| Undoable | No (changes nothing) |
| Returns | `{id, type, name, enabled, missing?, strip or channel, filter?, params: [{id, name, value, min, max, text}]}`; `text` is the value as the plugin shows it |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `strip` | string | with `effect` | Mixer strip id or name, or `"master"` |
| `effect` | string | with `strip` | The effect's id, position from 0, or name |
| `channel` | string | instead of `strip`/`effect` | A channel (id or name), for its instrument |
| `filter` | string | no | Only parameters whose id or name contains every word of it (case-insensitive), e.g. `"filter cutoff"` |

### show_effect_window

Opens an effect's window: the plugin's own editor, or sliders for a built-in effect. With
`channel`, opens its instrument's.

| | |
| --- | --- |
| Undoable | No |
| Returns | `{effect, shown}`: `shown` is false when there's no screen to show it on |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `strip` | string | with `effect` | Mixer strip id or name, or `"master"` |
| `effect` | string | with `strip` | The effect's id, position from 0, or name |
| `channel` | string | instead of `strip`/`effect` | A channel (id or name), for its instrument |

---

## Buses, sends and routing

Registered in `src/core/BusCommands.cpp`. A bus is a mixer strip that other strips send to (a
shared reverb, say). A project can have up to 32. Anything that would loop back into itself is
refused.

### create_bus

Adds a bus. It plays into master and can hold effects like any strip. Channels can also be
routed straight to it.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{strip, name}`: the bus strip's id and name |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `name` | string | no | Bus name (default `Bus 1`, `Bus 2`, ...) |
| `colour` | string | no | Hex colour (default: next in the palette) |

### set_send

Sends a mixer strip to a bus, or changes that send. A new send is post-fader at 0 dB unless
`level_db` or `pre_fader` says otherwise. Fails if the bus already feeds the strip.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{strip, bus, level_db, pre_fader}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `strip` | string | yes | Mixer strip id or name |
| `bus` | string | yes | Bus strip id or name |
| `level_db` | number | no | Send level in dB, -100 to 6 |
| `pre_fader` | boolean | no | True to send before the strip's fader, so the fader doesn't change it (a JSON boolean) |

```json
{"batch": "Shared reverb", "commands": [
  {"command": "create_bus", "params": {"name": "Reverb"}},
  {"command": "add_effect", "params": {"strip": "$0.strip", "effect": "reverb"}},
  {"command": "set_send", "params": {"strip": "Vox", "bus": "$0.strip", "level_db": -8}}]}
```

### remove_send

Removes a mixer strip's send to a bus.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{strip, bus}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `strip` | string | yes | Mixer strip id or name |
| `bus` | string | yes | Bus strip id or name |

### route_strip

Plays a mixer strip into another strip (a group or bus, which then carries it through its own
effects and fader) or into master. Fails if that would loop, and for a multi-output
instrument's output strips.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{strip, output}`: `output` is the destination's id or `"master"` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `strip` | string | yes | Mixer strip id or name |
| `output` | string | yes | The strip to play into (id or name), or `"master"` |

### move_strip

Moves a mixer strip to another place in the mixer (and in `get_state`'s `strips`).

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{strip, index}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `strip` | string | yes | Mixer strip id or name |
| `index` | integer | yes | Where it goes among the strips, from 0 to the number of other strips |

### fold_strip

Folds away (or shows again) the strips that play into a strip, such as a bus's channels, so the
mixer shows just the bus. The fold is saved with the project.

| | |
| --- | --- |
| Undoable | Registered as undoable, but the fold itself isn't an undo step (see [Undo semantics](#undo-semantics)) |
| Returns | `{strip, folded}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `strip` | string | yes | Mixer strip id or name |
| `folded` | boolean | no | True to fold, false to show them (toggles if left out) |

### tag_channel

Tags a channel with what it is, such as `drums`, `bass` or `vox`. Each tag has a bus (named
`Drums Bus` and so on), created the first time the tag is used, and the channel's mixer strip
plays into it, so all the drums can be mixed and processed together. A channel playing straight
into master, or into another tag's bus, goes through its own strip if it has one (keeping its
effects and fader), or else straight into the bus. An empty tag takes the channel off its bus.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{channel, tag, bus, bus_name, created_bus}`, or `{channel, tag: ""}` when the tag was removed |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `channel` | string | yes | Channel id or name |
| `tag` | string | yes | Up to 32 characters of letters, digits, spaces, `-` and `_` (lower-cased), or `""` for none |

```json
{"command": "tag_channel", "params": {"channel": "Kick", "tag": "drums"}}
→ {"ok": true, "result": {"channel": "1012", "tag": "drums", "bus": "1050", "bus_name": "Drums Bus", "created_bus": true}}
```

---

## Automation

Registered in `src/core/AutomationCommands.cpp`. A parameter is named by:

- `strip` and `parameter` `"volume"` (in dB, -100 to 6) or `"pan"` (-1 to 1), for a strip's
  (or master's) fader;
- `strip`, `effect` and one of the effect's parameter ids or names from `get_effect`;
- or `channel` and one of its instrument's parameter ids or names.

### set_automation

Automates a parameter so it changes over the song: a fade-out, a filter sweep, a louder chorus.
Replaces the parameter's automation with the points given. Points are sorted by beat, and the
value moves in a straight line from each to the next. Automated parameters follow their points
when the song plays and when it's exported.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{strip or channel, effect or instrument?, parameter, name, min, max, value, points: [{beat, value}]}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `strip` | string | for strip and effect parameters | Mixer strip id or name, or `"master"` |
| `effect` | string | no | To automate an effect's parameter: its id, name or position on the strip |
| `channel` | string | no | Instead of `strip`: a channel, to automate its instrument's parameter |
| `parameter` | string | yes | `"volume"`, `"pan"`, or with `effect` or `channel`, a parameter id or name |
| `points` | array | yes | `[{beat, value}]`, beats from 0 and values in the parameter's range (JSON numbers). `[]` removes the automation, so the parameter stays where its control is set |

```json
{"command": "set_automation", "params": {"strip": "master", "parameter": "volume",
  "points": [{"beat": 120, "value": 0}, {"beat": 128, "value": -60}]}}
```

### get_automation

Returns a parameter's automation points and its range. `get_state` lists which parameters have
automation (each strip's and master's `automation`, and a channel's for its instrument).

| | |
| --- | --- |
| Undoable | No (changes nothing) |
| Returns | As `set_automation` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `strip` | string | for strip and effect parameters | Mixer strip id or name, or `"master"` |
| `effect` | string | no | The effect's id, name or position |
| `channel` | string | no | Instead of `strip`: a channel, for its instrument |
| `parameter` | string | yes | As `set_automation` |

---

## Multi-output instruments

Registered in `src/core/OutputCommands.cpp`.

### set_multi_output

Gives each stereo output of a channel's plugin instrument (a drum plugin's kick, snare and hats
outputs, say) a mixer strip of its own, named `<channel> 1-2`, `<channel> 3-4` and so on, all
playing into the channel's own strip, which sums them. Turning it off puts the instrument back
to one stereo output and removes those strips.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{channel, multi_output, output_strips: [ids]}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `channel` | string | yes | Channel id or name |
| `enabled` | boolean | yes | True for a strip per output, false for one (a JSON boolean) |

Turning it on fails unless the channel has a strip of its own and a loaded plugin instrument
with more than one stereo output.

---

## Sampler

Registered in `src/core/SamplerCommands.cpp`. These work on a channel whose instrument is the
sampler (`set_instrument` with `"sampler"`). A sampler holds up to 64 samples. Each sample is
described as `{index, name, file, note, min_note, max_note, gain_db, pan, one_shot, missing?,
channel}`.

The sample settings that `add_sample` and `set_sample` share:

| Parameter | Type | Description |
| --- | --- | --- |
| `note` | integer | MIDI note it plays at its own pitch on, 0 to 127 (60 is middle C, 36 a kick's C1). A sample on one key moves its whole range with it |
| `min_note` | integer | Lowest key it plays on, pitched from `note` (default: `note`) |
| `max_note` | integer | Highest key it plays on (default: `note`) |
| `gain_db` | number | Level in dB, -48 to 48 (default 0) |
| `pan` | number | -1 (left) to 1 (right) (default 0) |
| `one_shot` | boolean | True (default) to play to the end whatever the note's length, as drums do; false to stop when the note ends |
| `name` | string | Name shown in the sampler (default: the file's name); can't be empty |

### add_sample

Adds an audio file to a channel's sampler. By default it's a one-shot on the lowest free key
from C1 (36) up, as for a drum kit; give `note`, `min_note` and `max_note` to play it pitched
across a range of keys instead.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | The new sample's description |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `channel` | string | yes | Channel id or name |
| `file` | string | yes | Absolute path to the audio file (WAV, AIFF, FLAC, Ogg or MP3) |
| The sample settings above | | no | |

```json
{"command": "add_sample", "params": {"channel": "Piano", "file": "D:\\Samples\\piano-c4.wav",
  "note": 60, "min_note": 48, "max_note": 72, "one_shot": false}}
```

### set_sample

Changes a sample: its keys, level, pan, one-shot, name or file.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | The sample's description |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `channel` | string | yes | Channel id or name |
| `sample` | string or integer | yes | The sample's index (from 0, as `get_state` lists them) or name |
| `file` | string | no | Absolute path to a different audio file to play |
| The sample settings above | | no | |

### remove_sample

Removes a sample from a channel's sampler.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{channel, removed, samples}`: the removed sample's name and how many are left |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `channel` | string | yes | Channel id or name |
| `sample` | string or integer | yes | The sample's index (from 0) or name |

### load_drum_kit

Loads a folder of samples into a channel's sampler as a drum kit, one one-shot per key. Samples
named for what they are go on their General MIDI keys; the rest fill the free keys from 36 up,
in name order. Looks in the folders inside the folder when it has no samples itself.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{channel, loaded: [sample descriptions]}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `channel` | string | yes | Channel id or name |
| `folder` | string | yes | Absolute path to the kit's folder |
| `replace` | boolean | no | True (default) to replace the sampler's samples; false to add to them |

The names recognised (as words in the file name, also split at lower-to-upper case changes like
`OpenHat`):

| Key | Words |
| --- | --- |
| 36 kick | kick, bd, bassdrum, kik |
| 37 rim | rim, rimshot, sidestick, stick |
| 38 snare | snare, sd, snr |
| 39 clap | clap, cp, clp |
| 42 closed hat | hat, hihat, hh, hats, ch, chh, chat, closedhat |
| 46 open hat | a hat word with open, op or o; or oh, ohh, ohat, openhat |
| 45 / 47 / 50 tom | tom, toms; with floor, low, lo, lt (45) or high, hi, ht (50) |
| 49 crash | crash, cym, cymbal |
| 51 ride | ride |
| 54 tambourine | tamb, tambourine |
| 56 cowbell | cowbell, bell, cb |
| 63 conga | conga |
| 70 shaker | shaker, shk, maraca, maracas |

---

## Library: sample folders and presets

Registered in `src/core/LibraryCommands.cpp`. The sample folders and the preset folder are the
app's settings, the same in every project. Presets are `.ngpreset` files kept in a folder per
plugin inside the preset folder (by default `Music/Ng DAW/Presets`).

### get_library

Returns the browser's folders: the sample folders it shows (and `find_samples` searches) and
the folder presets are saved in. It also starts indexing the sample folders in the background,
so a search soon after is quick.

| | |
| --- | --- |
| Undoable | No (changes nothing) |
| Parameters | None |
| Returns | `{sample_folders: [paths], preset_folder}` |

### set_library

Sets the browser's sample folders (the whole list) and/or the folder presets are saved in.

| | |
| --- | --- |
| Undoable | No |
| Returns | `{sample_folders, preset_folder}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `sample_folders` | array | no | Absolute paths of existing folders holding samples; replaces the list |
| `preset_folder` | string | no | Absolute path of the folder to save presets in (created if needed) |

### find_samples

Finds audio files in the sample folders (or one folder) whose path below the folder contains
every word of the query (case-insensitive), so `"kick 808"` finds `808 Kit/Kick Hard.wav`.
Load them with `add_sample` or `load_drum_kit`.

| | |
| --- | --- |
| Undoable | No (changes nothing) |
| Returns | `{samples: [{name, path, folder}], truncated}`: `truncated` is true when there were more than `limit` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `query` | string | no | Words to look for in the file's name and folders (default: all files) |
| `folder` | string | no | Absolute path of a folder to search instead of the sample folders |
| `limit` | integer | no | Most files to list, from 1 (default 100, at most 1000) |

Fails when no `folder` is given and there are no sample folders yet.

### save_preset

Saves the settings of a channel's instrument, or of an effect, as a named preset in the preset
folder, to load on any channel or strip in any project. A sampler's preset keeps its samples.
An existing preset with the same name for the same plugin is replaced.

| | |
| --- | --- |
| Undoable | No (writes a file) |
| Returns | `{name, plugin, plugin_name, kind, path}`: `kind` is `instrument` or `effect` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `name` | string | yes | Preset name |
| `channel` | string | `channel` or `strip` | Channel id or name, to save its instrument |
| `strip` | string | `channel` or `strip` | Mixer strip id or name (or `"master"`), with `effect` |
| `effect` | string | with `strip` | The effect's id, position from 0, or name |

### list_presets

Lists the presets in the preset folder, sorted by plugin, then name.

| | |
| --- | --- |
| Undoable | No (changes nothing) |
| Returns | `[{name, plugin, plugin_name, kind, path}]` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `plugin` | string | no | Only this plugin's presets (its id or name) |

### load_preset

Loads a preset. An instrument preset replaces a channel's instrument; an effect preset replaces
the effect named on a strip, or is added (at `index`, default last).

| | |
| --- | --- |
| Undoable | Yes |
| Returns | For an instrument: `{channel, instrument, preset}`. For an effect: `{strip, index, effect, preset}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `preset` | string | yes | The preset's absolute path, or its name when only one preset has it |
| `channel` | string | `channel` or `strip` | Channel id or name, for an instrument preset |
| `strip` | string | `channel` or `strip` | Mixer strip id or name (or `"master"`), for an effect preset |
| `effect` | string | no | The effect to replace: its id, position from 0, or name |
| `index` | integer | no | Where to add the effect, from 0 (default: last) |

Fails when an instrument preset is given a strip (or an effect preset a channel), on an audio
channel, while the channel has multiple outputs on, or when the preset's plugin isn't installed.

---

## Audio clips and inputs

Registered in `src/core/AudioCommands.cpp`. Audio clips live on audio channels (`create_track`
with `instrument: "audio"`), and move, trim, split and copy with the clip commands above.

### import_audio

Puts an audio file on the timeline as a clip that plays the whole file. It goes on the audio
channel named; left out, it goes on the audio channel already playing that file if it has room
there, or else on a new audio channel (and mixer strip) named after the file.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{clip, channel, start_beat, length_beats, file}`, plus `strip` when a new channel was made |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `file` | string | yes | Path to the audio file (WAV, AIFF, FLAC, Ogg or MP3) |
| `channel` | string | no | An audio channel's id or name |
| `start_beat` | number | no | Where the clip starts, 0 or later (default 0) |
| `name` | string | no | Name for a new channel (default: the file's name). Giving one always makes a new channel |

```json
{"command": "import_audio", "params": {"file": "D:\\Loops\\break-174.wav", "start_beat": 16}}
```

### set_audio_clip

Changes how an audio clip plays, like a sample editor. Only the values given change, and the
values are checked before anything changes.

A clip that follows the tempo is time-stretched to keep its length in beats when the tempo
changes. `source_bpm` is the file's own tempo (default: what it plays at now, so nothing changes
until the tempo does; `analyze_audio` detects it). `stretch_mode` picks how speed and pitch
change: `"stretch"` (default; Rubber Band's highest-quality engine, for most sounds), `"beats"`
(Rubber Band's percussive mode, which keeps drum hits crisp) or `"resample"` (speed and pitch
change together, as in FL Studio). Switching mode keeps the clip's pitch, and its length unless
it's resampled.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{clip, start_beat, length_beats}` plus the clip's settings: `file`, `missing?`, `max_end_beat`, `gain_db`, `fade_in_beats`, `fade_out_beats`, `follow_tempo`, `source_bpm?`, `stretch_mode`, `pitch_semitones`, `speed`, `reversed` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `clip` | string | yes | The audio clip's id |
| `gain_db` | number | no | Clip gain in dB, -60 to 24 (0 is as recorded). Not with `normalize` |
| `normalize` | boolean | no | True sets the gain so the file's loudest peak reaches 0 dB |
| `fade_in_beats` | number | no | Fade-in length in beats, 0 or more (0 for none) |
| `fade_out_beats` | number | no | Fade-out length in beats, 0 or more. The two fades together can't be longer than the clip |
| `pitch_semitones` | number | no | Pitch change, -24 to 24; fractions are cents (0.5 is 50 cents). In resample mode this changes the speed and length too, and the result can't be more than two octaves off |
| `stretch_to_beats` | number | no | Stretches (or squeezes) the clip to this many beats from its start, playing the same part of its file. Without `follow_tempo` the speed must stay within 0.25 to 4 times; with it, the implied file tempo within 20 to 400 bpm |
| `stretch_mode` | string | no | `"stretch"`, `"beats"` or `"resample"` |
| `reverse` | boolean | no | Plays the clip backwards |
| `follow_tempo` | boolean | no | Time-stretch the clip to follow the project tempo. Not with `"resample"` |
| `source_bpm` | number | no | With `follow_tempo` on: the file's own tempo, 20 to 400 |

```json
{"command": "set_audio_clip", "params": {"clip": "1101", "follow_tempo": true, "source_bpm": 174,
  "stretch_mode": "beats", "fade_out_beats": 0.5}}
```

### analyze_audio

Reads an audio clip's file, or any audio file, and measures it. A short loop's tempo is matched
to a whole number of bars.

| | |
| --- | --- |
| Undoable | No (changes nothing) |
| Returns | `{file, length_seconds, sample_rate, channels, peak_db, rms_db, loudness_lufs, clipped_samples, bpm?, beats?}`: `rms_db` is over the whole file, `loudness_lufs` the integrated loudness (ITU-R BS.1770, gated, as streaming services measure it), `clipped_samples` how many samples hit full scale; `bpm` and `beats` (its length at that tempo) are left out when no tempo is found |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `clip` | string | `clip` or `file` | An audio clip's id |
| `file` | string | `clip` or `file` | Or the path to an audio file |

### set_audio_input

Sets the audio input an audio channel records from, and how it's monitored. Arm the channel
with `arm_channel` and record with `record`.

| | |
| --- | --- |
| Undoable | Yes |
| Returns | `{channel, input, monitor, armed}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `channel` | string | yes | Audio channel id or name |
| `input` | string | one of these | An input from `audio_inputs` in `get_state`, such as `"Input 1 + 2"`, or `"none"` (which also disarms it) |
| `monitor` | string | one of these | `"auto"` (default; plays the input through the channel while it's armed), `"on"` (always) or `"off"` (never) |

---

## Export and mix analysis

Registered in `src/core/ExportCommands.cpp`. Both render offline, and while they run every
other command except `get_state` waits its turn by failing with
`wait until the export is finished`. Neither works while recording.

### export_audio

Exports the song (from the start to the end of the last clip, plus two seconds for tails) or
the loop region to a WAV or MP3 file, as heard through the master. With `stems: true`, writes one
file per mixer strip into the folder at `path` instead, named `<project> - <strip>`, all the same
length so they line up. A strip's stem has its channels through its own effects and fader; what
it sends to a bus is in that bus's stem; stems leave out the master's effects and fader.
Existing files are replaced. MP3 needs LAME or FFmpeg (on the PATH, or next to Ng DAW).

| | |
| --- | --- |
| Undoable | No |
| Returns | `{files: [paths], format, start_beat, end_beat, seconds}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `path` | string | yes | Absolute path of the file to write (its extension is set from the format) or, for stems, the folder |
| `format` | string | no | `"wav"` or `"mp3"` (default: `mp3` if the path ends in `.mp3`, else `wav`) |
| `range` | string | no | `"song"` (default) or `"loop"` |
| `stems` | boolean | no | One file per mixer strip instead of the mix |
| `bit_depth` | number | no | WAV bit depth: 16, 24 (default) or 32 (float) |
| `sample_rate` | number | no | 44100 (default), 48000, 88200 or 96000 |
| `mp3_kbps` | number | no | MP3 bit rate, 96 to 320 (default 320) |

```json
{"command": "export_audio", "params": {"path": "C:\\Users\\me\\Music\\Ng DAW\\mix.wav", "bit_depth": 24}}
{"command": "export_audio", "params": {"path": "C:\\Users\\me\\Music\\Ng DAW\\stems", "stems": true}}
```

### analyze_mix

Renders the loop region (or the song) offline and measures it, so a mix can be checked without
listening: the master's levels and the same for each mixer strip's stem, with its fader level and
how far below the loudest strip it sits. Warnings point out clipping, a master peaking above
-1 dBFS, a quiet master (below -24 LUFS), strips that clip on their own and strips that are
silent. Renders at 44.1 kHz into a temporary folder that is deleted afterwards. It takes about as
long as an export.

| | |
| --- | --- |
| Undoable | No (changes nothing) |
| Returns | `{range, start_beat, end_beat, master: {peak_db, rms_db, loudness_lufs, clipped_samples}, strips?: [{id, name, role, volume_db?, peak_db, rms_db, loudness_lufs, clipped_samples, muted?, relative_to_loudest_db?}], warnings: [text]}` |

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `range` | string | no | `"loop"` or `"song"` (default: `loop` while looping with a loop region, else `song`) |
| `stems` | boolean | no | Also measure each strip (default true); false measures only the master, faster |

---

## Index of all commands

| Command | Group | Undoable |
| --- | --- | --- |
| `add_effect` | Effects | Yes |
| `add_midi_clip` | Clips | Yes |
| `add_sample` | Sampler | Yes |
| `analyze_audio` | Audio | No |
| `analyze_mix` | Export | No |
| `arm_channel` | Transport | No |
| `create_bus` | Buses | Yes |
| `create_pattern` | Clips | Yes |
| `create_track` | Project | Yes |
| `delete_clip` | Clips | Yes |
| `delete_pattern` | Clips | Yes |
| `delete_track` | Project | Yes |
| `duplicate_clip` | Clips | Yes |
| `export_audio` | Export | No |
| `find_samples` | Library | No |
| `fold_strip` | Buses | Flag yes; no undo step in practice |
| `get_automation` | Automation | No |
| `get_effect` | Effects | No |
| `get_history` | Project | No |
| `get_library` | Library | No |
| `get_notes` | Clips | No |
| `get_state` | Project | No |
| `import_audio` | Audio | Yes |
| `list_plugins` | Plugins | No |
| `list_presets` | Library | No |
| `list_sounds` | Project | No |
| `load_drum_kit` | Sampler | Yes |
| `load_preset` | Library | Yes |
| `make_unique` | Clips | Yes |
| `move_clip` | Clips | Yes |
| `move_effect` | Effects | Yes |
| `move_strip` | Buses | Yes |
| `mute_clip` | Clips | Yes |
| `new_project` | Project | No |
| `open_project` | Project | No |
| `place_pattern` | Clips | Yes |
| `play` | Transport | No |
| `quantize_notes` | Clips | Yes |
| `record` | Transport | No |
| `remove_effect` | Effects | Yes |
| `remove_sample` | Sampler | Yes |
| `remove_send` | Buses | Yes |
| `rename_track` | Project | Yes |
| `resize_clip` | Clips | Yes |
| `route` | Project | Yes |
| `route_strip` | Buses | Yes |
| `save_preset` | Library | No |
| `save_project` | Project | No |
| `scan_plugins` | Plugins | No |
| `select_pattern` | Clips | Yes |
| `set_audio_clip` | Audio | Yes |
| `set_audio_input` | Audio | Yes |
| `set_automation` | Automation | Yes |
| `set_clip` | Clips | Yes |
| `set_effect` | Effects | Yes |
| `set_instrument` | Project | Yes |
| `set_library` | Library | No |
| `set_loop` | Transport | No |
| `set_metronome` | Transport | No |
| `set_mixer_strip` | Project | Yes (fader and pan; not mute and solo) |
| `set_multi_output` | Outputs | Yes |
| `set_notes` | Clips | Yes |
| `set_pattern` | Clips | Yes |
| `set_position` | Transport | No |
| `set_sample` | Sampler | Yes |
| `set_send` | Buses | Yes |
| `set_steps` | Clips | Yes |
| `set_tempo` | Transport | Yes |
| `set_time_signature` | Transport | Yes |
| `set_track_colour` | Project | Yes |
| `show_effect_window` | Effects | No |
| `show_plugin_window` | Project | No |
| `split_clip` | Clips | Yes |
| `stop` | Transport | No (finishing a recording is its own step) |
| `tag_channel` | Buses | Yes |

Plus the MCP-only tools `batch`, `undo` and `redo`.
