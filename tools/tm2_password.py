#!/usr/bin/env python3
"""Twisted Metal 2 password encoder / decoder.

The format was recovered from the encoder at `func_8012CDFC` and the validator
at `func_8012CE7C` in the retail executable.

A password is six symbols, read left to right as a base-5 number:

    _ = 0   (blank)      T = 1  (triangle)   O = 2 (circle)
    X = 3   (cross)      S = 4  (square)

That 14-bit value is laid out as:

    bit  0        always 0 (the encoder clears it, the validator rejects it set)
    bits 1-3      level - 1        valid levels are 2..8
    bits 4-8      character + 4    the encoder writes FIVE bits here
    bit  9        final-battle flag; if set, the level must be 8
    bits 10-13    checksum = ((character + 1) * (level - 1)) mod 13

Character indices are the roster order in docs/CARS.md.

THERE ARE EIGHT STAGES, NOT SEVEN
---------------------------------
Bit 9 is not a redundant marker for "level is 8". In the encoder it is the
third argument, passed in by the caller:

    8012CE2C  sll $a2, $a2, 9        ; final flag, an argument
    8012CE34  sll $a1, $a1, 4        ; (character + 4) << 4
    8012CE3C  sll $a0, $a0, 1        ; (level - 1) << 1

and the validator hands it back to its caller as a separate output byte. So
level 8 comes in two variants that differ by exactly 512:

    level 8, bit 9 clear   ->  Hong Kong
    level 8, bit 9 set     ->  Dark Tooth's final battle

Both carry the same checksum, because the checksum is computed from the level.

WHY THREE CHARACTERS' PASSWORDS NEVER WORK
------------------------------------------
The encoder writes `character + 4` as a **five**-bit field spanning bits 4..8.
The validator reads only **four** bits (4..7) and treats bit 8 as a separate
flag.

For characters 0..11 that is harmless: `character + 4` is 4..15 and fits in
four bits, so bit 8 stays clear and the password round-trips. (In fact bit 8
is then never set by anything, which makes it a dead field in every password
the stock game produces.)

Characters 12 (Sweet Tooth), 13 (Minion) and 14 (Dark Tooth) give
`character + 4` = 16, 17, 18 -- which overflow into bit 8. The validator then
reads a nibble of 0, 1 or 2, subtracts 4, gets -4, -3 or -2, and rejects it
with `sltiu ..., 12`.

So the game will happily *show* you a password while you play as any of those
three, on every stage, with a correct checksum computed from the real
character index. None of them can ever be typed back in. It is not a
placeholder -- it is a real password in a format that cannot represent the
character it is describing.

The `tm2.password-fix` mod repairs the validator to agree with the encoder, so
all three become enterable. Pass `--fixed` to model that. See
docs/PASSWORDS.md.

Usage:
    python tools/tm2_password.py decode "T O _ _ O X"
    python tools/tm2_password.py decode "T O _ _ O X" --fixed
    python tools/tm2_password.py encode --char 7 --level 8
    python tools/tm2_password.py encode --char 7 --level 8 --final
    python tools/tm2_password.py table --char 14 --fixed
"""

import argparse

SYMS = "_TOXS"
NAMES = {"_": "blank", "T": "triangle", "O": "circle", "X": "cross", "S": "square"}
CHARS = [
    "Hammerhead", "Outlaw 2", "Warthog", "Mr. Grimm", "Grasshopper", "Thumper",
    "Spectre", "Roadkill", "Twister", "Axel", "Mr. Slam", "Shadow",
    "Sweet Tooth", "Minion", "Dark Tooth",
]

# The eight entry points, in play order. Level 8 appears twice: the stage
# itself, and the boss fight that follows it.
STAGES = [
    (2, 0, "Moscow"),
    (3, 0, "Paris"),
    (4, 0, "Amazonia"),
    (5, 0, "New York"),
    (6, 0, "Antarctica"),
    (7, 0, "Holland"),
    (8, 0, "Hong Kong"),
    (8, 1, "Dark Tooth (final battle)"),
]


def stage_name(level, final):
    for lvl, fin, name in STAGES:
        if lvl == level and fin == final:
            return name
    return "level %d" % level


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


def encode(char, level, final=False):
    """Mirror func_8012CDFC: (level, character, final) -> value.

    `final` is the encoder's third argument, not a function of the level.
    """
    checksum = ((char + 1) * (level - 1)) % 13
    v = ((level - 1) & 7) << 1
    v |= ((char + 4) & 31) << 4          # five bits; overflows for char >= 12
    if final:
        v |= 1 << 9
    v |= (checksum & 15) << 10
    return v & ~1                        # the encoder's closing `andi $v0, -2`


