/* CBMC harness for one step of the NUX machine.
 *
 * Builds a completely nondeterministic VM -- every register, every stack
 * slot, every byte of memory and every byte of the program image is left
 * unconstrained except by the representation invariant of
 * docs/semantics.md section 2 -- runs exactly one vm_tick(), and checks the
 * proof obligations of section 12.
 *
 * Because the invariant is assumed before the step and asserted after it,
 * the result is inductive: it holds for one step, therefore for every step
 * of every run, for every program. That is the whole point of doing it this
 * way rather than by testing.
 *
 * The machine bounds are shrunk (see the Makefile in this directory) so the
 * solver sees a small model. The code under test is identical; only the
 * array sizes differ.
 */
#include "vm.h"
#include "opcodes.h"
#include <stddef.h>

#ifndef VERIFY_MEM_BYTES
#define VERIFY_MEM_BYTES 32
#endif

/* OUT would otherwise reach printf. The oracle's behaviour is not what is
 * being verified here, so it is a no-op that records nothing. */
static void out_stub(int32_t value, int32_t format) {
    (void)value;
    (void)format;
}

void harness(void) {
    VM vm;                          /* every field nondeterministic */
    uint8_t mem[VERIFY_MEM_BYTES];  /* nondeterministic contents */
    uint8_t before[VERIFY_MEM_BYTES];

    vm.memory = mem;
    vm.memory_size = VERIFY_MEM_BYTES;
    vm.bus = NULL;                  /* device reads trap; that is specified */
    vm.output_handler = out_stub;
    vm.trace = false;

    /* --- INV, section 2 -------------------------------------------- */
    __CPROVER_assume(vm.stack_ptr >= 0 && vm.stack_ptr <= MAX_STACK_SIZE);
    __CPROVER_assume(vm.return_stack_ptr >= 0 && vm.return_stack_ptr <= MAX_RETURN_STACK_SIZE);
    __CPROVER_assume(vm.loop_stack_ptr >= 0 && vm.loop_stack_ptr <= MAX_LOOP_STACK_SIZE);
    __CPROVER_assume(vm.fp >= -1 && vm.fp < MAX_LOCALS_SIZE);
    __CPROVER_assume(vm.image_base <= vm.image_end);
    __CPROVER_assume(vm.image_end <= VERIFY_MEM_BYTES);

    /* A running machine has not stopped for any reason. */
    __CPROVER_assume(vm.running);
    __CPROVER_assume(!vm.halted);
    __CPROVER_assume(vm.trap == TRAP_NONE);

    /* Property 4 in the pre-state, so preservation is what gets proved.
     * Note the bound is `pc <= ie`, not `pc < ie`: a 5-byte instruction at
     * the very end of the image leaves pc exactly at ie, and the machine is
     * still running. It traps on the NEXT fetch, which is precisely what the
     * fetch rule (section 5) is for. An earlier draft of this harness
     * asserted `pc < ie` and CBMC rejected it -- correctly. */
    __CPROVER_assume(vm.pc >= vm.image_base && vm.pc <= vm.image_end);
    __CPROVER_assume(vm.pc < vm.image_end);   /* about to execute, so strictly inside */

    for (unsigned i = 0; i < VERIFY_MEM_BYTES; i++) before[i] = mem[i];

    uint32_t ib = vm.image_base, ie = vm.image_end;

    /* --- one step --------------------------------------------------- */
    /* Properties 1 and 2 -- memory safety and freedom from undefined
     * behaviour -- are discharged by CBMC's built-in checks over this call
     * (--bounds-check, --pointer-check, --signed-overflow-check,
     * --undefined-shift-check, --div-by-zero-check, --conversion-check).
     * Nothing here has to state them; any violation inside vm_tick is a
     * counterexample. */
    vm_tick(&vm);

    /* --- Property 3: image immutability ----------------------------- */
    for (unsigned i = 0; i < VERIFY_MEM_BYTES; i++) {
        if (i >= ib && i < ie) {
            __CPROVER_assert(mem[i] == before[i],
                             "a step never modifies the program image");
        }
    }

    /* --- Property 5: the invariant is preserved --------------------- */
    __CPROVER_assert(vm.stack_ptr >= 0 && vm.stack_ptr <= MAX_STACK_SIZE,
                     "data stack pointer stays in range");
    __CPROVER_assert(vm.return_stack_ptr >= 0 && vm.return_stack_ptr <= MAX_RETURN_STACK_SIZE,
                     "return stack pointer stays in range");
    __CPROVER_assert(vm.loop_stack_ptr >= 0 && vm.loop_stack_ptr <= MAX_LOOP_STACK_SIZE,
                     "loop stack pointer stays in range");
    __CPROVER_assert(vm.fp >= -1 && vm.fp < MAX_LOCALS_SIZE,
                     "frame pointer stays in range");

    /* --- Property 4: execution confinement -------------------------- */
    __CPROVER_assert(!vm.running || (vm.pc >= ib && vm.pc <= ie),
                     "a running machine's PC stays within the image");

    /* --- Section 10: a running machine has not stopped -------------- */
    __CPROVER_assert(!vm.running || (vm.trap == TRAP_NONE && !vm.halted),
                     "running implies no trap and not halted");

    /* --- Section 8: stopping always records a reason ---------------- */
    __CPROVER_assert(vm.running || vm.halted || vm.trap != TRAP_NONE
                     || vm.last_opcode == OP_YIELD,
                     "a stopped machine has halted, yielded, or trapped");

    /* The image bounds and memory size are configuration, not state. */
    __CPROVER_assert(vm.image_base == ib && vm.image_end == ie
                     && vm.memory_size == VERIFY_MEM_BYTES,
                     "a step never changes the machine's configuration");
}
