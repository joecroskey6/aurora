# Validation record

## Windows x64 preview — 15 October 2026

The Windows VST3 was built from commit
`edb160bb96c38176b2fca037550e9cc8a137b11f` on GitHub's Windows Server 2022
runner using Visual Studio 2022/MSVC, CMake 3.31.6, a static C++ runtime and
JUCE 8.0.6 at `51a8a6d7aeae7326956d747737ccf1575e61e209`.
The [build and native test run](https://github.com/joecroskey6/aurora/actions/runs/38705922324)
completed successfully. The Windows source and binary are a separate preview;
the existing macOS release is unchanged.

- All **8 native CTest suites passed (12.71 s)**: analysis, visual framing,
  visual reactivity, waveform clock, waveform projection, waveform ribbon,
  processor and editor performance. This includes 26,976 processor checks,
  94 CPU editor checks and more than one million waveform checks.
- All **8 packaging regressions passed** on Windows. The packager verified an
  AMD64 PE32+ DLL with the VST3 `GetPluginFactory` export, archive integrity,
  matching source hashes and the exact JUCE dependency. The distribution is
  unsigned and includes complete corresponding source, notices and checksums.
- The exact downloadable VST3 passed **pluginval 1.0.4 at strictness level 5**
  in a [separate Windows host run](https://github.com/joecroskey6/aurora/actions/runs/38708547248).
  Scanning, cold/warm loading, processing at 44.1/48/96 kHz with 64–1024-sample
  blocks, state and bus checks completed with exit code 0 and a verified
  `SUCCESS` log. GUI tests were explicitly disabled; no external Steinberg
  validator was supplied. [Retained evidence](windows-preview-validation.json)
  includes the exact binary hash and test logs.
- A further [Windows run with GUI tests enabled](https://github.com/joecroskey6/aurora/actions/runs/38709472491)
  passed the same level-5 suite, including `Editor`, `Open editor whilst
  processing` and `Editor Automation`. This exercises native editor opening,
  reopening and operation alongside synthetic processing. The downloaded DLL
  hash is identical to the release. It does not check the appearance of rendered
  pixels, all controls or hardware-specific GPU behavior.
- Independent archive verification matched all **4,339 source file hashes**:
  117 Aurora files match the build commit (81 use Windows CRLF line endings),
  and all 4,222 JUCE files byte-match the official pinned Git tree. Both ZIPs
  passed CRC and path checks. Uploaded GitHub asset digests match local files.
- The Windows port uses native Windows fonts and platform temporary directories.
  It rejects unsupported OpenGL contexts before accessing shader entry points;
  native meters remain available with a scene fallback. The headless editor
  suite exercises initialization and cleanup with unavailable GL functions.
- The same source also passed a universal macOS build and all **8 CTest suites
  (6.05 s)**. A production GPU smoke test on Apple M2 Max rendered all six scenes
  with zero GL errors; Pearl Cloud evolved and inactive fluid resources remained
  unchanged for the other five scenes. This is Mac GPU evidence only.

Windows graphics-driver compatibility, actual DAW playback, exhaustive control
interactions, visual output, high-DPI scaling and long sessions remain unverified. The
[installation guide](../release-docs/WINDOWS-INSTALLATION.txt) includes a desktop
test checklist. Automated headless results do not establish those checks or
metering-standards compliance.

## Inline controls and rendering performance — 14 October 2026

Customise now expands inside the editor and closes with its header button or
Escape. Short layouts scroll the settings content. Selecting a header layout
closes the dropdown and immediately reveals the meters. Meter strip and Waveform
selection had an asynchronous ComboBox notification race: timer synchronization
could restore the previous value before the click callback ran. Controls now
synchronize only when their underlying model value changes.

Rendering now reuses unchanged palette and spectrogram caches, moves settings
lookups outside pixel loops, and skips covered or absent scene/stereo work.
Only Pearl Cloud runs its fluid solver; other scenes initialize it lazily if
needed. Opening Customise parks the existing GL surface outside the viewport,
preserving its context and resources. Frozen unchanged scenes avoid redraws.
Waveform resolution, measured PCM, fluid resolution and material detail remain
unchanged.

**Verification:**

- Universal arm64/x86_64 AU, VST3 and native QA builds succeeded. All **8 CTest
  suites passed (10.89 s)**, including 91 editor cache/raster checks. Stereo and
  spectrogram optimization output matched the reference pixels exactly.
- Native customization QA passed **520 checks, 53 captures, zero failures**.
  Tests force the asynchronous menu/timer race, exercise the header and embedded
  Layout row, verify 390/570-pixel layout heights, and cover scrolling, toggle,
  Escape, same native peer and absence of an additional settings window.
- Factory preset QA passed **1,044 checks, 42 captures and 17 actual GPU
  captures**. All 24 looks retain their settings and measurements. Repeated
  dropdown open/close and gallery browsing preserve the same non-null GL context.
- CPU-only median stereo raster cost fell from **14.561 ms to 0.462 ms**;
  full spectrogram recolouring fell from **58.895 ms to 10.740 ms**. Unrelated
  settings now reuse that spectral image. These compare retained test-only
  baseline routines with the production renderer using identical synthetic data.
- On Apple M2 Max at 1440×900 physical pixels, 30 synchronized frames per scene
  showed **64–75% lower median rendering cost** for the five non-Pearl scenes
  (2.357–2.900 ms before, 0.712–0.881 ms after). The baseline reinserts the old
  unused fluid workload into the current renderer; it is not an old-binary
  comparison. All GL checks passed, inactive fluid pixels stayed unchanged,
  and Pearl Cloud still evolved. No Pearl Cloud speedup is claimed.
- Apple's Audio Unit validator passed. Both signed universal formats were
  installed; executable hashes match the tested build. Previous versions are
  retained at `backups/20261014-113906-Aurora.vst3` and
  `backups/20261014-113907-Aurora.component`.

Build tools and source were staged in a temporary local directory after slow
filesystem reads disrupted the original build. All 43 source/test/CMake files
were hash-checked against the workspace and the built release. Full reports,
source and installed hashes, and validation output are recorded in
`docs/inline-controls-validation.json`. Native captures are in
`docs/images/aurora-inline-*`. Tests used synthetic PCM without an audio device
or changes to the user's preset library. The timings are isolated local
measurements, not an Ableton frame-rate or real-time audio guarantee. Real
Ableton playback, Intel runtime and other GPU configurations remain unverified.

## Factory preset collection — 13 October 2026

Added **24 complete factory looks** with distinct three-stop palettes, interface
colours, scenes, meter settings and module arrangements. The collection includes
8 Studio, 10 Custom, 3 Meters and 3 Waveform layouts, all six scene types, and
three light treatments. Each look explicitly supplies all 112 appearance XML
attributes so selection is deterministic even after extensive customization.
Existing startup defaults remain unchanged; Pearl Cloud is still the default scene.

The header selector and previous/next buttons apply a whole look. The native
**Customise → Presets → Collection** browser provides scrollable illustrated cards;
**Saved** retains the personal preset library. Edits mark the selected factory
name with an asterisk, and every setting remains editable. Factory selection
preserves measured PCM, meter histories and Freeze.

**Verification:**

- Universal arm64/x86_64 AU, VST3 and native QA builds succeeded.
- All **7 CTest suites passed (5.18 s)**. The processor suite passes **26,976
  checks**, including complete factory state, bounds, palette uniqueness, exact
  matching, numeric XML formatting, malformed input, host/preset round trips,
  measurement preservation and interface text contrast.
- Native preset QA passed **1,020 checks with 39 editor/gallery captures and 18
  additional GPU captures**. It selects all 24 looks through the actual header,
  tests previous/next wrap, modified-state labels, gallery selection and both
  tabs, and checks minimum editor/gallery dimensions.
- Full editor images include actual OpenGL scene pixels read on the GL thread
  from the presented front buffer and composited at the real clipped bounds.
  Every captured GPU surface passed error and nonblank checks. Separate GPU PNGs
  retain native physical resolution. Gallery miniatures are illustrations of
  layout and colour, not captured audio or rendered scene thumbnails.
- Apple's Audio Unit validator passed. Signed AU/VST3 bundles were installed
  with previous builds backed up at `backups/20261013-144805-*`. Installed
  executable hashes match the universal build and signatures verify.

The screenshots in `docs/images/aurora-presets-*` show the gallery and selected
actual editor looks. Full results, source hashes, installed hashes and AU output
are in `docs/factory-presets-validation.json`. Native tests used synthetic PCM
without opening an audio device or modifying the user's preset library. Real
Ableton playback, Intel runtime and other GPU configurations remain unverified.

## Customisation system — 13 October 2026

Added 96 validated settings alongside the existing scene/theme/palette controls.
The native **Customise** window exposes arbitrary RGB/hex colours, separate module
palettes, waveform materials and channel modes, meter ranges and persistence,
scene finish/motion/camera, custom module visibility/order/columns and reusable
appearance presets. Settings persist in host state; presets and appearance reset
preserve measurement histories and Freeze. Legacy state uses the new defaults.

**Verification:**

- Universal arm64/x86_64 AU, VST3 and native QA build succeeded.
- All **7 CTest suites passed (5.38 s)**, including 459,292 waveform raster checks,
  custom spectral range reference tests, preset validation and all 720 layout
  permutations through host-state and preset round trips.
- Native customization QA passed **412 checks with 36 captures**: all nine
  sections, actual editor settings-window open/close, exact `#13579B` colour
  entry through the native picker, Scope control pixel comparisons, custom-time
  transitions, Stereo/Left/Right/Overlay, minimum window, scrolling and reordering.
- Native GPU customization QA passed **62 checks** on Apple M2 Max: real shader
  pixels respond to bloom, exposure and saturation; held redraws and pigment are
  stable; zero speed holds fluid, and resuming evolves it. The harness exercises
  the production post-process and fluid material directly, not the VisualSurface
  UI/mutex bridge or the other scenes' geometry.
- Native waveform regression passed **62 checks with 25 captures** and zero
  sample-delivery error. Captures validate 2D UI/waveform; component snapshots do
  not include the separate OpenGL scene. The fixture uses synthetic PCM without
  an audio device and is not an Ableton performance benchmark.
- Apple Audio Unit validation passed. Ad hoc signed AU/VST3 bundles were installed
  with backups at `backups/20261013-141609-*`; installed executable hashes match
  the universal build. Real Ableton playback of this revision remains unverified.

The native settings and RGB-picker screenshots are in `docs/images/aurora-customise-*`.
Complete reports, final source hashes, installed hashes and validation output are
recorded in `docs/customization-validation.json`. No commercial source or assets
were copied. Reference comparison used the official [MiniMeters help](https://minimeters.app/help/),
[MiniMeters layout guide](https://minimeters.app/help/rearranging-meters/), and
[Ghost product page](https://adieusounds.com/plugins/ghost); this is not a claim of
complete competitor feature parity or a measured customization ranking.

## Measured waveform materials — 13 October 2026

The waveform now renders three original materials: **Silk**, **Prism** and
**Trace**. Each stereo time bucket retains 65 signed PCM amplitude-occupancy
counts, alongside exact min/max positions and raw sample/filter energies.
Counts combine without loss during projection. Interior pigment comes from this
measured distribution, RMS and filtered-band RMS contours; it introduces no
synthetic waveform paths. Gradients stay within the signed envelope. Brightness
is normalized for presentation, not a calibrated probability scale.

Added 1 s and 3 s history windows to the existing 6/12/24 s and Scope choices.
Shared 1x/2x/4x vertical magnification has fixed, explicit ±1/±0.5/±0.25 axes.
Numeric measurements and raw full-scale flags stay unchanged. All controls save
with host state; legacy sessions default to Silk and 1x. Bands uses apricot/mint/
violet; Palette draws from the user's arbitrary RGB stops. The material pass
preserves spectral hue, with restrained density highlights and fine crests.

An independent cache audit reproduced and fixed stale colour bleed at data gaps
and fragment shading outside magnified amplitude bounds. Populated atlas runs
now exclude missing history. Constant lane clips, cached crest pixels and lazy
bitmap writes remove thousands of per-column native graphics operations. Separate
1x and Retina caches avoid repeated full rebuilds during native image captures.
Stream resets invalidate both caches.

**Final verification:**

- Universal arm64/x86_64 build succeeded. All **7 CTest suites passed (4.81 s)**.
- **538,983 projection checks**, **380,642 raster/cache checks** and **24,324
  clock checks**. Cache checks compare fresh/cached pixels at fractional positions,
  gaps, wrapping, fragments, partial tails, material changes, DC/silence and 1/2/4x.
- Both native normal and explicit-overload runs passed **62 checks / 25 captures
  each**, with zero sample-delivery error. Held Prism/Trace pixels were identical;
  restoring Silk restored its exact interior. Layout, custom colours and zoom
  axes were inspected in native captures.
- Original procedural stereo music capture: **120 native frames / 4 seconds**,
  no missed nominal 30 Hz slots, zero sample-delivery error, no audio output.
  MP4 timestamps retain actual capture intervals; no frames are interpolated.
- Apple Audio Unit validation passed. Signed AU/VST3 bundles were installed with
  backups at `backups/20261013-132927-*`; installed executable hashes match build.

The final normal harness averaged **17.95 ms** between steady timer callbacks
(p95 **20.83 ms**); its 25 raster snapshots averaged **7.06 ms**. The separate
music capture averaged **4.01 ms** per snapshot. These measure GUI-thread QA,
not an Ableton frame-rate guarantee. Software-only renderer benchmarking at
1280×340 logical size showed ~2 ms steady 1x and ~8 ms steady 2x on this machine;
cold materials cost more and each steady benchmark frame changed one real column.
Analysis history storage is now 13.5 MiB, including the added occupancy counts.

`docs/images/aurora-waveform-music.png` and `aurora-waveform-detail.mp4` show the
native release renderer with original procedural music. The source provenance,
full native reports, source hashes and installed binary hashes are in
`docs/waveform-validation.json`. Research covers Mixxx, Audacity, Sonic Visualiser,
Wavesurfer, BBC Peaks/audiowaveform and Signalizer; no code or assets were copied.
See `docs/VISUAL-RESEARCH.md` for source links and limitations. Real Ableton
playback of this revision remains unverified.

## Waveform accuracy audit — 13 October 2026

The reported blue needles were traced to deliberately injected one-sample
+1.08/−1.04 overloads in the synthetic screenshot fixture. The normal fixture
now excludes these. `--impulses` explicitly enables them for stress validation.
Both preview hosts identify synthetic input; their PCM clocks now follow elapsed
time, including exact final partial blocks, rather than GUI callback count.

Actual implementation fixes from the audit:

- Approximately 1 ms history buckets, with 24.25 seconds of retention.
- Double-precision raw sample and filtered-band energy sums, normalized only
  after display grouping; no reconstruction from rounded RMS/band fractions.
- Exact earliest sample positions for extrema and sample-positioned clip marks.
- Provisional final buckets publish immediately and replace safely on polling.
- Independently tested projection at physical pixel density, with contiguous
  sample-time groups and no bridging across missing data.
- No artificial minimum waveform height; DC is a line at its actual value.
- Hover measurements report peak, RMS, sample count, peak time and band energy.
- Mono input consistently duplicates the declared input channel, even when an
  API caller supplies a conflicting right pointer.

All **6 CTest suites passed (4.87 s)**. Apple Audio Unit validation passed.
The AU/VST3 bundles were installed with backups and their installed executable
hashes match the final universal build. The projection suite passed **404,247
checks**, including independent PCM reductions at 44.1/48/192 kHz, 6/12/24 seconds
and several physical widths, energy conservation, signs/DC, future/partial
columns, gaps, extreme values and last-sample impulses. Analysis tests independently
verify the filter output, raw energies and exact timestamps; extreme-value tests
cover 8/44.1/48/96/192/384 kHz. The clock suite retains 24,324 checks.

Both native runs passed **30 checks each**, capturing 12 editor images per run.
The normal and explicit-impulse fixtures both had **zero sample-delivery error**
and exact analyzer/delivery totals, stable freeze/stopped pixels, working resume
and in-bounds controls. The clean 24-second, Palette and minimum strip images
were visually inspected. Existing stress spikes remain represented honestly
when their input is enabled. The clean screenshot from that audit is
`docs/images/aurora-waveform-accuracy-24s.png`; `aurora-waveform-stress.png` is the explicitly
injected overload fixture. These use synthetic PCM, not recorded music.

The clean harness's steady timer intervals averaged 17.72 ms (p95 23.39 ms),
excluding startup and the interval after capture/layout changes. Raster snapshots
averaged 7.13 ms, max 8.37 ms. The GUI-thread feed, capture work and separate native
window affect these observations; they are not an Ableton frame-rate guarantee.
An optimized analysis-only benchmark processed ten seconds of stereo PCM in
51.5 ms at 48 kHz and 203.3 ms at 192 kHz (median of five after warm-up).

`docs/waveform-accuracy-validation.json` stores both complete native reports and
source hashes from that audit. The initial record remains separately archived below. Real
Ableton playback of this revision remains unverified.

## Initial continuous waveform revision — 13 October 2026

The scrolling waveform now uses continuous PCM-derived, sample-timestamped
min/max/RMS buckets, independent of FFT or GUI polling. Both lanes share a
fractional sample clock with an 80 ms target display buffer. The 6/12/24-second
windows, Bands/Palette selection and wide Waveform layout persist in host state.
The close-up triggered view remains available as Scope.

The universal build passed all **5 CTest suites (3.43 s)** and Apple Audio Unit
validation. AU/VST3 bundles were installed with backups; installed binary hashes
match the build. Waveform tests cover
chunk/poll independence, exact signed extrema and RMS, isolated impulses, stereo
frequency weights, mono, ring wrap, reset/format changes and finite handling.
The clock suite passed **24,324 checks**, including block sizes 64–2048 at
44.1/48/96 kHz, delivery jitter/bursts, bounded progress, stops, freeze/resume and
stream resets. Processor tests retain bit-exact audio pass-through coverage and
verify new selector persistence, old presets and bounds.

A separate native `AuroraWaveformTests` app captured **12 actual JUCE editor
images**, opening no audio device. All 28 checks passed: control bounds in the
wide, Studio and minimum Meter strip layouts, exact frozen pixels while PCM
continues, changed pixels after Resume and exact stopped-input pixels after the
available tail. The screenshots were inspected for visible controls, stereo
separation and readable time scales. Their Studio OpenGL child is omitted by
component snapshots; that image is not evidence of the fluid renderer.

The GUI harness requests 60 Hz; observed timer intervals averaged 20.15 ms
(p95 21.14 ms), including PNG capture, analyzer work on the GUI thread and
window/layout changes. Maximum was 288.79 ms during this instrumentation. Raster
snapshot generation averaged 5.54 ms, max 6.99 ms. These do not establish sustained
60 fps in Ableton. The regular plug-in runs analysis on its background worker.
A separate optimized analyzer benchmark processed ten seconds of stereo PCM
in a median 49.1 ms at 48 kHz and 195.8 ms at 192 kHz (five runs after warm-up;
excludes UI and snapshot copies).

`docs/waveform-initial-validation.json` retains this earlier record. Its fixture
intentionally injected single-sample overloads, visible as tall blue needles in
`docs/images/aurora-waveform-initial-stress.png`. Its fixed-per-callback input
also ran slower than the declared rate during GUI delays; the later accuracy
revision corrects that fixture. Those needles were synthetic inputs, not a
recording or live music. Real Ableton playback of this revision remains unverified.


## Flight Deck design revision — 13 October 2026

The final universal AU/VST3/preview build passes all four CTest suites (3.13 s),
including all 312 scene/palette/theme state combinations and explicit legacy
palette/Custom-ID compatibility. New instances use Pearl Cloud, Flight Deck and
Phosphor. Existing sessions retain their saved appearance.

The GPU runner exits successfully with 73 checks passed. Added checks verify
Flight Deck recolouring, exact frozen output and unchanged pigment data. The
runner now stops its polling timer before requesting shutdown; this corrected
a repeated macOS shutdown request observed after an earlier successful render.
See `flight-deck-validation.json` for source hashes and rendering evidence.

The complete native Studio view was visually inspected with synthetic PCM;
`images/aurora-current.jpg` captures the new interface. Compact panel geometry
was reviewed in source, and Studio now has a 620-pixel minimum height to prevent
scope inversion; Meter strip retains its 390-pixel minimum. Final interactive
menu/picker checks were limited by macOS UI automation failing to focus the app.
The new palette menu previews are implemented and compiled, but were not visually
verified open in this pass. The earlier colour-picker interaction checks remain
recorded below.

The signed AU and VST3 were installed with backups; Apple's AU validation passed.
Actual Ableton playback and Intel runtime remain unverified. No audio analysis
formula or pass-through processing was changed for this design revision.

## Orbit, reactivity and instrument display revision — 13 October 2026

The final universal AU/VST3 build passes all four CTest suites (3.20 s): analysis,
visual framing, visual reactivity and processor. Real PCM tests compare sustained
notes with kicks/snares and the same recording 12 dB quieter. All attacks remain
visible in the artistic response; the maximum normalized-control difference was
0.0000142. This is a controlled synthetic fixture, not listening-session validation.
Camera persistence, defaults, limits and malformed values are covered by state tests.

Framing adds 1,176 full-orbit configurations to the prior 8,640 cases. All five
meshes and the layered volume rendered in the native editor. Drag orbit, wheel
zoom and double-click reset were exercised with the scene frozen. Zoom above one
intentionally permits cropping; reset framing keeps the complete shape inside
the viewport. The volume is a three-dimensional arrangement of fluid-derived
strata, not a full 3D Navier–Stokes solver.

The final material passes 67 real GPU checks, including side/back views, zoom,
exact camera reset and unchanged frozen simulation data. At 1200×720 on M2 Max,
30 full fluid/material/bloom frames measured 7.93 ms mean, 10.31 ms p95 and
11.04 ms maximum. This includes the 128-sample volume and excludes native meter
drawing and DAW costs. See `orbit-flow-validation.json` for details and source hashes.

The signed AU/VST3 bundles were installed with backups and Apple's `auval` passed.
Studio and minimum-height Meter strip layouts, Soft Club and Afterhours contrast,
custom RGB/hex controls, and all six scene materials were visually inspected.
The source waveform reduction was corrected to include every sample in the
displayed window. The preview drives the real processor with synthetic PCM and
opens no audio hardware. This revision has not been tested during actual Ableton
playback or on an Intel Mac. Older measurements below refer to earlier revisions.

## Earlier revision — 12 October 2026

Recorded for Aurora 0.1.0. This records completed checks and their limits for the simplified interface, custom gradient controls and revised scenes.

## Confirmed checks

| Area | Evidence and result |
| --- | --- |
| Plugin builds | AU and VST3 bundles built successfully. Inspection of both Mach-O executables confirms arm64 and x86_64 slices. This establishes universal packaging, not Intel runtime validation. |
| Automated tests | Latest CTest run passed `analysis`, `visual_framing` and `processor`, totaling 2.94 s. State tests include all 126 combinations of six scenes, seven palette selections and three themes, plus custom RGB values, masked input, new defaults and older presets. |
| Audio Unit validation | Apple's `auval` reported `AU VALIDATION SUCCEEDED` for `aufx / Auro / Aucl`. The log includes opening, mono/stereo render, varying buffer-size/sample-rate, and connection checks. This is an AU validation result, not metering certification. |
| Native visual preview | The native OpenGL preview displayed all six scenes and all three themes, with palettes controlled independently. A shader compilation failure caused by the reserved word `filter` was identified and fixed; the fallback display was also exercised. The latest fluid-film Pearl Cloud compiled and rendered successfully in native OpenGL with the default Soft Club/Glacier controls and input from the synthetic PCM processor path. Separate native screenshots show its shape evolving. The prior native preview was also observed with the bounds-fitted Liquid Aperture at maximum intensity. The complete set of scene/colour/window combinations has not been exhaustively checked by eye. |
| Ableton Live 11 discovery | Live's browser found both AU and VST3 entries. A VST3 instance was loaded into a newly empty set. Its plugin GUI was not visibly verified, and no playback test was completed in Live. |

The developer preview generates stereo PCM and sends it through the actual processor and analysis worker. It opens no audio hardware and plays or records no sound. Its meters and visuals therefore exercise the processing path, but do not establish performance or sound behavior during a DAW playback session.

## Persistent native fluid revision

The new native `PearlFlow` solver, optical material and bloom pipeline passed **59 actual GPU checks** on an Apple M2 Max (OpenGL 4.1 Metal). The suite checks shader/FBO support, finite bounded pigments, visible evolution, exact frozen textures and composed pixels, inactive/silent texture holds, reproducible reset/replay, a 900-step continuation, and each custom colour stop independently. Palette changes preserve simulation data. This is one GPU/driver configuration, not cross-device validation.

At **1200×720**, 30 synchronized frames of the full fluid/material/bloom pipeline with `dt=1/30` (two solver steps per displayed frame) took **2.76 ms mean, 2.64 ms median, 3.96 ms p95, 4.41 ms maximum**. These are `glFinish` wall-time measurements in the dedicated harness; they exclude DAW and native meter/UI cost and do not establish a whole-plugin frame-rate guarantee.

The final AU/VST3/preview build passed CTest (3/3, 2.94 s) and Apple's AU validator. The full native editor visibly rendered the final Pearl Cloud, including the fullscreen-triangle optimisation and the new spectrum/spectrogram/stereo history. New bundles were installed with backups. Real Ableton playback of this revision remains unverified.

`Tests/RenderingTests.cpp` is an explicit GUI GPU runner (`AuroraRenderTests`), separate from headless CTest. `docs/pearl-flow-validation.json` records checks, timings, fixtures and source hashes. `docs/images/pearl-flow.png` and `pearl-flow-custom.png` are actual native GPU outputs. `docs/images/aurora-current.jpg` is the full native editor. The 8-second `design/pearl-flow-preview.mp4` uses 240 actual native frames at 1200×720/30 fps with deterministic synthetic feature changes and no audio; it is distinct from the older Studio movie.

The living material has an explicit inset support and an additional soft elliptical taper. Bloom can extend light beyond that material support. The older Studio pixel-equal margin checks below validate the analytic fallback only; they do not assert identical edge pixels in the new composed output.

## Earlier Diffusion Studio render checks (analytic fallback)

The official Diffusion Studio 0.211.0 application was installed and used to build,
render and compare five Pearl Cloud treatments. The selected fluid-film material
uses five translucent optical layers, seven analytic curl steps per layer,
three colour stops and pixel-sized fine detail. The canonical GLSL is shared
with the native fallback; no Studio runtime is embedded in the plug-in.

Actual offscreen Studio captures verified:

- Motion changes between 0, 4 and 8 seconds (mean RGB change 9.80 on a 0–255 scale between 0 and 8).
- Holding phase and the other inputs produces identical pixels at 0 and 8 seconds (maximum channel difference 0).
- Each of the three colour stops and visual intensity changes visible pixels.
- Outer 7.9% margins match a background-only render exactly, including 450×900 Chrome and 1920×300 Afterhours scenes at intensity 4.
- The final integer-percent inspector controls produce exactly the same pixels as the validated normalized inputs.

See `design/pearl-cloud/validation.json` for the shader hash and fixtures.
`docs/images/pearl-cloud-studio.png` is a captured Studio frame; `pearl-cloud-studio-editor.jpg` shows the live Studio editor and controls. `aurora-current.jpg` shows the newer persistent-fluid material running natively. The separate
12-second MP4 preview was encoded by Studio as 1728×1080 H.264 at 30 fps,
without audio. Synthetic Studio feature controls are design inputs, not a live
DAW audio connection or evidence of meter calibration.

## Test coverage

- DSP: silence and invalid input; calibrated FFT, RMS and sample peaks; anti-phase preservation; correlation; loudness weighting and timing at four sample rates; absolute/relative gates; mono weighting; exact peak holds; resets and format changes; concurrent analysis/snapshot access; spectrum projection and narrow-bin peak preservation.
- Processor: bit-exact pass-through in mono/stereo at 44.1, 48, 96 and 192 kHz, including irregular/empty blocks and unusual floating-point values; repeated prepare/release cycles; worker delivery and channel mapping; receiving timeout; overflow detection, measurement reset and recovery; state round-trip, bounds, malformed input and older version 1 presets.

- Framing: 8,640 configurations and 1,805,760 independently evaluated mesh vertices; varying aspect ratio, intensity, phase, correlation and waveform overload. Projected enclosing bounds remain inside a 7% margin. Pearl Cloud uses a separately bounded fragment material with an 8% edge margin; Studio pixel comparisons cover normal, portrait and very wide views at high intensity.

Source tests: `Tests/AnalysisTests.cpp`, `Tests/VisualFramingTests.cpp` and `Tests/ProcessorTests.cpp`. Local execution evidence: `build/Testing/Temporary/LastTest.log` and `/tmp/aurora-auval-studio.log`. These logs are local build artifacts, not portable release evidence.

## Measurement and validation limits

The detailed definitions are in [METERING.md](METERING.md). This prototype measures **sample peaks**, not intersample true peak, and does not report loudness range. Integrated loudness is an estimate using a fixed 0.1 LU histogram; blocks near the relative gate can be grouped together. The full ITU/EBU compliance suite has not been run, and the meter is not certified.

Levels have a finite −120 floor, also used while loudness windows are incomplete. Momentary and short-term readings require 400 ms and 3 s of supplied audio. Spectrum resolution is fixed at a 4096-point Hann FFT; the close-up Scope uses the latest 1024-sample snapshot, while the scrolling waveform keeps up to 24 seconds of sample-derived envelopes. Low/mid/high animation controls are artistic intensity values, not calibrated loudness measurements. Missing analysis samples cause a reset rather than being silently joined into continuous windows.

Remaining checks include further art-direction review, exhaustive colour-picker interaction checks, visible plugin GUI and real playback in Live, longer production sessions, reference-meter comparisons, broader DAW coverage, Intel runtime, and other GPU/driver configurations. No results for these are claimed here.
