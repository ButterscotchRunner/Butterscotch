#ifndef _BS_OPWORD_H_
#define _BS_OPWORD_H_

typedef struct {
    uint32_t value;
} OpWord;

// Where's type 4? /j https://en.wikipedia.org/wiki/R4:_Ridge_Racer_Type_4

static inline uint8_t OpWord_opcode(OpWord opword) {
    return (opword.value >> 24 & 0xFF);
}

static inline uint8_t OpWord_type1(OpWord opword) {
    return (opword.value >> 16) & 0xF;
}

static inline uint8_t OpWord_type2(OpWord opword) {
    return (opword.value >> 20) & 0xF;
}

static inline int16_t OpWord_extra(OpWord opword) {
    return (int16_t) (opword.value & 0xFFFF);
}

static inline uint8_t OpWord_comparisonFunction(OpWord opword) {
    return (opword.value >> 8) & 0xF;
}

static inline bool OpWord_hasOperand(OpWord opword) {
    return (OpWord_opcode(opword) & 0x40) != 0;
}

static inline int32_t OpWord_branchOffset(OpWord opword) {
    return (((int32_t) opword.value) << 9) >> 7;
}

static uint32_t OpWord_operandSize(OpWord opword) {
    if (!OpWord_hasOperand(opword))
        return 0;

    switch (OpWord_type1(opword)) {
        case GML_TYPE_DOUBLE: return 8;
        case GML_TYPE_INT64:  return 8;
        case GML_TYPE_FLOAT:  return 4;
        case GML_TYPE_INT32:  return 4;
        case GML_TYPE_BOOL:   return 4;
        case GML_TYPE_VARIABLE: return 4;
        case GML_TYPE_STRING: return 4;
        case GML_TYPE_INT16:  return 0;
        default:              return 0;
    }
}

#endif