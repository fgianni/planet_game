#!/usr/bin/env python3
"""AssetForge Phase 4: a searchable catalogue of the built assets, one
static page that works offline (after build_all.py).

    catalogue.py [--out assets/generated]

Writes OUT/catalogue/index.html from OUT/report.json and each asset's
specification, manifest, provenance and preview: its type, status,
validator and Godot results, approval, ports, animations and bindings,
materials, size, triangles and licences. Standard library only.
"""

import argparse
import html
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools/asset_pipeline/validators"))
import validate_asset  # noqa: E402  (to read the GLBs)


def triangles(glb):
    document, _ = validate_asset.read_glb(glb)
    count = 0
    for node in document["nodes"]:
        if "mesh" not in node or node.get("name", "").endswith("-convcolonly"):
            continue
        for primitive in document["meshes"][node["mesh"]]["primitives"]:
            accessor = document["accessors"][primitive.get("indices", primitive["attributes"]["POSITION"])]
            count += accessor["count"] // 3
    return count


def card(row, out_root):
    asset = row["id"]
    spec = json.load(open(os.path.join(ROOT, row["specification"])))
    folder = os.path.join(out_root, asset)
    glb = os.path.join(folder, asset + ".glb")
    provenance_path = os.path.join(folder, asset + ".provenance.json")
    provenance = json.load(open(provenance_path)) if os.path.exists(provenance_path) else {}
    built = os.path.exists(glb)
    ports = [f"{p['id']} ({p['type']})" for p in spec.get("ports", [])]
    animations = [f"{key}: {a.get('kind', 'rotation')} ← {a.get('binding', {}).get('state', 'rotor_speed_rpm')}"
                  for key, a in spec.get("animations", {}).items()]
    licences = provenance.get("licences") or [provenance.get("licence", "procedural")]
    if "dimensions_m" in spec:
        size = " × ".join(f"{d:g}" for d in spec["dimensions_m"]) + " m"
    else:
        size = f"{spec.get('height_m', 0):g} m tall"
    validator = row.get("validator")
    facts = [
        ("Status", row["status"] + (f" ({row['built']})" if row.get("built") else "")),
        ("Validator", f"{validator['passed']}/{validator['passed'] + validator['failed']}" if validator else "–"),
        ("Godot", row.get("godot", "not checked")),
        ("Approval", row.get("approval", "not needed (procedural)")),
        ("Size", size),
        ("Triangles", f"{triangles(glb):,}" if built else "–"),
        ("File", f"{os.path.getsize(glb) / 1024:,.0f} KB" if built else "–"),
        ("Ports", ", ".join(ports) or "none"),
        ("Animations", "; ".join(animations) or "none"),
        ("Materials", ", ".join(spec.get("materials", {}).values())),
        ("Licences", "; ".join(licences)),
    ]
    searchable = " ".join([asset, spec["type"], spec.get("family", ""), spec.get("description", "")]
                          + ports + animations + list(spec.get("materials", {}).values()) + licences).lower()
    preview = f"../{asset}/{asset}.preview.png"
    status_class = {"PASS": "pass", "FAIL": "fail"}.get(row["status"], "skip")
    rows_html = "".join(f"<dt>{html.escape(k)}</dt><dd>{html.escape(v)}</dd>" for k, v in facts)
    return (f'<article class="card" data-search="{html.escape(searchable)}">'
            f'<img src="{html.escape(preview)}" alt="Preview of {html.escape(asset)}">'
            f'<div class="body"><h2>{html.escape(asset)} <span class="badge {status_class}">'
            f'{html.escape(row["status"])}</span></h2>'
            f'<p class="type">{html.escape(spec["type"])}{" · " + html.escape(spec["family"]) if spec.get("family") else ""}</p>'
            f'<p class="desc">{html.escape(spec.get("description", ""))}</p>'
            f'<dl>{rows_html}</dl></div></article>')


PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>AssetForge catalogue</title>
<style>
:root {{ --bg: #f6f6f4; --card: #ffffff; --text: #1d1d1b; --muted: #5f5f5a; --line: #e2e2dd;
        --pass: #1f7a3a; --fail: #b3261e; --skip: #8a6d00; }}
@media (prefers-color-scheme: dark) {{
  :root {{ --bg: #161615; --card: #22221f; --text: #ececea; --muted: #a3a39c; --line: #34342f;
          --pass: #6fcf8a; --fail: #f2867e; --skip: #e3c55a; }} }}
body {{ margin: 0; background: var(--bg); color: var(--text);
       font: 15px/1.45 system-ui, -apple-system, "Segoe UI", sans-serif; }}
header {{ padding: 20px 16px 8px; max-width: 1200px; margin: 0 auto; }}
h1 {{ font-size: 22px; margin: 0 0 4px; }}
header p {{ margin: 0 0 12px; color: var(--muted); }}
input {{ width: 100%; max-width: 480px; box-sizing: border-box; padding: 10px 12px; font: inherit;
        border: 1px solid var(--line); border-radius: 8px; background: var(--card); color: var(--text); }}
main {{ display: grid; gap: 16px; grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
       padding: 16px; max-width: 1200px; margin: 0 auto; }}
.card {{ background: var(--card); border: 1px solid var(--line); border-radius: 10px; overflow: hidden; }}
.card img {{ width: 100%; aspect-ratio: 4 / 3; object-fit: cover; display: block; background: #888; }}
.body {{ padding: 12px 14px 14px; }}
h2 {{ font-size: 17px; margin: 0; display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }}
.badge {{ font-size: 12px; padding: 1px 8px; border-radius: 999px; border: 1px solid currentColor; }}
.pass {{ color: var(--pass); }} .fail {{ color: var(--fail); }} .skip {{ color: var(--skip); }}
.type {{ margin: 2px 0 6px; color: var(--muted); }}
.desc {{ margin: 0 0 10px; }}
dl {{ display: grid; grid-template-columns: max-content 1fr; gap: 3px 10px; margin: 0; font-size: 13.5px; }}
dt {{ color: var(--muted); }} dd {{ margin: 0; overflow-wrap: anywhere; }}
.empty {{ padding: 16px; color: var(--muted); max-width: 1200px; margin: 0 auto; }}
</style></head>
<body><header><h1>AssetForge catalogue</h1>
<p>{count} assets · {summary} · built with {blender}</p>
<input id="q" type="search" placeholder="Search: id, type, port, material, licence…" aria-label="Search the assets">
</header>
<main id="cards">{cards}</main>
<p class="empty" id="none" hidden>No asset matches.</p>
<script>
const q = document.getElementById("q"), cards = [...document.querySelectorAll(".card")];
q.addEventListener("input", () => {{
  const words = q.value.toLowerCase().split(/\\s+/).filter(Boolean);
  let shown = 0;
  for (const c of cards) {{
    const ok = words.every(w => c.dataset.search.includes(w));
    c.hidden = !ok; shown += ok;
  }}
  document.getElementById("none").hidden = shown > 0;
}});
</script></body></html>
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", default=os.path.join(ROOT, "assets/generated"))
    args = parser.parse_args()
    out_root = os.path.abspath(args.out)
    report = json.load(open(os.path.join(out_root, "report.json")))
    rows = report["assets"]
    counts = {s: sum(1 for r in rows if r["status"] == s) for s in ("PASS", "FAIL", "SKIP")}
    summary = ", ".join(f"{n} {s.lower()}" for s, n in counts.items() if n)
    page = PAGE.format(count=len(rows), summary=summary, blender=html.escape(report["blender"]),
                       cards="".join(card(r, out_root) for r in rows))
    os.makedirs(os.path.join(out_root, "catalogue"), exist_ok=True)
    path = os.path.join(out_root, "catalogue", "index.html")
    with open(path, "w") as stream:
        stream.write(page)
    print(f"assetforge catalogue: {len(rows)} assets -> {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
