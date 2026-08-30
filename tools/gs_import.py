#!/usr/bin/env python3
"""Convert a GameShark code list into a validated cheat table.

GameShark codes are the cheapest RAM map available for a title with no
symbols: each code names a memory location and the value that makes something
happen. This turns a raw .txt dump into typed, region-classified,
EXE-validated TOML that the debug plugin under src/mods/ consumes at runtime.

PS1 GameShark code types handled here:

    30AAAAAA 00VV   write byte VV to 0x80AAAAAA every frame
    80AAAAAA VVVV   write halfword VVVV to 0x80AAAAAA every frame
    D0AAAAAA VVVV   run the NEXT line only if the halfword at 0x80AAAAAA == VVVV

The address nibbles are an offset into the 2 MB of main RAM, so OR-ing
0x80000000 recovers the KSEG0 address.

Every target is classified against this game's actual PS-X EXE layout:

    low    0x80010000..0x800CDC54  streamed level / shell data (not in the EXE)
    image  0x800CDC54..0x80181454  the EXE itself: code + initialised data
    bss    0x80181454..0x801FFFF0  runtime state (player structs, and so on)

For image targets the original word is read out of the EXE and recorded, so a
patch can be applied guarded (failing closed on the wrong disc revision) and
so instruction patches can be sanity-checked.

Usage:
    python tools/gs_import.py ../Cheats.txt -o mods/tm2-debug/cheats.toml
"""

import argparse
import collections
import os
import re
import struct
import sys

LOAD_ADDR = 0x800CDC54
TEXT_SIZE = 0x000B3800
TEXT_END = LOAD_ADDR + TEXT_SIZE
RAM_LO = 0x80010000
RAM_HI = 0x80200000

WRITE8, WRITE16, IF_EQ16 = 0x30, 0x80, 0xD0
KIND = {WRITE8: "write8", WRITE16: "write16", IF_EQ16: "if_eq16"}

MIPS_OPS = {
    2: "j", 3: "jal", 4: "beq", 5: "bne", 6: "blez", 7: "bgtz", 8: "addi",
    9: "addiu", 10: "slti", 11: "sltiu", 12: "andi", 13: "ori", 14: "xori",
    15: "lui", 32: "lb", 33: "lh", 35: "lw", 36: "lbu", 37: "lhu", 40: "sb",
    41: "sh", 43: "sw",
}
MIPS_SPECIAL = {
    0: "sll", 2: "srl", 3: "sra", 8: "jr", 9: "jalr", 12: "syscall",
    32: "add", 33: "addu", 34: "sub", 35: "subu", 36: "and", 37: "or",
    38: "xor", 42: "slt", 43: "sltu",
}
REGS = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]


def disasm(word, pc):
    """Just enough MIPS to make a patched instruction readable in a comment."""
    if word is None:
        return "?"
    if word == 0:
        return "nop"
    op = word >> 26
    rs = (word >> 21) & 31
    rt = (word >> 16) & 31
    rd = (word >> 11) & 31
    sa = (word >> 6) & 31
    imm = word & 0xFFFF
    simm = imm - 0x10000 if imm & 0x8000 else imm
    if op == 0:
        fn = word & 0x3F
        name = MIPS_SPECIAL.get(fn, "special.%02X" % fn)
        if fn == 8:
            return "jr $%s" % REGS[rs]
        if fn in (0, 2, 3):
            return "%s $%s, $%s, %d" % (name, REGS[rd], REGS[rt], sa)
        return "%s $%s, $%s, $%s" % (name, REGS[rd], REGS[rs], REGS[rt])
    name = MIPS_OPS.get(op, "op.%02X" % op)
    if op in (2, 3):
        return "%s 0x%08X" % (name, (pc & 0xF0000000) | ((word & 0x3FFFFFF) << 2))
    if op in (4, 5):
        return "%s $%s, $%s, 0x%08X" % (name, REGS[rs], REGS[rt], pc + 4 + simm * 4)
    if op in (6, 7):
        return "%s $%s, 0x%08X" % (name, REGS[rs], pc + 4 + simm * 4)
    if op == 15:
        return "lui $%s, 0x%04X" % (REGS[rt], imm)
    if op in (32, 33, 35, 36, 37, 40, 41, 43):
        return "%s $%s, %d($%s)" % (name, REGS[rt], simm, REGS[rs])
    return "%s $%s, $%s, %d" % (name, REGS[rt], REGS[rs], simm)


def region_of(addr):
    if RAM_LO <= addr < LOAD_ADDR:
        return "low"
    if LOAD_ADDR <= addr < TEXT_END:
        return "image"
    if TEXT_END <= addr < RAM_HI:
        return "bss"
    return "invalid"


def slugify(name):
    s = re.sub(r"[^a-z0-9]+", "-", name.lower())
    return s.strip("-") or "unnamed"


def categorise(name):
    if re.search(r"\bP1\b|Player 1", name):
        return "Player 1"
    if re.search(r"\bP2\b|Player 2", name):
        return "Player 2"
    if re.search(r"Kill Enemy|Computer \d", name):
        return "Enemies"
    if re.search(r"Infinite|Energy|Turbo|Shield|Frozen", name):
        return "Player"
    if re.search(r"Modifier|Select|Slot|Level|Difficulty|Map|Background", name):
        return "Setup"
    if re.search(r"Skip|Movie|Intro", name):
        return "Boot"
    return "Misc"


