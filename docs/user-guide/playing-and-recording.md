# Playing and recording

## The transport

The transport is in the middle of the top bar.

| Control | What it does | Shortcut |
| --- | --- | --- |
| **Back to the start** (`|<`) | Moves the playhead to the start of the song. | Home |
| **Play / Stop** | Starts playback from the playhead, or stops it. While recording, stopping also finishes the recording. | Space |
| **Record** | Starts recording into the armed channels from the playhead. Click again (or Stop) to finish. | R |
| **Tempo** | 20 to 300 BPM, in whole BPM. Type a value, click the up/down arrows, or drag the arrows up and down. A drag is one undo step. Clips keep their length in beats when the tempo changes. | |
| **Time signature** | 2/4, 3/4, 4/4, 5/4, 6/8, 7/8 or 12/8. Other signatures can be set by command (`set_time_signature`); the box then shows them as text. | |
| **Song position** | Where the playhead is, as *bar . beat . sixteenth*. | |
| **Loop** | Turns looping on or off. With no loop region drawn, the whole project loops. | Ctrl+L |
| **Metronome** | A click on every beat while playing, louder on the first beat of the bar. | Ctrl+M |

You can also move the playhead by clicking in the ruler of the [timeline](timeline.md#the-ruler-playhead-and-loop-region),
the [piano roll](piano-roll.md) or the waveform in the [sample editor](samples.md#the-sample-editor).
On the timeline, a click snaps the playhead to the nearest beat.

Play, stop, moving the playhead, looping, the metronome and arming are not undo steps. Tempo and
time signature changes are.

### The loop region

New projects loop the whole project, rounded up to whole bars. The loop region shows in the
timeline's ruler (dimmed while looping is off):

- **Drag** in the ruler (or **Shift+drag**) to draw a new loop. Drawing a loop also turns looping on.
- **Drag the loop's edges** to change it. This leaves looping on or off as it was.
- **Right-click** the ruler for **Loop bar *N*** (the bar you clicked), **Loop the whole project**,
  and a **Looping** switch.

See [The timeline](timeline.md#the-ruler-playhead-and-loop-region) for details.

## Arming a channel

A channel has to be **armed** for live input to reach it. Click the **●** button on its row in
the [channel rack](channel-rack-and-mixer.md#the-channel-rack). It turns blue when armed.

- **Instrument channels** arm for MIDI. Only one instrument channel is armed at a time: arming one
  disarms the others.
- **Audio channels** arm for their audio input. Any number can be armed at once, alongside one
  instrument channel. Arming an audio channel that has no input yet picks the device's first input.
  If the audio device has no inputs, you're told to pick one that does in Settings.

Other ways to arm an instrument channel:

- Click its name in the timeline's lane headers ("the keys play it").
- Right-click its name in the timeline (this also opens its menu).
- Open one of its clips in the piano roll while the typing keyboard is on.
- Make a sampler from the sample editor's **Sampler** button (the new channel is armed).
- If nothing is armed, playing the typing keyboard arms the first instrument channel, and
  showing the on-screen piano arms the first channel.

## Playing live

### MIDI keyboards

Any MIDI keyboard connected to the computer plays the armed instrument channel. Turn MIDI inputs
on in **Settings > Audio and MIDI** if your keyboard isn't heard.

### The typing keyboard

The typing keyboard plays the armed channel from the computer keyboard, as in FL Studio. Turn it
on or off with the **typing keyboard** button in the top bar, **View > Typing keyboard**, or
**Ctrl+K**. It is **on** by default, and the app remembers the setting.

| Keys | Notes |
| --- | --- |
| `Z S X D C V G B H N J M` | C4 (middle C) up to B4: the white and black keys of one octave. |
| `, L . ; /` | C5 to E5: the bottom row carries on. |
| `Q 2 W 3 E R 5 T 6 Y 7 U` | C5 up to B5: the octave above. |
| `I 9 O 0 P [ ]` | C6 to F#6: the top row carries on. |

How it behaves:

- Notes sound while the key is held and stop when you let go. Holding a key doesn't retrigger it.
- If no instrument channel is armed, the first key you press arms the first instrument channel.
- **While it's on, those keys play notes instead of running their shortcuts.** That includes the
  timeline tools (B, C, D, T), split (S), record (R) and quantize (Q). Turn the typing keyboard off
  (Ctrl+K) to use them. Shortcuts with Ctrl or Alt (Ctrl+S, Ctrl+Z, ...) and keys such as Space,
  Home, Del, Enter, Esc, the arrows, F1 and F2 always work.
- Keys typed into a text box (renaming, the browser's search, the console) are text, not notes.
- It works in detached panel windows too.

### The on-screen piano

The **piano** button in the top bar (or **Ctrl+Shift+K**, or **View > Piano keyboard**) shows a
piano along the bottom of the window, from C1 to C8 (MIDI notes 24 to 108). Click or drag across
its keys to play the armed channel. If no channel is armed, showing it arms the first channel.

In the [piano roll](piano-roll.md), clicking the keyboard on the left also plays notes, on the
clip's own channel.

## Recording

### Recording MIDI

1. Arm an instrument channel (its **●** in the channel rack).
2. Move the playhead to where you want to start.
3. Press **Rec** (or **R**) and play your MIDI keyboard, the typing keyboard or the on-screen piano.
4. Press **Rec** again, **Stop** or **Space** to finish.

What you played becomes a MIDI clip on that channel, as one undo step.

If nothing is armed, Record says *Arm a channel first: click its ● button in the channel rack.*

Note: with the typing keyboard on, **R** plays a note rather than starting the recording. Click the
Rec button, or turn the typing keyboard off first.

### Recording audio

1. In the channel rack, pick the audio channel's **input** from its drop-down. The list shows the
   inputs of the audio device chosen in **Settings > Audio and MIDI**. An input saved on another
   computer that isn't here shows as *(not here)*.
2. Choose its **monitoring**:
   - **Mon auto**: hear the input through the channel while it's armed (the default).
   - **Mon on**: always hear it.
   - **Mon off**: never hear it.
3. Arm it with its **●** button. You can arm several audio channels, and one instrument channel
   for MIDI at the same time.
4. Press **Rec** (or **R**), then **Stop** to finish.

Each take becomes a clip on its channel, all in one undo step. Takes are saved as WAV files in a
`Recordings` folder beside the project, or in `Music/Ng DAW/Recordings` (your
[project folder](panels-and-preferences.md#general)) before the project is saved. They're named
after the project, the channel and the take number.

You can't export while recording; stop first.

### Audio buffer and latency

If playing live feels late, or you hear clicks, look at the **CPU** meter's tooltip in the top bar.
It shows the device, sample rate, buffer size and output latency, and says which buffer size to try.
See [the CPU meter](panels-and-preferences.md#the-cpu-meter).
