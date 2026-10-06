"""Generates assets/models/test/worldpatch.vox.

A mock-up of the agreed world design, before any of it is built for real: a 6 x 6 m patch of
terrain laid out as 0.5 m blocks, each filled with voxel content at the world density of 40
per metre, so a block is 20 voxels across.

It imitates the real scheme rather than drawing a picture of it. Block content is rasterised
once per type and variant and shared by every block using it, and a block picks its variant
and quarter turn from a hash of its own position - so any tiling visible here is tiling the
real thing would have too.

Surface colour varies per 5 voxel cell rather than per voxel. Per-voxel variation reads as
grain at a camera where one voxel is about one pixel, which is the range this game is played
at, so the variation has to be coarse enough to survive as shape rather than as noise.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from voxfile import Palette, output_directory, write_vox

OUT = output_directory()

DENSITY = 40          # voxels per metre
BLOCK = 20            # voxels per block, so blocks are 0.5 m
PLAN = 12             # blocks across, so a 6 m square
LAYERS = 12           # blocks tall, so 6 m

# Colour varies over this many voxels. Smaller reads as grain, larger as flat paint
CELL = 5

# The flat ground surface, as a block layer. Everything below it is fill
GROUND = 2

PALETTE = Palette()

GRASS, DIRT, STONE, PATH, WOOD, LEAF = 8, 20, 32, 44, 56, 68
RAMP = 6

PALETTE.ramp(GRASS, RAMP, (74, 108, 50), (112, 148, 72))
PALETTE.ramp(DIRT, RAMP, (74, 58, 44), (96, 76, 58))
PALETTE.ramp(STONE, RAMP, (118, 118, 124), (148, 148, 154))
PALETTE.ramp(PATH, RAMP, (140, 130, 112), (166, 156, 138))
PALETTE.ramp(WOOD, RAMP, (84, 62, 44), (108, 82, 58))
PALETTE.ramp(LEAF, RAMP, (46, 84, 44), (84, 124, 58))

AIR = 0
B_GRASS, B_DIRT, B_STONE, B_PATH, B_WOOD, B_LEAF = 1, 2, 3, 4, 5, 6

RAMP_BASE = {
    B_GRASS: GRASS,
    B_DIRT: DIRT,
    B_STONE: STONE,
    B_PATH: PATH,
    B_WOOD: WOOD,
    B_LEAF: LEAF,
}


def mix(a, b, c, salt):
    """A cheap integer hash, deterministic so regeneration is byte identical"""
    h = (a * 73856093) ^ (b * 19349663) ^ (c * 83492791) ^ (salt * 2654435761)
    h &= 0xFFFFFFFF
    h ^= h >> 13
    h = (h * 1274126177) & 0xFFFFFFFF

    return h ^ (h >> 16)


def inside(bx, by, bz):
    """Blocks off the edge of the patch are dropped, so a tree near the boundary is cut off"""
    return 0 <= bx < PLAN and 0 <= by < LAYERS and 0 <= bz < PLAN


def ground_height(bx, bz):
    """Top block layer of the ground, as a mound in one corner rather than a flat plane"""
    dx = bx - 10
    dz = bz - 1
    rise = max(0, 3 - (dx * dx + dz * dz) // 2)

    return GROUND + min(2, rise)


def layout():
    """The block world, as (bx, by, bz) -> block type"""
    blocks = {}

    for bx in range(PLAN):
        for bz in range(PLAN):
            # A path two blocks wide, cut flat through whatever the ground is doing
            onPath = bz in (5, 6)
            top = GROUND if onPath else ground_height(bx, bz)

            for by in range(top + 1):
                if by == top:
                    blocks[(bx, by, bz)] = B_PATH if onPath else B_GRASS
                else:
                    blocks[(bx, by, bz)] = B_DIRT

    # A hut with a doorway and a window, walls five blocks up from the ground. Importing a
    # .vox mirrors the depth axis, so a high bz here ends up far from a camera looking along
    # negative z - which is where a tall thing has to be if it is not to block the view
    for bx in range(1, 6):
        for bz in range(8, 12):
            if not (bx in (1, 5) or bz in (8, 11)):
                continue

            for by in range(GROUND + 1, GROUND + 6):
                doorway = bz == 8 and bx in (2, 3) and by <= GROUND + 4
                window = bx == 1 and bz == 10 and by in (GROUND + 3, GROUND + 4)

                if not doorway and not window:
                    blocks[(bx, by, bz)] = B_STONE

    # A tree on the flat ground, trunk one block thick
    trunkX, trunkZ = 7, 9
    trunkTop = GROUND + 5

    for by in range(GROUND + 1, trunkTop + 1):
        blocks[(trunkX, by, trunkZ)] = B_WOOD

    for by in range(trunkTop - 1, trunkTop + 3):
        spread = 2 if by <= trunkTop else 1

        for dx in range(-spread, spread + 1):
            for dz in range(-spread, spread + 1):
                # Corners left off, so the canopy is round rather than a cube
                if abs(dx) == spread and abs(dz) == spread:
                    continue

                at = (trunkX + dx, by, trunkZ + dz)

                if inside(*at) and at not in blocks:
                    blocks[at] = B_LEAF

    # A few boulders, so the open ground is not empty
    for bx, bz in ((6, 3), (2, 4), (11, 7)):
        blocks[(bx, ground_height(bx, bz) + 1, bz)] = B_STONE

    return blocks


def content(blockType, lx, ly, lz, variant):
    """The material at one voxel inside a block, in the block's own local space.

    Everything varies per cell rather than per voxel, so a surface reads as patches of colour
    rather than as static.
    """
    cx, cy, cz = lx // CELL, ly // CELL, lz // CELL

    if blockType == B_GRASS:
        # A clumpy top rather than a flat one, varied per cell so it is shape and not speckle
        top = BLOCK - 1 - mix(cx, cz, variant, 11) % 2

        if ly > top:
            return AIR

        if ly >= top - 2:
            return GRASS + mix(cx, cz, variant, 23) % RAMP

        return DIRT + mix(cx, cy, cz + variant, 31) % RAMP

    if blockType == B_PATH:
        if ly >= BLOCK - 3:
            return PATH + mix(cx, cz, variant, 41) % RAMP

        return DIRT + mix(cx, cy, cz + variant, 31) % RAMP

    if blockType == B_LEAF:
        # Gaps on the cell grid, so a canopy reads as clumps with light coming through it
        if mix(cx, cy, cz + variant, 61) % 5 == 0:
            return AIR

        return LEAF + mix(cx, cy, cz + variant, 67) % RAMP

    return RAMP_BASE[blockType] + mix(cx, cy, cz + variant, 53) % RAMP


def patterns():
    """Every block type and variant rasterised once, as lists of non-air voxels.

    This is the shape the real thing takes too - block content is generated once per type and
    shared by every block using it, which is the whole reason terrain is cheap.
    """
    built = {}

    for blockType in RAMP_BASE:
        for variant in range(4):
            filled = []

            for ly in range(BLOCK):
                for lz in range(BLOCK):
                    for lx in range(BLOCK):
                        material = content(blockType, lx, ly, lz, variant)

                        if material != AIR:
                            filled.append((lx, ly, lz, material))

            built[(blockType, variant)] = filled

    return built


def build():
    width = PLAN * BLOCK
    height = LAYERS * BLOCK
    last = BLOCK - 1

    library = patterns()
    blocks = layout()
    voxels = []

    for (bx, by, bz), blockType in blocks.items():
        seed = mix(bx, by, bz, 7)
        variant = seed % 4
        turns = (seed >> 3) % 4

        originX = bx * BLOCK
        originY = by * BLOCK
        originZ = bz * BLOCK

        # Quarter turns applied as the pattern is placed, so one variant covers four
        # orientations without storing four copies of it. A .vox file is Z up, so the third
        # axis is height
        filled = library[(blockType, variant)]

        if turns == 0:
            voxels.extend((originX + x, originZ + z, originY + y, m) for x, y, z, m in filled)
        elif turns == 1:
            voxels.extend((originX + z, originZ + last - x, originY + y, m) for x, y, z, m in filled)
        elif turns == 2:
            voxels.extend((originX + last - x, originZ + last - z, originY + y, m) for x, y, z, m in filled)
        else:
            voxels.extend((originX + last - z, originZ + x, originY + y, m) for x, y, z, m in filled)

    return (width, width, height), voxels


def main():
    size, voxels = build()
    path = os.path.join(OUT, "worldpatch.vox")

    write_vox(path, size, voxels, PALETTE)

    print("%.1f m square, %.2f m blocks of %d voxels, %d per metre"
          % (PLAN * BLOCK / DENSITY, BLOCK / DENSITY, BLOCK, DENSITY))
    print("flat ground surface sits %.2f m above the base"
          % ((GROUND + 1) * BLOCK / DENSITY))


if __name__ == "__main__":
    main()
