# Pearl Cloud — editable Diffusion Studio study

This project is the editable authoring surface for Aurora's Pearl Cloud shader.
The official Diffusion Studio **v0.211.0** application was installed locally and
the CLI successfully opened this project. Five design iterations were rendered
in Studio; the selected fifth study was reviewed at 0, 4 and 8 seconds. Studio
used a local working copy after a cloud-folder access problem. The canonical
editable source is retained here. The same GLSL was then embedded in the native AU/VST3 build and visibly verified
in the native developer preview; see `../../docs/VALIDATION.md`.

The selected shader is a layered fluid film: **five optical layers, each with
seven domain-warp steps**. Analytically propagated gradients provide its lighting
normals; thin ridges, fibres and three selected colours create the material.
Fibre filtering receives the actual viewport pixel size. It returns RGB already
composited over the supplied background, with no separate alpha channel. A final
support mask covers the entire effect and preserves at least 8% margins. Holding
all supplied inputs fixed makes its output deterministic; it has no own clock.
Bass controls the envelope's width, mids change the flow's scale, and treble
changes the strength of fine fibres and highlights.

## Files

- `pearl-cloud.glsl` is the shader source to edit.
- `index.tsx` supplies the Studio surface, scene, inspector controls and synthetic
  audio-feature values.
- `shader.ts` is generated from the GLSL for Studio imports.
- `../../Source/PearlCloudShader.h` is the corresponding generated native embed.
- `package.json` identifies the Studio project. `solid-js` and
  `@diffusionstudio/jsx` are provided by the editor; this raw WebGL surface needs
  no project-local npm installation or external rendering package.
- `validation.json`, when present, records completed capture fixtures and their
  shader revision. Its absence means there is no portable capture record here.

The native plugin does not require Diffusion Studio to build or run. Its source
archive includes this small project, not the Studio application, its downloads,
node modules or runtime caches.

## Open and edit

Install Diffusion Studio and its `diffusion` command, then run from the Aurora
repository root:

```sh
diffusion open "$PWD/design/pearl-cloud" --background
diffusion context
```

If Studio cannot access a cloud-backed workspace, open a local working copy:

```sh
aurora_studio_work=$(mktemp -d "${TMPDIR:-/tmp}/aurora-pearl-studio.XXXXXX")
cp design/pearl-cloud/index.tsx design/pearl-cloud/shader.ts design/pearl-cloud/package.json design/pearl-cloud/pearl-cloud.glsl "$aurora_studio_work/"
diffusion open "$aurora_studio_work" --background
```

Keep the repository's GLSL as the canonical edit location. After regenerating
`shader.ts`, copy the updated files into the working copy before the next
capture. If editing inside the working copy instead, transfer the approved GLSL
and any intentional JSX changes back here before syncing and packaging.

Edit `pearl-cloud.glsl`, then regenerate the Studio import:

```sh
python3 scripts/sync-pearl-cloud.py
```

The scene ID is `pearl-cloud`. Colour, material and synthetic audio controls are
exposed by `@inspect` annotations in `index.tsx`. Numeric inspector controls use integer percentages (100 = 1.0) so the editor displays their precision clearly. Timeline-driven phase lets the
same timestamp render reproducibly. This study supplies feature values; it does
not capture live audio or prove the plugin's metering accuracy.

## Capture and review

Keep working captures outside the source tree. For example, from the same root:

```sh
diffusion check pearl-cloud
diffusion capture pearl-cloud -t 0 2 4 6 8 10 --per-sheet 3 -o /tmp/aurora-pearl-cloud-captures
diffusion logs
```

Inspect the images themselves for shape, gradients, edge margins, material
response and motion. A structural check alone cannot establish a successful
render. Record the shader SHA-256, application version, timestamp, resolution,
aspect, phase, energy, intensity, theme and three colours for any retained
fixture. Keep only a small, approved representative image in `docs/images`;
capture grids, exports and scratch renders are excluded from the source package.
Timestamp labels on Studio contact sheets are QA annotations; they are not part
of the shader or Aurora's native interface. Earlier candidate shaders and working
captures remain outside the source archive.

For bounds validation, compare the frame's outer margin with an otherwise
identical background-only render. The shader returns RGB already composited over
the supplied background; PNG alpha is not a measure of the sculpture's opacity.
For freeze validation, hold phase and every other shader input fixed while
capturing different timeline timestamps. The resulting pixels should remain
unchanged at a fixed render resolution. These checks were completed for the fixtures in `validation.json`; additional
colours, devices and host configurations still require their own review.

## Transfer an approved revision into Aurora

```sh
python3 scripts/sync-pearl-cloud.py --native
python3 scripts/sync-pearl-cloud.py --native --check
./scripts/build.sh
```

The first command regenerates both hosts from the same GLSL text. Review the
actual native rendering after compilation: Studio uses WebGL while the plugin
uses desktop OpenGL, so a Studio capture does not establish driver compatibility
or native performance. The packaging script checks that the two generated embeds
match the editable source before creating release archives.
