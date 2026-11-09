# Extending Ng DAW

A practical guide to the two most common changes, adding a command and adding a UI panel, plus
the conventions the codebase follows. Read [architecture.md](architecture.md) first for the
big picture. The existing commands are listed in [commands.md](commands.md); see
[building.md](building.md) for building and running the tests.

## Adding a command

A command is the only way to change the project. Once it's registered, it is available to the
UI, the JSON console, the tests and every MCP client with no further work: `NgDawMcp` lists
whatever `registerProjectCommands()` registers.

### 1. Pick the file

Commands are grouped by area in `src/core/*Commands.cpp`:

| File | Area |
| --- | --- |
| `ProjectCommands.cpp` | channels and strips, routing, the mixer strip, instruments, new/open/save, `get_state`, `get_history` |
| `ClipCommands.cpp` | MIDI clips, step patterns, notes |
| `TransportCommands.cpp` | play/stop/record, arming, playhead, tempo, time signature, loop, metronome |
| `PluginCommands.cpp` | scanning and listing plugins |
| `EffectCommands.cpp` | insert effects and their parameters |
| `BusCommands.cpp` | buses, sends, tags |
| `OutputCommands.cpp` | multi-output instruments |
| `SamplerCommands.cpp` | the sampler and drum kits |
| `LibraryCommands.cpp` | sample folders, sample search, presets |
| `AudioCommands.cpp` | audio channels and clips, audio inputs, `analyze_audio` |
| `AutomationCommands.cpp` | automation |
| `ExportCommands.cpp` | export and `analyze_mix` |

If a command fits one of them, add it there. For a new area:

1. Create `src/core/FooCommands.cpp`.
2. Declare `void registerFooCommands (CommandRegistry&);` in `src/core/CommandRegistry.h`, with a
   doc comment listing the commands it registers (as the other declarations do).
3. Call it at the end of `registerProjectCommands()` in `src/core/ProjectCommands.cpp`.
4. Add the file to `add_library(ng_core STATIC ...)` in `CMakeLists.txt`.

### 2. Write the handler

Handlers live in an anonymous namespace and have the signature
`CommandResult (Session&, const juce::var& params)`. A minimal one, in the style of `set_tempo`
(`src/core/TransportCommands.cpp`) and `rename_track` (`src/core/ProjectCommands.cpp`):

```cpp
#include "CommandRegistry.h"

namespace ng
{
namespace
{
    using params::isNumber;
    using params::object;
    using params::prop;
    using params::schema;

    CommandResult setSwing (Session& session, const juce::var& p)
    {
        auto ref = params::getString (p, "channel");
        auto channel = session.findTrack (ref, Session::Role::channel);

        if (channel == nullptr)
            return CommandResult::failure ("set_swing: no channel called '" + ref + "'");

        auto amount = p["amount"];

        if (! isNumber (amount) || static_cast<double> (amount) < 0.0 || static_cast<double> (amount) > 1.0)
            return CommandResult::failure ("set_swing: amount must be a number from 0 to 1");

        // Through the Edit's UndoManager, so it's part of this command's undo step.
        channel->state.setProperty ("ngSwing", static_cast<double> (amount), &session.getUndoManager());

        return CommandResult::success (object ({ { "channel", channel->itemID.toString() },
                                                 { "amount", static_cast<double> (amount) } }));
    }
}

void registerFooCommands (CommandRegistry& registry)
{
    registry.add ({ "set_swing",
                    "Sets how much a channel's steps swing, from 0 (straight) to 1.",
                    schema ({ { "channel", prop ("string", "Channel id or name") },
                              { "amount", prop ("number", "Swing from 0 (straight) to 1") } },
                            { "channel", "amount" }),
                    true, setSwing });
}

} // namespace ng
```

(`set_swing` and `ngSwing` are made up for the example.)

### 3. Parameters and schema

- The `CommandSpec` fields are `name`, `description`, `paramsSchema`, `undoable`, `run`, in that
  order (`src/core/CommandRegistry.h`).