def parse(path):
    """Return [(name, [(code, value, lineno) or ("BAD", text, lineno)], lineno)]."""
    cheats = []
    cur = None
    for lineno, raw in enumerate(open(path, encoding="latin1"), 1):
        line = raw.strip()
        if not line:
            continue
        m = re.match(r"^\[(.*)\]$", line)
        if m:
            cur = (m.group(1).strip(), [], lineno)
            cheats.append(cur)
            continue
        if cur is None:
            continue
        m = re.match(r"^([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{4})$", line)
        if m:
            cur[1].append((int(m.group(1), 16), int(m.group(2), 16), lineno))
        else:
            cur[1].append(("BAD", line, lineno))
    return cheats


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="GameShark code list (.txt)")
    ap.add_argument("-o", "--out", required=True, help="output .toml")
    ap.add_argument("--exe", default="disc/SCUS_943.06",
                    help="boot EXE, for validating image-region targets")
    args = ap.parse_args()

    img = None
    if os.path.isfile(args.exe):
        img = open(args.exe, "rb").read()[2048:2048 + TEXT_SIZE]
    else:
        sys.stderr.write("warning: %s not found; image targets unvalidated\n"
                         % args.exe)

    def word_at(addr):
        if img is None or not (LOAD_ADDR <= addr < TEXT_END - 3):
            return None
        off = addr - LOAD_ADDR
        return struct.unpack("<I", img[off:off + 4])[0]

    out = []
    problems = []
    seen = collections.Counter()
    stats = collections.Counter()

    for name, codes, lineno in parse(args.input):
        if not name:
            problems.append("line %d: cheat with an empty name; skipped" % lineno)
            continue
        if not codes:
            problems.append("line %d: [%s] has no code lines; skipped"
                            % (lineno, name))
            continue

        seen[name] += 1
        cid = slugify(name)
        if seen[name] > 1:
            cid = "%s-%d" % (cid, seen[name])
            problems.append("line %d: duplicate name [%s]; id -> '%s'"
                            % (lineno, name, cid))

        ops = []
        for entry in codes:
            if entry[0] == "BAD":
                problems.append("line %d: [%s] unparseable code line %r"
                                % (entry[2], name, entry[1]))
                continue
            code, value, ln = entry
            ctype = code >> 24
            addr = 0x80000000 | (code & 0x00FFFFFF)
            if ctype not in KIND:
                problems.append("line %d: [%s] unsupported code type %02X"
                                % (ln, name, ctype))
                continue
            reg = region_of(addr)
            if reg == "invalid":
                problems.append("line %d: [%s] target %08X outside main RAM"
                                % (ln, name, addr))
                continue
            op = {"kind": KIND[ctype], "addr": addr, "value": value,
                  "region": reg}
            if reg == "image" and ctype == WRITE16:
                wa = addr & ~3
                cur = word_at(wa)
                if cur is not None:
                    if addr & 2:
                        new = (cur & 0x0000FFFF) | (value << 16)
                    else:
                        new = (cur & 0xFFFF0000) | value
                    op["word_addr"] = wa
                    op["orig_word"] = cur
                    if (cur >> 26) != (new >> 26):
                        op["patch_was"] = disasm(cur, wa)
                        op["patch_now"] = disasm(new, wa)
            ops.append(op)
            stats[KIND[ctype]] += 1
            stats["region:" + reg] += 1

        if not ops:
            continue
        out.append({"id": cid, "name": name,
                    "category": categorise(name), "ops": ops})

    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# Generated by tools/gs_import.py from %s -- do not hand-edit.\n"
                % os.path.basename(args.input))
        f.write("# Regenerate:\n")
        f.write("#   python tools/gs_import.py %s -o %s\n#\n"
                % (args.input, args.out))
        f.write("# kind:   write8 / write16 are applied every frame while the\n")
        f.write("#         cheat is enabled; if_eq16 guards the NEXT op.\n")
        f.write("# region: low   = streamed data, only valid inside a level\n")
        f.write("#         image = inside the EXE (orig_word recorded to guard)\n")
        f.write("#         bss   = runtime state\n\n")
        for c in out:
            f.write("[[cheat]]\n")
            f.write('id = "%s"\n' % c["id"])
            f.write('name = "%s"\n' % c["name"].replace('"', "'"))
            f.write('category = "%s"\n' % c["category"])
            for op in c["ops"]:
                f.write("\n  [[cheat.op]]\n")
                f.write('  kind = "%s"\n' % op["kind"])
                f.write("  addr = 0x%08X\n" % op["addr"])
                f.write("  value = 0x%04X\n" % op["value"])
                f.write('  region = "%s"\n' % op["region"])
                if "orig_word" in op:
                    f.write("  word_addr = 0x%08X\n" % op["word_addr"])
                    f.write("  orig_word = 0x%08X\n" % op["orig_word"])
                if "patch_was" in op:
                    f.write('  patch_was = "%s"\n' % op["patch_was"])
                    f.write('  patch_now = "%s"\n' % op["patch_now"])
            f.write("\n")

    print("wrote %s" % args.out)
    print("  cheats: %d   ops: %d"
          % (len(out), sum(len(c["ops"]) for c in out)))
    kinds = dict((k, v) for k, v in sorted(stats.items())
                 if not k.startswith("region:"))
    regions = dict((k[7:], v) for k, v in sorted(stats.items())
                   if k.startswith("region:"))
    print("  by kind:   %s" % kinds)
    print("  by region: %s" % regions)
    if problems:
        print("")
        print("  %d problem(s) in the source list:" % len(problems))
        for p in problems:
            print("    - %s" % p)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
