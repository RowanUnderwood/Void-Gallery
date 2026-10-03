# Image Tunnel — Native Raspberry Pi 5 Port: Plan

## 1. Goals & constraints

| Item | Target |
|---|---|
| Hardware | Raspberry Pi 5 (4 GB min, 8 GB recommended), VideoCore VII GPU (Mesa V3D, GLES 3.1), 4× Cortex‑A76 |
| OS | Raspberry Pi OS Bookworm 64‑bit (Lite is fine; no desktop needed) |
| Display | 1920×1080 over HDMI, fullscreen kiosk via KMS/DRM |
| Performance | **≥30 fps with Vsync, p99 frame time ≤ 33.3 ms** in all four modes using the Pi preset. Aim for 60 where there's headroom. |
| Input | Keyboard + mouse. `H` toggles settings, same as the web version. |
| Content | Same asset layout produced by `process_assets.py` (`{dir}/{full,halfres,quarterres}/N.webp`, `config.json`, `manifest.json`) |
| Config | Same v2.2 JSON schema as `tunnel_config.json`, so the Gradio Settings tab keeps working |
| Removed | 3D SBS / stereo mode (`useStereo`, `stereoEyeSep`, `stereoFOVBoost`, `StereoEffect`) |

## 2. Issues found in the current web version

These should not be carried over into the port (and are worth fixing on the web side too):

1. **Texture dropped when the spawn queue empties** — `processSpawnQueue()` (index.html:973) calls `getTextureFromBuffer()` once more after the last task. That texture is shifted out of the buffer, queued for upload, and never used or disposed: wasted decode and upload work, plus a GPU memory leak.
2. **Out-of-scope `data`** — `loadConfigFromServer()` (index.html:372) uses `data` outside the `if (response.ok)` block where it was declared. That throws a ReferenceError, the catch swallows it, and the "always restore `availableTextures` from server" step never runs.
3. **Pi preset has no maze section** — `tunnel_config_potato.json` has no `modes.maze`, so a Pi gets desktop defaults: 6 spotlights casting 1024² shadows plus a point light casting cube shadows, about 12 shadow passes per frame.
4. **Idle spotlights still render shadows** — unused maze spotlights get `intensity = 0` but keep `castShadow = true`, so their shadow maps are still drawn every frame.
5. **Paintings don't load nearest-first** — `spawnMazePaintings()` sets `z = row * CELL` and the queue sorts on z, so paintings load from the south edge first, not nearest to the camera as the comment claims. The whole queue is also re-sorted every frame.
6. **`scene.add(undefined)`** — the `maze_painting` branch never sets `mesh`, so index.html:968 adds `undefined` to the scene once per painting.
7. **Draw-call explosion** — each painting is 4 meshes with their own geometry and material. At `mazeImageCount: 200` that's about 800 draw calls.
8. **Everything is blended** — every image card uses `transparent: true, depthWrite: false`, even for opaque folders. That means heavy overdraw and back-to-front sorting, and fill rate is the Pi GPU's main bottleneck.
9. **Tunnel geometry cloned per tile** — `tunnelGeometry.clone()` per tile just to get per-tile UVs. A UV transform uniform does the same job.
10. **Grid memory blow-up** — the saved grid config (20×20, 5 layers) needs about 2,000 unique textures in flight, because every tile gets its own decode and nothing is shared.
11. **Smaller issues:**
    - Drag and drop clears the buffer but never rebuilds the scene.
    - Blob URLs are never revoked.
    - The `lightIntensity` slider rebuilds every light on each tick.
    - Shadow resolution options go up to 8192.
    - The CSS `rotateY` maze transition has no native equivalent.
    - `CLAUDE.md` describes nav as `idle → moving → idle`, but there's also a `turning` phase.

## 3. Features dropped or changed for the Pi

| Feature | Decision | Reason |
|---|---|---|
| 3D SBS stereo | **Dropped** | Per request |
| Browser drag & drop / file picker upload | **Replaced** by a "Local folder" image source (path setting, plus a USB-mount convenience) | No browser |
| Fullscreen toggle | **Dropped** | Kiosk is always fullscreen |
| PBR `MeshStandardMaterial` (roughness maps) | **Replaced** by Blinn-Phong with a fixed specular; normal maps optional | Fragment cost |
| Real-time shadows in the maze | **Off by default.** Optional single 512² shadow map for the nearest spotlight | 6 + 6 shadow passes is far beyond budget |
| Shadow resolution options 2048–8192 | **Dropped** (256/512 only) | Memory and fill rate |
| `lightCount` up to 20 | **Capped at 8** (default 4); each card is lit by at most its 4 nearest lights | Per-fragment cost |
| Anisotropy up to 16 | **Capped at 4** (queried at runtime) | Texture bandwidth |
| Maze spotlight pool 6 | **4** | Lighting cost |
| Raycast line-of-sight against InstancedMesh | **Replaced** with a grid DDA walk on the maze array | Far cheaper, same result |
| CSS 3D flip on maze regeneration | **Replaced** with fade-through-black (default) or FBO quad flip (option) | No DOM |
| "Modern" nav mouse-look | **Kept** (mouse is available) | — |
| Stats.js | **Replaced** by an ImGui overlay (FPS, frame-time graph, CPU ms, textures resident, MB) | — |