- Build the schema with `params::schema ({ { "name", prop ("type", "description") }, ... },
  { "required", ... })`. For arrays or nested objects, add `items` or `properties` to the
  `prop`'s `DynamicObject`, as `McpServer::listTools` does for `batch`, or share fragments the way
  `notebuilder::chordsSchema()` is shared by `add_midi_clip` and `set_notes`.
- The schema and description are what an AI model sees as the tool's definition, so write the
  description for a reader who only has that: what the command does, what is optional, what
  isn't undoable, and which other command to use first (e.g. "a plugin's id from
  list_plugins").
- **The dispatcher doesn't validate against the schema.** It only checks that `params` is an
  object. Check every parameter yourself: use `params::getString` / `params::getDouble` with a
  fallback, `params::isNumber` for numbers (it rejects strings that look like numbers), and
  `p.hasProperty (...)` for optional values, so that "only the values given change" (as
  `set_mixer_strip` does).

### 4. Results

- Return `CommandResult::success (value)` with the ids of whatever you created or changed, as an
  object built with `params::object`. Batches refer to earlier results with `"$<n>.<field>"`
  (`CommandDispatcher::executeBatch`), so a command that creates something should return its id
  under an obvious key (`create_track` returns `channel`, `strip`, ...).
- Return `CommandResult::failure ("<command>: <what went wrong>")`. Messages are plain sentences
  that say how to fix the problem where possible.
- If the new state should be visible to `get_state` and the UI, add it to `Session::toVar()` (or
  to the relevant `describe...` helper on `Session`). The UI rebuilds itself from `toVar()`.

### 5. Undo

- Set `undoable = true` for anything that changes the song. The dispatcher opens a transaction
  named after the command, runs it, sorts track contents inside the same transaction, and rolls
  everything back if you return a failure. You don't need to call `beginNewTransaction` yourself.
- Make your changes through Tracktion's API (which records into the Edit's `UndoManager`) or
  through `ValueTree::setProperty (..., &session.getUndoManager())`. A change written with
  `nullptr` as the UndoManager is saved but not undone; that's deliberate for state that isn't
  the song (the loop region, the current pattern, folded strips).
- It's fine to fail half-way: the dispatcher undoes whatever the command did. Still, validate
  before changing anything where you can, as the existing handlers do.
- Set `undoable = false` for queries, transport, file operations and settings. The dispatcher
  then opens no transaction. It does compare the project with the saved state afterwards in case
  the command touched it anyway (`notify (..., checkUnsaved)`).
- If you add state that Tracktion's live objects cache (parameters, routing), check whether
  `Session::refreshAfterHistoryChange()` has to bring it back in line after undo and redo.
- If the command replaces the Edit, it clears the history, as `new_project` and `open_project` do.

### 6. Naming and logging

- Names are `snake_case`, verb first: `set_...`, `add_...`, `remove_...`, `get_...`.
- Commands whose names start with `get_`, `list_` or `find_` are treated as queries and aren't
  written to the AI command log (`isQuery` in `src/core/CommandDispatcher.cpp`). Use one of those
  prefixes for a read-only command, or it will show up in the log every time an AI calls it.
- During an export, every command except `get_state` is refused (`CommandDispatcher::run`).
  Don't rely on running while `export_audio` renders.

### 7. Test it

Tests are in `tests/CommandTests.cpp`, a single headless executable with no audio device or
window. Add a function in the anonymous namespace and a `RUN (...)` line in `main()`:

```cpp
void testSwing (tracktion::Engine& engine)
{
    std::cout << "set_swing sets and undoes a channel's swing\n";
    Fixture f (engine);

    f.dispatcher.execute ("create_track", obj ({ { "name", "Drums" } }));

    auto r = f.dispatcher.execute ("set_swing", obj ({ { "channel", "Drums" }, { "amount", 0.5 } }));
    CHECK (r.ok && near (r.value["amount"], 0.5));

    CHECK (! f.dispatcher.execute ("set_swing", obj ({ { "channel", "Nobody" }, { "amount", 0.5 } })).ok);
    CHECK (! f.dispatcher.execute ("set_swing", obj ({ { "channel", "Drums" }, { "amount", "lots" } })).ok);

    CHECK (f.dispatcher.undo());   // one undo step, and the failures left none behind
    // ... check f.state() is back to how it was
}
```

