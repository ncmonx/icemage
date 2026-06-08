# ClusterFuzzLite build image for icemage's pure-parser libFuzzer targets.
FROM gcr.io/oss-fuzz-base/base-builder:latest
COPY . $SRC/icemage
WORKDIR $SRC/icemage
COPY .clusterfuzzlite/build.sh $SRC/
