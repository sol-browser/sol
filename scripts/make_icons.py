"""Derive Sol's logo assets from the single source icons/logo_sol.svg.

The source is a thin ring with a wave at the bottom (one path: the outer
contour, then the inner contour that forms the ring and the wave). It reads
well large but its hairline ring vanishes below ~48 px, so small sizes (the UI
mark, icons up to 48 px) use a simplified mark: an even, thicker ring and a
deeper S-shaped wave. Larger icons scale the source's inner contour towards
the centre, which thickens the ring and the wave alike.

Outputs (checked in, small):
  icons/sol-mark.svg                  simplified mark, currentColor, 24-unit
                                      framing (UI logo button, Settings)
  icons/sol-logo.svg                  thin logo, square, currentColor (large uses)
  apps/desktop/resources/red.ico      multi-size app icon (exe, shortcuts, uninstaller)
  apps/desktop/resources/red-<n>.png  window/taskbar icon bitmaps loaded at runtime
  apps/installer/src/logo_shape.h     the thin logo's contours for Sol-Web-Installer.exe
Run after replacing icons/logo_sol.svg:  python scripts/make_icons.py
scripts/build_installer.ps1 also runs it with --installer-images <dir>.
Requires Pillow (no SVG renderer: the path is sampled and rasterised here).
"""
from pathlib import Path
import math
import re
import sys

from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "icons/logo_sol.svg"
ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
WINDOW_SIZES = (16, 32, 48, 64)
# Variants: SMALL is the simplified mark (UI and 16-48 px icons); a number is
# the source with its inner contour scaled by it (1 keeps the source).
SMALL = "small"
MEDIUM = 0.92    # 64-256 px icon frames
# The simplified mark, in units of the circle (centre 0, radius 1): an even
# ring and the wave as a smooth S that rises on the left, deep enough to read
# at 16 px. Two cubics, each (start, control, control, end).
SMALL_RING = 0.17
SMALL_WAVE = (((-1.2, 0.05), (-0.6, 0.0), (-0.3, 0.62), (0.15, 0.64)),
              ((0.15, 0.64), (0.55, 0.66), (0.8, 0.56), (1.2, 0.6)))
TILE = (22, 22, 25, 255)        # icon plate: Sol's dark chrome
SUN = (249, 202, 48, 255)       # #F9CA30: the mark on the plate, easy to spot in Start and the taskbar
MARK_ON_TILE = 0.70             # mark diameter / plate side
SUPERSAMPLE = 8


def contours():
    """[[segments]] per subpath; a segment is four (x, y) points (line = flat cubic)."""
    d = re.search(r'\sd="([^"]+)"', SOURCE.read_text(encoding="utf-8")).group(1)
    tokens = re.findall(r"[MCLZmclz]|-?\d*\.?\d+(?:e-?\d+)?", d)
    result, current, start, pos, i = [], [], None, None, 0
    while i < len(tokens):
        cmd = tokens[i]; i += 1
        if cmd == "M":
            if current:
                result.append(current)
            current, pos = [], (float(tokens[i]), float(tokens[i + 1])); start = pos; i += 2
        elif cmd == "C":
            while i < len(tokens) and tokens[i] not in "MCLZmclz":
                p = [(float(tokens[i + k]), float(tokens[i + k + 1])) for k in (0, 2, 4)]
                current.append((pos, p[0], p[1], p[2])); pos = p[2]; i += 6
        elif cmd == "L":
            while i < len(tokens) and tokens[i] not in "MCLZmclz":
                end = (float(tokens[i]), float(tokens[i + 1]))
                current.append((pos, pos, end, end)); pos = end; i += 2
        elif cmd == "Z":
            if pos != start:
                current.append((pos, pos, start, start))
            pos = start
        else:
            raise SystemExit(f"Unsupported path command {cmd} in {SOURCE}")
    if current:
        result.append(current)
    if len(result) != 2:
        raise SystemExit(f"{SOURCE} must have exactly two contours (outer, inner)")
    return result


def geometry():
    outer, inner = contours()
    xs = [p[0] for seg in outer for p in seg]; ys = [p[1] for seg in outer for p in seg]
    centre = ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2)
    radius = max(max(xs) - min(xs), max(ys) - min(ys)) / 2
    return outer, inner, centre, radius


def scaled(contour, centre, k):
    """Inner contour scaled towards the centre: thickens the ring and the wave alike."""
    cx, cy = centre
    return [tuple((cx + (x - cx) * k, cy + (y - cy) * k) for x, y in seg) for seg in contour]


