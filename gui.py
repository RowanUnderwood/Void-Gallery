import gradio as gr
import os
import json
import math
import time
import shutil
from process_assets import run_pipeline_stream, TARGET_DIRS

TUNNEL_CONFIG_PATH = "tunnel_config.json"
MODES = ['floating', 'tunnel', 'grid', 'maze']

# ── Defaults matching the JS modeStore / config object ─────────────────────────
_MAZE_DEFAULTS = {
    'showMazeMap':         True,
    'mazeComplexity':      21,
    'mazeWalkingSpeed':    1.0,
    'mazeTextureTiling':   2.0,
    'mazeImageCount':      55,
    'mazeSpotlightAngle':  math.pi / 5,
    'mazeSpotlightHeight': 6.0,
    'mazeShadowRes':       1024,
    'navStyle':            'win95',
    'mazeWallTexture':     'Bricks003_1K-JPG',
    'mazeFloorTexture':    'WoodFloor071_1K-PNG',
    'mazeCeilingTexture':  'OfficeCeiling001_1K-PNG',
}

MODE_DEFAULTS = {
    'floating': dict(
        stereoEyeSep=0.5,  stereoFOVBoost=30,  cameraSpeed=1.0,  fogDensity=0.0035,
        tunnelRadius=85.0, imageSize=60.0,      tunnelRows=5,     totalImages=20,
        rotationSpeed=0.1, noRotation=False,    tunnelRotation=0, lightCount=15,
        ambientIntensity=0.5, gridCols=6,       gridRows=4,       gridSpacing=400,
        pathRandomness=0.3,   gridEdgeBuffer=2, lightSpeed=1.0,   lightIntensity=1500,
        lightColorMode='white', maxUploadsPerFrame=2, anisotropyLevel=1, **_MAZE_DEFAULTS,
    ),
    'tunnel': dict(
        stereoEyeSep=0.1,  stereoFOVBoost=50,  cameraSpeed=1.0,   fogDensity=0.0025,
        tunnelRadius=85.0, imageSize=60.0,      tunnelRows=10,     totalImages=10,
        rotationSpeed=0.1, noRotation=True,     tunnelRotation=270, lightCount=2,
        ambientIntensity=0.2, gridCols=6,       gridRows=4,        gridSpacing=400,
        pathRandomness=0.3,   gridEdgeBuffer=2, lightSpeed=5.0,    lightIntensity=800,
        lightColorMode='random', maxUploadsPerFrame=1, anisotropyLevel=4, **_MAZE_DEFAULTS,
    ),
    'grid': dict(
        stereoEyeSep=0.8,  stereoFOVBoost=30,  cameraSpeed=1.5,  fogDensity=0.0015,
        tunnelRadius=85.0, imageSize=50.0,      tunnelRows=5,     totalImages=20,
        rotationSpeed=0.0, noRotation=True,     tunnelRotation=0, lightCount=5,
        ambientIntensity=0.5, gridCols=8,       gridRows=6,       gridSpacing=300,
        pathRandomness=0.5,   gridEdgeBuffer=2, lightSpeed=2.0,   lightIntensity=1500,
        lightColorMode='white', maxUploadsPerFrame=4, anisotropyLevel=4, **_MAZE_DEFAULTS,
    ),
    'maze': dict(
        stereoEyeSep=0.5,  stereoFOVBoost=30,  cameraSpeed=1.0,  fogDensity=0.012,
        tunnelRadius=85.0, imageSize=40.0,      tunnelRows=5,     totalImages=20,
        rotationSpeed=0.0, noRotation=True,     tunnelRotation=0, lightCount=0,
        ambientIntensity=0.08, gridCols=6,      gridRows=4,       gridSpacing=400,
        pathRandomness=0.3,    gridEdgeBuffer=2, lightSpeed=1.0,  lightIntensity=800,
        lightColorMode='white', maxUploadsPerFrame=1, anisotropyLevel=4, **_MAZE_DEFAULTS,
    ),
}

# Fields that must round-trip as integers
INT_FIELDS = {
    'tunnelRows', 'totalImages', 'lightCount', 'maxUploadsPerFrame',
    'gridCols', 'gridRows', 'gridEdgeBuffer', 'anisotropyLevel',
    'tunnelRotation', 'mazeShadowRes', 'mazeComplexity', 'mazeImageCount',
    'serverStart', 'serverEnd', 'stereoFOVBoost', 'gridSpacing',
}

