# The MCP server (NgDawMcp)

`NgDawMcp` is a [Model Context Protocol](https://modelcontextprotocol.io) server that lets
Claude (Claude Code, Claude Desktop) or any other MCP client work on the project open in Ng DAW.
Every app command is an MCP tool, plus `batch`, `undo` and `redo`. A tool call changes the
running app's project the same way a button does: it goes through the app's command dispatcher,
appears in the app's AI command log, and is one step in the undo history.

Ask Claude to "add a bass line following the chords and put it on a bass bus with compression"
and it happens in the open project, as one undo step you can take back.

This page covers how the server connects to the app, how to set it up in Claude, what it
exposes, and what to do when it doesn't work. The commands themselves are listed in
[commands.md](commands.md).

## Contents

- [How it fits together](#how-it-fits-together)
- [Installing and finding NgDawMcp](#installing-and-finding-ngdawmcp)
- [Setting up Claude Code](#setting-up-claude-code)
- [Setting up Claude Desktop](#setting-up-claude-desktop)
- [Other MCP clients](#other-mcp-clients)
- [What the server exposes](#what-the-server-exposes)
- [The bridge: how NgDawMcp talks to the app](#the-bridge-how-ngdawmcp-talks-to-the-app)
- [When Ng DAW isn't running](#when-ng-daw-isnt-running)
- [Security](#security)
- [Changes in the "enhance mcp" update](#changes-in-the-enhance-mcp-update)
- [Troubleshooting](#troubleshooting)
- [Source files](#source-files)

## How it fits together

```
 Claude Code / Claude Desktop                NgDawMcp                       Ng DAW (the app)
 ┌──────────────────────┐   stdio, JSON-RPC  ┌──────────────┐   TCP on       ┌──────────────────────────┐
 │ MCP client           │ ─────────────────▶ │ McpServer    │  127.0.0.1     │ BridgeServer             │
 │ (starts NgDawMcp     │ ◀───────────────── │ BridgeClient │ ─────────────▶ │   → CommandDispatcher    │
 │  as a child process) │  one message/line  └──────────────┘  port + token  │   → the open project     │
 └──────────────────────┘                          ▲          from bridge.json └──────────────────────────┘
                                                   │                                    │
                                                   └──────── reads bridge.json ◀────────┘ writes it at startup
```

- The MCP client starts `NgDawMcp` as a child process and talks to it over **stdin/stdout**:
  JSON-RPC 2.0 messages, one per line. NgDawMcp writes nothing else to stdout.
- **NgDawMcp doesn't hold a project.** It is a thin relay: it answers `initialize` and
  `tools/list` itself, and passes each `tools/call` to the running app.
- The app listens on a **local TCP port** (127.0.0.1 only, a port the OS picks) and writes the
  port and a secret token to a **connection file**, `bridge.json`. NgDawMcp reads that file,
  connects, and sends each call with the token.
- In the app, each request runs on the message thread through the same `CommandDispatcher`
  the UI uses, so it is checked, logged and undoable like any other change.

There is one server per client session and one app on the other end: whichever copy of Ng DAW
started most recently (see [Several copies of the app](#several-copies-of-the-app)).

## Installing and finding NgDawMcp

The Windows installer (`NgDaw-<version>-Windows-Setup.exe`, built from
`packaging/windows/NgDaw.iss`) puts `NgDawMcp.exe` in the same folder as `Ng DAW.exe`. That
folder is `{autopf}\Ng DAW`, which depends on the install mode the installer's first dialog
offers:

| Install mode | Path of the server |
| --- | --- |
| For all users (needs administrator rights) | `C:\Program Files\Ng DAW\NgDawMcp.exe` |
| For me only | `%LOCALAPPDATA%\Programs\Ng DAW\NgDawMcp.exe` (e.g. `C:\Users\<you>\AppData\Local\Programs\Ng DAW\NgDawMcp.exe`) |

or whatever folder you picked during setup. Nothing else needs installing: the server is a
single console program.

Built from source (see the README's *Building*), the server is the `NgDawMcp` target:

| Platform | Path |
| --- | --- |
| Windows | `build\NgDawMcp_artefacts\<Debug or Release>\NgDawMcp.exe` |
| macOS, Linux | `build/NgDawMcp_artefacts/<Debug or Release>/NgDawMcp` |

**The easiest way to get the right path is in the app:** *Settings* (Ctrl+,) > *AI* shows the
exact Claude Code command and Claude Desktop settings for this computer, with *Copy* buttons. It
looks for the server beside the app first, then in the build tree next to it.

## Setting up Claude Code

Add the server once, with Ng DAW's path (use the one *Settings* > *AI* shows):

```sh
claude mcp add ng-daw -- "C:\Program Files\Ng DAW\NgDawMcp.exe"
```

For a per-user install:

```sh
claude mcp add ng-daw -- "C:\Users\<you>\AppData\Local\Programs\Ng DAW\NgDawMcp.exe"
```

Built from source on macOS or Linux:

```sh
claude mcp add ng-daw -- /path/to/ng-audio/build/NgDawMcp_artefacts/Release/NgDawMcp
```

By default `claude mcp add` registers the server for the current project folder only. Add
`--scope user` to make it available in every folder:

```sh
claude mcp add --scope user ng-daw -- "C:\Program Files\Ng DAW\NgDawMcp.exe"
```

To share the setup with a repository instead, put it in that repository's `.mcp.json`:

```json
{
  "mcpServers": {
    "ng-daw": {
      "command": "C:\\Program Files\\Ng DAW\\NgDawMcp.exe"
    }
  }
}
```

Then, with Ng DAW open, start Claude Code and check the connection with `/mcp`; `ng-daw`
should be listed as connected with its tools. A good first request is "look at my Ng DAW
project", which makes Claude call `get_state`.

The server takes no arguments. To point it (and the app) at a different connection file, set
`NG_DAW_BRIDGE_FILE` (see [The connection file](#the-connection-file)), e.g.
`claude mcp add -e NG_DAW_BRIDGE_FILE=D:\tmp\bridge.json ng-daw -- "C:\Program Files\Ng DAW\NgDawMcp.exe"`.

## Setting up Claude Desktop

Add the server to Claude Desktop's `claude_desktop_config.json` (in Claude Desktop:
*Settings* > *Developer* > *Edit Config*). The file lives at:

| Platform | Location |
| --- | --- |
| Windows | `%APPDATA%\Claude\claude_desktop_config.json` |
| macOS | `~/Library/Application Support/Claude/claude_desktop_config.json` |

Windows, installed for all users (note the doubled backslashes JSON needs):

```json
{
  "mcpServers": {
    "ng-daw": {
      "command": "C:\\Program Files\\Ng DAW\\NgDawMcp.exe"
    }
  }
}
```

macOS, built from source:

```json
{
  "mcpServers": {
    "ng-daw": {
      "command": "/path/to/ng-audio/build/NgDawMcp_artefacts/Release/NgDawMcp"
    }
  }
}
```

If the file already has other servers, add `"ng-daw"` inside the existing `mcpServers` object.
Quit Claude Desktop completely and start it again to load the change. The Ng DAW tools then show
in the chat's tools menu.

## Other MCP clients

Any client that can start a stdio MCP server works: run `NgDawMcp` with no arguments and speak
MCP over its stdin and stdout. The server needs no environment variables, network access or
working directory of its own.

## What the server exposes

### Protocol

`src/core/McpServer.cpp` implements the server side of MCP over stdio:

| Method | Behaviour |
| --- | --- |
| `initialize` | Answers with the protocol version, `capabilities: {"tools": {}}`, `serverInfo` (`name: "ng-daw"`, `title: "Ng DAW"`, `version`: the app's version) and `instructions` (below). The supported versions are `2024-11-05`, `2025-03-26` and `2025-06-18`; any other requested version is answered with `2025-06-18`. Works without the app running |
| `ping` | Answers `{}` |
| `tools/list` | Lists every tool, in one page (no pagination). Works without the app running |
| `tools/call` | Runs a tool in the app (see [Tool results](#tool-results)) |
| Notifications (messages without an `id`, such as `notifications/initialized`) | Ignored, with no reply |
| Anything else | JSON-RPC error `-32601`, `Unknown method: <method>` |
| A line that isn't a JSON object | JSON-RPC error `-32700`, `Parse error` |

Only **tools** are offered. The server declares no resources and no prompts, so
`resources/list`, `prompts/list` and the like are answered with `-32601`. The project is read
with the `get_state` tool instead of a resource.

The `instructions` sent at `initialize`, which clients such as Claude pass to the model, are:

> Ng DAW is a music production app (a DAW). These tools change the project open in it, which
> must be running. Start with get_state (view "summary" for a quick overview) to see the
> channels (instruments and audio tracks, holding clips) and mixer strips (faders and effects);
> ids from it work wherever a channel, strip or clip is asked for, and so do names. Times are in
> beats from the start of the song. Use batch to make a change of several commands as one undo
> step, referring to earlier results with "$<n>.<field>". Each tool call or batch is one step in
> the app's undo history and appears in its command log, where the person can undo it.

### Tools

`tools/list` returns **78 tools**: the 75 app commands and three of the server's own.

| Tools | Maps to |
| --- | --- |
| Every command in [commands.md](commands.md), under the same name (`create_track`, `get_state`, `export_audio`...) | The command of that name. The tool's `inputSchema` is the command's parameter schema, its `description` the command's description, and the call's `arguments` are passed as the command's `params` |
| `batch` | `CommandDispatcher::executeBatch()`: `{"label": ..., "commands": [{"command": ..., "params": {...}}, ...]}` runs the commands as one undo step, with `"$<n>.<field>"` references to earlier results. An empty `label` becomes `AI changes`. See [Batches](commands.md#batches-undo-and-redo) |
| `undo` | `CommandDispatcher::undo()`: undoes the newest change, whoever made it. No arguments |
| `redo` | `CommandDispatcher::redo()`. No arguments |

Each schema is `{"type": "object", "properties": {...}, "required": [...]}` with a type and a
description for every property. Allowed values and ranges are in the descriptions; the app
checks them and says what's wrong.

### Tool results

A `tools/call` always succeeds at the JSON-RPC level (as MCP asks); whether the tool worked is
in the result:

```json
{"content": [{"type": "text", "text": "{\"channel\": \"1043\", \"strip\": \"1044\"}"}], "isError": false}
```

- On success, `text` is the command's result as JSON on one line, or `Done.` when it has none
  (`play`, `stop`, `undo`...).
- On failure, `isError` is true and `text` is the error: the command's own message (e.g.
  `set_tempo: bpm must be a number from 20 to 300`), a batch's
  `Batch '<label>' rolled back. ...`, `Unknown tool: <name>`, or a connection problem such as
  `Ng DAW isn't running. Open it, then try again.`
- Arguments that aren't an object are treated as `{}`.

### Who made the change

The server tells the app who sent each call, and the app's AI command log shows it. The name
comes from the client's `clientInfo.name` at `initialize`: anything containing "claude" (in any
case) is logged as **Claude**, any other client under its own name, and a client that gives no
name as **AI**. Each log entry has *Undo*, which takes back that whole command or batch while it
is still the newest change. Queries (`get_*`, `list_*`, `find_*`) aren't logged.

### An example exchange

What a client and NgDawMcp send each other (one JSON object per line; shortened, with
made-up ids):

```json
→ {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","clientInfo":{"name":"claude-code","version":"2.0"},"capabilities":{}}}
← {"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18","capabilities":{"tools":{}},"serverInfo":{"name":"ng-daw","title":"Ng DAW","version":"1.0.0"},"instructions":"Ng DAW is a music production app..."}}
→ {"jsonrpc":"2.0","method":"notifications/initialized"}
→ {"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"set_tempo","arguments":{"bpm":96}}}
← {"jsonrpc":"2.0","id":2,"result":{"content":[{"type":"text","text":"{\"bpm\":96.0}"}],"isError":false}}
→ {"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"batch","arguments":{"label":"Bass line","commands":[{"command":"create_track","params":{"name":"Bass","sound":"Round Bass"}},{"command":"add_midi_clip","params":{"channel":"$0.channel","length_beats":8,"chords":[{"chord":"Am"},{"chord":"F","start":4}],"chord_style":"bass_root_octave","chord_octave":2}}]}}}
← {"jsonrpc":"2.0","id":3,"result":{"content":[{"type":"text","text":"[{\"channel\":\"1043\",\"strip\":\"1044\"},{\"clip\":\"1046\",\"notes\":16}]"}],"isError":false}}
```

You can try this by hand: start Ng DAW, run `NgDawMcp` in a terminal, and paste lines into it.

## The bridge: how NgDawMcp talks to the app

The bridge (`src/core/Bridge.h`, `Bridge.cpp`) is the app's local socket for programs outside
it. NgDawMcp is its only client today, but anything that follows the protocol below can use it.

### The connection file

When the app starts, it opens a listening socket on **127.0.0.1** at a port the OS assigns
(different every run) and writes the connection file:

```json
{"port": 53127, "token": "9f2c...e41a", "pid": 18324}
```

- `token` is 64 hex digits, new every run.
- `pid` is the app's process id (informational).

The file's location:

| Platform | Default path |
| --- | --- |
| Windows | `%APPDATA%\Ng DAW\bridge.json` (e.g. `C:\Users\<you>\AppData\Roaming\Ng DAW\bridge.json`) |
| macOS | `~/Library/Application Support/Ng DAW/bridge.json` |
| Linux | `~/.config/Ng DAW/bridge.json` |

The environment variable **`NG_DAW_BRIDGE_FILE`** overrides the path for both ends (a relative
path is taken from the process's working directory). Set it the same way for the app and for
NgDawMcp, e.g. to run a second, separate copy of the app for testing.

When the app quits, it deletes the file, but only if the file is still its own (the token
matches), so it never removes the file of another copy that started after it.

### Several copies of the app

Each copy of Ng DAW that starts overwrites `bridge.json` with its own port and token, so
NgDawMcp connects to **the copy that started most recently**. If that copy quits, the file is
removed and NgDawMcp reports that Ng DAW isn't running, even if an older copy is still open;
restart the copy you want to use (or give the copies different `NG_DAW_BRIDGE_FILE`s).

### Requests

NgDawMcp reads the connection file when it first needs the app (at the first `tools/call`),
connects, and keeps the connection open for later calls. Each request is one JSON object:

```json
{"token": "...", "source": "Claude", "request": {"command": "set_tempo", "params": {"bpm": 96}}}
{"token": "...", "source": "Claude", "request": {"batch": "Bass line", "commands": [...]}}
{"token": "...", "source": "Claude", "undo": true}
{"token": "...", "source": "Claude", "redo": true}
{"token": "...", "describe": true}
```

and the reply is the dispatcher's `{"ok": true, "result": ...}` or `{"ok": false, "error": "..."}`.
`describe` returns the app's commands in `tools/list` shape (name, description, inputSchema).
`source` is who the command log names (default `AI`).

Messages are framed with JUCE's `InterprocessConnection`: each one is a 4-byte header
`0x4e674457` ("NgDW"), a 4-byte length (both little-endian 32-bit), then the UTF-8 JSON. A
connection with a different header is ignored, so other JUCE apps can't talk to it by accident.

Replies to bad requests:

| Request | Reply error |
| --- | --- |
| Not a JSON object | `The request isn't a JSON object` |
| Missing or wrong token | `Wrong token: read it again from the connection file` |
| No `request` object (and not undo, redo or describe) | `Give a request: {"command": ..., "params": ...} or {"batch": ..., "commands": [...]}` |

### Timing

- NgDawMcp sends **one request at a time** and waits for its reply.
- Connecting gives up after 3 seconds.
- A request may take up to **10 minutes** (an export or `analyze_mix` of a long song, or
  `scan_plugins` over many plugins) before NgDawMcp gives up with `Ng DAW didn't answer in time`.
- Commands run on the app's message thread, so the app's window is busy while a long command
  runs, as it is when you start the same action by hand.

## When Ng DAW isn't running

NgDawMcp starts and answers `initialize` and `tools/list` whether or not the app is open: the
tool list is built into NgDawMcp (it is compiled from the same command registry as the app), so
Claude sees the tools straight away. Only `tools/call` needs the app.

| Situation | What a tool call returns (`isError: true`) |
| --- | --- |
| The app isn't running (no connection file) | `Ng DAW isn't running. Open it, then try again.` |
| A connection file is there but nothing answers on its port (the app crashed, or is still starting) | `Ng DAW isn't running, or isn't answering. Open it, then try again.` |
| The app quit or restarted since the last call | NgDawMcp reconnects (reading the new port and token) and sends the call again, once |
| The app closed the connection after the request was sent | `Ng DAW closed the connection before answering`. The request is **not** sent again, because the app may already have run it: check with `get_state` or `get_history` |
| The reply took more than 10 minutes | `Ng DAW didn't answer in time` |

So you can start Claude first and open Ng DAW later: the next tool call connects. There is no
need to restart Claude or NgDawMcp after restarting the app.

## Security

- The app listens on **127.0.0.1 only**, so other computers can't reach it.
- Every request must carry the **token** from the connection file, which changes every run.
- On macOS and Linux the connection file is created readable and writable by your user only
  (mode 600) before the token is written into it. On Windows it lives in your profile's
  `AppData\Roaming` folder, which other standard users can't read.
- Anyone who can run programs as you can read the file and drive the app. Treat MCP access like
  any other tool you let Claude use: the tools can delete channels, overwrite files with
  `save_project` and `export_audio`, and change app settings (`set_library`, `scan_plugins`).
  Project edits can be undone; file writes can't.
- NgDawMcp makes no network connections beyond the local socket and needs no API key. The model
  runs in your MCP client (Claude Code or Claude Desktop), under that client's account.

## Changes in the "enhance mcp" update

The commit `d4398de` ("enhance mcp") made MCP the way AI works with Ng DAW and gave the tools
more reach. The MCP protocol code (`McpServer`, `Bridge`, `McpMain`) itself only had its line
endings changed; what changed is around it:

- **The built-in AI chat panel was removed** (`AiChat`, `ChatPanel`, and the provider and API
  key settings for Claude, OpenAI, Gemini, OpenRouter, Ollama and LM Studio). AI clients now
  connect through NgDawMcp, and *Settings* > *AI* only shows how to connect them. libcurl is no
  longer needed to build on Linux.
- **New tools:**
  - `list_sounds`, and a `sound` parameter on `create_track` and `set_instrument`: eight
    starter sounds for the built-in 4osc synth (`Soft Bell Lead`, `Square Lead`, `Round Bass`,
    `Sub Bass`, `Pluck`, `Warm Pad`, `Electric Keys`, `Organ`).
  - `get_history`: the undo and redo history, so a model can check what changed.
  - `analyze_mix`: renders the loop or song offline and measures the master and every strip's
    stem (peak, RMS, LUFS, clipping) with warnings, so a mix can be checked without listening.
- **Writing notes is shorter:** `add_midi_clip` and `set_notes` take note names (`"C4"`,
  `"F#3"`), `chords` written out in a `chord_style` (block, arpeggios, Alberti, bass lines), and
  `repeat`. `set_notes` no longer requires `notes` (`chords` will do).
- **Instrument sound design:** `set_effect`, `get_effect` and `show_effect_window` take
  `channel` instead of `strip` and `effect` to work on a channel's instrument (the 4osc synth
  or a plugin), and `set_automation` and `get_automation` can automate its parameters. `strip`
  is no longer a required parameter of these commands. `get_state` lists a channel's automated
  instrument parameters.
- **Shorter results:** `get_effect` takes a `filter` to list only matching parameters (a big
  plugin has hundreds), and `set_effect` returns only the parameters it changed.
- **Undo for built-in instruments:** `set_effect` now records every parameter change through its
  own undo action, so undoing a change to the 4osc synth's parameters works.
- **`analyze_audio`** reports `rms_db`, `loudness_lufs` and `clipped_samples` besides `peak_db`.

## Troubleshooting

**Every tool call says "Ng DAW isn't running".**
Open Ng DAW. If it is open, check that the connection file exists
(`%APPDATA%\Ng DAW\bridge.json` on Windows). If it doesn't, the app couldn't open its socket or
write the file at startup; that failure is only reported in debug builds' output, so restart
the app and check that the folder is writable and that security software isn't blocking local
connections. If you set `NG_DAW_BRIDGE_FILE`, make sure the app and NgDawMcp see the same value.

**"Ng DAW isn't running, or isn't answering".**
The connection file is left over from an app that crashed, or a firewall is blocking connections
to 127.0.0.1. Start Ng DAW again; it writes a fresh file.

**Claude doesn't list the Ng DAW tools.**
The server didn't start. Check the path in your configuration (copy it from *Settings* > *AI*),
remember that JSON needs `\\` for each backslash, and restart Claude Desktop fully after editing
its configuration. In Claude Code, `/mcp` shows the server's status, and `claude mcp list` (or
`claude mcp get ng-daw`) shows how it is configured. Running the configured command by hand in a
terminal should start a program that waits silently for input.

**Changes go to the wrong copy of the app.**
NgDawMcp talks to the copy that started last. Close the others, or restart the one you want.

**A tool call fails with "wait until the export is finished".**
An export or `analyze_mix` is running in the app (perhaps started by hand). Try again when it's
done; `get_state` still works meanwhile.

**"Ng DAW didn't answer in time" or "closed the connection before answering".**
The app was busy for more than 10 minutes, quit, or crashed mid-command. Look at the project (or
ask Claude to call `get_state` or `get_history`) before trying again, since the command may have
run.

**Claude's change was wrong.**
Use *Undo* on its entry in the AI command log, Ctrl+Z in the app, or ask Claude to call `undo`.
Each tool call or batch is one undo step.

**Tools are missing or behave differently from the app.**
NgDawMcp's tool list is compiled into it. If NgDawMcp and the app come from different versions
(say, a rebuilt app with an old NgDawMcp configured), the list may not match what the app can
run: a newer command fails with `Unknown command`, and a schema may be out of date. Use the
`NgDawMcp` that came with your copy of the app.

## Source files

| File | What it does |
| --- | --- |
| `src/mcp/McpMain.cpp` | `NgDawMcp`'s `main()`: reads stdin line by line, passes each to `McpServer`, prints replies |
| `src/core/McpServer.h`, `.cpp` | The MCP protocol: `initialize`, `ping`, `tools/list`, `tools/call`; the `batch`, `undo` and `redo` tools; the instructions |
| `src/core/Bridge.h`, `.cpp` | The connection file, `BridgeServer` (in the app) and `BridgeClient` (in NgDawMcp) |
| `src/core/CommandRegistry.*`, `CommandDispatcher.*` | The commands and how they run, batch and undo (see [commands.md](commands.md)) |
| `src/app/PreferencesPanel.cpp` | *Settings* > *AI*: finds `NgDawMcp` and shows the setup command and configuration |
| `src/app/CommandLogPanel.*` | The AI command log, with *Undo* for each entry |
| `packaging/windows/NgDaw.iss` | The installer, which puts `NgDawMcp.exe` beside `Ng DAW.exe` |
| `tests/CommandTests.cpp` | `testBridgeAndMcpServer` runs the bridge and the MCP server end to end |
