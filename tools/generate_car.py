"""Generates assets/models/test/car_*.vox.

A to-scale hatchback built from signed-distance primitives and sampled analytically at each
density, so the wheels are real circles at every resolution rather than one circle scaled
down. It's here to judge how much voxel density a curved, panelled object we see all the
time actually needs.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from voxfile import Palette, output_directory, write_vox

OUT = output_directory()

# ---------------------------------------------------------------- palette
PALETTE = Palette()

PAINT       = 1   # body colour
PAINT_DARK  = 2   # panel shut lines
ROOF        = 3   # contrast roof
STRIPE      = 4
GLASS       = 5
GLASS_DARK  = 6   # windscreen band, so the rake is readable
TRIM        = 7   # black plastic
BUMPER      = 8
CHROME      = 9
TYRE        = 10
RIM         = 11
HUB         = 12
BRAKE       = 13
LIGHT_FRONT = 14
LIGHT_REAR  = 15
INDICATOR   = 16
GRILLE      = 17
PLATE       = 18
SEAT        = 19
FLOOR_LIGHT = 20
FLOOR_DARK  = 21
EXHAUST     = 22

PALETTE.set(PAINT,       (186,  42,  46))   # a deep red, so shading on each panel reads
PALETTE.set(PAINT_DARK,  (128,  26,  30))
PALETTE.set(ROOF,        ( 32,  34,  40))
PALETTE.set(STRIPE,      (236, 232, 224))
PALETTE.set(GLASS,       ( 96, 122, 138))
PALETTE.set(GLASS_DARK,  ( 62,  84, 100))
PALETTE.set(TRIM,        ( 28,  29,  34))
PALETTE.set(BUMPER,      ( 54,  56,  62))
PALETTE.set(CHROME,      (188, 194, 202))
PALETTE.set(TYRE,        ( 26,  26,  29))
PALETTE.set(RIM,         (176, 182, 190))
PALETTE.set(HUB,         (126, 132, 140))
PALETTE.set(BRAKE,       ( 74,  70,  68))
PALETTE.set(LIGHT_FRONT, (246, 244, 226))
PALETTE.set(LIGHT_REAR,  (196,  34,  34))
PALETTE.set(INDICATOR,   (232, 150,  42))
PALETTE.set(GRILLE,      ( 38,  39,  44))
PALETTE.set(PLATE,       (226, 226, 220))
PALETTE.set(SEAT,        ( 48,  44,  48))
PALETTE.set(FLOOR_LIGHT, (150, 152, 160))
PALETTE.set(FLOOR_DARK,  (108, 110, 118))
PALETTE.set(EXHAUST,     (140, 144, 150))

# ---------------------------------------------------------------- car, in metres
# Roughly a compact hatchback: 4.30 x 1.80 x 1.48, wheelbase 2.62, 0.65 m wheels.
LENGTH, WIDTH, HEIGHT = 4.30, 1.80, 1.50
GROUND = 0.02                    # a thin slab under it, for contact shadow and scale
CENTRE_Y = WIDTH * 0.5

FRONT_AXLE, REAR_AXLE = 0.98, 3.60
WHEEL_RADIUS = 0.325
WHEEL_HALF = 0.105
WHEEL_Y = (0.255, WIDTH - 0.255)

def rounded_box(p, centre, half, radius):
    """Signed distance to a box with rounded corners"""
    q = [abs(p[i] - centre[i]) - (half[i] - radius) for i in range(3)]
    outside = math.sqrt(sum(max(v, 0.0) ** 2 for v in q))
    return outside + min(max(q), 0.0) - radius

def smooth_min(a, b, k):
    h = max(0.0, k - abs(a - b)) / k
    return min(a, b) - h * h * k * 0.25

def body_distance(x, dy, z):
    """dy is the distance from the centreline, so the body gets mirrored for free"""
    p = (x, dy, z)

    # Main hull, sills to shoulder
    d = rounded_box(p, (2.16, 0.0, 0.545), (1.95, 0.890, 0.345), 0.22)

    # Nose, lower than the hull so the bonnet slopes into it
    d = smooth_min(d, rounded_box(p, (0.36, 0.0, 0.660), (0.32, 0.845, 0.175), 0.15), 0.20)

    # Bonnet
    d = smooth_min(d, rounded_box(p, (0.96, 0.0, 0.795), (0.62, 0.865, 0.105), 0.11), 0.18)

    # Tail
    d = smooth_min(d, rounded_box(p, (3.98, 0.0, 0.820), (0.30, 0.860, 0.135), 0.12), 0.18)

    # Cabin, narrower than the body so the shoulders stay visible
    d = smooth_min(d, rounded_box(p, (2.34, 0.0, 1.135), (0.86, 0.735, 0.310), 0.27), 0.16)

    # Bonnet and rear deck rakes, done as half-space cuts. Blended boxes on their own just
    # leave a flat-topped brick, and the slope is most of what makes it read as a car.
    if x < 1.42:
        d = max(d, z - (0.760 + (x - 0.20) * 0.130 / 1.22))

    if x > 3.70:
        d = max(d, z - (0.890 - (x - 3.70) * 0.070 / 0.40))

    return d

def arch_distance(x, z, axle):
    return math.hypot(x - axle, z - 0.335) - 0.435

def wheel_colour(x, y, z, axle, wheelY):
    radial = math.hypot(x - axle, z - WHEEL_RADIUS)
    across = abs(y - wheelY)

    if across > WHEEL_HALF or radial > WHEEL_RADIUS:
        return None

    # Tyre, with a sidewall standing slightly proud of the rim so the step is a real concave
    # lip rather than a painted ring
    if radial > 0.215:
        return TYRE

    if across > WHEEL_HALF - 0.022:
        return None  # the dish, recessed behind the tyre sidewall

    if radial < 0.048:
        return HUB

    # Five spoke openings, showing the brake disc behind
    angle = math.atan2(z - WHEEL_RADIUS, x - axle)
    spoke = (angle % (2.0 * math.pi / 5.0)) - math.pi / 5.0

    if 0.085 < radial < 0.185 and abs(spoke * radial) < 0.042:
        return BRAKE

    return RIM

def build(vpm):
    sx = round(LENGTH * vpm)
    sy = round(WIDTH * vpm)
    sz = round((HEIGHT + GROUND) * vpm)

    grid = {}
    voxel = 1.0 / vpm

    # A whole number of voxels, so the car always sits on the same slab thickness
    groundVoxels = max(1, round(GROUND * vpm))

    for iz in range(sz):
        z = (iz + 0.5) / vpm - groundVoxels * voxel   # 0 is where the wheels touch

        for iy in range(sy):
            y = (iy + 0.5) / vpm
            dy = abs(y - CENTRE_Y)

            for ix in range(sx):
                x = (ix + 0.5) / vpm

                if iz < groundVoxels:
                    grid[(ix, iy, iz)] = FLOOR_LIGHT if (int(x) + int(y)) % 2 == 0 else FLOOR_DARK
                    continue

                colour = None

                for axle, wheelY in ((FRONT_AXLE, WHEEL_Y[0]), (FRONT_AXLE, WHEEL_Y[1]),
                                     (REAR_AXLE, WHEEL_Y[0]), (REAR_AXLE, WHEEL_Y[1])):
                    colour = wheel_colour(x, y, z, axle, wheelY)

                    if colour is not None:
                        break

                if colour is None:
                    colour = body_colour(x, y, dy, z, ix, iy, iz, vpm)

                if colour is not None:
                    grid[(ix, iy, iz)] = colour

    return (sx, sy, sz), [(x, y, z, c) for (x, y, z), c in grid.items()]

def body_colour(x, y, dy, z, ix, iy, iz, vpm):
    if z < 0.145:
        return None   # ground clearance

    if body_distance(x, dy, z) > 0.0:
        return None

    # Wheel arches, cut only outboard so the floor pan survives
    if dy > 0.385:
        for axle in (FRONT_AXLE, REAR_AXLE):
            if arch_distance(x, z, axle) < 0.0:
                return None

    # Cabin is hollow, which puts a real concave interior behind the glass
    cabinInner = body_distance(x, dy, z) < -0.075

    if cabinInner and z > 0.72 and 1.45 < x < 3.15:
        return SEAT if z < 0.95 else None

    # ---- glass ----------------------------------------------------------------
    # Pillars only near the sides, because across the middle of the car these x ranges are the
    # windscreen and rear screen and they need to stay glass
    pillar = dy > 0.42 and ((1.48 <= x <= 1.74) or (2.28 <= x <= 2.44) or (3.02 <= x <= 3.26))
    roofLine = 1.335

    if 0.88 < z < roofLine and 1.36 < x < 3.30 and not pillar:
        # The windscreen and rear screen are raked, so a darker glass picks them out and makes
        # the angle readable
        if x < 1.62 or x > 3.12:
            return GLASS_DARK
        return GLASS

    # ---- roof -----------------------------------------------------------------
    if z >= roofLine and 1.30 < x < 3.20:
        if dy < 0.10:
            return STRIPE
        return ROOF

    # ---- lights ---------------------------------------------------------------
    if x < 0.30 and 0.62 < z < 0.78:
        if dy > 0.60:
            return INDICATOR
        if dy > 0.24:
            return LIGHT_FRONT

    if x > 4.02 and 0.72 < z < 0.92 and dy > 0.30:
        return INDICATOR if dy > 0.70 else LIGHT_REAR

    # ---- front end ------------------------------------------------------------
    if x < 0.24 and 0.44 < z < 0.60 and dy < 0.56:
        return GRILLE

    if x < 0.22 and 0.30 < z < 0.44 and dy < 0.30:
        return PLATE

    if x > 4.06 and 0.44 < z < 0.60 and dy < 0.30:
        return PLATE

    # ---- bumpers and lower trim ------------------------------------------------
    if z < 0.30:
        return TRIM

    if (x < 0.42 or x > 3.94) and z < 0.52:
        return BUMPER

    # ---- exhaust ---------------------------------------------------------------
    if x > 4.00 and z < 0.34 and 0.42 < dy < 0.58:
        return EXHAUST

    # ---- window surround -------------------------------------------------------
    if abs(z - 0.88) < 0.9 / vpm and 1.36 < x < 3.30:
        return CHROME

    # ---- panel shut lines ------------------------------------------------------
    # One voxel wide at any density, so the gaps stay gaps rather than stripes
    for line in (1.44, 2.26, 3.08):
        if ix == round(line * vpm) and z < roofLine:
            return PAINT_DARK

    # Bonnet and hatch, across the car rather than along it
    if z > 0.70 and dy < 0.80:
        for line in (1.34, 3.34):
            if ix == round(line * vpm):
                return PAINT_DARK

    return PAINT

for vpm in (50, 40, 30, 20, 10):
    size, voxels = build(vpm)
    write_vox(f"{OUT}/car_{vpm}.vox", size, voxels, PALETTE)
