#!/usr/bin/env python3
"""A reference NUX interpreter, written from docs/semantics.md.

This is deliberately NOT a translation of src/vm.c. It is a second,
independent implementation derived from the specification, so that
tools/difftest.py comparing the two actually tests something: if the C and
this agree on millions of random programs, the specification, the C and this
model are probably all saying the same thing. Where they disagree, exactly
one of the three is wrong, and the trace says which step to look at.

Section numbers in comments refer to docs/semantics.md.
"""

import re
import sys
from pathlib import Path

# --- Trap codes (section 8) -----------------------------------------------
# Parsed out of the C header rather than duplicated, so the two enumerations
# cannot drift apart silently.

def load_traps(header=None):
    if header is None:
        header = Path(__file__).resolve().parent.parent / "include" / "vm.h"
    text = Path(header).read_text()
    body = re.search(r"typedef enum \{(.*?)\} NuxTrap;", text, re.S).group(1)
    names = [m.group(1) for m in re.finditer(r"^\s*(TRAP_[A-Z0-9_]+)", body, re.M)]
    names = [n for n in names if n != "TRAP__COUNT"]
    return {n: i for i, n in enumerate(names)}

TRAP = load_traps()

# --- Opcodes --------------------------------------------------------------

OPS = {
    "PUSH": 0x00, "POP": 0x01, "DUP": 0x02, "SWAP": 0x03, "OVER": 0x04,
    "ROT": 0x05, "PICK": 0x06, "ROLL": 0x07,
    "ADD": 0x08, "SUB": 0x09, "MUL": 0x0A, "DIV": 0x0B, "MOD": 0x0C,
    "INC": 0x0D, "DEC": 0x0E, "NEG": 0x0F, "ABS": 0x10, "DIVMOD": 0x11,
    "MIN": 0x12, "MAX": 0x13,
    "AND": 0x14, "OR": 0x15, "XOR": 0x16, "NOT": 0x17,
    "SHL": 0x18, "SHR": 0x19, "SAR": 0x1A,
    "EQ": 0x1B, "NEQ": 0x1C, "LT": 0x1D, "LTE": 0x1E, "GT": 0x1F, "GTE": 0x20,
    "JMP": 0x21, "JZ": 0x22, "JNZ": 0x23, "CALL": 0x24, "RET": 0x25,
    "CALLSTACK": 0x26, "JMPSTACK": 0x27,
    "LOAD": 0x28, "STORE": 0x29, "LOADI": 0x2A, "STOREI": 0x2B,
    "PUSHR": 0x2C, "POPR": 0x2D, "PEEKR": 0x2E, "PEEKR2": 0x2F,
    "FRAME": 0x30, "UNFRAME": 0x31, "LOCALGET": 0x32, "LOCALSET": 0x33,
    "OUT": 0x34, "HALT": 0x35, "YIELD": 0x36,
}
NAME = {v: k for k, v in OPS.items()}
MAX_OPCODE = 0x36

MAX_STACK = 8192
MAX_RETURN_STACK = 1024
MAX_LOOP_STACK = 1024
MAX_LOCALS = 4096

DEVICE_BASE = 0x010000
DEVICE_END = 0x011000

INT32_MIN = -(2 ** 31)
INT32_MAX = 2 ** 31 - 1


# --- Section 1: domains ---------------------------------------------------

def wrap(n):
    """The unique value in Int32 congruent to n modulo 2^32."""
    return ((n + 2 ** 31) % 2 ** 32) - 2 ** 31


def u32(a):
    """The Word32 reading of an Int32."""
    return a % 2 ** 32


def quo(a, b):
    """Truncating division; INT32_MIN / -1 wraps (section 9)."""
    if a == INT32_MIN and b == -1:
        return INT32_MIN
    q = abs(a) // abs(b)
    return -q if (a < 0) != (b < 0) else q


def rem(a, b):
    """Remainder takes the sign of the dividend (section 9)."""
    if a == INT32_MIN and b == -1:
        return 0
    return a - b * quo(a, b)


class Trapped(Exception):
    """Raised the moment a rule's guard fails. Because nothing is committed
    before every guard has passed (section 7, fault atomicity), unwinding to
    the top of the step is enough to leave the state untouched."""

    def __init__(self, code):
        super().__init__(code)
        self.code = code


