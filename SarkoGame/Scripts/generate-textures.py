"""
Generates the sector's surface detail textures. Run by Scripts/generate-textures.sh.

WHAT THESE ARE. One tiling greyscale map per surface that earns one, written to
Art/Generated/T_Surface_<Name>.png. They are NOT base colour maps: the base
colour of every surface still comes from Map/SarkoMapPalette.cpp, and the shader
multiplies it by (1 +- DetailStrength) driven by this map. A texel of 0.5 is
"exactly the palette colour". Every image below is normalised to a MEAN of 0.5,
so the average of any patch is the palette colour and a surface's identity
cannot drift when a texture is regenerated with a different seed.

WHY PYTHON AND NOT BLENDER. The other half of this project's art pipeline is a
Blender script (Scripts/prepare-assets.py) and the obvious move was to bake
these from shader nodes there. Two things decided against it:

 * SEAMLESSNESS IS THE WHOLE REQUIREMENT and Blender's noise textures are not
   periodic in UV space. Making them tile means 4D-noise-on-a-torus tricks, and
   the result is only approximately seamless. Every generator below is periodic
   BY CONSTRUCTION — the spectral ones because a finite Fourier series on a
   discrete grid is exactly periodic, the cellular ones because the feature-point
   lattice wraps modulo the cell count. There is no seam to hide.
 * FREQUENCY CONTROL IS THE WHOLE ART DIRECTION. The judging camera is a spring
   arm 1400 uu behind a 176 uu pawn at -70 degrees, which puts roughly 2.5 world
   units under each screen pixel. Detail finer than ~10 uu is sub-pixel shimmer;
   detail coarser than ~600 uu is a stain nobody reads as material. Building the
   maps in the frequency domain means that band is a NUMBER (see BandUU below)
   rather than something to squint at.

A third, smaller reason: this runs in about two seconds with no GPU, no scene
file and no render-engine version to drift under it. Blender is still the right
tool for the mesh half; it is the wrong one for this half.

DETERMINISM. Every random draw comes from numpy's PCG64 seeded per surface with
a fixed integer, so the bytes are reproducible: delete Art/Generated and re-run
and you get the same PNGs. That is what lets the .uasset be committed.

Usage: python generate-textures.py <out-dir> [--manifest <path>]
"""

import argparse
import json
import os

import numpy as np
from PIL import Image


# --------------------------------------------------------------------------
# Periodic primitives. Every one of these returns an array that tiles exactly.
# --------------------------------------------------------------------------


def spectral(res, rng, low, high, beta=1.0, aniso=1.0):
    """
    Band-limited noise: white noise whose spectrum is cut to [low, high] cycles
    per tile and shaped by r**-beta.

    `low` and `high` are in CYCLES PER TILE, which is the unit the art direction
    is actually expressed in — a feature at f cycles is (period / f) world units
    across, so the band maps straight onto "what size is a stain".

    `aniso` is kept and is unused by every recipe below, which is the whole
    point of the note: the material projects world XY onto every face, so a
    vertical surface already receives this pattern extruded along Z. The
    verticality of a bark fibre or a rust streak comes from the PROJECTION.
    Baking a direction in here as well would make it a direction in the WORLD —
    a grain running east across four hundred metres of field, and a trunk whose
    fibre density depended on which way it faced.
    """
    white = rng.standard_normal((res, res))
    spectrum = np.fft.rfft2(white)

    fy = np.fft.fftfreq(res) * res
    fx = np.fft.rfftfreq(res) * res
    # aniso multiplies the V (row) frequency, so a value of 8 means a feature is
    # eight times longer vertically than horizontally.
    radius = np.hypot(fy[:, None] * aniso, fx[None, :])

    amplitude = np.zeros_like(radius)
    inside = (radius >= low) & (radius <= high)
    amplitude[inside] = radius[inside] ** (-beta)

    return np.fft.irfft2(spectrum * amplitude, s=(res, res))


