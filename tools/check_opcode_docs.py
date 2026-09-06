#!/usr/bin/env python3
"""Check docs/opcodes.md against include/opcodes.h and include/vm.h.

docs/opcodes.md drifted away from the implementation once already -- it
described FRAME, UNFRAME and LOCALGET in ways that were never true, claimed
PEEKR2 pushed two values when it pushes one, and had OUT's operands in the
wrong order. None of that was caught, because nothing checked it.

This does not verify the prose. It checks the things that are mechanically
checkable, which is enough to catch an opcode being renamed, renumbered,
resized, or documented in only one of the two places:

  * every opcode in opcodes.h has a detail section and a summary-table row
  * names and numbers agree in all three places
  * instruction lengths in the table are right (5 bytes iff the opcode
    carries an immediate, 1 otherwise)
  * no opcode is documented that does not exist
  * every trap named in the doc is a real NuxTrap member

Run by `make test`.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "include" / "opcodes.h"
VM_H = ROOT / "include" / "vm.h"
DOC = ROOT / "docs" / "opcodes.md"

# The seven opcodes that carry a 4-byte immediate, and so are 5 bytes long.
IMMEDIATE = {"PUSH", "JMP", "JZ", "JNZ", "CALL", "LOAD", "STORE"}


def main():
    header = HEADER.read_text()
    doc = DOC.read_text()

    real = {int(m.group(2), 16): m.group(1)
            for m in re.finditer(r"#define OP_(\w+)\s+(0x[0-9A-Fa-f]+)", header)}
    table = {int(m.group(1), 16): (m.group(2), int(m.group(3)))
             for m in re.finditer(r"^\|\s*(0x[0-9A-F]{2})\s*\|\s*(\w+)\s*\|\s*(\d+)\s*\|",
                                  doc, re.M)}
    detail = {int(m.group(1), 16): m.group(2)
              for m in re.finditer(r"^### (0x[0-9A-F]{2}) — (\w+)$", doc, re.M)}

    problems = []
    for op in sorted(real):
        name = real[op]
        if op not in detail:
            problems.append("%#04x %s has no detail section" % (op, name))
        elif detail[op] != name:
            problems.append("%#04x detail says %s, opcodes.h says %s"
                            % (op, detail[op], name))
        if op not in table:
            problems.append("%#04x %s is missing from the summary table" % (op, name))
            continue
        doc_name, doc_bytes = table[op]
        if doc_name != name:
            problems.append("%#04x table says %s, opcodes.h says %s" % (op, doc_name, name))
        want = 5 if name in IMMEDIATE else 1
        if doc_bytes != want:
            problems.append("%#04x %s: table says %d bytes, should be %d"
                            % (op, name, doc_bytes, want))

    for op in sorted(set(table) | set(detail)):
        if op not in real:
            problems.append("%#04x is documented but is not in opcodes.h" % op)

    # Trap names cited in the doc must exist in the C enumeration.
    enum = set(re.findall(r"TRAP_([A-Z0-9_]+)", VM_H.read_text())) - {"_COUNT"}
    known = {"".join(p.capitalize() for p in e.split("_")) for e in enum}
    cited = set(re.findall(r"`(?:Trap\()?([A-Z][a-z]+(?:[A-Z][a-z]+)+)\)?`", doc))
    cited -= {"NuxTrap"}   # the type itself, not one of its members
    for name in sorted(cited - known):
        problems.append("trap %r is named in the doc but is not a NuxTrap member" % name)

    if problems:
        print("docs/opcodes.md disagrees with the headers:")
        for p in problems:
            print("  " + p)
        return 1

    print("  opcode docs: %d opcodes consistent with opcodes.h and vm.h" % len(real))
    return 0


if __name__ == "__main__":
    sys.exit(main())