## 4. Technology stack

| Layer | Choice | Notes |
|---|---|---|
| Language | C++20 | Predictable frame times, no GC |
| Window / context / input | SDL2 (Bookworm apt `libsdl2-dev`), `kmsdrm` video driver | Direct KMS page-flip, so Vsync is reliable. Mouse and keyboard via evdev. |
| Graphics API | OpenGL ES 3.1 (`#version 300 es` shaders) | Most mature path on Mesa V3D. Desktop dev build uses GL 3.3 core with a shader header swap. |
| Math | glm (vendored) | |
| UI | Dear ImGui (SDL2 + OpenGL3 backends) | Replaces lil-gui. Costs nothing when hidden. |
| WebP decode | libwebp (`WebPDecode` with `use_scaling` to decode straight to the target size) | Downscaling during decode saves CPU and RAM |
| PNG/JPG (textures, local folders) | stb_image (vendored) | |
| HTTP | libcurl (multi handle, keep-alive, HTTP conditional GET) | |
| JSON | nlohmann/json (apt `nlohmann-json3-dev`) | |
| Build | CMake + Ninja, built natively on the Pi or on a desktop Linux machine for development | |
| Deploy | systemd unit + install script | |

## 5. Architecture

```
          ┌──────────────────── main thread (owns GL context) ────────────────────┐
SDL events → Input → Settings(ImGui) → Mode::update(dt) → Renderer → swap (vsync)
                         │                      ▲
                         │                 TextureStreamer.uploadBudget()
                         ▼                      │ (decoded RGBA, ≤N bytes/frame)
                    ConfigStore           ┌─────┴─────────┐
                                          │ ready queue   │ (lock-free SPSC/MPSC)
                                          └─────▲─────────┘
                       worker pool (3 threads): fetch (HTTP/file/cache) → decode+scale → push
```

### 5.1 Modules
- `config/ConfigStore` — v2.2 schema covering `globals`, `modes.{floating,tunnel,grid,maze}`, `activeMode`, and per-mode `stateKeys` (minus the stereo keys). Unknown keys are preserved on save so the web version and the Gradio GUI keep working. New Pi-only keys live under `globals.pi` (the web version ignores them).
- `assets/ImageSource` — interface with `HttpSource` (base URL + `serverPath` + quality folder) and `FileSource` (local root or arbitrary folder).
- `assets/DiskCache` — `~/.cache/imagetunnel/<host>/<serverPath>/<quality>/N.webp` with stored ETag. Revalidated with `If-None-Match` (nginx sends ETags by default). **Important:** the pipeline renumbers files when it compacts gaps, so `N.webp` can change content. That's why revalidation is required; just checking whether the file exists isn't enough. If the server is unreachable, the cache is used as-is and the image list comes from the cached `config.json`.
- `assets/TextureStreamer` — port of `TextureManager` (`masterList`, shuffled `playDeck`, `refillDeck`, buffer with `maxBufferSize`, `preloadTarget`). Changes from the web version:
  - **Refcounted cache keyed by image index.** Several tiles can share one GPU texture, which fixes the grid memory blow-up. Least-recently-used textures with zero refs are evicted when over budget.
  - Each decode records `hasAlpha` (`WebPGetFeatures`), so opaque images are drawn as opaque.
  - Max texture edge is set per mode: tunnel/grid 512, floating/maze 1024. Decode-time scaling plus `glGenerateMipmap`.
  - Upload budget per frame is measured in **bytes** (default 6 MB), not texture count. `maxUploadsPerFrame` stays as a secondary cap.
  - Requests are prioritized by distance to the camera. The queue is re-sorted only when it changes, not every frame.