```cpp
RUN (testSwing);
```

What the harness gives you:

- `Fixture` (a `Session`, a `CommandRegistry` with every command registered, and a
  `CommandDispatcher`), `f.state()` (= `get_state`), `obj ({...})` to build params, `CHECK (...)`,
  `near (var, expected)`, and `note (pitch, start, length)`.
- `renderBuffer (f)` / `renderRms (f)` / `rmsBetween (...)` render the Edit offline at 44.1 kHz
  to check that something actually makes (or stops making) sound.
- The test plugins `NgTestSynth`, `NgTestGain` and `NgTestDrums`, built alongside, in the folders
  `NG_TEST_PLUGIN_FOLDER`, `NG_TEST_EFFECT_FOLDER` and `NG_TEST_DRUMS_FOLDER`.

Typical things to check, following the existing tests: the result, the effect on `get_state`,
that errors leave the project unchanged, that the command is exactly one undo step
(`testUndoIsOneStepPerCommand`), that it survives save and reopen if it's stored in the project
(`testSaveAndOpen`), and that it works inside a batch. `testJsonBridgeAndToolList` and
`testBridgeAndMcpServer` check that the tool list matches `registry.getAll()` (plus `batch`,
`undo` and `redo`), so new commands are covered there automatically.

Run them with `ctest --test-dir build --output-on-failure`, or run the executable directly with
a filter: `NgDawTests Swing` runs every test with "Swing" in its name, `NgDawTests =testSwing`
just that one.

### 8. Use it from the UI, and document it

- Call it from the UI with `dispatcher.execute ("set_swing", makeObject ({ ... }))`
  (`makeObject` is in `src/app/Widgets.h`). For a drag that sends many values, wrap it in
  `dispatcher.beginGesture ("Swing")` / `endGesture()` so it's one undo step.
- Add it to [commands.md](commands.md). The README also has a table of commands.

## Adding a UI panel

Panels are JUCE components in `src/app/`. They never change the Edit themselves: they read
state and call the dispatcher.

### 1. The component

```cpp
// src/app/SwingPanel.h
#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/** What the panel shows and how to use it, in a sentence or two. */
class SwingPanel  : public juce::Component
{
public:
    SwingPanel (Session&, CommandDispatcher&);

    /** Rebuilds what changed, from session.toVar(). */
    void update (const juce::var& state);

    /** Drops anything pointing into the Edit, before the project is replaced. */
    void clear();

    void resized() override;

private:
    Session& session;
    CommandDispatcher& dispatcher;
    juce::String key;   // what update() last built from

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwingPanel)
};

} // namespace ng
```

Follow the pattern of `ChannelRack`, `MixerView` and `TimelineView`:

- Take `Session&` and `CommandDispatcher&` in the constructor.
- **Change things only through the dispatcher.** Every button, slider and drop calls
  `dispatcher.execute`, `executeBatch` or a gesture. That keeps undo, the unsaved-changes flag,
  the command log and the other views right, and means anything the panel does an AI client can
  do too.
- **Rebuild from state, and only when it changed.** `update (const juce::var& state)` receives the
  snapshot `MainComponent::refreshViews()` takes after every command. Build a key from the part of
  the state you show and return early if it's the same as last time (see `ChannelRack::update`,
  which leaves out clips so that clip edits don't rebuild the rack, and
  `MixerView::getStructureKey`). Keep ids, not pointers, between updates.
- **Poll what changes without a command** (playhead, meters) on a `juce::Timer`, as
  `MixerView` (meters, 30 Hz) and `HistoryPanel` (the undo history, 4 Hz) do.
- **Let go before the Edit is replaced.** If you hold Tracktion objects (plugins, tracks, meter
  clients), give the panel a `clear()` and call it from `session.onEditAboutToChange` in
  `MainComponent`'s constructor.

Add the `.cpp` to `target_sources(NgDaw PRIVATE ...)` in `CMakeLists.txt`.

### 2. Wire it into MainComponent

For a **docked panel** (like the timeline, rack and mixer):

1. Add the content as a member of `MainComponent` (`src/app/MainComponent.h`) next to the others,
   e.g. `SwingPanel swing { session, dispatcher };`.