class Machine:
    def __init__(self, program, base=0, memory_size=4096):
        if memory_size < base + len(program):
            memory_size = base + len(program)
        # Section 11: initial state.
        self.msize = memory_size
        self.M = bytearray(memory_size)
        self.M[base:base + len(program)] = program
        self.ib = base
        self.ie = base + len(program)
        self.pc = base
        self.D = []
        self.R = []
        self.K = []
        self.L = [0] * MAX_LOCALS
        self.fp = -1
        self.run = True
        self.halt = False
        self.trap = TRAP["TRAP_NONE"]
        self.last = 0
        self.outputs = []
        # Hashing all of M on every step dominates the run time, and M
        # changes only on a store, so cache it.
        self._m_hash = None
        self._l_hash = None

    def locals_hash(self):
        if self._l_hash is None:
            self._l_hash = fnv_i32s(self.L)
        return self._l_hash

    def memory_hash(self):
        if self._m_hash is None:
            self._m_hash = fnv_bytes(self.M)
        return self._m_hash

    # --- Section 7: guards ------------------------------------------------

    def need(self, n):
        if len(self.D) < n:
            raise Trapped(TRAP["TRAP_STACK_UNDERFLOW"])

    def room(self, n):
        if len(self.D) > MAX_STACK - n:
            raise Trapped(TRAP["TRAP_STACK_OVERFLOW"])

    # --- Section 4: memory ------------------------------------------------

    def in_range(self, x):
        return x < self.msize and self.msize - x >= 4

    def read(self, x):
        if x & 3:
            raise Trapped(TRAP["TRAP_UNALIGNED_READ"])
        if DEVICE_BASE <= x < DEVICE_END:
            # No bus is attached in a traced run (see src/nuxstep.c).
            raise Trapped(TRAP["TRAP_NO_BUS"])
        if not self.in_range(x):
            raise Trapped(TRAP["TRAP_READ_OUT_OF_BOUNDS"])
        return wrap(int.from_bytes(self.M[x:x + 4], "big"))

    def write(self, x, v):
        if x & 3:
            raise Trapped(TRAP["TRAP_UNALIGNED_WRITE"])
        if not self.in_range(x):
            raise Trapped(TRAP["TRAP_WRITE_OUT_OF_BOUNDS"])
        if x < self.ie and x + 4 > self.ib:
            raise Trapped(TRAP["TRAP_WRITE_INTO_IMAGE"])
        self.M[x:x + 4] = u32(v).to_bytes(4, "big")
        self._m_hash = None

    # --- Sections 5 and 6: fetch and immediates ---------------------------

    def in_image(self, x):
        return self.ib <= x < self.ie

    def imm(self):
        if not self.in_image(self.pc) or self.ie - self.pc < 4:
            raise Trapped(TRAP["TRAP_TRUNCATED_IMMEDIATE"])
        v = int.from_bytes(self.M[self.pc:self.pc + 4], "big")
        self.pc += 4
        return v

    def target(self, t):
        if not self.in_image(t):
            raise Trapped(TRAP["TRAP_JUMP_OUTSIDE_IMAGE"])
        return t

    # --- Section 9: the step relation -------------------------------------

    def step(self):
        if not self.run:
            return False
        if not self.in_image(self.pc):
            self.fault(TRAP["TRAP_EXEC_OUTSIDE_IMAGE"])
            return False
        if self.pc >= self.msize:
            self.fault(TRAP["TRAP_PC_OUT_OF_BOUNDS"])
            return False

        op = self.M[self.pc]
        self.pc += 1
        self.last = op
        try:
            self.exec_op(op)
        except Trapped as t:
            self.fault(t.code)
        return self.run

    def fault(self, code):
        if self.trap == TRAP["TRAP_NONE"]:
            self.trap = code
        self.run = False

    def exec_op(self, op):
        D, name = self.D, NAME.get(op)
        if name is None:
            raise Trapped(TRAP["TRAP_UNKNOWN_OPCODE"])

        # -- stack manipulation --
        if name == "PUSH":
            self.room(1)
            D.append(wrap(self.imm()))
        elif name == "POP":
            self.need(1); D.pop()
        elif name == "DUP":
            self.need(1); self.room(1); D.append(D[-1])
        elif name == "SWAP":
            self.need(2); D[-1], D[-2] = D[-2], D[-1]
        elif name == "OVER":
            self.need(2); self.room(1); D.append(D[-2])
        elif name == "ROT":
            self.need(3)
            a, b, c = D[-3], D[-2], D[-1]
            D[-3], D[-2], D[-1] = b, c, a
        elif name == "PICK":
            self.need(1)
            n = D[-1]
            depth = len(D) - 1
            if not (0 <= n < depth):
                raise Trapped(TRAP["TRAP_PICK_RANGE"])
            D[-1] = D[depth - 1 - n]
        elif name == "ROLL":
            self.need(1)
            n = D[-1]
            depth = len(D) - 1
            if not (0 <= n < depth):
                raise Trapped(TRAP["TRAP_ROLL_RANGE"])
            D.pop()
            if n > 0:
                D.append(D.pop(len(D) - 1 - n))

        # -- arithmetic --
        elif name in ("ADD", "SUB", "MUL", "MIN", "MAX", "AND", "OR", "XOR",
                      "SHL", "SHR", "SAR", "EQ", "NEQ", "LT", "LTE", "GT", "GTE"):
            self.need(2)
            b = D.pop(); a = D.pop()
            D.append(self.binop(name, a, b))
        elif name in ("DIV", "MOD"):
            self.need(2)
            a, b = D[-2], D[-1]
            if b == 0:
                raise Trapped(TRAP["TRAP_DIVIDE_BY_ZERO"])
            D.pop(); D.pop()
            D.append(quo(a, b) if name == "DIV" else rem(a, b))
        elif name == "DIVMOD":
            self.need(2)
            a, b = D[-2], D[-1]
            if b == 0:
                raise Trapped(TRAP["TRAP_DIVIDE_BY_ZERO"])
            D[-2], D[-1] = quo(a, b), rem(a, b)
        elif name == "INC":
            self.need(1); D[-1] = wrap(D[-1] + 1)
        elif name == "DEC":
            self.need(1); D[-1] = wrap(D[-1] - 1)
        elif name == "NEG":
            self.need(1); D[-1] = wrap(-D[-1])
        elif name == "ABS":
            self.need(1); D[-1] = wrap(-D[-1]) if D[-1] < 0 else D[-1]
        elif name == "NOT":
            self.need(1); D[-1] = wrap(~D[-1])

        # -- control flow --
        elif name == "JMP":
            self.pc = self.target(self.imm())
        elif name in ("JZ", "JNZ"):
            # The target is checked before the condition is popped, and that
            # ordering is normative (section 9).
            t = self.target(self.imm())
            self.need(1)
            c = D.pop()
            if (c == 0) if name == "JZ" else (c != 0):
                self.pc = t
        elif name == "CALL":
            t = self.imm()
            if len(self.R) >= MAX_RETURN_STACK:
                raise Trapped(TRAP["TRAP_RETURN_STACK_OVERFLOW"])
            self.target(t)
            self.R.append(self.pc)
            self.pc = t
        elif name == "RET":
            if not self.R:
                raise Trapped(TRAP["TRAP_RETURN_STACK_UNDERFLOW"])
            t = self.target(self.R[-1])
            self.R.pop()
            self.pc = t
        elif name == "CALLSTACK":
            self.need(1)
            t = u32(D[-1])
            if len(self.R) >= MAX_RETURN_STACK:
                raise Trapped(TRAP["TRAP_RETURN_STACK_OVERFLOW"])
            self.target(t)
            D.pop()
            self.R.append(self.pc)
            self.pc = t
        elif name == "JMPSTACK":
            self.need(1)
            t = self.target(u32(D[-1]))
            D.pop()
            self.pc = t

        # -- memory --
        elif name == "LOAD":
            self.room(1)
            D.append(self.read(self.imm()))
        elif name == "STORE":
            x = self.imm()
            self.need(1)
            self.write(x, D[-1])
            D.pop()
        elif name == "LOADI":
            self.need(1)
            D[-1] = self.read(u32(D[-1]))
        elif name == "STOREI":
            self.need(2)
            self.write(u32(D[-1]), D[-2])
            D.pop(); D.pop()

        # -- loop stack --
        elif name == "PUSHR":
            self.need(1)
            if len(self.K) >= MAX_LOOP_STACK:
                raise Trapped(TRAP["TRAP_LOOP_STACK_OVERFLOW"])
            self.K.append(D.pop())
        elif name == "POPR":
            if not self.K:
                raise Trapped(TRAP["TRAP_LOOP_STACK_UNDERFLOW"])
            self.room(1); D.append(self.K.pop())
        elif name == "PEEKR":
            if not self.K:
                raise Trapped(TRAP["TRAP_LOOP_STACK_UNDERFLOW"])
            self.room(1); D.append(self.K[-1])
        elif name == "PEEKR2":
            if len(self.K) < 2:
                raise Trapped(TRAP["TRAP_LOOP_STACK_UNDERFLOW"])
            self.room(1); D.append(self.K[-2])

        # -- frames and locals --
        elif name == "FRAME":
            self.need(1)
            n = D[-1]
            # Both ends of the index are bounded; see docs/semantics.md.
            if n < 0 or self.fp + 1 < 0 or self.fp + 1 + n >= MAX_LOCALS:
                raise Trapped(TRAP["TRAP_FRAME_RANGE"])
            if len(D) - 1 < n:
                raise Trapped(TRAP["TRAP_STACK_UNDERFLOW"])
            old_fp = self.fp
            base = old_fp + 1
            D.pop()
            self.L[base] = old_fp
            for i in range(n):
                self.L[base + n - i] = D.pop()
            self._l_hash = None
            self.fp = base + n
        elif name == "UNFRAME":
            self.need(1)
            n = D[-1]
            if n < 0 or not (0 <= self.fp - n < MAX_LOCALS):
                raise Trapped(TRAP["TRAP_FRAME_RANGE"])
            # The saved-FP slot is an ordinary local and a guest can
            # overwrite it, so what comes back must be validated.
            restored = self.L[self.fp - n]
            if restored < -1 or restored >= MAX_LOCALS:
                raise Trapped(TRAP["TRAP_FRAME_RANGE"])
            D.pop()
            self.fp = restored
        elif name == "LOCALGET":
            self.need(1)
            idx = self.fp - D[-1]
            if not (0 <= idx < MAX_LOCALS):
                raise Trapped(TRAP["TRAP_LOCAL_RANGE"])
            D[-1] = self.L[idx]
        elif name == "LOCALSET":
            self.need(2)
            idx = self.fp - D[-1]
            if not (0 <= idx < MAX_LOCALS):
                raise Trapped(TRAP["TRAP_LOCAL_RANGE"])
            self.L[idx] = D[-2]
            self._l_hash = None
            D.pop(); D.pop()

        # -- I/O and termination --
        elif name == "OUT":
            self.need(2)
            v, f = D[-2], D[-1]
            D.pop(); D.pop()
            self.outputs.append((v, f))
        elif name == "HALT":
            self.run = False
            self.halt = True
        elif name == "YIELD":
            self.run = False
        else:
            raise AssertionError("unhandled opcode " + name)

    @staticmethod
    def binop(name, a, b):
        if name == "ADD": return wrap(a + b)
        if name == "SUB": return wrap(a - b)
        if name == "MUL": return wrap(a * b)
        if name == "MIN": return min(a, b)
        if name == "MAX": return max(a, b)
        if name == "AND": return wrap(u32(a) & u32(b))
        if name == "OR":  return wrap(u32(a) | u32(b))
        if name == "XOR": return wrap(u32(a) ^ u32(b))
        # Shift counts are reduced modulo 32, which is total for negative b.
        if name == "SHL": return wrap(u32(a) << (b % 32))
        if name == "SHR": return wrap(u32(a) >> (b % 32))
        if name == "SAR": return wrap(a >> (b % 32))   # floor division
        if name == "EQ":  return 1 if a == b else 0
        if name == "NEQ": return 1 if a != b else 0
        if name == "LT":  return 1 if a < b else 0
        if name == "LTE": return 1 if a <= b else 0
        if name == "GT":  return 1 if a > b else 0
        if name == "GTE": return 1 if a >= b else 0
        raise AssertionError(name)