# ── UI field schema ─────────────────────────────────────────────────────────────
GLOBAL_FIELDS = [
    # (key, label, type, choices_or_none)
    ('activeMode',  'Active Mode',    'dropdown', MODES),
    ('serverPath',  'Server Path',    'dropdown', ['transparentimages', 'movieposters', 'images', 'AIimages']),
    ('quality',     'Quality',        'dropdown', ['full', 'half', 'quarter']),
    ('serverStart', 'Server Start #', 'number',   None),
    ('serverEnd',   'Server End #',   'number',   None),
    ('showStats',   'Show Stats',     'checkbox',  None),
    ('useStereo',   '3D SBS Mode',    'checkbox',  None),
]

GLOBAL_DEFAULTS = {
    'activeMode': 'maze',  'serverPath': 'transparentimages',
    'quality':    'full',  'serverStart': 1, 'serverEnd': 1075,
    'showStats':  False,   'useStereo': False,
}

# (folder_name, [(key, label, type, opts)])
# slider opts: (min, max, step) | radio/dropdown opts: [choices] | None for checkbox/dropdown_texture
MODE_FOLDERS = [
    ('General', [
        ('cameraSpeed',    'Camera Speed',        'slider',   (0.1, 4.0,        0.01)),
        ('fogDensity',     'Fog Density',          'slider',   (0.0, 0.02,       0.0001)),
        ('stereoEyeSep',   '3D Eye Separation',    'slider',   (0.0, 2.0,        0.01)),
        ('stereoFOVBoost', 'SBS FOV Widen',        'slider',   (0,   80,         1)),
    ]),
    ('Geometry', [
        ('imageSize',      'Image Size',           'slider',   (10,  100,        0.1)),
        ('tunnelRadius',   'Tunnel/Float Radius',  'slider',   (10,  200,        1)),
        ('tunnelRows',     'Tunnel Depth',         'slider',   (1,   50,         1)),
        ('totalImages',    'Total Images',         'slider',   (1,   600,        1)),
        ('gridCols',       'Grid Width',           'slider',   (2,   40,         1)),
        ('gridRows',       'Grid Height',          'slider',   (2,   40,         1)),
        ('gridSpacing',    'Grid Spacing',         'slider',   (100, 1000,       10)),
        ('pathRandomness', 'Path Randomness',      'slider',   (0.0, 1.0,        0.01)),
        ('gridEdgeBuffer', 'Edge Buffer',          'slider',   (0,   10,         1)),
    ]),
    ('Images & Rotation', [
        ('anisotropyLevel', 'Anisotropy Level',   'slider',   (1,   16,         1)),
        ('tunnelRotation',  'Image Rotation',     'dropdown', [0, 90, 180, 270]),
        ('rotationSpeed',   'Spin Speed',         'slider',   (0.0, 5.0,        0.01)),
        ('noRotation',      'Stop Rotation',      'checkbox', None),
    ]),
    ('Lighting & Performance', [
        ('lightSpeed',         'Light Speed',         'slider',  (0,   30,         0.1)),
        ('ambientIntensity',   'Ambient Brightness',  'slider',  (0.0, 3.0,        0.01)),
        ('lightCount',         'Light Count',         'slider',  (0,   20,         1)),
        ('lightIntensity',     'Light Intensity',     'slider',  (0,   3000,       1)),
        ('lightColorMode',     'Light Color Mode',    'radio',   ['random', 'white']),
        ('maxUploadsPerFrame', 'Textures / Frame',    'slider',  (1,   20,         1)),
    ]),
    ('Maze', [
        ('showMazeMap',         'Show Minimap',              'checkbox',         None),
        ('mazeComplexity',      'Maze Complexity (odd)',      'slider',           (11,  41,           2)),
        ('mazeImageCount',      'Image Count',               'slider',           (5,   200,          1)),
        ('mazeWalkingSpeed',    'Walking Speed',             'slider',           (0.2, 5.0,          0.01)),
        ('mazeTextureTiling',   'Texture Tiling',            'slider',           (0.5, 8.0,          0.01)),
        ('mazeSpotlightAngle',  'Spotlight Angle (radians)', 'slider',           (0.1, math.pi / 2,  0.01)),
        ('mazeSpotlightHeight', 'Spotlight Height',          'slider',           (2.0, 15.0,         0.1)),
        ('mazeShadowRes',       'Shadow Resolution',         'dropdown',         [256, 512, 1024, 2048, 4096, 8192]),
        ('navStyle',            'Nav Style',                 'radio',            ['win95', 'modern']),
        ('mazeWallTexture',     'Wall Texture',              'dropdown_texture', None),
        ('mazeFloorTexture',    'Floor Texture',             'dropdown_texture', None),
        ('mazeCeilingTexture',  'Ceiling Texture',           'dropdown_texture', None),
    ]),
]

