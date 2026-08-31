#!/usr/bin/env python3
"""Twisted Metal 2 password encoder / decoder.

The format was recovered from the validator at `func_8012CE7C` in the retail
executable, and verified against every published password on the community
cheat list -- all decode, checksums included.

A password is six symbols, read left to right as a base-5 number:

    _ = 0   (blank)      T = 1  (triangle)   O = 2 (circle)
    X = 3   (cross)      S = 4  (square)

That 14-bit value is laid out as:

    bit  0        must be 0
    bits 1-3      level - 1        valid levels are 2..8
    bits 4-7      character + 4    valid characters are 0..11
    bit  8        flag             set when the level is the final stage
    bit  9        final-stage flag; if set, the level must be >= 8
    bits 10-13    checksum = ((character + 1) * (level - 1)) mod 13

Character indices are the roster order in docs/CARS.md.

WHY DARK TOOTH'S PASSWORD NEVER WORKS
-------------------------------------
The generator writes `character + 4` as a **five**-bit field spanning bits
4..8. The validator reads only **four** bits (4..7) and treats bit 8 as a
separate flag.

For characters 0..11 that is harmless: `character + 4` is 4..15 and fits in
four bits, so bit 8 stays clear and the password round-trips.

Characters 12 (Sweet Tooth), 13 (Minion) and 14 (Dark Tooth) give
`character + 4` = 16, 17, 18 -- which overflow into bit 8. The validator then
reads a nibble of 0, 1 or 2, subtracts 4, gets -4, -3 or -2, and rejects it
with `sltiu ..., 12`.

So the game will happily *show* you a password while you play as any of those
three, on every level, with a correct checksum computed from the real
character index. None of them can ever be typed back in. It is not a
placeholder -- it is a real password in a format that cannot represent the
character it is describing.

Usage:
    python tools/tm2_password.py decode "T O _ _ O X"
    python tools/tm2_password.py encode --char 7 --level 2
    python tools/tm2_password.py table --char 14
"""

import argparse

SYMS = "_TOXS"
NAMES = {"_": "blank", "T": "triangle", "O": "circle", "X": "cross", "S": "square"}
CHARS = [
    "Hammerhead", "Outlaw 2", "Warthog", "Mr. Grimm", "Grasshopper", "Thumper",
    "Spectre", "Roadkill", "Twister", "Axel", "Mr. Slam", "Shadow",
    "Sweet Tooth", "Minion", "Dark Tooth",
]
LEVELS = {
    2: "Moscow", 3: "Paris", 4: "Amazonia", 5: "New York",
    6: "Antarctica", 7: "Holland", 8: "Hong Kong / Dark Tooth",
}


def to_symbols(value):
    out = []
    for _ in range(6):
        out.append(SYMS[value % 5])
        value //= 5
    return " ".join(reversed(out))


def to_value(password):
    v = 0
    for s in password.replace(",", " ").split():
        s = s.upper()
        if s not in SYMS:
            raise SystemExit("unknown symbol %r (use _ T O X S)" % s)
        v = v * 5 + SYMS.index(s)
    return v


def encode(char, level):
    """Reproduce what the game displays, five-bit character field and all."""
    checksum = ((char + 1) * (level - 1)) % 13
    v = ((level - 1) & 7) << 1
    v |= ((char + 4) & 31) << 4          # five bits; overflows for char >= 12
    if level >= 8:
        v |= 1 << 9
    v |= (checksum & 15) << 10
    return v


def decode(value):
    """Mirror func_8012CE7C exactly, including every rejection."""
    if value & 1:
        return None, "bit 0 is set"
    level = ((value >> 1) & 7) + 1
    if not 0 <= level - 2 < 7:
        return None, "level %d outside 2..8" % level
    nibble = (value >> 4) & 15
    char = nibble - 4
    if not 0 <= char < 12:
        extra = ""
        if nibble in (0, 1, 2):
            extra = "  (looks like %s -- character field overflowed)" % CHARS[nibble + 12]
        return None, "character field %d -> %d, needs 0..11%s" % (nibble, char, extra)
    final_flag = (value >> 9) & 1
    other_flag = (value >> 8) & 1
    if final_flag and level < 8:
        return None, "final-stage flag set but level is %d" % level
    if not final_flag and other_flag:
        return None, "bit 8 set (character field overflowed)"
    want = ((char + 1) * (level - 1)) % 13
    got = (value >> 10) & 15
    if want != got:
        return None, "checksum %d, expected %d" % (got, want)
    return (char, level), "ok"


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    d = sub.add_parser("decode", help="decode a six-symbol password")
    d.add_argument("password", help='e.g. "T O _ _ O X"')

    e = sub.add_parser("encode", help="build the password the game would show")
    e.add_argument("--char", type=int, required=True, help="0..14, see docs/CARS.md")
    e.add_argument("--level", type=int, required=True, help="2..8")

    t = sub.add_parser("table", help="every level for one character")
    t.add_argument("--char", type=int, required=True)

    a = ap.parse_args()

    if a.cmd == "decode":
        v = to_value(a.password)
        res, why = decode(v)
        print("value %d (0x%04X)" % (v, v))
        if res:
            char, level = res
            print("  character %d  %s" % (char, CHARS[char]))
            print("  level     %d  %s" % (level, LEVELS[level]))
            print("  ACCEPTED")
        else:
            print("  REJECTED: %s" % why)
        return 0

    if a.cmd == "encode":
        if not 0 <= a.char < len(CHARS):
            raise SystemExit("char must be 0..%d" % (len(CHARS) - 1))
        if not 2 <= a.level <= 8:
            raise SystemExit("level must be 2..8")
        v = encode(a.char, a.level)
        res, why = decode(v)
        print("%s  ->  %s, %s" % (to_symbols(v), CHARS[a.char], LEVELS[a.level]))
        print("  value %d (0x%04X)" % (v, v))
        print("  round-trips: %s" % ("yes" if res else "NO -- " + why))
        return 0

    if a.cmd == "table":
        if not 0 <= a.char < len(CHARS):
            raise SystemExit("char must be 0..%d" % (len(CHARS) - 1))
        print("%s (index %d)\n" % (CHARS[a.char], a.char))
        for lvl in range(2, 9):
            v = encode(a.char, lvl)
            res, why = decode(v)
            mark = "accepted" if res else "REJECTED"
            print("  %-22s %-14s %s" % (LEVELS[lvl], to_symbols(v), mark))
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
