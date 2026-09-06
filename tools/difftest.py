#!/usr/bin/env python3
"""Differential fuzzer: src/vm.c against tools/nuxref.py.

Generates random NUX programs, traces each on both interpreters, and
compares the two traces step by step. A mismatch means the C interpreter,
the Python model, or docs/semantics.md is wrong -- the report names the
first step that diverged and both state lines, which is usually enough to
tell which.

  tools/difftest.py                     # 2000 programs, fixed seed
  tools/difftest.py --count 100000      # a longer soak
  tools/difftest.py --seed 7 --verbose
"""

import argparse
import random
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nuxref  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
NUXSTEP = ROOT / "bin" / "nuxstep"

IMMEDIATE_OPS = {0x00, 0x21, 0x22, 0x23, 0x24, 0x28, 0x29}  # PUSH JMP JZ JNZ CALL LOAD STORE
MAX_OPCODE = 0x36

INTERESTING = [
    0, 1, -1, 2, -2, 3, 4, 7, 8, 31, 32, 33, 64, 255, 256,
    -31, -32, -33, 0x7FFFFFFF, -0x80000000, 0x40000000, -0x40000000,
    0xFFFC, 0x10000, 0x100D0, 0xFFFFFFFC,
]


def gen_program(rng, memsize, base):
    """Random bytecode, biased toward programs that get somewhere.

    Uniformly random bytes almost always trap on the first instruction,
    which tests one rule very thoroughly and the other 54 not at all. Three
    biases fix that:

      * PUSH is heavily overweighted, so the stack is rarely empty and the
        arithmetic and stack opcodes have operands to consume;
      * jump and call immediates are drawn from the instruction boundaries
        recorded while generating, so control flow lands on real
        instructions and backward jumps make actual loops;
      * memory immediates are mostly word-aligned and in range.

    The remaining share of genuinely random bytes keeps the trap rules and
    the unknown-opcode rule covered.
    """
    # PUSH is 0x00; weight it far above the rest.
    ops = [0x00] * 10 + list(range(0, MAX_OPCODE + 1))

    body = bytearray()
    boundaries = []          # offsets at which an instruction starts

    def emit_push(value):
        boundaries.append(len(body))
        body.append(0x00)
        body.extend((value % 2 ** 32).to_bytes(4, "big"))

    # Seed the data stack.
    for _ in range(rng.randint(4, 14)):
        emit_push(rng.choice(INTERESTING))

    n = rng.randint(4, 45)
    for _ in range(n):
        boundaries.append(len(body))
        if rng.random() < 0.03:
            op = rng.randrange(256)          # may be an unknown opcode
        else:
            op = rng.choice(ops)
        body.append(op)
        if op not in IMMEDIATE_OPS:
            continue

        k = rng.random()
        if op in (0x21, 0x22, 0x23, 0x24):   # JMP JZ JNZ CALL: aim at code
            if k < 0.75 and boundaries:
                v = base + rng.choice(boundaries)
            elif k < 0.9:
                v = base + rng.randrange(0, len(body) + 8)
            else:
                v = rng.randrange(0, 2 ** 32)
        elif op in (0x28, 0x29):             # LOAD STORE: aim at data
            if k < 0.7:
                v = rng.randrange(0, memsize // 4) * 4
            elif k < 0.85:
                v = rng.choice(INTERESTING)
            else:
                v = rng.randrange(0, 2 ** 32)
        else:                                # PUSH
            v = rng.choice(INTERESTING) if k < 0.6 else rng.randrange(0, 2 ** 32)
        body.extend((v % 2 ** 32).to_bytes(4, "big"))

    return bytes(body)



def be(v):
    return (v % 2 ** 32).to_bytes(4, "big")


def directed_programs():
    """Programs aimed at states random generation almost never reaches.

    The three overflow traps need thousands of iterations of a tight loop,
    which a random walk will not stumble into, so they are constructed. Each
    entry is (name, image, memory_size, steps).
    """
    PUSH, JMP, CALL, PUSHR = 0x00, 0x21, 0x24, 0x2C
    FRAME, LOCALSET, LOCALGET, HALT = 0x30, 0x33, 0x32, 0x35
    DUP, ADD, STOREI, LOADI = 0x02, 0x08, 0x2B, 0x2A

    progs = []

    # Data stack overflow: push forever (8192 slots).
    progs.append(("stack-overflow",
                  bytes([PUSH]) + be(1) + bytes([JMP]) + be(0), 256, 20000))

    # Return stack overflow: recurse forever (1024 frames).
    progs.append(("return-stack-overflow",
                  bytes([CALL]) + be(0), 256, 4000))

    # Loop stack overflow: move a value to K forever (1024 slots).
    progs.append(("loop-stack-overflow",
                  bytes([PUSH]) + be(7) + bytes([PUSHR]) + bytes([JMP]) + be(0),
                  256, 8000))

    # Deep frames: nest FRAME until the locals array runs out.
    progs.append(("frame-exhaustion",
                  bytes([PUSH]) + be(1) + bytes([PUSH]) + be(1)
                  + bytes([FRAME]) + bytes([JMP]) + be(0), 256, 20000))

    # A long arithmetic chain that stays in range: exercises wrapping at the
    # 32-bit boundary over and over.
    progs.append(("arith-wrap",
                  bytes([PUSH]) + be(0x7FFFFFFF)
                  + bytes([DUP]) + bytes([ADD]) + bytes([JMP]) + be(5),
                  256, 5000))

    # Memory traffic: store and load back across the whole address space.
    body = bytearray()
    body += bytes([PUSH]) + be(0x11111111)
    body += bytes([PUSH]) + be(64)
    body += bytes([STOREI])
    body += bytes([PUSH]) + be(64)
    body += bytes([LOADI])
    body += bytes([JMP]) + be(0)
    progs.append(("memory-traffic", bytes(body), 256, 5000))

    # Locals traffic: set and get a local repeatedly inside a real frame.
    body = bytearray()
    body += bytes([PUSH]) + be(5)
    body += bytes([PUSH]) + be(1)
    body += bytes([FRAME])
    body += bytes([PUSH]) + be(9) + bytes([PUSH]) + be(0) + bytes([LOCALSET])
    body += bytes([PUSH]) + be(0) + bytes([LOCALGET])
    body += bytes([HALT])
    progs.append(("locals-traffic", bytes(body), 256, 200))

    return progs


def c_trace(program, base, memsize, steps):
    out = subprocess.run(
        [str(NUXSTEP), "--base", str(base), "--mem", str(memsize), "--steps", str(steps)],
        input=program.hex().encode(), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        check=True,
    )
    return out.stdout.decode().splitlines()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--steps", type=int, default=120)
    ap.add_argument("--mem", type=lambda x: int(x, 0), default=4096)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not NUXSTEP.exists():
        sys.exit("bin/nuxstep not built -- run `make bin/nuxstep` first")

    rng = random.Random(args.seed)
    total_steps = 0
    failures = 0

    # Directed phase first: the constructed cases are the ones that reach the
    # overflow traps, and a failure there is worth seeing before the soak.
    for name, program, mem, steps in directed_programs():
        want = c_trace(program, 0, mem, steps)
        got = nuxref.trace(program, 0, mem, steps)
        total_steps += len(got)
        if want != got:
            failures += 1
            print("=" * 72)
            print("DIVERGENCE in directed program %r" % name)
            print("  image: %s" % program.hex())
            for k in range(max(len(want), len(got))):
                w = want[k] if k < len(want) else "<no step>"
                g = got[k] if k < len(got) else "<no step>"
                if w != g:
                    print("  first difference at step %d:" % k)
                    print("    C      : %s" % w)
                    print("    ref    : %s" % g)
                    break
        elif args.verbose:
            d = dict(kv.split("=", 1) for kv in got[-1].split() if "=" in kv)
            print("ok directed %-24s %6d steps, trap=%s" % (name, len(got), d["trap"]))

    for i in range(args.count):
        base = rng.choice([0, 0, 0, 4, 0x1000])
        program = gen_program(rng, args.mem, base)
        try:
            want = c_trace(program, base, args.mem, args.steps)
        except subprocess.CalledProcessError as e:
            print("C harness crashed on program %d: %s" % (i, program.hex()))
            print("  exit status %s" % e.returncode)
            failures += 1
            continue
        got = nuxref.trace(program, base, args.mem, args.steps)
        total_steps += len(got)

        if want != got:
            failures += 1
            print("=" * 72)
            print("DIVERGENCE on program %d (seed=%d, base=%d, mem=%d)"
                  % (i, args.seed, base, args.mem))
            print("  image: %s" % program.hex())
            for k in range(max(len(want), len(got))):
                w = want[k] if k < len(want) else "<no step>"
                g = got[k] if k < len(got) else "<no step>"
                if w != g:
                    print("  first difference at step %d:" % k)
                    print("    C      : %s" % w)
                    print("    ref    : %s" % g)
                    break
            if failures >= 10:
                print("stopping after 10 divergences")
                break
        elif args.verbose:
            print("ok %d (%d steps)" % (i, len(got)))

    print("-" * 72)
    print("%d programs, %d steps compared, %d divergences"
          % (args.count, total_steps, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
