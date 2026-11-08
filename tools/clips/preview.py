#!/usr/bin/env python3
"""Draw the clips as the board does, to animated GIFs and contact sheets.

The board's layout (main/eyes.c: body_layout, clip_eye_layout, the hands)
is copied here closely enough to judge poses, timing and the transitions
between clips without the board or the Rive editor. Colours and shading are
simpler than the board's; shapes and positions are the same.

Needs Pillow. Usage:

  preview.py gif NAME... [--out DIR]       one GIF per clip
  preview.py chain NAME... [--out FILE]    the clips one after another, with
                                           the board's bridges between them
                                           ("rest" is the engine at rest)
  preview.py sheet NAME [--every N]        a PNG of every Nth frame, numbered

NAME is a clip in tools/clips/keyframes.json, or one in the authoring
library (tools/clips/library) when --library is given.
"""

import argparse
import json
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import clips  # noqa: E402

SCREEN = 466
CX = CY = SCREEN // 2
R = 98            # LOOK_SPHERE_R
K = 0.7           # LOOK_SCALE
FACE_Y = 16 * K
NEUTRAL_W, NEUTRAL_H = 26, 60
HAND_RX, HAND_RY = 27 * K, 22 * K
FRAME_MS = 1000 / 30
# main/eyes.c: BRIDGE_IN_MS, BRIDGE_OUT_MS, BRIDGE_CUT_MS.
BRIDGE_IN, BRIDGE_OUT, BRIDGE_CUT = 160, 260, 200
SCALE = 0.5       # the GIFs are drawn at half the screen's size

BG = (8, 8, 14)
PEARL = (232, 228, 240)
SHADE = (190, 184, 206)
EYE = (29, 27, 36)
HAND = (238, 234, 246)
RING = (40, 40, 52)


def frames_of(name, library):
    if library:
        sys.path.insert(0, os.path.join(HERE, "library"))
        from author import all_clips  # noqa
        # The library names some properties as author.py keys them; Rive
        # (and keyframes.json) name them otherwise.
        rename = {"vy": "y", "rad": "radius", "o": "opacity"}
        rows = [dict(r, property=rename.get(r["property"], r["property"])) for r in all_clips()[name]().rows()]
    else:
        rows = json.load(open(clips.KEYS))[name]
    return [dict(zip(clips.FIELDS, f)) for f in clips.sample(rows)], name.endswith("_loop")


def rest_frame():
    """The engine at rest, as a clip frame: what a clip starts from and ends in."""
    f = dict(zip(clips.FIELDS, [0] * len(clips.FIELDS)))
    f.update(bsx=1, bsy=1, near=1, lx=-112, ly=40, rx=112, ry=40, turn=1,
             lex=-32, lew=1, leh=1, rex=32, rew=1, reh=1, px=0, py=110)
    return f


def layout(f):
    """Screen layout of one frame: the body, the eyes, the hands, the prop."""
    d = f["near"]
    bx, by = CX + f["bx"] * K * d, CY + f["by"] * K * d
    rx, ry = R * f["bsx"] * d, R * f["bsy"] * d
    sx, sy = rx / R, ry / R
    a = math.radians(f["rot"])
    ca, sa = math.cos(a), math.sin(a)
    eyes = []
    for p in ("l", "r"):
        ex, ew, eh = f[p + "ex"], f[p + "ew"], f[p + "eh"]
        w = NEUTRAL_W * K * f["turn"] * ew * sx
        h = NEUTRAL_H * K * eh * sy
        dx = (ex * f["turn"] + f["fx"]) * K * sx
        dy = (FACE_Y + f["fy"] * K) * sy
        hh = max(h, 2)
        eyes.append({"x": bx + dx * ca - dy * sa, "y": by + dx * sa + dy * ca, "w": w, "h": h,
                     "tl": -hh / 2 + f[p + "tl"] * hh, "tr": -hh / 2 + f[p + "tr"] * hh,
                     "bottom": f[p + "bot"]})
    hands = [(bx + f["lx"] * K * d, by + f["ly"] * K * d, f["ls"] * d),
             (bx + f["rx"] * K * d, by + f["ry"] * K * d, f["rs"] * d)]
    prop = (bx + f["px"] * K * d, by + f["py"] * K * d, 34 * K * d * f["ps"])
    return {"bx": bx, "by": by, "rx": rx, "ry": ry, "eyes": eyes, "hands": hands,
            "front": f["front"] > 0.5, "prop": prop, "glow": f["glow"], "spark": f["spark"]}


