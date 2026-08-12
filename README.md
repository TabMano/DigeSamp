# BPM & Key Detector (VST3)

A JUCE audio plugin that estimates **tempo (BPM)** and **musical key**, either
from a dropped audio file or continuously from live input.

## How it works

**BPM (segment-voted, harmonic-reinforced autocorrelation)**
1. A spectral-flux onset envelope is computed from the audio (STFT
   frame-to-frame magnitude increase).
2. The track is split into overlapping ~20-second segments. Each segment is
   scored independently via autocorrelation, but the score at each candidate
   lag is reinforced with its harmonics (`ac(lag) + 0.6·ac(2·lag) +
   0.35·ac(3·lag)`) so the *fundamental* tempo period wins over an octave
   double/half.
3. All segment estimates vote: estimates within ±3 BPM of each other (also
   checking half/double relationships) are clustered, and the
   highest-weighted cluster becomes the final BPM. This means one weak
   section — a breakdown, an ambient intro — gets outvoted by the rest of
   the track instead of skewing the whole result.
4. A 0–1 confidence score reflects how much of the total vote weight the
   winning cluster captured.

**Key (higher-resolution, log-compressed chroma)**
1. Chroma is computed with an 8192-sample FFT (vs. 2048 for onsets) — enough
   frequency resolution to place bass notes in the right pitch class, which
   a smaller FFT would smear across bins.
2. Magnitudes are log-compressed (`log(1 + mag)`) before accumulating into
   the 12-bin chroma vector, so loud transients (kicks, snares) don't drown
   out sustained harmonic content.
3. The chroma vector is correlated against Krumhansl-Schmuckler major/minor
   profiles for all 24 keys; the winner is reported along with a confidence
   score that also accounts for how far ahead it was of the runner-up (a
   narrow win, e.g. relative major/minor confusion, reads as lower
   confidence than a clear one).

Both remain classic, well-documented MIR techniques — meaningfully more
robust than a single-shot pass, but still not a substitute for a dedicated
library like essentia or aubio if you need production-grade accuracy.

## Choosing a file: button, not drag-and-drop

Earlier versions of this UI used drag-and-drop. That was switched to a plain
"Choose File..." button because drag-and-drop relies on Windows' OLE/COM
drop-target registration when the plugin window is created - and that
codepath is a known weak spot under Wine, especially for a plugin window
embedded inside a host (like FL Studio) rather than a standalone top-level
window. If you saw the plugin window open but freeze with nothing rendering
inside it, that registration call stalling was the likely cause. The file
picker button sidesteps it entirely.

**If you still see a freeze after this change**, it's worth isolating
whether the problem is Wine-wide or specific to how FL Studio hosts VST3
plugins: build the Standalone target too (already included -
`FORMATS VST3 Standalone` in `CMakeLists.txt`) and run it directly via
`wine build/BpmKeyDetector_artefacts/Release/Standalone/"BPM & Key Detector.exe"`.
If the Standalone app also freezes, the issue is upstream of FL Studio
(Wine + JUCE GUI generally). If it opens fine there but still freezes
specifically inside FL, that points at FL's VST3 hosting/embedding
behavior under Wine rather than the plugin's own GUI code.

## The UI

## Shutdown safety, memory, and UI states

A few issues surfaced during real-world testing (freezing FL Studio on
close) and got fixed:

- **Cancellable analysis.** `AudioAnalyzer::analyze()` now accepts an
  optional `shouldCancel` callback, checked between FFT frames and
  autocorrelation lags throughout the pipeline. The background thread wires
  this to its own `threadShouldExit()`, so when the plugin is being torn
  down mid-analysis, the analysis bails out within roughly one frame's
  worth of work instead of running to completion. This matters because the
  processor's destructor calls `analysisThread.stopThread(4000)`, which
  blocks until the thread actually exits - without cancellation, a
  long-running analysis on a big file could make that block for its full
  duration, and a host waiting on that is a host that looks frozen.
