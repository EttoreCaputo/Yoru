"""The fifth batch: moods and the thinking loop, replacing procedural moods.
Frames at 30 fps; Rive units (screen px / 0.7)."""
from author import (BODY, HL, HR, LOOK, LTL, LTR, RTL, RTR, TUCK_L, TUCK_R, Clip, lid)
from library import near

LIDS = (LTL, LTR, RTL, RTR)
INNER, OUTER = (LTR, RTL), (LTL, RTR)


def still(c):
    """Key the last frame, so the clip runs its full length and ends on a still rest."""
    c.k(BODY, "y", (c.frames, 0, "h"))
    for h, tuck in ((HL, TUCK_L), (HR, TUCK_R)):
        if c.hands[h]:
            c.hand(h, (c.frames, tuck, "h"))
    return c


def focus():
    """Focus: a tiny pull back, then it leans in (nearer), the lids come down
    flat, a small determined nod, and it eases back with the eyes open.
    Plays on "focus"."""
    c = Clip("focus", 54)
    near(c, (0, 100, "h"), (2, 100), (6, 97, "i"), (14, 113, "o"), (18, 111, "h"), (36, 111), (44, 98), (48, 100, "h"))
    c.k(BODY, "y", (0, 0, "h"), (2, 0), (6, -4, "i"), (14, 4, "o"), (18, 3, "h"), (22, 3, "i"), (25, 12, "o"),
        (29, -2), (33, 3, "h"), (36, 3), (44, -2), (48, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (2, 100), (6, 97, "i"), (14, 104, "o"), (18, 102, "h"), (22, 102, "i"),
        (25, 107, "o"), (29, 98), (33, 102, "h"), (36, 102), (44, 99), (48, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (2, 100), (6, 103, "i"), (14, 96, "o"), (18, 98, "h"), (22, 98, "i"),
        (25, 93, "o"), (29, 102), (33, 98, "h"), (36, 98), (44, 101), (48, 100, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (22, 0, "i"), (25, 8, "o"), (30, 0, "h"))
    for v in LIDS:
        c.k(v, "vy", (0, -30, "h"), (6, -30), (14, lid(0.42), "h"), (36, lid(0.42)), (44, -30, "h"))
    c.eyes("sy", (0, 100, "h"), (6, 100), (14, 92, "h"), (36, 92), (42, 106), (47, 100, "h"))
    c.eyes("sx", (0, 100, "h"), (6, 100), (14, 104, "h"), (36, 104), (44, 100, "h"))
    return still(c)


def angry_huff():
    """Angry: a crouch, then it puffs up wider with a red glow, the lids
    slanting down to the nose and fists at its sides; two quick stomps, a
    huff out, and it deflates back to rest. Plays on "angry". Glow: red."""
    c = Clip("angry_huff", 66)
    near(c, (0, 100, "h"), (3, 100), (6, 98, "i"), (12, 107, "o"), (16, 106, "h"), (44, 106), (54, 98), (58, 100, "h"))
    c.k(BODY, "y", (0, 0, "h"), (3, 0), (6, 6, "i"), (12, -4, "o"), (16, 0, "h"),
        (18, 0), (20, -14, "i"), (23, 9, "o"), (27, 0, "h"),
        (29, 0), (31, -14, "i"), (34, 9, "o"), (38, 0, "h"),
        (40, 0), (43, -7, "i"), (46, 5, "o"), (54, -2), (58, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (3, 100), (6, 106, "i"), (12, 110, "o"), (16, 108, "h"),
        (18, 108), (20, 102, "i"), (23, 118, "o"), (27, 108, "h"),
        (29, 108), (31, 102, "i"), (34, 118, "o"), (38, 108, "h"),
        (40, 108), (43, 102, "i"), (46, 114, "o"), (54, 98), (58, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (3, 100), (6, 94, "i"), (12, 97, "o"), (16, 96, "h"),
        (18, 96), (20, 104, "i"), (23, 86, "o"), (27, 96, "h"),
        (29, 96), (31, 104, "i"), (34, 86, "o"), (38, 96, "h"),
        (40, 96), (43, 104, "i"), (46, 90, "o"), (54, 102), (58, 100, "h"))
    c.k(BODY, "r", (0, 0, "h"), (18, 0), (23, -3), (27, 0, "h"), (29, 0), (34, 3), (38, 0, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (3, 0), (10, 6, "h"), (44, 6), (52, 0, "h"))
    for v in INNER:
        c.k(v, "vy", (0, -30, "h"), (3, -30), (10, lid(0.55), "h"), (46, lid(0.55)), (54, -30, "h"))
    for v in OUTER:
        c.k(v, "vy", (0, -30, "h"), (3, -30), (10, lid(0.06), "h"), (46, lid(0.06)), (54, -30, "h"))
    c.eyes("sx", (0, 100, "h"), (3, 100), (10, 110, "h"), (46, 110), (54, 100, "h"))
    c.eyes("sy", (0, 100, "h"), (3, 100), (10, 92, "h"), (46, 92), (54, 100, "h"))
    # Fists at its sides, a beat behind the body on each stomp.
    c.hand(HL, (0, TUCK_L, "h"), (6, TUCK_L, "o"), (11, (-158, 62, 1.08)), (14, (-152, 70, 1), "h"),
           (19, (-152, 70, 1)), (22, (-152, 50, 1), "i"), (25, (-152, 80, 1), "o"), (29, (-152, 70, 1), "h"),
           (30, (-152, 70, 1)), (33, (-152, 50, 1), "i"), (36, (-152, 80, 1), "o"), (40, (-152, 70, 1), "h"),
           (44, (-152, 70, 1)), (48, (-160, 78, 1.04), "i"), (55, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (7, TUCK_R, "o"), (12, (158, 62, 1.08)), (15, (152, 70, 1), "h"),
           (19, (152, 70, 1)), (22, (152, 50, 1), "i"), (25, (152, 80, 1), "o"), (29, (152, 70, 1), "h"),
           (30, (152, 70, 1)), (33, (152, 50, 1), "i"), (36, (152, 80, 1), "o"), (40, (152, 70, 1), "h"),
           (45, (152, 70, 1)), (49, (160, 78, 1.04), "i"), (56, TUCK_R, "h"))
    c.glow((0, 0, "h"), (3, 0), (12, 0.6, "h"), (23, 0.6, "i"), (24, 0.8, "o"), (30, 0.6, "h"), (34, 0.6, "i"),
           (35, 0.85, "o"), (42, 0.6), (46, 0.7), (58, 0, "h"))
    return still(c)


def hot_fan():
    """Hot: it wilts (lower, wider, tilted), half-lidded, and fans its face
    fast with its right hand along a little arc beside the face; a warm glow;
    then it straightens up. Plays on "hot". Glow: warm orange."""
    c = Clip("hot_fan", 72)
    c.k(BODY, "y", (0, 0, "h"), (3, 0), (16, 12, "h"), (30, 12), (38, 16, "h"), (52, 16), (60, -3), (64, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (3, 100), (16, 106, "h"), (30, 106), (38, 108, "h"), (52, 108), (60, 98),
        (64, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (3, 100), (16, 94, "h"), (30, 94), (38, 92, "h"), (52, 92), (60, 102),
        (64, 100, "h"))
    c.k(BODY, "r", (0, 0, "h"), (3, 0), (16, -4, "h"), (52, -4), (62, 0, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (3, 0), (14, 8, "h"), (52, 8), (60, 0, "h"))
    c.k(LOOK, "x", (0, 0, "h"), (3, 0), (14, -6, "h"), (52, -6), (60, 0, "h"))
    for v in LIDS:
        c.k(v, "vy", (0, -30, "h"), (3, -30), (14, lid(0.45), "h"), (40, lid(0.45)), (44, lid(0.52), "h"),
            (52, lid(0.52)), (60, -30, "h"))
    c.eyes("sy", (0, 100, "h"), (3, 100), (14, 92, "h"), (30, 92, "l"), (31, 8, "l"), (33, 92, "h"), (52, 92),
           (58, 104), (62, 100, "h"))
    c.eyes("sx", (0, 100, "h"), (3, 100), (14, 104, "h"), (52, 104), (60, 100, "h"))
    # The fan: up (A) and down (B) round an outer middle (M), fast through M.
    a, m, b = (170, -68, 1.1), (206, -34, 1.1), (178, 4, 1.1)
    keys = [(0, TUCK_R, "h"), (5, TUCK_R, "o"), (11, (176, -50, 1.18)), (14, a, "i")]
    f, up = 14, True
    while f < 46:
        keys += [(f + 2, m, "o"), (f + 4, b if up else a, "i")]
        f, up = f + 4, not up
    keys[-1] = (f, keys[-1][1])
    keys += [(f + 10, TUCK_R, "h")]
    c.hand(HR, *keys)
    c.glow((0, 0, "h"), (3, 0), (16, 0.55, "h"), (46, 0.55), (62, 0, "h"))
    return still(c)


def sleepy_sway():
    """Sleepy: the lids get heavy, the body sags and sways, the head dips
    and it catches itself with a little jolt, then settles with the lids at
    half. Plays on "sleepy", "tired", "drowsy". Hands over to the sleepy
    face: it ends with the top lids at half."""
    c = Clip("sleepy_sway", 78)
    c.k(BODY, "y", (0, 0, "h"), (3, 0), (16, 8), (30, 12), (40, 14, "i"), (45, 26, "o"), (47, 26, "o"),
        (50, -6, "i"), (54, 4, "o"), (60, 0), (66, 0, "h"))
    c.k(BODY, "x", (0, 0, "h"), (3, 0), (14, -6), (26, 6), (38, -5), (46, 0, "h"))
    c.k(BODY, "r", (0, 0, "h"), (3, 0), (14, -4), (26, 4), (38, -3), (45, -7, "o"), (47, -7), (54, 2), (62, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (3, 100), (30, 104), (40, 105, "i"), (45, 108, "o"), (47, 108, "o"),
        (50, 94, "i"), (54, 104, "o"), (60, 99), (66, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (3, 100), (30, 96), (40, 95, "i"), (45, 92, "o"), (47, 92, "o"),
        (50, 108, "i"), (54, 96, "o"), (60, 101), (66, 100, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (3, 0), (30, 6), (45, 14, "h"), (47, 14), (50, -6), (60, 0, "h"))
    for v in LIDS:
        c.k(v, "vy", (0, -30, "h"), (3, -30), (14, lid(0.55)), (20, lid(0.45)), (34, lid(0.72)), (44, lid(0.92), "h"),
            (47, lid(0.92), "o"), (49, lid(0.15)), (58, lid(0.4)), (68, lid(0.5), "h"), (78, lid(0.5), "h"))
    c.eyes("sy", (0, 100, "h"), (3, 100), (30, 94), (44, 88, "h"), (47, 88, "o"), (49, 118), (60, 100, "h"))
    c.eyes("sx", (0, 100, "h"), (3, 100), (30, 104, "h"), (47, 104, "o"), (49, 108), (60, 100, "h"))
    return still(c)


def sigh():
    """A sigh: a slow inhale (rises, stretches, eyes a touch wider, shoulders
    up), a long exhale (sinks and squashes wide, hands drop, lids lower),
    a look off to the side, and back. Plays on "sigh" and the bored sigh."""
    c = Clip("sigh", 66)
    c.k(BODY, "y", (0, 0, "h"), (3, 0), (18, -12, "h"), (22, -12), (42, 25, "o"), (50, 24, "h"), (52, 24),
        (59, -3), (62, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (3, 100), (18, 95, "h"), (22, 95), (42, 112, "o"), (50, 111, "h"), (52, 111),
        (59, 98), (62, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (3, 100), (18, 107, "h"), (22, 107), (42, 88, "o"), (50, 89, "h"), (52, 89),
        (59, 102), (62, 100, "h"))
    near(c, (0, 100, "h"), (3, 100), (18, 102, "h"), (22, 102), (42, 97, "h"), (52, 97), (60, 100, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (3, 0), (18, -8, "h"), (22, -8), (40, 8, "h"), (52, 8), (60, 0, "h"))
    c.k(LOOK, "x", (0, 0, "h"), (26, 0), (36, 32, "h"), (50, 32), (60, 0, "h"))
    for v in LIDS:
        c.k(v, "vy", (0, -30, "h"), (22, -30), (40, lid(0.4), "h"), (52, lid(0.4)), (60, -30, "h"))
    c.eyes("sy", (0, 100, "h"), (3, 100), (18, 110, "h"), (22, 110), (40, 92, "h"), (52, 92), (60, 100, "h"))
    c.eyes("sx", (0, 100, "h"), (3, 100), (18, 104, "h"), (22, 104), (40, 100, "h"))
    c.hand(HL, (0, TUCK_L, "h"), (4, TUCK_L, "o"), (10, (-150, 24, 0.95)), (20, (-148, 10, 0.95), "h"),
           (24, (-148, 10, 0.95)), (44, (-150, 96, 0.9), "h"), (50, (-150, 96, 0.9)), (58, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (5, TUCK_R, "o"), (11, (150, 24, 0.95)), (21, (148, 10, 0.95), "h"),
           (25, (148, 10, 0.95)), (45, (150, 96, 0.9), "h"), (51, (150, 96, 0.9)), (59, TUCK_R, "h"))
    return still(c)


def scared_cover():
    """Scared: it flinches back (farther, smaller), both hands fly up over
    its eyes, it trembles, the right hand slides down so one wide eye peeks
    out, then the hands lower and it comes back. Plays on "scared"."""
    c = Clip("scared_cover", 72)
    near(c, (0, 100, "o"), (3, 86, "h"), (54, 86), (62, 103), (66, 100, "h"))
    c.k(BODY, "y", (0, 0, "o"), (3, -12), (8, 4), (12, 2, "h"), (54, 2), (62, 0, "h"))
    c.k(BODY, "sx", (0, 100, "o"), (3, 92), (8, 105), (12, 103, "h"), (54, 103), (60, 98), (64, 100, "h"))
    c.k(BODY, "sy", (0, 100, "o"), (3, 110), (8, 95), (12, 97, "h"), (54, 97), (60, 102), (64, 100, "h"))
    # The tremble: fast, then calmer once it dares to peek.
    shake = [(12, 0, "l")]
    for i, f in enumerate(range(14, 40, 2)):
        shake.append((f, 4 if i % 2 else -4, "l"))
    for i, f in enumerate(range(40, 52, 3)):
        shake.append((f, 2 if i % 2 else -2, "l"))
    c.k(BODY, "x", (0, 0, "h"), *shake, (54, 0, "h"))
    c.eyes("sy", (0, 100, "o"), (2, 130), (5, 12, "h"), (36, 12, "h"), (54, 12), (60, 100, "h"), which="l")
    c.eyes("sy", (0, 100, "o"), (2, 130), (5, 12, "h"), (36, 12), (40, 128, "h"), (46, 128, "l"), (47, 8, "l"),
           (49, 128, "h"), (54, 128), (60, 100, "h"), which="r")
    c.eyes("sx", (0, 100, "o"), (2, 115), (6, 100, "h"), (36, 100), (40, 112, "h"), (54, 112), (60, 100, "h"),
           which="r")
    c.k(LOOK, "x", (0, 0, "h"), (38, 0), (42, 10, "h"), (54, 10), (60, 0, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (38, 0), (42, 4, "h"), (54, 4), (60, 0, "h"))
    c.hand(HL, (0, TUCK_L, "o"), (5, (-44, 2, 1.58)), (8, (-38, 12, 1.48), "h"), (54, (-38, 12, 1.48), "i"),
           (62, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "o"), (6, (44, 2, 1.58)), (9, (38, 12, 1.48), "h"), (36, (38, 12, 1.48)),
           (42, (60, 86, 1.25), "h"), (54, (60, 86, 1.25), "i"), (63, TUCK_R, "h"))
    return still(c)


def bored():
    """Bored: it slumps, half-lidded, eyes off to the side, the right hand
    drumming little taps beside it; the eyes roll round once and it pulls
    itself back up. Plays on "bored"."""
    c = Clip("bored", 90)
    c.k(BODY, "y", (0, 0, "h"), (3, 0), (16, 14, "h"), (72, 14), (82, -3), (85, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (3, 100), (16, 106, "h"), (72, 106), (82, 98), (85, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (3, 100), (16, 94, "h"), (72, 94), (82, 102), (85, 100, "h"))
    c.k(BODY, "r", (0, 0, "h"), (3, 0), (16, -4, "h"), (72, -4), (82, 0, "h"))
    near(c, (0, 100, "h"), (3, 100), (16, 98, "h"), (72, 98), (82, 100, "h"))
    # Off to the left (away from the drumming hand), then a roll up and over.
    c.k(LOOK, "x", (0, 0, "h"), (4, 0), (14, -30, "h"), (44, -30), (50, -18), (54, 0), (58, 24), (64, 10),
        (70, -30, "h"), (72, -30), (82, 0, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (4, 0), (14, 6, "h"), (44, 6), (50, -20), (54, -28), (58, -18), (64, 2),
        (70, 6, "h"), (72, 6), (82, 0, "h"))
    for v in LIDS:
        c.k(v, "vy", (0, -30, "h"), (4, -30), (14, lid(0.5), "h"), (46, lid(0.5)), (54, lid(0.3)), (64, lid(0.5), "h"),
            (72, lid(0.5)), (80, -30, "h"))
    c.eyes("sy", (0, 100, "h"), (4, 100), (14, 94, "h"), (36, 94, "l"), (37, 8, "l"), (39, 94, "h"), (72, 94),
           (80, 104), (84, 100, "h"))
    # Drumming: two bursts of four taps, quick down, slower up.
    up, dn = (160, 86, 0.9), (160, 100, 0.9)
    keys = [(0, TUCK_R, "h"), (6, TUCK_R, "o"), (12, (164, 80, 0.98)), (16, up, "h")]
    for start in (18, 50):
        for i in range(4):
            f = start + 4 * i
            keys += [(f, up, "i"), (f + 1, dn, "o")]
        keys += [(start + 16, up, "h")]
    keys += [(70, up), (78, TUCK_R, "h")]
    c.hand(HR, *keys)
    return still(c)




def shoo():
    """Annoyed: a frown, a wind-up, then the right hand pushes out at the
    viewer twice (big, low in front, below the eyes), a little shake of the
    head, and back to rest. Plays on "annoyed"."""
    c = Clip("shoo", 48)
    c.k(BODY, "r", (0, 0, "h"), (2, 0), (7, -5, "i"), (11, 4, "o"), (15, 1), (19, 4, "o"), (23, 1), (27, 0, "l"),
        (29, -4, "l"), (31, 4, "l"), (33, -3, "l"), (35, 2, "l"), (37, 0, "h"))
    c.k(BODY, "x", (0, 0, "h"), (26, 0, "l"), (28, -6, "l"), (30, 6, "l"), (32, -5, "l"), (34, 4, "l"), (36, 0, "h"))
    c.k(BODY, "y", (0, 0, "h"), (2, 0), (7, -4, "i"), (11, 3, "o"), (15, 0), (19, 3, "o"), (24, 0, "h"))
    near(c, (0, 100, "h"), (2, 100), (7, 97, "i"), (11, 105, "o"), (15, 101), (19, 105, "o"), (25, 100, "h"))
    for v in INNER:
        c.k(v, "vy", (0, -30, "h"), (2, -30), (7, lid(0.45), "h"), (36, lid(0.45)), (42, -30, "h"))
    for v in OUTER:
        c.k(v, "vy", (0, -30, "h"), (2, -30), (7, lid(0.1), "h"), (36, lid(0.1)), (42, -30, "h"))
    c.eyes("sy", (0, 100, "h"), (2, 100), (7, 88, "h"), (36, 88), (42, 100, "h"))
    c.eyes("sx", (0, 100, "h"), (2, 100), (7, 108, "h"), (36, 108), (42, 100, "h"))
    c.k(LOOK, "x", (0, 0, "h"), (26, 0, "l"), (28, -10, "l"), (30, 10, "l"), (32, -8, "l"), (34, 6, "l"),
        (36, 0, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (2, TUCK_R, "o"), (8, (80, 74, 1.02), "i"), (12, (24, 114, 1.8), "o"),
           (14, (28, 112, 1.72)), (17, (74, 80, 1.04), "i"), (20, (22, 116, 1.88), "o"), (23, (28, 112, 1.75)),
           (28, (72, 90, 1.05), "i"), (36, TUCK_R, "h"))
    return still(c)


def think_loop():
    """While the assistant thinks: from rest the right hand comes up under
    the chin (in front, below the eyes) as the eyes go up and right; slow
    small tilts, the eyes drift, two finger taps on the chin and a blink;
    then the hand goes down to rest. Each 3 s cycle is rest -> think -> rest
    so the loop can be entered and left at its boundary."""
    c = Clip("think_loop", 90)
    c.k(BODY, "r", (0, 0), (10, -4), (30, -2), (50, -5), (70, -3), (82, -4), (90, 0))
    c.k(BODY, "y", (0, 0), (10, -3), (30, 0), (50, -4), (70, 0), (82, -2), (90, 0))
    c.k(LOOK, "x", (0, 0), (9, 30), (28, 22), (46, 34), (64, 20), (80, 30), (90, 0))
    c.k(LOOK, "y", (0, 0), (9, -28), (28, -24), (46, -30), (64, -24), (80, -28), (90, 0))
    for v in LIDS:
        c.k(v, "vy", (0, -30), (9, lid(0.13), "h"), (81, lid(0.13)), (90, -30))
    c.eyes("sy", (0, 100), (9, 100, "h"), (55, 100, "l"), (56, 8, "l"), (58, 100, "h"), (90, 100), which="l")
    c.eyes("sy", (0, 100), (9, 88, "h"), (55, 88, "l"), (56, 8, "l"), (58, 88, "h"), (81, 88), (90, 100), which="r")
    chin, tap = (28, 114, 1.15), (28, 106, 1.15)
    c.hand(HR, (0, TUCK_R, "o"), (7, (34, 110, 1.22)), (10, chin, "h"), (30, chin), (32, tap), (34, chin),
           (36, tap), (38, chin, "h"), (81, chin, "i"), (90, TUCK_R))
    return c


NEW = {f.__name__: f for f in (focus, angry_huff, hot_fan, sleepy_sway, sigh, scared_cover, bored, shoo,
                               think_loop)}