def lerp(a, b, k):
    return a + (b - a) * k


def blend(a, b, k):
    """The board's bridge: what was on screen eased into what is now."""
    out = dict(b)
    for key in ("bx", "by", "rx", "ry"):
        out[key] = lerp(a[key], b[key], k)
    out["eyes"] = [{q: lerp(ea[q], eb[q], k) for q in ea} for ea, eb in zip(a["eyes"], b["eyes"])]
    out["hands"] = [tuple(lerp(x, y, k) for x, y in zip(ha, hb)) for ha, hb in zip(a["hands"], b["hands"])]
    return out


def smooth(k):
    return k * k * (3 - 2 * k)


def eye_mask(e, scale):
    """The eye's coverage, as eye_render() works it out, at `scale`."""
    hw, hh = max(e["w"] / 2, 1), max(e["h"] / 2, 1)
    w, h = int(math.ceil(2 * hw * scale)) + 4, int(math.ceil(2 * hh * scale)) + 4
    m = Image.new("L", (w, h))
    px = m.load()
    rr = min(hw, hh)
    lid = max(e["tl"], e["tr"]) > -hh + 0.5
    lx0, lx1 = -hw - 6, hw + 6
    slope = (e["tr"] - e["tl"]) / (lx1 - lx0)
    norm = 1 / math.sqrt(1 + slope * slope)
    smile = e["bottom"] > 0.01
    rb = 1.8 * hw
    smile_cy = hh + 2 - e["bottom"] * (0.6 * hh + 2) + rb
    for y in range(h):
        py = (y + 0.5 - h / 2) / scale
        for x in range(w):
            pxf = (x + 0.5 - w / 2) / scale
            qx, qy = abs(pxf) - hw + rr, abs(py) - hh + rr
            dd = math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - rr
            if lid:
                dd = max(dd, (e["tl"] + (pxf - lx0) * slope - py) * norm)
            if smile:
                dd = max(dd, rb - math.hypot(pxf, py - smile_cy))
            px[x, y] = int(max(0, min(1, 0.5 - dd * scale)) * 255)
    return m


def draw(lay, rgb=(255, 212, 92), label=None, scale=SCALE):
    size = int(SCREEN * scale)
    img = Image.new("RGB", (size, size), BG)
    g = ImageDraw.Draw(img)
    s = scale

    def ell(x, y, rx, ry, fill):
        g.ellipse([(x - rx) * s, (y - ry) * s, (x + rx) * s, (y + ry) * s], fill=fill)

    g.ellipse([0, 0, size - 1, size - 1], outline=RING)
    if lay["glow"] > 0.01:
        k = lay["glow"]
        halo = tuple(int(lerp(BG[i], rgb[i], 0.55 * k)) for i in range(3))
        ell(lay["bx"], lay["by"], lay["rx"] * 1.35, lay["ry"] * 1.35, halo)

    def hands():
        for hx, hy, hs in lay["hands"]:
            if hs > 0.02:
                ell(hx, hy, HAND_RX * hs, HAND_RY * hs, HAND)
                g.ellipse([(hx - HAND_RX * hs) * s, (hy - HAND_RY * hs) * s,
                           (hx + HAND_RX * hs) * s, (hy + HAND_RY * hs) * s], outline=SHADE)

    if not lay["front"]:
        hands()
    ell(lay["bx"], lay["by"], lay["rx"], lay["ry"], PEARL)
    ell(lay["bx"] + lay["rx"] * 0.12, lay["by"] + lay["ry"] * 0.12, lay["rx"] * 0.86, lay["ry"] * 0.86, (226, 221, 236))
    if lay["glow"] > 0.01:
        wash = tuple(int(lerp(226, rgb[i], 0.35 * lay["glow"])) for i in range(3))
        ell(lay["bx"], lay["by"] + lay["ry"] * 0.35, lay["rx"] * 0.8, lay["ry"] * 0.55, wash)
    for e in lay["eyes"]:
        if e["w"] < 0.5 or e["h"] < 0.5:
            continue
        m = eye_mask(e, s)
        img.paste(EYE, (int(e["x"] * s - m.width / 2), int(e["y"] * s - m.height / 2)), m)
    px, py, half = lay["prop"]
    if half > 0.5:
        g.rounded_rectangle([(px - half) * s, (py - half) * s, (px + half) * s, (py + half) * s],
                            radius=half * 0.35 * s, fill=rgb, outline=(255, 255, 255))
    if lay["front"]:
        hands()
    if lay["spark"] > 0.01:
        n = int(3 + 10 * lay["spark"])
        for i in range(n):
            a = i * 2.4 + lay["bx"] * 0.01
            rr = lay["rx"] * (1.1 + 0.25 * ((i * 37) % 7) / 7)
            x, y = lay["bx"] + math.cos(a) * rr, lay["by"] - abs(math.sin(a)) * rr
            g.regular_polygon((x * s, y * s, 4), 4, rotation=45, fill=(255, 250, 220))
    if label:
        g.text((6, 4), label, fill=(150, 150, 170))
    return img


