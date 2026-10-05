"""Generates assets/models/test/menger_3.vox.

A debug block in the spirit of a command block: obviously synthetic, easy to pick out of a
scene, and a known size so anything near it can be judged against it. A Menger sponge is a
useful shape to keep around because it is corners and holes at several scales at once, which
is the hardest case for both the ambient occlusion and the smooth shading normal, and the
easiest place to notice either going wrong.

One voxel per Menger cell, so the sponge is exact rather than a resampling of a finer one.
Iteration n is 3^n voxels along each axis, which means the iteration and the cube it fills
together decide the density - the file says nothing about either.

Iteration 4 was tried first and dropped. In a 1 m cube its finest holes are 12.3 mm, which is
under a pixel at the framing the game is actually played at, so the two deepest levels were
storage that could never be seen. Iteration 3 is three times coarser and reads from across
the scene.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from voxfile import Palette, output_directory, write_vox

OUT = output_directory()


def rgb(hexColour):
    """#RRGGBB to a 0-255 triple"""
    value = int(hexColour.lstrip("#"), 16)

    return (value >> 16 & 255, value >> 8 & 255, value & 255)


PALETTE = Palette()

EVIE_PURPLE = 1
CHARLIE_TEAL = 2

PALETTE.set(EVIE_PURPLE, rgb("#784164"))
PALETTE.set(CHARLIE_TEAL, rgb("#2E5F80"))

ITERATIONS = 3

# The one file is instanced at both of these, so the voxel size is chosen per instance rather
# than baked in here. Only used to report what each works out as
CUBE_SIZES = [1.0, 0.5]


def is_solid(x, y, z, iterations):
    """A cell survives unless two of its three base 3 digits are the middle one, at any level"""
    for _ in range(iterations):
        if (x % 3 == 1) + (y % 3 == 1) + (z % 3 == 1) >= 2:
            return False

        x //= 3
        y //= 3
        z //= 3

    return True


def material_at(x, y, z, size, cell):
    """Teal skin with purple panel seams, purple everywhere inside.

    Painted outside and raw within is how manufactured things actually look, and it is the one
    scheme here that survives being viewed face on. Anything keyed to which top level cell a
    voxel belongs to becomes a chequerboard from straight ahead, because the surviving cells of
    a sponge alternate corner and strut across every face.

    It also puts the two colours to work on the thing that makes a sponge interesting: the
    holes stop being dark squares and start reading as openings into something.
    """
    onSkin = 0 in (x, y, z) or (size - 1) in (x, y, z)

    if not onSkin:
        return EVIE_PURPLE

    # A skin voxel already has one coordinate against the boundary, so a second one lands on the
    # seam between two top level cells. On a flat face those run as continuous panel lines
    onSeam = (
        (x % cell in (0, cell - 1))
        + (y % cell in (0, cell - 1))
        + (z % cell in (0, cell - 1))
    ) >= 2

    return EVIE_PURPLE if onSeam else CHARLIE_TEAL


def build(iterations):
    size = 3 ** iterations
    cell = size // 3

    voxels = []

    for z in range(size):
        for y in range(size):
            for x in range(size):
                if is_solid(x, y, z, iterations):
                    voxels.append((x, y, z, material_at(x, y, z, size, cell)))

    return size, voxels


size, voxels = build(ITERATIONS)
path = os.path.join(OUT, "menger_%d.vox" % ITERATIONS)

write_vox(path, (size, size, size), voxels, PALETTE)

print("%s  %d^3  %d voxels" % (path, size, len(voxels)))

for metres in CUBE_SIZES:
    print("  %.2f m cube  %.1f mm per voxel  %.0f voxels per metre"
          % (metres, metres / size * 1000.0, size / metres))
