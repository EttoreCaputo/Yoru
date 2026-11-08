"""Answers to an approval: yes and no."""
from author import BODY, LOOK, LTL, LTR, RTL, RTR, Clip, lid
from library import happy


def nod_yes():
    """An approval said yes: the face dips twice, the body bobbing with it
    (the second nod smaller), and it smiles. Ends at rest."""
    c = Clip("nod_yes", 32)
    c.k(LOOK, "y", (0, 0, "h"), (2, 0), (6, 24, "o"), (10, -6), (14, 16, "o"), (18, -2), (22, 0, "h"))
    c.k(BODY, "y", (0, 0, "h"), (3, 0), (7, 6, "o"), (11, -2), (15, 4, "o"), (19, 0, "h"))
    c.k(BODY, "sy", (0, 100, "h"), (3, 100), (7, 96, "o"), (11, 102), (15, 98), (19, 100, "h"))
    c.k(BODY, "sx", (0, 100, "h"), (3, 100), (7, 104, "o"), (11, 99), (15, 102), (19, 100, "h"))
    sx, sy = happy(c, 8, 12, 22, 27)
    c.eyes("sx", *sx)
    c.eyes("sy", *sy)
    return c


def shake_no():
    """An approval said no: a little frown, the face swings left, right,
    left and back (the face narrowing as it turns), the body rocking
    against it. Ends at rest."""
    c = Clip("shake_no", 34)
    c.k(LOOK, "x", (0, 0, "h"), (3, 0), (7, -36, "o"), (12, 34), (17, -24), (22, 8), (26, 0, "h"))
    c.k(LOOK, "sx", (0, 100, "h"), (3, 100), (7, 90), (10, 100), (12, 91), (15, 100), (17, 94), (22, 100, "h"))
    c.k(BODY, "r", (0, 0, "h"), (3, 0), (8, 7, "o"), (13, -7), (18, 4), (23, -1), (27, 0, "h"))
    c.k(BODY, "x", (0, 0, "h"), (3, 0), (8, -8, "o"), (13, 8), (18, -4), (24, 0, "h"))
    # Frowning: the inner corners of the top lids come down a little.
    for v, t in ((LTL, 0.1), (LTR, 0.42), (RTL, 0.42), (RTR, 0.1)):
        c.k(v, "vy", (0, -30, "h"), (2, -30), (6, lid(t), "h"), (24, lid(t)), (29, -30, "h"))
    return c


NEW = {f.__name__: f for f in (nod_yes, shake_no)}
