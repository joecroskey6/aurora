# Aurora

An original, free, open-source audio meter and reactive visual plug-in. The default **Flight Deck** design combines dark instrument panels, phosphor colour, warm technical typography and fluid 3D scenes. The earlier **Gen X Soft Club** light themes remain available.

[Downloads](https://github.com/joecroskey6/aurora/releases) · [Windows build](https://github.com/joecroskey6/aurora/actions/workflows/windows.yml) · [Report an issue](https://github.com/joecroskey6/aurora/issues)

**AU and VST3 on macOS; VST3 on Windows.** Aurora is an audio effect. Put it on a track or the master bus; its audio output is bit-for-bit unchanged. It adds zero algorithmic latency, does not generate sound, and makes no network requests.

The macOS developer preview requires macOS 13.3+ and includes Apple Silicon and
Intel code. Windows x64 support is being validated through the automated build;
the Windows installation guide is [here](release-docs/WINDOWS-INSTALLATION.txt).
These are development previews, not certified across every host or graphics
driver. Download the explicit matching source asset alongside each binary;
GitHub's automatic source ZIP does not include the pinned JUCE dependency.

![Aurora Pearl Cloud preset](docs/images/aurora-presets-pearl-cloud.png)

## What works in this prototype

- 4096-point Hann-window stereo spectrum, logarithmic frequency display.
- Continuous stereo waveform history with sample peak envelopes, RMS, measured amplitude density and filtered-band contours; Silk, Prism and Trace styles, 1/3/6/12/24-second windows, plus a triggered Scope.
- Shared 1x, 2x and 4x waveform amplitude magnification with explicit fixed scales; raw full-scale detection remains unchanged.
- Cached 256-row spectrogram and a persistent mid/side density scope.
- Width-adaptive spectrum traces with short measured-history trails.
- Sample peak, persistent peak holds and 50 ms RMS.
- 400 ms momentary, 3 s short-term and gated integrated **LUFS estimates**.
- Persistent music-responsive pigment simulation, a three-dimensional stratified Pearl Cloud, and shared floating-point bloom.
- Six custom GPU scenes: Folded Light, Liquid Aperture, Pearl Cloud, Night Transit, Diffraction Veil and Orbit Loom. New instances open with **Pearl Cloud**.
- Twelve original palette presets plus **Custom**, with gradient previews in the selector. New instances use **Phosphor**; Amberline, Ion Blue, Redshift, Petrol and Tape Violet join the original six palettes.
- Three editable gradient stops with RGB sliders and six-digit hex entry; custom colours are saved with the host state.
- Four material themes: **Flight Deck** (default), Soft Club, Afterhours and Chrome; Studio, Meter strip, wide Waveform and scrolling Custom layouts.
- **Customise** controls for each meter, independent module palettes, interface colours, waveform materials and channels, scene finishing and dashboard arrangement.
- Local `.aurora` appearance presets, with import/export and section or appearance resets.
- Drag-to-orbit through 360°, scroll/pinch zoom, double-click camera reset and saved view settings.
- Visual intensity, display freeze, meter reset, resize and saved host-state settings.
- Essential labels and controls, with no descriptive subtitles or routine status footer.
- Mono and stereo paths; bounded FIFO and background analysis keep DSP/rendering off the audio callback.

This is a first working foundation, not full feature parity with Ghost or MiniMeters. There is no true-peak meter, loudness range, calibrated analog VU/PPM, reassigned spectrogram, external audio server, recording or standalone system-audio capture. The included developer preview is a silent synthetic-signal host, not a standalone monitoring product.

## Build on macOS

On macOS, install Apple's Command Line Tools (`xcode-select --install`) and Python 3, then run:

```sh
./scripts/build.sh
```

The script downloads the official JUCE 8.0.6 release into `third_party/JUCE`, installs pinned CMake/Ninja in the project-local `.tools` environment, builds universal Apple Silicon/Intel binaries, and runs the test suite. Nothing is automatically installed into your DAW. Internet is needed for the first dependency download. Rebuilds use the local files. For another platform, use the CMake project with a suitable native compiler; only macOS is validated here.

Outputs:

- `build/Aurora_artefacts/Release/VST3/Aurora.vst3`
- `build/Aurora_artefacts/Release/AU/Aurora.component`
- `build/AuroraPreview_artefacts/Release/Aurora Preview.app` (silent development preview)

## Build on Windows

Use a 64-bit Windows system with Visual Studio 2022 or its Build Tools, the
**Desktop development with C++** workload and Windows SDK, Git, and Python 3.10+.
From PowerShell in this repository:

```powershell
.\scripts\build-windows.ps1
python Tests/WindowsPackagingTests.py
python scripts/package-windows.py --build-dir build-windows
```

The script installs CMake 3.31.6 into `.tools-windows`, fetches and verifies the
exact JUCE commit, builds a Release x64 VST3 with a static MSVC runtime, and runs
the eight native test suites. It does not install a plug-in or alter a DAW.
Output: `build-windows/Aurora_artefacts/Release/VST3/Aurora.vst3`.
Packaging produces a Windows binary ZIP, complete corresponding-source ZIP,
manifest and checksums. The GitHub Actions workflow runs these same commands on
`windows-2022` and retains the resulting download artifacts.

Add `-GuiTests` when building on a Windows desktop to compile the interactive
preview and GPU/UI test apps. Actual Windows DAW scanning, graphics-driver
behavior, high-DPI resizing and audio playback require a separate desktop smoke
test; a successful CI compile is not evidence for those checks. An unsupported
OpenGL context shows a scene fallback while the native meters remain available.

## Editable Diffusion Studio design

The source package includes [the Pearl Cloud Studio project](design/pearl-cloud/README.md):
its editable GLSL, JSX scene, inspector controls and generated shader import.
Official Diffusion Studio v0.211.0 was installed locally and the CLI opened the
project successfully. Five design iterations were rendered in Studio. The
selected fluid-film study was reviewed at 0, 4 and 8 seconds. A local working
copy was used after Studio encountered a problem accessing the cloud-backed
workspace; the editable source remains in this repository. These Studio results
do not establish native shader compatibility or performance.

Run `diffusion open "$PWD/design/pearl-cloud" --background` from the repository
root to open it. This raw WebGL study uses the editor's supplied imports and needs
no local npm install. Edit `design/pearl-cloud/pearl-cloud.glsl`, run
`python3 scripts/sync-pearl-cloud.py` for Studio, and use `--native` to transfer an
revision to the native fallback before rebuilding. The current persistent fluid engine is authored in `Source/PearlFlow.h` and `Source/PearlFlowMaterial.h`. The design README describes
capture and freeze/bounds checks. Studio is optional for native builds; its app,
downloads, caches, working captures and node modules are not included in Aurora's
source archive. Packaging verifies that both shader embeds match the GLSL source.

## Use in Ableton

Run `./scripts/install-macos.sh` to install locally (it backs up any previous Aurora build), or copy the VST3 bundle to `~/Library/Audio/Plug-Ins/VST3/` or the AU bundle to `~/Library/Audio/Plug-Ins/Components/`. Enable the corresponding system plug-in folder in Live's Plug-Ins preferences, rescan, and find **Aurora** under **Soft Club**. Add one instance to a track or master bus. Test in a separate set first. Play audio through that track to drive the meters.

`Freeze` holds only the display. `Reset` clears integration and peak history. The intensity slider changes only creative visual intensity. Choose a scene, material theme and palette independently. Existing sessions retain their saved theme and palette; choose **Flight Deck** and **Phosphor** to apply the new direction to an older instance. Click any of the three colour swatches to edit a gradient stop using RGB sliders or a `#RRGGBB` hex value; editing a preset switches to **Custom** while retaining its other two colours. Drag inside the large scene to orbit it, scroll or pinch to zoom, and double-click to restore the view. When the scene has keyboard focus, arrows orbit, +/− zoom and 0 resets. Camera, scene, theme, palette and custom colours are saved with the plug-in state. Camera controls remain available while frozen. Zooming in deliberately allows a close crop; the reset view is fitted to the panel. The level bars are sample peaks and the LUFS values are estimates; no metering standards certification is claimed.

**Waveform:** choose **Waveform** in the layout selector for the wide two-lane view,
or use the waveform panel in Studio or Meter strip. Its time menu offers **1 s,
3 s, 6 s, 12 s, 24 s** and **Scope**. **Silk** emphasizes measured sample density,
**Prism** emphasizes filtered-band RMS contours, and **Trace** emphasizes fine
RMS and band lines. All three share the same signed sample-peak envelope.
**Bands** blends apricot lows, mint mids and violet highs; **Palette** uses your
three chosen RGB colours, mapping RMS level through the gradient and applying
the selected material's shading. Choose **1x**, **2x** or **4x amplitude** for
a shared L/R scale of ±1, ±0.5 or ±0.25 at the default display gain. This changes only the view. Edge marks
indicate raw samples reaching ±1 at their actual sample positions; magnified
signals touching an edge are not automatically flagged. Scope retains its
separate triggered close-up and disables the scrolling material/zoom controls;
its shared adaptive amplitude scale is labelled. Customise can adjust its display
gain, channels, fill, crest, grid and labels.
Hover the history
for peak/RMS values, band-energy percentages and precise peak times; Freeze holds
those measurements for inspection. History is retained in memory, including while the
editor is closed; Freeze holds the display while analysis continues.

**24 complete looks** are built in. Choose a name from the preset menu beside the
logo, or click the previous/next arrows to cycle through the collection. These
change the full appearance: interface, module palettes, layout, waveform material,
scene lighting, motion and camera. The dropdown includes gradient swatches.
**Customise → Presets → Collection** opens a scrollable gallery with palette and
layout illustrations. The gallery expands to two columns in a wider window.

The collection includes **Pearl Cloud**, **Black Opal**, **Sea Glass**, **Rose Quartz**,
**Amber Archive**, **Cobalt Night**, **Ultraviolet**, **Afterglow**, **Glacier**,
**Verdigris**, **Supernova**, **Velvet Static**, **Citrus Circuit**, **Moonstone**,
**Porcelain**, **Infrared**, **Abyss**, **Honeycomb**, **Sakura**, **Magnetic Tape**,
**Blue Hour**, **Mercury**, **Laser Bloom** and **Quiet Geometry**. They span all
six scenes, eight Studio views, ten custom arrangements, three meter strips and
three wide waveform views. Moonstone, Porcelain and Sakura use light interfaces.

An asterisk beside a preset name means its settings have been edited. Factory
looks remain available unchanged; save your variation under **Presets → Saved**.
Selecting a look preserves the current measurements and Freeze state. Existing
sessions and the Pearl Cloud scene default remain unchanged until you select one.

**Customise** expands a dropdown inside the plug-in window. Click it again or
press Escape to close it. On shorter layouts, scroll the dropdown to reach all
settings. Choosing **Meter strip** or **Waveform** from the header closes the
dropdown and shows that layout immediately. Changes are saved in the host session.
The sections are:

- **Colours:** theme, three-stop gradient, reversal and meter gradient midpoint;
  any RGB/hex colour for the interface and clip markers, plus independent palettes
  for all six modules. Editing a module colour enables its palette automatically.
- **Waveform:** material, colour mode and individual low/mid/high colours; density,
  RMS body, peak crest, band detail, fill and gradient depth; grid and labels;
  Stereo, Left, Right or Overlay channels. Enable **Custom time window** for
  0.25–24 seconds. Display gain multiplies scrolling amplitude zoom, and the axis
  reports the resulting scale. Scrolling-only controls are disabled in Scope.
- **Spectrum:** frequency and dBFS limits, line width, fill, trail duration and grid.
- **Spectrogram:** dBFS limits, 3–24-second history, contrast, direction and grid.
- **Stereo:** persistence, brightness, rotation, grid and manual or automatic display gain.
- **Levels:** dBFS floor, warning threshold, RMS shading, peak holds, segments and loudness readouts.
- **Scene:** visual, intensity, camera, flow speed, music sensitivity, bloom,
  exposure, saturation and a 15/30/60 Hz frame-rate target. Speed zero holds flow;
  **Freeze** also holds the captured audio response. Exposure is a multiplier.
- **Layout:** choose a layout, adjust panel spacing and the built-in layout
  proportions, or show, hide and reorder modules in a one-, two- or three-column
  **Custom** dashboard. Visibility, position and column edits select Custom
  automatically. Use its right-hand scrollbar when the panels exceed the window.
- **Presets:** **Collection** browses the 24 built-in looks. In **Saved**, **Save as new** adds the current appearance to the library;
  **Replace selected** updates an existing entry; **Load** recalls it. On macOS
  the library is `~/Library/Aurora/Presets/`, with one `.aurora` file per preset.
  On Windows it is `%APPDATA%\Aurora\Presets\`.
  **Export** writes the current appearance to a chosen file; **Import** applies
  an appearance file. Use **Save as new** after importing to add it to the library.

**Reset section** restores that section's defaults. **Reset appearance** in
Presets restores all appearance settings, including Pearl Cloud, Flight Deck,
Phosphor and the Studio layout. Presets include camera and display settings;
loading or resetting appearance preserves audio, measurement history and the
current Freeze state. The main **Reset** button separately clears measurements.

## Measurement and architecture

See [meter definitions](docs/METERING.md), [reference research and roadmap](docs/REFERENCE-RESEARCH.md), [visual engine research](docs/VISUAL-RESEARCH.md), and [validation record](docs/VALIDATION.md).

`PluginProcessor` copies incoming PCM into a fixed-size single-producer/single-consumer queue. It does not write host audio, allocate buffers, lock a mutex or run FFTs in `processBlock`. An analysis worker drains the queue and publishes snapshots. The visible, running native meters request repaint at 60 Hz; the fluid scene targets 30 Hz by default, with 15 and 60 Hz options. Actual frame delivery depends on the host and GPU. The GPU context belongs to each open editor; closing the editor releases it. Overflow drops analysis data and resets the measurement window; a message appears only when that event occurs. Host audio still passes through.

Meters use native 2D drawing. Spectrum trails default to 220 ms of measured curves;
the stereo density field defaults to 420 ms with gently released shared display normalization.
Both durations and stereo display gain are configurable.
Unchanged palettes and spectrogram colours reuse their caches. Covered meters
and hidden scenes skip rendering work, and only Pearl Cloud runs the fluid
solver. Opening Customise preserves the existing GPU context and scene resources.
Waveform history is accumulated from every supplied PCM sample on the analysis worker,
at approximately 1,000 buckets per second. Each bucket retains signed minima/maxima,
exact peak sample positions, double-precision squared-sample and filtered-band energy
sums, and a 65-bin signed amplitude histogram independently for left and right.
Histogram counts and energies are summed before rendering at a different time
window or pixel density. RMS and colour are calculated after grouping
at the actual display pixel density; changing width does not average normalized colours.
The final partial bucket is also published, so stopping between bucket boundaries
does not lose the final samples.
The selected amplitude axis stays fixed rather than following recent peaks.
`WaveformRibbon.h` shades the measured density, RMS and band contours into a
circular pixel cache, reusing stable columns during fractional scrolling.
Density brightness is normalized and interpolated for appearance; it is not
a calibrated probability scale or a reconstruction of individual samples.
A fractional sample clock with an 80 ms
display buffer scrolls the history smoothly between worker updates, holds when
audio stops, and reanchors after Resume. This delay affects the display only.
The older triggered close-up remains available as **Scope**. Integrated LUFS has a larger readout; compact mode retains all three
loudness readings. Hovering the spectrum displays frequency and measured dBFS.
The 480×256 cached spectrogram holds up to 24 seconds of UI-sampled FFT history
(12 seconds shown by default),
recolours existing data when the palette changes, and leaves missed intervals blank.

Pearl Cloud now transports persistent pigment through a GPU fluid field: velocity
advection, curl forces, pressure projection and dye transport run in bounded fixed
steps. Separate pigment channels allow instant custom colour changes. Three curved optical
strata occupy actual depth and are integrated along 128 samples per view ray,
with relief, transmission and pearlescent highlights. The camera rotates around
this layered volume. A support mask contains the composition; display finishing is not fed back into the simulation. Freeze and
silence stop the simulation. `PearlFlow.h` contains the solver and its MIT notice;
`PearlFlowMaterial.h` contains the original material. All six scenes receive a
floating-point finishing pass with restrained quarter-resolution bloom.

The earlier Diffusion Studio study remains an editable design and a native fallback.
Its deterministic domain-warp shader is distinct from the new persistent native
simulation; the earlier Studio movie does not demonstrate the new fluid engine.

The other five scenes use the same evolving pigment field on OpenGL surfaces,
with original mode-specific laminae, aperture engraving, terrace reflections,
diffraction weaves and helical windings. All retain three-stop gradients. Their projection is fitted from the eight
corners of each scene's bounds. Artistic features use adaptive level normalization, fast attack/release and
transient envelopes in `VisualReactivity.h`; they remain responsive across
recording levels above a quiet gate. These are creative controls, not a tempo
or beat detector. Shaders receive copies of these analysis features; those never feed back into the meter values. The UI samples the most
recent FFT at display cadence, so spectrogram history is approximate wall time
and not a lossless recording. Offline export can outrun analysis; the audio
export itself is unaffected. No incoming samples are written to disk.

## License and provenance

Aurora's original source is licensed under **GNU Affero General Public License v3.0 or later**; see [LICENSE](LICENSE). This matches the open-source route used for JUCE 8. JUCE is pinned to 8.0.6, commit `51a8a6d7aeae7326956d747737ccf1575e61e209`. Its bundled dependencies retain their own notices, including the VST3 SDK and Apple AudioUnitSDK. See [THIRD_PARTY.md](THIRD_PARTY.md).

Ghost and MiniMeters were studied as functional references. No vendor plug-in binaries, source code, graphics, presets or branding were copied into Aurora. The interface and DSP are original. This repository has not been published to an external service.

## GPU visual validation and preview

Run `build/AuroraRenderTests_artefacts/Release/"Aurora Render Tests.app"/Contents/MacOS/"Aurora Render Tests"` in a graphical desktop session for the explicit GPU checks. Add `--clip` to save 240 native frames under `/tmp/aurora-flow-clip`. Compile `scripts/render-preview-video.swift` with Swift and run it with an output MP4 path to encode those frames at 30 fps. Both tools are development-only and open no audio device. The earlier 8-second native fluid preview, before orbit and transient controls, is `design/pearl-flow-preview.mp4`; it is separate from the earlier Diffusion Studio study.


The separate `AuroraWaveformTests` target captures the native waveform/UI and
checks its clocks, freeze/resume and stopped input without opening an audio device.
Its default synthetic fixture contains no intentionally injected overload spikes.
Pass `--impulses` to include isolated full-scale transients for explicit stress
checks. `--music` selects an original procedural stereo music fixture instead;
`--clip --music` records four seconds of native waveform motion plus a full-editor
PNG under `/tmp/aurora-waveform-motion`, without playing sound. The clip report
records actual capture times and missed frame slots. To retain that timing, run
`render-preview-video.swift`'s compiled executable with output MP4, PNG directory
and `report.json` as its three arguments. No intermediate frames are invented.
None of these fixtures is recorded music. See `docs/waveform-validation.json`.
