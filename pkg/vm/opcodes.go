package vm

import (
	"encoding/binary"
	"fmt"
)

// Opcode constants — 55 opcodes (0x00–0x36), grouped by function.
const (
	// Stack Manipulation: 0x00–0x07
	OpPush = 0x00 // [] → [value]; 5-byte encoding (opcode + 4-byte big-endian immediate)
	OpPop  = 0x01 // [a] → []
	OpDup  = 0x02 // [a] → [a, a]
	OpSwap = 0x03 // [a, b] → [b, a]
	OpOver = 0x04 // [a, b] → [a, b, a]
	OpRot  = 0x05 // [a, b, c] → [b, c, a]
	OpPick = 0x06 // [... n] → [... stack[n]]; pops n, copies nth element (0=top) to top
	OpRoll = 0x07 // [... x_n...x_0 n] → [... x_{n-1}...x_0 x_n]; rotates nth element to top

	// Arithmetic: 0x08–0x13
	OpAdd    = 0x08 // [a, b] → [a+b]
	OpSub    = 0x09 // [a, b] → [a-b]
	OpMul    = 0x0A // [a, b] → [a*b]
	OpDiv    = 0x0B // [a, b] → [a/b]
	OpMod    = 0x0C // [a, b] → [a%b]
	OpInc    = 0x0D // [a] → [a+1]
	OpDec    = 0x0E // [a] → [a-1]
	OpNeg    = 0x0F // [a] → [-a]
	OpAbs    = 0x10 // [a] → [|a|]
	OpDivmod = 0x11 // [a, b] → [a/b, a%b]; pushes quotient then remainder
	OpMin    = 0x12 // [a, b] → [min(a, b)]
	OpMax    = 0x13 // [a, b] → [max(a, b)]

	// Bitwise & Shifts: 0x14–0x1A
	OpAnd = 0x14 // [a, b] → [a&b]
	OpOr  = 0x15 // [a, b] → [a|b]
	OpXor = 0x16 // [a, b] → [a^b]
	OpNot = 0x17 // [a] → [~a]
	OpShl = 0x18 // [a, b] → [a << (b%32)]
	OpShr = 0x19 // [a, b] → [a >>> (b%32)]; logical (unsigned) right shift
	OpSar = 0x1A // [a, b] → [a >> (b%32)]; arithmetic right shift, sign-extended

	// Comparison: 0x1B–0x20
	OpEq  = 0x1B // [a, b] → [a==b ? 1 : 0]
	OpNeq = 0x1C // [a, b] → [a!=b ? 1 : 0]
	OpLt  = 0x1D // [a, b] → [a<b ? 1 : 0]
	OpLte = 0x1E // [a, b] → [a<=b ? 1 : 0]
	OpGt  = 0x1F // [a, b] → [a>b ? 1 : 0]
	OpGte = 0x20 // [a, b] → [a>=b ? 1 : 0]

	// Control Flow: 0x21–0x27
	OpJmp       = 0x21 // unconditional jump; 5-byte encoding
	OpJz        = 0x22 // [cond] → []; jump if zero; 5-byte encoding
	OpJnz       = 0x23 // [cond] → []; jump if non-zero; 5-byte encoding
	OpCall      = 0x24 // call inline address; 5-byte encoding; pushes return addr to return stack
	OpRet       = 0x25 // return from call; pops return stack
	OpCallStack = 0x26 // [addr] → [...]; call address from stack (quotations)
	OpJmpStack  = 0x27 // [addr] → []; jump to address from stack (tail calls)

	// Memory: 0x28–0x2B
	OpLoad   = 0x28 // [] → [mem[addr]]; inline address, 5-byte encoding
	OpStore  = 0x29 // [value] → []; inline address, 5-byte encoding
	OpLoadI  = 0x2A // [addr] → [mem[addr]]; address from stack
	OpStoreI = 0x2B // [value, addr] → []; address from stack

	// Loop Stack: 0x2C–0x2F
	OpPushR  = 0x2C // [a] → []; push to loop stack
	OpPopR   = 0x2D // [] → [a]; pop from loop stack to main stack
	OpPeekR  = 0x2E // [] → [a]; copy top of loop stack (non-destructive)
	OpPeekR2 = 0x2F // [] → [a, b]; copy top two of loop stack to main stack

	// Frame & Local Variables: 0x30–0x33
	OpFrame    = 0x30 // [n, v_n...v1] → []; save FP, set FP=SP, copy n locals into frame
	OpUnframe  = 0x31 // [] → []; restore SP=FP, restore old FP
	OpLocalGet = 0x32 // [offset] → [val]; load local variable at FP+offset
	OpLocalSet = 0x33 // [offset, val] → []; store local variable at FP+offset

	// I/O & System: 0x34–0x36
	OpOut   = 0x34 // [format, value] → []; console output (format: 0=number, 1=char)
	OpHalt  = 0x35 // stop execution
	OpYield = 0x36 // yield to host; calls YieldHandler if set
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
	case OpRoll:
		return "ROLL"
	default:
		return fmt.Sprintf("UNKNOWN(0x%02X)", op)
	}
}

func EncodeInt32(v int32) []byte {
	b := make([]byte, 4)
	binary.BigEndian.PutUint32(b, uint32(v))
	return b
}
