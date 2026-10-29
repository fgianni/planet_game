"""AssetForge image-to-3D worker (docs/assetforge_document §8): runs Stable
Fast 3D on a GPU machine and reports what it ran. Copied there and run by
sf3d_remote.py, inside SF3D's own environment:

    python sf3d_worker.py --sf3d DIR --image IMAGE.png --out DIR [--texture PX]

Prints one line "assetforge-worker: {json}" with the run's record.
"""

import argparse
import glob
import json
import os
import runpy
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sf3d", required=True, help="the stable-fast-3d checkout")
    parser.add_argument("--image", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--texture", type=int, default=2048)
    args = parser.parse_args()

    import torch

    os.chdir(args.sf3d)
    sys.path.insert(0, args.sf3d)
    started = time.time()
    sys.argv = ["run.py", args.image, "--output-dir", args.out,
                "--texture-resolution", str(args.texture)]
    runpy.run_path("run.py", run_name="__main__")
    seconds = time.time() - started

    snapshots = glob.glob(os.path.expanduser(
        "~/.cache/huggingface/hub/models--stabilityai--stable-fast-3d/snapshots/*"))
    record = {
        "model": "stabilityai/stable-fast-3d",
        "model_revision": os.path.basename(snapshots[0]) if len(snapshots) == 1 else "unknown",
        "code_revision": subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True,
                                        text=True).stdout.strip(),
        "licence": "Stability AI Community License (commercial use up to USD 1 M annual revenue)",
        "texture_resolution_px": args.texture,
        "gpu": torch.cuda.get_device_name(0),
        "torch": torch.__version__,
        "seconds": round(seconds, 1),
        "peak_gpu_memory_gib": round(torch.cuda.max_memory_reserved() / 2**30, 2),
        "mesh": os.path.join(args.out, "0", "mesh.glb"),
    }
    print("assetforge-worker: " + json.dumps(record, sort_keys=True))


if __name__ == "__main__":
    main()