def cellular(res, rng, cells, mode="f1", aniso=1.0):
    """
    Periodic Worley noise. `mode` is 'f1' (distance to the nearest feature point
    — blobs, aggregate) or 'edge' (F2 - F1 — the crack network between cells).

    Wrapping is not a post-process here: a pixel's nine candidate cells are
    indexed modulo `cells`, so the last column's neighbours ARE the first
    column's points. There is no edge to blend.
    """
    # aniso stretches the cells along V: fewer rows of feature points, and a
    # distance metric that discounts vertical separation by the same factor, so
    # a cell stays a cell rather than becoming a band.
    cy = max(1, int(round(cells / aniso)))
    cx = max(1, cells)
    jitter = rng.random((cy, cx, 2))
    # Feature point positions in tile space (0..1).
    py = (np.arange(cy)[:, None] + jitter[:, :, 0]) / cy
    px = (np.arange(cx)[None, :] + jitter[:, :, 1]) / cx

    gy = (np.arange(res) + 0.5) / res
    gx = (np.arange(res) + 0.5) / res
    iy = (gy * cy).astype(int)
    ix = (gx * cx).astype(int)

    best = np.full((res, res), 4.0)
    second = np.full((res, res), 4.0)
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            ny = (iy[:, None] + oy) % cy
            nx = (ix[None, :] + ox) % cx
            dy = gy[:, None] - py[ny, nx]
            dx = gx[None, :] - px[ny, nx]
            # Torus distance: the shortest way round, which is what makes the
            # -1/+1 neighbour offsets above legal across the wrap.
            dy = dy - np.round(dy)
            dx = dx - np.round(dx)
            # Vertical separation discounted by the same factor the row count
            # was, so a cell stays round in its own stretched space instead of
            # collapsing into a horizontal band.
            dist = np.hypot(dy / aniso, dx)
            second = np.minimum(second, np.maximum(best, dist))
            best = np.minimum(best, dist)

    return best if mode == "f1" else (second - best)


def flecks(res, rng, count, radius_texels, sharpness=2.0):
    """
    Sparse round specks — the grass tufts in the ground, the aggregate glints in
    the asphalt. Splatted on a torus, so a fleck near an edge appears on both.
    """
    field = np.zeros((res, res))
    ys = rng.integers(0, res, count)
    xs = rng.integers(0, res, count)
    field[ys, xs] = 1.0
    # Gaussian by FFT: a circular convolution on a periodic grid, which is
    # exactly the wrap this needs and is free compared with splatting kernels.
    kernel_y = np.fft.fftfreq(res) * res
    kernel_x = np.fft.rfftfreq(res) * res
    radius_freq = np.hypot(kernel_y[:, None], kernel_x[None, :])
    sigma = max(radius_texels, 0.5)
    gauss = np.exp(-2.0 * (np.pi * radius_freq * sigma / res) ** 2)
    blurred = np.fft.irfft2(np.fft.rfft2(field) * gauss, s=(res, res))
    peak = blurred.max()
    if peak > 0:
        blurred = blurred / peak
    return blurred ** sharpness


# --------------------------------------------------------------------------
# Normalisation. The palette is the source of colour truth, and that is a
# property of these arrays rather than a promise in a comment.
# --------------------------------------------------------------------------


def unitise(field):
    """Zero mean, unit standard deviation. Undefined input (a flat field) is zero."""
    field = field - field.mean()
    deviation = field.std()
    return field / deviation if deviation > 1e-9 else field


def compose(layers, contrast):
    """
    Sums pre-unitised layers with weights, then scales the result so its standard
    deviation is `contrast` around 0.5 and re-centres the MEAN on 0.5 exactly.

    The re-centring loop matters: clipping at 0 and 1 is asymmetric whenever the
    composition is skewed (cracks are dark and rare, flecks are bright and rare),
    so a single shift is not enough. Three passes put the mean within a 255th of
    a unit of 0.5 for every recipe in this file, which is the finest an 8-bit PNG
    can express anyway.
    """
    total = np.zeros_like(layers[0][0])
    for field, weight in layers:
        total = total + unitise(field) * weight
    total = unitise(total) * contrast + 0.5
    for _ in range(3):
        total = np.clip(total, 0.0, 1.0)
        total = total + (0.5 - total.mean())
    return np.clip(total, 0.0, 1.0)


