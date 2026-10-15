from pathlib import Path
from html import escape
import re
from reportlab.lib import colors
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, KeepTogether

import argparse
parser=argparse.ArgumentParser(description='Generate Aurora technical-stack HTML and PDF; requires reportlab.')
parser.add_argument('--output', type=Path, default=Path.cwd() / 'docs')
OUT=parser.parse_args().output.resolve()
OUT.mkdir(parents=True, exist_ok=True)
pages=[]
def page(title, subtitle=''):
    p={'title':title,'subtitle':subtitle,'blocks':[]};pages.append(p);return p['blocks']
def h(b,t):b.append(('h',t))
def p(b,t):b.append(('p',t))
def table(b,heads,rows,widths=None):b.append(('table',heads,rows,widths))
def code(b,t):b.append(('code',t))

b=page('Aurora · Technical stack','Version 0.1.0 · Release handoff · 14 October 2026')
p(b,'Aurora is a native, free, open-source audio analysis and reactive visual plug-in. It combines sample-derived meters with six GPU scenes, 24 complete appearance presets and detailed display controls. This document describes the code and binaries in this release, the tools used to produce them, and the limits of the measurements and validation.')
table(b,['Release','Included implementation'],[
('Formats','macOS Audio Unit effect (.component) and VST3 effect (.vst3).'),
('Architectures','Universal binary: Apple Silicon arm64 and Intel x86_64.'),
('Minimum OS in shipped binaries','macOS 13.3. Both architectures in both plug-ins declare 13.3 in their Mach-O build metadata.'),
('Native framework','JUCE 8.0.6, pinned to commit 51a8a6d7aeae7326956d747737ccf1575e61e209.'),
('Languages','C++17 for plug-in, analysis and native UI; GLSL for GPU shaders. Objective-C++ and platform code are supplied through JUCE.'),
('Default appearance','Pearl Cloud scene, Flight Deck material theme, Phosphor palette, Studio layout.'),
('Audio behavior','Mono or stereo, matched input/output layout; host samples are left unchanged; zero reported algorithmic latency.'),
('Local operation','No account, online activation, runtime AI, audio upload, network requests or recording to disk in Aurora.')
],[.29,.71])
h(b,'What is delivered')
p(b,'The macOS download contains both plug-in bundles and release documentation. The corresponding-source download contains Aurora source, tests, build scripts, technical records, the small editable Pearl Cloud design project and the exact vendored JUCE source with its notices. The development preview and QA applications are tools for synthetic signals; they are not standalone system-audio monitoring products.')
h(b,'Platform scope')
p(b,'This release does not include Windows, Linux, AAX, VST2 or a standalone audio capture application. A universal binary includes Intel machine code, but Intel runtime compatibility has not yet been tested. Actual Ableton playback of this revision and other GPU configurations remain unverified; the completed checks are listed later in this document.')
p(b,'The CMake source contains an intended deployment default of macOS 11.0. The verified release was built with an empty deployment-target cache entry and macOS 13.3 SDK, so its actual minimum is 13.3. This handoff states the binary requirement rather than advertising the unverified intended default.')

b=page('Native runtime & host integration','The plug-in is compiled software hosted by a DAW.')
table(b,['Layer','Technology and responsibility'],[
('DAW interface','JUCE AudioProcessor and AudioProcessorEditor; JUCE Audio Unit and VST3 wrappers. Product Aurora, manufacturer Soft Club, bundle ID audio.softclub.aurora.'),
('Native interface','JUCE Components, Graphics, Paths, Images, controls and Viewports. Native 2D drawing renders the waveform, spectrum, spectrogram, stereo display and levels.'),
('GPU renderer','JUCE OpenGLContext requests desktop OpenGL 3.2. GLSL shaders, vertex/index buffers, floating-point textures and framebuffer passes produce the creative scenes.'),
('State and files','JUCE ValueTree and XML for host state and .aurora appearance files; a local preset directory. No database or server.'),
('Standard library','C++ arrays, vectors, algorithms, atomics, mutexes and numeric functions. The independent analysis engine has no third-party DSP dependency.'),
('System frameworks','Apple frameworks supply windowing, graphics, plug-in hosting and system integration. They are supplied by macOS, not shipped inside Aurora.')
],[.25,.75])
h(b,'JUCE modules and system linkage')
p(b,'The CMake target links juce_audio_utils and juce_opengl plus JUCE recommended configuration flags. JUCE resolves their supporting modules, including audio processors, audio basics, audio formats/devices, GUI, graphics, events, data structures and core utilities.')
p(b,'The verified link command includes AudioUnit, AudioToolbox, CoreAudio, CoreAudioKit, CoreMIDI, Cocoa, Foundation, IOKit, Security, Accelerate, QuartzCore, OpenGL, WebKit and DiscRecording, with weak Metal and MetalKit linkage. Some arrive transitively from JUCE. This list does not imply Aurora captures MIDI, uses a browser UI, burns discs or renders its scenes with Metal.')
h(b,'Build-time feature switches')
code(b,'JUCE_WEB_BROWSER=0\nJUCE_USE_CURL=0\nJUCE_VST3_CAN_REPLACE_VST2=0\nFORMATS AU VST3\nIS_SYNTH FALSE\nNEEDS_MIDI_INPUT FALSE\nNEEDS_MIDI_OUTPUT FALSE')
p(b,'The UI uses local system typography and native widgets. There is no Electron, React web front end, embedded website, CDN, telemetry service, cloud database, machine-learning model or external shader service at plug-in runtime. A hosted download page is separate from the plug-in and requires no Aurora backend.')

