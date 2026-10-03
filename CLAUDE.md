# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Project Is

A Three.js 3D image viewer served by nginx, with a Python/Gradio management GUI. The frontend (`index.html`) renders images in one of three modes (floating, tunnel, grid) by fetching numbered `.webp` sequences from the server. The Python side manages the asset pipeline that converts, resizes, and sequences those images.

## Running the GUI

```bat
rungui.bat         # Windows: launches Gradio server at http://127.0.0.1:7860
python gui.py      # Directly via Python
```

Dependencies: `pip install gradio Pillow`

## Running the Asset Pipeline Directly

```bash
python process_assets.py
```

## Architecture

### Frontend (`index.html`)
- Pure ES module JavaScript using Three.js (`three@0.160.0`) and `lil-gui`
- Four render modes selectable at runtime: **floating** (images drift through space), **tunnel** (cylindrical corridor), **grid** (flat grid flythrough), **maze** (procedurally generated first-person maze walkthrough)
- On load, fetches `tunnel_config.json` (or `tunnel_config_potato.json` on Raspberry Pi, detected via user agent) to restore saved settings, then applies any `localStorage` overrides on top
- Images are fetched as numbered sequences: `{serverPath}/{quality}/{N}.webp` where quality is `full`, `halfres`, or `quarterres`
- Each image directory exposes a `config.json` (contains `totalImages`) and `manifest.json` (maps numbered filename → original name)
- Settings are saved to `localStorage` (key: `imageTunnelConfig`) via the "💾 Save Config" button — no file download or manual server replacement required. Server-side `tunnel_config.json` remains the baseline; localStorage overrides it on load if versions match (2.2).

### Asset Pipeline (`process_assets.py`)
- `TARGET_DIRS = ["movieposters", "transparentimages", "images", "AIimages"]` — the four managed image directories
- Each directory gets `halfres/` and `quarterres/` subdirectories auto-created
- Converts PNG/JPG/JPEG/WebP → `.webp` lossless (full), quality 85 (half), quality 80 (quarter)
- After conversion, renumbers all images as a gapless integer sequence (`1.webp`, `2.webp`, …). Deleting images leaves gaps; the next pipeline run compacts the sequence.
- Writes `config.json` (totalImages count) and `manifest.json` (rename tracking) per directory
- Uses `multiprocessing.Pool` for parallel conversion
- `run_pipeline_stream()` is the generator API used by `gui.py`

### Management GUI (`gui.py`)
- Gradio app with three tabs: **Ingestion & Pipeline** (upload files, run pipeline), **Gallery** (browse/delete processed images), **Manage Global Settings** (edit `tunnel_config.json` in-browser)
- `save_uploaded_files()` copies dragged files into a selected target directory
- `trigger_pipeline()` calls `run_pipeline_stream()` and streams terminal output to the UI
- The Settings tab reads/writes `tunnel_config.json` directly on disk (unlike the browser download approach)

### Maze Mode (`index.html`)
- Fourth render mode: procedurally generated grid maze the camera navigates automatically
- `mazeGen` generates the grid; walls are rendered as `InstancedMesh` chunks (`mazeChunks`)
- Images from the active server directory are hung as paintings on wall faces (`mazePaintings`), lit by a pool of `SpotLight`s (`mazeSpotlights`) that are assigned per-frame based on proximity and line-of-sight raycasting
- An exit portal (`mazeExitObject`: group + spinning mesh + point light) marks the maze end; its visibility is also raycasted each frame inside `updateSpotlightAssignments()`
- A 2D mini-map is rendered to `<canvas id="maze-map">` and hidden in stereo mode
- Navigation (`mazeNav`) phases: `idle → moving → idle`, with configurable `navStyle` ('win95' or others)
- Key config fields: `mazeComplexity`, `mazeWalkingSpeed`, `mazeTextureTiling`, `mazeImageCount`, `mazeSpotlightAngle`, `mazeSpotlightHeight`, `mazeShadowRes`, `mazeWallTexture`, `mazeFloorTexture`, `mazeCeilingTexture`, `showMazeMap`
- Available textures list (`availableTextures`) is stored in `globals` of the config and driven by what's in the `textures/` directory

### Configuration (`tunnel_config.json`)
- Version 2.2 format with `globals` (serverPath, quality, serverStart/End, showStats, useStereo, availableTextures) and per-mode settings under `modes.{floating,tunnel,grid,maze}`
- `tunnel_config_potato.json` is a lower-quality preset for Raspberry Pi clients
- **Save flow**: "💾 Save Config" writes to `localStorage` (`imageTunnelConfig`). On next load, server JSON is fetched first, then localStorage overrides it if `version === 2.2`. The Gradio Settings tab still edits `tunnel_config.json` on disk directly (affects all clients without a localStorage override).
