# Panels and preferences

## Arranging the panels

The **timeline**, **channel rack**, **mixer**, **piano roll** and **sample editor** are panels. Each
has a header with its title, its own buttons, and a **detach** button on the right. The browser runs
down the left beside them.

### Resizing

Drag the bars between panels to give each more room:

| Bar | Between | Range |
| --- | --- | --- |
| Vertical bar right of the browser | Browser and everything else | 160 pixels up to half the window |
| Horizontal bar under the timeline | Timeline and the rack/mixer (or the open editor) | 10% to 90% of the height |
| Vertical bar between rack and mixer | Channel rack and mixer | 10% to 90% of the width |

**Double-click** a bar to put it back to its default.

### Detaching panels into windows

Click a panel's **detach** button, or **double-click its header**, to move it into a window of its
own, for example to put the mixer on a second screen. You can also use **View > (panel) in its own
window**.

- The window can be moved and resized freely. Its position and size are remembered.
- Keyboard shortcuts and the typing keyboard work in detached windows too.
- To put the panel back, click its button again (*Put back into the main window*), double-click its
  header, untick it in the View menu, or close its window.
- The piano roll's and sample editor's windows only show while a clip is open in them.
- If every panel is detached, the main window says *Every panel is in its own window. Attach one
  with its button, or from the View menu.*

When the piano roll or sample editor is open and attached, it takes the place of the channel rack
and mixer. If the rack or mixer is detached, it stays visible in its own window.

### What's remembered

Ng DAW remembers, between runs: the main window's size and position; whether the browser is shown
and its width; the size of the timeline and rack; which panels are detached and where their windows
are; the timeline's Snap setting; the piano roll's Ghosts, Key and Scale; Dock buses; and whether
the typing keyboard is on.

**View > Reset the layout** attaches every panel, shows the browser, and puts the bars back to their
defaults.

## The command palette

Press **Ctrl+Shift+P** (or **Help > Command palette**) to search every app action by name.

1. Type a few words. Actions whose name starts with what you typed come first, then those with every
   word somewhere in their name, description or keys.
2. Use **Up** / **Down** (or **Page Up** / **Page Down**) to pick one. Each row shows the action's
   keyboard shortcut, so the palette also teaches you the shortcuts. Actions that are switched on
   (Loop, Metronome, Browser, ...) are ticked.
3. Press **Enter**, or click it, to run it.

Actions that can't run right now (Undo with nothing to undo, say) show dimmed. **Esc**, or clicking
elsewhere, closes the palette.

The palette covers the app's actions (the ones in the menus and the [shortcuts list](keyboard-shortcuts.md)).
For the full command layer, see the [command console](#the-command-console) and the
[command reference](../commands.md).

## The keyboard shortcuts list

**F1**, the **?** button in the top bar, or **Help > Keyboard shortcuts** shows every shortcut in two
columns: the app-wide ones and the typing keyboard on the left, the timeline's and piano roll's on
the right. Typing **?** also opens it while the typing keyboard is off. See also
[Keyboard shortcuts](keyboard-shortcuts.md).

## History

**Ctrl+H**, the **History** button, or **Edit > History...** opens the undo history: every change
since the project was opened, oldest at the top.

- Steps that were undone (and can be redone) are greyed out below the others.
- **Click a step** to undo, or redo, back to just after it.
- The first row, **Opened**, goes back to how the project was when it was opened.
- The list updates while it's open, so you can watch changes (yours or an AI's) arrive.

Each step is named after what it did, for example *Create track*, *Paint clips* or *Fader*. A batch
of commands, such as one request from Claude, is one step.

## The command console

Click the **Console** button in the top bar (or **View > Command console**) to show the command
console along the bottom of the window. It runs the same JSON commands the MCP server sends.

- Type a command, or a batch of commands, in the input box. It starts with `{"command": "get_state"}`.
- Click **Run**. The result (or the error) shows below as JSON.
- Each command or batch you run is one undo step, and shows in the AI command log as from *Console*.

