FROM gcr.io/oss-fuzz-base/base-builder@sha256:6dd7113e000cf047f96c612ae9d960629578c319e7c78a9f67833bc95b80f0c3

COPY . $SRC/icm-graph
COPY .clusterfuzzlite/build.sh $SRC/
WORKDIR $SRC/icm-graph
