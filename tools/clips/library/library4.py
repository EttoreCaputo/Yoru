"""The fourth batch: joy and company (happy, excited, love, a high five and a
chat with another Yoru on its right). 30 fps; Rive units."""
from author import (BODY, HL, HR, LBM, LOOK, LTL, LTR, RBM, RTL, RTR, TUCK_L, TUCK_R, Clip, lid)
from library import happy, near

LIDS = (LTL, LTR, RTL, RTR)


def tucked_at_end(c):
    """Pin the tucked hands at the last frame: hand keys follow the body's
    offset at their frame, so a tuck keyed while the body still moves would
    end a few units off (hidden, but not exactly rest)."""
    for h, tuck in ((HL, TUCK_L), (HR, TUCK_R)):
        if c.hands[h]:
            c.hand(h, (c.frames, tuck, "h"))
    return c


def happy_on(c, on, full):
    """^ ^ from `on`, full by `full`, kept to the end (hands over to a happy face)."""
    for bm in (LBM, RBM):
        c.k(bm, "vy", (0, 30, "h"), (on, 30, "o"), (full, -12, "h"))
        c.k(bm, "rad", (0, 0, "h"), (on, 0, "o"), (full, 8, "h"))
    return [(0, 100, "h"), (on, 100, "o"), (full, 115, "h")], [(0, 100, "h"), (on, 100, "o"), (full, 60, "h")]


