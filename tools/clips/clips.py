#!/usr/bin/env python3
"""Turn the clips keyed in docs/mascot/yoru.riv into tables the eye engine plays.

Input: tools/clips/keyframes.json, the keys of each `clip_<name>` timeline as
the Rive editor's MCP reports them (object id, property, frame at 60 fps,
value, interpolation and its cubic curve). Output: main/clips.h and
main/clips.c, one frame every 1/30 s with the rig's values already eased, so
the board only blends two neighbouring frames.

The rig's numbers stay in the Rive file's units (the board scales them by
LOOK_SCALE, as it does the gestures). Hands are made relative to the body, as
the board places them, and in front of it where they overlap it. A clip whose
name ends in `_loop` loops. The hands' rotation and the face's opacity have
no counterpart on the board yet and are left out.

The effects: `Prop` (a thing held, relative to the body like the hands; the
board draws it as a card with the service's glyph), `Halo`'s opacity (a glow
round the body) and `Stars`' opacity (how fast sparkles fly). Their colour is
not in the clip: whoever plays it picks one (the service's, the memory's).

Usage: python3 tools/clips/clips.py
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
KEYS = os.path.join(HERE, "keyframes.json")
OUT_H = os.path.join(ROOT, "main", "clips.h")
OUT_C = os.path.join(ROOT, "main", "clips.c")

# Rive timelines run at 60 fps; the board's frames are 1/30 s.
RIVE_FPS = 60
CLIP_FPS = 30

# The rig's nodes in yoru.riv, and their values at rest (the design pose)
# for a property a clip doesn't key.
CHARACTER, BODY, HAND_L, HAND_R, LOOK = "0-17", "0-19", "0-20", "0-21", "0-5637"
PROP, HALO, STARS = "3-7167", "0-18", "0-5788"
EYES = [  # node, shape, top-left, top-right and bottom-middle vertices
    {"node": "0-77", "shape": "0-79", "tl": "0-81", "tr": "0-82", "bm": "0-84", "x": -32},
    {"node": "0-78", "shape": "0-88", "tl": "0-90", "tr": "0-91", "bm": "0-93", "x": 32},
]
REST = {
    (CHARACTER, "sx"): 100,
    (BODY, "x"): 0, (BODY, "y"): 0, (BODY, "sx"): 100, (BODY, "sy"): 100, (BODY, "r"): 0,
    (HAND_L, "x"): -112, (HAND_L, "y"): 40, (HAND_L, "sx"): 0,
    (HAND_R, "x"): 112, (HAND_R, "y"): 40, (HAND_R, "sx"): 0,
    (LOOK, "x"): 0, (LOOK, "y"): 0, (LOOK, "sx"): 100,
    (PROP, "x"): 0, (PROP, "y"): 110, (PROP, "sx"): 0,
    (HALO, "opacity"): 0, (STARS, "opacity"): 0,
}
for e in EYES:
    REST.update({(e["node"], "x"): e["x"], (e["shape"], "sx"): 100, (e["shape"], "sy"): 100,
                 (e["tl"], "y"): -30, (e["tr"], "y"): -30, (e["bm"], "y"): 30})
# The eye's half height, and its bottom-middle vertex at its happiest (^ ^).
EYE_HALF, BM_HAPPY = 30.0, -12.0
# Rive's happy eye is 0.6 tall; the board's lid cuts it instead, at 0.95.
HAPPY_EH = 0.6
# A hand this near the body's centre, and out, is drawn in front of it.
FRONT_R = 120
FIELDS = ["bx", "by", "bsx", "bsy", "rot", "near",
          "lx", "ly", "ls", "rx", "ry", "rs", "front",
          "fx", "fy", "turn",
          "lex", "lew", "leh", "ltl", "ltr", "lbot",
          "rex", "rew", "reh", "rtl", "rtr", "rbot",
          "px", "py", "ps", "glow", "spark"]


def bezier_ease(x1, y1, x2, y2, t):
    """CSS-style cubic: the curve's y where its x is t."""
    def coord(a, b, u):
        return 3 * a * u * (1 - u) ** 2 + 3 * b * u * u * (1 - u) + u ** 3
    lo, hi = 0.0, 1.0
    for _ in range(40):
        mid = (lo + hi) / 2
        if coord(x1, x2, mid) < t:
            lo = mid
        else:
            hi = mid
    return coord(y1, y2, (lo + hi) / 2)


def track_value(keys, frame, rest):
    if not keys:
        return rest
    if frame <= keys[0]["frame"]:
        return keys[0]["value"]
    for a, b in zip(keys, keys[1:]):
        if a["frame"] <= frame < b["frame"]:
            t = (frame - a["frame"]) / (b["frame"] - a["frame"])
            kind = a["interpolation"]
            if kind == "hold":
                k = 0.0
            elif kind == "cubic":
                k = bezier_ease(*a["cubic"], t)
            else:
                k = t
            return a["value"] + (b["value"] - a["value"]) * k
    return keys[-1]["value"]


