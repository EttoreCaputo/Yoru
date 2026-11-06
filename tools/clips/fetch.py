#!/usr/bin/env python3
"""Fetch the keys of every clip_* timeline from the Rive editor into keyframes.json.

The Rive desktop app must be open on the "Yoru mascot" file (docs/mascot/yoru.riv);
its MCP server listens on 127.0.0.1:9791 while it is. Each key is saved with
its cubic curve, read from the key's interpolator. Then run clips.py.

Usage: python3 tools/clips/fetch.py
"""

import json
import os
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "keyframes.json")
URL = "http://127.0.0.1:9791/mcp"
# The cubic interpolator's x1 y1 x2 y2.
CUBIC_KEYS = [63, 64, 65, 66]


class Rive:
    def __init__(self):
        self.session = None
        self.id = 0
        self._post({"method": "initialize", "params": {
            "protocolVersion": "2025-11-25", "capabilities": {},
            "clientInfo": {"name": "yoru-clips", "version": "1"}}})

    def _post(self, body):
        self.id += 1
        body = dict(body, jsonrpc="2.0", id=self.id)
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.session:
            headers["mcp-session-id"] = self.session
        req = urllib.request.Request(URL, json.dumps(body).encode(), headers)
        with urllib.request.urlopen(req) as r:
            self.session = self.session or r.headers.get("mcp-session-id")
            raw = r.read().decode()
        data = [l[5:].strip() for l in raw.splitlines() if l.startswith("data:")]
        return json.loads(data[-1] if data else raw)

    def call(self, tool, args):
        res = self._post({"method": "tools/call", "params": {"name": tool, "arguments": args}})
        out = json.loads(res["result"]["content"][0]["text"])
        # Some tools wrap their answer once more.
        return json.loads(out["content"]) if isinstance(out.get("content"), str) else out


def main():
    rive = Rive()
    anims = rive.call("animation_editor", {"command": "listLinearAnimations"})["linearAnimations"]
    clips = {a["id"]: a["name"][len("clip_"):] for a in anims if a["name"].startswith("clip_")}
    keys = rive.call("animation_editor", {"command": "queryKeyFrames",
                                          "data": {"animationIds": list(clips)}})["keyframes"]
    interps = sorted({k["interpolatorId"] for ks in keys.values() for k in ks if "interpolatorId" in k})
    curves = rive.call("query_property_values",
                       {"propertyKeys": {i: CUBIC_KEYS for i in interps}})["values"] if interps else {}
    out = {}
    for aid, name in sorted(clips.items(), key=lambda c: c[1]):
        rows = []
        for k in sorted(keys.get(aid, []), key=lambda k: (k["objectId"], k["propertyName"], k["frame"])):
            r = {"object": k["objectId"], "property": k["propertyName"], "frame": k["frame"],
                 "value": round(k["value"], 3), "interpolation": k["interpolationType"]}
            if "interpolatorId" in k:
                c = curves[k["interpolatorId"]]
                r["cubic"] = [round(c[str(p)], 3) for p in CUBIC_KEYS]
            rows.append(r)
        out[name] = rows
        print(f"{name}: {len(rows)} keys")
    with open(OUT, "w") as f:
        json.dump(out, f, indent=0)


if __name__ == "__main__":
    main()
