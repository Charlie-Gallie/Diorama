"""Generates assets/models/test/worldpatch.vox and ground_0/1/2.vox.

A mock-up of the agreed world design, before any of it is built for real: a 6 x 6 m patch of
terrain laid out as 0.5 m blocks, each filled with voxel content at the world density of 40
per metre, so a block is 20 voxels across.

The ground tiles exist because a .vox axis stops at 256 voxels, which is 6.4 m - so filling a
view means instancing. Three of them, picked and turned per instance, because shared block
content hashes on a block's position inside its own model and every copy would otherwise be
identical. That is the repetition problem the real thing has too, and the same two answers
apply: several variants, and a rotation.

It imitates the real scheme rather than drawing a picture of it. Block content is rasterised
once per type and variant and shared by every block using it, and a block picks its variant
and quarter turn from a hash of its own position - so any tiling visible here is tiling the
real thing would have too.

Every material carries its detail in voxel height as well as colour, because relief is what the
shading can actually work with - the ambient occlusion and the shadow ray turn a carved surface
into something that reads, where a painted one stays flat.

Detail is carried by shape and by rules, never by random colour. A voxel with a random tone is
grain at a camera where one voxel is about one pixel; a voxel that is part of a blade, a cobble
or a course of stone is read as that thing instead. So every material here has real geometry at
voxel scale, and its colour is a function of that geometry - height up a blade, which stone of
the wall it belongs to - rather than a hash thrown at each voxel.

Anything that has to line up across blocks has to survive a quarter turn about the vertical,
since that is how a block picks its orientation. Horizontal courses do; vertical joints do not,
which is why they are staggered and read as masonry rather than as a mistake.
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


# The flat ground surface, as a block layer. Everything below it is fill
GROUND = 2

PALETTE = Palette()

GRASS, GRASS_DRY, DIRT, STONE, PATH, WOOD, LEAF = 8, 16, 24, 32, 40, 48, 56
RAMP = 8

# Narrow ranges on purpose. The ramps exist to shade a shape, not to decorate a flat surface,
# so a wide spread would be read as noise however carefully it was placed.
#
# Grass is the exception and has two, because it is the one material here carrying its detail in
# colour rather than in relief. Lush and dry are close enough to read as the same ground and far
# enough apart to read as patches of it, and a block picks between them by its variant
PALETTE.ramp(GRASS, RAMP, (62, 96, 48), (108, 146, 74))
PALETTE.ramp(GRASS_DRY, RAMP, (88, 100, 54), (134, 144, 80))
PALETTE.ramp(DIRT, RAMP, (98, 78, 58), (128, 104, 78))
PALETTE.ramp(STONE, RAMP, (108, 108, 114), (152, 152, 158))
PALETTE.ramp(PATH, RAMP, (128, 120, 106), (172, 164, 148))
PALETTE.ramp(WOOD, RAMP, (76, 56, 40), (106, 82, 58))
PALETTE.ramp(LEAF, RAMP, (42, 76, 40), (88, 128, 62))

AIR = 0
B_GRASS, B_DIRT, B_STONE, B_PATH, B_WOOD, B_LEAF, B_ROCK = 1, 2, 3, 4, 5, 6, 7

RAMP_BASE = {
    B_GRASS: GRASS,
    B_DIRT: DIRT,
    B_STONE: STONE,
    B_PATH: PATH,
    B_WOOD: WOOD,
    B_LEAF: LEAF,
    B_ROCK: STONE,
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
        blocks[(bx, ground_height(bx, bz) + 1, bz)] = B_ROCK

    return blocks


# The soil a grass or path block sits on, as a voxel height inside the block
SOIL = 13


def face_depth(lx, ly, lz):
    """How far a voxel is from the nearest face of its block.

    Relief is carved from the outside in, so this is how much room there is to cut away. A block
    is 20 voxels thick and nothing here cuts deeper than 3, so carving can never make a wall see
    through, and where two blocks meet the gap it leaves is sealed by both of them being solid
    further in. That is the whole reason this is safe to do blind, without knowing which faces
    of a block are actually exposed.
    """
    return min(lx, BLOCK - 1 - lx, ly, BLOCK - 1 - ly, lz, BLOCK - 1 - lz)


def dirt_at(lx, ly, lz, variant):
    """Earth, crumbling at a cut face rather than slicing clean"""
    lump = mix(lx // 2, ly // 2, lz // 2 + variant, 31)

    if face_depth(lx, ly, lz) < lump % 3:
        return AIR

    return DIRT + lump % RAMP

# The turf surface, flat across every block. Deliberately one height rather than a range: with
# almost no relief left to hide a seam, any per block variation in the surface would step at the
# 0.5 m grid and draw it
TURF = SOIL + 4


def grass_at(lx, ly, lz, variant):
    """Turf carrying its detail in colour rather than in blades standing up.

    The other materials here are carved, and the shading does the rest. This one is the opposite
    experiment: a nearly flat surface, with three scales of colour laid over it.

    Scale is the whole difficulty, and it is tighter than it looks. Block content is shared by
    every block using it, so a pattern only disappears if it is small enough that its repeat at
    the 0.5 m grid cannot be read. Putting the lush and dry choice on the variant was tried first
    and was hopeless - a whole block of one tone against a whole block of the other draws the
    grid in a way no amount of fine detail covers. So every scale here, hue included, is a few
    voxels across, and broad variation is simply given up on. At this block size it has to be.
    """
    if ly <= SOIL:
        return dirt_at(lx, ly, lz, variant)

    # Single voxels lifted a step above the turf, in small clumps rather than one at a time. At
    # 2.5 cm that is just enough for the occlusion and the shadow ray to find, without a surface
    # continuous enough to show a seam where two blocks meet
    tussock = mix(lx // 2, lz // 2, variant, 11) % 6 == 0
    top = TURF + (1 if tussock else 0)

    if ly > top:
        return AIR

    # The blade is the unit everything else hangs off. Two voxels wide so it survives at one
    # voxel per pixel, ten long so it reads as lying in a direction, and aligned to one axis -
    # a quarter turn lays it the other way in the next block, the same trick the masonry courses
    # use, which is what stops the field becoming corduroy.
    #
    # Earlier attempts put tone, shade and hue on their own hashes at four and three voxels. All
    # of them were finer than the blade, so they chopped it up and what was left read as grain.
    # One scale, with everything agreeing on it, is what makes this a surface rather than noise
    column = lx // 2

    # Staggered along their own direction, so the ends of neighbouring blades do not line up into
    # a rank across the block. Unstaggered this came out as woven fabric rather than as grass -
    # the same reason the masonry staggers its vertical joints
    bladeHash = mix(column, (lz + column * 3) // 7, variant, 23)

    # Separate bits of the same hash, so a blade is one thing: its own tone, its own colour
    tone = 2 + (bladeHash >> 5) % 4

    # A dried blade among the green ones rather than a straw patch. Keyed to the blade too, so
    # the hue change has a shape instead of being a dot sitting on top of one
    base = GRASS_DRY if bladeHash % 6 == 0 else GRASS

    # The lifted voxels catch the light, the same role the blade tips used to play
    if tussock and ly == top:
        tone += 2

    # Below the surface it darkens, so a cut edge beside the path reads as thatch under the
    # blades instead of as more lawn turned on its side
    if ly < TURF:
        tone -= 3

    return base + min(max(tone, 0), RAMP - 1)


def stone_at(lx, ly, lz, variant):
    """Masonry: courses of stone with a darker joint between them.

    Course height is fixed rather than varied, so courses line up across blocks - a quarter turn
    about the vertical leaves the height alone. The vertical joints do move, which is what makes
    the coursing read as staggered instead of as a grid.
    """
    COURSE = 7

    course = ly // COURSE
    offset = (course % 2) * 3
    depth = face_depth(lx, ly, lz)

    # Joints run on all three axes, so whichever face is exposed shows coursing and the lines
    # carry round a corner
    joint = ly % COURSE == 0 or (lx + offset) % COURSE == 0 or (lz + offset) % COURSE == 0

    if joint:
        # Mortar sits back from the stones it holds, which is the shadow line that makes a wall
        # read as built rather than printed
        return AIR if depth < 2 else STONE

    stone = mix((lx + offset) // COURSE, course, (lz + offset) // COURSE + variant, 53)

    # Each stone is dressed a little differently, so the face is not a single plane
    if depth < stone % 2:
        return AIR

    # One tone per stone, so a stone is a single thing rather than a patch of speckle
    return STONE + 2 + (stone >> 5) % (RAMP - 2)


def rock_at(lx, ly, lz, variant):
    """Rough stone, for boulders. Masonry would be wrong on something nobody built"""
    lump = mix(lx // 2, ly // 2, lz // 2 + variant, 83)

    # Cut back by up to three voxels, so a boulder is lumpy all over rather than a chipped crate
    if face_depth(lx, ly, lz) < lump % 4:
        return AIR

    return STONE + 1 + (lump >> 5) % (RAMP - 1)


def path_at(lx, ly, lz, variant):
    """Cobbles bedded in earth, each sitting at its own height"""
    if ly < BLOCK - 4:
        return dirt_at(lx, ly, lz, variant)

    cobble = mix(lx // 4, lz // 4, variant, 41)

    # The gaps are cut down to the bedding rather than drawn darker, so they catch a shadow
    if lx % 4 == 0 or lz % 4 == 0:
        return AIR if ly >= BLOCK - 2 else dirt_at(lx, ly, lz, variant)

    if ly > BLOCK - 1 - cobble % 2:
        return AIR

    return PATH + 1 + (cobble >> 5) % (RAMP - 1)


def leaf_at(lx, ly, lz, variant):
    """Clumps of foliage with light coming through the gaps between them"""
    cx, cy, cz = lx // 3, ly // 3, lz // 3
    cluster = mix(cx, cy, cz + variant, 61)

    if cluster % 4 == 0:
        return AIR

    # Thinned at its own edges, so a clump is ragged instead of a little cube of leaves
    if cluster % 5 == 1 and (lx % 3 == 0 or ly % 3 == 0 or lz % 3 == 0):
        return AIR

    return LEAF + (cluster >> 5) % RAMP


def wood_at(lx, ly, lz, variant):
    """Bark ridged up the trunk, with the grooves cut rather than painted"""
    ridge = mix(lx, lz, variant, 71)

    if face_depth(lx, ly, lz) < ridge % 2:
        return AIR

    return WOOD + ridge % RAMP


def content(blockType, lx, ly, lz, variant):
    """The material at one voxel inside a block, in the block's own local space"""
    if blockType == B_GRASS:
        return grass_at(lx, ly, lz, variant)

    if blockType == B_PATH:
        return path_at(lx, ly, lz, variant)

    if blockType == B_STONE:
        return stone_at(lx, ly, lz, variant)

    if blockType == B_ROCK:
        return rock_at(lx, ly, lz, variant)

    if blockType == B_LEAF:
        return leaf_at(lx, ly, lz, variant)

    if blockType == B_WOOD:
        return wood_at(lx, ly, lz, variant)

    return dirt_at(lx, ly, lz, variant)

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


