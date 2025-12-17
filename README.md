# DriftSpace 🌌

DriftSpace is an immersive, high-performance WebGL image gallery built with Three.js. It transforms standard image directories into mesmerizing 3D environments, featuring infinite tunnels, floating chaos, and spiral grids. 

The project includes a robust Python asset pipeline that optimizes, resizes, and sequences large image collections for web delivery.

## ✨ Features

### Frontend (WebGL)
* **Three Unique Display Modes:**
    * **Floating:** Images float in a chaotic, zero-gravity void with wobble effects.
    * **Tunnel:** An infinite, cylindrical tunnel of images with adjustable curvature and speed.
    * **Grid:** A multi-layered, spiraling grid system that moves toward the camera.
* **Performance Optimized:** * Uses shared geometry instancing to reduce draw calls.
    * Implements texture upload throttling to prevent frame drops during loading.
    * Smart resource recycling (infinite scrolling illusion).
* **Interactive:**
    * **Drag & Drop:** Drop local images directly into the browser to view them instantly.
    * **Live Settings:** Press `H` to toggle a GUI for tweaking fog, speed, lighting, and geometry in real-time.
    * **Mouse Parallax:** Scene reacts subtly to cursor movement for added depth.

### Backend (Python Pipeline)
* **Automated Optimization:** Converts images to high-efficiency `.webp` format.
* **Mipmap Generation:** Automatically generates half-resolution and quarter-resolution copies for performance scaling.
* **Smart Sequencing:** Renames files to sequential integers (`1.webp`, `2.webp`) to allow simple looping logic in the frontend.
* **Manifest Tracking:** Generates a `manifest.json` to map the new numbered filenames back to their original source names.
* **Multithreaded:** Uses `multiprocessing` to process thousands of images in parallel.

---

## 🚀 Installation & Setup

### 1. Prerequisites
* **Python 3.x** (for the asset pipeline).
* **Web Server:** Because this project uses ES Modules (`import * as THREE`), you cannot run it by double-clicking `index.html`. You must use a local server (e.g., VS Code Live Server, Python `http.server`, or Node.js).

### 2. Python Environment
Install the required dependencies for the asset processor:

```bash
pip install tqdm Pillow