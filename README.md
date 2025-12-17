# DriftSpace 🌌

DriftSpace is an immersive, high-performance WebGL image gallery built with Three.js. It transforms standard image directories into mesmerizing 3D environments, featuring infinite tunnels, floating chaos, and grid-based galleries.

The project includes a robust Python asset pipeline that optimizes, resizes, and sequences large image collections for web delivery.

---

## ✨ Features

### Frontend (WebGL)

#### Three Unique Display Modes:
1. **Floating**: Images float in a chaotic, zero-gravity void with wobble effects.
2. **Tunnel**: An infinite, cylindrical tunnel of images with adjustable curvature and speed.
3. **Grid**: A multi-layered, spiraling grid system that moves toward the camera.

#### Performance Optimized:
- Uses shared geometry instancing to reduce draw calls.
- Implements texture upload throttling to prevent frame drops during loading.
- Smart resource recycling (infinite scrolling illusion).

#### Interactive:
- **Drag & Drop**: Drop local images directly into the browser to view them instantly.
- **Live Settings**: Press `H` to toggle a GUI for tweaking fog, speed, lighting, and geometry in real-time.
- **Mouse Parallax**: Scene reacts subtly to cursor movement for added depth.

---

### Backend (Python Pipeline)

#### Automated Optimization:
- Converts images to high-efficiency `.webp` format.
- Mipmap generation: Automatically generates half-resolution and quarter-resolution copies for performance scaling.

#### Smart Sequencing:
- Renames files to sequential integers (e.g., `1.webp`, `2.webp`) to allow simple looping logic in the frontend.
- Generates a `manifest.json` to map the new numbered filenames back to their original source names.

#### Multithreaded:
- Uses multiprocessing to process thousands of images in parallel.

---

## 🚀 Installation & Setup

### 1. Prerequisites
1. **Python 3.x**: Required for the asset pipeline.
2. **Web Server**: Since the project uses ES Modules (e.g., `import * as THREE`), you cannot run it directly by opening `index.html`. Use a local server, such as:
   - VS Code Live Server
   - Python `http.server`
   - Node.js HTTP server

---

### 2. Python Environment
Install the required dependencies for the asset processor:

```bash
pip install tqdm Pillow
```

---

### 3. Directory Structure
Ensure your project folder follows this structure:

```plaintext
/
├── index.html
├── process_assets.py
├── transparentimages/      <-- Source images go here
├── movieposters/           <-- Or here
└── images/                 <-- Or here
```

---

## 🛠 Usage

### Step 1: Process Assets
Place your raw images (`.jpg`, `.png`, `.jpeg`) into one of the target directories (e.g., `transparentimages`). Then run the script:

```bash
python process_assets.py
```

The script will:
- Convert images to `.webp`.
- Create `halfres/` and `quarterres/` subfolders.
- Rename files to `1.webp`, `2.webp`, etc.
- Update `config.json` with the total image count.

---

### Step 2: Run the Frontend
Start your local web server and open `index.html`.

- **Default Behavior**: The app attempts to load images from the folder defined in `config.serverPath` (default: `transparentimages`).
- **Quality Settings**: Switch between Full, Half, and Quarter resolution in the settings menu.

---

## ⚙️ Configuration

### In-App Controls (GUI)
Press `H` to open the settings panel. Key controls include:
- **Mode**: Switch between Floating, Tunnel, and Grid.
- **Texture Throttle**: Adjust textures per frame to balance loading speed and frame rate.
- **Geometry**: Modify tunnel radius, image size, and grid spacing.
- **Lighting**: Adjust ambient intensity and moving point lights.
- **Save Config**: Downloads a `tunnel_config.json` file with your current presets.

---

### Backend Configuration
Modify the top of `process_assets.py` to change target folders:

```python
TARGET_DIRS = ["movieposters", "transparentimages", "images"]
TARGET_EXT = ".webp"
```

---

## 📂 Project Architecture

### TextureManager:
- Handles the loading buffer, shuffles the "deck" of images, and manages WebGL texture uploads.

---

### Geometry Recycling:
- **Floating/Grid**: Uses shared 1x1 plane geometry, scaling it per mesh to reduce memory overhead.
- **Tunnel**: Uses a custom cylinder geometry with UV mapping logic to curve images around the player.

---

### Smart Caching:
- The Python script checks file modification times (`mtime`) to avoid re-processing unchanged images.

---

## 📄 License

MIT License. Free to use and modify.