2. Add a `DockPanel` for it **after** the contents (members are destroyed in reverse order, so the
   panels go first): `swingPanel { "swing", "Swing", icons::Icon::..., swing, getProperties(), this }`.
   The id names its saved settings (`ngPanelDetached_swing`, `ngPanelWindow_swing`). Add header
   buttons with `swingPanel.addHeaderItem (...)`.
3. Add it to the `for (auto* panel : { &timelinePanel, ... })` lists in `MainComponent.cpp`: in
   `setUpPanels()` (which sets `onDetachChanged` and adds it), `restoreDetachedPanels()`,
   `resetLayout()`, the View menu in `getMenu()`, and `ownsComponent()`; then give it a place in
   `layOutPanels()`.
4. Call `swing.update (state)` in `refreshViews()` and `swing.clear()` in `onEditAboutToChange`.

For a **dialog** (like History, Settings and Export), create it on demand in a
`juce::DialogWindow::LaunchOptions`, as `MainComponent::showHistory()` does, and refresh it from a
timer or from the dispatcher's callbacks while it's open.

### 3. Make it reachable

Add an `AppAction` in `MainComponent::setUpActions()`:

```cpp
add ("swing", "Swing...", "Show the swing settings", { KeyPress ('w', command | shift, 0) },
     [this] { showSwing(); });
```

The action's keys work everywhere (including detached windows), it appears in the shortcuts list
(F1) and the command palette (Ctrl+Shift+P), and adding its id to a menu in
`MainComponent::getMenu()` (which uses `actions.menuItem (id)`) shows it there with its key. Check that the key isn't taken: `AppActions::add` asserts that ids are unique, but not
keys.

## Conventions

Observed across `src/core` and `src/app`:

- **One way to change the project.** Commands for everything that changes the song; the UI
  calls the dispatcher. The exceptions are deliberate and documented where they happen: live
  MIDI (`Session::sendLiveMidi`) and per-user preferences (`src/app/AppSettings.h`).
- **Ids or names.** Commands that take a channel, strip or clip accept its item id or its name
  (`Session::findTrack`, `findAnyTrack`), and `"master"` where the master strip makes sense.
  Results return ids.
- **Beats, not seconds.** Positions and lengths in commands and `get_state` are in beats from the
  start of the song (clips' notes from the clip's start). Round results with `params::roundBeats`
  to tidy float noise.
- **Plain-English errors prefixed with the command name**, saying how to fix it where possible
  (`"set_library: '" + path + "' isn't a folder (give the full path)"`).
- **Ng state lives in the Edit's ValueTree** as properties prefixed `ng` (or elements prefixed
  `NG`), written through the UndoManager unless it isn't part of the song. List any new ones in
  [project-format.md](project-format.md). Keep old projects opening unchanged: patterns kept
  pattern 1's rows on the channels for exactly that reason.
- **Fix Tracktion's side effects inside the command.** If Tracktion does something later as a
  separate change (sorting, output numbering, lazy creation), do it inside the command's
  transaction so undo stays one step. See `Session::sortTrackContents`, `writeTrackOutputs` and
  the `getSceneList()` call in `adoptEdit`.
- **App settings** go in Tracktion's properties file under `ng`-prefixed keys, through helpers
  in `src/app/AppSettings.h` (UI preferences) or next to the command that uses them (library
  settings in `LibraryCommands.cpp`).
- **Comments explain why**, in full sentences: every public function has a doc comment saying
  what it does in the user's terms, and workarounds say what they work around. British spelling
  (`colour`, `recolour`) in identifiers, parameters and text.
- **Style**: JUCE style (4-space indents, a space before the parenthesis in calls and
  declarations, braces on their own lines, `//====` separators between sections,
  `JUCE_DECLARE_NON_COPYABLE...` on classes). Everything is in `namespace ng`; Tracktion is
  aliased as `te` (`src/core/Session.h`). Files use LF line endings.
- **No logging to stdout in core code** that `NgDawMcp` links: its stdout carries only JSON-RPC
  (`src/mcp/McpMain.cpp`). Use `DBG` for diagnostics.