# --------------------------------------------------------------------------
# The recipes.
#
# `band` is the one number the top-down camera cares about: the world size in uu
# of one full tile. Everything else is expressed in cycles per tile, so a recipe
# reads as "features between period/high and period/low world units across".
# --------------------------------------------------------------------------


def ground(res, rng):
    """
    Bare earth: broad dry/damp patching, dirt clumps, and sparse grass tufts.

    Isotropic on purpose. The UVs are world-aligned (see the material), so any
    direction baked in here is a direction in the WORLD — a grain running east
    across four hundred metres of field, which is the one thing that would make
    procedural ground look procedural.
    """
    return compose([
        (spectral(res, rng, low=2, high=7, beta=0.8), 1.0),    # damp/dry patches, ~230-800 uu
        (spectral(res, rng, low=10, high=45, beta=0.9), 0.75),  # clumps, ~35-160 uu
        (cellular(res, rng, cells=14, mode="f1"), 0.35),        # trodden hollows
        (flecks(res, rng, count=900, radius_texels=3.0), 0.45),  # grass tufts, ~9 uu
    ], contrast=0.20)


def dirt(res, rng):
    """A used track: gravel, wheel-polished patches, no baked direction (roads run both ways)."""
    return compose([
        (spectral(res, rng, low=2, high=6, beta=0.7), 0.9),
        (cellular(res, rng, cells=40, mode="f1"), 0.55),      # gravel, ~30 uu
        (spectral(res, rng, low=12, high=50, beta=1.0), 0.6),
        (flecks(res, rng, count=500, radius_texels=3.0), 0.30),  # loose stones catching sun
    ], contrast=0.22)


def asphalt(res, rng):
    """
    Aggregate plus a crack network. The cracks are the cellular EDGE field
    sharpened and subtracted, so they are thin dark lines between plates rather
    than a soft veining — at 2.5 uu per pixel a soft crack is just a smudge.
    """
    edges = cellular(res, rng, cells=9, mode="edge")
    cracks = np.clip(1.0 - edges * 45.0, 0.0, 1.0)
    return compose([
        (cellular(res, rng, cells=52, mode="f1"), 0.9),      # aggregate, ~19 uu stones
        (spectral(res, rng, low=2, high=8, beta=0.8), 0.8),  # wear patches / old repairs
        (-cracks, 0.55),
        (flecks(res, rng, count=700, radius_texels=3.0), 0.25),
    ], contrast=0.17)


def concrete(res, rng):
    """Pitting, broad stains, and a sparser crack network than the asphalt's."""
    edges = cellular(res, rng, cells=6, mode="edge")
    cracks = np.clip(1.0 - edges * 55.0, 0.0, 1.0)
    # A pit is where the distance to a feature point is SMALL. The obvious
    # `pits - threshold` reads the field upside down: f1 with 40 cells never
    # exceeds ~0.03, so that expression clips to a field of zeros and the layer
    # silently contributes nothing at all. It did, for one revision.
    pits = np.clip(1.0 - cellular(res, rng, cells=40, mode="f1") * 90.0, 0.0, 1.0)
    return compose([
        (spectral(res, rng, low=2, high=6, beta=1.1), 1.0),   # damp staining
        (-pits, 0.65),                                        # pits: dark, small, sparse
        (spectral(res, rng, low=14, high=55, beta=1.0), 0.55),
        (-cracks, 0.40),
    ], contrast=0.19)


def structure(res, rng):
    """
    Generic built grey — walls, crates, wrecks, and the fifty-one car bodies on
    the highway. Mostly broad grime, because these are SMALL objects: a 200 uu
    crate covers a third of a 600 uu tile, so the low frequencies here are what
    makes two crates look different from each other, and the high ones are what
    would make one crate look noisy.

    ISOTROPIC, and that is the second version. The first had an anisotropic
    "weather running down" layer, which is right on a wall and was very wrong on
    a car: the wrecks are curved meshes, so the side projection sweeps that
    streak around the bodywork, and in the road frame every car on the highway
    came out looking like varnished plywood. A directional pattern on a surface
    shared by walls and car roofs picks a fight it cannot win — Rust keeps its
    streaks because everything wearing Rust is a tank, a freight car or a pylon
    leg, and all of those are upright.
    """
    return compose([
        (spectral(res, rng, low=1, high=5, beta=1.2), 1.0),
        (spectral(res, rng, low=6, high=22, beta=1.0), 0.50),
        (cellular(res, rng, cells=18, mode="f1"), 0.25),
    ], contrast=0.16)


