# Keyboard shortcuts and mouse gestures

**Ctrl** is Cmd on macOS. Press **F1** in the app for the same list, or **Ctrl+Shift+P** to search
actions by name.

> **The typing keyboard is on by default.** While it's on, the keys that play notes
> (`Z S X D C V G B H N J M , L . ; /` and `Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P [ ]`) play the armed
> channel instead of running a shortcut. That affects **B, C, D, T** (timeline tools), **S** (split),
> **R** (record) and **Q** (quantize). Press **Ctrl+K** to turn it off. Shortcuts with Ctrl or Alt,
> and keys such as Space, Home, Enter, Del, Esc, the arrows, F1 and F2, always work.

## Everywhere

| Shortcut | Action |
| --- | --- |
| Space | Play or stop |
| Home | Back to the start |
| R | Record into the armed channels; again to stop |
| Ctrl+M | Metronome on or off |
| Ctrl+L | Looping on or off |
| Ctrl+Z | Undo |
| Ctrl+Y, Ctrl+Shift+Z | Redo |
| Ctrl+N | New project |
| Ctrl+O | Open a project |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save as |
| Ctrl+T | Add an instrument |
| Ctrl+I | Import audio files |
| Ctrl+E | Export the song, the loop or stems |
| Ctrl+K | Typing keyboard on or off |
| Ctrl+Shift+K | Show or hide the on-screen piano |
| Ctrl+B | Show or hide the browser |
| Ctrl+H | Undo history |
| Ctrl+, | Settings |
| F1 (or ?) | Keyboard shortcuts list |
| Ctrl+Shift+P | Command palette |
| Esc | Close the piano roll or sample editor (in the piano roll, deselects notes first) |

These actions have no shortcut but are in the menus and the command palette: Add audio channel, Add
bus, Command console, Plugins..., About Ng DAW, Quit.

## Typing keyboard

With the typing keyboard on (Ctrl+K), these keys play the armed channel:

| Keys | Notes |
| --- | --- |
| Z S X D C V G B H N J M | C4 (middle C) to B4 |
| , L . ; / | C5 to E5 |
| Q 2 W 3 E R 5 T 6 Y 7 U | C5 to B5 |
| I 9 O 0 P [ ] | C6 to F#6 |

## Timeline

### Keys

Click in the timeline first so it has the keyboard focus.

| Shortcut | Action |
| --- | --- |
| B | Paint tool |
| C | Slice tool |
| D | Delete tool |
| T | Mute tool |
| Enter | Open the selected clip (piano roll or sample editor) |
| F2 | Rename the selected clip |
| Del, Backspace | Delete the selected clips |
| Ctrl+D | Duplicate the selection after itself |
| Ctrl+C | Copy the selected clips |
| Ctrl+V | Paste the copied clips at the playhead |
| S | Split the selected clips at the playhead |
| Left, Right | Move the selection by a beat |
| Shift+Left, Shift+Right | Move the selection by a bar |
| Up, Down | Move the selection to the channel above or below |
| Ctrl+A | Select every clip |
| Esc | Deselect |

### Mouse: clips (Paint tool)

| Gesture | Action |
| --- | --- |
| Click a clip | Pick it (it becomes what clicks on its lane paint) |
| Click an empty spot on a lane | Paint a copy of the lane's clip (or a new one-bar clip on an empty instrument lane) |
| Click and drag from an empty spot | Paint copies one after another |
| Shift+click an empty spot | New empty one-bar MIDI clip |
| Drag a clip | Move it (onto another lane too) |
| Drag a clip's edge | Trim it |
| Shift+drag an audio clip's right edge | Stretch it |
| Drag the handles in an audio clip's top corners | Fade in or out |
| Alt while dragging | Place freely, off the snap grid |
| Double-click a clip | Open it (piano roll or sample editor) |
| Double-click an empty spot on an audio lane | Pick audio files to put there |
| Click the ▾ on a clip, or Shift+right-click it | Clip menu |
| Right-click a clip | Delete it (any tool) |
| Right-drag across clips | Delete them all (any tool) |
| Ctrl+click a clip | Add it to the selection, or take it out |
| Ctrl+drag on the lanes | Select the clips in a box |
| Ctrl+click an empty spot | Zoom to fit the whole song |

### Mouse: other tools

| Tool | Click | Drag |
| --- | --- | --- |
| Slice (C) | Cut the clip in two there (Alt: off the grid) | |
| Delete (D) | Delete the clip | Delete every clip crossed |
| Mute (T) | Mute or unmute the clip | Mute or unmute every clip crossed |

### Mouse: lane headers

| Gesture | Action |
| --- | --- |
| Click a channel's name | Arm it (the keys play it) |
| Double-click a channel's name | Rename it |
| Right-click a channel's name | Channel menu: rename, colour, automation, delete |
| Click **A** | Show or hide the automation lane |
| Drag a lane's bottom edge | Size just that lane |
| Double-click a lane's bottom edge | Reset that lane's height |

