#!/bin/bash
# Runs inside the container: build, unit tests, then every mode against a synthetic web root under
# Xvfb + Mesa llvmpipe, saving screenshots/logs to /out. Frame times here say nothing about the Pi;
# this checks that it builds, shaders compile, and no GL errors occur.
set -u
cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build /build || exit 1
/build/imagetunnel_tests || exit 1
[ -f /web/tunnel_config_potato.json ] || { mkdir -p /web && python3 /src/tools/docker/make_webroot.py /web; }
Xvfb :99 -screen 0 1280x720x24 >/dev/null 2>&1 &
sleep 2
export DISPLAY=:99 SDL_VIDEODRIVER=x11 IT_GL_DEBUG=1
mkdir -p /out
status=0
for mode in floating tunnel grid maze; do
  rm -rf /tmp/cfg
  /build/imagetunnel --source /web --windowed --config-dir /tmp/cfg --bench $mode --seconds 6 > /out/$mode.log 2>&1 &
  pid=$!; sleep 7; import -window root /out/$mode.png 2>/dev/null; wait $pid
  grep -q "\[gl\] error\|\[shader\]" /out/$mode.log && { echo "GL/shader errors in $mode"; status=1; }
  grep BENCH /out/$mode.log || { echo "$mode did not finish"; status=1; }
done
exit $status
