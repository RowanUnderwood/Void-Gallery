# Debian Bookworm (= Raspberry Pi OS base) with build deps plus Xvfb + Mesa llvmpipe for headless runs.
FROM debian:bookworm
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git ca-certificates \
    libsdl2-dev libgles-dev libegl-dev libwebp-dev libcurl4-openssl-dev nlohmann-json3-dev libglm-dev \
    xvfb xauth libgl1-mesa-dri libegl-mesa0 python3-pil imagemagick xdotool \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
