#ifndef MACHINE_IR_FORWARD_H
#define MACHINE_IR_FORWARD_H

#include <codegen/codegen_forward.h>
#include <utils.h>
#include <vector.h>

typedef struct MIRValue_x86_64 MIRValue_x86_64;

#define DEFINE_MIR_INSTRUCTION_TYPE(type, ...) CAT(MIR_, type),
typedef enum MIROpcodeCommon {
  ALL_IR_INSTRUCTION_TYPES(DEFINE_MIR_INSTRUCTION_TYPE)
  /// Marks beginning of function
  MIR_FUNCTION,
  /// Marks beginning of block
  MIR_BLOCK,
  MIR_COUNT
} MIROpcodeCommon;
#undef DEFINE_MIR_INSTRUCTION_TYPE

typedef struct MIRValue_x86_64 MIRValue_x86_64;

typedef enum MIROperandKind {
  MIR_OP_NONE,
  MIR_OP_REFERENCE,
  MIR_OP_REGISTER,
  MIR_OP_IMMEDIATE,
  MIR_OP_BLOCK,
  MIR_OP_FUNCTION,
} MIROperandKind;

typedef struct MIRInstruction MIRInstruction;
typedef Vector(MIRInstruction*) MIRVector;

typedef struct MIROperandRegister MIROperandRegister;
typedef int64_t MIROperandImmediate;
typedef IRBlock* MIROperandBlock;
typedef IRFunction* MIROperandFunction;
typedef MIRInstruction* MIROperandReference;

#endif /* MACHINE_IR_FORWARD_H */
