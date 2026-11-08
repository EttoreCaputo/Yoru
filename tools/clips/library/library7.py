"""Group D: reactions to events (touch, the case moving, sleep, the portal).
They replace the procedural reactions in main/behaviour.c and the hand
gestures in main/eyes.c. Frames at 30 fps; Rive units (screen px / 0.7)."""
from author import (BODY, HL, HR, LOOK, LTL, LTR, RTL, RTR, TUCK_L, TUCK_R, Clip, lid)
from library import happy, near

LIDS = (LTL, LTR, RTL, RTR)
ASLEEP_LID = lid(0.95)
ASLEEP_NEAR = 85


def still(c, end):
    """Hold everything to `end`, the clip's last frame: a still tail for the
    hand-over. Tucked hands get a key there too, so they stay put as the body
    settles (a hand's keys are made relative to the body at that key)."""
    c.k(BODY, "r", (end, 0, "h"))
    for h, tuck in ((HL, TUCK_L), (HR, TUCK_R)):
        if c.hands[h] and max(c.hands[h], key=lambda k: k[0])[1] == tuck:
            c.hand(h, (end, tuck, "h"))
    if c.props:
        c.prop((end, (0, 110, 0), "h"))


def wave():
    """Hello or goodbye. A dip (anticipation), the right hand shoots up past
    its mark and settles, then waves three times on a little arc (out low,
    up through the top, in low: a wrist swinging), the body leaning into it
    and bobbing on each swing, ^ ^ eyes. The hand drops back and tucks.
    Plays on hello/goodbye; authored on the right, mirrorable."""
    c = Clip("wave", 54)
    c.k(BODY, "y", (0, 0, "h"), (1, 0), (5, 5, "i"), (10, -10, "o"), (16, 0), (22, -4), (30, 0), (38, -4),
        (44, 2), (48, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (1, 100), (5, 105, "i"), (10, 95, "o"), (16, 100, "h"), (40, 100), (44, 102),
        (48, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (1, 100), (5, 95, "i"), (10, 105, "o"), (16, 100, "h"), (40, 100), (44, 98),
        (48, 100, "h"))
    c.k(BODY, "x", (0, 0, "h"), (4, 0), (14, 10, "h"), (38, 10), (48, 0, "h"))
    c.k(BODY, "r", (0, 0, "h"), (4, -2), (12, 8), (16, 6), (22, 8), (30, 5), (38, 7), (44, -1), (48, 0, "h"))
    c.k(LOOK, "x", (0, 0, "h"), (4, 0), (12, 8, "h"), (38, 8), (46, 0, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (4, 0), (12, -6, "h"), (38, -6), (46, 0, "h"))
    sx, sy = happy(c, 6, 10, 40, 46)
    c.eyes("sx", (0, 100, "h"), *sx)
    c.eyes("sy", (0, 100, "h"), *sy)
    out, top, inn = (182, -70, 1.05), (158, -88, 1.05), (134, -72, 1.05)
    c.hand(HR, (0, TUCK_R, "h"), (3, TUCK_R, "o"), (9, (166, -104, 1.15)), (14, out, "i"),
           (16, top, "o"), (18, inn, "i"), (20, top, "o"), (22, out, "i"),
           (24, top, "o"), (26, inn, "i"), (28, top, "o"), (30, out, "i"),
           (32, top, "o"), (34, inn, "i"), (36, top, "o"), (38, out, "i"), (46, TUCK_R, "h"))
    still(c, 53)
    return c


def fall_asleep():
    """Nods off for good. The lids slowly fall and the body sags, a little
    jerk awake (lids snap up, a stretch), then the lids drop again, the body
    sinks and drifts farther (nearness 100 to 85). Hands over to the asleep
    face: lids at 0.95, nearness 85, body offset 0 (the board eases the
    rest). Plays when Yoru falls asleep; symmetric."""
    c = Clip("fall_asleep", 78)
    c.k(BODY, "y", (0, 0, "h"), (2, 0), (26, 12, "i"), (28, 13, "o"), (31, -6, "o"), (36, -2), (60, 16),
        (72, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (2, 100), (26, 104, "h"), (28, 104, "o"), (31, 94, "o"), (38, 100), (60, 105),
        (72, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (2, 100), (26, 96, "h"), (28, 96, "o"), (31, 107, "o"), (38, 100), (60, 95),
        (72, 100, "h"))
    c.k(BODY, "r", (0, 0, "h"), (2, 0), (26, -4, "h"), (28, -4, "o"), (32, 2), (38, 0), (60, -4), (72, 0, "h"))
    near(c, (0, 100, "h"), (2, 100), (26, 96, "h"), (28, 96, "o"), (32, 99), (40, 98), (72, ASLEEP_NEAR, "h"))
    for v in LIDS:
        c.k(v, "vy", (0, -30, "h"), (2, -30), (26, lid(0.72), "h"), (28, lid(0.72), "o"), (31, lid(0.1), "h"),
            (36, lid(0.1)), (62, ASLEEP_LID, "h"))
    c.eyes("sy", (0, 100, "h"), (2, 100), (26, 92, "h"), (28, 92, "o"), (31, 116), (38, 104), (62, 96, "h"),
           (72, 100, "h"))
    c.eyes("sx", (0, 100, "h"), (28, 100, "o"), (31, 108), (38, 102), (62, 100, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (2, 0), (26, 12, "h"), (28, 12, "o"), (31, -6), (38, -2), (60, 10), (72, 0, "h"))
    still(c, 77)
    return c


def wake_up():
    """From asleep. Frame 0 is the ASLEEP pose, not rest (it plays as the
    board leaves sleep): lids at 0.95, nearness 85. A stir (a wriggle, the
    lids flutter), a squash, then a jolt up: eyes wide open, nearer with an
    overshoot. Two quick blinks, then a small hello wave with the right hand
    and ^ ^ eyes. Ends at rest. Mirrorable (the waving hand)."""
    c = Clip("wake_up", 60)
    c.k(BODY, "y", (0, 0, "h"), (3, 0), (8, 4), (12, 2), (16, 7, "i"), (21, -18, "o"), (25, 4), (29, 0, "h"),
        (38, 0), (42, -3), (46, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (3, 100), (8, 103), (12, 98), (16, 107, "i"), (21, 91, "o"), (25, 104),
        (29, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (3, 100), (8, 97), (12, 102), (16, 93, "i"), (21, 111, "o"), (25, 96),
        (29, 100, "h"))
    c.k(BODY, "r", (0, 0, "h"), (3, 0), (7, -4), (11, 3), (15, 0, "h"), (32, 0), (38, 5, "h"), (48, 5),
        (54, 0, "h"))
    near(c, (0, ASLEEP_NEAR, "h"), (3, ASLEEP_NEAR), (12, 88), (16, 88, "i"), (22, 104, "o"), (28, 100, "h"))
    for v in LIDS:
        c.k(v, "vy", (0, ASLEEP_LID, "h"), (4, ASLEEP_LID), (8, lid(0.75)), (11, ASLEEP_LID), (17, ASLEEP_LID, "o"),
            (20, -30, "h"))
    sx, sy = happy(c, 38, 42, 50, 55)
    c.eyes("sy", (0, 100, "h"), (17, 100, "o"), (20, 132), (26, 110, "h"), (28, 110, "l"), (29, 8, "l"),
           (31, 110, "h"), (32, 110, "l"), (33, 8, "l"), (35, 104), (37, 100, "h"), *sy)
    c.eyes("sx", (0, 100, "h"), (17, 100, "o"), (20, 114), (26, 104, "h"), (35, 104), (37, 100, "h"), *sx)
    c.k(LOOK, "y", (0, 0, "h"), (16, 0), (21, -8, "o"), (30, 0, "h"))
    out, top, inn = (166, -64, 1.0), (150, -76, 1.0), (134, -64, 1.0)
    c.hand(HR, (0, TUCK_R, "h"), (32, TUCK_R, "o"), (38, (156, -86, 1.1)), (40, out, "i"), (42, top, "o"),
           (44, inn, "i"), (46, top, "o"), (48, out, "i"), (55, TUCK_R, "h"))
    still(c, 59)
    return c


def tapped():
    """A finger taps the screen. Squash at once (pressed in the centre), the
    eyes blink shut, then pop open bigger with a little surprised hop and a
    flick of both hands, a small landing squash, still. Short and snappy;
    ends at rest. Symmetric."""
    c = Clip("tapped", 24)
    c.k(BODY, "y", (0, 0, "h"), (1, 0, "o"), (4, 5, "o"), (9, -12, "i"), (13, 2, "o"), (17, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (1, 100, "o"), (4, 113, "o"), (9, 94, "i"), (13, 104, "o"), (17, 99), (19, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (1, 100, "o"), (4, 87, "o"), (9, 107, "i"), (13, 96, "o"), (17, 101), (19, 100, "h"))
    near(c, (0, 100, "h"), (1, 100, "o"), (4, 96), (9, 103), (16, 100, "h"))
    c.eyes("sy", (0, 100, "h"), (1, 100, "l"), (3, 8, "h"), (5, 8, "o"), (8, 132), (14, 108), (19, 100, "h"))
    c.eyes("sx", (0, 100, "h"), (1, 100, "l"), (3, 118, "h"), (5, 118, "o"), (8, 114), (14, 104), (19, 100, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (4, 0, "o"), (8, -6), (16, 0, "h"))
    c.hand(HL, (0, TUCK_L, "h"), (3, TUCK_L, "o"), (8, (-160, -30, 0.95)), (16, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (4, TUCK_R, "o"), (9, (160, -30, 0.95)), (17, TUCK_R, "h"))
    still(c, 23)
    return c


def bonk():
    """Slapped from the left, so knocked to the right (+40 x) with a tilt:
    squashed on the hit side, eyes squeezed shut in a wince (lids sloping to
    the nose, > <), the hands trailing. It springs back past its place and
    wobbles to a stop, blinking. Ends at rest; mirrored for a slap from the
    right."""
    c = Clip("bonk", 33)
    c.k(BODY, "x", (0, 0, "h"), (1, 0, "o"), (5, 40, "h"), (7, 40), (15, -9), (20, 4), (25, 0, "h"))
    c.k(BODY, "y", (0, 0, "h"), (1, 0, "o"), (5, -6), (12, 2), (18, 0, "h"))
    c.k(BODY, "r", (0, 0, "h"), (1, 0, "o"), (5, 13), (8, 11), (15, -6), (20, 3), (25, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (1, 100, "o"), (3, 85), (8, 107), (13, 97), (18, 101), (22, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (1, 100, "o"), (3, 112), (8, 95), (13, 103), (18, 99), (22, 100, "h"))
    c.k(LOOK, "x", (0, 0, "h"), (1, 0, "o"), (4, -14), (12, -4), (18, 4), (24, 0, "h"))
    c.eyes("sy", (0, 100, "h"), (1, 100, "o"), (3, 26, "h"), (12, 26, "o"), (16, 112), (19, 112, "l"), (20, 8, "l"),
           (22, 106), (27, 100, "h"))
    c.eyes("sx", (0, 100, "h"), (1, 100, "o"), (3, 124, "h"), (12, 124, "o"), (16, 106), (24, 100, "h"))
    for v, cut in ((LTL, 0.1), (LTR, 0.55), (RTL, 0.55), (RTR, 0.1)):
        c.k(v, "vy", (0, -30, "h"), (1, -30, "o"), (3, lid(cut), "h"), (12, lid(cut), "o"), (16, -30, "h"))
    c.hand(HL, (0, TUCK_L, "h"), (2, TUCK_L, "o"), (7, (-172, -40, 1.0)), (12, (-164, 10, 0.95), "i"),
           (21, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (3, TUCK_R, "o"), (8, (150, -90, 1.0)), (13, (162, -40, 0.95), "i"),
           (22, TUCK_R, "h"))
    still(c, 32)
    return c


def set_down():
    """Put down on the desk. Hanging a moment (a slight stretch, a lift), a
    soft drop, squash on landing with a blink and the hands out low for
    balance, a small rebound, settles. Ends at rest. Symmetric."""
    c = Clip("set_down", 27)
    c.k(BODY, "y", (0, 0, "h"), (2, 0), (6, -6, "i"), (10, 8, "o"), (15, -3), (19, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (2, 100), (6, 97, "i"), (10, 110, "o"), (15, 97), (19, 101), (21, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (2, 100), (6, 104, "i"), (10, 90, "o"), (15, 103), (19, 99), (21, 100, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (2, 0), (8, 8, "h"), (12, 8), (18, 0, "h"))
    c.eyes("sy", (0, 100, "h"), (9, 100, "l"), (11, 8, "h"), (12, 8, "o"), (15, 104), (20, 100, "h"))
    c.hand(HL, (0, TUCK_L, "h"), (5, TUCK_L, "o"), (10, (-158, 76, 0.95)), (13, (-162, 68, 0.95), "i"),
           (19, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (6, TUCK_R, "o"), (11, (158, 76, 0.95)), (14, (162, 68, 0.95), "i"),
           (20, TUCK_R, "h"))
    still(c, 26)
    return c


def landed():
    """Landing after a fall. Hits hard at once (a big squash, pushed down),
    eyes squeezed shut and the hands splayed out; bounces back up, then the
    eyes open dizzy-wide and mismatched, rolling, while the body wobbles to a
    stop. Ends at rest. Symmetric."""
    c = Clip("landed", 36)
    c.k(BODY, "y", (0, 0, "h"), (1, 0, "o"), (3, 30, "h"), (5, 30, "o"), (10, -16, "i"), (14, 5, "o"), (18, -2),
        (22, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (1, 100, "o"), (3, 128, "h"), (5, 128, "o"), (10, 91, "i"), (14, 106, "o"),
        (18, 98), (22, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (1, 100, "o"), (3, 74, "h"), (5, 74, "o"), (10, 111, "i"), (14, 94, "o"),
        (18, 102), (22, 100, "h"))
    c.k(BODY, "r", (0, 0, "h"), (10, 0), (14, 8), (19, -7), (24, 4), (28, -2), (31, 0, "h"))
    c.eyes("sy", (0, 100, "h"), (1, 100, "o"), (3, 12, "h"), (9, 12, "o"), (13, 134), (18, 76), (23, 124), (28, 96),
           (31, 100, "h"), which="l")
    c.eyes("sy", (0, 100, "h"), (1, 100, "o"), (3, 12, "h"), (9, 12, "o"), (13, 82), (18, 130), (23, 86), (28, 106),
           (31, 100, "h"), which="r")
    c.eyes("sx", (0, 100, "h"), (1, 100, "o"), (3, 126, "h"), (9, 126, "o"), (13, 110), (28, 100, "h"))
    c.k(LOOK, "x", (0, 0, "h"), (10, 0), (14, 22), (18, 0), (22, -20), (26, 0), (29, 8), (32, 0, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (1, 0, "o"), (4, 10), (10, 0), (14, 0), (18, 18), (22, 0), (26, -12), (30, 0, "h"))
    c.hand(HL, (0, TUCK_L, "h"), (1, TUCK_L, "o"), (5, (-180, 70, 1.05)), (12, (-170, 30, 1.0), "i"),
           (22, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (1, TUCK_R, "o"), (5, (180, 70, 1.05)), (13, (170, 34, 1.0), "i"),
           (23, TUCK_R, "h"))
    still(c, 35)
    return c


def jolt():
    """The case was knocked. A tiny squash from below, a jump up stretched
    with wide eyes and the hands flung up, down into a squash, a little
    rebound, settles. Ends at rest. Symmetric."""
    c = Clip("jolt", 24)
    c.k(BODY, "y", (0, 0, "h"), (1, 0, "o"), (2, 3, "o"), (7, -30, "i"), (12, 5, "o"), (15, -3), (18, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (1, 100, "o"), (2, 106, "o"), (6, 90, "i"), (12, 108, "o"), (15, 98), (18, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (1, 100, "o"), (2, 94, "o"), (6, 112, "i"), (12, 92, "o"), (15, 102), (18, 100, "h"))
    c.eyes("sy", (0, 100, "h"), (1, 100, "o"), (4, 132, "h"), (13, 132), (19, 100, "h"))
    c.eyes("sx", (0, 100, "h"), (1, 100, "o"), (4, 114, "h"), (13, 114), (19, 100, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (1, 0, "o"), (5, -8, "h"), (12, -8), (18, 0, "h"))
    c.hand(HL, (0, TUCK_L, "h"), (2, TUCK_L, "o"), (7, (-150, -80, 1.0)), (11, (-160, -40, 0.95), "i"),
           (17, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (2, TUCK_R, "o"), (7, (150, -80, 1.0)), (11, (160, -40, 0.95), "i"),
           (18, TUCK_R, "h"))
    still(c, 23)
    return c


def falling():
    """Being dropped. A gasp, the body stretched tall and trembling, eyes
    wide and scared (lids sloping down outside) looking down, both hands
    flung up flapping out of step. Before the landing it pulls back to rest
    (the impact is landed). Symmetric."""
    c = Clip("falling", 45)
    shake = [(f, 3 if (f // 2) % 2 else -3, "l") for f in range(8, 34, 2)]
    c.k(BODY, "x", (0, 0, "h"), (6, 0, "l"), *shake, (34, 0, "h"))
    c.k(BODY, "y", (0, 0, "h"), (1, 0, "o"), (6, -10, "h"), (32, -10), (38, 2), (41, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (1, 100, "o"), (6, 88, "h"), (32, 88), (37, 103), (40, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (1, 100, "o"), (6, 118, "h"), (32, 118), (37, 97), (40, 100, "h"))
    c.eyes("sy", (0, 100, "h"), (1, 100, "o"), (4, 136, "h"), (32, 136), (38, 100, "h"))
    c.eyes("sx", (0, 100, "h"), (1, 100, "o"), (4, 114, "h"), (32, 114), (38, 100, "h"))
    for v in (LTL, RTR):
        c.k(v, "vy", (0, -30, "h"), (1, -30, "o"), (5, -16, "h"), (32, -16), (38, -30, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (1, 0, "o"), (6, 16, "h"), (32, 16), (38, 0, "h"))
    up_l, hi_l = (-150, -118, 1.05), (-132, -150, 1.05)
    flap_l = [(f, hi_l if (f // 3) % 2 else up_l) for f in range(9, 31, 3)]
    flap_r = [(f + 1, (-p[0], p[1], p[2])) for f, p in flap_l]
    c.hand(HL, (0, TUCK_L, "h"), (1, TUCK_L, "o"), (6, (-160, -130, 1.12)), *flap_l, (32, up_l, "i"),
           (39, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (2, TUCK_R, "o"), (7, (160, -130, 1.12)), *flap_r, (33, (150, -118, 1.05), "i"),
           (40, TUCK_R, "h"))
    still(c, 44)
    return c


def portal_dive():
    """About to dive into a portal (the board moves the body during the jump,
    so the body offsets stay near 0). The hands rise and meet over the head
    like a diver's, the body crouches (squash), then stretches up on the last
    frames with the eyes squeezed shut. Ends stretched, hands up: the board
    eases out of it into the jump. Symmetric."""
    c = Clip("portal_dive", 21)
    c.k(BODY, "y", (0, 0, "h"), (2, 0), (12, 6, "h"), (15, 6, "i"), (21, -6))
    c.k(BODY, "sx", (0, 100, "h"), (2, 100), (6, 98), (12, 114, "h"), (15, 114, "i"), (21, 84))
    c.k(BODY, "sy", (0, 100, "h"), (2, 100), (6, 103), (12, 86, "h"), (15, 86, "i"), (21, 124))
    c.k(LOOK, "y", (0, 0, "h"), (2, 0), (8, -14, "h"), (21, -14))
    for v in LIDS:
        c.k(v, "vy", (0, -30, "h"), (2, -30), (8, lid(0.25), "h"), (21, lid(0.25)))
    c.eyes("sy", (0, 100, "h"), (2, 100), (8, 96, "h"), (16, 96, "i"), (19, 24, "h"), (21, 24))
    c.hand(HL, (0, TUCK_L, "h"), (2, TUCK_L, "o"), (6, (-118, -70, 0.9)), (9, (-16, -150, 1.05)),
           (12, (-14, -132, 1.0), "h"), (15, (-14, -132, 1.0), "i"), (21, (-12, -178, 1.0)))
    c.hand(HR, (0, TUCK_R, "h"), (2, TUCK_R, "o"), (6, (118, -70, 0.9)), (9, (16, -150, 1.05)),
           (12, (14, -132, 1.0), "h"), (15, (14, -132, 1.0), "i"), (21, (12, -178, 1.0)))
    still(c, 21)
    return c


def portal_carry():
    """Back from the portal with a parcel. The hands grow in low in front with
    the card between them, low under the face (the prop appears with them),
    a proud lift with ^ ^ eyes and a bob, then the hands push the card into
    the body: it shrinks into the belly with a glow flash and a pinch of sparkles, the body
    gulps, the hands tuck. Ends at rest. Prop, glow and sparkles in the
    service's colour (the board draws its glyph on the card). Symmetric."""
    c = Clip("portal_carry", 66)
    c.k(BODY, "y", (0, 0, "h"), (2, 0), (8, 4, "i"), (14, -12, "o"), (20, -6), (26, -10), (32, -6, "h"), (38, -6),
        (46, 4, "o"), (52, -2), (56, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (2, 100), (8, 104, "i"), (14, 96, "o"), (20, 100, "h"), (42, 100), (46, 108, "o"),
        (51, 97), (56, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (2, 100), (8, 96, "i"), (14, 104, "o"), (20, 100, "h"), (42, 100), (46, 93, "o"),
        (51, 103), (56, 100, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (2, 0), (8, 14, "h"), (12, 14), (18, -6, "h"), (34, -6), (40, 12, "h"), (46, 12),
        (54, 0, "h"))
    sx, sy = happy(c, 12, 16, 34, 40)
    c.eyes("sx", (0, 100, "h"), *sx)
    c.eyes("sy", (0, 100, "h"), *sy)
    c.prop((0, (0, 110, 0), "h"), (1, (0, 100, 0), "o"), (7, (0, 100, 1.08)), (10, (0, 102, 1.0)),
           (14, (0, 88, 1.12), "o"), (20, (0, 92, 1.1)), (26, (0, 88, 1.12)), (32, (0, 92, 1.1), "h"),
           (36, (0, 92, 1.1), "i"), (45, (0, 52, 0.1), "h"), (46, (0, 52, 0), "h"))
    c.hand(HL, (0, TUCK_L, "h"), (1, TUCK_L, "o"), (7, (-56, 106, 1.05)), (10, (-54, 108, 1.0)),
           (14, (-56, 94, 1.0), "o"), (20, (-54, 98, 1.0)), (26, (-56, 94, 1.0)), (32, (-54, 98, 1.0), "h"),
           (36, (-54, 98, 1.0), "i"), (44, (-34, 62, 1.0)), (54, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (1, TUCK_R, "o"), (7, (56, 106, 1.05)), (10, (54, 108, 1.0)),
           (14, (56, 94, 1.0), "o"), (20, (54, 98, 1.0)), (26, (56, 94, 1.0)), (32, (54, 98, 1.0), "h"),
           (36, (54, 98, 1.0), "i"), (44, (34, 62, 1.0)), (55, TUCK_R, "h"))
    c.glow((0, 0, "h"), (1, 0, "o"), (8, 0.2), (36, 0.2, "i"), (45, 1, "o"), (58, 0, "h"))
    c.spark((0, 0, "h"), (1, 0, "o"), (8, 0.1, "h"), (40, 0.1, "i"), (45, 0.8, "o"), (56, 0, "h"))
    still(c, 65)
    return c


NEW = {f.__name__: f for f in (wave, fall_asleep, wake_up, tapped, bonk, set_down, landed, jolt, falling,
                               portal_dive, portal_carry)}
