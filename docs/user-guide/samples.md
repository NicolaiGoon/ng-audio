# Samples

This page covers working with audio files: the browser, audio channels and clips, the sample
editor, and the sampler instrument.

Ng DAW reads **WAV, AIFF, FLAC, Ogg and MP3** files. Projects remember where each file is rather
than copying it, so keep your samples where they are. A file that has gone missing is marked as
missing.

## The browser

The browser runs down the left of the main window. Show or hide it with the **Browser** button in
the top bar, **View > Browser**, or **Ctrl+B**. Drag the bar on its right to make it wider (160
pixels up to half the window) and double-click the bar to reset it. Whether it's shown, and its
width, are remembered.

It has four sections:

| Section | What's in it |
| --- | --- |
| **Instruments** | The built-in 4OSC synth and Sampler, then every scanned plugin instrument. |
| **Effects** | The built-in EQ, Compressor, Reverb and Delay, then every scanned plugin effect. |
| **Presets** | Your saved presets, grouped by the plugin they're for. |
| **Samples** | Your sample folders. Open one to see its subfolders and audio files. |

A small marker shows what each item is: a green dot for an instrument, blue for an effect, purple
for a preset, orange for a sample, and a yellow square for a folder. Hover over an item to see what
you can do with it.

Sample folders and presets belong to the app, not the project, so they're the same in every
project.

### Sample folders

- **+ Folder** (top right of the browser) adds a folder of samples.
- Right-click a folder in *Samples* > **Remove from the browser** to take it out (the files stay on
  disk).
- Folders are read when you open them, and read again each time, so new files show up.
- Right-click anything > **Refresh** to read everything again.

### Searching

Type in the **Search** box to find instruments, effects, presets and samples by name. Every word
you type has to appear in the name (for samples, anywhere in the path below the sample folder), so
`kick 909` finds `Drums/909/Kick 1.wav`. Only sections with matches show, opened.

- Sample search shows up to 200 files. If there are more, it says *More found: type more to narrow
  it down*.
- **Esc** in the search box clears it.

Searching uses Ng DAW's **sample index**: the list of audio files in your sample folders, built once
and kept in memory, so searching even tens of thousands of files is fast. The first search of a
folder lists it then (or it was already listed in the background). After that a search uses the
list it has, and if the list is more than about ten seconds old it is refreshed in the background,
so new files show up shortly after you add them. **Refresh** in the right-click menu forgets the
lists and starts again.

### Using items

**Drag** an item to where you want it:

| Item | Drop on the channel rack | Drop on a channel | Drop on a mixer strip | Drop on the timeline |
| --- | --- | --- | --- | --- |
| Instrument | New channel | Swaps its instrument | | |
| Instrument preset | New channel with the preset | Loads the preset | | |
| Effect | | | Adds it | |
| Effect preset | | | Adds the effect with the preset | |
| Sample | New sampler channel | Adds it (sampler channels only) | | New audio channel, or onto an audio lane |
| Folder | New sampler channel, loaded as a drum kit | Adds the kit (sampler channels only) | | Its audio files as clips |

