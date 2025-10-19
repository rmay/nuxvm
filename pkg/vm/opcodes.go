package vm

import (
	"encoding/binary"
	"fmt"
)

// Opcode constants
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
	OpNeg       = 0x0D
	OpAnd       = 0x0E
	OpOr        = 0x0F
	OpXor       = 0x10
	OpNot       = 0x11
	OpShl       = 0x12
	OpEq        = 0x13
	OpLt        = 0x14
	OpGt        = 0x15
	OpCallStack = 0x16
	OpJmp       = 0x17
	OpJz        = 0x18
	OpJnz       = 0x19
	OpCall      = 0x1A
	OpRet       = 0x1B
	OpLoad      = 0x1C
	OpStore     = 0x1D
	OpOut       = 0x1E
	OpHalt      = 0x1F
)

// OpcodeName returns the human-readable name for an opcode
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
	case OpNeg:
		return "NEG"
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
	case OpGt:
		return "GT"
	case OpCallStack:
		return "CALLSTACK"
	case OpJmp:
		return "JMP"
	case OpJz:
		return "JZ"
	case OpJnz:
		return "JNZ"
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
	default:
		return fmt.Sprintf("UNKNOWN(0x%02X)", op)
	}
}

// Helper functions for building programs

// EncodeInt32 encodes a 32-bit integer as big-endian bytes
func EncodeInt32(value int32) []byte {
	buf := make([]byte, 4)
	binary.BigEndian.PutUint32(buf, uint32(value))
	return buf
}

// PushInstruction creates a PUSH instruction with the given value
func PushInstruction(value int32) []byte {
	return append([]byte{OpPush}, EncodeInt32(value)...)
}

// JmpInstruction creates a JMP instruction to the given address
func JmpInstruction(addr int32) []byte {
	return append([]byte{OpJmp}, EncodeInt32(addr)...)
}

// JzInstruction creates a JZ instruction to the given address
func JzInstruction(addr int32) []byte {
	return append([]byte{OpJz}, EncodeInt32(addr)...)
}

// JnzInstruction creates a JNZ instruction to the given address
func JnzInstruction(addr int32) []byte {
	return append([]byte{OpJnz}, EncodeInt32(addr)...)
}

// CallInstruction creates a CALL instruction to the given address
func CallInstruction(addr int32) []byte {
	return append([]byte{OpCall}, EncodeInt32(addr)...)
}

// LoadInstruction creates a LOAD instruction from the given address
func LoadInstruction(addr int32) []byte {
	return append([]byte{OpLoad}, EncodeInt32(addr)...)
}

// StoreInstruction creates a STORE instruction to the given address
func StoreInstruction(addr int32) []byte {
	return append([]byte{OpStore}, EncodeInt32(addr)...)
}

// OutNumber emits bytecode to output top of stack as number
func OutNumber() []byte {
	return append(PushInstruction(0), OpOut)
}

// OutCharacter emits bytecode to output top of stack as character
func OutCharacter() []byte {
	return append(PushInstruction(1), OpOut)
}
