"""Minimal MagicaVoxel .vox writer, enough for the generated test models.

The format is RIFF-like, a MAIN chunk with SIZE, XYZI and RGBA inside it. Two things
catch people out. Palette index i in XYZI reads RGBA[i - 1], so index 1 is the first
colour and index 0 means air. Coordinates are single bytes, which caps every axis at
256 voxels.
"""

import os
import struct

MAX_AXIS = 256


def chunk(identifier, content=b"", children=b""):
    return identifier + struct.pack("<ii", len(content), len(children)) + content + children


class Palette:
    """The 255 usable colours, indexed the way a .vox file indexes them"""

    def __init__(self):
        self.entries = [(0, 0, 0, 0)] * 255

    def set(self, index, rgb):
        self.entries[index - 1] = (
            int(round(rgb[0])),
            int(round(rgb[1])),
            int(round(rgb[2])),
            255,
        )

    def ramp(self, start, count, first, last):
        """A run of count indices fading from first to last"""
        for step in range(count):
            t = step / (count - 1)
            self.set(start + step, tuple(first[i] + (last[i] - first[i]) * t for i in range(3)))

    @staticmethod
    def pick(start, count, t):
        """The index t of the way through a ramp, with t clamped to 0..1"""
        t = min(max(t, 0.0), 1.0)
        return start + min(count - 1, int(t * count))


def write_vox(path, size, voxels, palette):
    """voxels is a sequence of (x, y, z, paletteIndex) in MagicaVoxel's Z-up space"""
    sx, sy, sz = size

    if max(size) > MAX_AXIS:
        raise ValueError(f"axis over {MAX_AXIS}: {size}")

    body = chunk(b"SIZE", struct.pack("<iii", sx, sy, sz))

    xyzi = struct.pack("<i", len(voxels))

    for x, y, z, index in voxels:
        xyzi += struct.pack("<BBBB", x, y, z, index)

    body += chunk(b"XYZI", xyzi)

    rgba = b""

    for i in range(256):
        r, g, b, a = palette.entries[i] if i < len(palette.entries) else (0, 0, 0, 0)
        rgba += struct.pack("<BBBB", r, g, b, a)

    body += chunk(b"RGBA", rgba)

    data = b"VOX " + struct.pack("<i", 150) + chunk(b"MAIN", b"", body)

    os.makedirs(os.path.dirname(path), exist_ok=True)

    with open(path, "wb") as handle:
        handle.write(data)

    name = os.path.basename(path)
    print(f"{name:>18}  {sx:>3}x{sy:>3}x{sz:<3}  {len(voxels):>8} voxels  {len(data) / 1024:>8.0f} KB")


def output_directory():
    """assets/models/test, resolved from this file rather than the working directory"""
    return os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "models", "test"))
