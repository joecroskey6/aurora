#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
for format in VST3 AU; do
  if [[ "$format" == VST3 ]]; then
    artifact="Aurora.vst3"; destination="$HOME/Library/Audio/Plug-Ins/VST3"
  else
    artifact="Aurora.component"; destination="$HOME/Library/Audio/Plug-Ins/Components"
  fi
  source="build/Aurora_artefacts/Release/$format/$artifact"
  [[ -d "$source" ]] || { echo "Build first: missing $source"; exit 1; }
  mkdir -p "$destination"
  if [[ -e "$destination/$artifact" ]]; then
    backup="$PWD/backups/$(date +%Y%m%d-%H%M%S)-$artifact"
    mkdir -p "$PWD/backups"
    mv "$destination/$artifact" "$backup"
    echo "Previous build backed up to $backup"
  fi
  ditto --norsrc --noextattr "$source" "$destination/$artifact"
  codesign --verify --deep --strict "$destination/$artifact"
  echo "Installed $destination/$artifact"
done
echo "In Ableton: Preferences > Plug-Ins > enable AU or VST3 system folders, then Rescan."
