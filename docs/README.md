# Ng DAW documentation

## Using Ng DAW

The [user guide](user-guide/README.md) covers the app page by page:

- [Getting started](user-guide/getting-started.md): installing, the main window, project files, autosave and recovery
- [Playing and recording](user-guide/playing-and-recording.md): transport, arming, MIDI keyboards, the typing keyboard, recording
- [Timeline](user-guide/timeline.md): lanes, clips, tools, loop region, zoom, automation lanes
- [Piano roll](user-guide/piano-roll.md): drawing and editing notes, snap, quantize, velocity, scales
- [Channel rack and mixer](user-guide/channel-rack-and-mixer.md): instruments, patterns, mixer strips, buses, sends, effects, plugins
- [Samples](user-guide/samples.md): the browser, sample folders, the sample editor and the sampler
- [Export](user-guide/export.md): rendering the song, the loop or stems to WAV/MP3
- [Panels and preferences](user-guide/panels-and-preferences.md): docking, command palette, history, AI command log, settings
- [Keyboard shortcuts](user-guide/keyboard-shortcuts.md): every key and mouse gesture

## AI and the command layer

- [Commands](commands.md): reference for every command (parameters, results, undo behaviour)
- [MCP server](mcp.md): connecting Claude Code / Claude Desktop to Ng DAW through `NgDawMcp`

## Developing

- [Architecture](architecture.md): targets, `Session`, the command registry and dispatcher, the bridge, threading, undo
- [Project format](project-format.md): `.ngdaw` files, presets, autosave files, `bridge.json`, settings locations
- [Extending](extending.md): adding a command or a UI panel, and code conventions
- [Building](building.md): prerequisites, CMake targets and options, caching, troubleshooting
- [Testing](testing.md): the test runner, what the tests cover, the test plugins
- [Releasing](release.md): CI, the release workflow and the Windows installer
