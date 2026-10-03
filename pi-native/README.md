# Image Tunnel — native viewer (Raspberry Pi 5 kiosk + Windows screensaver)

A C++20 / SDL2 port of [`../index.html`](../index.html), originally built from
[`../PI_NATIVE_PLAN.md`](../PI_NATIVE_PLAN.md). It shows the same four modes (floating, tunnel, grid, maze) plus a
"random" mode. It reads the same asset layout produced by `process_assets.py` and uses the same v2.2 config files.

One codebase, two platforms:

| | Raspberry Pi 5 | Windows |
|---|---|---|
| Graphics | OpenGL ES 3.1 (Mesa V3D) | OpenGL 4.3+ core via [glad](third_party/glad) |
| Runs as | KMS/DRM kiosk under systemd | `ImageTunnel.scr` screensaver, plus `imagetunnel.exe` for the console |
| HTTP | libcurl | WinHTTP |
| Frame cap | 30 / 60 (vsync) | 30 / 60 / 120 (vsync) or unlocked (VRR) |
| Limits | cut down for the Pi (see the plan, section 3) | the web version's full ranges, and more |
| 3D SBS, roughness maps, "flip" maze transition, shadows on every spotlight, drag & drop images | no | yes |

See [Windows screensaver](#windows-screensaver) below for the Windows build.

## Build (Linux / Raspberry Pi)

On Raspberry Pi OS (Bookworm or Trixie, 64-bit), or any Linux machine with Mesa:

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

## Windows screensaver

### Build (MSYS2 UCRT64)

```bash
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,SDL2,libwebp,glm,nlohmann-json,pkgconf}
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
build-win/imagetunnel_tests.exe
```

This produces:
- **`ImageTunnel.scr`:** the screensaver, about 9 MB. It is fully static (SDL2, libwebp and the C++ runtime are
  linked in) and needs only DLLs that ship with Windows 10/11.
- **`imagetunnel.exe`:** the same program built for the console, for `--bench` runs and logs.

### Install

Copy `ImageTunnel.scr` to `C:\Windows\System32`, or right-click it and choose **Install**. Then select
**Image Tunnel** in *Settings → Personalization → Lock screen → Screen saver*.

The control panel uses the standard screensaver arguments:

| Argument | What happens |
|---|---|
| `/s` | Fullscreen on the primary monitor. Other monitors go black. Any key, click or real mouse movement exits. |
| `/c` | Settings window: the live scene plus the full settings panel, with **Save & Close**. Also opens when the `.scr` is run with no arguments. |
| `/p <hwnd>` | Live preview inside the control panel's monitor picture, at 30 fps with cheap settings. |

Settings are saved to `%APPDATA%\ImageTunnel\config.json`, and **Image Source** (the nginx URL or a web-root
folder) is set in the same window. Downloads are cached in `%LOCALAPPDATA%\ImageTunnel\cache`. Because the screensaver
has no console, it writes its log to `%LOCALAPPDATA%\ImageTunnel\imagetunnel.log`.

Launching the `.scr` from a script goes through ShellExecute, which Windows rewrites to `"%1" /S` for `.scr` files, so the
script's own arguments are dropped. Use `CreateProcess`, or run `imagetunnel.exe`, when testing `/c` and `/p`.

### Quality presets and frame cap

The **Display & Quality** section of the settings has a **Quality Preset**:

| Preset | Render scale | MSAA | Maze shadows | Lights / draw | Roughness | Textures | Anisotropy | Images |
|---|---|---|---|---|---|---|---|---|
| low | 0.75 | off | off | 4 | no | 1024 | 4× | quarter-res |
| medium | 1.0 | 2× | nearest, 1024 | 6 | no | 1024 | 8× | half-res |
| high | 1.0 | 4× | all, 2048 | 8 | yes | 2048 | 16× | half-res |
| **ultra** (default) | 2.0 (supersampled) | 8× | all, 4096 | 8 | yes | 4096 | 16× | full-res |

Ultra is the maximum of every quality setting. It was measured on an RTX 5090 at 3840×2160, with vsync off:
- **Ultra:** the worst mode's GPU p99 is 1.4 ms per frame.
- **Ultra with every content limit maxed and 3D SBS on:** the worst frame p99 is 4 ms. The content limits are 20 lights, a 50-row tunnel, a 20×20 grid, 500 floating images, and a 41×41 maze with 200 paintings.

Pick a lower preset on weaker GPUs; the stats overlay (`P`) shows GPU time per frame. Changing any individual
setting switches the preset to "custom".

The **Frame Cap** options are 30, 60 (the default), 120 or Unlocked.
- **When the cap divides the refresh rate:** it is locked with vsync (swap interval = refresh / cap), for example 60 fps on a 120 Hz display.
- **When it doesn't** (for example 60 on 144 Hz): vsync plus a precise software limiter, which is what G-Sync/FreeSync displays want.
- **Unlocked:** vsync is off.

### Features the Pi build cuts, restored on Windows

- **3D SBS:** half-width side by side, with per-mode separation and FOV widening, as in `index.html`'s StereoEffect.
- **Drag & drop:** drop images or a folder onto the window (`imagetunnel.exe --windowed` or the settings window).
  **Clear custom** goes back to the server images.
- **Fullscreen toggle:** F11 or Alt+Enter.
- **Maze shadows on all 6 spotlights:** one depth-texture-array layer per spotlight, up to 4096².
- **Roughness maps:** a normalised Blinn-Phong lobe driven by `_Roughness` textures, approximating three's
  MeshStandardMaterial.
- **"flip" maze transition:** the web version's CSS `rotateY(90deg)` squeeze.
- **Full limits:** 50 tunnel rows, a 20×20 grid, 500 floating images, a 41×41 maze, 200 paintings, 20 lights, 16× anisotropy, MSAA 8×,
  render scale up to 2×.

### Floating mode collisions

The images fly toward the camera at speeds between 1.0× and 1.5×, so faster ones catch up with slower ones.
`index.html` let them pass through each other, which made a visible "pop" whenever two overlapping images
swapped depth order. **Collisions** in the floating settings (`modes.floating.floatingCollisions`) controls this:

| Value | Behaviour | Cost (measured) |
|---|---|---|
| `off` | Pass through, as `index.html` did. About 2.5 pops per second with 112 images. | none |
| `makeway` (default) | Overlapping images slide apart sideways before they meet; if they still touch, they bump gently (momentum conserved) and stay a small gap apart. Zero pops measured. | 0.07 ms for 500 images |
| `physics` | Native only. [Jolt](https://github.com/jrouwe/JoltPhysics) rigid bodies: off-centre hits make images spin, unless **Stop Rotation** is ticked, in which case they only translate. The same gentle drift toward the camera and containment in the ring apply. | 0.2 ms for 112 images, 1.2 ms for 500 (Ryzen 9950X3D) |

Collisions use each image's visible pixels, not the whole rectangle, so cut-out (transparent) images
bump where they actually look solid. The web version supports `off` and `makeway`, and treats `physics` as `makeway`.

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
