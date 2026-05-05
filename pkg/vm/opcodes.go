package vm

import (
	"encoding/binary"
	"fmt"
)

// Opcode constants — 32 original opcodes (0x00–0x1F) + new opcodes (0x20+).
const (
	OpPush      = 0x00 // [] → [value]
	OpPop       = 0x01 // [a] → []
	OpDup       = 0x02 // [a] → [a, a]
	OpSwap      = 0x03 // [a, b] → [b, a]
	OpOver      = 0x04 // [a, b] → [a, b, a]
	OpRot       = 0x05 // [a, b, c] → [b, c, a]
	OpAdd       = 0x06 // [a, b] → [a+b]
	OpSub       = 0x07 // [a, b] → [a-b]
	OpMul       = 0x08 // [a, b] → [a*b]
	OpDiv       = 0x09 // [a, b] → [a/b]
	OpMod       = 0x0A // [a, b] → [a%b]
	OpInc       = 0x0B // [a] → [a+1]
	OpDec       = 0x0C // [a] → [a-1]
	OpAnd       = 0x0D // [a, b] → [a&b]
	OpOr        = 0x0E // [a, b] → [a|b]
	OpXor       = 0x0F // [a, b] → [a^b]
	OpNot       = 0x10 // [a] → [~a]
	OpShl       = 0x11 // [a, b] → [a << (b%32)]
	OpEq        = 0x12 // [a, b] → [a==b ? 1 : 0]
	OpLt        = 0x13 // [a, b] → [a<b ? 1 : 0]
	OpCallStack = 0x14 // [addr] → [...]
	OpJmp       = 0x15 // unconditional jump
	OpJz        = 0x16 // [cond] → [], jump if zero
	OpCall      = 0x17 // call inline address
	OpRet       = 0x18 // return from call
	OpLoad      = 0x19 // [] → [mem[addr]]
	OpStore     = 0x1A // [value] → []
	OpOut       = 0x1B // [format, value] → []
	OpHalt      = 0x1C // stop
	OpYield     = 0x1D // Yield to host; triggers YieldHandler if set
	OpLoadI     = 0x1E // [addr] → [mem[addr]] Pop addr from stack, push memory[addr]
	OpStoreI    = 0x1F // [addr, value] → [] Pop addr from stack, pop value, store value at addr
	OpShr       = 0x20 // [a, b] → [a >>> (b%32)] Logical right shift
	OpSar       = 0x21 // [a, b] → [a >> (b%32)] Arithmetic right shift, with sign extension
	OpJnz       = 0x22 // [cond] → [] Jump if non-zero, jump if cond != 0
	OpNeg       = 0x23 // [a] → [-a]
	OpGt        = 0x24 // [a, b] → [a > b ? 1 : 0]
	OpNeq       = 0x25 // [a, b] → [a != b ? 1 : 0]
	OpLte       = 0x26 // [a, b] → [a <= b ? 1 : 0]
	OpGte       = 0x27 // [a, b] → [a >= b ? 1 : 0]
	OpPick      = 0x28 // Pick: [... n] → [... stack[n]]; copies nth element (0=top) to top
	OpDivmod    = 0x29 // [a, b] → [a/b, a%b] Divide with modulus
	OpAbs       = 0x2A // [a] → [|a|]
	OpMin       = 0x2B // [a, b] → [min(a, b)]
	OpMax       = 0x2C // [a, b] → [max(a, b)]
	OpJmpStack  = 0x2D // [addr] → [], pc = addr
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
	case OpOver:
		return "OVER"
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
	case OpJmpStack:
		return "JMPSTACK"
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