- **Lower memory cap.** File reads are capped at 3 minutes of audio (down
  from 10) - plenty for BPM/key detection, since the segment-voting BPM
  algorithm doesn't need the whole track anyway, and it keeps the worst-case
  read buffer around 63MB instead of ~210MB for a long stereo file.
  Longer files just get their first 3 minutes analyzed rather than being
  rejected; sampling a representative excerpt instead of always the head of
  the file would be a reasonable next step if that ever matters in practice.
- **Explicit UI states.** The result label now always shows one of: "Ready"
  (nothing analyzed yet), "Analyzing..." (background thread running), a
  result like "128.0 BPM - F# Minor", or an error message - never blank or
  ambiguous about what's currently happening.

Deliberately minimal otherwise, because this is a detection tool, not a mixing tool:
a drop zone and one result line. Drop a file, wait a moment, read the BPM
and key. That's the whole interface.

Analysis runs on a single background thread so the UI never freezes while
a file is being read and analyzed.

## Supported file formats

| Format | Support |
|---|---|
| WAV, AIFF | Always (built into JUCE core) |
| FLAC, OGG Vorbis | Always (enabled via `JUCE_USE_FLAC` / `JUCE_USE_OGGVORBIS` in `CMakeLists.txt`) |
| MP3 | JUCE's built-in reader (`JUCE_USE_MP3AUDIOFORMAT`), plus OS decoders below |
| M4A, AAC, CAF | macOS only, via `CoreAudioFormat` (added automatically by `registerBasicFormats()`) |
| WMA, additional MP3/AAC variants | Windows only, via `WindowsMediaAudioFormat` |

If a dropped file can't be read (wrong format, corrupt, DRM-protected), the
UI shows an error message rather than silently failing.

## Project layout

```
BpmKeyDetector/
├── CMakeLists.txt
├── Source/
│   ├── PluginProcessor.h/.cpp   – AudioProcessor, background analysis thread
│   ├── PluginEditor.h/.cpp      – Drop zone + result display, nothing else
│   └── AudioAnalyzer.h/.cpp     – BPM (segment-voted) + key (chroma) algorithms
```

## Building for a Wine-hosted DAW (you're on Linux, DAW runs via Wine)

Important first: Wine runs *Windows* programs, so your DAW - even though
it's running on Linux hardware - is a Windows binary as far as plugins are
concerned. It can only load a Windows-format VST3 (Windows `.dll` inside the
bundle), not a native Linux build. So the goal below is a Windows VST3,
built from Linux.

**Recommended path: let GitHub build it for you (free, and avoids local
cross-compiler headaches).** This repo already includes
`.github/workflows/build-windows.yml`, which builds on an actual
Windows machine using Microsoft's own compiler - the same toolchain JUCE
is officially tested against, so you don't hit the flaky edge cases that
MinGW cross-compilation on Linux sometimes runs into for VST3 specifically.

### Step 1 - Get a GitHub account and a new repository
If you don't already have one: sign up at github.com (free), then click
"New repository". Any name is fine, e.g. `bpm-key-detector`. Keep it public
(private repos get far fewer free Actions minutes).

### Step 2 - Push this project's contents to that repository
From inside the `BpmKeyDetector` folder (the contents, not a parent folder
wrapping it - `CMakeLists.txt` should sit at the repo root):

```bash
cd BpmKeyDetector
git init
git add .
git commit -m "Initial commit"
git branch -M main
git remote add origin https://github.com/YOUR_USERNAME/bpm-key-detector.git
git push -u origin main
```

(Replace the URL with the one GitHub shows you after creating the repo.)

### Step 3 - Let it build
Pushing to `main` triggers the workflow automatically. Watch it run under
the "Actions" tab of your repository on github.com. It takes a few minutes
(JUCE is a big codebase to compile). If it's your first Actions run, you
may need to click "I understand my workflows, go ahead and enable them."

### Step 4 - Download the built VST3
Once the run finishes (green checkmark), open it, scroll to "Artifacts",
and download `BpmKeyDetector-VST3-Windows`. Unzip it - inside you'll find
`BPM & Key Detector.vst3`, which is a **folder**, not a single file.

### Step 5 - Install it for personal use (no installer needed)
For just using it yourself, skip the installer entirely - copy the folder
straight into your Wine prefix's VST3 directory:

