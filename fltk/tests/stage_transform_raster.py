#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fixture-specific, independent PDF raster bounds for the green image."""

import math
from PIL import Image


def verify_image_bounds(path, geometry, dpi=100):
    # The producer uses a translucent green PNG; other shapes/text are red,
    # blue or dark ink. This identifies the printed image independently of
    # the native scene model, including translation, rotation and reflection.
    image = Image.open(path).convert("RGB")
    points = [
        (i % image.width, i // image.width)
        for i, (r, g, b) in enumerate(image.getdata())
        if g > 80 and g > r * 1.5 and b < g * 0.9
    ]
    assert len(points) > 200, "Missing printed image"
    actual = [
        min(x for x, _ in points),
        min(y for _, y in points),
        max(x for x, _ in points) + 1,
        max(y for _, y in points) + 1,
    ]
    x, y, width, height, angle = geometry
    c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    corners = [
        (x + c * a - s * b, y + s * a + c * b)
        for a, b in [(0, 0), (width, 0), (width, height), (0, height)]
    ]
    expected = [
        min(p[0] for p in corners) * dpi / 72,
        min(p[1] for p in corners) * dpi / 72,
        max(p[0] for p in corners) * dpi / 72,
        max(p[1] for p in corners) * dpi / 72,
    ]
    # Pixel coverage/antialiasing can include the pixel at the far edge.
    assert max(abs(a - b) for a, b in zip(actual, expected)) <= 2, (
        path,
        actual,
        expected,
    )
    return dict(actual=actual, expected=expected, tolerance_pixels=2)
