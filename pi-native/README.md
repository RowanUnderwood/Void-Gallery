# Image Tunnel — native Raspberry Pi 5 viewer

A C++20 / SDL2 / OpenGL ES 3.1 port of [`../index.html`](../index.html), built from
[`../PI_NATIVE_PLAN.md`](../PI_NATIVE_PLAN.md). It shows the same four modes (floating, tunnel, grid, maze), reads the
same asset layout produced by `process_assets.py`, and uses the same v2.2 config files. 3D SBS mode is
removed.

## Build

On Raspberry Pi OS Bookworm (64-bit), or any Linux machine with Mesa:

```bash
sudo apt-get install build-essential cmake ninja-build pkg-config git \
    libsdl2-dev libgles-dev libegl-dev libwebp-dev libcurl4-openssl-dev nlohmann-json3-dev libglm-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Dear ImGui and stb are downloaded by CMake at configure time. glm and nlohmann/json are also
downloaded if they aren't installed.

## Run

```bash
# Kiosk, from a console (no desktop running): takes over the display via KMS/DRM
SDL_VIDEODRIVER=kmsdrm ./build/imagetunnel --source http://192.168.1.10/

# Desktop development: a window under X11/Wayland
./build/imagetunnel --source /path/to/nginx/html --windowed
```

`--source` takes either the nginx base URL or a local copy of the web root. Run `--help` to see all options.

| Key | Action |
|---|---|
| `H` | Settings panel (shows the mouse cursor) |
| `P` | Performance overlay: fps, frame-time graph, draw calls, texture MB, SoC temperature, throttle flags |
| `R` | Reload the server config and discard local settings (the old file is kept as `config.json.bak`) |
| `Esc` | Quit. The systemd service restarts the app. |
| `Ctrl+Q` | Quit and stay stopped (exit code 10) |

### Random mode

"random" is a fifth choice in **Display Mode**. Every *Switch Every (s)* seconds it fades to black and
switches to a different one of the four modes. The timer starts once the new mode has finished loading.
- Each mode keeps its own settings, and the panel shows the settings of the mode currently on screen.
- Random mode is stored as `globals.pi.randomMode` / `randomInterval`, not as `activeMode`, so a saved file
  still works in `index.html`. Use **Save Config** to keep it across restarts.

### Rotated monitors and frame cap

`--rotate 90` handles a monitor turned 90° clockwise (portrait); 180 and 270 also work. KMS on the Pi can't
rotate the display by 90°, so the app rotates its own output:
- **Normally:** the turn is folded into the projection and overlay matrices, so it costs nothing.
- **While an ImGui window is open** (settings, stats, loading): the frame is drawn into a portrait buffer
  first and then copied to the screen rotated.

`--frame-cap 30` locks presentation to every second vblank. SDL's KMSDRM backend accepts swap interval 2 but
ignores it, so the app paces the swaps itself and uses blocking page flips. `--frame-cap 0` turns vsync off;
it's for measuring raw render time only.

With a rotated monitor:
- **Mouse:** SDL switches to relative mouse mode and the app keeps its own pointer in rotated
  coordinates. Moving the mouse right moves the pointer right on screen. ImGui draws the pointer while
  settings are open.
- **Field of view:** in portrait, the 75° applies to the short (horizontal) axis instead of the vertical
  one. That keeps the web version's framing; the taller screen just shows more.

On the kiosk, both flags go in `IMAGETUNNEL_ARGS` in `/etc/default/imagetunnel`, which is device-specific and
overrides the config files. Keep the value in quotes:
`IMAGETUNNEL_ARGS="--rotate 90 --frame-cap 30"`.

Measured on the Pi 5: **Render Scale below 1.0 is slower, not faster.** The extra offscreen buffer and blit cost
more than the pixels saved, so leave it at 1.0.

## Deploy as a kiosk

```bash
./deploy/install.sh http://192.168.1.10/ --kiosk
```

The script:
- installs the build dependencies, then builds, tests and installs to `/opt/imagetunnel`;
- adds the user to the `video`, `render` and `input` groups;
- writes `/etc/default/imagetunnel` and enables `imagetunnel.service`;
- with `--kiosk`, stops the Pi from booting into the desktop.

Logs: `journalctl -u imagetunnel -f`.

## Configuration

Settings are applied in this order, each overriding the previous one:
1. Built-in defaults. These match `config` and `modeStore` in `index.html`.
2. `tunnel_config_potato.json` from the source. Use `--config` to pick a different file or URL.
3. `~/.config/imagetunnel/config.json`, written by **Save Config**. It replaces the browser's
   localStorage and is only used if its `version` is `2.2`.
4. `availableTextures` always comes from step 2, because it lists the textures that exist on the server.

Saving keeps keys the Pi doesn't use (such as the stereo settings), so the saved file still works in
`index.html`. Settings that only matter on the Pi are stored under `globals.pi`:

```json
"pi": { "frameCap": 60, "renderScale": 1.0, "msaa": 0, "maxTextureEdge": 1024,
        "uploadBytesPerFrame": 6291456, "mazeShadows": "off", "maxLights": 4,
        "normalMaps": true, "regenTransition": "fade", "autoQuality": false,
        "textureMemoryMB": 600, "localFolder": "", "useLocalFolder": false }
