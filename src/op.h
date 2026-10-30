#ifndef _BS_OP_H_
#define _BS_OP_H_

#include <stdint.h>

#define OPCODES(X)             \
    X(OP_PUSH, 0xC0)           \
    X(OP_PUSH_LOCAL, 0xC1)     \
    X(OP_PUSH_GLOBAL, 0xC2)    \
    X(OP_PUSH_BUILTIN, 0xC3)   \
    X(OP_PUSH_IMMEDIATE, 0x84) \
    X(OP_POP, 0x45)            \
    X(OP_DUP, 0x86)            \
    X(OP_CONV, 0x07)           \
    X(OP_MUL, 0x08)            \
    X(OP_DIV, 0x09)            \
    X(OP_REM, 0x0A)            \
    X(OP_MOD, 0x0B)            \
    X(OP_ADD, 0x0C)            \
    X(OP_SUB, 0x0D)            \
    X(OP_AND, 0x0E)            \
    X(OP_OR, 0x0F)             \
    X(OP_XOR, 0x10)            \
    X(OP_NEG, 0x11)            \
    X(OP_NOT, 0x12)            \
    X(OP_SHL, 0x13)            \
    X(OP_SHR, 0x14)            \
    X(OP_CMP, 0x15)            \
    X(OP_B, 0xB6)              \
    X(OP_BT, 0xB7)             \
    X(OP_BF, 0xB8)             \
    X(OP_CALL, 0xD9)           \
    X(OP_CALLV, 0x99)          \
    X(OP_PUSH_ENV, 0xBA)       \
    X(OP_POP_ENV, 0xBB)        \
    X(OP_RET, 0x9C)            \
    X(OP_EXIT, 0x9D)           \
    X(OP_POPZ, 0x9E)           \
    X(OP_BREAK, 0xFF)

#define MAKE_ENUM(name, val) name = val,
typedef enum {
    OPCODES(MAKE_ENUM)
} Op;

static inline const char* Op_getOpcodeName(Op op) {
    switch (op) {
#define X_CASE(name, val) case val: return #name;
        OPCODES(X_CASE)
#undef X_CASE
        default: return "UNKNOWN";
    }
}

#endif