# Meter definitions and implementation limits

The analyzer is original C++17 code with no external dependencies. It operates on mono/stereo floating-point PCM, with 1.0 representing full scale. A null right-channel pointer duplicates the left channel visually. The `channelCount` argument defaults to 2; pass 1 for a mono bus so that loudness counts the signal once. Mono and dual-mono therefore differ by approximately 3.01 LU, while their waveform, spectrum and peak displays match.

This prototype reports sample peaks, RMS, a spectrum, correlation, and estimated loudness. It does not report true peak or loudness range and is not a certified BS.1770 / EBU R 128 meter.

## Display definitions

| Field | Definition |
| --- | --- |
| Peak L/R | Maximum absolute sample in the latest complete 50 ms window, expressed as dBFS. No intersample peak detection or release smoothing is applied. |
| Peak hold L/R | Maximum absolute sample since the last reset. All delivered samples are counted independently of FFT cadence, UI polling, or display freeze. |
| RMS L/R | Mean square in the same 50 ms window, converted to dBFS. A full-scale sine reads approximately −3.01 dBFS. |
| Correlation | Sum of L×R divided by the geometric mean of the two channel energies in the same 50 ms window. +1 is in phase; −1 is inverted polarity. Silent/empty-channel windows report 0. This measure does not remove DC. |
| Spectrum | 4096-point periodic Hann window, updated every 1024 samples. Bins include DC and Nyquist; bin frequency is index × sample rate / 4096. Stereo channel powers are averaged before conversion to dBFS, so inverted stereo signals remain visible. Coherent-gain correction makes a bin-centred full-scale sine on both channels read 0 dBFS. An off-bin sine spreads across bins. |
| Scope source | The latest 1024 samples from each channel at the most recent FFT update; the triggered close-up draws up to 768 of them. |
| Scrolling waveform | Continuous signed extrema, squared-sample energy, filtered-band energy and 65-bin amplitude counts in approximately 1 ms buckets, independent of FFT/UI cadence. See below. |
| Low / mid / high | Artistic 0–1 controls derived from window-energy-normalized band RMS: 20–250 Hz, 250–4000 Hz, and 4000–20000 Hz. A linear mapping from −60 to 0 dB controls the animation; these are not calibrated loudness measurements. |

All level fields use a finite −120 floor. This also represents “not enough samples yet” for loudness windows. `sequence` advances on new FFT frames and resets, allowing the UI to avoid adding duplicate spectrogram columns. `resetGeneration` advances only on reset or format change, so the UI can clear obsolete history even when the sample rate stays the same. Meters and peak holds can update before the next FFT, so the UI should read them independently of `sequence`. Visual smoothing and time spent without new audio belong to the UI/processor layer.

## Loudness