```bash
cp -r "BPM & Key Detector.vst3" \
  ~/.wine/drive_c/Program\ Files/Common\ Files/VST3/
```

(If your DAW uses a different Wine prefix than the default `~/.wine`,
adjust the path accordingly - check your DAW's Wine setup or run
`WINEPREFIX=/path/to/prefix wine winecfg` to confirm it.)

Then rescan plugins inside your DAW and it should show up.

### Step 6 (optional) - Package it as a Windows installer .exe
Only worth doing if you want a clean double-click install experience or
plan to share it with someone else. This repo includes
`installer/installer.nsi`, an NSIS script that copies the VST3 into the
standard Windows VST3 folder.

NSIS runs natively on Linux to *build* the installer (you don't need Wine
for that step - only to later run the resulting `.exe`):

```bash
sudo apt install nsis
```

Then:
1. Put the downloaded `BPM & Key Detector.vst3` folder inside
   `installer/VST3/`, so the path reads
   `installer/VST3/BPM & Key Detector.vst3/`.
2. Build the installer:
   ```bash
   cd installer
   makensis installer.nsi
   ```
3. This produces `BpmKeyDetectorSetup.exe` in that same folder. Run it
   through Wine (`wine BpmKeyDetectorSetup.exe`) or copy it to a real
   Windows machine - either way it installs into
   `C:\Program Files\Common Files\VST3\`.

One heads-up: since this installer isn't code-signed (that requires a paid
certificate), Windows SmartScreen may warn that it's from an "unknown
publisher" the first time it runs. That's expected for an unsigned hobby
build, not a sign anything's wrong - click "More info" -> "Run anyway".

## Building natively on Linux (only useful for testing, not for a Wine DAW)

A native Linux `.vst3` won't load in a Windows DAW under Wine (see above),
but building one locally is a fast way to check the code compiles and the
Standalone app runs before waiting on a GitHub Actions build.

You'll need CMake 3.22+ and a C++ compiler (Xcode on macOS, Visual Studio on
Windows, or build-essential on Linux).

```bash
cd BpmKeyDetector

# 1. Clone JUCE as a sibling folder (only needs to be done once)
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git

# 2. Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. Build (VST3 + Standalone)
cmake --build build --config Release
```

The built plugin lands in:

```
build/BpmKeyDetector_artefacts/Release/VST3/BPM & Key Detector.vst3
```

`COPY_PLUGIN_AFTER_BUILD TRUE` (already set in `CMakeLists.txt`) copies it
into your system's VST3 folder automatically — just rescan plugins in your
DAW afterward.

On Linux you'll also need the JUCE GUI dependencies before step 2:

```bash
sudo apt install libasound2-dev libjack-jackd2-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev libwebkit2gtk-4.1-dev \
  libglu1-dev freeglut3-dev
```

## Building a Windows installer

`Setup/installer.iss` is a ready-made [Inno Setup](https://jrsoftware.org/isinfo.php)
script. After building the plugin (steps above), open it in Inno Setup and
press F9 — it packages the `.vst3` into `Setup/Output/BpmKeyDetectorSetup.exe`,
an installer that drops the plugin straight into
`C:\Program Files\Common Files\VST3` on whoever runs it.

## Extending it further

- **Beat tracking**: swap the single best-lag pick for a full tempogram +
  dynamic-programming beat tracker (essentia/aubio have reference
  implementations you could port) for sample-accurate beat grids, not just
  an average BPM.
- **Key profile alternatives**: try Temperley or Albrecht-Shanahan profiles
  alongside Krumhansl-Schmuckler and let them vote, the way the BPM segments
  do now.
- **Onset-weighted chroma**: weight chroma frames by local onset strength so
  sustained notes between hits count more than the transients themselves.
- **Live input analysis**: if you later want it to listen to a track
  continuously instead of only analyzing dropped files, feed `processBlock`
  into a `juce::AbstractFifo` and drain it into a rolling window on the
  background thread — worth adding only if you actually need it, since it
  roughly doubles the surface area of the processor for a "just detect it"
  tool.