b=page('Audio & analysis architecture','Real-time audio, worker analysis and graphics have separate responsibilities.')
table(b,['Stage','Data and scheduling'],[
('1 · Host callback','processBlock receives floating-point PCM and copies it into a preallocated single-producer/single-consumer FIFO. It reads host buffers without writing them.'),
('2 · Bounded queue','Two fixed channel arrays have capacity 32,768 frames. JUCE AbstractFifo coordinates indices. A full queue drops analysis frames instead of waiting.'),
('3 · Worker','A JUCE thread drains up to 1,024 frames at a time into fixed arrays and runs AudioAnalyzer. While empty, it waits 4 ms before polling again.'),
('4 · Snapshots','The analyzer publishes meter/FFT snapshots and continuous waveform history. Snapshot/reset access uses a mutex outside the audio callback; snapshots own their copied data.'),
('5 · Display','The editor consumes the latest available results. Visible, running native meters request repaint at 60 Hz; the scene has selectable 15/30/60 Hz targets, default 30.'),
('6 · GPU thread','The scene renderer receives a copied feature state, runs its required shader passes and presents to the editor’s own OpenGL context.')
],[.24,.76])
h(b,'Audio callback contract')
p(b,'The callback performs no buffer allocation, FFT, rendering, mutex locking or operating-system event notification. It does not alter, normalize or delay the DAW’s samples. The processor reports zero latency and accepts only matching mono or stereo buses. A null right input in the standalone analyzer duplicates left visually; mono loudness counts one channel rather than two.')
h(b,'Discontinuities and bounded memory')
p(b,'FIFO overflow is counted and marks a discontinuity. The worker discards its pending backlog and resets analysis windows so missing samples are not joined into false continuous measurements. Reset or format changes also invalidate old history. The audio path remains untouched. Incoming PCM is not retained indefinitely or written to a file; fixed rings and histograms keep working storage bounded with session duration.')
h(b,'Timing limits')
p(b,'Analysis is asynchronous. Offline exports can advance faster than the worker and drop analysis data, while the exported audio remains unchanged. The 80 ms waveform display buffer affects the visual timeline only. Freeze holds the display, while audio and analysis continue. Actual frame delivery depends on the host, system load, GPU and display scale; repaint targets are not guaranteed frame rates.')

b=page('Meter definitions & accuracy','Measured data and creative visual controls are kept separate.')
table(b,['Meter','Definition'],[
('Sample peak','Maximum absolute sample in the latest complete 50 ms window, converted to dBFS. No intersample reconstruction.'),
('Peak hold','Maximum absolute sample since reset. Counts all delivered samples independently of FFT cadence, UI polling or display freeze.'),
('RMS','Unweighted mean-square amplitude over the same 50 ms window. A full-scale sine is about −3.01 dBFS.'),
('Correlation','Sum(L × R) / sqrt(sum(L²) × sum(R²)) over 50 ms. In-phase is +1; inverted polarity is −1; silent/empty-channel windows report 0. DC is not removed.'),
('Spectrum','4,096-point periodic Hann window, 1,024-sample hop, 2,049 bins including DC and Nyquist. Stereo powers are averaged before dB conversion, preserving anti-phase energy.'),
('Spectral calibration','Bin frequency is index × sample rate / 4,096. Coherent-gain correction makes a bin-centred full-scale sine on both channels read 0 dBFS. Off-bin tones spread over bins.'),
('Momentary / short-term','Two-stage K weighting; summed channel power and −0.691 offset. Windows are 400 ms and 3 s, updated every 100 ms after the first full window.'),
('Integrated LUFS estimate','Overlapping 400 ms blocks; −70 LUFS absolute gate and relative gate 10 LU below the absolute-gated mean. A fixed 0.1 LU histogram stores actual summed energies and counts.')
],[.26,.74])
h(b,'What “estimate” means')
p(b,'Integrated gate decisions use each occupied histogram bucket’s mean energy. Blocks near the relative threshold may be included or excluded together, although the final average uses actual energies rather than rounded bucket centres. There is no fixed session-duration expiry. This implementation is not a certified BS.1770 / EBU R 128 meter and has not passed a full standards compliance suite.')
p(b,'The analysis accepts 8–384 kHz sample rates, recalculating filter coefficients per rate. Time windows round to whole samples. Invalid formats are ignored and nonfinite analysis samples become zero; host audio is still not rewritten. Level fields use a finite −120 dB floor, also used before enough loudness samples exist.')
h(b,'Excluded measurements')
p(b,'Aurora does not currently measure true peak, loudness range or calibrated analog VU/PPM. The low/mid/high controls used for animation, visual sensitivity, waveform colours, automatic stereo display gain and scene transients are creative encodings. They do not change calibrated meter values and are not beat or tempo detectors.')

