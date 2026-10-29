#!/usr/bin/env python3
"""AssetForge concept-image stage (docs/assetforge_document §1): a reference
image from a ComfyUI server, with its provenance.

    text_to_image.py --prompt "..." --out IMAGE.png [--seed N] [--server URL]

Runs FLUX.2 [klein] 4B (Apache 2.0) with its Qwen3 4B text encoder and the
FLUX.2 VAE, through the server's HTTP API (standard library only). Writes
IMAGE.png and IMAGE.provenance.json. A fixed seed and workflow make the
request reproducible; the image itself may differ across GPUs and versions.
"""

import argparse
import hashlib
import json
import time
import urllib.parse
import urllib.request
import uuid

MODEL = "flux-2-klein-4b.safetensors"
TEXT_ENCODER = "qwen_3_4b.safetensors"
VAE = "flux2-vae.safetensors"
LICENCE = "Apache-2.0 (FLUX.2 [klein] 4B, Black Forest Labs)"


def workflow(prompt, seed, size, steps):
    return {
        "unet": {"class_type": "UNETLoader",
                 "inputs": {"unet_name": MODEL, "weight_dtype": "default"}},
        "clip": {"class_type": "CLIPLoader",
                 "inputs": {"clip_name": TEXT_ENCODER, "type": "flux2"}},
        "vae": {"class_type": "VAELoader", "inputs": {"vae_name": VAE}},
        "text": {"class_type": "CLIPTextEncode",
                 "inputs": {"clip": ["clip", 0], "text": prompt}},
        "guider": {"class_type": "BasicGuider",
                   "inputs": {"model": ["unet", 0], "conditioning": ["text", 0]}},
        "noise": {"class_type": "RandomNoise", "inputs": {"noise_seed": seed}},
        "sampler": {"class_type": "KSamplerSelect", "inputs": {"sampler_name": "euler"}},
        "sigmas": {"class_type": "Flux2Scheduler",
                   "inputs": {"steps": steps, "width": size, "height": size}},
        "latent": {"class_type": "EmptyFlux2LatentImage",
                   "inputs": {"width": size, "height": size, "batch_size": 1}},
        "sample": {"class_type": "SamplerCustomAdvanced",
                   "inputs": {"noise": ["noise", 0], "guider": ["guider", 0],
                              "sampler": ["sampler", 0], "sigmas": ["sigmas", 0],
                              "latent_image": ["latent", 0]}},
        "decode": {"class_type": "VAEDecode",
                   "inputs": {"samples": ["sample", 0], "vae": ["vae", 0]}},
        "save": {"class_type": "SaveImage",
                 "inputs": {"images": ["decode", 0], "filename_prefix": "assetforge"}},
    }


def request(server, path, payload=None):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(server + path, data=data,
                                 headers={"Content-Type": "application/json"} if data else {})
    with urllib.request.urlopen(req, timeout=60) as response:
        return response.read()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--prompt", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--size", type=int, default=1024)
    parser.add_argument("--steps", type=int, default=4)
    parser.add_argument("--server", default="http://localhost:8188")
    args = parser.parse_args()

    graph = workflow(args.prompt, args.seed, args.size, args.steps)
    started = time.time()
    queued = json.loads(request(args.server, "/prompt",
                                {"prompt": graph, "client_id": str(uuid.uuid4())}))
    prompt_id = queued["prompt_id"]
    while True:
        history = json.loads(request(args.server, f"/history/{prompt_id}"))
        if prompt_id in history:
            entry = history[prompt_id]
            if entry.get("status", {}).get("status_str") == "error":
                raise SystemExit(f"ComfyUI error: {entry['status']}")
            if entry.get("outputs"):
                break
        time.sleep(1.0)
    image = entry["outputs"]["save"]["images"][0]
    query = urllib.parse.urlencode({"filename": image["filename"],
                                    "subfolder": image["subfolder"], "type": image["type"]})
    data = request(args.server, "/view?" + query)
    with open(args.out, "wb") as stream:
        stream.write(data)

    stats = json.loads(request(args.server, "/system_stats"))
    provenance = {
        "stage": "concept image",
        "prompt": args.prompt,
        "seed": args.seed,
        "size_px": args.size,
        "steps": args.steps,
        "sampler": "euler, Flux2Scheduler, BasicGuider",
        "model": MODEL,
        "text_encoder": TEXT_ENCODER,
        "vae": VAE,
        "licence": LICENCE,
        "server": {"comfyui": stats["system"].get("comfyui_version"),
                   "devices": [d["name"] for d in stats.get("devices", [])]},
        "workflow_sha256": hashlib.sha256(json.dumps(graph, sort_keys=True).encode()).hexdigest(),
        "image_sha256": hashlib.sha256(data).hexdigest(),
        "seconds": round(time.time() - started, 1),
    }
    with open(args.out.rsplit(".", 1)[0] + ".provenance.json", "w") as stream:
        json.dump(provenance, stream, indent=2, sort_keys=True)
        stream.write("\n")
    print(f"assetforge: wrote {args.out} in {provenance['seconds']} s")


if __name__ == "__main__":
    main()