def build(blocks, library):
    width = PLAN * BLOCK
    height = (max(by for _, by, _ in blocks) + 1) * BLOCK
    last = BLOCK - 1

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


def tile_layout(kind):
    """A plain 6 m square of ground, with something standing on it for two of the three"""
    blocks = {}

    for bx in range(PLAN):
        for bz in range(PLAN):
            blocks[(bx, 0, bz)] = B_GRASS

    if kind == 1:
        for bx, bz in ((4, 5), (5, 5), (5, 6), (4, 6), (6, 5)):
            blocks[(bx, 1, bz)] = B_ROCK

        blocks[(5, 2, 5)] = B_ROCK

    if kind == 2:
        trunkX, trunkZ = 6, 6
        trunkTop = 6

        for by in range(1, trunkTop + 1):
            blocks[(trunkX, by, trunkZ)] = B_WOOD

        for by in range(trunkTop - 1, trunkTop + 3):
            spread = 2 if by <= trunkTop else 1

            for dx in range(-spread, spread + 1):
                for dz in range(-spread, spread + 1):
                    if abs(dx) == spread and abs(dz) == spread:
                        continue

                    at = (trunkX + dx, by, trunkZ + dz)

                    if inside(*at) and at not in blocks:
                        blocks[at] = B_LEAF

    return blocks


def write(name, blocks, library):
    size, voxels = build(blocks, library)
    path = os.path.join(OUT, name + ".vox")

    write_vox(path, size, voxels, PALETTE)

    return size


def main():
    # Rasterised once and shared by every file, which is the point of the scheme
    library = patterns()

    write("worldpatch", layout(), library)

    for kind in range(3):
        write("ground_%d" % kind, tile_layout(kind), library)

    print("%.1f m square, %.2f m blocks of %d voxels, %d per metre"
          % (PLAN * BLOCK / DENSITY, BLOCK / DENSITY, BLOCK, DENSITY))
    print("worldpatch ground sits %.2f m above its base, a tile %.2f m above its own"
          % ((GROUND + 1) * BLOCK / DENSITY, BLOCK / DENSITY))


if __name__ == "__main__":
    main()