b=page('Waveform & instrument rendering','The visual detail is derived from stored sample statistics.')
h(b,'Continuous waveform history')
p(b,'The worker accumulates every sanitized sample into buckets of round(sample rate / 1,000) samples. Each channel retains signed minima/maxima, the exact first sample positions of those extrema, double-precision squared-sample energy, filtered-band energy and 65 signed amplitude-occupancy counters. The ring retains at least 24.25 seconds. An unfinished bucket is published as a replaceable tail, so stopping between bucket boundaries does not erase final samples.')
p(b,'At the actual display pixel density, the editor groups only contiguous buckets on sample-time boundaries. Energies and histogram counts are summed before deriving RMS and colours. Missing intervals stay blank. This preserves measured extrema and avoids averaging already-normalized colours. The histogram is quantized, does not preserve sample order and cannot reconstruct individual PCM samples.')
table(b,['Display feature','Implementation'],[
('Materials','Silk emphasizes measured amplitude occupancy; Prism emphasizes filtered-band RMS contours; Trace emphasizes fine RMS/band lines. All share the same signed envelope.'),
('Colour','Bands uses configurable low/mid/high colours. Its 12 dB/oct Butterworth splits are 250 Hz and min(4 kHz, 0.45 × sample rate). Palette maps RMS from −60 to 0 dBFS through three RGB stops.'),
('Brightness','Density receives local three-bin smoothing, per-column maximum normalization and square-root contrast. This is artistic shading of measured occupancy, not calibrated probability.'),
('Time / amplitude','1, 3, 6, 12 or 24 s; custom 0.25–24 s. Shared 1×/2×/4× magnification and display gain produce explicit fixed axes. Raw full-scale flags still require actual samples reaching ±1.'),
('Cache / scrolling','A circular pixel cache reuses stable columns. A fractional sample clock uses an 80 ms display buffer and bounded correction, never scrolls beyond received audio, holds on stop and reanchors on resume.'),
('Scope','Separate triggered close-up: up to 768 original samples from the latest 1,024-sample snapshot. Shared, labelled adaptive amplitude range; history materials and magnification do not apply.')
],[.25,.75])
h(b,'Other meter surfaces')
p(b,'The spectrogram stores a 480 × 256 cache of UI-sampled FFT history, up to 24 seconds, and recolours retained dB data through a lookup table. It is approximate wall-time history, not a lossless spectrogram recording. The stereo display accumulates mid/side sample density into a 256-square cache, with configurable persistence and display gain. Spectrum traces and trails project measured data onto a logarithmic axis and clip at Nyquist.')

b=page('Creative scenes & GPU pipeline','Procedural shaders react to copied audio features.')
p(b,'The six scenes are Folded Light, Liquid Aperture, Pearl Cloud, Night Transit, Diffraction Veil and Orbit Loom. Scene selection, camera orbit, zoom, three-stop palette, theme, exposure, bloom, saturation, speed and sensitivity are editable. VisualReactivity separates artistic response from numeric metering with adaptive normalization, 18 ms attack, 160 ms release and a 180 ms transient envelope; missing snapshots release after 250 ms.')
h(b,'Pearl Cloud: persistent fluid and layered material')
p(b,'PearlFlow uses floating-point ping-pong textures for velocity, pressure and pigment. Its bounded fixed steps perform velocity advection, curl/confinement and audio forcing, divergence, 12 pressure iterations, pressure projection and pigment transport. Limited MacCormack correction preserves fine dye structure without introducing new extrema. The production pigment canvas has a fixed 848:512 aspect; the solver’s long side is 256 cells and the dye long side is 768 texels.')
p(b,'PearlFlowMaterial integrates three curved optical strata over 128 samples per view ray. Density, relief, transmission and highlights create the pearlescent volume; a support mask contains it. Colour channels stay palette-independent so colour edits can update the appearance immediately. Silence and Freeze stop simulation advance. A zero flow-speed control also holds motion. The fallback shader is the earlier deterministic design study.')
h(b,'Shared rendering and performance choices')
p(b,'Only Pearl Cloud initializes and advances the fluid solver. The other five scenes use their own procedural geometry/material shaders. All scenes receive a floating-point finishing pass with quarter-resolution bloom; finishing is not fed back into the simulation. Camera framing considers rotated bounds, while intentional zoom can crop the image.')
p(b,'Uniform locations are cached after linking. Unchanged frozen scenes avoid redraws. Covered or absent scene panels skip updates, and Customise parks the existing OpenGL surface outside the visible area to preserve its context and resources. Closing the editor releases its GPU context. Palette, spectrum and stereo caching reduce repeated CPU work without reducing measured waveform resolution or Pearl Cloud’s material sampling.')
h(b,'Optional Diffusion Studio authoring')
p(b,'Diffusion Studio v0.211.0 was used to edit and render the earlier Pearl Cloud shader study. Its project is TypeScript/TSX plus GLSL, with solid-js and @diffusionstudio/jsx supplied by the editor. There is no local npm dependency installation for that project. Studio is optional and is not linked, bundled or required by Aurora. Despite its name, no diffusion model or runtime AI is part of the plug-in.')
p(b,'design/pearl-cloud/pearl-cloud.glsl generates shader.ts for Studio and Source/PearlCloudShader.h for the native fallback. scripts/sync-pearl-cloud.py checks those embeds. The current persistent solver and material live in PearlFlow.h and PearlFlowMaterial.h; the earlier Studio renders do not demonstrate that newer engine’s performance.')

