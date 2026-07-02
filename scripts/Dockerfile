FROM debian:bookworm-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        gcc-aarch64-linux-gnu \
        libc6-dev-arm64-cross \
        make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

# A fresh BUILD_STAMP each build invalidates this layer, so the plugin always
# recompiles with the current stamp (rm -f forces the rebuild inside the layer).
ARG BUILD_STAMP=
RUN rm -f build/aarch64/dsp.so && make aarch64 BB_BUILD_STAMP="$BUILD_STAMP"

# Output: build/aarch64/dsp.so