- `render/` — thin GL wrapper: `Shader`, `Mesh` (VAO/VBO/IBO), `Texture`, `Fbo`, `Camera`, `Fog`. Render order: opaque front-to-back, then blended back-to-front.
- `modes/` — `FloatingMode`, `TunnelMode`, `GridMode`, `MazeMode`, all behind `IMode { enter, exit, update, render, onConfigChanged }`. Switching modes saves and restores `stateKeys` per mode, matching `handleModeChange()`.
- `ui/SettingsPanel` (ImGui mirror of the lil-gui folders, showing only the controls relevant to the current mode, like `updateGUIVisibility()`), `ui/StatsOverlay`, `ui/LoadingOverlay`, `ui/MazeMinimap`.

### 5.2 Shaders (3 programs total)
1. **card.glsl** — textured quad or cylinder segment with a UV transform (`mat3`), ambient light, up to 4 point lights (chosen per draw on the CPU from the moving-light pool by distance), exp2 fog, opacity, optional alpha-test.
2. **maze.glsl** — albedo (+ optional normal map), Blinn-Phong with ambient + ≤4 spotlights + exit point light, exp2 fog, optional single shadow map.
3. **unlit.glsl** — light-orb spheres, spotlight housings and lenses, exit crystal (wireframe via `GL_LINES`), minimap, fades.

## 6. Mode-by-mode port and optimizations

### Floating
- One shared unit quad VBO. Per card: model matrix + texture + opacity uniforms. About 100–150 draws, sorted by texture.
- Camera wobble and mouse parallax ported unchanged (`wobbleStrength`, `wobbleSpeed`, parallax 0.05).
- Recycle at `z > 50`, same as the web version, but the texture ref is released rather than disposed.
- Pi cap: `totalImages ≤ 150` (default 100).

### Tunnel
- **One** shared cylinder-segment mesh (16 segments, open-ended, built like `setupTunnelGrid()`). Per tile: `rotationZ`, `z`, and a UV transform matrix computed once with the same math as `updateTileUVs()` (rotation 0/90/180/270, flip on the far side, aspect fit). No geometry cloning.
- The wrap-around ring keeps the same `totalDepth` recycling. Recycled tiles keep the same column and get a new texture.
- Pi cap: `tunnelRows ≤ 20` (the saved desktop value is 15). Rows beyond the fog distance are skipped.

### Grid
- Layers of quads, recycled by moving the layer back, with the same target-picking logic (`pickNextTarget`, `pathRandomness`, `gridEdgeBuffer`).
- Textures are shared through the refcount cache, so a layer can reuse images already loaded.
- Pi caps: `gridCols, gridRows ≤ 12`, max texture edge 256–512, and frustum culling per tile.

### Maze
- **Generation:** port `MazeGenerator` (recursive-backtracker DFS, odd size, `getWallFaces`) and the start/furthest-exit logic exactly.
- **Geometry:** one static merged mesh of *only the wall faces that border open cells*, replacing the InstancedMesh of full boxes. That's roughly 4× fewer triangles and no hidden faces. Floor and ceiling are single quads sized to the maze bounds instead of 2000×2000, with the same tiling formula as `MaterialManager.updateTiling()`.
- **Materials:** textures from `textures/<Name>/<Name>_{Color,NormalGL}.{jpg,png}` (Roughness is ignored), fetched through the same `ImageSource` and cached. Normal maps are on by default, with a toggle.
- **Paintings:**
  - Frames are a single **instanced** draw: unit box plus a baked dark backing plate and AO gasket, scaled per instance to `pW + frameWidth` by `pH + frameWidth`.
  - Image planes are one draw per *visible* painting.
  - Paintings spawn in true camera-distance order, nearest first.
- **Visibility:** each time the camera changes cell, build a set of visible cells with a grid DDA line-of-sight walk from the camera cell, limited to 120 units for paintings and 200 for the exit. Only those paintings are drawn and lit. This replaces both per-frame raycasts.
- **Spotlights:** pool of 4. They're assigned to the nearest visible paintings using the same positioning as the web version (`normalDir * 2`, `mazeSpotlightHeight`). Light changes fade over about 0.2 s to avoid popping. Housing meshes are only drawn for active lights.
- **Shadows:** `globals.pi.mazeShadows: "off" | "nearest"`. `nearest` gives one 512² depth map for the closest spotlight.
- **Exit portal:** wireframe octahedron + inner solid + unshadowed point light. Spin and bob are ported.
- **Navigation:** port the `mazeNav` state machine (`moving`, `turning`, `idle`), `TURN_MAP`, the random straight/left/right choice with back only at dead ends, smoothstep easing, head-bob (win95), and modern mouse-look.
- **Regeneration:** fade to black (0.4 s), rebuild the maze (generation is under 5 ms; mesh build under 10 ms), fade back in. Optional `flip` style renders the last frame to an FBO and rotates the quad like the CSS effect.
- **Minimap:** the maze grid is uploaded once as an R8 texture and drawn as a 200×200 quad with an exit marker and a player arrow in a single unlit pass. Hidden when `showMazeMap` is off.
- Pi caps: `mazeComplexity ≤ 31`, `mazeImageCount ≤ 120`, paintings at a max edge of 1024.