b=page('Appearance, presets & persistence','Presentation settings never rewrite audio measurements.')
p(b,'Aurora supplies 24 complete factory looks, 12 named palette choices plus Custom, four material themes and four layouts: Studio, Meter strip, Waveform and Custom. The appearance model adds 96 validated custom settings to the core selectors, colours, camera and intensity. A complete factory look sets all 112 appearance XML attributes explicitly.')
table(b,['Area','Editable controls'],[
('Colours / interface','Arbitrary RGB sliders and six-digit hex values; three gradient stops and midpoint; interface background, text, grid, panel and clip-marker colours; independent module palettes.'),
('Waveform','Material, colour mode and band colours; channels; custom time window; gain, density, RMS body, crest, band detail, fill, gradient, grid and labels.'),
('Spectrum / spectrogram','Frequency and dBFS limits, line/fill, trails, contrast, time span, direction and grid.'),
('Stereo / levels','Stereo persistence, brightness, rotation and gain; levels floor, warning threshold, RMS shading, peak holds, segments and loudness visibility.'),
('Scene / layout','Scene, camera, motion, response, finish and frame-rate target; gaps, proportions, one/two/three columns, module visibility and order.'),
('Presets','Factory collection, previous/next navigation, modified-state indicator, local saved library, import/export, section reset and whole-appearance reset.')
],[.27,.73])
h(b,'Storage contracts')
p(b,'Host state is a JUCE ValueTree serialized to XML and copied to the host’s binary state block. Its root is AuroraMetersState, version 1; it includes appearance and Freeze. Imported appearance presets use a separate appearance-only contract. Finite values and limits are validated, RGB values are masked to 24 bits and module positions remain a permutation. Missing older properties receive defined defaults.')
p(b,'The local macOS library is ~/Library/Aurora/Presets/, one .aurora XML file per saved appearance. Presets contain no recorded audio, FFT history, loudness integration or measurement history. Loading/importing a preset or resetting appearance preserves current measurements and Freeze. The main Reset button separately clears measurement state.')
h(b,'UI behavior')
p(b,'Customise is a native dropdown within the editor, with scrollable content in short layouts. Toggle or Escape closes it. Header layout selection closes it and reveals the chosen meters. Controls synchronize only when their model changes, preventing timer refreshes from erasing an asynchronously delivered menu selection. Camera drag, wheel/pinch zoom and reset remain available for the 3D scene.')

b=page('Build tools & reproducible source','Tools used to author and package Aurora are separate from runtime dependencies.')
table(b,['Tool','Role / verified version'],[
('Apple toolchain','Apple Clang 14.0.3.14030022, Command Line Tools compiler and macOS 13.3 SDK produced the verified release. C++17 is required.'),
('CMake','3.31.6 installed locally from PyPI; project minimum is 3.22. Generates plug-in and test targets through JUCE CMake helpers.'),
('Ninja','Pinned Python distribution ninja==1.11.1.4. Executes generated builds.'),
('Python 3','Creates the local tool environment, synchronizes shader embeds and writes ZIP archives with zipfile. Not required to run the plug-in.'),
('Shell / Git','Bash build, signing and install scripts; Git fetches JUCE 8.0.6 when the vendored tree is absent.'),
('Apple release utilities','codesign signs/verifies bundles; lipo checks architectures; otool checks linked libraries and deployment metadata; auval validates Audio Units; ditto copies bundles.'),
('QA / capture tools','CTest and original native test runners. Optional Swift video encoder uses AVFoundation, CoreGraphics, CoreVideo, ImageIO and Accelerate to encode captured PNGs.'),
('Authoring assistance','OpenAI Codex assisted implementation, research, tests and packaging in this workspace. It is a development tool, not an Aurora dependency or service.'),
('Optional visual editor','Diffusion Studio 0.211.0 and its CLI; only needed to open the included editable design study.')
],[.25,.75])
h(b,'Standard macOS build')
p(b,'Install Apple Command Line Tools and Python 3, extract the source archive, then run from aurora-meters:')
code(b,'xcode-select --install\n./scripts/build.sh')
p(b,'build.sh obtains JUCE 8.0.6 if needed, creates .tools, installs pinned CMake/Ninja, configures Release for arm64;x86_64, builds with four jobs, runs CTest and ad hoc signs macOS bundles. With the included JUCE tree it does not need a fresh JUCE clone; the first tooling install still requires internet. Nothing is automatically installed into a DAW.')
p(b,'For a release with an explicit deployment target, pass -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 when configuring. Do not infer a binary’s minimum OS from the default in CMakeLists.txt; inspect its Mach-O metadata. Changing the target is a new build that requires validation.')
code(b,'.tools/bin/cmake -S . -B build -G Ninja \\\n  -DCMAKE_BUILD_TYPE=Release \\\n  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \\\n  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3\n.tools/bin/cmake --build build --parallel 4\n.tools/bin/ctest --test-dir build --output-on-failure')