def points(contour, steps=48):
    out = []
    for p0, p1, p2, p3 in contour:
        for s in range(steps):
            t = s / steps; u = 1 - t
            out.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                        u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))
    return out


def small_alpha(big):
    """The simplified mark's coverage at |big| px: ring plus the wave, clipped to the disc."""
    half = big / 2
    place = lambda x, y: (x * half + half, y * half + half)
    disc = Image.new("L", (big, big), 0)
    ImageDraw.Draw(disc).ellipse((0, 0, big - 1, big - 1), fill=255)
    alpha = disc.copy()
    hole = 1 - SMALL_RING
    ImageDraw.Draw(alpha).ellipse((*place(-hole, -hole), *place(hole, hole)), fill=0)
    wave = Image.new("L", (big, big), 0)
    curve = [place(*p) for segment in SMALL_WAVE for p in points([segment])] + [place(*SMALL_WAVE[-1][3])]
    ImageDraw.Draw(wave).polygon(curve + [place(1.3, 1.3), place(-1.3, 1.3)], fill=255)
    return ImageChops.multiply(ImageChops.lighter(alpha, wave), disc)


def mark(size, k, colour=(255, 255, 255, 255)):
    """The mark as an RGBA image whose circle fills |size|."""
    big = size * SUPERSAMPLE
    if k == SMALL:
        alpha = small_alpha(big)
    else:
        outer, inner, centre, radius = geometry()
        scale = big / (2 * radius)
        place = lambda pts: [((x - centre[0]) * scale + big / 2, (y - centre[1]) * scale + big / 2) for x, y in pts]
        alpha = Image.new("L", (big, big), 0)
        draw = ImageDraw.Draw(alpha)
        draw.polygon(place(points(outer)), fill=255)
        draw.polygon(place(points(scaled(inner, centre, k))), fill=0)
    alpha = alpha.resize((size, size), Image.LANCZOS)
    image = Image.new("RGBA", (size, size), colour)
    image.putalpha(Image.eval(alpha, lambda a: a * colour[3] // 255))
    return image


def tile_icon(size, k):
    """Dark rounded plate with the yellow mark: readable on light and dark taskbars."""
    big = size * SUPERSAMPLE
    plate = Image.new("RGBA", (big, big))
    ImageDraw.Draw(plate).rounded_rectangle((0, 0, big - 1, big - 1), radius=round(big * 0.225), fill=TILE)
    plate = plate.resize((size, size), Image.LANCZOS)
    inner = max(1, round(size * MARK_ON_TILE))
    plate.alpha_composite(mark(inner, k, SUN), ((size - inner) // 2, (size - inner) // 2))
    return plate


def svg(k, name, padding):
    """Square SVG of the mark (fill currentColor); |padding| is the margin per side as a fraction of the circle."""
    outer, inner, centre, radius = geometry()
    side = 2 * radius * (1 + 2 * padding)
    x0, y0 = centre[0] - side / 2, centre[1] - side / 2
    fmt = lambda v: f"{v:.2f}".rstrip("0").rstrip(".")
    def path(contour):
        start = contour[0][0]
        parts = [f"M{fmt(start[0])} {fmt(start[1])}"]
        parts += [f"C{fmt(a[0])} {fmt(a[1])} {fmt(b[0])} {fmt(b[1])} {fmt(c[0])} {fmt(c[1])}" for _, a, b, c in contour]
        return "".join(parts) + "Z"
    d = path(outer) + path(scaled(inner, centre, k))
    body = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{fmt(x0)} {fmt(y0)} {fmt(side)} {fmt(side)}" fill="none">'
            f'<path fill="currentColor" fill-rule="evenodd" d="{d}"/></svg>\n')
    (ROOT / "icons" / name).write_text(body, encoding="utf-8")


def small_svg(name, side):
    """The simplified mark as SVG (fill currentColor) in a square of |side| circle radii."""
    fmt = lambda v: f"{v:.3f}".rstrip("0").rstrip(".")
    hole = 1 - SMALL_RING
    wave = f"M{fmt(SMALL_WAVE[0][0][0])} {fmt(SMALL_WAVE[0][0][1])}" + "".join(
        f"C{fmt(a[0])} {fmt(a[1])} {fmt(b[0])} {fmt(b[1])} {fmt(c[0])} {fmt(c[1])}" for _, a, b, c in SMALL_WAVE) + "L1.3 1.3L-1.3 1.3Z"
    ring = f"M-1 0a1 1 0 1 0 2 0a1 1 0 1 0-2 0ZM-{fmt(hole)} 0a{fmt(hole)} {fmt(hole)} 0 1 0 {fmt(2 * hole)} 0a{fmt(hole)} {fmt(hole)} 0 1 0-{fmt(2 * hole)} 0Z"
    # 24x24 like every authored UI icon (the shell masks them in a 24 px box).
    body = (f'<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="{fmt(-side / 2)} {fmt(-side / 2)} {fmt(side)} {fmt(side)}" fill="none">'
            f'<clipPath id="disc"><circle r="1"/></clipPath><g fill="currentColor" clip-path="url(#disc)">'
            f'<path fill-rule="evenodd" d="{ring}"/><path d="{wave}"/></g></svg>\n')
    (ROOT / "icons" / name).write_text(body, encoding="utf-8")


def installer_shape(path: Path):
    """The source logo's two contours for the installer window (Direct2D), in units of
    the circle (centre 0, radius 1), plus where the ring's inner edge sits."""
    outer, inner, centre, radius = geometry()
    unit = lambda p: ((p[0] - centre[0]) / radius, (p[1] - centre[1]) / radius)
    fmt = lambda v: f"{v:.5f}".rstrip("0") + "f"  # Keeps the point: "1.f", a valid float literal.
    point = lambda p: "{" + fmt(unit(p)[0]) + ", " + fmt(unit(p)[1]) + "}"
    def contour(name, segments):
        rows = ",\n".join("    {" + ", ".join(point(q) for q in seg[1:]) + "}" for seg in segments)
        return (f"inline constexpr Point k{name}Start = {point(segments[0][0])};\n"
                f"inline constexpr Point k{name}[][3] = {{\n{rows}}};\n")
    upper = [math.hypot(*unit(q)) for q in points(inner, 24) if q[1] < centre[1]]
    body = ("// Generated by scripts/make_icons.py from icons/logo_sol.svg. Do not edit.\n"
            "#pragma once\n\nnamespace sol_installer::logo {\n"
            "struct Point { float x, y; };\n"
            "// Cubic segments (control, control, end), each starting where the last ended.\n"
            + contour("Outer", outer) + contour("Inner", inner) +
            "// The ring's inner edge: the closest the hole's upper half comes to the centre.\n"
            f"inline constexpr float kRingInner = {fmt(min(upper))};\n"
            "}  // namespace sol_installer::logo\n")
    path.write_text(body, encoding="utf-8", newline="\n")


def installer_images(out: Path):
    """Setup wizard images (not checked in): side panel in light/dark, small header logo."""
    out.mkdir(parents=True, exist_ok=True)
    # The panels match Sol's light and dark chrome; the thin logo has room here.
    for name, background, colour in (("wizard", (241, 239, 233, 255), (27, 27, 29, 255)),
                                     ("wizard-dark", (21, 21, 23, 255), (254, 254, 254, 255))):
        for scale in (1, 2):
            width, height, size = 164 * scale, 314 * scale, 104 * scale
            panel = Image.new("RGBA", (width, height), background)
            panel.alpha_composite(mark(size, MEDIUM if scale == 1 else 0.94, colour),
                                  ((width - size) // 2, (height - size) // 2 - 24 * scale))
            panel.convert("RGB").save(out / f"{name}-{scale}x.png", optimize=True)
    for scale in (1, 2):
        tile_icon(55 * scale, SMALL if scale == 1 else MEDIUM).save(out / f"wizard-small-{scale}x.png", optimize=True)
    print(out)


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--installer-images":
        installer_images(Path(sys.argv[2]))
        return
    # The UI mask is drawn in a 24 px box; the circle fills its middle 15 px.
    small_svg("sol-mark.svg", side=2 * 24 / 15)
    svg(1.0, "sol-logo.svg", padding=0.0)
    installer_shape(ROOT / "apps/installer/src/logo_shape.h")
    out = ROOT / "apps/desktop/resources"
    out.mkdir(parents=True, exist_ok=True)
    frames = [tile_icon(n, SMALL if n <= 48 else MEDIUM) for n in ICO_SIZES]
    # Pillow stores every frame as PNG; Windows Vista+ reads PNG frames at any size.
    frames[-1].save(out / "red.ico", format="ICO", sizes=[(n, n) for n in ICO_SIZES], append_images=frames[:-1])
    for n in WINDOW_SIZES:
        tile_icon(n, SMALL if n <= 48 else MEDIUM).save(out / f"red-{n}.png", optimize=True)
    print(out)


if __name__ == "__main__":
    main()
