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
	OpPushR     = 0x2E // [a] → [], LoopStack += a
	OpPopR      = 0x2F // [] → [a], a = LoopStack.Pop()
	OpPeekR     = 0x30 // [] → [a], a = LoopStack.Top()
	OpFrame     = 0x31 // [n, v_n...v1] → [], save old FP, FP=SP, copy n items
	OpUnframe   = 0x32 // [] → [], SP=FP, FP=[SP]
	OpLocalGet  = 0x33 // [offset] → [val], val = Frame[FP+offset]
	OpLocalSet  = 0x34 // [offset, val] → [], Frame[FP+offset] = val
	OpPeekR2    = 0x35 // [] → [a, b], a = LoopStack.Top-1, b = LoopStack.Top
)

func PushInstruction(v int32) []byte {
	b := []byte{OpPush}
	b = append(b, EncodeInt32(v)...)
	return b
}

func StoreInstruction(addr uint32) []byte {
	b := []byte{OpStore}
	b = append(b, EncodeInt32(int32(addr))...)
	return b
}

func LoadInstruction(addr uint32) []byte {
	b := []byte{OpLoad}
	b = append(b, EncodeInt32(int32(addr))...)
	return b
}

func JmpInstruction(addr uint32) []byte {
	b := []byte{OpJmp}
	b = append(b, EncodeInt32(int32(addr))...)
	return b
}

func JzInstruction(addr uint32) []byte {
	b := []byte{OpJz}
	b = append(b, EncodeInt32(int32(addr))...)
	return b
}

func CallInstruction(addr uint32) []byte {
	b := []byte{OpCall}
	b = append(b, EncodeInt32(int32(addr))...)
	return b
}

func OutNumber() []byte {
	b := PushInstruction(0)
	b = append(b, OpOut)
	return b
}

func OutCharacter() []byte {
	b := PushInstruction(1)
	b = append(b, OpOut)
	return b
}

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
	case OpPushR:
		return "PUSHR"
	case OpPopR:
		return "POPR"
	case OpPeekR:
		return "PEEKR"
	case OpFrame:
		return "FRAME"
	case OpUnframe:
		return "UNFRAME"
	case OpLocalGet:
		return "LOCALGET"
	case OpLocalSet:
		return "LOCALSET"
	case OpPeekR2:
		return "PEEKR2"
	default:
		return fmt.Sprintf("UNKNOWN(0x%02X)", op)
	}
}

func EncodeInt32(v int32) []byte {
	b := make([]byte, 4)
	binary.BigEndian.PutUint32(b, uint32(v))
	return b
}
