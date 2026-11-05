#!/usr/bin/env python3
"""Compare a fixed-pose fan render with independent pixel-centred relief.

Usage: python3 tools/render/measure_coast_fan_reference.py FAN.png CANONICAL.ppm
       [--patch PARENT.png]
Only the common visible interior is measured. The unit-sphere reference
omits raised-terrain parallax; the optional raised reference is a sampled
ray-intersection control, not an exact visibility solver. Neither is a
production-style, moving-camera, or matched-contour acceptance test.
"""

import argparse

from scipy import ndimage

from measure_coast_render_mesh import compare, read


def metrics(drawn, visible, reference, reference_visible):
    if drawn.shape != reference.shape:
        raise ValueError("reference and render dimensions differ")
    interior = ndimage.binary_erosion(visible & reference_visible, iterations=2)
    return dict(common_interior=int(interior.sum()),
                visible_xor=int((visible ^ reference_visible).sum()),
                drawn_water=int((drawn & interior).sum()),
                canonical_water=int((reference & interior).sum()),
                water=compare(drawn, reference, interior))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fan")
    parser.add_argument("reference")
    parser.add_argument("--patch", help="independent tile patch render for a control")
    args = parser.parse_args()
    reference, rv = read(args.reference)
    print("fan:", metrics(*read(args.fan), reference, rv))
    if args.patch:
        print("patch:", metrics(*read(args.patch), reference, rv))


if __name__ == "__main__":
    main()