b=page('Validation & measured performance','Evidence is scoped to the fixtures and hardware actually tested.')
p(b,'The 14 October 2026 revision built universal AU, VST3 and QA applications. Apple’s Audio Unit validator passed. Signed installed executables matched the tested build hashes. The full machine-readable record is docs/inline-controls-validation.json; the readable sequence of revisions is docs/VALIDATION.md.')
table(b,['Check','Completed result'],[
('Headless CTest','8 suites passed in 10.89 s: analysis, visual_framing, visual_reactivity, waveform_clock, waveform_projection, waveform_ribbon, processor and editor_performance.'),
('Editor cache/raster','91 checks, including pixel equivalence against retained reference routines, relevant invalidation, unrelated-setting cache reuse and incremental spectrogram consistency.'),
('Native customization','520 checks, 53 captures, zero failures. Exercises asynchronous menu/timer races, header/embedded layouts, compact heights, scrolling, toggle, Escape and same-window behavior.'),
('Factory collection','1,044 checks, 42 captures and 17 actual GPU captures. All 24 looks; settings/history preservation and repeated same-context dropdown lifecycle.'),
('Audio Unit','auval -v aufx Auro Aucl passed. Universal architecture and strict code-signature verification were also checked.')
],[.27,.73])
h(b,'Isolated CPU comparison')
p(b,'Median stereo raster work fell from 14.561 ms to 0.462 ms. Full spectrogram recolouring fell from 58.895 ms to 10.740 ms. These compare retained test-only baseline routines with the production renderer on identical synthetic data. Output pixels match exactly. They are timings of those routines, not whole-editor, DAW or audio-thread timings.')
h(b,'Isolated GPU comparison')
p(b,'On Apple M2 Max at 1,440 × 900 physical pixels, 30 synchronized frames per scene showed 64–75% lower median rendering cost for the five non-Pearl scenes: 2.357–2.900 ms before and 0.712–0.881 ms after. The baseline deliberately reinserts the previous unused fluid workload into the current renderer; it is not an old-binary comparison. GL checks passed, inactive fluid pixels stayed unchanged and Pearl Cloud still evolved. No Pearl Cloud speedup is claimed.')
h(b,'What these tests do not establish')
p(b,'Fixtures use synthetic PCM and open no audio device. Results do not establish Ableton frame rate, real-time audio guarantees, Intel runtime compatibility, every host/version/GPU combination, notarization or full ITU/EBU certification. Prior waveform tests also cover exact extrema, energy grouping, partial tails, clipping, gaps, fixed axes, freeze/resume and cache correctness. The native GUI and GPU runners require a graphical desktop and are separate from headless CTest. This handoff document is generated with Python 3 and ReportLab; pypdf checks its extracted text and Poppler renders the pages for visual inspection. These document tools are optional and not plug-in dependencies.')

b=page('Distribution, signing & installation','The release can be served as ordinary downloadable files.')
h(b,'Release artifacts')
p(b,'The public binary archive is Aurora-0.1.0-macOS-universal.zip and the corresponding-source archive is Aurora-0.1.0-source.zip. Keep the matching source and dependency notices alongside the binary when publishing the release. A website can link these files directly; GitHub Releases can host them as assets. Aurora needs no server code, package manager, runtime database or account service. This handoff does not create or publish a website.')
h(b,'Signature status')
p(b,'The bundles are locally ad hoc signed with codesign --sign - and pass strict signature verification. They are not Developer ID signed or notarized. Local validation does not establish acceptance of a browser-downloaded, quarantined copy on another Mac. This is a material release property, not a notarized installer.')
h(b,'DAW installation locations')
code(b,'~/Library/Audio/Plug-Ins/VST3/Aurora.vst3\n~/Library/Audio/Plug-Ins/Components/Aurora.component')
p(b,'Copy the VST3 and/or Audio Unit bundle into the corresponding user Library folder, then enable that format’s system plug-in location and rescan in the host. Look for Aurora under Soft Club and place it on a track or master bus. Actual host labels and menus depend on the host version. The source repository also provides scripts/install-macos.sh, which installs a locally built release and backs up an existing Aurora bundle.')
h(b,'Source packaging and shader consistency')
code(b,'python3 scripts/sync-pearl-cloud.py --native --check\npython3 scripts/package.py')
p(b,'scripts/package-release.py wraps the validated base archives, documentation and screenshots into Aurora-0.1.0-website-kit.zip. This standard-library Python packager creates the public 0.1.0 downloads, preserves executable modes and writes checksums, with the matching source and dependency notices. It does not publish to GitHub or a website.')
p(b,'The original package.py generates the base Aurora-0.1-macOS-universal.zip and Aurora-0.1-source.zip inputs. It first checks that the editable GLSL and generated Studio/native embeds match. It includes Aurora source, Tests, scripts, docs, CMakeLists.txt, README, LICENSE, THIRD_PARTY.md and the JUCE tree. The small Studio project is explicitly enumerated. Git metadata, build outputs, tool environments, node_modules, caches, backups and working captures are excluded from the source ZIP. The binary ZIP carries the AU/VST3 bundles and project notices.')
h(b,'Releasing a later revision')
p(b,'Build and test the new source, verify architecture and actual minimum OS, sign with the intended release identity, validate AU and representative hosts, and regenerate matching binary/source archives. Use fresh checksums for the final files. Developer ID signing and notarization would be additional release work; neither is implied by the current ad hoc signature.')