## 7. Performance strategy

### 7.1 Frame budget (30 fps = 33.3 ms; design target ≤ 25 ms GPU, ≤ 10 ms main-thread CPU)
- **Vsync:** `SDL_GL_SetSwapInterval(1)` with a 60 Hz mode. New option `globals.pi.frameCap: 60|30`. At 30, try swap interval 2. If kmsdrm rejects that, pick a 1080p30 display mode, or pace by sleeping to the next vblank. Locking to 30 avoids judder from flipping between 60 and 30.
- **Resolution:** native 1080p. `globals.pi.renderScale` (1.0 / 0.85 / 0.75) renders the 3D scene to an FBO and upscales with a bilinear blit, while UI stays at native resolution. This is the main fallback lever.
- **MSAA:** off by default. 4× is an option (V3D is tile-based, so measure it rather than guess).
- **Overdraw:** opaque cards write depth and are drawn front to back. Blending is only used when the image has alpha (e.g. `transparentimages`). Fog lets the far plane drop from 3000 to the distance where fog reaches about 99%.
- **Draw calls:** under 300 per frame in every mode, achieved through shared meshes, instanced frames, and visibility culling.
- **CPU:**
  - No allocation per frame, so object pools for cards, tasks and lights.
  - Decode runs on 3 worker threads, one core left for the main thread.
  - GL uploads only on the main thread, with the bytes-per-frame budget.
- **Memory budget:** about 600 MB of GPU textures, enforced by the streamer's LRU (1024² RGBA + mips ≈ 5.6 MB each; 512² ≈ 1.4 MB).

### 7.2 Pi preset (`tunnel_config_potato.json` + `globals.pi`)
Add the missing `modes.maze` section to the potato config and the new `globals.pi` block:
```json
"pi": { "frameCap": 60, "renderScale": 1.0, "msaa": 0, "maxTextureEdge": 1024,
        "uploadBytesPerFrame": 6291456, "mazeShadows": "off", "maxLights": 4,
        "normalMaps": true, "regenTransition": "fade" }
```

### 7.3 Auto-quality (optional, milestone 8)
If p95 frame time stays above 30 ms for 5 s, step down one level: renderScale → normal maps → maxLights → texture edge. Shown in the stats overlay. Off by default.

