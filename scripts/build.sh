#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ ! -d third_party/JUCE ]]; then
  git clone --depth 1 --branch 8.0.6 https://github.com/juce-framework/JUCE.git third_party/JUCE
fi
if [[ ! -x .tools/bin/cmake ]]; then
  python3 -m venv .tools
  .tools/bin/pip install cmake==3.31.6 ninja==1.11.1.4
fi
export PATH="$PWD/.tools/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
if [[ "$(uname -s)" == Darwin ]]; then
  ./scripts/sign-macos.sh
fi
