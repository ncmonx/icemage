# ClusterFuzzLite build image for icemage's pure-parser libFuzzer targets.
FROM gcr.io/oss-fuzz-base/base-builder:latest@sha256:058a0147efd84878a345fbf64d2d4ffbf72bccbeda10fe666844b10c26e92f43
COPY . $SRC/icemage
WORKDIR $SRC/icemage
COPY .clusterfuzzlite/build.sh $SRC/