# --- Trace format, matching src/nuxstep.c ---------------------------------

FNV_BASIS = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1


def fnv_bytes(data, h=FNV_BASIS):
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & MASK64
    return h


def fnv_i32s(values, h=FNV_BASIS):
    for v in values:
        h = fnv_bytes(u32(v).to_bytes(4, "little"), h)
    return h


def state_line(m, step):
    out_digest = FNV_BASIS
    for v, f in m.outputs:
        out_digest = fnv_i32s([v, f], out_digest)
    return (
        "STEP %d pc=%d run=%d halt=%d trap=%d last=%d sp=%d fp=%d rsp=%d ksp=%d"
        " D=%s R=%s K=%s L=%d M=%d OUT=%d/%d"
        % (step, m.pc, 1 if m.run else 0, 1 if m.halt else 0, m.trap, m.last,
           len(m.D), m.fp, len(m.R), len(m.K),
           ",".join(str(x) for x in m.D),
           ",".join(str(x) for x in m.R),
           ",".join(str(x) for x in m.K),
           m.locals_hash(), m.memory_hash(), len(m.outputs), out_digest)
    )


def trace(program, base=0, memory_size=4096, max_steps=200):
    m = Machine(program, base, memory_size)
    lines = [state_line(m, 0)]
    for s in range(1, max_steps + 1):
        if not m.run:
            break
        m.step()
        lines.append(state_line(m, s))
    return lines


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", type=lambda x: int(x, 0), default=0)
    ap.add_argument("--mem", type=lambda x: int(x, 0), default=4096)
    ap.add_argument("--steps", type=int, default=200)
    args = ap.parse_args()
    hexdata = re.sub(r"[^0-9a-fA-F]", "", sys.stdin.read())
    if len(hexdata) % 2:
        hexdata = hexdata[:-1]
    program = bytes.fromhex(hexdata)
    for line in trace(program, args.base, args.mem, args.steps):
        print(line)


if __name__ == "__main__":
    main()
