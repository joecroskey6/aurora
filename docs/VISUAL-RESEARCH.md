# Visual research and native rendering decisions

Research recorded 12 October 2026. Aurora is an original JUCE VST3/AU project with
a Gen_X_Soft_Club visual direction. The references below inform motion, readable
history and rendering structure. They do not establish feature parity or native
measurement accuracy. See [REFERENCE-RESEARCH.md](REFERENCE-RESEARCH.md) for Ghost
and the initial product comparison, and [VALIDATION.md](VALIDATION.md) for actual
Aurora test results.

## What was observed

| Reference | Evidence inspected in this development session | Limits |
| --- | --- | --- |
| MiniMeters | The official [meter-strip video](https://minimeters.app/video/preview_vid_new.mp4), approximately 13.66 seconds, was played in a browser and inspected at multiple frames. The approximately 4.2-second [stereometer video](https://minimeters.app/video/stereometer.mp4) was observed near its end and beginning. | These demonstrate moving displays. They are not measurements against known signals, a source audit of the proprietary product, or a test of the user's installed version. |
| Pulse | The maintainer's approximately 3.9-second [embedded demonstration](https://github.com/user-attachments/assets/8a65f8bc-7dfd-477d-88d7-20f65e7e54ad) was watched multiple times. Its phosphor decay, blur and final color shaders were inspected alongside the [official README](https://github.com/Audio-Solutions/pulse-visualizer). | This is a targeted shader inspection, not an audit of all its analysis algorithms or runtime behavior. |
| Pavel Dobryakov's fluid simulation | The [official interactive demo](https://paveldogreat.github.io/WebGL-Fluid-Simulation/) was opened and interacted with; initial splats were visibly observed evolving. The public solver and render code were also inspected. | The observed demo is pointer-driven. It does not demonstrate Aurora's audio mappings, host integration or performance. |
| Butterchurn | The [official live page](https://butterchurnviz.com/) was opened. Its renderer, audio analysis and shader source were inspected. | Playback did not start because no audio was loaded. No moving Butterchurn preset was verified in this session. |
| projectM | Public native renderer, audio and shader source were inspected. Sampled passages of the [repository-linked demonstration](https://www.youtube.com/watch?v=jJmLQGhYWys) were watched: near 14 seconds, thin amber recursive oscillator rings; near 1:07, dense recursive color-block patterns and transitions. | The approximately 4:03 video was not watched in full. These are selected examples, not Aurora's intended style. The [projectMSDL native preview](https://github.com/projectM-visualizer/frontend-sdl-cpp/releases) was identified but not exercised. |
| Prism and OpenMeters | Selected renderer and spectrogram processing source was inspected, with pinned links below. | Neither application was run. Source behavior must not be described as an observed live demo. |

MiniMeters is a useful reference for simultaneous, compact views of one audio
stream. Its [v1 announcement](https://directmusic.itch.io/minimeters/devlog/999502/minimeters-v10-a-new-minimeters)
documents synchronized waveform/spectrogram speed, recolouring of existing
spectrogram history, zero-crossing tracking and time/frequency sharpening.
These are specific product behaviors to evaluate later; none should be inferred
from a similar-looking Aurora panel.

Pulse's [README](https://github.com/Audio-Solutions/pulse-visualizer#features)
documents phosphor persistence, configurable glow/light spread, spline Lissajous,
pitch-following scope and iterative spectrogram reassignment. Its presentation
suggests a useful separation between the newest trace and fading history. The
phosphor implementation was checked below; the other analysis features remain
documented claims in this research record.

## Source findings

The projectM and Butterchurn links below pin the actual revisions inspected:
projectM `349b7cc2db312d47cef6be24c6856ced6643a385`, Butterchurn
`fbac2f6bab62fd9c6a50ebbeb29359c5eb05903e`. Pavel's inspected revision is
`a2d292931f19d9b3b9f564e23e6c32729d2121c3`.

### Pulse: separate trace energy from its display treatment

At inspected revision `50466f1f64765bb463a17a5ce4bcfa3388293262`,
[phosphor_decay.comp](https://github.com/Audio-Solutions/pulse-visualizer/blob/50466f1f64765bb463a17a5ce4bcfa3388293262/shaders/phosphor_decay.comp#L44)
stores persistent RGB energy and blends fast/slow decay according to intensity.
[phosphor_blur.comp](https://github.com/Audio-Solutions/pulse-visualizer/blob/50466f1f64765bb463a17a5ce4bcfa3388293262/shaders/phosphor_blur.comp#L44)
uses separable Gaussian accumulation and adds light spread during its vertical
pass. [phosphor_colormap.comp](https://github.com/Audio-Solutions/pulse-visualizer/blob/50466f1f64765bb463a17a5ce4bcfa3388293262/shaders/phosphor_colormap.comp#L234)
applies energy grain, tanh compression, optional display effects and small
anti-banding dither. The transferable idea is separating signal/history from
finishing. Aurora does not import these shaders or reproduce the CRT treatment.

### Numerical history and efficient drawing

Prism's [Spectrogram.ts](https://github.com/Boof2015/prism/blob/49721802252b115d169e353c98b27408daa5e2b7/src/renderer/visualizers/Spectrogram.ts#L408)
scrolls a retained waterfall and writes narrow new strips rather than rebuilding
the full history. It uses a 256-entry color table.
[Vectorscope.ts](https://github.com/Boof2015/prism/blob/49721802252b115d169e353c98b27408daa5e2b7/src/renderer/visualizers/Vectorscope.ts#L263)
fades a retained canvas and preserves separate dot blending, while
[SpectrumAnalyzer.ts](https://github.com/Boof2015/prism/blob/49721802252b115d169e353c98b27408daa5e2b7/src/renderer/visualizers/SpectrumAnalyzer.ts#L1343)
caches static guides. These suggest inexpensive history updates and density-aware
stereo traces, not a need to redraw static decoration every frame.

OpenMeters' [spectrogram processor](https://github.com/httpsworldview/openmeters/blob/3345c89f986c52aa2e2787a01c21919c5110dd64/src/visuals/spectrogram/processor.rs#L45)
stores classic history as packed dB values so it can be recoloured later. Its
[reassignment calculation](https://github.com/httpsworldview/openmeters/blob/3345c89f986c52aa2e2787a01c21919c5110dd64/src/visuals/spectrogram/processor.rs#L324)
uses normal, derivative and time-weighted windows.
The [WGSL renderer](https://github.com/httpsworldview/openmeters/blob/3345c89f986c52aa2e2787a01c21919c5110dd64/src/visuals/render/shaders/spectrogram.wgsl#L156)
places reassigned points using frequency and fractional time offsets. This
demonstrates why true time/frequency sharpening is analysis work, not merely
image sharpening. Numerical history and late palette resolution are useful
principles; Aurora does not incorporate these implementations.

### projectM: image feedback with separate finishing

[MilkdropPreset::RenderFrame](https://github.com/projectM-visualizer/projectm/blob/349b7cc2db312d47cef6be24c6856ced6643a385/src/libprojectM/MilkdropPreset/MilkdropPreset.cpp#L78)
evaluates per-frame expressions, warps the prior image, updates blur textures,
draws fresh shapes/waveforms and renders a final composite. Buffer swapping keeps
the image before final compositing as the next feedback source. The current
implementation also handles vertical texture flips and a motion-vector UV map;
those are compatibility details, not necessary Aurora design requirements.

[PerPixelMesh](https://github.com/projectM-visualizer/projectm/blob/349b7cc2db312d47cef6be24c6856ced6643a385/src/libprojectM/MilkdropPreset/PerPixelMesh.cpp#L283)
combines zoom, rotation, distortion and decay when sampling history. The important
mechanism is transporting previous detail over time. A complicated single-frame
formula alone does not provide that memory.

[BlurTexture](https://github.com/projectM-visualizer/projectm/blob/349b7cc2db312d47cef6be24c6856ced6643a385/src/libprojectM/MilkdropPreset/BlurTexture.cpp#L106)
alternates horizontal and vertical passes at reduced sizes. Three exposed blur
levels are approximately one quarter, one eighth and one sixteenth of the source
dimensions. Allocation and execution depend on requested blur use.
[FinalComposite](https://github.com/projectM-visualizer/projectm/blob/349b7cc2db312d47cef6be24c6856ced6643a385/src/libprojectM/MilkdropPreset/FinalComposite.cpp#L97)
applies a custom shader or legacy echo/filter treatment separately.

### Butterchurn: a clear browser implementation of the same family

[Renderer](https://github.com/jberg/butterchurn/blob/fbac2f6bab62fd9c6a50ebbeb29359c5eb05903e/src/rendering/renderer.js#L825)
swaps two history textures, evaluates a warp mesh, warps history, creates requested
blur levels, draws new audio geometry and composites the output. Its default
48-by-36 warp mesh is independent of image resolution. Preset changes blend
equation results and render treatments rather than merely switching a color.

The default [warp shader](https://github.com/jberg/butterchurn/blob/fbac2f6bab62fd9c6a50ebbeb29359c5eb05903e/src/rendering/shaders/warp.js#L153)
samples the previous image and applies decay. The
[composite shader](https://github.com/jberg/butterchurn/blob/fbac2f6bab62fd9c6a50ebbeb29359c5eb05903e/src/rendering/shaders/comp.js#L153)
can independently sample sharp history, blur levels and noise, or apply echo and
color effects. An optional [output pass](https://github.com/jberg/butterchurn/blob/fbac2f6bab62fd9c6a50ebbeb29359c5eb05903e/src/rendering/shaders/output.js#L23)
provides FXAA. These are useful pipeline references; Aurora does not import the
JavaScript engine, its preset equation interpreter or its preset collection.

### Audio controls are not meter readings

Butterchurn's [AudioLevels](https://github.com/jberg/butterchurn/blob/fbac2f6bab62fd9c6a50ebbeb29359c5eb05903e/src/audio/audioLevels.js)
uses approximate 20–320, 320–2800 and 2800–11025 Hz bands. It exposes immediate
and smoothed band energy relative to a slower running average, with smoothing
adjusted for frame rate. Its [AudioProcessor](https://github.com/jberg/butterchurn/blob/fbac2f6bab62fd9c6a50ebbeb29359c5eb05903e/src/audio/audioProcessor.js)
also supplies separate stereo waveforms and spectra.

projectM's [PCM](https://github.com/projectM-visualizer/projectm/blob/349b7cc2db312d47cef6be24c6856ced6643a385/src/libprojectM/Audio/PCM.cpp#L52)
averages the two channel spectra for band activity while retaining separate
waveforms. Its [Loudness](https://github.com/projectM-visualizer/projectm/blob/349b7cc2db312d47cef6be24c6856ced6643a385/src/libprojectM/Audio/Loudness.cpp)
class divides band activity by a long-running average and applies elapsed-time
smoothing. Despite the class name, these are creative relative controls, not
LUFS. Both implementations use a neutral relative value near silence; Aurora
must retain an absolute silence gate before such a value can drive motion.

### Fluid simulation: transport a field, then light it

Pavel's [advection and pressure shaders](https://github.com/PavelDoGreat/WebGL-Fluid-Simulation/blob/a2d292931f19d9b3b9f564e23e6c32729d2121c3/script.js#L746)
transport velocity/dye, estimate divergence, iteratively solve pressure and
subtract its gradient. Curl confinement preserves visible rotational activity.
[Double framebuffers](https://github.com/PavelDoGreat/WebGL-Fluid-Simulation/blob/a2d292931f19d9b3b9f564e23e6c32729d2121c3/script.js#L1079)
provide distinct read and write textures; the
[step function](https://github.com/PavelDoGreat/WebGL-Fluid-Simulation/blob/a2d292931f19d9b3b9f564e23e6c32729d2121c3/script.js#L1231)
orders the passes. Its [render/bloom functions](https://github.com/PavelDoGreat/WebGL-Fluid-Simulation/blob/a2d292931f19d9b3b9f564e23e6c32729d2121c3/script.js#L1296)
finish the resulting dye image separately. This source is a direct technical
reference for the new native Pearl Flow solver.

## Decisions implemented in Aurora's current source

These descriptions record source structure. They are not a claim that every
new combination has completed native visual or DAW testing.

| Component | Current implementation and reason |
| --- | --- |
| [PearlFlow.h](../Source/PearlFlow.h) | Persistent half-float velocity, pressure and pigment fields. Velocity transport, curl confinement, divergence, 12 pressure iterations and projection precede pigment transport. Limited MacCormack correction reduces dye diffusion without allowing new extrema. This gives Pearl Cloud material an evolving history. |
| Audio forcing | Low-band energy and positive rises drive broad vortices; mid activity adds shear and affects curl; high activity adds finer forcing. Correlation changes directional motion. These are artistic mappings of copied analysis features, separate from calibrated meter values. |
| Palette representation | Three pigment concentrations are stored independently of display colors. Changing any of the user's three colors recolours the existing material immediately rather than waiting for old RGB dye to disappear. |
| [PearlFlowMaterial.h](../Source/PearlFlowMaterial.h) | Three curved optical strata now occupy depth and are ray-integrated for an orbiting view, sampling the persistent field with offsets/refraction. Density gradients provide normals, transmission and edge light. User colors determine the material. This replaces the analytic study as the preferred native Pearl Cloud path when flow targets are available. |
| [VisualSurface.cpp](../Source/VisualSurface.cpp) | The solver runs on the OpenGL thread before rendering. Its canvas aspect is fixed independently of the panel, so resizing does not stretch or reseed the flow. Copied audio features cross into rendering; GPU work stays out of the audio callback. |
| [VisualPostProcess.h](../Source/VisualPostProcess.h) | All scenes render into an RGBA16F target with depth24, capped at a 1440-pixel long axis. Quarter-size highlight extraction and horizontal/vertical Gaussian blur add restrained bloom. Display colors are explicitly decoded for linear-light blur/composition, then encoded again. Theme amounts are 0.12, 0.28 and 0.10. This pass is original code. |
| Lifecycle | The solver seeds on initialization/reset, then uses fixed 1/60-second steps with at most two steps per update. Freeze and silence stop evolution after initialization and discard accumulated catch-up time. Context teardown releases GPU resources. Incomplete flow targets select the analytic fallback; failed postprocess allocation selects direct scene rendering. |

Bloom is not fed back into velocity or pigment. This avoids repeatedly amplifying
the same highlight and lets theme finishing change without changing fluid state.
The other five scenes retain their own geometry and audio mappings. The orbit
revision now samples the shared persistent pigment field in all five materials.

The earlier Diffusion Studio design remains the editable analytic study and
fallback in `design/pearl-cloud`. Its five optical layers and seven warp steps
are distinct from the new stateful native solver. Studio captures of that study
do not prove the new solver's behavior. A stateful flow also needs its starting
state and complete input/step sequence to reproduce a frame; matching only phase
and current energy is insufficient.

The material's support mask leaves an 8% margin before postprocessing. Bloom can
spread light beyond that support. The analytic study's exact background-only
border checks must not be reused as proof of the final native composed image.

## License and reuse boundaries

| Source | Verified license / handling |
| --- | --- |
| projectM core | [LGPL-2.1-or-later](https://github.com/projectM-visualizer/projectm/blob/349b7cc2db312d47cef6be24c6856ced6643a385/COPYING). Architecture was studied; its renderer was not imported. Reusing implementation would require preserving its license/attribution and satisfying applicable source/linking terms. |
| Butterchurn core | [MIT](https://github.com/jberg/butterchurn/blob/fbac2f6bab62fd9c6a50ebbeb29359c5eb05903e/LICENSE). Studied, not imported. Substantial reuse requires the notice. Its [preset repository](https://github.com/jberg/butterchurn-presets/blob/master/LICENSE) separately declares MIT; that is not a blanket license for unrelated MilkDrop collections. |
| Pavel's fluid simulation | [MIT, copyright 2017 Pavel Dobryakov](https://github.com/PavelDoGreat/WebGL-Fluid-Simulation/blob/a2d292931f19d9b3b9f564e23e6c32729d2121c3/LICENSE). Pearl Flow identifies the solver reference and retains the complete notice. Aurora's audio forcing, ribbon sources, lifecycle and optical material are its own implementation. |
| Pulse | The [repository license](https://github.com/Audio-Solutions/pulse-visualizer/blob/50466f1f64765bb463a17a5ce4bcfa3388293262/LICENSE) is GPLv3; the inspected shader headers specify v3 or later. Demo, documentation and selected shaders were studied; no Pulse implementation was incorporated. |
| Prism / OpenMeters | [Prism](https://github.com/Boof2015/prism/blob/49721802252b115d169e353c98b27408daa5e2b7/LICENSE) and [OpenMeters](https://github.com/httpsworldview/openmeters/blob/3345c89f986c52aa2e2787a01c21919c5110dd64/LICENSE) carry GPLv3 repository licenses; inspected OpenMeters shader headers specify GPL-3.0-or-later. Selected source was studied; no implementation from either was incorporated. |
| projectM Cream of the Crop presets | The [pack's own notice](https://github.com/projectM-visualizer/presets-cream-of-the-crop/blob/master/LICENSE.md) says most presets lacked explicit licenses and assumes public-domain status. This is not verified author permission. Aurora does not redistribute that pack. |
| Ghost and MiniMeters | Functional and visual references only. No proprietary native code, graphics, branding or presets were incorporated. Public website/demo access is not a license to redistribute a vendor's implementation. |

Aurora's AGPLv3/JUCE licensing remains separate from the notices of its reference
and dependency code. A public source URL alone is not a substitute for a license.

## Scope still to validate or develop

The new bloom header passed C++ syntax checking, actual macOS OpenGL shader
compilation/linking and RGBA16F/depth24 framebuffer completeness checks. Those
checks do not assess the appearance of all scenes, GPU cost or host behavior.
The evolving [validation record](VALIDATION.md) is the place for completed native
and plugin checks.

Next evidence should cover fixed-seed input sequences, exact freeze behavior,
reset and context recreation, palette edits during persistence, finite silence
and overload behavior, small/portrait/wide panels, and native frame time during
real DAW playback. The finite-step budget intentionally drops excess elapsed
time under load; identical wall-clock duration alone does not guarantee identical
fluid state.

MiniMeters-style synchronized history controls, persistence settings on every
meter, advanced pitch tracking, reassigned spectrograms, projectM/MilkDrop preset
compatibility and arbitrary visual scripting remain outside the current scope.
There is no full Ghost, MiniMeters, Pulse or MilkDrop parity claim.

## Cowboy Bebop graphic reference — 13 October 2026

The [official Cowboy Bebop homepage](https://www.cowboy-bebop.net/) and
[official episode 6–10 page](https://www.cowboy-bebop.net/story/02.html) were
opened and visually inspected in the browser. The homepage uses black gutters,
cropped rectangular image fields, oversized condensed headings, repeated text
textures and controlled red, navy and mustard accents. The story page adds a
wide red heading strip and thin rules. These are observations of the current
official website presentation, not claims that its web controls appeared in the
original animation.

The publisher's [episode-nine image strip](https://www.cowboy-bebop.net/story/img/09.png)
was opened directly and inspected. Its first small still shows a dense blue
computer-space composition with bright outlined elements; the right still shows
reflected graphical patterns in Ed's goggles. The supplied image is only
705 by 213 pixels including its title and four stills. It supports a reference
to layered graphical information and sharply separated color fields, but is
too small to infer the controls, typography or operation of a complete fictional
computer interface. No episode or opening sequence was watched during this pass.

Sunrise's [official design-book announcement](https://www.sunrise-world.net/news/news.php?id=20932)
explicitly says the book includes monitor-screen settings. That confirms a
primary design source exists; the complete book and its monitor plates were not
accessed. Its credit to graphic designer Toshiaki Uesugi for the book box and
series eye-catch work is documented there. No fan mockup is treated as a
production interface reference.

Two in-show technology stills were also inspected directly in a
[community-hosted frame collection](https://www.reddit.com/r/cassettefuturism/comments/177crsk/the_interfaces_of_cowboy_bebop_1998/).
The first shows a small green monochrome sector display: two simple arcs and
radial divisions on a dark face, framed by substantial physical controls. The
second shows a handheld diagnostic instrument with a pale mint display, one
vertical color bar and a central magenta signal shape; its small physical keys
are separated from the graphic readout. These are observations of the displayed
stills, not a verified episode attribution or an official distribution source.
The useful UI lesson is disciplined separation of the signal from the controls,
with sparse reference marks and a single active accent. Aurora can use that
hierarchy for its meters without reproducing either screen or adding decorative
gauge marks that imply measurements it does not make.

For Aurora, the useful translation is an original instrument layout: clear
rectangular divisions, compact labels beside large readings, thin functional
rules, deliberate empty space and a small number of strong accent fields.
Cream/charcoal with muted red, blue and mustard is an optional palette direction,
not a sampled or mandatory replacement for the user's custom colors. Existing
artwork, characters, logos, screenshots and proprietary lettering are not
embedded or redistributed.

The new [SceneMaterial.h](../Source/SceneMaterial.h) develops Aurora's own optical
language for the five geometric scenes. It samples palette-independent pigment
history beneath mode-specific engraving: flowing laminae for Folded Light,
concentric/radial detail for Liquid Aperture, facade reflections for Night
Transit, two crossing optical weaves for Diffraction Veil, and helical winding
for Orbit Loom. Surface-gradient relief, internal color strata and bounded
reflections provide depth without modifying the mesh. Fine patterns fade when
they become smaller than a pixel; closed surfaces sample periodic coordinates
to avoid a seam. Its supplied phase and history determine motion, and themes
change lighting while retaining arbitrary user colors. This is source design;
native compilation and visual review of this particular material revision are
separate checks.


## Palette and console design pass — 13 October 2026

For this pass, the official MiniMeters [quad screenshot](https://minimeters.app/images/press/quad_logic.png),
[spectrogram screenshot](https://minimeters.app/images/meters/spectrogram.png) and
[waveform screenshot](https://minimeters.app/images/meters/waveform.png) were opened
and visually inspected directly in a browser. The quad view leaves most inactive
space nearly black, gives the spectrum a thin pale crest, and subordinates grid
lines to signals. The spectrogram example separates deep purple, magenta and
warm orange energy; the waveform uses stronger, sometimes very pale colour only
inside the trace. The transferable principle is that colour belongs to the data,
with restrained neutral framing. These are selected vendor stills, not a live
analysis test or a source inspection of MiniMeters. The user's installed copy
and DAW were not operated in this pass.

MiniMeters' [official product page](https://minimeters.app/) documents independent
waveform colour modes and colour maps, while its [v1 announcement](https://minimeters.app/changelog/1.0.0/)
identifies colour-map support across spectrum, spectrogram and waveform. This
supports preserving Aurora's independent appearance choices. It does not imply
Aurora implements the same mapping or analysis.

Pulse's [maintainer README](https://github.com/Audio-Solutions/pulse-visualizer#theming)
was also read again: it describes editable themes, immediate theme reload and
separate configurable light spread and phosphor persistence. The earlier video
and shader observations above remain the evidence for its appearance. No new
Pulse playback was performed in this pass. For Aurora, the relevant design
inference is to keep the bright trace, its faint history, and the panel surface
visually distinct; global glow should not flatten these layers.

### Original Flight Deck palette proposals

The new console direction combines the restrained instrument hierarchy in the
Cowboy Bebop references above with MiniMeters' use of bright colour against quiet
space. It is an original design direction, not a reproduction of a fictional
screen or vendor preset. Warm green-charcoal is the chassis; warm off-white and
muted grey-green carry text. The selected three-stop palette belongs to the
visualization and meter traces. Arbitrary user RGB colours remain available.

| Palette | Low-energy colour | Main signal colour | Highlight colour |
| --- | --- | --- | --- |
| Phosphor | `#123F35` | `#64DFAC` | `#E8F1A4` |
| Amberline | `#592F35` | `#ECA75C` | `#FFEAC2` |
| Ion Blue | `#263A73` | `#63BCE0` | `#E8ECD9` |
| Redshift | `#482346` | `#DE6974` | `#F7DCAD` |
| Petrol | `#0D424D` | `#55C5BA` | `#D8EAA3` |
| Tape Violet | `#373058` | `#A68CDD` | `#F0C6B0` |

These hex triplets were chosen for Aurora, not sampled from the references. Each
has increasing sRGB relative luminance from first to last. Against reference
charcoal `#171B1A`, the opaque middle colours have calculated contrast ratios of
5.28:1–10.51:1 and the highlights 11.10:1–14.73:1. The first colours are deliberately
close to the dark field (1.33:1–1.60:1); use them for low-energy density/fill, never
as the sole colour for small labels or a fine active trace. These calculations
are design checks, not an accessibility certification of composited/transparent
frames or user-selected colours.

For the console, compact technical type, hard rectangular divisions, a narrow
active colour rail and functional tick marks make a stronger change than adding
more decorative text. An amber or red status colour should retain its meaning
when a visualization palette changes. Use the pale signal crest to preserve fine
detail, and retain an unambiguous numeric reading independently of its palette.

The interface gallery was inspected again for this revision: the handheld radar
uses a narrow mint trace on a near-black display inside a grey mechanical body;
the bounty terminal uses separate dark diagnostic panes, hard bevels, compact
readouts and pale frame accents. Aurora borrows that division between instrument
structure and luminous information. It adds no fictional status copy, episode
labels or reproduced art. The resulting Flight Deck panel geometry and palette
triplets are original.


## Scrolling waveform reference — 13 October 2026

MiniMeters distinguishes a historical waveform from its immediate oscilloscope.
Its [product page](https://minimeters.app/) lists two-channel combinations,
Static/Multi-Band/Color Map choices, adjustable speed and a scrolling/loop choice.
The [v1 announcement](https://minimeters.app/changelog/1.0.0/) explains that Color
Map assigns a gradient by the volume of each waveform block, and that waveform
speed can link to the spectrogram. A volume gradient is therefore not evidence
of frequency-derived colour. Its [v0.7 notes](https://minimeters.app/changelog/0.7.0/)
define the separate history overlay's red/green/blue as low/mid/high RMS, with
1024- or 16384-sample windows at that release. Exact current waveform crossover
frequencies and colour blending were not specified in the primary pages read.
No fresh live MiniMeters session was operated during this waveform pass.

The recommended Aurora translation is a continuous 12-second two-lane L/R history
with 6/12/24-second choices, while retaining the brief triggered display as an
explicit Scope option. Worker-generated bins should retain sample time, channel
min/max and sum-of-squares/count. Combining them preserves extrema and computes
RMS from weighted energy. A restrained inner RMS shape and a fine outer min/max
envelope can expose density without smoothing away peaks. Both channels need the
same vertical gain and time axis. A 60 Hz display should transport history by
fractional sample time rather than stepping a whole pixel per analysis snapshot;
it must stop extrapolating when audio stalls and leave actual data gaps visible.
These are Aurora design recommendations, not claims about MiniMeters internals
or completed native validation.


## Waveform source and detail study — 13 October 2026

This focused review covers three established open-source implementations, not
every GitHub visualizer. Public source was read at the revisions linked below.
Their assets and implementation were not copied into Aurora. Official Mixxx,
Audacity and Sonic Visualiser waveform screenshots were also opened and visually
inspected in a browser; these were stills, not fresh application playback tests.

### Mixxx: spectral colour and physical-pixel rendering

At revision `0fffd3d0d07b755c34c1730f7b87afadd8f2be19`,
[`src/waveform/renderers/allshader/waveformrendererrgb.cpp`](https://github.com/mixxxdj/mixxx/blob/0fffd3d0d07b755c34c1730f7b87afadd8f2be19/src/waveform/renderers/allshader/waveformrendererrgb.cpp#L71)
sizes columns using the device pixel ratio, finds band/full-band maxima within
each column and batches thin rectangles as triangles. It mixes the three band
colours and normalizes by the largest RGB component, keeping the result vivid.
Its DJ EQ gains also affect appearance; those display gains are inappropriate
for Aurora's calibrated amplitude envelope.

The same tree contains a texture-driven alternative:
[`waveformrenderertextured.cpp`](https://github.com/mixxxdj/mixxx/blob/0fffd3d0d07b755c34c1730f7b87afadd8f2be19/src/waveform/renderers/allshader/waveformrenderertextured.cpp#L207)
uses a four-times-per-axis framebuffer, nearest sampling of waveform data and
linear downsampling of the rendered result. Texture uploads follow newly
completed analysis. This is an anti-aliasing technique with a substantial pixel
cost, not extra measured resolution. The inspected
[`rgbsignal.frag`](https://github.com/mixxxdj/mixxx/blob/0fffd3d0d07b755c34c1730f7b87afadd8f2be19/res/shaders/rgbsignal.frag)
separates shape coverage from band-colour mixing;
[`stackedsignal.frag`](https://github.com/mixxxdj/mixxx/blob/0fffd3d0d07b755c34c1730f7b87afadd8f2be19/res/shaders/stackedsignal.frag)
composites band regions with a shared spectral tint. Its stacked band geometry
should not be interpreted as a calibrated full-band sample peak. This review
does not establish which alternative is selected by every current configuration.

The official [RGB L/R still](https://mixxx.org/images/news/glsl-rgb-lr-waveform.png)
shows dense coral/yellow bodies, narrow mint attack edges and violet tails against
near-black. Richness comes from time-varying colour and fine silhouette detail,
not from a blurred outer glow. The [maintainer's rendering article](https://mixxx.org/news/2024-02-23-improved-waveforms/)
also describes batching the surrounding layers, 60 fps and refresh synchronization
to reduce jitter. For Aurora, consistent frame pacing should take priority over
more full-panel render passes.

### Sonic Visualiser: separate measurement layers

At revision `d5dccf33e3431bbe48731cd168cf2fb25c8ace32`,
[`layer/WaveformLayer.cpp`](https://github.com/sonic-visualiser/svgui/blob/d5dccf33e3431bbe48731cd168cf2fb25c8ace32/layer/WaveformLayer.cpp#L1009)
draws the range, an inner absolute-mean statistic and clipping as separate paths.
The inner statistic is **absolute mean, not RMS**. It is inset inside the range
and omitted when there is insufficient vertical space. At sample-level zoom,
individual sample markers become useful; optional cached painting avoids
rebuilding an unchanged view. These are useful hierarchy and level-of-detail
ideas, not a reason to replace Aurora's energy-weighted RMS.

The [official layered screenshot](https://www.sonicvisualiser.org/images/sv-1.0-wide.png)
and its [description](https://www.sonicvisualiser.org/screenshots.html) show a fine
waveform, spectral-centroid shading and a separate onset-likelihood curve aligned
to the same time axis. The transferable idea is that extra visible detail should
represent an additional measurement. Aurora should keep those distinctions clear
instead of adding curves that resemble measurements without data behind them.

### Audacity: multiresolution summaries and an RMS core

At revision `a5b307c942886e6d2bdc61375d42ceb99704dfc6`,
[`WaveDataCache.cpp`](https://github.com/audacity/audacity/blob/a5b307c942886e6d2bdc61375d42ceb99704dfc6/au3/libraries/au3-wave-track-paint/waveform/WaveDataCache.cpp#L287)
selects raw samples, 256-sample summaries or 65,536-sample summaries according to
samples per display column. RMS comes from accumulated squared energy and count.
Its adjacent-column continuity adjustment extends some displayed ranges; Aurora
should not adopt that adjustment under its exact time-bin measurement contract.
The separate
[`WaveBitmapCache.cpp`](https://github.com/audacity/audacity/blob/a5b307c942886e6d2bdc61375d42ceb99704dfc6/au3/libraries/au3-wave-track-paint/waveform/WaveBitmapCache.cpp#L224)
handles colours and clips the RMS core to the actual signed range. Separating
numeric summaries from colour rendering permits recolouring without reanalysis.

The [official annotated still](https://manual.audacityteam.org/m/images/0/00/waveannotated.png)
makes the darker peak body and lighter sustained RMS core immediately legible.
The [manual](https://manual.audacityteam.org/man/audacity_waveform.html) describes
two signed stereo lanes and replacing the aggregate RMS display with individual
sample detail at sufficient zoom. This supports Aurora's common L/R scale and
an inspection view that gains detail by narrowing the actual time window.

### Related interaction and rendering references

The [wavesurfer.js maintainer README](https://github.com/katspaugh/wavesurfer.js)
documents separate-channel rendering, timeline/hover/minimap tools and predecoded
peaks for large files. The useful interaction principle is to make time, channel
identity and inspection unambiguous; this does not imply embedding a web audio
player into Aurora. The [BBC Peaks.js API](https://github.com/bbc/peaks.js/blob/master/doc/API.md)
separates horizontal zoom from amplitude scaling and colour, supports gradients,
and caches waveforms at different zoom levels. Its repository notes that ongoing
development has moved to [Codeberg](https://codeberg.org/chrisn/peaks.js).
[BBC audiowaveform](https://github.com/bbc/audiowaveform/blob/master/doc/audiowaveform.1)
describes reducing audio to min/max groups; such peak summaries alone cannot
recover an amplitude distribution or an RMS interior.

[Signalizer's maintainer page](https://www.jthorborg.com/signalizer.html) documents
spectral waveform colouring, scalable separate-channel scopes, multisampling and
display synchronization. Its release notes describe reducing draw calls through
batching. Its [public source repository](https://github.com/jthorborg/signalizer)
is a reference for a native real-time visualization product, not a dependency.
These additional notes are based on primary documentation, not an exhaustive
source audit or fresh playback of their demonstrations.

### Implemented Aurora waveform material

The original renderer in [WaveformRibbon.h](../Source/WaveformRibbon.h) now combines
a coloured signed min/max body, measured RMS and filtered-band contours, a sharp
crest and a 65-bin PCM amplitude-occupancy texture. Each worker bucket stores
integer counts at signed amplitude centres from −1 to +1, spaced by 1/32. Counts
are summed on the same fixed sample grid as extrema and energy. This supplies
real interior information that min/max/RMS alone cannot recover. Samples above
full scale enter the histogram's end bins, while peak and energy measurements
remain unclipped.

**Silk** emphasizes occupancy, **Prism** emphasizes filtered-band RMS contours,
and **Trace** emphasizes fine RMS/band lines. The contours represent symmetric
RMS magnitudes rather than separate reconstructed waveforms. Three-bin smoothing,
per-column normalization and square-root contrast turn occupancy into pigment;
gradients and highlights are visual styling, not additional measurements or a
calibrated density scale. A circular pixel cache reuses stable columns during
fractional scrolling and redraws changing tails. Appearance changes invalidate
the cache. The signed envelope bounds the material, and real gaps remain gaps.

The history controls offer 1/3/6/12/24 seconds and fixed shared L/R amplitude
magnification of 1x/2x/4x (±1/±0.5/±0.25). Raw ±1 sample events retain separate
edge flags at their measured times; magnified cropping alone does not trigger
them. **Bands** uses apricot, mint and violet. **Palette** retains the user's
three RGB stops as its colour sources and applies material shading, without
forcing the fixed band colours or rewriting the stored stops. **Scope** remains
the independent triggered close-up.

This paragraph records the source implementation, not a test result. Native
performance, appearance and accuracy checks for this revision belong in the
separate validation record. The evaluation should include musical fixtures,
isolated impulses, silence, signed/DC input, custom palettes and steady-frame
timing at both ordinary and high display pixel density.