# Component registries — populated during gr.Blocks() layout construction
_C_globals = {}                        # key → component
_C_modes   = {m: {} for m in MODES}   # mode → key → component


# ── Data helpers ────────────────────────────────────────────────────────────────

def _read_config():
    if os.path.exists(TUNNEL_CONFIG_PATH):
        with open(TUNNEL_CONFIG_PATH) as f:
            return json.load(f)
    return {'version': 2.2, 'savedAt': 0, 'activeMode': 'maze', 'globals': {}, 'modes': {}}


def _coerce(key, val):
    """Cast val to the type expected by the JS frontend."""
    if val is None:
        return val
    if key in INT_FIELDS:
        try:
            return int(round(float(str(val))))
        except (ValueError, TypeError):
            return val
    return val


def _make_mode_component(key, label, ctype, opts):
    """Instantiate one Gradio component from schema; must be called inside gr.Blocks()."""
    if ctype == 'slider':
        return gr.Slider(minimum=opts[0], maximum=opts[1], step=opts[2], label=label)
    if ctype == 'checkbox':
        return gr.Checkbox(label=label)
    if ctype == 'radio':
        return gr.Radio(choices=opts, label=label)
    if ctype in ('dropdown', 'dropdown_texture'):
        choices = opts if isinstance(opts, list) else []
        return gr.Dropdown(choices=choices, label=label)
    raise ValueError(f"Unknown component type: {ctype}")


def _all_form_components():
    """Ordered list of all components; must match load_settings_form() return order."""
    comps = [_C_globals[key] for key, *_ in GLOBAL_FIELDS]
    for mode in MODES:
        for _, fields in MODE_FOLDERS:
            for key, *_ in fields:
                comps.append(_C_modes[mode][key])
    return comps


# ── Settings form I/O ───────────────────────────────────────────────────────────

def load_settings_form():
    """Returns an ordered list of values (+ gr.update for texture dropdowns + JSON string)."""
    cfg = _read_config()
    glb = cfg.get('globals', {})
    available_textures = glb.get('availableTextures', [])
    result = []

    # Globals (order matches GLOBAL_FIELDS)
    result.append(cfg.get('activeMode',        GLOBAL_DEFAULTS['activeMode']))
    result.append(glb.get('serverPath',        GLOBAL_DEFAULTS['serverPath']))
    result.append(glb.get('quality',           GLOBAL_DEFAULTS['quality']))
    result.append(glb.get('serverStart',       GLOBAL_DEFAULTS['serverStart']))
    result.append(glb.get('serverEnd',         GLOBAL_DEFAULTS['serverEnd']))
    result.append(glb.get('showStats',         GLOBAL_DEFAULTS['showStats']))
    result.append(glb.get('useStereo',         GLOBAL_DEFAULTS['useStereo']))

    # Per-mode fields (order matches MODES × MODE_FOLDERS)
    modes_data = cfg.get('modes', {})
    for mode in MODES:
        mode_data = modes_data.get(mode, {})
        defs = MODE_DEFAULTS[mode]
        for _, fields in MODE_FOLDERS:
            for key, _, ctype, _ in fields:
                val = mode_data.get(key, defs.get(key))
                if ctype == 'dropdown_texture':
                    result.append(gr.update(choices=available_textures, value=val))
                else:
                    result.append(val)

    result.append(json.dumps(cfg, indent=4))  # raw JSON editor
    return result


