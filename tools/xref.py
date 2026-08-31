#!/usr/bin/env python3
"""Static cross-referencer for the Twisted Metal 2 executable.

The game reaches most of its globals in one of three ways, and a naive
`lui`/offset search only finds the first:

  1. `lui $v0, 0x8016` + `lbu $v1, 0x4764($v0)`        -- direct
  2. `lui $s0, 0x8016` + `addiu $s0, $s0, 0x4700`      -- base in a register,
     then `lbu $v1, 0x64($s0)` much later                 indexed off it
  3. `lbu $v1, 816($gp)`                               -- $gp is 0x801809D4

So this walks every instruction keeping a small map of "which registers hold a
known constant", updated by `lui` / `addiu` / `ori` / `addu $x,$y,$zero`, and
cleared for any register written by something it cannot follow. That resolves
all three forms. It is deliberately optimistic across branches -- this is a
search tool, so a false positive costs one glance and a false negative costs
an afternoon.

Usage:
    python tools/xref.py addr 0x80164760 0x8016477F   # who touches this range
    python tools/xref.py imm 14                       # who compares against 14
    python tools/xref.py calls 0x800E3800             # who calls this function
    python tools/xref.py dis 0x8012CE7C 0x8012CF00    # disassemble
    python tools/xref.py func 0x8012CE7C              # disassemble one function

Add `--proto` to run against the Aug 1996 prototype instead of retail.
"""

import argparse
import os
import struct

RETAIL = dict(path="disc/SCUS_943.06", base=0x800CDC54, size=0x000B3800,
              gp=0x801809D4)
PROTO = dict(path="analysis/proto/SCUS_943.06", base=0x800CFA68, size=None,
             gp=None)

REGS = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]

OPS = {2: "j", 3: "jal", 4: "beq", 5: "bne", 6: "blez", 7: "bgtz", 8: "addi",
       9: "addiu", 10: "slti", 11: "sltiu", 12: "andi", 13: "ori", 14: "xori",
       15: "lui", 32: "lb", 33: "lh", 34: "lwl", 35: "lw", 36: "lbu", 37: "lhu",
       38: "lwr", 40: "sb", 41: "sh", 42: "swl", 43: "sw", 46: "swr"}
SPECIAL = {0: "sll", 2: "srl", 3: "sra", 4: "sllv", 6: "srlv", 7: "srav",
           8: "jr", 9: "jalr", 12: "syscall", 13: "break", 16: "mfhi",
           17: "mthi", 18: "mflo", 19: "mtlo", 24: "mult", 25: "multu",
           26: "div", 27: "divu", 32: "add", 33: "addu", 34: "sub", 35: "subu",
           36: "and", 37: "or", 38: "xor", 39: "nor", 42: "slt", 43: "sltu"}
MEM = {32: "lb", 33: "lh", 34: "lwl", 35: "lw", 36: "lbu", 37: "lhu",
       38: "lwr", 40: "sb", 41: "sh", 42: "swl", 43: "sw", 46: "swr"}
LOADS = set([32, 33, 34, 35, 36, 37, 38])