def rust(res, rng):
    """
    Industry. Mottled corrosion with streaks running down the V axis — which on
    a vertical face IS down, because the side projection's V is world -Z.
    """
    return compose([
        (spectral(res, rng, low=2, high=9, beta=0.9), 0.9),    # corrosion fronts
        (spectral(res, rng, low=4, high=30, beta=0.7), 1.0),   # the runoff, once the projection extrudes it
        (cellular(res, rng, cells=30, mode="f1"), 0.45),       # scale/pitting
    ], contrast=0.24)


def timber(res, rng):
    """
    Weathered plank: fine tonal grain plus board-to-board variation.

    Isotropic, which costs the one thing "grain" really means — a direction.
    Timber clothes roofs AND fences here, i.e. horizontal faces and vertical
    ones, and a direction baked into the map would have been a compass bearing
    in the world: right on half of them by luck. On the fences the projection
    supplies verticality for free; on the roofs this reads as weathering, which
    is what a roof in this sector should read as.
    """
    return compose([
        (spectral(res, rng, low=8, high=36, beta=0.6), 1.0),   # grain, ~11-50 uu
        (spectral(res, rng, low=1, high=4, beta=1.0), 0.7),    # board tone
        (flecks(res, rng, count=120, radius_texels=2.5), -0.20),  # knots
    ], contrast=0.17)


def bark(res, rng):
    """
    Vertical fibre — supplied by the projection, not by the map. What this has
    to provide is the fibre's SPACING: a trunk is about 60 uu across and the tile
    is 220 uu, so features of 5 to 14 uu put a countable handful of ridges around
    one trunk, and world XY extrudes them up its length.

    DEEPER in contrast than anything else here, because a trunk is a vertical
    surface under a 55-degree sun: it receives a fraction of the light the ground
    does (the same argument the palette makes for Bark's luminance), so the same
    modulation reads as less.
    """
    return compose([
        (spectral(res, rng, low=16, high=48, beta=0.7), 1.0),  # fibre, ~5-14 uu across
        (spectral(res, rng, low=5, high=15, beta=0.8), 0.7),   # broad tone
        (cellular(res, rng, cells=26, mode="edge"), -0.4),     # fissures
    ], contrast=0.26)


def vegetation(res, rng):
    """Bush and treeline mass: clumps of leaf, nothing finer — leaves are sub-pixel here."""
    return compose([
        (cellular(res, rng, cells=11, mode="f1"), 0.9),
        (spectral(res, rng, low=4, high=18, beta=0.9), 1.0),
        (spectral(res, rng, low=1, high=4, beta=1.0), 0.6),
    ], contrast=0.22)


def ravine(res, rng):
    """
    Rubble in the gorge bed. UNUSED — kept because the reasoning is worth more
    than the twelve lines it costs.

    It was generated, imported, wired up and photographed, and at the judging
    camera the ravine bed measured 6.45 units of local detail before and 6.55
    after. The surface is a linear 0.013, the darkest thing in the sector by
    design, so a 35% swing is a swing of 0.005 in linear terms: below what the
    frame can show and far below what a phone screen can. Flat is not a
    compromise here, it is the same picture for one less texture and one less
    sample on the largest thing a player walks across.

    Re-add it to RECIPES (and the palette's detail table) only with a frame that
    shows a difference.
    """
    return compose([
        (cellular(res, rng, cells=16, mode="f1"), 1.0),
        (spectral(res, rng, low=2, high=10, beta=0.9), 0.8),
        (cellular(res, rng, cells=44, mode="f1"), 0.4),
    ], contrast=0.20)