b=page('Licenses & provenance','This inventory records project notices; the included license texts remain authoritative.')
table(b,['Component / source','License and role'],[
('Aurora original code','Copyright 2026 Aurora contributors; GNU Affero General Public License v3.0 or later. Full text in LICENSE; project notice in THIRD_PARTY.md.'),
('JUCE 8.0.6','Pinned commit 51a8a6d7aeae7326956d747737ccf1575e61e209. JUCE modules offer AGPLv3 or commercial licensing; Aurora uses the AGPLv3 route. See third_party/JUCE/LICENSE.md.'),
('VST3 SDK in pinned JUCE','The vendored SDK’s notice lists the proprietary Steinberg / GPLv3 options. Aurora’s dependency record identifies the open-source GPLv3 option. Retain the exact bundled notices.'),
('Apple AudioUnitSDK','Apache 2.0 notice in JUCE’s Audio Unit client source. System frameworks remain macOS dependencies.'),
('Pearl Flow solver reference','Finite-difference GLSL informed by/adapted from Pavel Dobryakov’s WebGL Fluid Simulation; MIT, copyright 2017 Pavel Dobryakov. Full MIT notice retained in Source/PearlFlow.h.'),
('Original fluid additions','Aurora’s audio forcing, ribbon sources, lifecycle, optical material and post-processing. No upstream assets or preset pack are bundled.'),
('Bundled JUCE dependencies','Examples include FLAC, Ogg/Vorbis, zlib, PNG, JPEG, HarfBuzz, SheenBidi and OpenGL helper code. Exact licenses/notices are retained in the vendored JUCE tree. Presence in that tree is not a claim that every module is active.'),
('Build tools','CMake, Ninja, Python, Git, Apple tools and optional Diffusion Studio are authoring/build tools; their applications and environments are not linked into Aurora or shipped as plug-in runtime dependencies.')
],[.29,.71])
h(b,'Research references')
p(b,'Ghost and MiniMeters were studied as functional and visual references. No vendor native source, binaries, graphics, presets or branding were copied into Aurora. Public open-source visualizer research also examined projectM, Butterchurn, Pulse, Prism and OpenMeters. Their renderers or preset collections are not incorporated. The solver adaptation listed above is the explicitly retained third-party implementation reference.')
p(b,'Research links, inspected revisions and distinctions between observed demos and source inspection are recorded in docs/REFERENCE-RESEARCH.md and docs/VISUAL-RESEARCH.md. The earlier Soft Club and later Flight Deck directions are original treatments; the release does not bundle Cowboy Bebop imagery, logos or media.')
h(b,'Source and notices stay together')
p(b,'The source archive includes the exact JUCE source used for the release, its dependency notices and Aurora’s build scripts. Read LICENSE, THIRD_PARTY.md and the relevant vendored files for their actual terms. This document is an implementation inventory and does not replace those texts.')

b=page('Source map & further reading','Paths below are relative to the extracted aurora-meters source folder.')
table(b,['Path','Responsibility'],[
('CMakeLists.txt','Plug-in formats, C++17, JUCE module linkage, preview and test targets.'),
('Source/PluginProcessor.{h,cpp}','Audio passthrough, FIFO, analysis worker, host state, preset import/export and defaults.'),
('Source/Analysis.{h,cpp}','Independent DSP: FFT, peaks, RMS, correlation, K weighting, gated loudness, waveform statistics and history.'),
('Source/PluginEditor.{h,cpp}','Native meter drawing, display caches, controls, dropdown, layouts and editor timing.'),
('Source/Customization.h; CustomizationPanel.cpp','96 validated appearance settings; native section controls, colour entry, preset library and layout controls.'),
('Source/FactoryPresets.cpp; FactoryPresetBrowser.cpp','24 fully specified factory appearances and illustrated native gallery.'),
('Source/Palette.h; SpectralProjection.h','Palette definitions and frequency projection used consistently for drawing and readouts.'),
('Source/WaveformClock.h; WaveformProjection.h; WaveformRibbon.h','Fractional sample timeline, precise grouping and measured-material pixel caching.'),
('Source/VisualSurface.{h,cpp}; VisualFraming.h; VisualReactivity.h','GPU bridge, scenes/camera, fitted bounds and independent artistic music response.'),
('Source/PearlFlow.h; PearlFlowMaterial.h','Persistent solver, retained MIT notice and original layered ray-integrated material.'),
('Source/SceneMaterial.h; VisualPostProcess.h; PearlCloudShader.h','Other scene materials, floating-point finishing/bloom and generated fallback shader.'),
('design/pearl-cloud/','Editable optional Studio TSX/GLSL study, generated import and its capture record.'),
('Tests/','Independent DSP and rendering tests, native UI/GPU runners, procedural music fixtures and silent preview.'),
('scripts/','Build, sign, install, package, shader synchronization and optional Swift preview-video encoder.'),
('docs/METERING.md','Detailed meter definitions, timing, display encodings and known accuracy limits.'),
('docs/VALIDATION.md; inline-controls-validation.json','Human-readable verification history and exact current machine-readable results/hashes.'),
('LICENSE; THIRD_PARTY.md; third_party/JUCE/','Aurora license, dependency summary and pinned framework source/notices.')
],[.43,.57])