The stereo loudness calculation applies two-stage K weighting, sums the two channel powers, and uses the −0.691 offset. The momentary and short-term windows span 400 ms and 3 s respectively. Their first reading requires a full window, and subsequent readings arrive every 100 ms. Integrated measurement uses overlapping 400 ms blocks, an absolute −70 LUFS gate, then a relative gate 10 LU below the absolute-gated mean. Silence does not reset the integrated reading. These choices follow the [ITU-R BS.1770 recommendation](https://www.itu.int/rec/R-REC-BS.1770-5-202311-I/en) and [EBU Tech 3341](https://tech.ebu.ch/docs/tech/tech3341.pdf).

The integrated history is held in a fixed 0.1 LU histogram containing actual summed energies and counts, rather than an unbounded list of blocks. Gate decisions use each occupied bucket’s mean energy. Consequently blocks near the relative threshold can be admitted or excluded together; results are estimates. The final average uses actual stored energy, not rounded bucket centres. There is no fixed session-duration limit or rolling expiry.

Filter coefficients are recalculated for each sample rate from bilinear-transform parameters matching the published 48 kHz response. The mathematical parameter values can also be inspected in [FFmpeg’s filter implementation](https://www.ffmpeg.org/doxygen/9.0/ebur128_8c_source.html). No source from the reference commercial products is used. Supported rates are 8–384 kHz; timing rounds the 50/100 ms windows to whole samples. Common rates (44.1, 48, 88.2, 96, 176.4, and 192 kHz) divide those windows exactly. Very low sample rates cannot represent the full audible spectrum.

## Integration contract

Call `process(left, right, count, sampleRate, channelCount)` on the analysis worker. It locks and must not run inside a real-time audio callback. `snapshot()` and `reset()` share the lock, and snapshots own their vectors. A sample-rate or channel-count change resets filters and measurement history. Explicit reset preserves the current format. Empty input or an invalid format is ignored; nonfinite samples are replaced with zero. An input gap is not automatically filled with silence: the processor must handle discontinuities and dropped FIFO data explicitly.

The state uses fixed arrays for audio, FFT scratch space, filters, short history, and integrated histogram. It neither retains incoming buffers nor grows memory with playback time. Construction, reset, and UI snapshot copies may allocate; ordinary processing does not resize its buffers.

## Validation

Build and run the independent DSP tests without JUCE:

```sh
clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread \
  Source/Analysis.cpp Tests/AnalysisTests.cpp -o /tmp/aurora-analysis-tests
/tmp/aurora-analysis-tests
```

The tests cover silence and invalid input, spectral amplitude/DC/Nyquist normalization, RMS, anti-phase preservation, stereo correlation, −18 dBFS stereo 1 kHz calibration at four sample rates, momentary/short-term window timing, callback-size independence, both loudness gates, exact impulse peak holds, mono loudness, reset, format changes, and simultaneous worker/UI access. They are useful engineering checks; the full ITU/EBU compliance suite, true-peak tests, listening-session comparisons, and broad host validation remain future work.


## Display revision: orbit and music response

Meter values remain unchanged. The separate **Scope** option shows up to 768 original samples
using a shared, sample-aligned rising-edge trigger. A common slowly released
amplitude scale preserves channel differences and is labelled on both lanes.
Every original sample in the displayed window contributes to a min/max envelope.
The thin trace is a separate interpolated drawing. It is not a reconstructed
continuous waveform or a long-duration recording.

The stereo display adds bilinear sample density into a 256-square cache and keeps
420 ms of weighted history by default (configurable up to 2 s). At zero rotation, mid is vertical and side horizontal. Shared
automatic display gain is disclosed in its tooltip; correlation remains the
calculated 50 ms value. The spectrogram's 256-entry colour lookup table recolours
retained dB values; it does not change spectral magnitude.

`VisualReactivity.h` reconstructs artistic band amplitudes from the analyzer's
compressed controls, normalizes against recent energy, and uses an 18 ms attack,
160 ms release and 180 ms transient envelope. The quiet gate spans approximately
−58 to −46 dBFS of reconstructed band RMS. Missing snapshots release controls
after 250 ms. GUI sampling can miss attacks between snapshots, and the band
encoding cannot recover energy below its floor. These controls affect only the
creative scenes; they do not normalize audio or alter calibrated meter values.


## Scrolling waveform

The worker accumulates every finite input sample into buckets of `round(Fs / 1000)`
samples. Each channel stores signed minimum, signed maximum, squared-sample energy,
filtered-band energy and 65 signed amplitude counts for the exact bucket interval;
unweighted RMS is derived from energy and sample count. Invalid samples are treated
as zero. The histogram centres run from −1 to +1 in steps of 1/32. Every sanitized
sample increments its nearest centre once. Values outside ±1 enter the end bins;
extrema, squared energy and full-scale detection retain the unclipped input.
The ring retains at least 24.25 seconds (including display-buffer margin) and
resets at stream discontinuities.
The unfinished bucket is available separately from completed delta columns; readers
replace that provisional tail on each snapshot, never append it twice. Its last
sample remains visible even if no subsequent audio arrives. The editor
aggregates only contiguous buckets on fixed sample-time boundaries at the physical
pixel density. It sums double-precision sample and filter energies and integer
histogram counts directly, then derives RMS and band fractions. It retains exact first-occurrence sample indices
for extrema and never bridges missing history. It does not reconstruct energies
from rounded RMS or normalized band fractions. The histogram is quantized and
does not retain sample order; shorter history windows expose finer time buckets,
not reconstructed individual PCM samples.

The time menu offers 1, 3, 6, 12 and 24 seconds; 12 seconds is the initial setting.
Shared L/R amplitude magnification offers 1x (±1), 2x (±0.5) and 4x (±0.25).
Each chosen scale is fixed and does not follow recent peaks. Samples beyond the
selected display range are cropped for drawing. Edge flags (red by default, with an editable clip-marker colour) still require raw
samples reaching ±1, and use their exact extremum sample positions; ordinary
magnified cropping is not a full-scale event. No automatic minimum amplitude is
imposed: quiet signals retain their measured range. Constant/DC input within the
view is a thin hairline centred at its measured value. Bands colour is an original artistic encoding of
normalized energy from 12 dB/oct Butterworth filters at 250 Hz and 4 kHz (upper
cutoff limited to 0.45 Fs), with a cascaded high/low mid band. It is not a
calibrated band meter or a reproduction of MiniMeters' undocumented algorithm.
Bands defaults to apricot, mint and violet for low, mid and high energy; each band accepts an arbitrary RGB colour. Palette mode maps
RMS from −60 to 0 dBFS through the user's three RGB stops and uses those stops for
the material, without substituting the fixed Bands colours. Shading and highlight
blends affect the rendered appearance; the saved RGB values are not rewritten.

The three materials share the same measurement data. **Silk** emphasizes amplitude
occupancy, **Prism** emphasizes filtered-band RMS contours and **Trace** emphasizes
thin RMS/band lines. The contours mark symmetric ±RMS magnitudes of the filtered
bands, bounded by the full-band signed envelope; they are not separate band
waveforms or a calibrated stacked amplitude total. Density counts receive local
three-bin smoothing, per-column maximum normalization and square-root contrast
for display. That brightness is an artistic encoding of measured occupancy,
not an absolute probability/density scale. None of these material operations
changes the extrema, energy or numeric readouts. A circular pixel cache reuses
stable columns, while changed partial columns and appearance settings are shaded
again. The final sharp crest follows the measured envelope at the selected scale.

History window, colour mode, material and amplitude magnification are saved in
host state. Scope uses its own triggered snapshot and shared automatic range;
the history material and magnification controls do not apply to it.

Display time follows sample counts with an 80 ms target buffer and bounded
phase correction, requesting 60 Hz repaint. It never scrolls beyond received
PCM. On stopped input it consumes the available partial tail then holds; on
Resume it reanchors to live buffered history. GUI stalls may still reduce actual
frame rate. The buffer adds no audio latency and no audio is recorded to disk.


## Customisation and measurement boundaries

All 96 custom settings change presentation. They do not rewrite PCM or change the
analysis FFT, filter crossovers, loudness gates, peak hold or measured correlation.
Display ranges crop/remap the same measured data. Spectrum limits are projected
on a logarithmic frequency axis and clipped to Nyquist; hover readouts use that
same projection. The level warning threshold is a visual warning, while clip
flags still require actual samples reaching full scale.

Scrolling waveform display gain multiplies its 1x/2x/4x zoom, giving an amplitude
axis of ±1/(gain × zoom). Scope applies gain to its separate labelled adaptive
range. Left and Right selection show that channel at the full available height;
Overlay draws both measured channels on the same scale without summing them.
Density, RMS body, crest and band-detail settings change material intensity;
they never move the signed sample extrema. Scope fill affects the min/max
interior, which has zero area when a display column contains a single sample.

Spectrogram time selection crops the retained 24-second display history. Spectrum
and stereo persistence change visible history; increasing either control collects
longer trails as new snapshots arrive. Missing history remains blank. Freeze
holds the sampled display while colour and rendering controls remain editable.

Appearance presets contain no audio or analysis state. Importing, loading or
resetting appearance leaves the current Freeze state and measured histories
intact. Main-panel Reset remains the separate operation for resetting meters.
