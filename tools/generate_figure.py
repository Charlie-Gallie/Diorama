"""Generates assets/models/test/figure_40.vox.

A blocky 1.8 m person, built to be a scale reference rather than a character. Everything else
in a shot is judged against it, so what matters is that the proportions are right and that it
reads as a person at gameplay framing, not that it is detailed. Rigid boxes on purpose, since
that is the shape a skeleton of voxel body parts would end up producing anyway.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from voxfile import Palette, output_directory, write_vox

OUT = output_directory()

PALETTE = Palette()

SKIN = 1
HAIR = 2
SHIRT = 3
SLEEVE = 4
TROUSER = 5
SHOE = 6
BELT = 7

PALETTE.set(SKIN,    (214, 164, 126))
PALETTE.set(HAIR,    ( 74,  50,  36))
PALETTE.set(SHIRT,   ( 72, 108, 150))
PALETTE.set(SLEEVE,  ( 58,  90, 128))
PALETTE.set(TROUSER, ( 62,  66,  78))
PALETTE.set(SHOE,    ( 34,  34,  38))
PALETTE.set(BELT,    ( 44,  40,  44))

DENSITY = 40

# Every measurement in metres, from the floor up. Roughly standard proportions: the head is
# about an eighth of the height and the legs a little under half
HEIGHT = 1.80

PARTS = [
    # (material, x half width, z half depth, bottom, top)
    (SHOE,    0.085, 0.130, 0.00, 0.07),
    (TROUSER, 0.075, 0.095, 0.07, 0.88),
    (BELT,    0.145, 0.105, 0.88, 0.94),
    (SHIRT,   0.145, 0.105, 0.94, 1.44),
    (SKIN,    0.075, 0.075, 1.44, 1.56),
    (SKIN,    0.095, 0.100, 1.56, 1.72),
    (HAIR,    0.100, 0.103, 1.68, 1.80),
]

# Arms hang either side of the torso and stop short of the hips
ARMS = [
    (SLEEVE, 0.145, 0.205, 0.090, 1.06, 1.40),
    (SKIN,   0.145, 0.205, 0.090, 0.92, 1.06),
]

# The gap between the legs, so they read as two rather than as one column
LEG_INNER = 0.022
LEG_PARTS = (SHOE, TROUSER)


def material_at(x, y, z):
    """The palette index at a point in metres, or 0 for air"""
    for part, halfX, halfZ, bottom, top in PARTS:
        if bottom <= y < top and abs(x) < halfX and abs(z) < halfZ:
            if part in LEG_PARTS and abs(x) < LEG_INNER:
                continue
            return part

    for part, innerX, outerX, halfZ, bottom, top in ARMS:
        if bottom <= y < top and innerX <= abs(x) < outerX and abs(z) < halfZ:
            return part

    return 0


def build(density):
    voxel = 1.0 / density

    halfWidth = max(outer for _, _, outer, _, _, _ in ARMS)
    halfDepth = max(half for _, _, half, _, _ in PARTS)

    # A .vox file is Z up, so the third axis is height and the second is depth
    sizeX = int(round(halfWidth * 2 / voxel))
    sizeY = int(round(halfDepth * 2 / voxel))
    sizeZ = int(round(HEIGHT / voxel))

    voxels = []

    for iz in range(sizeZ):
        y = (iz + 0.5) * voxel
        for iy in range(sizeY):
            z = (iy + 0.5) * voxel - halfDepth
            for ix in range(sizeX):
                x = (ix + 0.5) * voxel - halfWidth

                material = material_at(x, y, z)

                if material:
                    voxels.append((ix, iy, iz, material))

    return (sizeX, sizeY, sizeZ), voxels


size, voxels = build(DENSITY)
path = os.path.join(OUT, "figure_%d.vox" % DENSITY)

write_vox(path, size, voxels, PALETTE)

print("%s  %dx%dx%d  %d voxels" % (path, size[0], size[1], size[2], len(voxels)))