# Self-contained HTML, designed both for browser reading and print.
css='''*{box-sizing:border-box}html{color-scheme:light}body{margin:0;background:#eef2f1;color:#182f34;font:16px/1.62 -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}main{max-width:1050px;margin:48px auto;padding:0 24px}.coverline{font:12px/1.5 ui-monospace,monospace;letter-spacing:.12em;text-transform:uppercase;color:#416f73}.toc{background:#dce8e6;border-radius:14px;padding:24px 30px;margin-bottom:28px}.toc ol{columns:2;margin:12px 0 0;padding-left:22px}.toc a{color:#245b63;text-underline-offset:3px}.chapter{background:#fff;border:1px solid #d9e3e1;border-radius:16px;margin:24px 0;padding:42px 46px;box-shadow:0 7px 25px #17393805}h1{font-size:34px;line-height:1.12;letter-spacing:-.035em;margin:8px 0 12px}h2{font-size:20px;line-height:1.3;margin:28px 0 10px}p{margin:12px 0}.subtitle{color:#567177;font-size:15px;margin-bottom:24px}.num{color:#4c8a8b;font:13px ui-monospace,monospace;letter-spacing:.14em}table{width:100%;border-collapse:collapse;margin:20px 0;font-size:14px;line-height:1.5;table-layout:fixed}th,td{text-align:left;vertical-align:top;padding:11px 13px;border-bottom:1px solid #dae4e2;overflow-wrap:anywhere}th{background:#183e46;color:white;font-size:12px;letter-spacing:.04em}td:first-child{font-weight:600;color:#214b52}tbody tr:nth-child(even){background:#f1f6f5}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#eaf1ef;border-left:3px solid #70a5a4;padding:16px;font:12px/1.6 ui-monospace,SFMono-Regular,monospace;border-radius:5px}footer{color:#597175;font-size:13px;text-align:center;padding:20px}@media(max-width:650px){main{margin:20px auto;padding:0 12px}.chapter{padding:25px 21px}h1{font-size:28px}.toc ol{columns:1}table{font-size:12px}th,td{padding:8px}}@media print{@page{size:A4;margin:17mm 17mm 18mm}body{background:white;font-size:10pt;line-height:1.43}main{margin:0;padding:0;max-width:none}.toc,footer{display:none}.chapter{margin:0;padding:0;border:0;box-shadow:none;border-radius:0;break-after:page}.chapter:last-child{break-after:auto}h1{font-size:24pt}h2{font-size:12pt;break-after:avoid}table{font-size:8.6pt;line-height:1.4}tr{break-inside:avoid}th,td{padding:6px 8px}pre{font-size:8.4pt}p{orphans:3;widows:3}}'''
html=['<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Aurora — Technical stack</title><meta name="description" content="Complete technical stack, architecture, build tools and validation for the Aurora audio analysis plug-in."><style>'+css+'</style></head><body><main><div class="coverline">Aurora / Engineering record</div><nav class="toc" aria-label="Contents"><strong>Technical stack · Contents</strong><ol>']
for i,pg in enumerate(pages):html.append(f'<li><a href="#chapter-{i+1}">{escape(pg["title"])}</a></li>')
html.append('</ol></nav>')
for i,pg in enumerate(pages):
    html.append(f'<section class="chapter" id="chapter-{i+1}"><div class="num">{i+1:02d}</div><h1>{escape(pg["title"])}</h1><p class="subtitle">{escape(pg["subtitle"])}</p>')
    for block in pg['blocks']:
        if block[0]=='h':html.append('<h2>'+escape(block[1])+'</h2>')
        if block[0]=='p':html.append('<p>'+escape(block[1])+'</p>')
        if block[0]=='code':html.append('<pre><code>'+escape(block[1])+'</code></pre>')
        if block[0]=='table':
            _,heads,rows,widths=block
            html.append('<table><colgroup>'+''.join(f'<col style="width:{x*100}%">' for x in (widths or [.3,.7]))+'</colgroup><thead><tr>'+''.join('<th scope="col">'+escape(x)+'</th>' for x in heads)+'</tr></thead><tbody>')
            for row in rows:html.append('<tr>'+''.join('<td>'+escape(x)+'</td>' for x in row)+'</tr>')
            html.append('</tbody></table>')
    html.append('</section>')
html.append('</main><footer>Aurora 0.1.0 · Technical stack · 14 October 2026</footer></body></html>')
(OUT/'TECHNICAL-STACK.html').write_text('\n'.join(html))

