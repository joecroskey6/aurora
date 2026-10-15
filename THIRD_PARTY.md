# Dependency and source notices

Aurora original code: copyright 2026 Aurora contributors, AGPL-3.0-or-later (see LICENSE).

The build downloads JUCE 8.0.6 from https://github.com/juce-framework/JUCE at commit `51a8a6d7aeae7326956d747737ccf1575e61e209`.

JUCE 8 modules are available under AGPLv3 or a commercial license. This project uses the AGPLv3 option. Retain `third_party/JUCE/LICENSE.md` and the notices included with each enabled JUCE module when redistributing. The JUCE tree lists bundled dependencies and licenses, including VST3 (GPLv3 open-source option), AudioUnitSDK (Apache 2.0), FLAC, Ogg/Vorbis, zlib, pnglib, HarfBuzz and SheenBidi. macOS frameworks are system dependencies.

Build tooling: CMake 3.31.6 and Ninja 1.11.1.4, installed locally from PyPI; not linked into the plug-ins. They are not required at plug-in runtime.

Reference product names are used only in research documentation. No Ghost or MiniMeters code or assets are included.

When publishing binaries, publish the corresponding Aurora source and the exact JUCE source/dependency notices used to build them, including build scripts. The source archive prepared in this workspace includes the pinned JUCE source (excluding git metadata).

Pearl Cloud fluid solver: the finite-difference GLSL solver is informed by and adapted from [Pavel Dobryakov’s WebGL Fluid Simulation](https://github.com/PavelDoGreat/WebGL-Fluid-Simulation), MIT license, copyright 2017 Pavel Dobryakov. Its full MIT notice is retained in `Source/PearlFlow.h`. Aurora's sources, lifecycle, optical material and post-processing are original. No upstream assets or presets are bundled.
