package vm

import (
	"encoding/binary"
	"fmt"
)

// Opcode constants — 32 original opcodes (0x00–0x1F) + new opcodes (0x20+).
const (
	OpPush      = 0x00
	OpPop       = 0x01
	OpDup       = 0x02
	OpSwap      = 0x03
	OpRoll      = 0x04
	OpRot       = 0x05
	OpAdd       = 0x06
	OpSub       = 0x07
	OpMul       = 0x08
	OpDiv       = 0x09
	OpMod       = 0x0A
	OpInc       = 0x0B
	OpDec       = 0x0C
	OpAnd       = 0x0D
	OpOr        = 0x0E
	OpXor       = 0x0F
	OpNot       = 0x10
	OpShl       = 0x11
	OpEq        = 0x12
	OpLt        = 0x13
	OpCallStack = 0x14
	OpJmp       = 0x15
	OpJz        = 0x16
	OpCall      = 0x17
	OpRet       = 0x18
	OpLoad      = 0x19
	OpStore     = 0x1A
	OpOut       = 0x1B
	OpHalt      = 0x1C
	OpYield     = 0x1D // Yield to host; triggers YieldHandler if set
	OpLoadI     = 0x1E // Pop addr from stack, push memory[addr]
	OpStoreI    = 0x1F // Pop addr from stack, pop value, store value at addr
	// Tier 1 additions (0x20–0x23)
	OpShr       = 0x20 // Logical right shift: [a, b] → [a >>> (b%32)]
	OpSar       = 0x21 // Arithmetic right shift: [a, b] → [a >> (b%32)] with sign extension
	OpJnz       = 0x22 // Jump if non-zero: [cond] → [], jump if cond != 0
	OpNeg       = 0x23 // Negate: [a] → [-a]
	// Tier 2 additions (0x24–0x28)
	OpGt        = 0x24 // Greater than: [a, b] → [a > b ? 1 : 0]
	OpNeq       = 0x25 // Not equal: [a, b] → [a != b ? 1 : 0]
	OpLte       = 0x26 // Less than or equal: [a, b] → [a <= b ? 1 : 0]
	OpGte       = 0x27 // Greater than or equal: [a, b] → [a >= b ? 1 : 0]
	OpPick      = 0x28 // Pick: [... n] → [... stack[n]]; copies nth element (0=top) to top
	// Tier 3 additions (0x29–0x2C)
	OpDivmod    = 0x29 // Divide with modulus: [a, b] → [a/b, a%b]
	OpAbs       = 0x2A // Absolute value: [a] → [|a|]
	OpMin       = 0x2B // Minimum: [a, b] → [min(a, b)]
	OpMax       = 0x2C // Maximum: [a, b] → [max(a, b)]
)

// OpcodeName returns the human-readable name for an opcode.
func OpcodeName(op byte) string {
	switch op {
	case OpPush:
		return "PUSH"
	case OpPop:
		return "POP"
	case OpDup:
		return "DUP"
	case OpSwap:
		return "SWAP"
	case OpRoll:
		return "ROLL"
	case OpRot:
		return "ROT"
	case OpAdd:
		return "ADD"
	case OpSub:
		return "SUB"
	case OpMul:
		return "MUL"
	case OpDiv:
		return "DIV"
	case OpMod:
		return "MOD"
	case OpInc:
		return "INC"
	case OpDec:
		return "DEC"
	case OpAnd:
		return "AND"
	case OpOr:
		return "OR"
	case OpXor:
		return "XOR"
	case OpNot:
		return "NOT"
	case OpShl:
		return "SHL"
	case OpEq:
		return "EQ"
	case OpLt:
		return "LT"
	case OpCallStack:
		return "CALLSTACK"
	case OpJmp:
		return "JMP"
	case OpJz:
		return "JZ"
	case OpCall:
		return "CALL"
	case OpRet:
		return "RET"
	case OpLoad:
		return "LOAD"
	case OpStore:
		return "STORE"
	case OpOut:
		return "OUT"
	case OpHalt:
		return "HALT"
	case OpYield:
		return "YIELD"
	case OpLoadI:
		return "LOADI"
	case OpStoreI:
		return "STOREI"
	case OpShr:
		return "SHR"
	case OpSar:
		return "SAR"
	case OpJnz:
		return "JNZ"
	case OpNeg:
		return "NEG"
	case OpGt:
		return "GT"
	case OpNeq:
		return "NEQ"
	case OpLte:
		return "LTE"
	case OpGte:
		return "GTE"
	case OpPick:
		return "PICK"
	case OpDivmod:
		return "DIVMOD"
	case OpAbs:
		return "ABS"
	case OpMin:
		return "MIN"
	case OpMax:
		return "MAX"
	default:
		return fmt.Sprintf("UNKNOWN(0x%02X)", op)
	}
}

// Helper functions for building programs

// EncodeInt32 encodes a 32-bit integer as big-endian bytes.
func EncodeInt32(value int32) []byte {
	buf := make([]byte, 4)
	binary.BigEndian.PutUint32(buf, uint32(value))
	return buf
}

// PushInstruction creates a PUSH instruction with the given value.
func PushInstruction(value int32) []byte {
	return append([]byte{OpPush}, EncodeInt32(value)...)
}

// JmpInstruction creates a JMP instruction to the given address.
func JmpInstruction(addr int32) []byte {
	return append([]byte{OpJmp}, EncodeInt32(addr)...)
}

// JzInstruction creates a JZ instruction to the given address.
func JzInstruction(addr int32) []byte {
	return append([]byte{OpJz}, EncodeInt32(addr)...)
}

// CallInstruction creates a CALL instruction to the given address.
func CallInstruction(addr int32) []byte {
	return append([]byte{OpCall}, EncodeInt32(addr)...)
}

// LoadInstruction creates a LOAD instruction from the given address.
func LoadInstruction(addr int32) []byte {
	return append([]byte{OpLoad}, EncodeInt32(addr)...)
}

// StoreInstruction creates a STORE instruction to the given address.
func StoreInstruction(addr int32) []byte {
	return append([]byte{OpStore}, EncodeInt32(addr)...)
}

// OutNumber emits bytecode to output top of stack as a number.
func OutNumber() []byte {
	return append(PushInstruction(0), OpOut)
}

// OutCharacter emits bytecode to output top of stack as a character.
func OutCharacter() []byte {
	return append(PushInstruction(1), OpOut)
}
