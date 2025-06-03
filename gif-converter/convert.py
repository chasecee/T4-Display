"""
GIF/MP4 to JPEG Sequence Converter

Usage:
    python convert.py --source ./source --output ./output [--size 320 240] [--rotate {0,90,180,-90}] [--jpeg_quality 50]

Virtual Environment Setup:
    python3 -m venv .venv
    source .venv/bin/activate
    pip install pillow imageio imageio-ffmpeg

This script will:
- Take all GIFs and MP4s from the source directory
- Rotate frames if specified (0, 90, 180, or -90 degrees)
- Resize and center-crop them to the specified size (default 320x240)
- Replace transparency with black (for GIFs)
- Save frames as JPEG files directly to the output directory
- Generate a manifest.txt with all JPEG filenames
"""

import os
import argparse
from PIL import Image
import imageio

# Ensure Image.FASTOCTREE is available, or use its integer value (2) if needed.
# from PIL import Image # Image.FASTOCTREE should be available directly

def crop_and_resize(img, size=(320, 240)): # Default changed
    # Resize to fill, then center-crop
    aspect = img.width / img.height
    target_aspect = size[0] / size[1]
    if aspect > target_aspect:
        # Wider than target: resize height, crop width
        new_height = size[1]
        new_width = int(aspect * new_height)
    else:
        # Taller than target: resize width, crop height
        new_width = size[0]
        new_height = int(new_width / aspect)
    
    # Ensure new_width and new_height are at least 1
    new_width = max(1, new_width)
    new_height = max(1, new_height)
    
    img = img.resize((new_width, new_height), Image.LANCZOS)
    
    left = (new_width - size[0]) // 2
    top = (new_height - size[1]) // 2
    img = img.crop((left, top, left + size[0], top + size[1]))
    return img

def process_media_file(input_path, base_output_dir, size=(320, 240), rotation=0, global_frame_idx_offset=0, frame_stride=1, jpeg_quality=50, args=None): # Added jpeg_quality
    reader = imageio.get_reader(input_path)
    base_name = os.path.splitext(os.path.basename(input_path))[0]
    generated_manifest_entries = []
    processed_frames_in_this_file_count = 0
    
    for original_frame_idx, frame_data in enumerate(reader):
        if original_frame_idx % frame_stride != 0:
            continue # Skip this frame

        current_processed_frame_idx_for_file = processed_frames_in_this_file_count # Index for this specific file's frames
        
        img = Image.fromarray(frame_data)
        img = img.convert('RGBA')
        
        if rotation != 0:
            img = img.rotate(rotation, expand=True)
        
        img_resized = crop_and_resize(img, size)
        
        background = Image.new('RGBA', img_resized.size, (0, 0, 0, 255)) # Black background
        img_composited = Image.alpha_composite(background, img_resized)
        img_final_rgb = img_composited.convert('RGB')

        # Output JPEG
        # Filename: original_basename-001.jpg, original_basename-002.jpg etc.
        jpeg_filename = f"{base_name}-{current_processed_frame_idx_for_file + 1:03d}.jpg"
        out_path = os.path.join(base_output_dir, jpeg_filename)
        
        try:
            img_final_rgb.save(out_path, "JPEG", quality=jpeg_quality)
            print(f"Saved JPEG frame (original index {original_frame_idx}) as: {out_path} (Quality: {jpeg_quality})")
            generated_manifest_entries.append(jpeg_filename)
            processed_frames_in_this_file_count += 1
        except Exception as e:
            print(f"Error saving JPEG frame {out_path}: {e}")
        
    reader.close()
    if processed_frames_in_this_file_count == 0:
        print(f"No frames processed from {input_path} with stride {frame_stride}")
    # Return local count, manifest entries. Global counter is managed in main.
    return generated_manifest_entries, processed_frames_in_this_file_count 

def main():
    parser = argparse.ArgumentParser(description="Convert GIFs and MP4s to JPEG sequences.")
    parser.add_argument('--source', type=str, required=True, help='Source directory containing GIFs and MP4s')
    parser.add_argument('--output', type=str, required=True, help='Output directory for JPEG frames')
    parser.add_argument('--size', nargs=2, type=int, default=[320, 240], 
                        help='Target size W H (default: 320 240)')
    parser.add_argument('--rotate', type=int, choices=[0, 90, 180, -90], default=0,
                        help='Rotation angle in degrees (default: 0)')
    parser.add_argument('--frame_stride', type=int, default=1, help='Process one frame every N frames (default: 1, process all). Value > 0.')
    parser.add_argument('--jpeg_quality', type=int, default=50, choices=range(1, 101), metavar="[1-100]",
                        help='JPEG quality (1-100, default: 50, lower is smaller file size)')
    args = parser.parse_args()

    if args.frame_stride <= 0:
        print("Error: --frame_stride must be a positive integer.")
        return

    output_size = tuple(args.size) # Already a list of two integers

    os.makedirs(args.output, exist_ok=True)
    
    all_manifest_entries = [] 
    # master_frame_counter = 0 # Not strictly needed if filenames are per-source-file based
    
    script_dir = os.path.dirname(os.path.abspath(__file__))

    for fname in sorted(os.listdir(args.source)): # Sort to process files in a consistent order
        if fname.lower().endswith(('.gif', '.mp4')): # Process only GIFs and MP4s
            in_path = os.path.join(args.source, fname)
            print(f"Processing {'video' if fname.lower().endswith('.mp4') else 'GIF'}: {fname} with stride {args.frame_stride}")
            
            # Pass jpeg_quality to process_media_file
            manifest_entries, num_frames_processed = process_media_file(
                in_path, 
                args.output, 
                size=output_size, 
                rotation=args.rotate,
                # global_frame_idx_offset is not used for per-file naming scheme
                frame_stride=args.frame_stride,
                jpeg_quality=args.jpeg_quality, # Pass new arg
                args=args # Pass all args if needed by sub-functions, though currently not much used
            )
            
            all_manifest_entries.extend(manifest_entries)
            # master_frame_counter += num_frames_processed # Not strictly needed

    if all_manifest_entries:
        manifest_path = os.path.join(args.output, 'manifest.txt')
        with open(manifest_path, 'w') as mf:
            for frame_file_rel_path in sorted(all_manifest_entries): # Sort manifest entries
                mf.write(frame_file_rel_path + '\n')
        print(f"Generated manifest.txt at {manifest_path} with {len(all_manifest_entries)} entries.")
    else:
        print("No frames were processed, so no manifest.txt was generated.")

    try:
        run_command_file_path = os.path.join(script_dir, "run-command.txt")
        command_to_write = f"python convert.py --source ./source --output ./output --size {output_size[0]} {output_size[1]} --rotate {args.rotate} --frame_stride {args.frame_stride} --jpeg_quality {args.jpeg_quality}"
        with open(run_command_file_path, 'w') as rcf:
            rcf.write(command_to_write + '\n')
        print(f"Updated run command in: {run_command_file_path}")

    except Exception as e:
        print(f"Error writing run-command.txt: {e}")

if __name__ == '__main__':
    main()