class Image(object):
    def __init__(self, cfg, root):
        path = os.path.join(root, cfg["path"])
        raw = open(path, "rb").read()
        hdr = struct.unpack("<IIII", raw[0x10:0x20])
        self.base = hdr[2] or cfg["base"]
        self.size = hdr[3] or (len(raw) - 2048)
        self.img = raw[2048:2048 + self.size]
        self.gp = cfg["gp"]
        self.words = [struct.unpack("<I", self.img[i:i + 4])[0]
                      for i in range(0, len(self.img) & ~3, 4)]

    def valid(self, addr):
        return self.base <= addr < self.base + len(self.words) * 4

    def word(self, addr):
        return self.words[(addr - self.base) // 4]

    def addr(self, index):
        return self.base + index * 4


def op(w):
    return w >> 26


def rs(w):
    return (w >> 21) & 31


def rt(w):
    return (w >> 16) & 31


def rd(w):
    return (w >> 11) & 31


def sa(w):
    return (w >> 6) & 31


def simm(w):
    v = w & 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def text(im, pc):
    """One instruction, as a string."""
    w = im.word(pc)
    if w == 0:
        return "nop"
    o = op(w)
    if o == 0:
        f = w & 0x3F
        n = SPECIAL.get(f, "sp.%02X" % f)
        if f == 8:
            return "jr    $%s" % REGS[rs(w)]
        if f == 9:
            return "jalr  $%s" % REGS[rs(w)]
        if f in (0, 2, 3):
            return "%-5s $%s, $%s, %d" % (n, REGS[rd(w)], REGS[rt(w)], sa(w))
        if f in (16, 18):
            return "%-5s $%s" % (n, REGS[rd(w)])
        if f in (24, 25, 26, 27):
            return "%-5s $%s, $%s" % (n, REGS[rs(w)], REGS[rt(w)])
        return "%-5s $%s, $%s, $%s" % (n, REGS[rd(w)], REGS[rs(w)], REGS[rt(w)])
    n = OPS.get(o, "op.%02X" % o)
    if o in (2, 3):
        return "%-5s 0x%08X" % (n, (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
    if o in (4, 5):
        return "%-5s $%s, $%s, 0x%08X" % (n, REGS[rs(w)], REGS[rt(w)],
                                          pc + 4 + simm(w) * 4)
    if o in (6, 7):
        return "%-5s $%s, 0x%08X" % (n, REGS[rs(w)], pc + 4 + simm(w) * 4)
    if o == 15:
        return "%-5s $%s, 0x%04X" % (n, REGS[rt(w)], w & 0xFFFF)
    if o in MEM:
        return "%-5s $%s, %d($%s)" % (n, REGS[rt(w)], simm(w), REGS[rs(w)])
    return "%-5s $%s, $%s, %d" % (n, REGS[rt(w)], REGS[rs(w)], simm(w))


def propagate(im, on_mem=None, on_const=None):
    """Walk the image tracking known register constants.

    Calls on_mem(pc, w, effective_address) for every load/store whose base
    register holds a known value, and on_const(pc, reg, value) whenever a
    register takes a newly computed constant.
    """
    known = [None] * 32
    known[0] = 0
    if im.gp is not None:
        known[28] = im.gp
    for i, w in enumerate(im.words):
        pc = im.addr(i)
        o = op(w)
        dest = None
        value = None

        if o == 15:                                   # lui
            dest, value = rt(w), (w & 0xFFFF) << 16
        elif o == 9 or o == 8:                        # addiu / addi
            dest = rt(w)
            if known[rs(w)] is not None:
                value = (known[rs(w)] + simm(w)) & 0xFFFFFFFF
        elif o == 13:                                 # ori
            dest = rt(w)
            if known[rs(w)] is not None:
                value = known[rs(w)] | (w & 0xFFFF)
        elif o in MEM:
            if known[rs(w)] is not None and on_mem:
                on_mem(pc, w, (known[rs(w)] + simm(w)) & 0xFFFFFFFF)
            if o in LOADS:
                dest = rt(w)                          # loaded value is unknown
        elif o == 0:
            f = w & 0x3F
            if f == 33 and rt(w) == 0:                # addu rd, rs, $zero
                dest = rd(w)
                value = known[rs(w)]
            elif f in (8, 12, 13):                    # jr / syscall / break
                dest = None
            elif f in (24, 25, 26, 27):               # mult/div write hi/lo
                dest = None
            else:
                dest = rd(w)
        elif o in (10, 11, 12, 14):                   # slti/sltiu/andi/xori
            dest = rt(w)
        elif o in (2, 3):                             # j / jal
            if o == 3:                                # a call clobbers caller-saves
                for r in list(range(1, 16)) + [24, 25, 31]:
                    known[r] = None
            continue
        elif o in (4, 5, 6, 7):
            continue

        if dest is not None and dest != 0:
            known[dest] = value
            if value is not None and on_const:
                on_const(pc, dest, value)
        known[0] = 0
        if im.gp is not None:
            known[28] = im.gp


def cmd_addr(im, args):
    lo = int(args.lo, 0)
    hi = int(args.hi, 0) if args.hi else lo + 3
    hits = []

    def on_mem(pc, w, eff):
        if lo <= eff <= hi:
            hits.append((pc, op(w), eff, REGS[rt(w)]))

    propagate(im, on_mem=on_mem)
    print("accesses to 0x%08X-0x%08X: %d" % (lo, hi, len(hits)))
    for pc, o, eff, reg in sorted(set(hits)):
        kind = "read" if o in LOADS else "WRITE"
        print("  %08X  %-5s %-4s %08X   $%s" % (pc, MEM[o], kind, eff, reg))


def cmd_ref(im, args):
    """Where does a register come to *hold* an address in this range?

    This is how string and table pointers are found -- they are materialized
    into a register and passed on, never loaded from.
    """
    lo = int(args.lo, 0)
    hi = int(args.hi, 0) if args.hi else lo + 3
    hits = []

    def on_const(pc, reg, value):
        if lo <= value <= hi:
            hits.append((pc, REGS[reg], value))

    propagate(im, on_const=on_const)
    print("pointers to 0x%08X-0x%08X: %d" % (lo, hi, len(hits)))
    for pc, reg, value in sorted(set(hits)):
        print("  %08X  $%-3s = %08X    %s" % (pc, reg, value, text(im, pc)))


def cmd_imm(im, args):
    want = int(args.value, 0)
    print("instructions with immediate %d (0x%X):" % (want, want))
    for i, w in enumerate(im.words):
        o = op(w)
        if o in (8, 9, 10, 11, 12, 13, 14) and (w & 0xFFFF) == (want & 0xFFFF):
            print("  %08X  %s" % (im.addr(i), text(im, im.addr(i))))


def cmd_calls(im, args):
    target = int(args.target, 0)
    print("jal 0x%08X:" % target)
    n = 0
    for i, w in enumerate(im.words):
        if op(w) == 3:
            pc = im.addr(i)
            if ((pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)) == target:
                print("  %08X" % pc)
                n += 1
    print("  %d call site(s)" % n)


def cmd_dis(im, args):
    start = int(args.start, 0)
    end = int(args.end, 0) if args.end else start + 0x80
    for pc in range(start, end, 4):
        if im.valid(pc):
            print("  %08X  %s" % (pc, text(im, pc)))


def cmd_func(im, args):
    """Disassemble from `start` to the jr $ra that ends the function."""
    pc = int(args.start, 0)
    while im.valid(pc):
        line = text(im, pc)
        print("  %08X  %s" % (pc, line))
        if line.startswith("jr    $ra"):
            print("  %08X  %s" % (pc + 4, text(im, pc + 4)))   # delay slot
            return
        pc += 4


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--proto", action="store_true", help="use the prototype EXE")
    sub = ap.add_subparsers(dest="cmd", required=True)

    a = sub.add_parser("addr", help="find accesses to an address range")
    a.add_argument("lo")
    a.add_argument("hi", nargs="?")

    r = sub.add_parser("ref", help="find where a register is given an address")
    r.add_argument("lo")
    r.add_argument("hi", nargs="?")

    b = sub.add_parser("imm", help="find instructions using an immediate")
    b.add_argument("value")

    c = sub.add_parser("calls", help="find call sites of a function")
    c.add_argument("target")

    d = sub.add_parser("dis", help="disassemble a range")
    d.add_argument("start")
    d.add_argument("end", nargs="?")

    e = sub.add_parser("func", help="disassemble until jr $ra")
    e.add_argument("start")

    args = ap.parse_args()
    im = Image(PROTO if args.proto else RETAIL, root)
    {"addr": cmd_addr, "ref": cmd_ref, "imm": cmd_imm, "calls": cmd_calls,
     "dis": cmd_dis, "func": cmd_func}[args.cmd](im, args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