### Mouse: ruler and loop

| Gesture | Action |
| --- | --- |
| Click the ruler | Move the playhead (to the nearest beat) |
| Drag in the ruler, or Shift+drag | Draw a new loop region (turns looping on) |
| Drag the loop's start or end | Change the loop |
| Right-click the ruler | Loop this bar, loop the whole project, looping on/off |

### Mouse: automation lanes

| Gesture | Action |
| --- | --- |
| Click | Add a point |
| Drag a point | Move it |
| Double-click or right-click a point | Remove it |
| Right-click the lane, or click its name | Choose the parameter, remove the points, hide the lane |

### Zoom and scroll

| Gesture | Action |
| --- | --- |
| Ctrl + wheel, or wheel over the ruler | Zoom in time |
| Alt + wheel | Taller or shorter lanes |
| Shift + wheel, or sideways scroll | Scroll through time |
| Wheel | Scroll the lanes |
| Ctrl + left click | Fit the whole song in view |

## Piano roll

### Keys

| Shortcut | Action |
| --- | --- |
| Ctrl+A | Select every note |
| Del, Backspace | Delete the selected notes |
| Ctrl+C | Copy the selected notes |
| Ctrl+X | Cut the selected notes |
| Ctrl+V | Paste at the playhead |
| Ctrl+D | Duplicate the selected notes after themselves |
| Up, Down | Move by a semitone |
| Shift+Up, Shift+Down | Move by an octave |
| Left, Right | Move by the snap grid |
| Q | Quantize the selected notes (or all) |
| Esc | Deselect; with nothing selected, close the piano roll |

### Mouse

| Gesture | Action |
| --- | --- |
| Click an empty spot | Draw a note (as long as the last one) |
| Click and drag from an empty spot | Draw notes along the way |
| Drag a note | Move it (and the selection) |
| Drag a note's right edge | Resize it |
| Right-click a note | Delete it |
| Right-drag | Delete every note crossed |
| Ctrl+click a note | Add to or remove from the selection |
| Ctrl+drag | Select with a box |
| Alt while drawing or dragging | Place freely, off the grid |
| Click or drag in the velocity lane | Set the velocity of the notes there |
| Click the keyboard on the left | Play the key (further right is louder) |
| Click the ruler | Move the playhead |
| Wheel | Scroll pitches |
| Shift + wheel | Scroll through time |
| Ctrl + wheel | Zoom in time |
| Alt + wheel | Taller or shorter rows |

## Channel rack

| Gesture | Action |
| --- | --- |
| Click a step | Turn it on or off |
| Drag across steps | Paint them on (or off, if the first was on) |
| Right-click or right-drag steps | Turn them off |
| Double-click a channel's name | Rename it |
| Right-click a channel's row | Save its instrument as a preset |
| Click the colour chip | Recolour |
| Click **●** | Arm for input and recording |
| Drop an instrument, preset, samples or a folder | New channel (on the rack) or change the channel (on a row) |

## Mixer

| Gesture | Action |
| --- | --- |
| Double-click a strip's name | Rename it |
| Drag a strip's name onto another strip | Move the strip there |
| Click the inputs label | Choose where the strip plays (master or another strip) |
| Double-click the fader | 0 dB |
| Double-click the pan knob | Centre |
| Click an effect | Open its window |
| Right-click an effect | Open, bypass, move, save as preset, remove |
| Drag across a send | Set its level |
| Double-click a send | 0 dB |
| Right-click a send | Pre-fader, 0 dB, remove |
| Drop an effect or effect preset on a strip | Add it |

## Sample editor

| Gesture | Action |
| --- | --- |
| Click or drag in the lit part | Move the playhead there |
| Drag the lit part's edges | Trim the clip (Alt: off the 16th grid) |
| Double-click a knob | Reset it |
| Double-click File tempo or Length | Type a value |
| Esc | Close the sample editor |

## Sampler window

| Gesture | Action |
| --- | --- |
| Double-click a sample's name | Rename it |
| Drag the Key arrows up and down | Change its key |
| Double-click Level | 0 dB |
| Drop audio files or a folder | Add them |

## Browser

| Gesture | Action |
| --- | --- |
| Drag an item | Drop it on the rack, a channel, a mixer strip or the timeline |
| Double-click an item | New channel from it (a loop goes on the timeline) |
| Right-click an item | New channel, remove folder, show in folder, delete preset, refresh |
| Esc in the search box | Clear the search |

## Panels and dialogs

| Gesture | Action |
| --- | --- |
| Drag a bar between panels | Resize the panels |
| Double-click a bar | Reset it |
| Double-click a panel's header, or click its detach button | Detach it into its own window, or put it back |
| Close a detached panel's window | Put it back in the main window |
| Up, Down, Page Up, Page Down (palette) | Pick an action |
| Enter (palette) | Run it |
| Esc (palette, Settings, History, Export) | Close |
