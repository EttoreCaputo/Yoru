"""Key clips into the open Rive file from pose lists (30 fps frames, Rive units).

Each library*.py here holds clips as functions returning a Clip, gathered in
a dict NEW; all_clips() collects them all. After pushing, fetch the keys
(tools/clips/fetch.py) and generate the tables (tools/clips/clips.py);
tools/clips/preview.py --library draws a clip before it is pushed.

python3 -I author.py push NAME...      create clip_NAME timelines (refuses if one exists)
python3 -I author.py pose NAME FRAME   set the design pose to that frame (for a capture)
python3 -I author.py rest              put the design pose back
"""
import glob
import importlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from fetch import Rive  # noqa: E402
from clips import track_value  # noqa: E402

CHAR, BODY, HL, HR, LOOK = "0-17", "0-19", "0-20", "0-21", "0-5637"
EL, ER, SL, SR = "0-77", "0-78", "0-79", "0-88"
LTL, LTR, LBM, RTL, RTR, RBM = "0-81", "0-82", "0-84", "0-90", "0-91", "0-93"
PROP = {"x": 13, "y": 14, "r": 15, "sx": 16, "sy": 17, "o": 18, "vy": 25, "rad": 26}
CURVE = {"io": [0.45, 0, 0.55, 1], "o": [0.2, 0, 0, 1], "i": [0.4, 0, 1, 1]}
REST = {(CHAR, "sx"): 100, (CHAR, "sy"): 100, (BODY, "x"): 0, (BODY, "y"): 0, (BODY, "sx"): 100,
        (BODY, "sy"): 100, (BODY, "r"): 0, (HL, "x"): -168, (HL, "y"): 40, (HL, "sx"): 100,
        (HL, "sy"): 100, (HR, "x"): 168, (HR, "y"): 40, (HR, "sx"): 100, (HR, "sy"): 100,
        (LOOK, "x"): 0, (LOOK, "y"): 0, (LOOK, "sx"): 100, (EL, "x"): -32, (ER, "x"): 32,
        (SL, "sx"): 100, (SL, "sy"): 100, (SR, "sx"): 100, (SR, "sy"): 100,
        (LTL, "vy"): -30, (LTR, "vy"): -30, (RTL, "vy"): -30, (RTR, "vy"): -30,
        (LBM, "vy"): 30, (RBM, "vy"): 30, (LBM, "rad"): 0, (RBM, "rad"): 0}
REST.update({("3-7167", "x"): 0, ("3-7167", "y"): 110, ("3-7167", "sx"): 0, ("3-7167", "sy"): 0,
             ("0-18", "o"): 0, ("0-5788", "o"): 0})
TUCK_L, TUCK_R = (-112, 40, 0), (112, 40, 0)
PROPOBJ, HALO, STARS = "3-7167", "0-18", "0-5788"


def lid(t):
    """A top-lid cut (0 open .. 1 shut) as the corner vertex's y."""
    return -30 + 60 * t


