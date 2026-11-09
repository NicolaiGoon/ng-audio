# Channel rack and mixer

Ng DAW keeps **channels** (what plays: an instrument or audio clips) apart from **mixer strips**
(where the sound goes: effects, sends, fader). When you add an instrument, you get both, already
linked: a channel and a mixer strip with the same name and colour. Rename or recolour one and the
other follows. You can then route channels to other strips, share a strip between channels, and
add buses, as in REAPER or Cubase.

## The channel rack

The channel rack lists the channels, one row each. It scrolls when there are more than fit.

Its header has:

- **+ Instrument** (**Ctrl+T**, or **Edit > Add instrument**): adds a 4OSC synth channel called
  *Synth N*, with its own linked mixer strip and an empty step row.
- **Pattern** picker and **Pattern** menu: see [Patterns](#patterns).

With no channels, the rack says *No channels yet. Add an instrument, or drop one (or samples) here
from the browser.*

### A channel's row

From left to right, then the step row underneath:

| Control | What it does |
| --- | --- |
| **Colour chip** | Click to pick a colour from the palette. A linked mixer strip changes too. |
| **Name** | Double-click to rename. A linked mixer strip is renamed too. Right-click for **Save instrument as preset...** |
| **linked** / **detached** | Whether the channel plays into its own mixer strip (*linked*) or another one (*detached*). |
| **●** (arm) | Arms the channel for live input and recording. See [Arming a channel](playing-and-recording.md#arming-a-channel). |
| **Instrument** | The instrument it plays: **4OSC synth**, **Sampler**, **No instrument**, or any scanned plugin instrument under *Plugins*. A plugin the project uses that isn't installed here shows as *(missing)*. |
| **Outs** | Only for plugins with more than one stereo output. See [Multi-output instruments](#multi-output-instruments). |
| **Edit** | Opens the instrument's window: the plugin's own editor, or the [sampler](samples.md#the-sampler). Disabled for the 4OSC synth, which has no window of its own. |
| **Output** | Where the channel plays: **Master**, or `-> (strip)` for any mixer strip. See [Routing](#routing-channels-to-strips). |
| **×** | Deletes the channel, and its own mixer strip unless other channels share it. |
| **Step row** | The channel's steps in the current pattern. See [Step patterns](#step-patterns). |
| **−** / **+** | Fewer or more steps. |

**Audio channels** have no instrument, steps or Edit button. Instead their row has an **input**
drop-down and a **monitoring** choice (**Mon auto**, **Mon on**, **Mon off**). See
[Recording audio](playing-and-recording.md#recording-audio). Add an empty audio channel from
**Edit > Add audio channel** or the timeline's **Audio** button; dropping audio files on the timeline
also makes them. See [Samples](samples.md#audio-channels).

### Dropping things on the rack

Drag from the [browser](samples.md#the-browser) or from the desktop:

| Drop | On the rack (empty space) | On a channel's row |
| --- | --- | --- |
| An instrument | A new channel playing it | Swaps that channel's instrument |
| An instrument preset | A new channel with that preset | Loads the preset on that channel |
| One or more samples | A new **sampler** channel with the samples on it (named after the sample, or its folder when there are several) | Adds them to the channel, if it plays the sampler |
| A folder | A new sampler channel loaded with the folder as a **drum kit** | Adds the kit to the channel, if it plays the sampler |

The rack and the row light up blue while you drag over them. Each drop is one undo step.

### Instruments

- **4OSC synth**: Tracktion's built-in four-oscillator synth. It has no window of its own; the AI
  can still read and set its parameters by command.
- **Sampler**: Ng DAW's sampler. Click **Edit** to add samples or load a drum kit. See
  [The sampler](samples.md#the-sampler).
- **No instrument**: a silent channel.
- **Plugins**: every VST3 instrument found by a [plugin scan](#plugins).

If changing the instrument fails (for example, the plugin was moved or uninstalled), you're told
why and the previous instrument stays. Scan again in **Options > Plugins...**.

### Multi-output instruments

For a plugin with more than one stereo output (a drum plugin with separate kick, snare and hat
outputs, say), click **Outs** to give each stereo output its own mixer strip. The output strips
show right after the instrument's own strip, slightly lower and paler, and the instrument's strip
sums them. Click **Outs** again to go back to one stereo output. While Outs is on you can't change
the channel's instrument.

## Step patterns

Each channel has a row of steps (16th notes) under its name, grouped in fours.

| Gesture | What it does |
| --- | --- |
| Click a step | Turns it on or off. |
| Drag across steps | Paints them on, or off if the first step you clicked was on. One undo step. |
| Right-click (or right-drag) | Turns steps off. |
| **−** | A bar (16 steps) fewer, or halves a row shorter than a bar: 64, 48, 32, 16, 8, 4. |
| **+** | A bar more, up to 64 steps. |

While playing, the step under the playhead is outlined.

Lengthening a pattern's clip on the timeline also adds steps (see
[Step patterns on the timeline](timeline.md#step-patterns-on-the-timeline)).

### Patterns

As in FL Studio, a project can have several **patterns**. Each pattern has its own step row for
every channel. Pattern 1 starts at the beginning of the project.

- The **picker** in the rack's header shows which pattern the rack is editing. Pick another to edit
  it.
- Clicking a pattern's clip on the timeline also switches the rack to that pattern.
- The **Pattern** menu:
  - **New pattern**: a new, empty pattern, which becomes the current one.
  - **Clone (pattern)**: a copy of the current pattern.
  - **Rename (pattern)...**
  - **Delete (pattern)**: not available for pattern 1.
  - **Place (pattern) at the playhead**: puts it on the timeline as a block, at the start of the bar
    the playhead is in.

A placed pattern is a **block** on the timeline: one clip on each channel with steps in it. See
[The timeline](timeline.md#step-patterns-on-the-timeline).

## The mixer

The mixer has one column per strip, with the **master** strip on the right. Its header has
**+ Bus** and **Dock buses**.

### A strip

From top to bottom:

| Control | What it does |
| --- | --- |
| **Colour chip** | Recolours the strip (and its linked channel). |
| **Name** | Double-click to rename. **Drag** it onto another strip to move this strip there. |
| **Fold** (▾ / ▸) | Only on a bus or group that other strips play into: hides those strips so the mixer shows just the bus, or shows them again. Not an undo step. |
| **Inputs / routing** | Shows how many inputs the strip has (*2 inputs*, *Bus, 3 inputs*), or `> (strip)` if it plays into another strip. Click it to choose where the strip plays: **Master**, or any other strip. |
| **Pan** | Drag to pan. Double-click to centre. |
| **Effects** | The insert effects. See [Insert effects](#insert-effects). |
| **Sends** | The strip's sends to buses. See [Buses and sends](#buses-and-sends). |
| **M** / **S** | Mute and solo. These aren't undo steps, as in most DAWs. |
| **Fader** | Volume, from -100 dB to +6 dB. Double-click for 0 dB. Type in the box under it. A whole drag is one undo step. |
| **Meter** | The strip's level. |
| **×** | Deletes the strip. Channels that played into it go to the master. |

The master strip has a name, pan, effects, a fader and a meter; it has no mute, solo or sends.

Faders and pan knobs follow [automation](timeline.md#automation) live while the song plays.

When the mixer is short (for example with the console open) it hides the sends and effects, to keep
room for the faders.

### Routing channels to strips

- A new instrument channel plays into its own strip (*linked*).
- Pick another strip in the channel's **Output** drop-down in the rack to send it there instead.
  The channel is then *detached*. Several channels can share one strip.
- Pick its own strip again to re-link it, or **Master** to send it straight to the master.
- In the mixer, click a strip's inputs label to route the **strip** into another strip (a group or
  bus), or into the master. Routing that would loop back into itself is refused.

### Buses and sends

A **bus** is a strip other strips send to, such as a shared reverb.

- **+ Bus** (in the mixer header, or **Edit > Add bus**) adds one.
- **+ Send** on a strip sends it to a bus: pick a bus from the list (buses it already sends to are
  ticked and greyed), or **New bus** to make one and send to it in one undo step.
- A send shows as a bar with the bus's name. **Drag** across it to set the level (-100 to +6 dB);
  the drag is one undo step. **Double-click** it for 0 dB.
- **Right-click** a send for **Pre-fader** (the strip's fader doesn't change it), **Set to 0 dB**
  and **Remove**. Sends are post-fader unless you make them pre-fader; pre-fader sends say *(pre)*.
- Sends that would loop back into themselves are refused.

**Dock buses** (in the mixer header) gathers the buses, and any strip other strips play into, in
a section of their own beside the master, which stays put while the channels scroll. The setting
is remembered.

### Arranging the mixer

- Drag a strip by its **name** onto another strip to move it to that place. The target strip lights
  up blue.
- Fold a bus or group to hide the strips that play into it.

## Insert effects

Each strip (the master included) has a list of insert effects. They run top to bottom, **before the
fader**.

- **+ FX** opens a menu: the built-in **EQ**, **Compressor**, **Reverb** and **Delay**, then every
  scanned VST3 effect (with its maker), or *None yet: scan in Plugins*.
- Or drag an effect or effect preset from the [browser](samples.md#the-browser) onto a strip. It goes
  after the strip's other effects.
- **Click** an effect to open its window.
- **Right-click** it for:
  - **Open window**
  - **Bypass** / **Turn on**
  - **Move up** / **Move down**
  - **Save as preset...**
  - **Remove**

Bypassed effects show greyed. A plugin that isn't installed on this computer shows in red, with
*(not installed on this computer)* in its tooltip; its settings are kept in the project.

Effects and their settings are saved with the project.

## Plugins

Ng DAW hosts **VST3** plugins.

### Scanning

**Options > Plugins...** opens the Plugins window: the list of plugins found on this computer, with
a summary such as *12 instruments and 30 effects*.

1. Click **Scan for VST3 plugins...**.
2. Choose which folders to look in. The usual VST3 folders are filled in, and your choice is
   remembered for next time.
3. The scan adds new and updated plugins.

- A plugin that crashes the scan is skipped the next time you scan.
- Right-click a plugin in the list to remove it or show its file.
- The list is sortable, and is saved with the app's settings.

After a scan, the new instruments appear in every channel's **Instrument** drop-down and in the
browser, and the new effects in **+ FX** and the browser.

### Plugin windows

- **Instruments**: click **Edit** on the channel's row in the rack.
- **Effects**: click the effect in the mixer strip, or right-click > **Open window**.

A plugin with its own editor opens it in a window of its own. **Built-in effects** (EQ, Compressor,
Reverb, Delay) have no editor of their own, so they get a window of sliders: an **On** switch (turn
it off to bypass), and a slider for each parameter (plus **Length** in ms for the delay). Each slider
drag is one undo step, and the sliders follow automation while the song plays.

A plugin's window closes when the plugin is removed. Plugins and all their settings are saved with
the project.

### Presets

- Save an instrument's settings: right-click the channel's name in the rack > **Save instrument as
  preset...**
- Save an effect's settings: right-click the effect > **Save as preset...**

Name the preset and it goes into the preset folder, so it shows in the browser's *Presets* section in
every project. See [Samples](samples.md#presets).

## Commands

Everything on this page is also available as commands for the console and AI clients, for example
`create_track`, `route`, `set_instrument`, `set_steps`, `create_pattern`, `set_mixer_strip`,
`add_effect`, `set_effect`, `create_bus`, `set_send`, `route_strip`, `move_strip`, `fold_strip`,
`set_multi_output`, `tag_channel` and `scan_plugins`. See the [command reference](../commands.md).
