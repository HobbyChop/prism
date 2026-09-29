#!/usr/bin/env bash
# Builds the Munt library, then the app. Run on Linux or WSL with vitasdk.
set -e
export VITASDK=${VITASDK:-/usr/local/vitasdk}
export PATH="$VITASDK/bin:$PATH"
HERE="$(cd "$(dirname "$0")" && pwd)"

mkdir -p "$HERE/munt/build"
cd "$HERE/munt/build"
cmake -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
      -Dlibmt32emu_SHARED=OFF -Dlibmt32emu_BUILD_TESTING=OFF -Dlibmt32emu_C_INTERFACE=ON \
      -Dlibmt32emu_WITH_VERSION_TAGGING=OFF -DCMAKE_CXX_FLAGS="-flto -ffat-lto-objects" \
      ../mt32emu > /dev/null
make

mkdir -p "$HERE/prism/build"
cd "$HERE/prism/build"
cmake .. > /dev/null
make
ls -la "$HERE/prism/build/prism.vpk"
