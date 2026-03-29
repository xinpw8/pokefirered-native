"""
pfr_heatmap.py — Exploration heatmap rendering for PFRN

Generates an HSV overlay of visited tiles on a blank canvas.
If a Kanto background image is available, overlays on that.
Otherwise renders on black.

Called from the training loop and logged to wandb.
"""

import os
import numpy as np

from pfr_env import PADDED_SHAPE, PAD

# Try to load a Kanto background map image
KANTO_MAP_PATH = os.path.join(os.path.dirname(__file__), "kanto_map_dsv.png")
BACKGROUND = None
try:
    import cv2
    if os.path.isfile(KANTO_MAP_PATH):
        BACKGROUND = np.array(cv2.imread(KANTO_MAP_PATH))
        BACKGROUND = np.pad(
            BACKGROUND,
            ((PAD * 16, PAD * 16), (PAD * 16, PAD * 16), (0, 0)),
        )
except ImportError:
    pass


def hsv_to_rgb_simple(hsv):
    """Simple HSV to RGB conversion. hsv shape: (H, W, 3), values in [0,1]."""
    h, s, v = hsv[..., 0], hsv[..., 1], hsv[..., 2]
    i = (h * 6.0).astype(np.int32)
    f = h * 6.0 - i
    p = v * (1.0 - s)
    q = v * (1.0 - s * f)
    t = v * (1.0 - s * (1.0 - f))

    r = np.zeros_like(h)
    g = np.zeros_like(h)
    b = np.zeros_like(h)

    for idx_val, rv, gv, bv in [
        (0, v, t, p), (1, q, v, p), (2, p, v, t),
        (3, p, q, v), (4, t, p, v), (5, v, p, q),
    ]:
        mask = (i % 6) == idx_val
        r[mask] = rv[mask]
        g[mask] = gv[mask]
        b[mask] = bv[mask]

    zero_s = s == 0
    r[zero_s] = v[zero_s]
    g[zero_s] = v[zero_s]
    b[zero_s] = v[zero_s]

    return np.stack([r, g, b], axis=-1)


def make_exploration_overlay(counts):
    """
    Generate an RGB image showing exploration heatmap.

    Args:
        counts: np.ndarray of shape PADDED_SHAPE (or (N, H, W) to sum over axis 0)

    Returns:
        np.ndarray of shape (H*16, W*16, 3) uint8 RGB image
    """
    if counts.ndim == 3:
        counts = np.sum(counts, axis=0)

    # Normalize
    max_val = np.max(counts)
    if max_val == 0:
        # No exploration yet — return blank
        h, w = counts.shape
        return np.zeros((h * 4, w * 4, 3), dtype=np.uint8)

    scaled = counts.astype(np.float32) / max_val
    nonzero = (scaled > 0).astype(np.float32)

    # HSV: hue goes from red (high visit) to blue (low visit)
    # H: 2/3 * (1 - scaled) maps [0,1] -> [2/3, 0] (blue to red)
    # S: 1 where nonzero, V: 1 where nonzero
    hsv = np.stack([
        2.0 * (1.0 - scaled) / 3.0,
        nonzero,
        nonzero,
    ], axis=-1)

    rgb = (255 * hsv_to_rgb_simple(hsv)).astype(np.uint8)

    # Upscale: each tile -> 4x4 pixels (smaller than pokered's 16x16 for speed)
    SCALE = 2
    r = np.kron(rgb[..., 0], np.ones((SCALE, SCALE), dtype=np.uint8))
    g = np.kron(rgb[..., 1], np.ones((SCALE, SCALE), dtype=np.uint8))
    b = np.kron(rgb[..., 2], np.ones((SCALE, SCALE), dtype=np.uint8))
    overlay = np.stack([r, g, b], axis=-1)

    # If background available and size matches, blend
    if BACKGROUND is not None:
        bg_h, bg_w = BACKGROUND.shape[:2]
        ov_h, ov_w = overlay.shape[:2]
        if bg_h == ov_h and bg_w == ov_w:
            mask = np.kron(nonzero, np.ones((SCALE, SCALE))).astype(bool)
            mask3 = np.stack([mask, mask, mask], axis=-1)
            render = BACKGROUND.copy().astype(np.int32)
            render[mask3] = (0.2 * render[mask3] + 0.8 * overlay[mask3].astype(np.int32))
            overlay = np.clip(render, 0, 255).astype(np.uint8)

    return overlay
