"""Group C: what Yoru does on its own (the life acts of main/life_acts.c).

The board brings Yoru to the centre before it plays these, so each starts
and ends at offset 0,0 and does its travelling inside the clip. 30 fps;
Rive units (screen px / 0.7). Authored to Yoru's right; mirrored for the left.
"""
import math

from author import (BODY, HL, HR, LBM, LOOK, LTL, LTR, RBM, RTL, RTR, TUCK_L, TUCK_R, Clip, lid)
from library import happy, near

LIDS = (LTL, LTR, RTL, RTR)


def look_around():
    """A look round: the eyes snap left, then right (a blink on the way
    across), then up; the body turns after them two frames late, the face
    narrowing with each turn, the eyes a little wide and curious. Back to
    its own gaze with a small settle. Plays when Yoru idly looks about."""
    c = Clip("look_around", 72)
    c.k(LOOK, "x", (0, 0, "h"), (2, 0, "o"), (8, -44), (11, -40, "h"), (24, -40, "o"), (31, 44), (34, 40, "h"),
        (44, 40, "o"), (51, 6, "h"), (60, 6), (68, 0, "h"), (72, 0, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (2, 0, "o"), (8, 2, "h"), (44, 2, "o"), (50, -28), (53, -25, "h"), (60, -25),
        (68, 0, "h"), (72, 0, "h"))
    c.k(LOOK, "sx", (0, 100, "h"), (2, 100, "o"), (10, 86, "h"), (24, 86), (28, 100), (33, 86, "h"), (44, 86, "o"),
        (52, 100, "h"), (72, 100, "h"))
    c.k(BODY, "x", (0, 0, "h"), (4, 0), (13, -9), (16, -8, "h"), (26, -8), (36, 9), (39, 8, "h"), (46, 8),
        (56, -1), (60, 0, "h"), (72, 0, "h"))
    c.k(BODY, "r", (0, 0, "h"), (4, 0), (13, -4, "h"), (26, -4), (36, 4, "h"), (46, 4), (56, -1), (62, 0, "h"),
        (72, 0, "h"))
    c.k(BODY, "y", (0, 0, "h"), (2, 0), (5, 2), (10, 0, "h"), (44, 0), (48, 2), (54, -7), (57, -6, "h"), (60, -6),
        (67, 1), (71, 0, "h"), (72, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (2, 100), (5, 103), (10, 100, "h"), (44, 100), (48, 103), (54, 97, "h"), (60, 97),
        (67, 102), (71, 100, "h"), (72, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (2, 100), (5, 97), (10, 100, "h"), (44, 100), (48, 97), (54, 104, "h"), (60, 104),
        (67, 98), (71, 100, "h"), (72, 100, "h"))
    c.eyes("sy", (0, 100, "h"), (2, 100, "o"), (8, 110, "h"), (25, 110, "l"), (26, 8, "l"), (28, 110, "h"),
           (44, 110), (52, 116, "h"), (60, 116), (68, 100, "h"), (72, 100, "h"))
    c.eyes("sx", (0, 100, "h"), (2, 100, "o"), (8, 104, "h"), (60, 104), (68, 100, "h"), (72, 100, "h"))
    # Curious: the right eye a touch lidded while it glances.
    for v in (RTL, RTR):
        c.k(v, "vy", (0, -30, "h"), (2, -30), (10, lid(0.15), "h"), (44, lid(0.15)), (52, -30, "h"), (72, -30, "h"))
    return c


def peek_side():
    """Off to the very edge to peer out of the glass: a lean back, a glide
    right with the body leaning into it, an overshoot at the edge; it leans
    further out, face pushed against the glass, a hand up on its brow,
    curious (one eye wide, one lidded); a "hmm" bob with a hand at the chin
    and a blink; then it glides home looking back over its shoulder. Plays
    as the peek act."""
    c = Clip("peek_side", 120)
    c.k(BODY, "x", (0, 0, "h"), (2, 0), (6, -10), (30, 184), (36, 175, "h"), (40, 175), (48, 190, "h"), (84, 190),
        (88, 186), (110, -6), (115, 0, "h"), (120, 0, "h"))
    c.k(BODY, "y", (0, 0, "h"), (6, 0), (30, -8, "h"), (62, -8), (66, -16), (70, -6), (74, -9), (77, -8, "h"),
        (88, -8), (110, 2), (115, 0, "h"), (120, 0, "h"))
    c.k(BODY, "r", (0, 0, "h"), (4, 0), (10, -6), (22, 0), (30, 8), (36, 0, "h"), (40, 0), (48, 12, "h"), (84, 12),
        (94, 4), (104, 0), (110, -5), (116, 0, "h"), (120, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (4, 100), (8, 95), (18, 106), (30, 96), (36, 100, "h"), (62, 100), (66, 96),
        (70, 104), (74, 100, "h"), (84, 100), (92, 105), (104, 100), (110, 96), (116, 100, "h"), (120, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (4, 100), (8, 105), (18, 95), (30, 104), (36, 100, "h"), (62, 100), (66, 104),
        (70, 96), (74, 100, "h"), (84, 100), (92, 96), (104, 100), (110, 104), (116, 100, "h"), (120, 100, "h"))
    c.k(LOOK, "x", (0, 0, "h"), (2, 0, "o"), (10, 40, "h"), (40, 40), (48, 52, "h"), (84, 52), (92, 26, "h"),
        (102, 26), (112, 0, "h"), (120, 0, "h"))
    c.k(LOOK, "sx", (0, 100, "h"), (2, 100, "o"), (10, 88, "h"), (40, 88), (48, 80, "h"), (84, 80), (92, 90, "h"),
        (102, 90), (112, 100, "h"), (120, 100, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (40, 0), (48, -4, "h"), (84, -4), (92, 0, "h"), (120, 0, "h"))
    c.eyes("sy", (0, 100, "h"), (2, 100, "o"), (10, 108, "h"), (40, 108), (48, 116, "h"), (69, 116, "l"),
           (70, 8, "l"), (73, 116, "h"), (84, 116), (92, 104, "h"), (104, 104), (112, 100, "h"), (120, 100, "h"),
           which="l")
    c.eyes("sy", (0, 100, "h"), (2, 100, "o"), (10, 104, "h"), (40, 104), (48, 94, "h"), (69, 94, "l"),
           (70, 8, "l"), (73, 94, "h"), (84, 94), (92, 104, "h"), (104, 104), (112, 100, "h"), (120, 100, "h"),
           which="r")
    c.eyes("sx", (0, 100, "h"), (2, 100), (10, 104, "h"), (104, 104), (112, 100, "h"), (120, 100, "h"))
    for v in (RTL, RTR):
        c.k(v, "vy", (0, -30, "h"), (40, -30), (48, lid(0.22), "h"), (84, lid(0.22)), (92, -30, "h"), (120, -30, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (40, TUCK_R, "o"), (46, (70, -104, 1.1)), (50, (62, -98, 1.0), "h"),
           (82, (62, -98, 1.0), "i"), (90, TUCK_R, "h"), (120, TUCK_R, "h"))
    c.hand(HL, (0, TUCK_L, "h"), (56, TUCK_L, "o"), (62, (-40, 136, 0.95)), (66, (-36, 128, 0.9)),
           (70, (-36, 134, 0.9)), (74, (-36, 130, 0.9), "h"), (80, (-36, 130, 0.9), "i"), (88, TUCK_L, "h"),
           (120, TUCK_L, "h"))
    return c


def sniff():
    """Down to the bottom-right corner to sniff at something there: a little
    rise, then down and a bit nearer, leaning to the spot with paws out and
    eyes on it; four little sniffing hops back and forth (squash on each
    landing, the eyes scrunching); then a happy ^ ^ look up and a hop; and
    home with a settle. Plays as the sniff act."""
    c = Clip("sniff", 120)
    xs = [(36, 115), (47, 103), (58, 115), (69, 107), (80, 115)]
    x_keys = [(0, 0, "h"), (2, 0), (6, -8), (30, 120), (34, 115, "h")] + [(f, x) for f, x in xs]
    y_keys = [(0, 0, "h"), (2, 0), (6, -8), (30, 80), (34, 75, "h")]
    sx_keys = [(0, 100, "h"), (2, 100), (6, 96), (18, 104), (30, 106), (34, 100, "h")]
    sy_keys = [(0, 100, "h"), (2, 100), (6, 104), (18, 96), (30, 94), (34, 100, "h")]
    eye_sy = [(0, 100, "h"), (2, 100), (10, 104, "h"), (36, 104)]
    for i, (f, _) in enumerate(xs[:-1]):
        y_keys += [(f, 75, "o"), (f + 5, 66, "i")]
        sx_keys += [(f, 108 if i else 100, "o"), (f + 5, 96, "i")]
        sy_keys += [(f, 92 if i else 100, "o"), (f + 5, 105, "i")]
        eye_sy += [(f + 9, 104, "l"), (f + 11, 86, "o"), (f + 14, 104)]
    x_keys += [(84, 115), (96, 115), (110, -6), (115, 0, "h"), (120, 0, "h")]
    y_keys += [(80, 75, "o"), (84, 75), (87, 64, "i"), (91, 75, "o"), (96, 75), (110, -5), (115, 0, "h"),
               (120, 0, "h")]
    sx_keys += [(80, 108, "o"), (84, 100), (87, 96), (91, 106, "o"), (95, 100, "h"), (106, 100), (111, 104),
                (116, 100, "h"), (120, 100, "h")]
    sy_keys += [(80, 92, "o"), (84, 100), (87, 104), (91, 95, "o"), (95, 100, "h"), (106, 100), (111, 96),
                (116, 100, "h"), (120, 100, "h")]
    c.k(BODY, "x", *x_keys)
    c.k(BODY, "y", *y_keys)
    c.k(BODY, "sx", *sx_keys)
    c.k(BODY, "sy", *sy_keys)
    c.k(BODY, "r", (0, 0, "h"), (4, 0), (10, -5), (24, 6), (32, 8, "h"), (84, 8), (96, 2), (104, -4), (112, 1),
        (116, 0, "h"), (120, 0, "h"))
    near(c, (0, 100, "h"), (6, 100), (30, 105, "h"), (96, 105), (114, 100, "h"), (120, 100, "h"))
    c.k(LOOK, "x", (0, 0, "h"), (4, 0), (14, 34, "h"), (80, 34), (86, 0, "h"), (120, 0, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (4, 0), (14, 28, "h"), (80, 28), (86, -10, "h"), (100, -10), (110, 0, "h"),
        (120, 0, "h"))
    for v in LIDS:
        c.k(v, "vy", (0, -30, "h"), (6, -30), (16, lid(0.15), "h"), (80, lid(0.15)), (84, -30, "h"), (120, -30, "h"))
    sx, sy = happy(c, 82, 86, 98, 104)
    c.eyes("sx", (0, 100, "h"), (82, 100, "h"), *sx, (120, 100, "h"))
    c.eyes("sy", *eye_sy, (80, 104), (82, 100, "h"), *sy, (120, 100, "h"))
    c.hand(HL, (0, TUCK_L, "h"), (26, TUCK_L, "o"), (33, (-50, 124, 0.95)), (37, (-48, 118, 0.9), "h"),
           (80, (-48, 118, 0.9), "i"), (88, TUCK_L, "h"), (120, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (28, TUCK_R, "o"), (35, (64, 122, 0.95)), (39, (60, 116, 0.9), "h"),
           (81, (60, 116, 0.9), "i"), (89, TUCK_R, "h"), (120, TUCK_R, "h"))
    return c


def loop_the_loop():
    """A loop-the-loop: a dip and squash to wind up, then a full vertical
    circle (radius 55: right, up over the top, down the left side, back to
    the bottom), fastest at the bottom, the body turning a whole turn with
    the path (its head always towards the loop's centre) and stretched in
    flight, ^ ^ the whole way; it lands with a squash and a wobble. Plays as
    the loop act. Rotation ends at -360 and is cut to 0 (the same pose) on
    the landing frame."""
    c = Clip("loop_the_loop", 66)
    r, f0, f1, step = 55, 8, 50, 2
    xs, ys, rs = [(0, 0, "h"), (2, 0)], [(0, 0, "h"), (2, 0), (6, 8, "h"), (f0, 8, "l")], [(0, 0, "h"), (f0, 0, "l")]
    sxs = [(0, 100, "h"), (2, 100), (6, 110, "h"), (f0, 104, "l")]
    sys_ = [(0, 100, "h"), (2, 100), (6, 90, "h"), (f0, 96, "l")]
    for f in range(f0 + step, f1 + 1, step):
        u = (f - f0) / (f1 - f0)
        th = 2 * math.pi * (u + 0.08 * math.sin(2 * math.pi * u))
        xs.append((f, round(r * math.sin(th), 2), "l"))
        ys.append((f, round(-r + r * math.cos(th) + 8 * (1 - u), 2), "l"))
        rs.append((f, round(-math.degrees(th), 2), "l"))
        # Stretched along the flight (the ball's axes stay the screen's).
        a = 9 * math.sin(math.pi * u) ** 0.5
        sxs.append((f, round(100 + a * math.cos(2 * th), 2), "l"))
        sys_.append((f, round(100 - a * math.cos(2 * th), 2), "l"))
    xs[-1] = (f1, 0, "o")
    ys[-1] = (f1, 0, "o")
    rs[-1] = (f1, -366, "h")
    c.k(BODY, "x", *xs, (53, -3), (57, 0, "h"), (66, 0, "h"))
    c.k(BODY, "y", *ys, (53, 6), (57, -3), (61, 0, "h"), (66, 0, "h"))
    c.k(BODY, "r", *rs, (51, -6), (55, 2), (60, 0, "h"), (66, 0, "h"))
    sxs[-1] = (f1, 100, "o")
    sys_[-1] = (f1, 100, "o")
    c.k(BODY, "sx", *sxs, (53, 114), (57, 96), (61, 101), (64, 100, "h"), (66, 100, "h"))
    c.k(BODY, "sy", *sys_, (53, 86), (57, 105), (61, 99), (64, 100, "h"), (66, 100, "h"))
    sx, sy = happy(c, 2, 6, 54, 60)
    c.eyes("sx", *sx, (66, 100, "h"))
    c.eyes("sy", *sy, (66, 100, "h"))
    return c


def bounce_walls():
    """Bouncing off the walls of the round screen: a wind-up, then five hops
    (right, left, right-up, left-down, home), each an arc, stretched in flight and leaning the way it
    flies, squashed flat against the wall for two frames at each hit, the
    eyes leading, no hands (a clean ball); it lands home with a squash. The
    hits are pushed out to where the ball touches the glass: (175, 45),
    (-180, 10), (120, -120), (-140, 95). ^ ^ all
    the way. Plays as the bounce act."""
    c = Clip("bounce_walls", 96)
    hits = [(6, (-10, 0)), (20, (175, 45)), (37, (-180, 10)), (52, (120, -120)), (66, (-140, 95)), (80, (0, 0))]
    xs = [(0, 0, "h"), (2, 0)]
    ys = [(0, 0, "h"), (2, 0)]
    sxk = [(0, 100, "h"), (2, 100), (6, 88, "h")]
    syk = [(0, 100, "h"), (2, 100), (6, 110, "h")]
    rk = [(0, 0, "h"), (2, 0), (6, -4)]
    lk = [(0, 0, "h"), (2, 0, "o"), (5, 22, "h")]
    for i in range(len(hits) - 1):
        (fa, (xa, ya)), (fb, (xb, yb)) = hits[i], hits[i + 1]
        la = fa + (2 if i else 0)       # leaves the wall after a two-frame squash
        mid = (la + fb) // 2
        side = 1 if xb > xa else -1
        xs += [(fa, xa, "h"), (la, xa, "l")]
        ys += [(fa, ya, "h"), (la, ya, "o"), (mid, min(ya, yb) - 26, "i")]
        if i:
            sxk += [(la, 94)]
            syk += [(la, 106)]
        sxk += [(la + 3, 110, "h"), (fb - 2, 110, "i")]
        syk += [(la + 3, 92, "h"), (fb - 2, 92, "i")]
        rk += [(la + 3, 8 * side, "h"), (fb - 2, 8 * side)]
        lk += [(la, 22 * side, "o"), (la + 3, 22 * side, "h")]
        if i < len(hits) - 2:
            sxk += [(fb, 84, "h")]
            syk += [(fb, 114, "h")]
            rk += [(fb, 0, "h")]
    xs += [(80, 0, "h"), (96, 0, "h")]
    ys += [(80, 0, "o"), (83, 5), (87, -3), (91, 0, "h"), (96, 0, "h")]
    sxk += [(80, 114, "o"), (84, 96), (88, 102), (92, 100, "h"), (96, 100, "h")]
    syk += [(80, 86, "o"), (84, 105), (88, 98), (92, 100, "h"), (96, 100, "h")]
    rk += [(80, 0), (84, -3), (89, 1), (93, 0, "h"), (96, 0, "h")]
    lk += [(78, 0, "h"), (96, 0, "h")]
    c.k(BODY, "x", *xs)
    c.k(BODY, "y", *ys)
    c.k(BODY, "sx", *sxk)
    c.k(BODY, "sy", *syk)
    c.k(BODY, "r", *rk)
    c.k(LOOK, "x", *lk)
    c.k(LOOK, "y", (0, 0, "h"), (2, 0), (6, -8, "h"), (78, -8), (84, 0, "h"), (96, 0, "h"))
    sx, sy = happy(c, 3, 7, 86, 92)
    c.eyes("sx", *sx, (96, 100, "h"))
    c.eyes("sy", *sy, (96, 100, "h"))
    return c


def stargaze():
    """Up high, as near the sky as the glass lets it: a little dip, then it
    drifts up to the top of the screen and a bit farther away, hands clasped
    under its chin, eyes up with soft half lids; starlight glows round it
    (pale blue) and sparks twinkle; it floats, a slow blink; a sigh (breathe
    in, breathe out, lids lower) and it drifts home with a soft settle.
    Plays as the stars act. Colour: pale starlight blue."""
    c = Clip("stargaze", 180)
    c.k(BODY, "y", (0, 0, "h"), (2, 0), (6, 6), (48, -98), (56, -95), (80, -99), (104, -95), (128, -99),
        (134, -103), (142, -92), (146, -93), (170, 4), (175, 0, "h"), (180, 0, "h"))
    c.k(BODY, "x", (0, 0, "h"), (8, 0), (50, 10), (100, 6), (140, 10), (168, -3), (174, 0, "h"), (180, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (2, 100), (6, 105), (16, 96), (34, 98), (48, 100, "h"), (128, 100), (134, 97),
        (142, 104), (150, 100, "h"), (166, 100), (171, 104), (176, 100, "h"), (180, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (2, 100), (6, 95), (16, 104), (34, 102), (48, 100, "h"), (128, 100), (134, 104),
        (142, 96), (150, 100, "h"), (166, 100), (171, 96), (176, 100, "h"), (180, 100, "h"))
    c.k(BODY, "r", (0, 0, "h"), (10, 0), (40, -3), (90, 3), (130, -2), (160, 0, "h"), (180, 0, "h"))
    near(c, (0, 100, "h"), (8, 100), (50, 90, "h"), (142, 90), (172, 100, "h"), (180, 100, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (4, 0), (16, -30, "h"), (140, -30), (146, -18), (166, 0, "h"), (180, 0, "h"))
    c.k(LOOK, "x", (0, 0, "h"), (4, 0), (16, 6), (70, 10), (110, 0), (140, 6), (166, 0, "h"), (180, 0, "h"))
    for v in LIDS:
        c.k(v, "vy", (0, -30, "h"), (10, -30), (36, lid(0.35), "h"), (82, lid(0.35)), (90, lid(1), "h"),
            (95, lid(1)), (105, lid(0.35), "h"), (134, lid(0.35)), (142, lid(0.55), "h"), (148, lid(0.55)),
            (166, -30, "h"), (180, -30, "h"))
    c.eyes("sy", (0, 100, "h"), (10, 100), (30, 106, "h"), (150, 106), (166, 100, "h"), (180, 100, "h"))
    c.hand(HL, (0, TUCK_L, "h"), (14, TUCK_L, "o"), (22, (-46, 108, 1.05)), (27, (-40, 102, 0.95), "h"),
           (140, (-40, 102, 0.95), "i"), (150, TUCK_L, "h"), (180, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (16, TUCK_R, "o"), (24, (46, 108, 1.05)), (29, (40, 102, 0.95), "h"),
           (141, (40, 102, 0.95), "i"), (151, TUCK_R, "h"), (180, TUCK_R, "h"))
    c.glow((0, 0, "h"), (30, 0), (70, 0.4), (110, 0.5), (140, 0.4), (166, 0, "h"), (180, 0, "h"))
    c.spark((0, 0, "h"), (40, 0), (60, 0.5), (76, 0.25), (96, 0.6), (112, 0.3), (128, 0.55), (148, 0, "h"),
            (180, 0, "h"))
    return c


def hum_sway():
    """Humming to itself: eyes up and dreamy under soft lids, it sways side
    to side four times (a beat a second, a little up off its spot, a touch
    higher at each end of the swing), leaning into each sway three frames
    late; the hands, out by its sides, bob down on every half beat like
    keeping time (the right two frames after the left); a slow blink; then
    it settles home. Plays as the hum act."""
    c = Clip("hum_sway", 150)
    c.k(BODY, "x", (0, 0, "h"), (3, 0), (18, -26), (48, 26), (78, -26), (108, 26), (130, -4), (136, 0, "h"),
        (150, 0, "h"))
    c.k(BODY, "y", (0, 0, "h"), (3, 0), (18, -14), (33, -8), (48, -14), (63, -8), (78, -14), (93, -8), (108, -14),
        (130, 2), (136, 0, "h"), (150, 0, "h"))
    c.k(BODY, "r", (0, 0, "h"), (5, 0), (21, -6), (51, 6), (81, -6), (111, 6), (132, -1), (140, 0, "h"),
        (150, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (2, 100), (5, 103), (12, 99), (18, 100, "h"), (126, 100), (131, 103),
        (137, 100, "h"), (150, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (2, 100), (5, 97), (12, 101), (18, 100, "h"), (126, 100), (131, 97),
        (137, 100, "h"), (150, 100, "h"))
    c.k(LOOK, "x", (0, 0, "h"), (3, 0), (14, 8), (48, 12), (78, 4), (108, 12), (126, 0, "h"), (150, 0, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (3, 0), (14, -22, "h"), (120, -22), (132, 0, "h"), (150, 0, "h"))
    for v in LIDS:
        c.k(v, "vy", (0, -30, "h"), (4, -30), (16, lid(0.38), "h"), (120, lid(0.38)), (134, -30, "h"),
            (150, -30, "h"))
    c.eyes("sy", (0, 100, "h"), (67, 100), (71, 8, "h"), (73, 8), (78, 100, "h"), (150, 100, "h"))
    c.eyes("sx", (0, 100, "h"), (4, 100), (16, 104, "h"), (120, 104), (134, 100, "h"), (150, 100, "h"))
    for h, base, lag in ((HL, -150, 0), (HR, 150, 2)):
        keys = [(0, TUCK_L if h == HL else TUCK_R, "h"), (6 + lag, TUCK_L if h == HL else TUCK_R, "o"),
                (13 + lag, (base, 50, 1.0))]
        for f in range(18, 109, 15):
            keys += [(f + lag, (base, 66, 0.95), "o"), (f + 7 + lag, (base, 50, 0.95))]
        keys += [(118 + lag, (base, 56, 0.95), "i"), (128 + lag, TUCK_L if h == HL else TUCK_R, "h"),
                 (150, TUCK_L if h == HL else TUCK_R, "h")]
        c.hand(h, *keys)
    return c


def search_you():
    """Looking for you, a little sad: worried lids sloping down outside;
    it looks far left (the body turning after the eyes), far right (a blink
    on the way), up-left; then a small call: a dip and a rise, eyes wide, a
    hand cupped by the side of its face; no answer: it droops, eyes down,
    and comes back to rest. Plays as the search act (looking for the user)."""
    c = Clip("search_you", 108)
    c.k(LOOK, "x", (0, 0, "h"), (3, 0, "o"), (11, -46), (14, -42, "h"), (30, -42, "o"), (39, 46), (42, 42, "h"),
        (54, 42, "o"), (61, -30, "h"), (70, -30), (76, 0, "h"), (108, 0, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (54, 0, "o"), (61, -24, "h"), (70, -24), (76, -10, "h"), (86, -10), (94, 12, "h"),
        (98, 12), (104, 0, "h"), (108, 0, "h"))
    c.k(LOOK, "sx", (0, 100, "h"), (3, 100, "o"), (11, 84, "h"), (30, 84), (35, 100), (40, 84, "h"), (54, 84),
        (61, 92, "h"), (70, 92), (76, 100, "h"), (108, 100, "h"))
    c.k(BODY, "x", (0, 0, "h"), (5, 0), (14, -12, "h"), (32, -12), (42, 12, "h"), (56, 12), (64, -6, "h"), (70, -6),
        (78, 0, "h"), (108, 0, "h"))
    c.k(BODY, "r", (0, 0, "h"), (5, 0), (14, -4, "h"), (32, -4), (42, 4, "h"), (56, 4), (64, -2, "h"), (72, -2),
        (78, 0, "h"), (86, 0), (92, -3, "h"), (96, -3), (102, 0, "h"), (108, 0, "h"))
    c.k(BODY, "y", (0, 0, "h"), (56, 0), (64, -4, "h"), (70, -4), (74, 2), (78, -18, "o"), (84, -16, "h"), (86, -16),
        (94, 10, "h"), (96, 10), (101, -2), (104, 0, "h"), (108, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (70, 100), (74, 105), (78, 93), (84, 96, "h"), (86, 96), (94, 105, "h"),
        (96, 105), (101, 99), (104, 100, "h"), (108, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (70, 100), (74, 95), (78, 109), (84, 105, "h"), (86, 105), (94, 95, "h"),
        (96, 95), (101, 101), (104, 100, "h"), (108, 100, "h"))
    c.eyes("sy", (0, 100, "h"), (3, 100), (10, 96, "h"), (34, 96, "l"), (35, 8, "l"), (37, 96, "h"), (74, 96),
           (78, 110, "h"), (86, 110), (92, 90, "h"), (96, 90), (104, 100, "h"), (108, 100, "h"))
    # Worried: the lids slope down outside, lower still when it droops.
    for v in (LTL, RTR):
        c.k(v, "vy", (0, -30, "h"), (3, -30), (10, -14, "h"), (74, -14), (78, -20, "h"), (86, -20), (92, -6, "h"),
            (96, -6), (104, -30, "h"), (108, -30, "h"))
    for v in (LTR, RTL):
        c.k(v, "vy", (0, -30, "h"), (86, -30), (92, lid(0.15), "h"), (96, lid(0.15)), (104, -30, "h"),
            (108, -30, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (70, TUCK_R, "o"), (76, (130, -4, 1.25)), (79, (122, 2, 1.15), "h"),
           (80, (122, 2, 1.15)), (82, (128, -2, 1.18)), (85, (122, 2, 1.15), "h"), (87, (122, 2, 1.15), "i"),
           (96, TUCK_R, "h"), (108, TUCK_R, "h"))
    return c


def gaze_blink():
    """Looking at you: it drifts a little nearer, eyes on you and soft;
    then the cat's slow blink (I like you): the lids close slowly as it
    relaxes and tilts its head, hold, open slowly to half; a small happy
    hop into ^ ^, and it eases back to its size. Ends in ^ ^ (with the rest
    of the pose at rest), handing over to a happy face. Plays as the gaze
    act."""
    c = Clip("gaze_blink", 105)
    near(c, (0, 100, "h"), (3, 100), (28, 110, "h"), (84, 110), (100, 100, "h"), (105, 100, "h"))
    c.k(BODY, "y", (0, 0, "h"), (3, 0), (10, -4), (28, 0, "h"), (34, 0), (50, 4, "h"), (60, 4), (76, 0, "h"),
        (78, 0), (82, -5), (86, 1), (90, 0, "h"), (105, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (34, 100), (50, 103, "h"), (60, 103), (76, 100, "h"), (78, 100), (82, 96),
        (86, 103), (90, 100, "h"), (105, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (34, 100), (50, 97, "h"), (60, 97), (76, 100, "h"), (78, 100), (82, 104),
        (86, 97), (90, 100, "h"), (105, 100, "h"))
    c.k(BODY, "r", (0, 0, "h"), (30, 0), (50, 4, "h"), (62, 4), (78, 0), (84, -1), (90, 0, "h"), (105, 0, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (3, 0), (14, -3, "h"), (88, -3), (98, 0, "h"), (105, 0, "h"))
    for v in LIDS:
        c.k(v, "vy", (0, -30, "h"), (32, -30), (46, lid(1), "h"), (54, lid(1)), (72, lid(0.25), "h"), (74, lid(0.25)),
            (80, -30, "h"), (105, -30, "h"))
    c.eyes("sx", (0, 100, "h"), (3, 100), (20, 104, "h"), (74, 104), (76, 100, "o"), (80, 115, "h"), (105, 115, "h"))
    c.eyes("sy", (0, 100, "h"), (3, 100), (20, 106, "h"), (74, 106), (76, 100, "o"), (80, 60, "h"), (105, 60, "h"))
    for bm in (LBM, RBM):
        c.k(bm, "vy", (0, 30, "h"), (76, 30, "o"), (80, -12, "h"), (105, -12, "h"))
        c.k(bm, "rad", (0, 0, "h"), (76, 0, "o"), (80, 8, "h"), (105, 8, "h"))
    return c


NEW = {f.__name__: f for f in (look_around, peek_side, sniff, loop_the_loop, bounce_walls, stargaze, hum_sway,
                               search_you, gaze_blink)}