def timeline(names, library):
    """Each output frame (every 1/30 s) as a layout, bridges included."""
    out = []
    shown = layout(rest_frame())
    for idx, name in enumerate(names):
        if name == "rest":
            target = layout(rest_frame())
            n, bridge = 18, BRIDGE_OUT if idx else 0
            seq = [target] * n
        else:
            frames, _ = frames_of(name, library)
            seq = [layout(f) for f in frames]
            bridge = BRIDGE_IN if idx == 0 or names[idx - 1] == "rest" else BRIDGE_CUT
        start = shown
        for i, lay in enumerate(seq):
            t = i * FRAME_MS
            if bridge and t < bridge:
                lay = blend(start, lay, smooth(t / bridge))
            out.append((name, i, lay))
        shown = out[-1][2]
    # Back to rest at the end, as the engine takes over.
    end = layout(rest_frame())
    for i in range(int(BRIDGE_OUT / FRAME_MS) + 6):
        t = i * FRAME_MS
        out.append(("rest", i, blend(shown, end, smooth(min(1, t / BRIDGE_OUT)))))
    return out


def save_gif(frames, path):
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=int(FRAME_MS), loop=0,
                   optimize=False, disposal=1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["gif", "chain", "sheet"])
    ap.add_argument("names", nargs="+")
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    ap.add_argument("--every", type=int, default=4)
    ap.add_argument("--library", action="store_true", help="read the authoring library, not keyframes.json")
    ap.add_argument("--rgb", default="FFD45C")
    a = ap.parse_args()
    rgb = tuple(int(a.rgb[i:i + 2], 16) for i in (0, 2, 4))
    if a.cmd == "gif":
        os.makedirs(a.out, exist_ok=True)
        for n in a.names:
            tl = timeline([n], a.library)
            path = os.path.join(a.out, n + ".gif")
            save_gif([draw(lay, rgb) for _, _, lay in tl], path)
            print(path)
    elif a.cmd == "chain":
        path = a.out if a.out.endswith(".gif") else os.path.join(a.out, "chain.gif")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tl = timeline(a.names, a.library)
        save_gif([draw(lay, rgb, f"{n} {i}") for n, i, lay in tl], path)
        print(path)
    else:
        n = a.names[0]
        frames, _ = frames_of(n, a.library)
        picks = list(range(0, len(frames), a.every))
        if picks[-1] != len(frames) - 1:
            picks.append(len(frames) - 1)
        cell = int(SCREEN * 0.4)
        cols = 6
        rows = (len(picks) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * cell, rows * cell), (0, 0, 0))
        for j, i in enumerate(picks):
            sheet.paste(draw(layout(frames[i]), rgb, f"{n} f{i}", 0.4), ((j % cols) * cell, (j // cols) * cell))
        os.makedirs(a.out, exist_ok=True)
        path = os.path.join(a.out, f"{n}_sheet.png")
        sheet.save(path)
        print(path)


if __name__ == "__main__":
    main()
