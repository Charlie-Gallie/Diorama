"""Generates assets/models/test/density_*.vox.

One scene of curved, colourful shapes resampled at several voxel densities. Every shape is
rasterised analytically at each density rather than upscaled, so the files only differ in
resolution and the grid proportions match exactly. That's what makes switching between them
a fair test of density on its own.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from voxfile import Palette, output_directory, write_vox

OUT = output_directory()

# ---------------------------------------------------------------- palette
PALETTE = Palette()

PALETTE.set(1, (152, 154, 162))   # floor light
PALETTE.set(2, (108, 110, 118))   # floor dark
PALETTE.set(3, (236, 236, 240))   # white
PALETTE.set(4, ( 58,  60,  68))   # near black
PALETTE.set(5, (198,  58,  52))   # red
PALETTE.set(6, (242, 228, 196))   # cream
PALETTE.set(7, (122,  86,  58))   # wood
PALETTE.set(8, (236, 190,  80))   # yellow

BLUE, GREEN, FIRE, PINK, WARM = 16, 32, 48, 64, 80
RAMP = 16
PALETTE.ramp(BLUE,  RAMP, ( 38,  68, 158), (142, 206, 246))
PALETTE.ramp(GREEN, RAMP, ( 24,  94,  70), (152, 220, 140))
PALETTE.ramp(FIRE,  RAMP, (148,  34,  30), (250, 192,  92))
PALETTE.ramp(PINK,  RAMP, ( 72,  30, 112), (240, 132, 216))
PALETTE.ramp(WARM,  RAMP, ( 88,  80,  70), (250, 246, 236))

# ---------------------------------------------------------------- scene, in metres
SPAN_X, SPAN_Y, SPAN_Z = 6.2, 3.5, 1.8
FLOOR = 0.1

def hash2(a, b):
    h = (a * 73856093) ^ (b * 19349663)
    return (h % 1013) / 1013.0

def build(vpm):
    sx, sy, sz = round(SPAN_X * vpm), round(SPAN_Y * vpm), round(SPAN_Z * vpm)
    grid = {}

    def fill(lo, hi, fn):
        """Set every voxel whose centre is inside the metre-space box and accepted by fn"""
        rng = []
        for axis, count in enumerate((sx, sy, sz)):
            first = max(0, int(math.floor(lo[axis] * vpm)))
            last = min(count - 1, int(math.ceil(hi[axis] * vpm)))
            rng.append(range(first, last + 1))
        for iz in rng[2]:
            pz = (iz + 0.5) / vpm
            for iy in rng[1]:
                py = (iy + 0.5) / vpm
                for ix in rng[0]:
                    colour = fn((ix + 0.5) / vpm, py, pz)
                    if colour:
                        grid[(ix, iy, iz)] = colour

    # Chequered every metre, so the scale of everything standing on it is readable
    fill((0, 0, 0), (SPAN_X, SPAN_Y, FLOOR),
         lambda px, py, pz: 1 if (int(px) + int(py)) % 2 == 0 else 2)

    def sphere(cx, cy, r, start):
        cz = FLOOR + r
        def f(px, py, pz):
            if math.dist((px, py, pz), (cx, cy, cz)) > r:
                return None
            return Palette.pick(start, RAMP, (pz - cz + r) / (2 * r))
        fill((cx - r, cy - r, cz - r), (cx + r, cy + r, cz + r), f)

    def torus(cx, cy, major, minor, start):
        cz = FLOOR + major + minor
        def f(px, py, pz):
            radial = math.hypot(px - cx, pz - cz) - major
            if radial * radial + (py - cy) ** 2 > minor * minor:
                return None
            angle = math.atan2(pz - cz, px - cx)
            return Palette.pick(start, RAMP, (angle + math.pi) / (2 * math.pi))
        fill((cx - major - minor, cy - minor, cz - major - minor),
             (cx + major + minor, cy + minor, cz + major + minor), f)

    def vase(cx, cy, height, start):
        # A bulb low down, a narrow neck and a flared lip, so the curvature changes sign twice.
        # That's where a low density loses the shape first.
        def profile(u):
            return (0.085
                    + 0.200 * math.exp(-((u - 0.30) / 0.24) ** 2)
                    + 0.075 * math.exp(-((u - 0.97) / 0.07) ** 2))
        def f(px, py, pz):
            u = (pz - FLOOR) / height
            if u < 0.0 or u > 1.0:
                return None
            if math.hypot(px - cx, py - cy) > profile(u):
                return None
            return Palette.pick(start, RAMP, u)
        fill((cx - 0.4, cy - 0.4, FLOOR), (cx + 0.4, cy + 0.4, FLOOR + height), f)

    def cone(cx, cy, radius, height, start):
        def f(px, py, pz):
            u = (pz - FLOOR) / height
            if u < 0.0 or u > 1.0:
                return None
            if math.hypot(px - cx, py - cy) > radius * (1.0 - u):
                return None
            return Palette.pick(start, RAMP, 1.0 - u)
        fill((cx - radius, cy - radius, FLOOR), (cx + radius, cy + radius, FLOOR + height), f)

    def rounded_box(cx, cy, half, radius, colour):
        cz = FLOOR + half
        inner = half - radius
        def f(px, py, pz):
            q = (abs(px - cx) - inner, abs(py - cy) - inner, abs(pz - cz) - inner)
            outside = math.sqrt(sum(max(v, 0.0) ** 2 for v in q))
            if outside + min(max(q), 0.0) - radius > 0.0:
                return None
            return colour
        fill((cx - half, cy - half, cz - half), (cx + half, cy + half, cz + half), f)

    def twisted_column(cx, cy, half, height, turns, start):
        def f(px, py, pz):
            u = (pz - FLOOR) / height
            if u < 0.0 or u > 1.0:
                return None
            angle = u * turns * 2.0 * math.pi
            dx, dy = px - cx, py - cy
            rx = dx * math.cos(angle) - dy * math.sin(angle)
            ry = dx * math.sin(angle) + dy * math.cos(angle)
            if max(abs(rx), abs(ry)) > half:
                return None
            return Palette.pick(start, RAMP, u)
        reach = half * 1.5
        fill((cx - reach, cy - reach, FLOOR), (cx + reach, cy + reach, FLOOR + height), f)

    def capsule(a, b, radius, start):
        ab = tuple(b[i] - a[i] for i in range(3))
        lengthSquared = sum(v * v for v in ab)
        def f(px, py, pz):
            p = (px, py, pz)
            t = sum((p[i] - a[i]) * ab[i] for i in range(3)) / lengthSquared
            t = min(max(t, 0.0), 1.0)
            near = tuple(a[i] + ab[i] * t for i in range(3))
            if math.dist(p, near) > radius:
                return None
            return Palette.pick(start, RAMP, t)
        lo = tuple(min(a[i], b[i]) - radius for i in range(3))
        hi = tuple(max(a[i], b[i]) + radius for i in range(3))
        fill(lo, hi, f)

    def mushroom(cx, cy, capRadius, stalkRadius, stalkHeight):
        capZ = FLOOR + stalkHeight
        def f(px, py, pz):
            dx, dy = px - cx, py - cy
            if pz >= capZ:
                ex = dx / capRadius
                ey = dy / capRadius
                ez = (pz - capZ) / (capRadius * 0.85)
                if ex * ex + ey * ey + ez * ez <= 1.0:
                    # Spots are keyed to metre-space cells, so they come out the same
                    # physical size at every density
                    if hash2(int(px / 0.13), int(py / 0.13)) < 0.28:
                        return 6
                    return 5
                return None
            u = (pz - FLOOR) / stalkHeight
            if math.hypot(dx, dy) <= stalkRadius * (1.0 - 0.25 * u):
                return 6
            return None
        fill((cx - capRadius, cy - capRadius, FLOOR),
             (cx + capRadius, cy + capRadius, capZ + capRadius), f)

    def reference_cube(cx, cy):
        half = 0.5
        cz = FLOOR + half
        def f(px, py, pz):
            if max(abs(px - cx), abs(py - cy), abs(pz - cz)) > half:
                return None
            # Quartered every 25 cm on all faces, so one metre is unmistakable
            cells = (int((px - cx + half) * 4)
                     + int((py - cy + half) * 4)
                     + int((pz - cz + half) * 4))
            return 8 if cells % 2 == 0 else 4
        fill((cx - half, cy - half, cz - half), (cx + half, cy + half, cz + half), f)

    ROW_A, ROW_B = 1.0, 2.5

    sphere(0.8, ROW_A, 0.45, BLUE)
    torus(1.9, ROW_A, 0.40, 0.13, GREEN)
    vase(3.0, ROW_A, 1.15, GREEN)
    cone(4.1, ROW_A, 0.42, 0.95, FIRE)

    rounded_box(0.8, ROW_B, 0.40, 0.16, 8)
    twisted_column(1.9, ROW_B, 0.17, 1.40, 0.75, PINK)
    capsule((2.65, ROW_B - 0.35, FLOOR + 0.18), (3.35, ROW_B + 0.35, FLOOR + 0.62), 0.16, WARM)
    mushroom(4.1, ROW_B, 0.36, 0.11, 0.42)

    reference_cube(5.65, 1.75)

    voxels = [(x, y, z, c) for (x, y, z), c in grid.items()]
    return (sx, sy, sz), voxels

for vpm in (40, 30, 20, 10):
    size, voxels = build(vpm)
    write_vox(f"{OUT}/density_{vpm}.vox", size, voxels, PALETTE)