def happy_hop():
    """A little joyful hop. Plays for "happy".

    0-6 a crouch (anticipation), 6-13 up with ^ ^ and both hands popping up
    at the sides, 13-19 the fall and a squashed landing, 19-30 a small
    rebound and settle, hands tucking away. Ends on ^ ^: it hands over to
    the happy face.
    """
    c = Clip("happy_hop", 48)
    c.k(BODY, "y", (0, 0), (6, 8, "i"), (13, -40, "o"), (16, -36, "i"), (20, 6, "o"), (25, -6), (30, 0, "h"),
        (48, 0, "h"))
    c.k(BODY, "sx", (0, 100), (6, 108, "i"), (11, 91, "o"), (16, 98, "i"), (20, 112, "o"), (25, 97), (30, 100, "h"))
    c.k(BODY, "sy", (0, 100), (6, 92, "i"), (11, 111, "o"), (16, 102, "i"), (20, 89, "o"), (25, 103), (30, 100, "h"))
    c.k(BODY, "r", (0, 0, "h"), (6, 0), (13, 4), (22, -2), (30, 0, "h"))
    near(c, (0, 100, "h"), (6, 100), (13, 106), (24, 100, "h"))
    sx, sy = happy_on(c, 7, 11)
    c.eyes("sx", *sx)
    c.eyes("sy", *sy)
    c.k(LOOK, "y", (0, 0, "h"), (6, 0), (13, -10), (22, 0, "h"))
    c.hand(HL, (0, TUCK_L, "h"), (6, TUCK_L, "o"), (11, (-166, -78, 1.12)), (14, (-160, -70, 1.02)),
           (20, (-162, -30, 1)), (34, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (7, TUCK_R, "o"), (12, (166, -78, 1.12)), (15, (160, -70, 1.02)),
           (21, (162, -30, 1)), (36, TUCK_R, "h"))
    return tucked_at_end(c)


def excited_clap():
    """Rises and claps three times. Plays for "excited", "clap" and when a
    cable is plugged in.

    0-4 a dip, 4-10 up and stretched with ^ ^, hands growing in front below
    the eyes; claps at 16, 25 and 34 (each a squash of the body and a burst
    of sparkles); 38-48 the hands tuck, the body comes down with a small
    overshoot, the eyes open. Ends at rest. Colour: gold.
    """
    c = Clip("excited_clap", 54)
    c.k(BODY, "y", (0, 0), (4, 5, "i"), (10, -18, "o"), (16, -12), (20, -18), (25, -12), (29, -18), (34, -12),
        (40, -16), (46, 3), (50, 0, "h"), (54, 0, "h"))
    c.k(BODY, "sx", (0, 100), (4, 106, "i"), (10, 95, "o"), (16, 104), (20, 96), (25, 104), (29, 96), (34, 105),
        (40, 98), (46, 104), (50, 100, "h"))
    c.k(BODY, "sy", (0, 100), (4, 94, "i"), (10, 106, "o"), (16, 96), (20, 104), (25, 96), (29, 104), (34, 95),
        (40, 102), (46, 96), (50, 100, "h"))
    near(c, (0, 100), (10, 106, "h"), (38, 106), (48, 100, "h"))
    sx, sy = happy(c, 6, 10, 40, 46)
    c.eyes("sx", *sx)
    c.eyes("sy", *sy)
    c.k(LOOK, "y", (0, 0), (10, -8, "h"), (38, -8), (46, 0, "h"))
    # Apart (+-72, 92) between claps, together (+-16, 98) on them.
    for h, s, d in ((HL, -1, 0), (HR, 1, 1)):
        c.hand(h, (0, TUCK_L if s < 0 else TUCK_R, "h"), (4 + d, TUCK_L if s < 0 else TUCK_R, "o"),
               (11 + d, (s * 78, 88, 1.08)), (14, (s * 70, 92, 1), "i"), (16, (s * 16, 98, 1.05), "o"),
               (20, (s * 72, 88, 1), "i"), (25, (s * 16, 98, 1.05), "o"), (29, (s * 72, 88, 1), "i"),
               (34, (s * 16, 98, 1.05), "o"), (38, (s * 60, 92, 1), "i"), (46 + d, TUCK_L if s < 0 else TUCK_R, "h"))
    c.glow((0, 0, "h"), (6, 0), (16, 0.45), (36, 0.5), (48, 0, "h"))
    c.spark((0, 0, "h"), (15, 0, "l"), (16, 0.9, "o"), (22, 0.2), (24, 0.2, "l"), (25, 0.9, "o"), (31, 0.2),
            (33, 0.2, "l"), (34, 1, "o"), (46, 0, "h"))
    return tucked_at_end(c)


def wink():
    """A cheeky wink with the right eye (mirrored for the left). Plays for
    "wink" and as a playful aside.

    0-4 a small dip, 4-9 the body tilts towards the winking side and bobs up
    while the right eye shuts into a happy arch (the other opens a bit
    wider); 9-22 held with a little bob; 22-28 the eye opens and the body
    comes back. Ends at rest.
    """
    c = Clip("wink", 36)
    c.k(BODY, "y", (0, 0), (4, 4, "i"), (9, -8, "o"), (14, -3), (18, -6), (24, 2), (28, 0, "h"), (36, 0, "h"))
    c.k(BODY, "sx", (0, 100), (4, 104, "i"), (9, 97, "o"), (14, 100, "h"), (22, 100), (25, 102), (28, 100, "h"))
    c.k(BODY, "sy", (0, 100), (4, 96, "i"), (9, 104, "o"), (14, 100, "h"), (22, 100), (25, 98), (28, 100, "h"))
    c.k(BODY, "r", (0, 0), (4, -2, "i"), (10, 9, "o"), (14, 7, "h"), (20, 7), (26, -1), (30, 0, "h"))
    c.k(LOOK, "x", (0, 0, "h"), (3, 0), (9, 8, "h"), (20, 8), (28, 0, "h"))
    # The right eye shuts into a happy arch (the ^ of happy(), on one eye).
    c.k(RBM, "vy", (0, 30, "h"), (4, 30, "o"), (8, -12, "h"), (21, -12), (26, 30, "h"))
    c.k(RBM, "rad", (0, 0, "h"), (4, 0, "o"), (8, 8, "h"), (21, 8), (26, 0, "h"))
    c.eyes("sy", (0, 100, "h"), (4, 100, "o"), (8, 60, "h"), (21, 60), (26, 100, "h"), which="r")
    c.eyes("sx", (0, 100, "h"), (4, 100, "o"), (8, 115, "h"), (21, 115), (26, 100, "h"), which="r")
    c.eyes("sy", (0, 100, "h"), (4, 100, "o"), (8, 112, "h"), (21, 112), (26, 100, "h"), which="l")
    c.eyes("sx", (0, 100, "h"), (4, 100, "o"), (8, 106, "h"), (21, 106), (26, 100, "h"), which="l")
    return tucked_at_end(c)


def relieved():
    """ "Phew": tense, then a big exhale. Plays for "relieved", "phew".

    0-10 a tense hunch (small, a touch up, eyes narrow) while the right hand
    comes up to the brow; 10-22 the hand wipes across the top of the body
    and the body sinks and widens as it breathes out, the eyes closing
    softly; 22-34 the hand drops away, the eyes open; 34-46 a small
    overshoot and settle. Ends at rest.
    """
    c = Clip("relieved", 54)
    c.k(BODY, "y", (0, 0), (10, -6, "h"), (12, -6, "o"), (26, 16, "h"), (32, 16), (40, -4), (46, 0, "h"),
        (54, 0, "h"))
    c.k(BODY, "sx", (0, 100), (10, 94, "h"), (12, 94, "o"), (26, 112, "h"), (32, 112), (40, 97), (46, 100, "h"))
    c.k(BODY, "sy", (0, 100), (10, 105, "h"), (12, 105, "o"), (26, 89, "h"), (32, 89), (40, 103), (46, 100, "h"))
    c.k(BODY, "r", (0, 0), (10, -3, "h"), (12, -3), (24, 2), (32, 0, "h"))
    near(c, (0, 100), (10, 96, "h"), (12, 96), (26, 102, "h"), (32, 102), (42, 100, "h"))
    c.eyes("sx", (0, 100), (8, 92, "h"), (14, 92), (22, 112, "h"), (30, 112), (38, 100, "h"))
    c.eyes("sy", (0, 100), (8, 92, "h"), (14, 92), (22, 8, "h"), (30, 8), (36, 104), (40, 100, "h"))
    # Tense: the lids pulled a little, worried (down outside).
    c.k(LTL, "vy", (0, -30), (8, lid(0.25), "h"), (14, lid(0.25)), (22, -30, "h"))
    c.k(RTR, "vy", (0, -30), (8, lid(0.25), "h"), (14, lid(0.25)), (22, -30, "h"))
    c.k(LOOK, "y", (0, 0), (8, -6, "h"), (14, -6), (26, 10, "h"), (32, 10), (40, 0, "h"))
    # Wipe across the brow, above the eyes, in front of the body.
    c.hand(HR, (0, TUCK_R, "h"), (1, TUCK_R, "o"), (9, (78, -86, 1.05)), (12, (70, -92, 1)),
           (17, (0, -104, 1)), (21, (-68, -90, 1), "o"),
           (24, (-80, -78, 0.8), "i"), (29, (-88, -50, 0), "h"), (30, TUCK_R, "h"))
    return tucked_at_end(c)


def charged():
    """Battery full: a jolt of energy. Plays when the battery is full.

    0-3 a quick squash, 3-8 a jolt up, stretched tall, fists up over the
    sides, eyes wide, a strong glow and a burst of sparks, a buzz (8-14);
    14-30 it settles proud: a little high, ^ ^, fists coming down; 30-46
    hands tuck, body back with a small overshoot, glow fading. Ends at rest.
    Colour: green.
    """
    c = Clip("charged", 54)
    c.k(BODY, "x", (0, 0, "h"), (8, 0, "l"), (9, 4, "l"), (10, -4, "l"), (11, 4, "l"), (12, -3, "l"), (13, 2, "l"),
        (14, 0, "h"))
    c.k(BODY, "y", (0, 0), (3, 6, "i"), (8, -26, "o"), (16, -16), (22, -10, "h"), (32, -10), (40, 3), (46, 0, "h"),
        (54, 0, "h"))
    c.k(BODY, "sx", (0, 100), (3, 108, "i"), (8, 86, "o"), (16, 96), (22, 97, "h"), (32, 97), (40, 103),
        (46, 100, "h"))
    c.k(BODY, "sy", (0, 100), (3, 92, "i"), (8, 118, "o"), (16, 105), (22, 104, "h"), (32, 104), (40, 97),
        (46, 100, "h"))
    near(c, (0, 100), (3, 98, "i"), (8, 108, "o"), (22, 104, "h"), (32, 104), (44, 100, "h"))
    sx, sy = happy(c, 16, 20, 34, 40)
    c.eyes("sx", (0, 100), (3, 100, "o"), (7, 112), (14, 100, "h"), *sx)
    c.eyes("sy", (0, 100), (3, 92, "o"), (7, 130), (14, 100, "h"), *sy)
    c.k(LOOK, "y", (0, 0), (8, -12, "h"), (32, -12), (42, 0, "h"))
    c.hand(HL, (0, TUCK_L, "h"), (2, TUCK_L, "o"), (8, (-136, -132, 1.2)), (12, (-128, -122, 1.15)),
           (22, (-140, -90, 1.1), "h"), (30, (-140, -90, 1.1)), (42, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (3, TUCK_R, "o"), (9, (136, -132, 1.2)), (13, (128, -122, 1.15)),
           (23, (140, -90, 1.1), "h"), (31, (140, -90, 1.1)), (43, TUCK_R, "h"))
    c.glow((0, 0), (3, 0.2, "o"), (8, 1), (16, 0.7), (30, 0.6), (48, 0, "h"))
    c.spark((0, 0, "h"), (3, 0, "o"), (8, 1), (16, 0.5), (30, 0.35), (44, 0, "h"))
    return tucked_at_end(c)


def love():
    """Hugs itself, swaying. Plays for "love".

    0-4 a little dip, 4-12 both hands cross over the lower front (a hug), the
    body squeezes in a touch and leans near, ^ ^; 12-54 a sway side to side
    with the pink glow beating like a heart and a few sparkles; 54-66 the
    hands let go, the eyes open, it settles. Ends at rest. Colour: pink.
    """
    c = Clip("love", 72)
    c.k(BODY, "x", (0, 0, "h"), (12, 0), (22, -10), (34, 10), (46, -8), (56, 4), (64, 0, "h"))
    c.k(BODY, "y", (0, 0), (4, 4, "i"), (12, -4), (22, 0), (28, -4), (34, 0), (40, -4), (46, 0), (52, -3), (60, 2),
        (66, 0, "h"), (72, 0, "h"))
    c.k(BODY, "sx", (0, 100), (4, 104, "i"), (12, 96, "h"), (54, 96), (60, 102), (66, 100, "h"))
    c.k(BODY, "sy", (0, 100), (4, 96, "i"), (12, 104, "h"), (54, 104), (60, 98), (66, 100, "h"))
    c.k(BODY, "r", (0, 0, "h"), (12, 0), (22, -7), (34, 7), (46, -6), (56, 3), (64, 0, "h"))
    near(c, (0, 100), (12, 106, "h"), (56, 106), (66, 100, "h"))
    sx, sy = happy(c, 4, 9, 56, 62)
    c.eyes("sx", *sx)
    c.eyes("sy", *sy)
    c.k(LOOK, "x", (0, 0, "h"), (12, 0), (22, -8), (34, 8), (46, -6), (56, 0, "h"))
    c.k(LOOK, "y", (0, 0), (12, 6, "h"), (56, 6), (64, 0, "h"))
    # Crossed: the left hand on the right side, the right hand on the left.
    c.hand(HL, (0, TUCK_L, "h"), (3, TUCK_L, "o"), (11, (36, 106, 1.12)), (14, (34, 100, 1.05), "h"),
           (54, (34, 100, 1.05), "i"), (64, TUCK_L, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (5, TUCK_R, "o"), (13, (-38, 96, 1.12)), (16, (-34, 92, 1.05), "h"),
           (55, (-34, 92, 1.05), "i"), (65, TUCK_R, "h"))
    c.glow((0, 0, "h"), (4, 0), (12, 0.5, "i"), (14, 0.8, "o"), (22, 0.35, "i"), (24, 0.75, "o"), (34, 0.35, "i"),
           (36, 0.8, "o"), (46, 0.35, "i"), (48, 0.75, "o"), (66, 0, "h"))
    c.spark((0, 0, "h"), (10, 0), (16, 0.35, "h"), (50, 0.35), (62, 0, "h"))
    return tucked_at_end(c)


def giggle():
    """Tittering behind a hand. Plays for "giggle", "funny".

    0-4 a small dip, 4-8 ^ ^ and the right hand up to the mouth, below the
    eyes; 8-32 quick small shakes of the body (up and down, a wobble);
    32-42 the hand goes back, the eyes open. Ends at rest.
    """
    c = Clip("giggle", 48)
    shake = [(8 + 3 * i, -5 if i % 2 == 0 else 0, "l") for i in range(8)]
    c.k(BODY, "y", (0, 0), (4, 3, "i"), *shake, (32, 0), (36, -2), (40, 0, "h"), (48, 0, "h"))
    c.k(BODY, "r", (0, 0), (4, 0), *[(8 + 3 * i, 3 if i % 2 == 0 else -3, "l") for i in range(8)], (32, 0, "h"))
    c.k(BODY, "sx", (0, 100), (4, 104, "i"), *[(8 + 3 * i, 97 if i % 2 == 0 else 102, "l") for i in range(8)],
        (32, 100, "h"))
    c.k(BODY, "sy", (0, 100), (4, 96, "i"), *[(8 + 3 * i, 103 if i % 2 == 0 else 98, "l") for i in range(8)],
        (32, 100, "h"))
    sx, sy = happy(c, 3, 7, 34, 40)
    c.eyes("sx", *sx)
    c.eyes("sy", *sy)
    c.k(LOOK, "y", (0, 0), (6, -4, "h"), (32, -4), (40, 0, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (2, TUCK_R, "o"), (8, (36, 98, 0.95)), (10, (34, 102, 0.92), "h"),
           (30, (34, 102, 0.92), "i"), (40, TUCK_R, "h"))
    return tucked_at_end(c)


def high_five():
    """A high five with the other Yoru on its right (mirrored for the left).

    0-10 anticipation: eyes to the right, a lean back left and a crouch;
    10-17 it reaches up-right, the right hand grows and swings up; 18 the
    slap: the hand flattens big, the body squashes, a burst of sparkles and
    a flash of glow, a squint; 20-34 recoil, ^ ^, the hand comes back;
    34-48 settle with a small overshoot. Ends at rest. Colour: gold.
    """
    c = Clip("high_five", 54)
    c.k(BODY, "x", (0, 0), (10, -12, "i"), (17, 26, "o"), (19, 22), (26, 8), (36, -3), (42, 0, "h"), (54, 0, "h"))
    c.k(BODY, "y", (0, 0), (10, 6, "i"), (17, -14, "o"), (19, -10), (26, -4), (34, 2), (40, 0, "h"))
    c.k(BODY, "sx", (0, 100), (10, 106, "i"), (16, 94, "o"), (18, 94, "l"), (19, 108, "o"), (26, 97), (32, 102),
        (38, 100, "h"))
    c.k(BODY, "sy", (0, 100), (10, 94, "i"), (16, 107, "o"), (18, 107, "l"), (19, 92, "o"), (26, 103), (32, 98),
        (38, 100, "h"))
    c.k(BODY, "r", (0, 0), (10, -5, "i"), (17, 9, "o"), (19, 11), (28, 2), (36, -2), (42, 0, "h"))
    c.k(LOOK, "x", (0, 0, "o"), (4, 34, "h"), (30, 34), (42, 0, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (4, 0), (12, -14, "h"), (28, -14), (40, 0, "h"))
    sx, sy = happy(c, 19, 23, 38, 44)
    c.eyes("sx", (0, 100, "h"), (2, 100), (8, 108, "h"), (17, 108), *sx)
    c.eyes("sy", (0, 100, "h"), (2, 100), (8, 114, "h"), (17, 114), *sy)
    c.hand(HR, (0, TUCK_R, "h"), (6, TUCK_R, "o"), (11, (150, 10, 1)), (16, (196, -112, 1.15), "i"),
           (18, (206, -120, 1.15), "o"), (19, (200, -116, 1.4), "o"), (22, (190, -108, 1.1)), (28, (170, -80, 1.05)),
           (34, (150, -20, 1), "i"), (40, TUCK_R, "h"))
    c.hand(HL, (0, TUCK_L, "h"), (8, TUCK_L, "o"), (13, (-150, 40, 0.95)), (19, (-158, 20, 0.95)), (30, TUCK_L, "h"))
    c.glow((0, 0, "h"), (17, 0, "l"), (19, 0.8, "o"), (36, 0, "h"))
    c.spark((0, 0, "h"), (17, 0, "l"), (19, 1, "o"), (38, 0, "h"))
    return tucked_at_end(c)


def chat():
    """Chatting with the other Yoru on its right (mirrored for the left).

    0-6 it turns its face that way and leans a touch; 6-28 little "talking"
    bobs with the lids lifting and dropping like eyebrows and the right hand
    making a small gesture; 28-33 back to the front. Ends at rest.
    """
    c = Clip("chat", 39)
    c.k(BODY, "y", (0, 0), (3, 2, "i"), (8, -4), (11, 0), (14, -5), (17, 0), (21, -4), (24, 0), (27, -3), (32, 0, "h"),
        (39, 0, "h"))
    c.k(BODY, "sy", (0, 100), (3, 98, "i"), (8, 103), (11, 99), (14, 104), (17, 99), (21, 103), (24, 99), (27, 102),
        (32, 100, "h"))
    c.k(BODY, "sx", (0, 100), (3, 102, "i"), (8, 98), (11, 101), (14, 97), (17, 101), (21, 98), (24, 101), (27, 99),
        (32, 100, "h"))
    c.k(BODY, "r", (0, 0), (6, 5, "h"), (26, 5), (33, 0, "h"))
    c.k(LOOK, "x", (0, 0, "o"), (6, 36, "h"), (26, 36), (33, 0, "h"))
    c.k(LOOK, "sx", (0, 100, "o"), (6, 88, "h"), (26, 88), (33, 100, "h"))
    c.k(LOOK, "y", (0, 0), (6, -4, "h"), (26, -4), (33, 0, "h"))
    # Eyebrows: the lids drop between words and lift on them.
    for v in LIDS:
        c.k(v, "vy", (0, -30, "h"), (6, -30), (9, lid(0.2)), (13, -30), (16, lid(0.25)), (20, -30), (23, lid(0.2)),
            (27, -30, "h"))
    c.eyes("sy", (0, 100, "h"), (6, 100), (8, 108), (13, 100), (15, 110), (20, 100), (22, 106), (27, 100, "h"))
    c.hand(HR, (0, TUCK_R, "h"), (4, TUCK_R, "o"), (10, (150, 24, 0.95)), (15, (164, -18, 1)), (19, (152, 10, 0.95)),
           (23, (160, -8, 0.95), "i"), (31, TUCK_R, "h"))
    return tucked_at_end(c)


def chat_reply():
    """The reply in a chat with the other Yoru on its right (mirrored for
    the left).

    0-6 it turns to listen, eyes wide, a slight tilt; 8-22 two nods (the
    face dips, the body follows); 22-31 a small happy bounce with ^ ^;
    31-34 back to the front. Ends at rest.
    """
    c = Clip("chat_reply", 39)
    c.k(BODY, "y", (0, 0, "h"), (8, 0), (11, 7), (14, 0), (17, 7), (20, 0), (23, 4, "i"), (27, -10, "o"),
        (31, 2), (34, 0, "h"), (39, 0, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (8, 100), (11, 103), (14, 100), (17, 103), (20, 100), (23, 105, "i"),
        (27, 95, "o"), (31, 103), (34, 100, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (8, 100), (11, 97), (14, 100), (17, 97), (20, 100), (23, 95, "i"),
        (27, 105, "o"), (31, 97), (34, 100, "h"))
    c.k(BODY, "r", (0, 0), (6, 4, "h"), (24, 4), (32, 0, "h"))
    c.k(LOOK, "x", (0, 0, "o"), (5, 34, "h"), (26, 34), (33, 0, "h"))
    c.k(LOOK, "sx", (0, 100, "o"), (5, 90, "h"), (26, 90), (33, 100, "h"))
    c.k(LOOK, "y", (0, 0, "h"), (8, 0), (11, 18), (14, 0), (17, 18), (20, 0, "h"), (33, 0, "h"))
    sx, sy = happy(c, 22, 25, 30, 34)
    c.eyes("sx", (0, 100, "o"), (5, 106, "h"), (20, 106), (22, 100), *sx[1:])
    c.eyes("sy", (0, 100, "o"), (5, 112, "h"), (20, 112), (22, 100), *sy[1:])
    return tucked_at_end(c)


NEW = {f.__name__: f for f in (happy_hop, excited_clap, wink, relieved, charged, love, giggle, high_five, chat,
                               chat_reply)}
