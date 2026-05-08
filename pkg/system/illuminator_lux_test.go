package system_test

import (
	"os"
	"testing"

	"github.com/rmay/nuxvm/pkg/lux"
	"github.com/rmay/nuxvm/pkg/system"
)

func TestIlluminatorGridLoopStackBalance(t *testing.T) {
	origDir, _ := os.Getwd()
	t.Cleanup(func() { os.Chdir(origDir) })

	root := repoRoot(t)
	if err := os.Chdir(root); err != nil {
		t.Fatalf("chdir to repo root: %v", err)
	}

	source := `
INCLUDE "lib/core.lux"
( Mock dependencies )
@WIDTH_TABLE  0x1000 ; ( Space in reserved memory )
@GLYPH_DATA   0x2000 ;

@DRAW-ONE-GLYPH ( ptr cp -- )
    drop drop
;

@DRAW-GRID-LOOP ( ptr cp -- )
    dup 256 < [
        dup WIDTH_TABLE + load-byte ( ptr cp width )
        dup 0 > [
            7 + 8 / 8 * ( stride )
            2 PICK 2 PICK DRAW-ONE-GLYPH
            ROT + swap 1 +
            DRAW-GRID-LOOP
        ] [
            drop 1 +
            DRAW-GRID-LOOP
        ] ?:
    ] [
        drop drop
    ] ?:
;

( Setup: all chars have width 8, so stride is 8 )
0 [
    8 OVER WIDTH_TABLE + store-byte
    1 +
] 256 #: drop

GLYPH_DATA 0 DRAW-GRID-LOOP
`
	bytecode, err := lux.Compile(source)
	if err != nil {
		t.Fatalf("compile test: %v", err)
	}

	machine := system.NewMachine(bytecode, 16*1024*1024)
	if _, err := machine.Tick(); err != nil {
		t.Fatalf("run failed: %v", err)
	}

	stack := machine.CPU.Stack()
	if len(stack) != 0 {
		t.Errorf("expected empty stack, got %v", stack)
	}
}