class Clip:
    def __init__(self, name, frames):
        self.name, self.frames = name, frames
        self.tracks = {}
        self.hands = {HL: [], HR: []}
        self.props = []

    def k(self, obj, prop, *keys):
        """keys: (frame, value[, ease]) with ease io/o/i/h/l; io by default."""
        self.tracks.setdefault((obj, prop), []).extend(keys)

    def hand(self, obj, *keys):
        """keys: (frame, (x, y, s)[, ease]), x and y from the body's centre."""
        self.hands[obj].extend(keys)

    def prop(self, *keys):
        """The held thing: keys (frame, (x, y, s)[, ease]) from the body's centre."""
        self.props.extend(keys)

    def glow(self, *keys):
        """keys (frame, 0..1[, ease])."""
        self.k(HALO, "o", *[(k[0], k[1] * 100) + tuple(k[2:]) for k in keys])

    def spark(self, *keys):
        self.k(STARS, "o", *[(k[0], k[1] * 100) + tuple(k[2:]) for k in keys])

    def eyes(self, prop, *keys, which="both"):
        objs = {"both": [SL, SR], "l": [SL], "r": [SR]}[which]
        for o in objs:
            self.k(o, prop, *keys)

    def rows(self):
        """Keyframes as the Rive MCP takes them, and as keyframes.json stores them."""
        out = []

        def add(obj, prop, keys):
            for key in keys:
                f, val = key[0], key[1]
                ease = key[2] if len(key) > 2 else "io"
                r = {"object": obj, "property": prop, "frame": f * 2, "value": val,
                     "interpolation": {"io": "cubic", "o": "cubic", "i": "cubic", "h": "hold", "l": "linear"}[ease]}
                if r["interpolation"] == "cubic":
                    r["cubic"] = CURVE[ease]
                out.append(r)

        for (obj, prop), keys in self.tracks.items():
            add(obj, prop, sorted(keys, key=lambda k: k[0]))
        body = {p: sorted([r for r in out if r["object"] == BODY and r["property"] == p], key=lambda r: r["frame"])
                for p in ("x", "y")}
        items = list(self.hands.items()) + ([(PROPOBJ, self.props)] if self.props else [])
        for obj, keys in items:
            keys = sorted(keys, key=lambda k: k[0])
            if not keys:
                keys = [(0, TUCK_L if obj == HL else TUCK_R, "h")]
            for prop, idx in (("x", 0), ("y", 1)):
                ks = []
                for key in keys:
                    f = key[0]
                    off = track_value(body[prop], f * 2, 0) if body[prop] else 0
                    ks.append((f, round(key[1][idx] + off, 2)) + tuple(key[2:]))
                add(obj, prop, ks)
            for prop in ("sx", "sy"):
                add(obj, prop, [(key[0], key[1][2] * 100) + tuple(key[2:]) for key in keys])
        return out

    def push(self, rive, aid=None):
        if aid is None:
            res = rive.call("animation_editor", {"command": "createLinearAnimations",
                                                 "data": {"linearAnimations": [{"name": "clip_" + self.name,
                                                                                "duration": self.frames / 30}]}})
            aid = res["animations"][0]["id"]
        else:
            old = rive.call("animation_editor", {"command": "queryKeyFrames",
                                                 "data": {"animationIds": [aid]}})["keyframes"][aid]
            if old:
                rive.call("animation_editor", {"command": "modifyKeyFrames",
                                               "data": {"animationId": aid, "delete": [k["keyframeId"] for k in old]}})
        add = []
        for r in self.rows():
            a = {"objectId": r["object"], "propertyKey": PROP[r["property"]], "frame": r["frame"],
                 "value": r["value"], "interpolationType": r["interpolation"]}
            if "cubic" in r:
                a["cubicParams"] = dict(zip(["x1", "y1", "x2", "y2"], r["cubic"]))
            add.append(a)
        res = rive.call("animation_editor", {"command": "modifyKeyFrames", "data": {"animationId": aid, "add": add}})
        print(self.name, aid, len(add), "keys", "ok" if res.get("success") else res)

    def pose(self, rive, frame):
        rows = self.rows()
        tracks = {}
        for r in rows:
            tracks.setdefault((r["object"], r["property"]), []).append(r)
        vals = {}
        for (obj, prop), ks in tracks.items():
            ks.sort(key=lambda r: r["frame"])
            vals.setdefault(obj, {})[str(PROP[prop])] = round(track_value(ks, frame * 2, 0), 3)
        rive.call("set_property_values", {"propertyValues": vals})


def all_clips():
    """Every clip in the library*.py files next to this one, by name."""
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, here)
    out = {}
    for path in sorted(glob.glob(os.path.join(here, "library*.py"))):
        out.update(getattr(importlib.import_module(os.path.basename(path)[:-3]), "NEW", {}))
    return out


def rest(rive):
    vals = {}
    for (obj, prop), v in REST.items():
        vals.setdefault(obj, {})[str(PROP[prop])] = v
    rive.call("set_property_values", {"propertyValues": vals})


if __name__ == "__main__":
    CLIPS = all_clips()
    rive = Rive()
    cmd = sys.argv[1]
    if cmd == "push":
        names = [a["name"] for a in rive.call("animation_editor", {"command": "listLinearAnimations"})["linearAnimations"]]
        for n in sys.argv[2:]:
            if "clip_" + n in names:
                print(n, "exists, not pushed")
                continue
            CLIPS[n]().push(rive)
    elif cmd == "repush":
        anims = rive.call("animation_editor", {"command": "listLinearAnimations"})["linearAnimations"]
        ids = {a["name"]: a["id"] for a in anims}
        for n in sys.argv[2:]:
            CLIPS[n]().push(rive, ids["clip_" + n])
    elif cmd == "pose":
        CLIPS[sys.argv[2]]().pose(rive, float(sys.argv[3]))
        print("posed", sys.argv[2], sys.argv[3])
    elif cmd == "rest":
        rest(rive)
        print("rest")
