import os
import json
import glob
import time
import multiprocessing
from multiprocessing import Pool, cpu_count
import shutil

try:
    from PIL import Image, ImageOps
    from tqdm import tqdm
except ImportError:
    print("Error: Required libraries not found.")
    print("Please run: pip install tqdm Pillow")
    exit(1)

# --- CONFIGURATION ---
TARGET_DIRS = ["movieposters", "transparentimages", "images", "AIimages"]
TARGET_EXT = ".webp"
CONFIG_FILENAME = "config.json"
MANIFEST_FILENAME = "manifest.json"
DRY_RUN = False 

def ensure_dirs(base_dir):
    """Creates main, halfres, and quarterres directories if they don't exist."""
    subdirs = ["halfres", "quarterres"]
    if not os.path.exists(base_dir):
        print(f"Warning: Directory '{base_dir}' does not exist. Skipping.")
        return False, []
    
    paths = {
        "full": base_dir,
        "half": os.path.join(base_dir, "halfres"),
        "quarter": os.path.join(base_dir, "quarterres")
    }
    
    for key in ["half", "quarter"]:
        if not os.path.exists(paths[key]):
            os.makedirs(paths[key])
            
    return True, paths

def convert_and_resize(task_info):
    """
    Worker function. Returns (final_filename, original_filename) if successful.
    task_info: (source_path, filename, paths_dict, is_dry_run)
    """
    src_full_path, filename, paths, is_dry_run = task_info
    
    name_no_ext = os.path.splitext(filename)[0]
    final_name = name_no_ext + ".webp"
    
    full_res_target = os.path.join(paths['full'], final_name)
    half_res_target = os.path.join(paths['half'], final_name)
    quat_res_target = os.path.join(paths['quarter'], final_name)

    try:
        if is_dry_run:
            return (final_name, filename)

        # Smart Caching: Check modification times
        src_mtime = os.path.getmtime(src_full_path)
        needs_process = True
        
        # If target exists, check if source is newer
        if os.path.exists(full_res_target):
             dst_mtime = os.path.getmtime(full_res_target)
             if src_mtime <= dst_mtime:
                 needs_process = False

        img = None
        
        # 1. Convert/Copy to Main Directory
        # This explicit step ensures we convert PNG/JPG -> WEBP before renaming logic touches it
        if needs_process or src_full_path != full_res_target:
             with Image.open(src_full_path) as img_src:
                if src_full_path != full_res_target:
                    img_src.save(full_res_target, "webp", lossless=True)
        
        # 2. Generate Half Res (Smart Cache Check)
        half_needs_update = True
        if os.path.exists(half_res_target):
             if os.path.getmtime(full_res_target) <= os.path.getmtime(half_res_target):
                 half_needs_update = False

        if half_needs_update:
            if img is None: img = Image.open(full_res_target)
            w, h = img.size
            img_half = img.resize((max(1, w // 2), max(1, h // 2)), Image.Resampling.LANCZOS)
            img_half.save(half_res_target, "webp", quality=85)
        
        # 3. Generate Quarter Res (OPTIMIZED: Chain from Half Res)
        quat_needs_update = True
        if os.path.exists(quat_res_target):
             if os.path.getmtime(half_res_target) <= os.path.getmtime(quat_res_target):
                 quat_needs_update = False
        
        if quat_needs_update:
            # Load half res to resize down (faster than loading full res)
            if os.path.exists(half_res_target):
                img_half_src = Image.open(half_res_target)
                w, h = img_half_src.size
                img_quat = img_half_src.resize((max(1, w // 2), max(1, h // 2)), Image.Resampling.LANCZOS)
                img_quat.save(quat_res_target, "webp", quality=80)

    except Exception as e:
        print(f"Error processing {filename}: {str(e)}")
        return None

    return (final_name, filename)

def standardize_names_and_fill_gaps(base_dir, manifest):
    """
    Renames files to 1.webp, 2.webp... and updates manifest keys.
    Only touches files ending in .webp.
    """
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
    
    # Calculate Total Operations for Progress Bar
    gaps = []
    gap_moves = 0
    if existing_nums:
        max_val = existing_nums[-1]
        existing_set = set(existing_nums)
        gaps = [i for i in range(1, max_val) if i not in existing_set]
        gap_moves = min(len(gaps), len(existing_nums))

    total_ops = gap_moves + len(others)
    
    with tqdm(total=total_ops, desc="Standardizing", unit="file") as pbar:
        # 1. Fill Gaps
        if existing_nums and gaps:
            curr_high = len(existing_nums) - 1
            for gap in gaps:
                if curr_high < 0: break
                source_num = existing_nums[curr_high]
                if source_num < gap: break 
                
                src_name = f"{source_num}{TARGET_EXT}"
                dst_name = f"{gap}{TARGET_EXT}"
                
                if perform_rename_set(base_dir, src_name, dst_name):
                    # Update Manifest
                    if src_name in manifest:
                        manifest[dst_name] = manifest.pop(src_name)
                    pbar.update(1)
                        
                existing_nums[curr_high] = gap
                curr_high -= 1
                
        # 2. Rename "others"
        existing_nums = sorted(list(set(existing_nums))) # Re-sort after gap filling
        next_num = (existing_nums[-1] + 1) if existing_nums else 1
        
        for f in others:
            new_name = f"{next_num}{TARGET_EXT}"
            if perform_rename_set(base_dir, f, new_name):
                 if f in manifest:
                     manifest[new_name] = manifest.pop(f)
                 pbar.update(1)
            next_num += 1

    return next_num - 1 

def perform_rename_set(base_dir, src_name, dst_name):
    """Renames files safely."""
    dirs = [base_dir, os.path.join(base_dir, "halfres"), os.path.join(base_dir, "quarterres")]
    
    if os.path.exists(os.path.join(base_dir, dst_name)):
        return False

    success = True
    for d in dirs:
        s = os.path.join(d, src_name)
        t = os.path.join(d, dst_name)
        if os.path.exists(s):
            if not DRY_RUN:
                try:
                    os.rename(s, t)
                except OSError as e:
                    print(f"Error renaming {s} -> {t}: {e}")
                    success = False
            else:
                pass
    return success

def update_config_and_manifest(base_dir, total_count, manifest):
    if DRY_RUN: return
    
    # Save Config
    config_path = os.path.join(base_dir, CONFIG_FILENAME)
    data = {
        "totalImages": total_count,
        "lastUpdated": time.time(),
        "formats": ["full", "halfres", "quarterres"]
    }
    with open(config_path, 'w') as f:
        json.dump(data, f, indent=4)
        
    # Save Manifest
    manifest_path = os.path.join(base_dir, MANIFEST_FILENAME)
    with open(manifest_path, 'w') as f:
        json.dump(manifest, f, indent=4)
        
    print(f"Updated {config_path}: Total {total_count}")
    print(f"Updated {manifest_path}")

def process_directory(dir_name):
    print(f"\n--- Processing: {dir_name} ---")
    exists, paths = ensure_dirs(dir_name)
    if not exists: return

    # Load existing manifest if present
    manifest_path = os.path.join(dir_name, MANIFEST_FILENAME)
    manifest = {}
    if os.path.exists(manifest_path):
        try:
            with open(manifest_path, 'r') as f:
                manifest = json.load(f)
        except:
            print("Could not load existing manifest, starting fresh.")

    # Gather images
    exts = ['*.png', '*.jpg', '*.jpeg', '*.webp']
    all_files = []
    for ext in exts:
        all_files.extend(glob.glob(os.path.join(dir_name, ext)))
        all_files.extend(glob.glob(os.path.join(dir_name, ext.upper())))
    
    all_files = sorted(list(set(all_files)))
    root_files = [f for f in all_files if os.path.dirname(f) == dir_name]
    
    # --- VITAL FIX: Filter out already processed sources ---
    # This prevents the script from re-converting 'img.jpg' -> 'img.webp'
    # if 'img.jpg' was already converted and renamed to '1.webp' in a previous run.
    known_sources = set(manifest.values())
    pending_files = []
    for f in root_files:
        if os.path.basename(f) not in known_sources:
            pending_files.append(f)
            
    print(f"Found {len(root_files)} files ({len(pending_files)} new/untracked).")

    workers = cpu_count()
    
    tasks = []
    for f_path in pending_files:
        filename = os.path.basename(f_path)
        tasks.append((f_path, filename, paths, DRY_RUN))
    
    # Process (Conversion Step)
    if tasks:
        with Pool(processes=workers) as pool:
            for result in tqdm(pool.imap_unordered(convert_and_resize, tasks), total=len(tasks), unit="img", desc="Converting"):
                if result:
                    final_name, original_name = result
                    if final_name not in manifest:
                        manifest[final_name] = original_name
    else:
        print("No new files to convert.")

    # Standardize (Renaming Step)
    total_images = standardize_names_and_fill_gaps(dir_name, manifest)
    
    # Write Data
    update_config_and_manifest(dir_name, total_images, manifest)

def main():
    if __name__ == "__main__":
        multiprocessing.freeze_support()
        print("--- Starting Optimized Asset Pipeline ---")
        for d in TARGET_DIRS:
            process_directory(d)
        print("\n--- Pipeline Complete ---")

if __name__ == "__main__":
    main()