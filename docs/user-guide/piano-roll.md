# The piano roll

The piano roll edits one MIDI clip's notes. It has a keyboard down the left, a bar ruler on top, a
grid with one row per pitch, and a velocity lane underneath.

## Opening and closing

- **Double-click** a MIDI clip on the timeline, or select it and press **Enter**, or pick
  **Edit notes** from its menu.
- The piano roll takes the place of the channel rack and mixer. Its title says
  *Piano roll: (clip) on (channel)*.
- Only one editor is open at a time: opening an audio clip closes the piano roll and opens the
  sample editor instead.
- With the [typing keyboard](playing-and-recording.md#the-typing-keyboard) on, opening a clip arms
  its channel, so the keys play the instrument you're writing for.
- **Close it** with the × in its header, or **Esc**. If notes are selected, the first Esc deselects
  them and the next one closes the piano roll.
- If the clip is deleted (or undone away) while it's open, the piano roll closes by itself.
- Like the other panels, it can be [detached into its own window](panels-and-preferences.md#arranging-the-panels).

Step pattern clips don't open in the piano roll; edit their steps in the
[channel rack](channel-rack-and-mixer.md#step-patterns).

## The header

| Item | What it does |
| --- | --- |
| Zoom in / out | Zoom in time (same as Ctrl + wheel). |
| Bigger / smaller rows | Make the pitch rows taller or shorter (same as Alt + wheel). |
| **Ghosts** | Shows the song's other MIDI clips faintly behind this one, in their channel's colour. On by default; remembered. |
| **Key** and **Scale** | Highlight a scale (see [Scales](#scales)). Remembered. |
| **Quantize** | Moves the selected notes, or all of them, onto the grid (**Q**). |
| **Snap** | The grid: **1/4**, **1/8**, **1/16** (default) or **1/32** note. |
| × | Closes the piano roll (**Esc**). |

## Drawing and editing notes

| Gesture | What it does |
| --- | --- |
| Click an empty spot | Draws a note in that grid cell. It's as long as the last note you drew, resized or moved. |
| Click and drag from an empty spot | Draws a note and paints more along the way, as on the timeline. |
| Drag a note | Moves it (and the rest of the selection) in time and pitch. You hear it at its new pitch. |
| Drag a note's right edge | Resizes it. |
| Right-click a note | Deletes it. |
| Right-drag | Deletes every note it passes over, as one undo step. |
| Ctrl+click a note | Adds it to the selection, or takes it out. |
| Ctrl+drag on an empty spot | Selects the notes in a box. |
| Click the keyboard on the left | Plays that key. Drag up and down for a glissando. Clicking further right on a key plays it louder. |
| Click in the ruler | Moves the playhead there, on the grid. |

- A note can't be drawn over another note on the same key, or past the end of the clip.
- The whole selection moves together, and stays inside the clip and the keyboard.
- New notes have velocity 100.
- Drawing, clicking or moving a note plays it on the clip's channel, so you hear what you're placing.

## Snap and quantize

Everything snaps to the **Snap** grid: where notes start, their lengths, and moves. Hold **Alt** to
place and size notes freely (to a 64th of a beat).

**Quantize** (or **Q**) moves the selected notes onto the grid, or every note if none is selected.
With the typing keyboard on, Q plays a note; use the Quantize button instead.

## The velocity lane

Under the grid, each note's velocity (how hard it's played) shows as a bar.

- **Click or drag** across the velocity lane to set the velocity of the notes there: the height
  where you click is the new velocity. It sets every note playing at that point in time, or only
  the selected ones if any are selected.
- A whole drag is one undo step.

## Keys

Click in the piano roll first so it has the keyboard focus.

| Key | What it does |
| --- | --- |
| Ctrl+A | Select every note. |
| Del or Backspace | Delete the selected notes. |
| Ctrl+C | Copy the selected notes. |
| Ctrl+X | Cut the selected notes. |
| Ctrl+V | Paste at the playhead. If the playhead is outside the clip, the notes go straight after where they were copied from. Notes that would start past the end of the clip are left out. |
| Ctrl+D | Duplicate the selected notes straight after themselves. |
| Up / Down | Move the selected notes up or down a semitone. |
| Shift+Up / Shift+Down | Move them up or down an octave. |
| Left / Right | Move them by one grid step. |
| Q | Quantize (typing keyboard off). |
| Esc | Deselect; with nothing selected, close the piano roll. |

## Zoom and scroll

| Gesture | What it does |
| --- | --- |
| Wheel | Scroll up and down through the pitches. |
| Shift + wheel, or a sideways scroll | Scroll through time. |
| Ctrl + wheel | Zoom in time, around the mouse. |
| Alt + wheel | Make the pitch rows taller or shorter. |

When a clip opens, the view scrolls to its notes.

## Ghost notes

With **Ghosts** on, the notes of the song's other MIDI clips show faintly, where they play in the song,
behind this clip's notes, each in its channel's colour. Use them to write a bass line against the
chords, or a melody against the bass. Step patterns aren't shown as ghosts. You can't click ghost
notes.

## Scales

Pick a **Scale** to dim the rows outside it and mark the key's root. The **Key** box (C to B) picks
the root and is only enabled once a scale is picked.

The scales are: No scale (the default), Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Harmonic
minor, Major pentatonic, Minor pentatonic and Blues.

Scale highlighting is only a guide: you can still draw notes outside it.

## Undo

Every edit replaces the clip's notes in one `set_notes` command, so each click, drag, paste,
quantize or velocity drag is exactly one undo step. The AI and the console can read and write notes
with the same commands (`get_notes`, `set_notes`, `quantize_notes`); see the
[command reference](../commands.md).

Middle C (MIDI note 60) is **C4** in the piano roll and the on-screen piano.