**Double-click** an item to make a new channel from it, as if you'd dropped it on the rack. A
sample that's a **loop** goes on the timeline instead, on an audio channel of its own at the start of
the bar the playhead is in, and opens in the [sample editor](#the-sample-editor). If that file is
already on the timeline, its clip opens instead.

A sample counts as a loop if its path contains "loop", its name contains "bpm", or it's at least
four seconds long. Anything else is a one-shot, and gets a sampler channel.

**Right-click** an item for:

| Item | When |
| --- | --- |
| **New channel from this** | Instruments, instrument presets, samples and folders. |
| **Remove from the browser** | Sample folders (the top level). |
| **Show in folder** | Anything with a file: samples, folders, presets. |
| **Delete preset...** | Presets. Asks first. |
| **Refresh** | Always. |

You can also drag audio files and folders straight from the desktop onto the timeline, the channel
rack or a sampler.

### Presets

Save an instrument's settings by right-clicking its channel's name in the rack, or an effect's by
right-clicking it in the mixer. Name it, and it goes into the preset folder. Presets show in the
browser's *Presets* section, grouped by plugin, in every project. Until you save one, the section
says *Right-click an effect, or a channel's name, to save a preset*.

## Audio channels

An audio channel plays audio clips and records from an audio input. It has a mixer strip like any
other channel, but no instrument or steps.

- Drop audio files on the timeline: each gets a new audio channel named after the file, or goes on
  the audio lane you drop it on. See [Audio clips](timeline.md#audio-clips).
- **File > Import audio files...** (**Ctrl+I**) or the timeline's **Audio** button imports files at
  the playhead.
- **Edit > Add audio channel**, or **Audio > Empty audio channel**, adds an empty one.
- Double-click an empty spot on an audio lane to pick files for it.

Audio and MIDI clips each stay on their own kind of channel.

## The sample editor

Double-click an audio clip on the timeline (or select it and press **Enter**, or pick **Edit
sample** from its menu) to open the sample editor. It takes the place of the channel rack and mixer,
like the piano roll. Its title says *Sample editor: (clip) on (channel)*. Close it with the × or
**Esc**.

It shows the clip's **whole file** as a waveform, with:

- the part the clip plays lit up, and the rest dimmed;
- the song's bars and beats laid over it (bars brighter), so you can line the file up;
- the waveform scaled by the clip's gain, and flipped when the clip is reversed;
- the playhead, while playing.

| Gesture | What it does |
| --- | --- |
| Click or drag in the lit part | Moves the song's playhead there. Outside it, the playhead goes to the nearest end. |
| Drag the lit part's edges | Trims the clip, within the file. Snaps to 16th notes; hold **Alt** to trim freely. |

### The controls

| Control | What it does |
| --- | --- |
| **Stretch mode** | How speed and pitch change. **Stretch** (the default): Rubber Band's R3 engine, its highest quality, for most sounds. **Beats**: Rubber Band's percussive mode, which keeps drum hits crisp. **Resample**: speed and pitch change together, like a record played faster, as FL Studio does. |
| **Pitch** | -24 to +24 semitones. In Resample mode it changes the speed too. Double-click for 0. |
| **Fine** | -100 to +100 cents. Double-click for 0. |
| **Gain** | -24 to +24 dB. Double-click for 0 dB. |
| **Normalize** | Sets the gain so the file's loudest peak reaches 0 dB. |
| **Follow tempo** | Stretches the clip to follow the song's tempo, so it keeps its length in beats when the tempo changes. |
| **File tempo** | The file's own tempo. Double-click to type it; the clip then follows the song's tempo. |
| **Detect** | Detects the file's tempo and makes the clip follow the song's tempo. A short loop's tempo is matched to a whole number of bars, so loops line up exactly. If there's no steady tempo, it says so; type it instead. |
| **Song tempo** | Sets the song's tempo to the file's tempo, so the clip plays at its own speed. |
| **Length (beats)** | The clip's length in beats. Double-click to type a new length: the clip is stretched to fit. |
| **÷2** / **×2** | Squeezes the clip into half as many beats (twice as fast), or stretches it over twice as many (half as fast). |
| **Reverse** | Plays the clip backwards. |
| **Sampler** | Makes a new sampler channel that plays this file across all the keys, at its own pitch on middle C, and arms it so you can play it straight away. If a sampler channel already plays this file, it arms that one instead. |

Knobs apply when you let go of them (or type into them), not on every step of a drag. Each change is
one undo step. Clips are stretched as they play, so a change is heard straight away with no gap.

If the file is missing, the editor says *File missing: (path)*.

The same settings are available to the AI through `set_audio_clip` and `analyze_audio`; see the
[command reference](../commands.md).

## The sampler

The sampler is a built-in instrument that plays audio files from MIDI notes, one-shot drum style or
pitched across the keys.

To get one:

- Pick **Sampler** as a channel's instrument in the rack, then click **Edit**.
- Drop samples or a folder on the channel rack (a new sampler channel is made).
- Double-click a one-shot sample in the browser.
- Click **Sampler** in the sample editor.

### The sampler window

The window lists the samples, one row each:

| Column | What it does |
| --- | --- |
| **Sample** | The sample's name. Double-click to rename. Its tooltip shows the file. A missing file shows in red as *(missing)*. |
| **Key** | The key it plays on. Click the arrows, drag them up and down, or type a note name (for example `C1`). If the sample spreads across a range of keys, the tooltip says which. |
| **Level** | -48 to +12 dB. Double-click for 0 dB. |
| **One-shot** | On: plays to its end, as drums do. Off: stops when the note ends. |
| **×** | Removes the sample. |

Above the list:

- **Add samples...** adds audio files, each on the next free key from **C1**.
- **Load kit...** replaces the samples with a folder of drum samples.
- Or **drop** audio files, or a kit folder, onto the window. Dropped files and folders are
  **added** to what's there (Load kit replaces it).

Each add or load is one undo step. If some files don't load, you're told which.

### Drum kits

Loading a folder as a kit takes the samples in the folder itself or, for a kit kept in folders
such as `Kicks` and `Snares`, in the folders inside it. Samples whose names say what they are go on
their General MIDI keys, when those keys are free. Names are split into words (`OpenHat 02` reads as
*open hat*):

| Words in the name | Key (MIDI note) |
| --- | --- |
| kick, bd, bassdrum, kik | C1 (36) |
| rim, rimshot, sidestick, stick | C#1 (37) |
| snare, sd, snr | D1 (38) |
| clap, cp, clp | D#1 (39) |
| hat, hihat, hh, ch, closedhat | F#1 (42), closed hat |
| hat with open/op/o, or oh, ohat, openhat | A#1 (46), open hat |
| tom with floor/low/lo/lt | A1 (45) |
| tom | B1 (47) |
| tom with high/hi/ht | D2 (50) |
| crash, cym, cymbal | C#2 (49) |
| ride | D#2 (51) |
| tamb, tambourine | F#2 (54) |
| cowbell, bell, cb | G#2 (56) |

The rest fill the free keys from C1 up, in name order. Note the sampler window names keys with middle C as **C3**
(so C1 is MIDI note 36, the General MIDI kick), while the piano roll calls middle C **C4**.

The project saves where each sample file is. A pitched range across keys can be set by command
(`set_sample`); see the [command reference](../commands.md).
