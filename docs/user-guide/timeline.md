# The timeline

The timeline is the arrangement, like FL Studio's playlist. It has a bar ruler along the top and one
lane per channel, each showing that channel's clips. The playhead runs over it all, and the loop
region shows in the ruler.

- **MIDI clips** show a small picture of their notes.
- **Audio clips** show their waveform, scaled by the clip's gain, with their fades.
- **Step pattern clips** are the channel rack's patterns placed on the timeline (see
  [patterns](#step-patterns-on-the-timeline)).
- **Muted clips** stay in place, greyed out, and play nothing.

The clip's name is at the top of the clip, with a small **▾** button that opens its menu.

While the song plays, the view glides along to keep the playhead in sight.

## The header

The timeline panel's header holds:

| Item | What it does |
| --- | --- |
| Hint text | A short reminder of what a click does with the current tool. |
| **Audio** | Opens a menu: **Import audio files...** (Ctrl+I) onto new audio channels at the playhead, or **Empty audio channel**. |
| **Paint**, **Slice**, **Delete**, **Mute** | The [tools](#tools). |
| **Snap** | What clips snap to: **Auto**, **Bar**, **Beat**, **1/8**, **1/16** or **Off**. Remembered between sessions. |
| Taller / shorter lanes | Make every lane taller or shorter (same as Alt + wheel). |
| Zoom in / out | Zoom in time (same as Ctrl + wheel). |
| Detach button | Puts the timeline in its own window. See [arranging the panels](panels-and-preferences.md#arranging-the-panels). |

## Lane headers

The column of names on the left has one entry per channel, in the channel's colour. The armed
channel is highlighted.

| Gesture | What it does |
| --- | --- |
| Click a name | Picks (arms) that channel, so the keyboard and MIDI keyboards play it. |
| Double-click a name | Renames the channel (and its linked mixer strip). |
| Right-click a name | Arms it and opens its menu: **Rename...**, **Colour** (a palette), **Show automation** / **Hide automation**, **Delete channel**. |
| Click the **A** button | Shows or hides the channel's [automation lane](#automation). |
| Drag a lane's bottom edge | Makes just that lane taller or shorter (24 to 400 pixels), for example to see a waveform. |
| Double-click a lane's bottom edge | Puts the lane back to the common height. |

## Tools

Four tools decide what a **left click** on the lanes does, as in FL Studio. Pick one with the
buttons in the header or with its key. **Right-click deletes with any tool.**

| Tool | Key | Left click | Left drag |
| --- | --- | --- | --- |
| **Paint** (default) | B | Picks a clip; on an empty spot, paints a copy of the lane's clip. | Moves or trims a clip; from an empty spot, paints copies one after another. |
| **Slice** | C | Cuts the clip in two where you click, on the snap grid (hold **Alt** to cut off the grid). Step pattern clips can't be sliced. The right-hand half is selected. | |
| **Delete** | D | Deletes the clip. | Deletes every clip you drag across, as one undo step. |
| **Mute** | T | Mutes or unmutes the clip. | Mutes or unmutes every clip you drag across, as one undo step. |

The tool keys don't work while the [typing keyboard](playing-and-recording.md#the-typing-keyboard)
is on, because B, C, D and T play notes. Use the buttons or turn it off with Ctrl+K.

## Working with clips (Paint tool)

### Picking and painting

- **Click a clip** to pick it. It becomes the lane's *brush*.
- **Click an empty spot on the lane** to paint a copy of the brush there. With nothing picked on
  that lane, the brush is the last clip you picked there, or else the lane's last clip.
- **Drag along the lane** from an empty spot to paint copies one after another, each starting where
  the previous one ends. The whole drag is one undo step.
- On an instrument lane with no clips, a click makes a **new empty one-bar MIDI clip**.
  **Shift+click** always makes a new empty one-bar clip, even when the lane has clips.
- Audio lanes don't make empty clips. **Double-click an empty spot** on an audio lane to pick audio
  files to put there, from the start of the bar you clicked.
- Painted copies land on the bar when zoomed out, on the beat at medium zoom, and on the 16th when
  zoomed in far. Hold **Alt** to place them freely.

### Moving, trimming and opening

- **Drag a clip** to move it. You can drag it onto another channel's lane too. MIDI clips stay on
  instrument lanes and audio clips on audio lanes; step pattern clips stay on their own channel.
- **Drag a clip's left or right edge** to trim it. A clip can't be shorter than a 16th of a beat.
  Audio clips can't be trimmed past the end of their file.
- **Shift+drag an audio clip's right edge** to **stretch** it (time-stretch to the new length)
  instead of trimming it, between a quarter and four times its length.
- **Double-click a clip**, or select it and press **Enter**, to open it: a MIDI clip opens in the
  [piano roll](piano-roll.md), an audio clip in the [sample editor](samples.md#the-sample-editor).
  Step pattern clips are edited in the [channel rack](channel-rack-and-mixer.md#step-patterns)
  instead.
- **Right-click a clip** to delete it. **Right-drag** across clips to delete them all, as one undo
  step. This works with every tool.

### Snapping

Moving, trimming, painting, slicing and dropping snap to what **Snap** in the header picks:

| Snap | Grid |
| --- | --- |
| **Auto** (default) | Beats, or 16th notes when zoomed in far enough. |
| **Bar** | The start of the nearest bar (following the time signature). |
| **Beat**, **1/8**, **1/16** | That note value. |
| **Off** | Free placement. |

Hold **Alt** while dragging to place freely, whatever Snap is set to.

### The clip menu

Click the **▾** on a clip's name, or **Shift+right-click** the clip, to open its menu:

| Item | Clips | What it does |
| --- | --- | --- |
| **Edit notes (Enter)** | MIDI | Opens the piano roll. Disabled for step pattern clips. |
| **Edit sample: pitch, stretch, tempo (Enter)** | Audio | Opens the sample editor. Disabled if the file is missing. |
| **Gain** | Audio | +6, +3, 0, -3, -6 or -12 dB. The waveform grows or shrinks with it. |
| **Follow tempo** | Audio | Time-stretches the clip so it keeps its length in beats when the tempo changes. |
| **Remove fades** | Audio | Removes the fade-in and fade-out. |
| **Rename... (F2)** | All | Names the clip apart from its channel. |
| **Colour** | All | A colour of its own, or **Channel's colour** to follow the channel again. |
| **Split at playhead (S)** | Not step patterns | Cuts the clip in two at the playhead. Only enabled when the playhead is inside the clip. |
| **Duplicate (Ctrl+D)** | All | A copy straight after it. |
| **Make unique** | Step patterns | Gives this block its own copy of the pattern. |
| **Copy (Ctrl+C)** | All | Copies it for Ctrl+V. |
| **Delete (Del)** | All | Deletes it. |

### Keys for the selected clips

Click in the timeline first so it has the keyboard focus.

| Key | What it does |
| --- | --- |
| Enter | Open the clip (piano roll or sample editor). |
| F2 | Rename the clip. |
| Del or Backspace | Delete the selected clips. |
| Ctrl+D | Duplicate the selection straight after itself. |
| Ctrl+C | Copy the selected clips. |
| Ctrl+V | Paste the copied clips at the playhead. The earliest goes at the playhead and the rest keep their places after it, each on its own channel. |
| S | Split the selected clips at the playhead. The right-hand halves stay selected, so pressing S again keeps chopping forwards. |
| Left / Right | Move the selection by a beat. |
| Shift+Left / Shift+Right | Move the selection by a bar. |
| Up / Down | Move the selection to the channel above or below. |
| Ctrl+A | Select every clip. |
| Esc | Deselect. |

Each of these is one undo step, even with many clips selected. **S** only works with the typing
keyboard off.

## Selecting several clips

- **Ctrl+click** a clip to add it to the selection, or take it out.
- **Ctrl+drag** across the lanes to select every clip in a box.
- **Ctrl+A** selects every clip.
- **Ctrl+click on an empty spot** (without dragging) zooms to fit the whole song instead.

Dragging any selected clip moves them all, onto other lanes too when every clip fits there. Del,
Ctrl+D, Ctrl+C, Ctrl+V, S and the arrow keys act on the whole selection, as one undo step.

## Step patterns on the timeline

A [pattern](channel-rack-and-mixer.md#patterns) placed on the timeline is a **block**: one clip on
each channel that has steps in that pattern, all starting together.

- Clicking any clip of a block selects the whole block, and switches the channel rack to that
  pattern.
- Moving, copying, duplicating, resizing or deleting any clip of a block does the same to all of it.
- Every copy follows the channel's steps: change the steps in the rack and every copy changes.
- Drag a copy's right edge to resize it. Making it longer adds empty steps to the channel's step
  row, as in FL Studio. Making it shorter cuts off the steps past its end.
- **Make unique** in the clip menu gives a block its own copy of the pattern, so you can change it
  without changing the others.
- To place the current pattern at the playhead's bar, use **Pattern > Place ... at the playhead** in
  the channel rack's header.

## Audio clips

Drop WAV, AIFF, FLAC, Ogg or MP3 files on the timeline, from the [browser](samples.md#the-browser)
or from your desktop:

- Dropped on an **audio channel's lane**, they go on that channel, one after another, from where you
  dropped them.
- Dropped **anywhere else**, each file gets a new audio channel of its own, named after the file,
  with its own mixer strip.
- A file that's already on an audio channel goes back on that channel when there's room.
- A **folder** brings in the audio files directly in it (not its subfolders), in name order.
- Up to 32 files at a time.

While you drag, the timeline says *Drop to add here* or *Drop for a new audio channel*.

**Import audio files...** (Ctrl+I, or the **Audio** button) does the same from a file chooser, at
the playhead (rounded to a 16th note).

Audio clips move, trim, split, copy and delete like MIDI clips. On top of that:

- **Fades**: select an audio clip and drag the small handles in its top-left and top-right corners
  to fade it in or out. Fades snap to 32nd notes and can't overlap.
- **Stretch**: Shift+drag its right edge.
- **Gain**, **Follow tempo** and **Remove fades** are in its menu.
- Double-click it for the [sample editor](samples.md#the-sample-editor): pitch, stretch mode,
  tempo, length, reverse, normalize.

## The ruler, playhead and loop region

| Gesture in the ruler | What it does |
| --- | --- |
| Click | Moves the playhead there, snapped to the nearest beat. |
| Drag (from anywhere but the loop's edges) | Draws a new loop region from where you clicked, and turns looping on. |
| Shift+drag | Draws a new loop region. |
| Drag the loop region's start or end | Changes the loop. Looping stays on or off as it was. |
| Right-click | **Loop bar *N***, **Loop the whole project**, and a **Looping** switch. |
| Mouse wheel | Zooms in time. |

Loop edges snap like clips do (hold Alt for free placement). The loop region is dimmed while looping
is off.

## Zoom and scroll

| Gesture | What it does |
| --- | --- |
| Ctrl + wheel (anywhere), or wheel over the ruler | Zoom in time, around the mouse. |
| Alt + wheel | Make every lane taller or shorter. |
| Shift + wheel, or a sideways scroll | Scroll through time. |
| Wheel | Scroll the lanes up and down. |
| Ctrl + left click (no drag) | Zoom to fit the whole song and every lane in view, as in FL Studio. Also **View > Zoom the timeline to fit the song**. |
| Header buttons | Zoom in/out in time, taller/shorter lanes. |
| Scroll bars | Along the bottom and the right. |

## Automation

Automation changes a mixer strip's volume, pan or an effect parameter over time. Automated
parameters follow their points while the song plays and when it's exported, and the mixer's knobs
and faders move with them.

1. Click the **A** in a channel's lane header (or right-click the name > **Show automation**).
   An automation lane opens under it, for the **volume** of the strip the channel plays into (or the
   master, if it plays straight there). Its header says *on its strip* or *on master*.
2. Click the automation lane's name to pick what it automates: **Volume**, **Pan**, or any
   parameter of the strip's effects (in a submenu per effect; parameters that already have points
   are marked `*`).
3. Edit the points:

| Gesture | What it does |
| --- | --- |
| Click | Adds a point there (snapped in time like clips; Alt for free). |
| Drag a point | Moves it, between its neighbours. The value shows while you drag (in dB for volume). |
| Double-click a point | Removes it. |
| Right-click a point | Removes it. |
| Right-click the lane | Opens the parameter menu, with **Remove these points** and **Hide automation**. |

Each click or drag is one undo step. With no points, the lane shows a dashed line at the control's
current value. While playing, a dot rides the line at the playhead, showing the value being played.

The AI and the console can automate too: see `set_automation` in the [command reference](../commands.md).
