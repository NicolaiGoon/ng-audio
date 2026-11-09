# Exporting

Export renders the song to audio files: the whole song or the loop region, as one mixed file or as
stems (one file per mixer strip).

Open the Export dialog with the **Export** button in the top bar, **File > Export...**, or
**Ctrl+E**. You can't export while recording; stop first.

## The Export dialog

| Setting | Choices |
| --- | --- |
| **Export** | **The whole song**, or **The loop region**. The loop region is only offered when one is set. |
| **Format** | **WAV**, or **MP3**. MP3 shows as *MP3 (needs LAME or FFmpeg)* and is greyed out when no encoder is found. See [MP3 export](#mp3-export). |
| **Bit depth** (WAV) | **16-bit**, **24-bit** (the default) or **32-bit float**. |
| **Bit rate** (MP3) | **128**, **192**, **256** or **320 kbps** (the default). |
| **Sample rate** | **44.1 kHz** (the default), **48 kHz**, **88.2 kHz** or **96 kHz**. |
| **One file per mixer strip (stems)** | Exports stems instead of the mix. See [Stems](#stems). |

The dialog remembers your choices until you quit the app.

**The whole song** runs from the start to the end of the last clip, plus two seconds for reverb
and delay tails. **The loop region** is exactly the loop.

Click **Export...** to choose where to save:

- For a single file, a save dialog suggests the project's name (with ` loop` added for the loop
  region) and warns before overwriting.
- For stems, you choose a folder.

The suggested place is an `Exports` folder beside the project, or `Exports` in the
[project folder](panels-and-preferences.md#general) (`Music/Ng DAW` by default) if the project
hasn't been saved yet. The folder is created if needed.

While exporting, the dialog says *Exporting...* and its controls are disabled. When it's done, a
message says what was saved and where, with a **Show** button that opens the folder. If something
goes wrong, the dialog stays open and says why.

Automation is applied in the export, as in playback.

## Stems

With **One file per mixer strip (stems)** ticked, you get one file per mixer strip, all the same
length and all starting at the same point, so they line up when you import them into another app.

- Each strip's file is that strip's channels through its own effects and fader.
- Each bus's file is what's sent to it, through the bus.
- Together they add up to the mix, before the master strip's effects and fader.
- A silent part of the song still gets its file, so the stems line up.
- Files are named `(project) - (strip).wav` (or `.mp3`). If two strips share a name, a number is
  added.

If no strip plays anything, the export fails with *no mixer strip plays anything, so there are no
stems*.

## MP3 export

Ng DAW uses an external encoder for MP3: **LAME** or **FFmpeg**. It doesn't ship with one. Ng DAW
looks for `lame` (or `lame.exe`) first, then `ffmpeg` (or `ffmpeg.exe`):

1. next to the Ng DAW program (for the Windows installer, `C:\Program Files\Ng DAW`);
2. in the folders on your `PATH`;
3. on macOS and Linux, also in `/opt/homebrew/bin`, `/usr/local/bin` and `/usr/bin` (an app started
   from the Dock or Finder doesn't get the shell's `PATH`).

Install one, for example with `winget install ffmpeg` on Windows, `brew install lame` on macOS, or
`sudo apt install lame` on Debian/Ubuntu, then open the Export dialog again. Without an encoder,
the dialog says *For MP3, install LAME or FFmpeg and open this again.*

## Exporting by command

The console and AI clients export with `export_audio` (path, format, range, stems, sample rate,
bit depth or bit rate). See the [command reference](../commands.md).