def save_settings_form(*args):
    """Reconstruct config dict from form values and write to disk (merge strategy)."""
    cfg = _read_config()  # start from existing file to preserve unknown keys
    idx = 0

    # Globals
    cfg['activeMode']             = args[idx]; idx += 1
    cfg.setdefault('globals', {})
    cfg['globals']['serverPath']  = args[idx]; idx += 1
    cfg['globals']['quality']     = args[idx]; idx += 1
    cfg['globals']['serverStart'] = _coerce('serverStart', args[idx]); idx += 1
    cfg['globals']['serverEnd']   = _coerce('serverEnd',   args[idx]); idx += 1
    cfg['globals']['showStats']   = bool(args[idx]); idx += 1
    cfg['globals']['useStereo']   = bool(args[idx]); idx += 1

    # Per-mode
    cfg.setdefault('modes', {})
    for mode in MODES:
        cfg['modes'].setdefault(mode, {})
        for _, fields in MODE_FOLDERS:
            for key, *_ in fields:
                cfg['modes'][mode][key] = _coerce(key, args[idx]); idx += 1

    cfg['savedAt'] = int(time.time() * 1000)
    cfg['version'] = 2.2

    with open(TUNNEL_CONFIG_PATH, 'w') as f:
        json.dump(cfg, f, indent=4)

    return "✅ Settings saved successfully!", json.dumps(cfg, indent=4)


# ── Tab 1 / Tab 2 functions (unchanged) ────────────────────────────────────────

def save_uploaded_files(files, target_dir):
    """Moves uploaded files from Gradio's temp storage to the selected directory."""
    if not files:
        return "No files uploaded."
    if not target_dir:
        return "Please select a target directory first."
    os.makedirs(target_dir, exist_ok=True)
    saved_count = 0
    for temp_file in files:
        filename = os.path.basename(temp_file.name)
        destination = os.path.join(target_dir, filename)
        shutil.copy(temp_file.name, destination)
        saved_count += 1
    return f"Successfully ingested {saved_count} files into '{target_dir}'. Ready for pipeline processing."


def trigger_pipeline(selected_dirs):
    """Hooks into process_assets.py and streams the output to the UI."""
    if not selected_dirs:
        yield "Please select at least one directory to process."
        return
    log_output = ""
    for update in run_pipeline_stream(target_dirs=selected_dirs):
        log_output += update
        yield log_output


def load_gallery(target_dir, progress=gr.Progress()):
    """Reads the selected directory, returns valid webp images, and streams progress."""
    if not target_dir or not os.path.exists(target_dir):
        return []
    progress(0.1, desc=f"Scanning '{target_dir}' for .webp files...")
    files = [f for f in os.listdir(target_dir) if f.lower().endswith(".webp")]
    progress(0.4, desc=f"Found {len(files)} images. Sorting numerically...")
    def numeric_sort(filename):
        name = os.path.splitext(filename)[0]
        return int(name) if name.isdigit() else float('inf')
    files.sort(key=numeric_sort)
    progress(0.8, desc="Pushing images to the visual grid...")
    return [os.path.join(target_dir, f) for f in files]


def delete_selected_image(filepath, current_dir, progress=gr.Progress()):
    """Deletes the selected image and refreshes the gallery."""
    if filepath and os.path.exists(filepath):
        try:
            os.remove(filepath)
            msg = f"Deleted: {os.path.basename(filepath)}"
        except Exception as e:
            msg = f"Error deleting file: {str(e)}"
    else:
        msg = "File not found or no selection."
    updated_gallery = load_gallery(current_dir, progress)
    return updated_gallery, msg, "", gr.update(interactive=False)


def scan_textures():
    """Scans ./textures/ for AmbientCG material folders and injects their names into tunnel_config.json."""
    textures_dir = "textures"
    if not os.path.isdir(textures_dir):
        return "Error: 'textures' directory not found in current working directory."
    folders = sorted([d for d in os.listdir(textures_dir) if os.path.isdir(os.path.join(textures_dir, d))])
    if not folders:
        return "No texture folders found in 'textures/'."
    try:
        with open(TUNNEL_CONFIG_PATH, "r") as f:
            cfg = json.load(f)
        cfg.setdefault("globals", {})["availableTextures"] = folders
        with open(TUNNEL_CONFIG_PATH, "w") as f:
            json.dump(cfg, f, indent=4)
        return f"Found {len(folders)} texture set(s): {', '.join(folders)}"
    except Exception as e:
        return f"Error updating config: {str(e)}"


def scan_and_update_textures():
    """Runs texture scan then refreshes all 12 maze texture dropdowns."""
    msg = scan_textures()
    cfg = _read_config()
    textures = cfg.get('globals', {}).get('availableTextures', [])
    updates = [gr.update(choices=textures) for _ in range(12)]  # 3 surfaces × 4 modes
    return [msg] + updates