# name -> (recipe, resolution, seed, tile period in world uu)
#
# The 512s are the surfaces that cover ground area and are therefore ON SCREEN
# in bulk (a texel is ~3 uu, about a pixel at the judging camera). The 256s go on
# objects a few hundred uu across, where a 256 tile at a 200-600 uu period is
# already finer than the screen resolves.
RECIPES = {
    "Ground":     (ground,     512, 20260801, 1600.0),
    "Dirt":       (dirt,       512, 20260802, 1200.0),
    "Asphalt":    (asphalt,    512, 20260803, 1000.0),
    "Concrete":   (concrete,   512, 20260804,  800.0),
    "Structure":  (structure,  256, 20260805,  600.0),
    "Rust":       (rust,       256, 20260806,  500.0),
    "Timber":     (timber,     256, 20260807,  400.0),
    "Bark":       (bark,       256, 20260808,  220.0),
    "Vegetation": (vegetation, 256, 20260809,  700.0),
    # "Ravine" is deliberately absent — see the recipe's docstring for the frame
    # that decided it.
}


def seam_ratio(image):
    """
    The worst of the two wrap edges, measured as a RATIO against the image's own
    texel-to-texel step IN THE SAME DIRECTION.

    Per-direction is the whole subtlety. Several of these maps are deliberately
    anisotropic — bark's fibre varies eighteen times faster across than along —
    so a single pooled "average neighbour difference" compares the wrap in the
    fast direction against a number dominated by the slow one and calls a
    perfectly seamless texture broken. (It did. That is why this function is
    shaped like this.)

    And it is measured against the LOUDEST line rather than the average line.
    Bark varies eighteen times more slowly down the tile than across it, so its
    row-to-row steps are all small and strongly correlated; one particular pair
    of rows being 1.5x the mean of 256 such pairs is ordinary variance in a
    smooth field, not a seam. Comparing the wrap against the 99th percentile of
    every other line asks the question that actually matters: is the join
    visible ABOVE the texture's own busiest place? A real seam is not a
    borderline outlier — a discontinuity in a field like this lands at three to
    ten times the loudest legitimate step.
    """
    pixels = image.astype(np.int32)
    worst = 0.0
    for axis in (0, 1):
        steps = np.abs(np.roll(pixels, -1, axis=axis) - pixels)
        # One mean per line of steps; the last is the wrap.
        lines = steps.mean(axis=1 - axis)
        wrap = lines[-1]
        loudest = np.percentile(lines[:-1], 99)
        worst = max(worst, wrap / loudest if loudest > 1e-6 else 0.0)
    return float(worst)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out_dir")
    parser.add_argument("--manifest", default=None)
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    manifest = {}

    for name, (recipe, res, seed, period) in sorted(RECIPES.items()):
        rng = np.random.default_rng(seed)
        field = recipe(res, rng)
        image = np.rint(field * 255.0).astype(np.uint8)

        path = os.path.join(args.out_dir, "T_Surface_%s.png" % name)
        Image.fromarray(image, mode="L").save(path, optimize=True)

        seam = seam_ratio(image)
        mean = float(image.mean()) / 255.0
        manifest[name] = {
            "file": os.path.basename(path),
            "resolution": res,
            "seed": seed,
            "tileUU": period,
            "mean": round(mean, 5),
            "stdDev": round(float(image.std()) / 255.0, 5),
            "seamRatio": round(seam, 3),
            "bytes": os.path.getsize(path),
        }

        # The two invariants that make the palette argument true and the tiling
        # claim true. Loud, and fatal — a texture whose mean is not 0.5 shifts
        # its surface's colour away from the palette, permanently and silently,
        # and a texture with a seam repeats that seam every few metres forever.
        assert abs(mean - 0.5) < 0.004, "%s: mean %.4f is not the palette colour" % (name, mean)
        assert seam < 1.15, "%s: wrap step is %.2fx the loudest line — not seamless" % (name, seam)

        print("SARKO_TEXTURE %-11s %dx%d  tile=%.0fuu  texel=%.2fuu  mean=%.4f  sd=%.3f  seam=%.2fx  %d B"
              % (name, res, res, period, period / res, mean, manifest[name]["stdDev"], seam,
                 manifest[name]["bytes"]))

    manifest_path = args.manifest or os.path.join(args.out_dir, "textures.json")
    with open(manifest_path, "w") as handle:
        json.dump(manifest, handle, indent=1, sort_keys=True)
        handle.write("\n")
    print("SARKO_TEXTURE_MANIFEST " + manifest_path)


main()