def decode(value, fixed=False):
    """Mirror func_8012CE7C exactly, including every rejection.

    Returns ((character, level, final), "ok") or (None, reason).

    With `fixed`, mirror it as the `tm2.password-fix` mod patches it: the
    character field is read as five bits (`andi 31`), the bound is 15
    (`sltiu 15`), and the spare flag comes from bit 14 (`srl 14`) because
    bit 8 now belongs to the character field. See docs/PASSWORDS.md.
    """
    field_mask, limit, flag_bit = (31, 15, 14) if fixed else (15, 12, 8)

    if value & 1:
        return None, "bit 0 is set"
    level = ((value >> 1) & 7) + 1
    if not 0 <= level - 2 < 7:
        return None, "level %d outside 2..8" % level
    nibble = (value >> 4) & field_mask
    char = nibble - 4
    if not 0 <= char < limit:
        extra = ""
        if not fixed and nibble in (0, 1, 2):
            extra = "  (looks like %s -- character field overflowed)" % CHARS[nibble + 12]
        return None, "character field %d -> %d, needs 0..%d%s" % (
            nibble, char, limit - 1, extra)
    final = (value >> 9) & 1
    other_flag = (value >> flag_bit) & 1
    if final and level < 8:
        return None, "final-battle flag set but level is %d" % level
    if not final and other_flag:
        return None, "bit %d set%s" % (
            flag_bit, "" if fixed else " (character field overflowed)")
    want = ((char + 1) * (level - 1)) % 13
    got = (value >> 10) & 15
    if want != got:
        return None, "checksum %d, expected %d" % (got, want)
    return (char, level, final), "ok"


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    fix_help = "validate as the tm2.password-fix mod does (all 15 characters)"
    sub = ap.add_subparsers(dest="cmd", required=True)

    d = sub.add_parser("decode", help="decode a six-symbol password")
    d.add_argument("password", help='e.g. "T O _ _ O X"')
    d.add_argument("--fixed", action="store_true", help=fix_help)

    e = sub.add_parser("encode", help="build the password the game would show")
    e.add_argument("--char", type=int, required=True, help="0..14, see docs/CARS.md")
    e.add_argument("--level", type=int, required=True, help="2..8")
    e.add_argument("--final", action="store_true",
                   help="the Dark Tooth battle after Hong Kong (level 8 only)")
    e.add_argument("--fixed", action="store_true", help=fix_help)

    t = sub.add_parser("table", help="every stage for one character")
    t.add_argument("--char", type=int, required=True)
    t.add_argument("--fixed", action="store_true", help=fix_help)

    a = ap.parse_args()

    if a.cmd == "decode":
        v = to_value(a.password)
        res, why = decode(v, a.fixed)
        print("value %d (0x%04X)" % (v, v))
        if res:
            char, level, final = res
            print("  character %d  %s" % (char, CHARS[char]))
            print("  stage     %d  %s" % (level, stage_name(level, final)))
            print("  ACCEPTED")
        else:
            print("  REJECTED: %s" % why)
        return 0

    if a.cmd == "encode":
        if not 0 <= a.char < len(CHARS):
            raise SystemExit("char must be 0..%d" % (len(CHARS) - 1))
        if not 2 <= a.level <= 8:
            raise SystemExit("level must be 2..8")
        if a.final and a.level != 8:
            raise SystemExit("--final is only valid with --level 8")
        v = encode(a.char, a.level, a.final)
        res, why = decode(v, a.fixed)
        print("%s  ->  %s, %s" % (
            to_symbols(v), CHARS[a.char], stage_name(a.level, int(a.final))))
        print("  value %d (0x%04X)" % (v, v))
        print("  round-trips: %s" % ("yes" if res else "NO -- " + why))
        return 0

    if a.cmd == "table":
        if not 0 <= a.char < len(CHARS):
            raise SystemExit("char must be 0..%d" % (len(CHARS) - 1))
        print("%s (index %d)\n" % (CHARS[a.char], a.char))
        for level, final, name in STAGES:
            v = encode(a.char, level, final)
            res, why = decode(v, a.fixed)
            mark = "accepted" if res else "REJECTED"
            print("  %-26s %-14s %s" % (name, to_symbols(v), mark))
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