For example:

```json
{"command": "route", "params": {"channel": "Synth 2", "strip": "Synth 1"}}
```

```json
{"batch": "Two synths", "commands": [
  {"command": "create_track", "params": {"name": "Bass"}},
  {"command": "create_track", "params": {"name": "Lead"}}]}
```

See the [command reference](../commands.md) for every command and its parameters.

## The AI command log

The **AI command log** is on the right of the console. It shows what AI clients (such as Claude,
through the [MCP server](../mcp.md)) and the console did to the project, newest first.

- The first time an AI client changes the project, the console opens by itself, so you can see the
  log.
- Each entry shows the time, who sent it, and what it was (a command, or a batch with its number of
  commands). Failed entries are in orange-red with the error. Hover over an entry to see its
  commands, or the full error.
- **Undo** on an entry takes back that whole command or batch, as one step. Only the newest change
  can be undone this way; for an older entry, undo what came after it first.
- **Clear** empties the log. The changes stay, and Undo (Ctrl+Z) still works.

To connect Claude, see [Settings > AI](#ai) and the [MCP guide](../mcp.md).

## The CPU meter

**CPU** in the top bar shows how much of each audio buffer the engine needs to fill it, updated
twice a second.

| Colour | Meaning |
| --- | --- |
| Grey | Fine. |
| Amber | Over 50%. |
| Red | Over 80%, or the audio dropped out since the last update. |

Hover over it for the audio device's name, sample rate, buffer size and output latency. When the CPU
is nearly full or the audio drops out, the tooltip suggests the next larger buffer size to try in
**Settings > Audio and MIDI** (or freezing or removing heavy plugins). Otherwise it reminds you that
a larger buffer gives the CPU more room and a smaller one makes playing live feel tighter.

## Settings

**Ctrl+,**, the **Settings** (gear) button, or **Options > Settings...** opens the Settings window.
It has three tabs. **Esc** closes it.

### Audio and MIDI

The standard device settings:

- Audio device type and output device (and input device, for recording audio).
- Output and input channels (up to two each).
- Sample rate and buffer size.
- MIDI inputs to listen to.

See [Playing and recording](playing-and-recording.md) and [the CPU meter](#the-cpu-meter) for
choosing a buffer size.

### General

| Setting | What it does |
| --- | --- |
| **Theme** | The colour theme: **Dark** (the default), **Midnight** or **Grey**. It changes every window straight away. |
| **Project folder** | Where new projects are saved, and where recordings and exports go until a project is saved. **Choose...** picks a folder; **Reset** goes back to `Music/Ng DAW`. |
| **Autosave** | How often unsaved changes are saved for crash recovery: **Off**, **Every minute** (the default), or every **2**, **5** or **10 minutes**. See [Autosave and crash recovery](getting-started.md#autosave-and-crash-recovery). |

### AI

Shows how to connect Claude to Ng DAW on this computer, with the path to `NgDawMcp` filled in:

- the `claude mcp add ng-daw -- "..."` command for **Claude Code**, with a **Copy Claude Code
  command** button;
- the `mcpServers` entry for **Claude Desktop**'s `claude_desktop_config.json`, with a **Copy Claude
  Desktop settings** button.

Ng DAW must be running for Claude to use it. Each change Claude makes goes in the command log and the
undo history. See the [MCP guide](../mcp.md) for details.

## The typing keyboard and on-screen piano

The **typing keyboard** (Ctrl+K) plays the armed channel from the computer keyboard, and the
**on-screen piano** (Ctrl+Shift+K) shows a piano along the bottom of the window. Both are described
in [Playing and recording](playing-and-recording.md#the-typing-keyboard). Remember that while the
typing keyboard is on, the note keys don't run their single-key shortcuts.

## Plugins

**Options > Plugins...** opens the Plugins window, for scanning VST3 plugins. See
[Channel rack and mixer](channel-rack-and-mixer.md#plugins).

## About

**Help > About Ng DAW** shows the version.