def sample(rows):
    tracks = {}
    for r in rows:
        tracks.setdefault((r["object"], r["property"]), []).append(r)
    for keys in tracks.values():
        keys.sort(key=lambda r: r["frame"])
    last = max(r["frame"] for r in rows)

    def v(obj, prop, f):
        return track_value(tracks.get((obj, prop)), f, REST[(obj, prop)])

    frames = []
    for i in range(last * CLIP_FPS // RIVE_FPS + 1):
        f = i * RIVE_FPS / CLIP_FPS
        bx, by = v(BODY, "x", f), v(BODY, "y", f)
        hands = []
        for h in (HAND_L, HAND_R):
            hands += [v(h, "x", f) - bx, v(h, "y", f) - by, v(h, "sx", f) / 100]
        front = any(s > 0.6 and (x * x + y * y) ** 0.5 < FRONT_R
                    for x, y, s in (hands[0:3], hands[3:6]))
        eyes = []
        for e in EYES:
            bottom = min(1.0, max(0.0, (EYE_HALF - v(e["bm"], "y", f)) / (EYE_HALF - BM_HAPPY)))
            # Where Rive draws the happy eye short, the board keeps it tall and
            # lets the bottom lid make the arch.
            eh = v(e["shape"], "sy", f) / 100 + (0.95 - HAPPY_EH) * bottom
            lid = lambda vert: min(1.0, max(0.0, (v(vert, "y", f) + EYE_HALF) / (2 * EYE_HALF)))
            eyes += [v(e["node"], "x", f), v(e["shape"], "sx", f) / 100, eh, lid(e["tl"]), lid(e["tr"]), bottom]
        frames.append([bx, by, v(BODY, "sx", f) / 100, v(BODY, "sy", f) / 100, v(BODY, "r", f),
                       v(CHARACTER, "sx", f) / 100]
                      + hands + [1.0 if front else 0.0]
                      + [v(LOOK, "x", f), v(LOOK, "y", f), v(LOOK, "sx", f) / 100] + eyes
                      + [v(PROP, "x", f) - bx, v(PROP, "y", f) - by, v(PROP, "sx", f) / 100,
                         v(HALO, "opacity", f) / 100, v(STARS, "opacity", f) / 100])
    return frames


def fmt(x):
    s = f"{x:.3f}".rstrip("0").rstrip(".")
    return (s if s not in ("-0", "") else "0") + ("f" if "." in s else "")


def main():
    clips = json.load(open(KEYS))
    names = list(clips)
    hdr = [
        "#pragma once",
        "",
        "/* Generated by tools/clips/clips.py from tools/clips/keyframes.json (the",
        " * clip_* timelines of docs/mascot/yoru.riv). Don't edit: key the clip in",
        " * Rive, fetch its keys and run the script again. */",
        "",
        "#include <stdbool.h>",
        "#include <stdint.h>",
        "",
        "/* One frame of a clip, every 1/30 s, in the Rive file's units. The body:",
        " * offset, scale, tilt (degrees) and nearness (Character's scale: 2 twice",
        " * as near). The hands from the body's centre (scale 0 tucked), and whether",
        " * they are in front of it. The face's offset and width (a turn narrows it).",
        " * Each eye (l, r): its x on the face, its scale, the top lid's cut at its",
        " * left and right edge (0 none, 1 shut) and the bottom lid (1: ^ ^).",
        " * The effects, in the colour the player picks: the prop from the body's",
        " * centre and its scale (0 hidden), the glow round the body (0..1) and how",
        " * fast sparkles fly (0..1). */",
        "typedef struct {",
        "    float bx, by, bsx, bsy, rot, near;",
        "    float lx, ly, ls, rx, ry, rs, front;",
        "    float fx, fy, turn;",
        "    float lex, lew, leh, ltl, ltr, lbot;",
        "    float rex, rew, reh, rtl, rtr, rbot;",
        "    float px, py, ps, glow, spark;",
        "} clip_frame_t;",
        "",
        "typedef enum {",
        "    CLIP_NONE,",
    ] + [f"    CLIP_{n.upper()}," for n in names] + [
        "    CLIP_COUNT,",
        "} clip_id_t;",
        "",
        "typedef struct {",
        "    const char *name;",
        "    const clip_frame_t *frames;",
        "    uint16_t n;",
        "    bool loop; // plays until stopped",
        "} clip_t;",
        "",
        "/** The clip, or NULL for CLIP_NONE or an unknown one. */",
        "const clip_t *clips_get(clip_id_t id);",
        "/** The clip called `name` (\"sneeze\"), or CLIP_NONE. */",
        "clip_id_t clips_find(const char *name);",
        "",
    ]
    src = [
        '#include "clips.h"',
        "",
        "/* Generated by tools/clips/clips.py: don't edit. */",
        "",
        "#include <stddef.h>",
        "#include <string.h>",
        "",
    ]
    for n in names:
        frames = sample(clips[n])
        src.append(f"static const clip_frame_t {n.upper()}[] = {{")
        for fr in frames:
            src.append("    {" + ", ".join(fmt(x) for x in fr) + "},")
        src.append("};")
        src.append("")
        print(f"{n}: {len(frames)} frames, {len(frames) / CLIP_FPS:.2f} s", file=sys.stderr)
    src.append("static const clip_t CLIPS[CLIP_COUNT] = {")
    for n in names:
        src.append(f'    [CLIP_{n.upper()}] = {{"{n}", {n.upper()}, sizeof({n.upper()}) / sizeof({n.upper()}[0]), {"true" if n.endswith("_loop") else "false"}}},')
    src += [
        "};",
        "",
        "const clip_t *clips_get(clip_id_t id)",
        "{",
        "    if (id <= CLIP_NONE || id >= CLIP_COUNT) {",
        "        return NULL;",
        "    }",
        "    return &CLIPS[id];",
        "}",
        "",
        "clip_id_t clips_find(const char *name)",
        "{",
        "    for (int i = CLIP_NONE + 1; i < CLIP_COUNT; i++) {",
        "        if (name != NULL && strcmp(name, CLIPS[i].name) == 0) {",
        "            return (clip_id_t) i;",
        "        }",
        "    }",
        "    return CLIP_NONE;",
        "}",
        "",
    ]
    open(OUT_H, "w").write("\n".join(hdr))
    open(OUT_C, "w").write("\n".join(src))


if __name__ == "__main__":
    main()
