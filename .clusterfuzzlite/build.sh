#!/bin/bash -eu

# Build standalone fuzz targets (no icmg source required — public repo).
# Fuzzes the markdown/text parsing utilities used in docs tooling.

for fuzzer in $SRC/icm-graph/fuzz/fuzz_*.cpp; do
    name=$(basename "${fuzzer%.cpp}")
    $CXX $CXXFLAGS -std=c++17 \
        "$fuzzer" \
        $LIB_FUZZING_ENGINE \
        -o "$OUT/$name"
done
