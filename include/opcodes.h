#ifndef OPCODES_H
#define OPCODES_H

#include <stdint.h>

// Stack Manipulation: 0x00–0x07
#define OP_PUSH  0x00
#define OP_POP   0x01
#define OP_DUP   0x02
#define OP_SWAP  0x03
#define OP_OVER  0x04
#define OP_ROT   0x05
#define OP_PICK  0x06
#define OP_ROLL  0x07

// Arithmetic: 0x08–0x13
#define OP_ADD    0x08
#define OP_SUB    0x09
#define OP_MUL    0x0A
#define OP_DIV    0x0B
#define OP_MOD    0x0C
#define OP_INC    0x0D
#define OP_DEC    0x0E
#define OP_NEG    0x0F
#define OP_ABS    0x10
#define OP_DIVMOD 0x11
#define OP_MIN    0x12
#define OP_MAX    0x13

// Bitwise & Shifts: 0x14–0x1A
#define OP_AND 0x14
#define OP_OR  0x15
#define OP_XOR 0x16
#define OP_NOT 0x17
#define OP_SHL 0x18
#define OP_SHR 0x19
#define OP_SAR 0x1A

// Comparison: 0x1B–0x20
#define OP_EQ  0x1B
#define OP_NEQ 0x1C
#define OP_LT  0x1D
#define OP_LTE 0x1E
#define OP_GT  0x1F
#define OP_GTE 0x20

// Control Flow: 0x21–0x27
#define OP_JMP       0x21
#define OP_JZ        0x22
#define OP_JNZ       0x23
#define OP_CALL      0x24
#define OP_RET       0x25
#define OP_CALLSTACK 0x26
#define OP_JMPSTACK  0x27

// Memory: 0x28–0x2B
#define OP_LOAD   0x28
#define OP_STORE  0x29
#define OP_LOADI  0x2A
#define OP_STOREI 0x2B

// Loop Stack: 0x2C–0x2F
#define OP_PUSHR  0x2C
#define OP_POPR   0x2D
#define OP_PEEKR  0x2E
#define OP_PEEKR2 0x2F

// Frame & Local Variables: 0x30–0x33
#define OP_FRAME    0x30
#define OP_UNFRAME  0x31
#define OP_LOCALGET 0x32
#define OP_LOCALSET 0x33

// I/O & System: 0x34–0x36
#define OP_OUT   0x34
#define OP_HALT  0x35
#define OP_YIELD 0x36

const char* opcode_name(uint8_t op);

#endif // OPCODES_H
