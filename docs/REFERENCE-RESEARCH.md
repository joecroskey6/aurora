# Reference research and implementation direction

Research date: 12 October 2026. These observations describe the public material available on that date, not a certification of either vendor's native DSP.

## Project direction

Aurora Meters is an original, free, open-source **VST3 and Audio Unit plugin first**. Its purpose is to combine practical metering with expressive, audio-reactive visuals. JUCE 8 is the selected native framework, using its AGPLv3 licensing option. The bundled framework's [license notice](../third_party/JUCE/LICENSE.md) documents that option and its dependencies; the upstream [JUCE license notice](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md) is the corresponding public reference.

The requested art direction is **Gen_X_Soft_Club**. The proposed interpretation is a restrained silver and off-white instrument surface, cool blue and soft green accents, fine technical rules, compact typography and organic motion. This is our design direction, not a description of Ghost's appearance. Meter labels, scales and overload states must remain legible within it.

## Ghost: documented product versus inspected preview

[Ghost's official product page](https://adieusounds.com/plugins/ghost) advertises **33 reactive visuals and 11 metering visuals**, VST3/AU formats, macOS 11+ and Windows 10+, resizable layouts, saveable presets and a free price. It is designed to monitor the master bus without changing audio. The [public download-page bundle](https://adieusounds.com/assets/GhostDownloadPage-B0VoIica.js) describes a DAW plugin with no separate application, universal Intel/Apple Silicon Mac support and a Windows WebView2 dependency. That bundle also contains a conditional Windows availability message; it does not establish that a particular installer is currently available.

The website exposes a [readable HTML/JavaScript preview](https://adieusounds.com/assets/plugins/ghost-preview.html), embedded by its [preview component](https://adieusounds.com/assets/GhostPreview-q9Cw0-tx.js). This was inspected as reference material. The preview expressly generates synthetic signals and disables persistence. Its animation is **not a live audio capture or a test of the native plugin**.

### Counts and observable catalogue

The preview defines **29 drawing modules**. For this research, they are grouped into 18 creative modules and 11 measurement-oriented modules. This grouping is descriptive; it does not reconcile the vendor's advertised count of 33 reactive visuals.

| Inspected category | Modules defined in the public preview |
| --- | --- |
| Creative, 18 | Floor, burst, ridges, ring, scope, stereo/Lissajous trace, terrain, tunnel, mirror, helix, orbit, rain, moiré, harmonograph, lattice, strata, comet, contour |
| Measurement-oriented, 11 | Spectrum analyser, stereo level bars, numerical readouts, spectrum bars, spectrogram, goniometer, PPM ladder, VU-style needles, crest history, five-band levels, level histogram |

The numerical panel exposes peak, RMS, crest factor and stereo correlation. Level bars include held peaks and a clip counter. There are ten palettes and eleven combination layouts in the main preset menu, alongside single-module views. Preset records pair a view with a palette. Additional website-only views exist; a headroom view is referenced without a matching drawing-module definition in the inspected file. These are preview observations, not a complete installed-plugin inventory. [Preview source](https://adieusounds.com/assets/plugins/ghost-preview.html)

### Architecture visible in the preview

The renderer uses Canvas 2D, a module registry and offscreen buffers for individual panels. It targets 60 frames per second, while the website can cap its embedded previews at lower rates. A JUCE backend event supplies the display data; comments identify C++ as responsible for analysis and host-state persistence.

The displayed data contract includes per-channel peak and RMS, correlation, crest, held peaks, clipping, a mono flag, 256 waveform points per channel, optional min/max waveform envelopes and 64 spectrum values. The UI derives five frequency-band intensities and a spectral-change measure used for transient reactions. Visual examples include waveform history becoming terrain, spectrum energy expanding rings, stereo waveforms shaping a helix and transient activity emitting tunnel segments. [Preview source](https://adieusounds.com/assets/plugins/ghost-preview.html)

This exposes useful behavior and the UI-side data boundary. It does **not** expose or verify native FFT configuration, RMS integration windows, correlation windows, peak detection, calibrated VU/PPM ballistics, true-peak measurement or LUFS. Drawing a meter shape is not evidence of standards-conformant measurement.

## MiniMeters: practical workflow reference

[MiniMeters' official site](https://minimeters.app/) now describes a standalone application plus individual meter plugins and a Quad plugin layout. It lists seven modules: VU, waveform, spectrogram, spectrum, stereometer, loudness and oscilloscope. The standalone can receive stereo input, supported output capture or audio from an Audio Server plugin. Layout choices include a horizontal bar, popped-out meters and Quad.

Relevant controls include L/R and M/S views, spectrum smoothing, FFT sizes through 16384, logarithmic/linear/mel scales, frequency and note overlays, and a target curve derived from an audio file. The spectrogram supports orientation, piano overlay and scrolling or static presentation. Stereo displays include multiband correlation. Loudness offers momentary and short-term LUFS or fast/slow RMS. The oscilloscope can follow zero crossings. Audio recording and retrospective clips are additional features.

Its workflow is a strong reference for compact, configurable meters. Standalone routing and recording are later opportunities for Aurora, rather than prerequisites for its first plugin release. These findings describe the current website, not the version installed locally. [Official feature catalogue](https://minimeters.app/)

## Source boundaries

- Ghost's public web UI and preview were inspected. No native source repository or explicit open-source grant for the inspected preview was found in this research. Publicly delivered JavaScript is not being treated as licensed project source.
- MiniMeters' official product documentation was inspected. This document makes no claim that its complete application is open source, and does not infer internal implementation from its appearance.
- No vendor binaries were decompiled. No vendor code, branding, embedded images, palettes or presets are incorporated into Aurora by this research.
- Aurora's analysis, rendering, visual identity and presets should be independently implemented. Public feature descriptions guide behavior; mathematical definitions and explicit tests establish measurement correctness.

## Original architecture

1. **Transparent audio processor.** Accept host audio, analyse it and pass samples through unchanged. Support mono/stereo deliberately. Report latency and tail consistently with the actual processing behavior.
2. **Preallocated analysis pipeline.** Keep allocation, file access, UI work and blocking locks out of the audio callback. Accumulate measurement windows and deliver bounded snapshots or samples through a suitable producer/consumer buffer. Heavier spectral analysis can run away from the audio callback.
3. **One measurement model.** Define peak in dBFS, RMS window and calibration, crest calculation, correlation window, FFT window and amplitude normalization explicitly. Preserve min/max envelopes when reducing waveform data. Define silence behavior so displays remain finite and meaningful.
4. **Separate visual mapping.** Feed creative views normalized band energy, transients, waveform shape and stereo relationships. Visual gain and smoothing must not alter the calibrated numerical meters. Advance history and motion using elapsed time, independent of UI frame rate.
5. **Native modular editor.** Use reusable meter panels and original reactive scenes in a resizable JUCE editor. Persist view, palette and display options in host state. Support safe editor closure/reopening while audio continues.

These are design requirements and proposals, not claims that every item is already implemented.

## Phased parity roadmap

| Phase | Scope | Evidence needed before calling it complete |
| --- | --- | --- |
| 1 — Plugin foundation | Build VST3 and AU; transparent processing; stereo peak/RMS/crest/correlation; spectrum, waveform and stereo display; a small set of original reactive views; Gen_X_Soft_Club editor | Successful native builds, host loading and state round-trip; known-signal checks; unchanged output samples; finite silence results; editor lifecycle check |
| 2 — Meter depth | Spectrogram, peak hold/reset, configurable FFT and smoothing, L/R and M/S display, useful history controls, richer layouts and presets | Frequency/amplitude calibration across sample rates; consistent window durations across block sizes; responsive resizing and bounded history; audio-thread performance checks |
| 3 — Expressive range | More original waveform, spectral, spatial and transient-driven scenes; visual sensitivity, trails and motion controls; reduced-motion option | Repeatable audio-to-motion behavior; frame-rate-independent timing; intelligible behavior on silence, impulses, tones, noise and music; acceptable CPU at maximum layout size |
| 4 — Advanced measurement and expansion | Properly specified VU/PPM, true peak and LUFS if desired; optional reference curves; consider standalone capture, audio-server routing and other formats | Reference-signal validation for every claimed measurement standard; host/platform testing; documented limitations; packaging and release instructions |

There is no requirement to reproduce a vendor's module count. The initial milestone is a dependable native plugin with useful meters and a coherent original visual language; breadth follows validated foundations.

## Diffusion Studio authoring and native transfer

Following the user's suggestion, the official **Diffusion Studio v0.211.0**
application was installed and its local CLI opened the Pearl Cloud design.
Five iterations were rendered in Studio. The selected fifth study's contact
sheet was inspected at 0, 4 and 8 seconds: the frames show changing fluid folds,
open internal spaces and a composition contained within the surface. These are
observations of actual Studio captures, not results inferred from compilation.

A local working copy was used after Studio encountered a cloud-folder access
problem. The canonical project remains under `design/pearl-cloud` in Aurora's
source. Its JSX surface hosts the original GLSL directly through WebGL and uses
the editor's provided Solid/JSX modules; it needs no project-local npm install.
See the [official surface workflow](https://github.com/diffusionstudio/editor/blob/main/docs/reference/jsx/surface-paint.md)
and [capture tool](https://github.com/diffusionstudio/editor/blob/main/docs/reference/tools/capture.md).

The selected Pearl Cloud design composites **five optical layers**, each driven
by **seven domain-warp steps**. Its analytic gradient determines shading; thin
fibres, optical ridges and the selected three colours create a translucent fluid
film. The function accepts phase, audio features, intensity, aspect, theme and
viewport pixel dimensions. It returns RGB composited over the caller's
background. An explicit support rectangle and soft mask bound every contribution,
including highlights and haze, with at least 8% margins. There is no independent
wall clock, and fixed arguments produce a fixed image. This code inspection does
not replace a native frame-rate or pixel-level validation run.

`scripts/sync-pearl-cloud.py` generates the Studio import from the canonical GLSL;
its `--native` option embeds that same source in the JUCE renderer. Transfer is a
shader build step, not an embedded Studio runtime or a native Studio plugin
bridge. The revised shader was subsequently compiled and visibly verified in the native
developer preview; the AU validator also passed. Real Ableton playback and
GPU performance profiling remain separate checks. See [validation](VALIDATION.md).

The source package includes only the small editable design and optional capture
metadata. Studio's application, downloads, local work copy, runtime caches,
earlier candidates and working captures are excluded. Contact-sheet time labels
are capture annotations; they do not appear in the native artwork. See the
[design instructions](../design/pearl-cloud/README.md) for the portable workflow.