# Accessible text PDF with fixed chapter starts and restrained print typography.
styles=getSampleStyleSheet()
styles.add(ParagraphStyle(name='DocTitle',fontName='Helvetica-Bold',fontSize=23,leading=27,textColor=colors.HexColor('#173e46'),spaceAfter=9))
styles.add(ParagraphStyle(name='Subtitle2',fontName='Helvetica',fontSize=9.4,leading=13,textColor=colors.HexColor('#5a767b'),spaceAfter=18))
styles.add(ParagraphStyle(name='Body2',fontName='Helvetica',fontSize=9.1,leading=13.3,textColor=colors.HexColor('#20373d'),spaceAfter=8))
styles.add(ParagraphStyle(name='Head2',fontName='Helvetica-Bold',fontSize=11.2,leading=15,textColor=colors.HexColor('#214e58'),spaceBefore=13,spaceAfter=6,keepWithNext=True))
styles.add(ParagraphStyle(name='Cell2',fontName='Helvetica',fontSize=8.05,leading=11.2,textColor=colors.HexColor('#20373d')))
styles.add(ParagraphStyle(name='CellBold2',parent=styles['Cell2'],fontName='Helvetica-Bold'))
styles.add(ParagraphStyle(name='TableHead2',parent=styles['Cell2'],fontName='Helvetica-Bold',textColor=colors.white))
styles.add(ParagraphStyle(name='Code2',fontName='Courier',fontSize=7.6,leading=11,textColor=colors.HexColor('#254d54'),spaceBefore=0,spaceAfter=0))
# Standard PDF fonts do not cover all typographic symbols; use faithful ASCII equivalents.
def pdftext(t):
    return escape(t.replace('−','-').replace('×','x').replace('²','^2').replace('±','+/-').replace('–','-').replace('—','-').replace('’',"'").replace('“','"').replace('”','"').replace('·',' / '))
W,H=A4
width=W-96
story=[]
for i,pg in enumerate(pages):
    if i:story.append(PageBreak())
    story.append(Paragraph(f'AURORA  /  {i+1:02d}',styles['Subtitle2']))
    story.append(Paragraph(pdftext(pg['title']),styles['DocTitle']))
    story.append(Paragraph(pdftext(pg['subtitle']),styles['Subtitle2']))
    for block in pg['blocks']:
        typ=block[0]
        if typ=='h':story.append(Paragraph(pdftext(block[1]),styles['Head2']))
        elif typ=='p':story.append(Paragraph(pdftext(block[1]),styles['Body2']))
        elif typ=='code':
            box=Table([[Paragraph(pdftext(block[1]).replace('\n','<br/>').replace('  ','&#160;&#160;'),styles['Code2'])]],colWidths=[width])
            box.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,-1),colors.HexColor('#edf4f2')),('BOX',(0,0),(-1,-1),.7,colors.HexColor('#c7ddda')),('LEFTPADDING',(0,0),(-1,-1),10),('RIGHTPADDING',(0,0),(-1,-1),10),('TOPPADDING',(0,0),(-1,-1),10),('BOTTOMPADDING',(0,0),(-1,-1),10)]))
            story.extend([Spacer(1,3),box,Spacer(1,9)])
        elif typ=='table':
            _,heads,rows,widths=block
            data=[[Paragraph(pdftext(t),styles['TableHead2']) for t in heads]]
            data += [[Paragraph(pdftext(t),styles['CellBold2'] if n==0 else styles['Cell2']) for n,t in enumerate(row)] for row in rows]
            t=Table(data,colWidths=[width*x for x in (widths or [.3,.7])],repeatRows=1,hAlign='LEFT')
            t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),colors.HexColor('#183e46')),('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),8),('RIGHTPADDING',(0,0),(-1,-1),8),('TOPPADDING',(0,0),(-1,-1),7),('BOTTOMPADDING',(0,0),(-1,-1),7),('LINEBELOW',(0,1),(-1,-1),.4,colors.HexColor('#d9e5e2')),('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,colors.HexColor('#f0f5f3')])]))
            story.extend([Spacer(1,3),t,Spacer(1,10)])
while story and isinstance(story[-1], Spacer):
    story.pop()
def footer(c,d):
    c.saveState();c.setStrokeColor(colors.HexColor('#ccddda'));c.line(48,39,W-48,39);c.setFont('Helvetica',7.5);c.setFillColor(colors.HexColor('#547277'));c.drawString(48,26,'Aurora 0.1.0  |  Technical stack  |  14 October 2026');c.drawRightString(W-48,26,str(d.page));c.restoreState()
SimpleDocTemplate(str(OUT/'TECHNICAL-STACK.pdf'),pagesize=A4,rightMargin=48,leftMargin=48,topMargin=41,bottomMargin=53,title='Aurora - Technical stack',author='Aurora contributors',subject='Native runtime, metering, graphics, build tools, validation and distribution').build(story,onFirstPage=footer,onLaterPages=footer)
print(OUT/'TECHNICAL-STACK.html')
print(OUT/'TECHNICAL-STACK.pdf')
