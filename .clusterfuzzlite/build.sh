#!/bin/bash -eu
# Compile icemage's header-only, pure-parser fuzz targets. These need no project
# build (no DB/sqlcipher) -- they include only self-contained headers under src/,
# so the harness links directly against $LIB_FUZZING_ENGINE.
for target in fuzz_context_budget; do
  $CXX $CXXFLAGS -std=c++17 -I src \
    "fuzz/${target}.cpp" \
    -o "$OUT/${target}" $LIB_FUZZING_ENGINE
done
