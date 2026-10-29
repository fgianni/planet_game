#!/usr/bin/env python3
"""AssetForge image-to-3D step (docs/assetforge_document §8): Stable Fast 3D
on a GPU machine reached by SSH, a replaceable worker.

    sf3d_remote.py --image CONCEPT.png --out MESH.glb
                   [--host fred@192.168.31.25] [--remote-root ~/src/sf3d]
                   [--texture 2048] [--comfyui http://localhost:8188]

Frees the ComfyUI server's GPU memory if one shares the GPU, copies the
image and the worker (sf3d_worker.py) over, runs it in SF3D's environment
there, fetches the mesh and writes MESH.provenance.json: the step's record
with the input's and the output's hashes. Standard library only.
"""

import argparse
import hashlib
import json
import os
import subprocess
import sys
import urllib.request

WORKER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sf3d_worker.py")


def sha256(path):
    with open(path, "rb") as stream:
        return hashlib.sha256(stream.read()).hexdigest()


def run(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"assetforge: {' '.join(command[:3])}... failed:\n{result.stderr[-3000:]}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--image", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--host", default="fred@192.168.31.25")
    parser.add_argument("--remote-root", default="src/sf3d", help="relative to the remote home")
    parser.add_argument("--texture", type=int, default=2048)
    parser.add_argument("--comfyui", default="http://localhost:8188",
                        help="a ComfyUI server sharing the GPU, asked to unload its models ('' : none)")
    args = parser.parse_args()

    if args.comfyui:
        try:
            request = urllib.request.Request(
                args.comfyui + "/free", data=json.dumps({"unload_models": True, "free_memory": True}).encode(),
                headers={"Content-Type": "application/json"})
            urllib.request.urlopen(request, timeout=30).read()
        except OSError as error:
            print(f"assetforge: ComfyUI not freed ({error}); continuing")

    image_sha = sha256(args.image)
    remote_run = f"{args.remote_root}/runs/{image_sha[:16]}_{args.texture}"
    ssh = ["ssh", "-o", "BatchMode=yes", args.host]
    run(ssh + [f"mkdir -p {remote_run}"])
    run(["scp", "-q", args.image, f"{args.host}:{remote_run}/input.png"])
    run(["scp", "-q", WORKER, f"{args.host}:{args.remote_root}/sf3d_worker.py"])
    output = run(ssh + [
        f"cd {args.remote_root} && . venv/bin/activate && "
        f"PYTORCH_CUDA_ALLOC_CONF=expandable_segments:True python sf3d_worker.py "
        f"--sf3d $HOME/{args.remote_root}/stable-fast-3d --image $HOME/{remote_run}/input.png "
        f"--out $HOME/{remote_run} --texture {args.texture}"])
    lines = [line for line in output.splitlines() if line.startswith("assetforge-worker: ")]
    if not lines:
        raise SystemExit(f"assetforge: the worker reported nothing:\n{output[-3000:]}")
    record = json.loads(lines[-1].split(": ", 1)[1])
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    run(["scp", "-q", f"{args.host}:{record['mesh']}", args.out])

    record.pop("mesh")
    record.update({"step": "image to 3D", "host": args.host,
                   "input_image_sha256": image_sha, "output_sha256": sha256(args.out)})
    with open(args.out.rsplit(".", 1)[0] + ".provenance.json", "w") as stream:
        json.dump(record, stream, indent=2, sort_keys=True)
        stream.write("\n")
    print(f"assetforge: wrote {args.out} ({record['seconds']} s, "
          f"{record['peak_gpu_memory_gib']} GiB on {record['gpu']})")


if __name__ == "__main__":
    sys.exit(main())