## 8. Config & persistence
Load order:
1. Built-in defaults.
2. Server or local `tunnel_config_potato.json`. The path or URL comes from the CLI flag `--config`, default `http://<server>/tunnel_config_potato.json`, and is cached for offline use.
3. Local override `~/.config/imagetunnel/config.json`, if `version == 2.2`. This replaces `localStorage`.
4. `availableTextures` is always taken from the server or base config. This is the behaviour the web version intended (see bug #2).

"💾 Save Config" writes the local override. "Reload from server" (key `R`) discards the local override, which also picks up edits from the Gradio Settings tab. The `serverPath` dropdown options come from the config instead of a hard-coded list.

## 9. Kiosk deployment
- `/etc/systemd/system/imagetunnel.service`: `User=pi`, `Environment=SDL_VIDEODRIVER=kmsdrm`, `Restart=always`, `After=network-online.target`. The user must be in the `video`, `render` and `input` groups.
- Boot to console (Lite or `raspi-config` → console autologin off). The app takes DRM master.
- Mouse cursor is hidden unless the settings panel is open. `Esc` quits (systemd restarts it), `Ctrl+Q` quits without restart via an exit code that systemd's `RestartPreventExitStatus` respects.
- `install.sh`: apt dependencies, CMake build, install the binary to `/opt/imagetunnel`, enable the service.
- Cooling: an active cooler is recommended. The stats overlay shows temperature and throttle state from `vcgencmd get_throttled` or `/sys/class/thermal`, because throttling is the most likely cause of missing 30 fps.

## 10. Source layout
Originally planned to live outside the nginx web root. It now lives in `pi-native/` in the Void-Gallery repo, alongside the website. It's only source code, so being served on the LAN exposes nothing sensitive.
```
pi-native/
  CMakeLists.txt
  third_party/ (imgui, glm, stb)
  shaders/ card.glsl maze.glsl unlit.glsl
  src/
    main.cpp  App.{h,cpp}
    config/ConfigStore.*
    assets/ImageSource.* HttpSource.* FileSource.* DiskCache.* Decoder.* TextureStreamer.*
    render/Gl.* Shader.* Mesh.* Texture.* Fbo.* Camera.*
    modes/IMode.h FloatingMode.* TunnelMode.* GridMode.* MazeMode.* MazeGen.* MazeNav.* MazeVisibility.*
    ui/SettingsPanel.* StatsOverlay.* LoadingOverlay.* MazeMinimap.*
  deploy/imagetunnel.service install.sh
  tools/bench.sh
```

## 11. Milestones
| # | Deliverable | Exit criteria |
|---|---|---|
| M0 | Skeleton: SDL kmsdrm + GLES 3.1 context, Vsync, ImGui, stats overlay, desktop GL dev build | Blank scene at 60 fps on the Pi; builds on desktop Linux |
| M1 | ConfigStore, HttpSource/FileSource/DiskCache, decoder pool, TextureStreamer with upload budget | Streams 1,000 images in a loop with no leaks (RSS stable over 30 min) |
| M2 | Floating mode | ≥30 fps p99 with 150 cards and 4 lights |
| M3 | Tunnel mode (shared mesh + UV transforms) | Visually matches the web version at all 4 rotations; ≥30 fps p99 with 20 rows |
| M4 | Grid mode with shared textures | ≥30 fps p99 at 12×12; texture memory within budget |
| M5 | Maze: gen, merged walls, materials, nav, paintings, visibility, spotlights, exit, minimap, regen fade | ≥30 fps p99 at size 31 with 120 paintings and normal maps on |
| M6 | Settings panel parity, save/reload, mode memory | Every non-stereo lil-gui control has an equivalent (§12) |
| M7 | Kiosk deploy, systemd, thermal overlay, Pi preset tuning | Cold boot to running app in under 20 s; 8-hour soak test with no leaks or crashes |
| M8 (opt) | Auto-quality, nearest-spotlight shadows, flip transition, texture arrays for paintings | Each stays within budget |

## 12. Feature parity checklist
- [ ] Modes: floating / tunnel / grid / maze, switchable at runtime with per-mode memory (`stateKeys`)
- [ ] General: Show Performance, Save Config, Speed, Fog Density
- [ ] Geometry: Tunnel/Float Radius, Image Size, Tunnel Depth, Grid Width/Height/Spacing, Path Randomness, Edge Buffer, Float Count
- [ ] Images: Resolution (full/half/quarter), Anisotropy (≤4), Image Rotation, Spin Speed, Stop Rotation, Opacity
- [ ] Server/Local: Image Folder, Start #/End # range, Reload Range. Local folder source replaces Upload and Clear Custom.
- [ ] Maze: Show Minimap, Maze Size, Image Count, Spotlight Size, Spotlight Height, Shadow mode (replaces Shadow Resolution), Walking Speed, Texture Tiling, Nav Style, Wall/Floor/Ceiling texture
- [ ] Lighting: lightSpeed, Ambient Brightness, Textures/Frame, lightCount (≤8), lightIntensity (applied live, no rebuild), lightColorMode
- [ ] Pi-only: frameCap, renderScale, MSAA, max texture edge, normal maps, regen transition, auto-quality
- [ ] Loading overlay with preload progress (`preloadTarget`)
- [ ] Reads `config.json` (`totalImages`) per folder; `manifest.json` read for display names in the stats overlay

## 13. Testing & benchmarking
- `--bench <mode> --seconds 120` logs average, p95 and p99 frame time, missed vsyncs, peak texture MB and throttle flags to CSV. `tools/bench.sh` runs all four modes with the Pi preset. **Acceptance: p99 ≤ 33.3 ms in every mode on a Pi 5 with an active cooler.**
- Unit tests (desktop): maze generator (always connected, odd sizes), nav never gets stuck, DDA visibility against brute-force raycast, config round-trip that preserves unknown keys, TunnelMode UV math against values captured from `updateTileUVs()`.
- Soak test: 8 hours cycling modes every 10 minutes, with RSS and GPU memory logged.
- Offline test: unplug the network mid-run; the app keeps running from the cache.

## 14. Suggested additions (optional)
- Fix web bugs #1–#6 in `index.html` and add `modes.maze` to `tunnel_config_potato.json` now. This helps current Pi browser users too.
- `process_assets.py`: also write `dims.json` (width, height, hasAlpha per image) so layouts can be computed before decode, and optionally a 1024-edge `pi/` variant.
- Gradio GUI: a "Pi preset" editor for `globals.pi`.
- Crossfade when a recycled tunnel tile changes images (cheap, and hides texture pop-in).