```

Values are clamped to Pi limits when loaded:
- tunnel depth: 20 or less
- grid size: 12×12 or less
- floating images: 150 or less
- maze size: 31 or less
- maze images: 120 or less
- moving lights: 8 or less
- anisotropy: 4 or less
- shadow resolution: 512 or less

### Offline cache

HTTP downloads are cached in `~/.cache/imagetunnel/<host>/…`, together with each file's ETag.
- **Revalidation:** each file is checked with the server once per session using `If-None-Match`. This matters because the asset pipeline renumbers `N.webp` files when it fills gaps, so a cached `N.webp` can go stale.
- **Offline:** if the server can't be reached, the app keeps running from the cache and tries the network again every 30 s.

## Benchmark (acceptance test)

```bash
tools/bench.sh http://192.168.1.10/ 120
```

Runs each mode for 120 s and prints the frame-time average and percentiles plus missed vsyncs. It
writes a CSV per mode. A mode passes if its **p99 frame time is 33.3 ms or less**.

## Headless test from Windows (Docker)

`tools/docker/` holds a Debian Bookworm image (the same base as Raspberry Pi OS) with Xvfb and Mesa's
llvmpipe software renderer. The test script does the following:
1. builds the app and runs the unit tests;
2. creates a synthetic web root (numbered test images and procedural textures);
3. runs each mode for a few seconds;
4. fails if any GL or shader error is logged.

Screenshots and logs end up in `/out`. The V: drive isn't visible to Docker, so the source is streamed
in with `tar`:

```bash
docker build -t imagetunnel-dev tools/docker
tar -cf - --exclude=./build . | docker run --rm -i -v it-out:/out imagetunnel-dev \
    bash -c 'mkdir -p /src && tar -xf - -C /src && bash /src/tools/docker/headless-test.sh'
```

Frame times measured this way say nothing about the Pi. Use `tools/bench.sh` on the Pi for performance.

## How this differs from PI_NATIVE_PLAN.md

- **Shaders** are embedded in `src/render/ShaderSources.h` instead of separate `shaders/*.glsl` files, so
  there's nothing extra to install.
- **Texture memory** is controlled by choosing the decode size per mode so everything fits
  `textureMemoryMB`, instead of evicting least-recently-used textures. Textures already on screen can't be
  evicted anyway. Identical images share one GPU texture.
- **Maze regeneration** offers `fade` (default) or `cut`. The optional FBO "flip" transition (M8) is not
  implemented.
- **Picture frames** drop the fake-AO gasket and the black backing plane from the web version. The
  backing plane sat inside the frame box and was never visible.
- **Maze textures** ignore the `Roughness` map. Walls use Lambert lighting plus a faint specular term,
  with a normal map if one exists.
- **Desktop development** uses Mesa's GLES 3 through SDL, which is the same code path as the Pi. There is
  no separate desktop-GL build.
- **Texture arrays** for paintings (M8) are not implemented. Paintings beyond the fog distance are
  skipped instead.

## Layout

```
src/config     Config model, v2.2 JSON load/save, Pi clamps
src/assets     ImageSource (HTTP + cache / files), decoder (libwebp, stb), worker pool, TextureStreamer
src/render     GL helpers, primitives, shaders, scaled/MSAA scene target
src/modes      Floating, Tunnel (+UV math), Grid, Maze (+generator / line of sight), CardBatch, MovingLights
src/ui         Dear ImGui settings / stats / loading overlays
src/util       Frame statistics, benchmark recorder, auto-quality, thermal monitor
deploy/        systemd unit + install script
tools/         bench.sh
tests/         unit tests (maze connectivity, line of sight vs brute force, tunnel UVs, config round-trip)
```
