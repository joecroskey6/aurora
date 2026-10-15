#!/bin/bash
# Sign outside cloud-synced folders, where Finder metadata can race codesign.
set -euo pipefail
cd "$(dirname "$0")/.."
staging="$(mktemp -d /private/tmp/aurora-sign.XXXXXX)"
trap 'rm -rf "$staging"' EXIT
for relative in 'Aurora_artefacts/Release/AU/Aurora.component' 'Aurora_artefacts/Release/VST3/Aurora.vst3' 'AuroraPreview_artefacts/Release/Aurora Preview.app'; do
  bundle="build/$relative"
  name="$(basename "$bundle")"
  ditto --norsrc --noextattr "$bundle" "$staging/$name"
  codesign --force --deep --sign - "$staging/$name"
  codesign --verify --deep --strict "$staging/$name"
  ditto --norsrc --noextattr "$staging/$name" "$bundle"
done
