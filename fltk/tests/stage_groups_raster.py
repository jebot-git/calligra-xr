#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Authored red A geometry and clipping oracle for the fixed grouping corpus."""

import math
from PIL import Image


def verify_red_bounds(
    path, action=(1, 0, 0, 1, 0, 0), input_tolerance=1e-7, linear_tolerance=1e-8
):
    a, b, c, d, e, f = action
    corners = [
        (a * x + c * y + e, b * x + d * y + f)
        for x, y in [(40, 60), (220, 60), (220, 180), (40, 180)]
    ]
    expected = [
        max(0, min(p[0] for p in corners)),
        max(0, min(p[1] for p in corners)),
        min(400, max(p[0] for p in corners)),
        min(240, max(p[1] for p in corners)),
    ]
    image = Image.open(path).convert("RGB")
    pixels = image.load()
    # The unchanged N2 also uses red, at310..360pt. Every authored A case
    # lies left of270pt, so exclude that separately verified other object.
    points = [
        (x, y)
        for y in range(image.height)
        for x in range(min(image.width, math.ceil(270 * 100 / 72)))
        if all(abs(got - want) <= 3 for got, want in zip(pixels[x, y], (204, 48, 48)))
    ]
    assert points, "Missing red object A"
    actual = [
        min(p[0] for p in points),
        min(p[1] for p in points),
        max(p[0] for p in points) + 1,
        max(p[1] for p in points) + 1,
    ]
    expected = [v * 100 / 72 for v in expected]
    # Original corners have |x|+|y|<=400pt. Propagate the explicit input and
    # affine tolerances, plus two raster pixels for the transformed stroke edge.
    tolerance = 2 + math.ceil((input_tolerance + 400 * linear_tolerance) * 100 / 72)
    assert all(abs(got - want) <= tolerance for got, want in zip(actual, expected)), (
        str(path),
        actual,
        expected,
        tolerance,
    )
    return dict(actual=actual, expected=expected, tolerance_pixels=tolerance)
