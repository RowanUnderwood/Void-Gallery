import os
import json
import glob
import time
import multiprocessing
from multiprocessing import Pool, cpu_count
import shutil

try:
    from PIL import Image
except ImportError:
    print("Error: Required libraries not found. Please run: pip install Pillow")
    exit(1)

# --- CONFIGURATION ---
TARGET_DIRS = ["movieposters", "transparentimages", "images", "AIimages"]
TARGET_EXT = ".webp"
CONFIG_FILENAME = "config.json"
MANIFEST_FILENAME = "manifest.json"
DRY_RUN = False 

def ensure_dirs(base_dir):
    subdirs = ["halfres", "quarterres"]
    if not os.path.exists(base_dir):
        return False, [], f"Warning: Directory '{base_dir}' does not exist. Skipping."
    
    paths = {
        "full": base_dir,
        "half": os.path.join(base_dir, "halfres"),
        "quarter": os.path.join(base_dir, "quarterres")
    }
    
    for key in ["half", "quarter"]:
        if not os.path.exists(paths[key]):
            os.makedirs(paths[key])
            
    return True, paths, f"Validated directories for {base_dir}"

def convert_and_resize(task_info):
    """Worker function for multiprocessing."""
    src_full_path, filename, paths, is_dry_run = task_info
    name_no_ext = os.path.splitext(filename)[0]
    final_name = name_no_ext + ".webp"
    
    full_res_target = os.path.join(paths['full'], final_name)
    half_res_target = os.path.join(paths['half'], final_name)
    quat_res_target = os.path.join(paths['quarter'], final_name)

    try:
        if is_dry_run: return (final_name, filename)

        src_mtime = os.path.getmtime(src_full_path)
        needs_process = True
        
        if os.path.exists(full_res_target):
             if src_mtime <= os.path.getmtime(full_res_target):
                 needs_process = False

        img = None
        if needs_process or src_full_path != full_res_target:
             with Image.open(src_full_path) as img_src:
                if src_full_path != full_res_target:
                    img_src.save(full_res_target, "webp", lossless=True)
        
        half_needs_update = True
        if os.path.exists(half_res_target) and os.path.exists(full_res_target):
             if os.path.getmtime(full_res_target) <= os.path.getmtime(half_res_target):
                 half_needs_update = False

        if half_needs_update:
            if img is None: img = Image.open(full_res_target)
            w, h = img.size
            img_half = img.resize((max(1, w // 2), max(1, h // 2)), Image.Resampling.LANCZOS)
            img_half.save(half_res_target, "webp", quality=85)
        
        quat_needs_update = True
        if os.path.exists(quat_res_target) and os.path.exists(half_res_target):
             if os.path.getmtime(half_res_target) <= os.path.getmtime(quat_res_target):
                 quat_needs_update = False
        
        if quat_needs_update:
            if os.path.exists(half_res_target):
                img_half_src = Image.open(half_res_target)
                w, h = img_half_src.size
                img_quat = img_half_src.resize((max(1, w // 2), max(1, h // 2)), Image.Resampling.LANCZOS)
                img_quat.save(quat_res_target, "webp", quality=80)

    except Exception as e:
        return f"ERROR:{filename}:{str(e)}"

    return (final_name, filename)

def perform_rename_set(base_dir, src_name, dst_name):
    dirs = [base_dir, os.path.join(base_dir, "halfres"), os.path.join(base_dir, "quarterres")]
    if os.path.exists(os.path.join(base_dir, dst_name)): return False
    success = True
    for d in dirs:
        s = os.path.join(d, src_name)
        t = os.path.join(d, dst_name)
        if os.path.exists(s):
            if not DRY_RUN:
                try:
                    os.rename(s, t)
                except OSError:
                    success = False
    return success

def standardize_names_and_fill_gaps(base_dir, manifest):
    files = [f for f in os.listdir(base_dir) if f.lower().endswith(TARGET_EXT)]
    numbered_map = {} 
    others = []
    
    for f in files:
        name, _ = os.path.splitext(f)
        if name.isdigit():
            numbered_map[int(name)] = f
        else:
            others.append(f)
            
    existing_nums = sorted(numbered_map.keys())
    gaps = []
    
    if existing_nums:
        max_val = existing_nums[-1]
        existing_set = set(existing_nums)
        gaps = [i for i in range(1, max_val) if i not in existing_set]

    # Fill Gaps
    if existing_nums and gaps:
        curr_high = len(existing_nums) - 1
        for gap in gaps:
            if curr_high < 0: break
            source_num = existing_nums[curr_high]
            if source_num < gap: break 
            
            src_name = f"{source_num}{TARGET_EXT}"
            dst_name = f"{gap}{TARGET_EXT}"
            
            if perform_rename_set(base_dir, src_name, dst_name):
                if src_name in manifest:
                    manifest[dst_name] = manifest.pop(src_name)
            existing_nums[curr_high] = gap
            curr_high -= 1
            
    # Rename new files
    existing_nums = sorted(list(set(existing_nums)))
    next_num = (existing_nums[-1] + 1) if existing_nums else 1
    
    for f in others:
        new_name = f"{next_num}{TARGET_EXT}"
        if perform_rename_set(base_dir, f, new_name):
             if f in manifest: manifest[new_name] = manifest.pop(f)
             else: manifest[new_name] = f
        next_num += 1

    return next_num - 1 

def update_config_and_manifest(base_dir, total_count, manifest):
    if DRY_RUN: return
    config_path = os.path.join(base_dir, CONFIG_FILENAME)
    data = {"totalImages": total_count, "lastUpdated": time.time(), "formats": ["full", "halfres", "quarterres"]}
    with open(config_path, 'w') as f: json.dump(data, f, indent=4)
        
    manifest_path = os.path.join(base_dir, MANIFEST_FILENAME)
    with open(manifest_path, 'w') as f: json.dump(manifest, f, indent=4)

def run_pipeline_stream(target_dirs=None):
    """API entry point that yields progress strings."""
    if target_dirs is None: target_dirs = TARGET_DIRS
    yield f"--- Starting Optimized Asset Pipeline ---\nTargeting: {', '.join(target_dirs)}\n\n"
    
    for dir_name in target_dirs:
        yield f"▶ Processing Directory: {dir_name}\n"
        exists, paths, msg = ensure_dirs(dir_name)
        if not exists:
            yield msg + "\n\n"
            continue

        manifest = {}
        manifest_path = os.path.join(dir_name, MANIFEST_FILENAME)
        if os.path.exists(manifest_path):
            try:
                with open(manifest_path, 'r') as f: manifest = json.load(f)
            except: yield "  Could not load existing manifest, starting fresh.\n"

        exts = ['*.png', '*.jpg', '*.jpeg', '*.webp']
        all_files = []
        for ext in exts:
            all_files.extend(glob.glob(os.path.join(dir_name, ext)))
            all_files.extend(glob.glob(os.path.join(dir_name, ext.upper())))
        
        all_files = sorted(list(set(all_files)))
        root_files = [f for f in all_files if os.path.dirname(f) == dir_name]
        
        known_sources = set(manifest.values())
        pending_files = []
        for f in root_files:
            fname = os.path.basename(f)
            name_part, ext_part = os.path.splitext(fname)
            if name_part.isdigit() and ext_part.lower() == TARGET_EXT: continue
            if fname not in known_sources: pending_files.append(f)
                
        yield f"  Found {len(root_files)} total files ({len(pending_files)} new/untracked).\n"

        if pending_files:
            tasks = [(f_path, os.path.basename(f_path), paths, DRY_RUN) for f_path in pending_files]
            yield "  Converting and generating mipmaps (half/quarter)...\n"
            
            with Pool(processes=cpu_count()) as pool:
                processed_count = 0
                for result in pool.imap_unordered(convert_and_resize, tasks):
                    processed_count += 1
                    if isinstance(result, str) and result.startswith("ERROR"):
                        yield f"  [!] {result}\n"
                    elif result:
                        final_name, original_name = result
                        if final_name not in manifest:
                            manifest[final_name] = original_name
                    
                    if processed_count % max(1, len(tasks)//10) == 0 or processed_count == len(tasks):
                        yield f"  Progress: {processed_count}/{len(tasks)} files processed...\n"
        else:
            yield "  No new files to convert.\n"

        yield "  Standardizing names and filling numerical gaps...\n"
        total_images = standardize_names_and_fill_gaps(dir_name, manifest)
        update_config_and_manifest(dir_name, total_images, manifest)
        yield f"  Directory complete. Final sequential image count: {total_images}\n\n"

    yield "--- Pipeline Complete ---"

if __name__ == "__main__":
    multiprocessing.freeze_support()
    for log in run_pipeline_stream():
        print(log, end="")