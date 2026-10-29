#!/usr/bin/env python3
"""AssetForge human approval (docs/assetforge_document §10: neural meshes are
candidates for human approval). Records a person's decision on exactly the
asset they reviewed:

    approve_asset.py --spec SPEC.json --reviewer NAME --decision approved|rejected
                     [--note TEXT] [--built assets/generated]

Writes assets/approvals/<id>.json (tracked in git) with the built asset's
geometry hash and its preview's, so that a rebuild with different bytes
needs a new review. The validator's --require-approval checks it.
"""

import argparse
import datetime
import hashlib
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def sha256(path):
    with open(path, "rb") as stream:
        return hashlib.sha256(stream.read()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--spec", required=True)
    parser.add_argument("--reviewer", required=True)
    parser.add_argument("--decision", required=True, choices=["approved", "rejected"])
    parser.add_argument("--note", default="")
    parser.add_argument("--built", default=os.path.join(ROOT, "assets/generated"))
    args = parser.parse_args()

    spec = json.load(open(args.spec))
    asset = spec["id"]
    directory = os.path.join(args.built, asset)
    glb = os.path.join(directory, asset + ".glb")
    preview = os.path.join(directory, asset + ".preview.png")
    if not os.path.exists(preview):
        raise SystemExit("assetforge: build the asset with --preview first: a review needs an image")
    record = {
        "asset": asset,
        "decision": args.decision,
        "reviewer": args.reviewer,
        "date": datetime.date.today().isoformat(),
        "note": args.note,
        "geometry_sha256": sha256(glb),
        "preview_sha256": sha256(preview),
        "specification_sha256": sha256(args.spec),
    }
    os.makedirs(os.path.join(ROOT, "assets/approvals"), exist_ok=True)
    path = os.path.join(ROOT, "assets/approvals", asset + ".json")
    with open(path, "w") as stream:
        json.dump(record, stream, indent=2, sort_keys=True)
        stream.write("\n")
    print(f"assetforge: {asset} {args.decision} by {args.reviewer} -> {os.path.relpath(path, ROOT)}")


if __name__ == "__main__":
    sys.exit(main())
