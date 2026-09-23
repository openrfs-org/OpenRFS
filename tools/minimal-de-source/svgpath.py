#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Turn the shapes in an SVG into polylines on its own grid.

This is the half of the icon pipeline that has nothing to do with pixels:
path data in, points out. Two generators need it and neither should own it -
make-glyphs.py, which STROKES what comes back, and make-shell-icons.py,
which FILLS it.

Everything is flattened here: arcs into line segments by the endpoint-to-centre
conversion in the SVG specification's F.6.5, curves by fixed subdivision,
circles and rounded rectangles into their own polygons. What a caller gets is
always a list of point lists.
"""

import math
import re

# Segments per curve.  Well under one sampling step at every size these are
# rasterized at, so the flattening error never reaches a pixel.
FLATTEN_STEPS = 32
NUMBER = re.compile(r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")


def numbers(text):
    return [float(m.group()) for m in NUMBER.finditer(text)]


def attribute(tag, name):
    match = re.search(rf'\b{name}\s*=\s*"([^"]*)"', tag)
    return match.group(1) if match else None


def flatten_cubic(points, p0, p1, p2, p3):
    for step in range(1, FLATTEN_STEPS + 1):
        t = step / FLATTEN_STEPS
        u = 1.0 - t
        x = (u * u * u * p0[0] + 3 * u * u * t * p1[0] +
             3 * u * t * t * p2[0] + t * t * t * p3[0])
        y = (u * u * u * p0[1] + 3 * u * u * t * p1[1] +
             3 * u * t * t * p2[1] + t * t * t * p3[1])
        points.append((x, y))


def flatten_quadratic(points, p0, p1, p2):
    for step in range(1, FLATTEN_STEPS + 1):
        t = step / FLATTEN_STEPS
        u = 1.0 - t
        points.append((u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
                       u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]))


def flatten_arc(points, start, rx, ry, rotation, large, sweep, end):
    """SVG's endpoint arc, converted to a centre parameterization and walked.

    Straight out of the SVG implementation notes, F.6.5. Lucide uses arcs for
    the Wi-Fi and volume marks, so this is not optional.
    """
    if rx == 0 or ry == 0 or start == end:
        points.append(end)
        return
    rx, ry = abs(rx), abs(ry)
    phi = math.radians(rotation)
    cos_phi, sin_phi = math.cos(phi), math.sin(phi)
    dx2 = (start[0] - end[0]) / 2.0
    dy2 = (start[1] - end[1]) / 2.0
    x1 = cos_phi * dx2 + sin_phi * dy2
    y1 = -sin_phi * dx2 + cos_phi * dy2

    # Scale the radii up if they are too small to span the two endpoints.
    lam = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry)
    if lam > 1.0:
        scale = math.sqrt(lam)
        rx *= scale
        ry *= scale

    numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1
    denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1
    factor = math.sqrt(max(0.0, numerator / denominator)) if denominator else 0.0
    if large == sweep:
        factor = -factor
    cx1 = factor * rx * y1 / ry
    cy1 = -factor * ry * x1 / rx
    cx = cos_phi * cx1 - sin_phi * cy1 + (start[0] + end[0]) / 2.0
    cy = sin_phi * cx1 + cos_phi * cy1 + (start[1] + end[1]) / 2.0

    def angle(ux, uy, vx, vy):
        dot = ux * vx + uy * vy
        length = math.hypot(ux, uy) * math.hypot(vx, vy)
        if length == 0.0:
            return 0.0
        value = max(-1.0, min(1.0, dot / length))
        sign = -1.0 if ux * vy - uy * vx < 0 else 1.0
        return sign * math.acos(value)

    theta = angle(1.0, 0.0, (x1 - cx1) / rx, (y1 - cy1) / ry)
    delta = angle((x1 - cx1) / rx, (y1 - cy1) / ry,
                  (-x1 - cx1) / rx, (-y1 - cy1) / ry)
    if not sweep and delta > 0:
        delta -= 2 * math.pi
    elif sweep and delta < 0:
        delta += 2 * math.pi

    for step in range(1, FLATTEN_STEPS + 1):
        angle_at = theta + delta * step / FLATTEN_STEPS
        px = rx * math.cos(angle_at)
        py = ry * math.sin(angle_at)
        points.append((cos_phi * px - sin_phi * py + cx,
                       sin_phi * px + cos_phi * py + cy))


def parse_path(data):
    """One SVG path into a list of polylines.

    Scanned with a cursor rather than pre-tokenized, because of the arcs.
    SVG's grammar makes an arc's large-arc-flag and sweep-flag SINGLE
    CHARACTERS, and every real-world minifier exploits that: Lucide's zap
    writes `a1.5 1.5 0 00-2.474-1.561`, where `00` is two flags and not the
    number zero. A tokenizer that reads numbers greedily turns that into one
    zero and then shifts every remaining parameter of the path by one, which
    fails loudly if you are lucky and draws nonsense if you are not.
    """
    polylines, current = [], []
    at = (0.0, 0.0)
    start = (0.0, 0.0)
    last_cubic = None
    last_quadratic = None
    index = 0
    command = None
    length = len(data)

    def skip_separators():
        nonlocal index
        while index < length and data[index] in " \t\r\n,":
            index += 1

    def read_number():
        nonlocal index
        skip_separators()
        match = NUMBER.match(data, index)
        if match is None:
            raise ValueError(f"expected a number at {index} in {data!r}")
        index = match.end()
        return float(match.group())

    def read_flag():
        """One character, 0 or 1 - never a general number."""
        nonlocal index
        skip_separators()
        if index >= length or data[index] not in "01":
            raise ValueError(f"expected an arc flag at {index} in {data!r}")
        index += 1
        return data[index - 1] == "1"

    def more_parameters():
        """Whether the current command repeats: a number comes next."""
        nonlocal index
        skip_separators()
        return index < length and NUMBER.match(data, index) is not None

    while True:
        skip_separators()
        if index >= length:
            break
        if data[index].isalpha():
            command = data[index]
            index += 1
            if command in "Zz":
                if current:
                    current.append(start)
                    polylines.append(current)
                    current = []
                at = start
                continue
        elif command is None:
            break
        elif not more_parameters():
            break

        relative = command.islower()
        upper = command.upper()

        if upper == "M":
            x, y = read_number(), read_number()
            if relative:
                x, y = at[0] + x, at[1] + y
            if current:
                polylines.append(current)
            at = start = (x, y)
            current = [at]
            command = "l" if relative else "L"
        elif upper == "L":
            x, y = read_number(), read_number()
            if relative:
                x, y = at[0] + x, at[1] + y
            at = (x, y)
            current.append(at)
        elif upper == "H":
            x = read_number()
            at = (at[0] + x if relative else x, at[1])
            current.append(at)
        elif upper == "V":
            y = read_number()
            at = (at[0], at[1] + y if relative else y)
            current.append(at)
        elif upper == "C":
            x1, y1 = read_number(), read_number()
            x2, y2 = read_number(), read_number()
            x, y = read_number(), read_number()
            if relative:
                x1, y1 = at[0] + x1, at[1] + y1
                x2, y2 = at[0] + x2, at[1] + y2
                x, y = at[0] + x, at[1] + y
            flatten_cubic(current, at, (x1, y1), (x2, y2), (x, y))
            last_cubic = (x2, y2)
            at = (x, y)
        elif upper == "S":
            x2, y2 = read_number(), read_number()
            x, y = read_number(), read_number()
            if relative:
                x2, y2 = at[0] + x2, at[1] + y2
                x, y = at[0] + x, at[1] + y
            first = (2 * at[0] - last_cubic[0], 2 * at[1] - last_cubic[1]) \
                if last_cubic else at
            flatten_cubic(current, at, first, (x2, y2), (x, y))
            last_cubic = (x2, y2)
            at = (x, y)
        elif upper == "Q":
            x1, y1 = read_number(), read_number()
            x, y = read_number(), read_number()
            if relative:
                x1, y1 = at[0] + x1, at[1] + y1
                x, y = at[0] + x, at[1] + y
            flatten_quadratic(current, at, (x1, y1), (x, y))
            last_quadratic = (x1, y1)
            at = (x, y)
        elif upper == "T":
            x, y = read_number(), read_number()
            if relative:
                x, y = at[0] + x, at[1] + y
            control = (2 * at[0] - last_quadratic[0],
                       2 * at[1] - last_quadratic[1]) if last_quadratic else at
            flatten_quadratic(current, at, control, (x, y))
            last_quadratic = control
            at = (x, y)
        elif upper == "A":
            rx, ry, rotation = read_number(), read_number(), read_number()
            large, sweep = read_flag(), read_flag()
            x, y = read_number(), read_number()
            if relative:
                x, y = at[0] + x, at[1] + y
            flatten_arc(current, at, rx, ry, rotation, large, sweep, (x, y))
            at = (x, y)
        else:
            break
        if not current:
            current = [at]

    if current:
        polylines.append(current)
    return polylines


def circle_polyline(cx, cy, rx, ry):
    steps = 96
    return [(cx + rx * math.cos(2 * math.pi * k / steps),
             cy + ry * math.sin(2 * math.pi * k / steps))
            for k in range(steps + 1)]


# Below this many device pixels a corner radius is not drawn at all.  A
# rounded corner and a square one of the same size contain the SAME pixels
# once the radius is under about two of them - but the rounded one arrives at
# them unevenly, because each point of its arc snaps to the grid on its own
# and a one-pixel arc has no room to snap consistently.  Lucide's battery is
# the clearest case: rx="2" is 1.33 pixels at tray size, and it turns a
# rectangle into a shape whose top edge is eleven pixels wide and whose
# bottom edge is nine, which is exactly what "hand drawn" looks like.
MIN_CORNER_PIXELS = 2.0


def hint_radius(radius, size, grid):
    """Quantize a corner radius to whole device pixels, or to none."""
    device = radius * size / grid
    if device < MIN_CORNER_PIXELS:
        return 0.0
    return round(device) * grid / size


def rounded_rect_polyline(x, y, width, height, rx, ry, size, grid):
    rx = hint_radius(rx or ry, size, grid)
    ry = hint_radius(ry or rx, size, grid)
    if rx <= 0 and ry <= 0:
        return [(x, y), (x + width, y), (x + width, y + height),
                (x, y + height), (x, y)]
    rx = rx or ry
    ry = ry or rx
    rx = min(rx, width / 2.0)
    ry = min(ry, height / 2.0)
    points = []
    corners = [
        (x + width - rx, y + ry, -math.pi / 2, 0.0),
        (x + width - rx, y + height - ry, 0.0, math.pi / 2),
        (x + rx, y + height - ry, math.pi / 2, math.pi),
        (x + rx, y + ry, math.pi, 3 * math.pi / 2),
    ]
    for cx, cy, start, end in corners:
        for step in range(17):
            angle = start + (end - start) * step / 16
            points.append((cx + rx * math.cos(angle), cy + ry * math.sin(angle)))
    points.append(points[0])
    return points


VIEWBOX = re.compile(r'viewBox\s*=\s*"([^"]+)"')


def svg_grid(text):
    """The width of the icon's own coordinate system.

    Lucide draws everything on 24.  Microsoft's Fluent icons ship one file per
    size and put 16, 20, 24 or 32 in the viewBox, so assuming 24 silently
    scales two thirds of them wrong.
    """
    match = VIEWBOX.search(text)
    if match is None:
        return 24.0
    values = numbers(match.group(1))
    return values[2] if len(values) >= 4 and values[2] > 0 else 24.0


def parse_shapes(text, size):
    """Every shape in the SVG, in document order, with its own paint.

    A shape is {"polylines", "fill", "stroke", "stroke_width"}: `fill` and
    `stroke` are "#rrggbb", "currentColor" or None. The caller decides what to
    do with that - the Lucide generator strokes everything at a fixed width
    and ignores the paint; the shell-icon generator fills, and needs it.

    Document order matters: these are composited back to front, which is how a
    file icon's coloured band lands on top of its page.
    """
    grid = svg_grid(text)
    root_fill = attribute(text[:text.index(">") + 1], "fill")
    root_stroke = attribute(text[:text.index(">") + 1], "stroke")
    root_width = attribute(text[:text.index(">") + 1], "stroke-width")
    shapes = []
    for match in re.finditer(
            r"<(path|circle|ellipse|rect|line|polyline|polygon)\b([^>]*)>",
            text):
        kind, body = match.group(1), match.group(2)
        polylines = []
        if kind == "path":
            polylines.extend(parse_path(attribute(body, "d")))
        elif kind == "circle":
            cx = float(attribute(body, "cx") or 0)
            cy = float(attribute(body, "cy") or 0)
            r = float(attribute(body, "r") or 0)
            polylines.append(circle_polyline(cx, cy, r, r))
        elif kind == "ellipse":
            cx = float(attribute(body, "cx") or 0)
            cy = float(attribute(body, "cy") or 0)
            rx = float(attribute(body, "rx") or 0)
            ry = float(attribute(body, "ry") or 0)
            polylines.append(circle_polyline(cx, cy, rx, ry))
        elif kind == "rect":
            polylines.append(rounded_rect_polyline(
                float(attribute(body, "x") or 0),
                float(attribute(body, "y") or 0),
                float(attribute(body, "width") or 0),
                float(attribute(body, "height") or 0),
                float(attribute(body, "rx") or 0),
                float(attribute(body, "ry") or 0),
                size, grid))
        elif kind == "line":
            polylines.append([
                (float(attribute(body, "x1") or 0),
                 float(attribute(body, "y1") or 0)),
                (float(attribute(body, "x2") or 0),
                 float(attribute(body, "y2") or 0))])
        else:
            values = numbers(attribute(body, "points") or "")
            shape = list(zip(values[0::2], values[1::2]))
            if kind == "polygon" and shape:
                shape.append(shape[0])
            polylines.append(shape)
        polylines = [p for p in polylines if len(p) >= 2]
        if not polylines:
            continue
        fill = attribute(body, "fill")
        stroke = attribute(body, "stroke")
        width = attribute(body, "stroke-width")
        shapes.append({
            "polylines": polylines,
            "fill": (fill if fill is not None else root_fill),
            "stroke": (stroke if stroke is not None else root_stroke),
            "stroke_width": float(width if width is not None else
                                  (root_width or 1.0)),
            "grid": grid,
        })
    return shapes


def parse_svg(text, size):
    """Every stroked shape in one icon, as flat polylines on its own grid."""
    polylines = []
    for shape in parse_shapes(text, size):
        polylines.extend(shape["polylines"])
    return polylines