def load_global_settings():
    """Reads the configuration JSON as a raw string (for the fallback editor)."""
    if not os.path.exists(TUNNEL_CONFIG_PATH):
        if os.path.exists("tunnel_config_potato.json"):
            return open("tunnel_config_potato.json", "r").read()
        return '{\n  "error": "No tunnel_config.json found in the current directory."\n}'
    with open(TUNNEL_CONFIG_PATH, "r") as f:
        return f.read()


def save_global_settings(json_string):
    """Saves raw JSON edits back to the configuration file."""
    try:
        parsed = json.loads(json_string)
        with open(TUNNEL_CONFIG_PATH, "w") as f:
            json.dump(parsed, f, indent=4)
        return "Settings saved successfully! Click '↩️ Reload / Revert' to sync the form above."
    except Exception as e:
        return f"Failed to save! Invalid JSON format: {str(e)}"


# ── Gradio UI Layout ────────────────────────────────────────────────────────────
with gr.Blocks(title="Image Tunnel Manager") as app:

    gr.Markdown("# 🚇 Image Tunnel Command Center")

    with gr.Tabs():

        # ── TAB 1: INGESTION & PIPELINE ────────────────────────────────────────
        with gr.Tab("Ingestion & Pipeline"):
            with gr.Row():
                with gr.Column(scale=1):
                    gr.Markdown("### 1. Ingestion Zone")
                    target_dropdown = gr.Dropdown(choices=TARGET_DIRS, label="Select Target Directory", value=TARGET_DIRS[0])
                    ingest_btn = gr.Button("Save to Folder", variant="primary")
                    ingest_status = gr.Textbox(label="Ingestion Status", interactive=False)
                    file_uploader = gr.File(file_count="multiple", label="Drag & Drop Images Here")

                with gr.Column(scale=2):
                    gr.Markdown("### 2. The Control Deck")
                    pipeline_dirs = gr.CheckboxGroup(choices=TARGET_DIRS, label="Directories to Process", value=TARGET_DIRS)
                    run_btn = gr.Button("🚀 Run Asset Pipeline", variant="primary", size="lg")
                    terminal_output = gr.Textbox(label="Terminal Output", lines=15, interactive=False, max_lines=25)

            ingest_btn.click(
                fn=save_uploaded_files,
                inputs=[file_uploader, target_dropdown],
                outputs=[ingest_status]
            ).then(fn=lambda: None, outputs=[file_uploader])

            run_btn.click(fn=trigger_pipeline, inputs=[pipeline_dirs], outputs=[terminal_output])

        # ── TAB 2: THE GALLERY ─────────────────────────────────────────────────
        with gr.Tab("The Gallery"):
            gr.Markdown("### Processed Asset Viewer")
            gr.Markdown("Click on an image to select it for deletion. *(Note: Deleting an image will create a numerical gap. Running the Asset Pipeline will automatically fill the gap and rename the sequence.)*")

            with gr.Row():
                gallery_dropdown = gr.Dropdown(choices=TARGET_DIRS, label="View Directory", value=TARGET_DIRS[0], scale=3)
                refresh_gallery_btn = gr.Button("🔄 Refresh Gallery", scale=1)

            with gr.Row():
                delete_btn = gr.Button("🗑️ Delete Selected Image", variant="stop", interactive=False, scale=1)
                delete_status = gr.Textbox(label="Delete Status", interactive=False, scale=3)

            image_gallery = gr.Gallery(label="Processed .webp Files", columns=6, rows=4, height="600px", object_fit="contain", interactive=False)
            selected_file_state = gr.State("")

            def on_gallery_select(evt: gr.SelectData, current_dir):
                files = [f for f in os.listdir(current_dir) if f.lower().endswith(".webp")]
                def numeric_sort(filename):
                    name = os.path.splitext(filename)[0]
                    return int(name) if name.isdigit() else float('inf')
                files.sort(key=numeric_sort)
                if evt.index < len(files):
                    selected_path = os.path.join(current_dir, files[evt.index])
                    return selected_path, gr.update(interactive=True)
                return "", gr.update(interactive=False)

            image_gallery.select(fn=on_gallery_select, inputs=[gallery_dropdown], outputs=[selected_file_state, delete_btn])
            delete_btn.click(fn=delete_selected_image, inputs=[selected_file_state, gallery_dropdown], outputs=[image_gallery, delete_status, selected_file_state, delete_btn])
            refresh_gallery_btn.click(fn=load_gallery, inputs=[gallery_dropdown], outputs=[image_gallery])
            gallery_dropdown.change(fn=load_gallery, inputs=[gallery_dropdown], outputs=[image_gallery])
            app.load(fn=load_gallery, inputs=[gallery_dropdown], outputs=[image_gallery])

        # ── TAB 3: GLOBAL SETTINGS ─────────────────────────────────────────────
        with gr.Tab("Manage Global Settings"):
            gr.Markdown("### 3D Viewer Configuration")
            gr.Markdown("Edit settings for all four display modes. Changes take effect on the viewer after saving.")

            # --- Global settings ---
            with gr.Accordion("🌐 Global Settings", open=True):
                with gr.Row():
                    _C_globals['activeMode'] = gr.Dropdown(choices=MODES, label="Active Mode")
                    _C_globals['serverPath'] = gr.Dropdown(
                        choices=['transparentimages', 'movieposters', 'images', 'AIimages'],
                        label="Server Path")
                    _C_globals['quality'] = gr.Dropdown(
                        choices=['full', 'half', 'quarter'],
                        label="Quality")
                with gr.Row():
                    _C_globals['serverStart'] = gr.Number(label="Server Start #", precision=0)
                    _C_globals['serverEnd']   = gr.Number(label="Server End #",   precision=0)
                    _C_globals['showStats']   = gr.Checkbox(label="Show Stats")
                    _C_globals['useStereo']   = gr.Checkbox(label="3D SBS Mode")

            # --- Per-mode settings ---
            with gr.Tabs():
                for _mode in MODES:
                    with gr.Tab(_mode.capitalize()):
                        for _folder_name, _fields in MODE_FOLDERS:
                            # Open Maze accordion by default only on the Maze tab
                            _open = (_folder_name != 'Maze') or (_mode == 'maze')
                            with gr.Accordion(_folder_name, open=_open):
                                for _key, _label, _ctype, _opts in _fields:
                                    _C_modes[_mode][_key] = _make_mode_component(_key, _label, _ctype, _opts)

            # --- Save / reload ---
            with gr.Row():
                settings_reload_btn = gr.Button("↩️ Reload / Revert")
                settings_save_btn   = gr.Button("💾 Save All Settings", variant="primary")
            settings_status = gr.Textbox(label="Status", interactive=False)

            # --- Raw JSON fallback (collapsed) ---
            with gr.Accordion("⚙️ Raw JSON Editor", open=False):
                gr.Markdown("Direct JSON editing. After saving here, click **↩️ Reload / Revert** above to sync the form.")
                json_editor = gr.Code(language="json", lines=25)
                with gr.Row():
                    reload_json_btn = gr.Button("Reload File")
                    save_json_btn   = gr.Button("Save Raw JSON", variant="secondary")
                json_status = gr.Textbox(label="JSON Save Status", interactive=False)

            # --- Texture scanner ---
            gr.Markdown("---")
            gr.Markdown("### 🗂️ Texture Scanner")
            gr.Markdown("Scans the `textures/` directory and updates the `availableTextures` list in the config. Run this after adding new texture sets.")
            with gr.Row():
                scan_btn    = gr.Button("🔍 Scan Textures Folder", variant="secondary")
                scan_status = gr.Textbox(label="Scan Result", interactive=False)

        # ── Tab 3 event wiring ──────────────────────────────────────────────────
        # Build the master ordered component list (matches load_settings_form return order)
        _all_comps = _all_form_components()

        # 12 texture dropdowns in scan-update order: wall/floor/ceiling × 4 modes
        _texture_dds = [_C_modes[m][k]
                        for m in MODES
                        for k in ['mazeWallTexture', 'mazeFloorTexture', 'mazeCeilingTexture']]

        app.load(fn=load_settings_form, outputs=_all_comps + [json_editor])
        settings_reload_btn.click(fn=load_settings_form, outputs=_all_comps + [json_editor])
        settings_save_btn.click(
            fn=save_settings_form,
            inputs=_all_comps,
            outputs=[settings_status, json_editor],
        )
        reload_json_btn.click(fn=load_global_settings, outputs=[json_editor])
        save_json_btn.click(fn=save_global_settings, inputs=[json_editor], outputs=[json_status])
        scan_btn.click(fn=scan_and_update_textures, outputs=[scan_status] + _texture_dds)

if __name__ == "__main__":
    app.launch(server_name="127.0.0.1", server_port=7860, inbrowser=True, theme=gr.themes.Monochrome())
