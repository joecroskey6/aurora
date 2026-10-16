AURORA 0.1.1
Free, open-source audio metering and reactive visuals for your DAW.

macOS 13.3 or later • Apple Silicon + Intel • AU + VST3
Development release • Ad hoc signed • Not Apple notarized

INSTALL

1. Quit your DAW. Open INSTALLATION.html for the installation guide.
2. In Finder, choose Go > Go to Folder (Shift-Command-G).
3. Copy VST3/Aurora.vst3 into:
   ~/Library/Audio/Plug-Ins/VST3/
   For an Audio Unit host, copy AU/Aurora.component into:
   ~/Library/Audio/Plug-Ins/Components/
   Create the destination folder if it does not already exist. Keep each
   plug-in bundle intact. Back up an older Aurora bundle before replacing it.
4. Open your DAW, enable the matching plug-in format/folder and rescan.
5. Add Aurora as an audio effect on a track or master bus. Play audio through
   that track to drive its meters and visuals. It does not generate sound.

The package contains plug-ins, not a standalone app. Logic uses the AU;
Ableton Live can use AU or VST3. Select the format your host supports.

MACOS APPROVAL

This downloaded development build may be blocked by macOS or the host because
it is not Developer ID signed/notarized. Read the Gatekeeper section in
INSTALLATION.html. Only approve software you trust. Do not disable Gatekeeper
globally. A host may offer no approval path for a non-notarized plug-in; the
matching source can be built locally, or use a future notarized release.

DOCUMENTATION AND SOURCE

INSTALLATION.html — installation, host scanning, controls, troubleshooting.
TECHNICAL-STACK.html / .pdf — historical 0.1.0 implementation reference.
The source README and current release notes describe later additions.
LICENSE.txt — Aurora's AGPL-3.0-or-later license text.
THIRD_PARTY.md and licenses/ — dependency/provenance notices.

The corresponding source download is Aurora-0.1.1-source.zip, distributed
alongside this binary download. It includes the pinned JUCE source and build
scripts. No paid license, login, network service or runtime AI is required.

Measured loudness is an estimate; this release does not provide a true-peak
meter. Actual host compatibility and macOS-version support beyond the tested
environment are not certified. See the full stack and installation documents.
