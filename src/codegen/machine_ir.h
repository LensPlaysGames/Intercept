#ifndef MACHINE_IR_H
#define MACHINE_IR_H

#include <codegen/codegen_forward.h>
#include <stddef.h>
#include <stdint.h>
#include <vector.h>
#include <utils.h>

#define DEFINE_MIR_INSTRUCTION_TYPE(type, ...) CAT(MIR_, type),
typedef enum MIROpcodeCommon {
  ALL_IR_INSTRUCTION_TYPES(DEFINE_MIR_INSTRUCTION_TYPE)
  /// Marks beginning of block
  // TODO: Do we need this?
  MIR_BLOCK,
  MIR_COUNT,
  MIR_ARCH_START = 0x420
} MIROpcodeCommon;
#undef DEFINE_MIR_INSTRUCTION_TYPE

typedef enum MIROperandKind {
  MIR_OP_NONE,
  MIR_OP_REGISTER,
  MIR_OP_IMMEDIATE,
  MIR_OP_BLOCK,
  MIR_OP_FUNCTION,
  MIR_OP_NAME,
  MIR_OP_STATIC_REF,
  MIR_OP_LOCAL_REF,
  MIR_OP_COUNT
} MIROperandKind;

typedef struct MIROperandRegister {
  uint32_t value;
  uint16_t size;
} MIROperandRegister;
typedef const char* MIROperandName;
typedef int64_t MIROperandImmediate;
typedef MIRBlock* MIROperandBlock;
typedef MIRFunction* MIROperandFunction;
typedef IRStaticVariable* MIROperandStatic;
typedef IRStackAllocation* MIROperandLocal;

typedef unsigned int MIRRegister;

typedef struct MIRValue_x86_64 {
  uint16_t instruction;
  uint8_t instruction_form;
  uint8_t reg_src_sz;
  uint8_t reg_src;
  uint8_t reg_dst_sz;
  uint8_t reg_dst;
  uint8_t reg_addr;
  int64_t immediate;
  int64_t offset;
  const char *name;
  IRBlock *ir_block;
  IRFunction *ir_function;
} MIRValue_x86_64;

typedef struct MIROperand {
  MIROperandKind kind;
  union {
    MIROperandRegister reg;
    MIROperandImmediate imm;
    MIROperandName name;
    MIROperandBlock block;
    MIROperandFunction function;
    MIROperandStatic static_ref;
    MIROperandLocal local_ref;
  } value;
} MIROperand;

#define MIR_OPERAND_SSO_THRESHOLD 3

// NOTE: There is no way of keeping track of which ISA a particular
// instruction is for; that's up to each backend that uses it!
typedef struct MIRInstruction {
  size_t id;

  MIRRegister reg;

  /// An MIR opcode is some integer that has the 1:1 IR types from 0->N
  /// and then after that each ISA starts it's own opcode enum for
  /// architecture-specific instructions.
  /// The idea is that each ISA gets fed an MIR full of regular IR
  /// instructions and it's job is to translate them into (or build a
  /// new MIR with) the arch-specific opcodes.
  uint32_t opcode;

  uint8_t operand_count;
  union {
    MIROperand arr[MIR_OPERAND_SSO_THRESHOLD];
    Vector(MIROperand) vec;
  } operands;

  // Architecture-specific values.
  union {
    MIRValue_x86_64 x64;
  };

  MIRBlock *block;

  // Keep track of originating IR instruction.
  IRInstruction *origin;

  // If an architecture lowers a generically-lowered MIR instruction,
  // store the arch-lowered MIR instruction in the generic instruction
  // so that we may properly update references going forward.
  struct MIRInstruction *lowered;

} MIRInstruction;

typedef struct MIRBlock {
  string name;

  MIRInstructionVector instructions;

  MIRFunction *function;
  IRBlock *origin;
} MIRBlock;

typedef struct MIRFunction {
  string name;

  uint32_t inst_count;
  MIRBlockVector blocks;

  IRFunction *origin;
} MIRFunction;

/// dwisott
MIRInstruction *mir_makenew(uint32_t opcode);
/// Copy the entire instruction
MIRInstruction *mir_makecopy(MIRInstruction *original);

/// Clear the given instructions operands.
void mir_op_clear(MIRInstruction *);

/// Caller is responsible for calling vector_delete() on returned vector.
MIRFunctionVector mir_from_ir(CodegenContext *context);

/// If opcode is within common opcode range (less than or equal to
/// MIR_COUNT), return a pointer to a NULL-terminated string containing
/// a human-readable name of the opcode.
const char *mir_common_opcode_mnemonic(uint32_t opcode);

/// \param opcode_mnemonic
///   A function that will be used to get a human-readable mnemonic for
///   any given opcode. If NULL returned, will fallback to mir_common_opcode_mnemonic().
FUNCTION_POINTER(const char*, OpcodeMnemonicFunction, uint32_t);
void print_mir_instruction_with_mnemonic(MIRInstruction *inst, OpcodeMnemonicFunction opcode_mnemonic);
void print_mir_block_with_mnemonic(MIRBlock *block, OpcodeMnemonicFunction opcode_mnemonic);
void print_mir_function_with_mnemonic(MIRFunction *function, OpcodeMnemonicFunction opcode_mnemonic);

/// Same as above but with mir_common_opcode_mnemonic passed implicitly.
void print_mir_operand(MIROperand *op);
void print_mir_instruction(MIRInstruction *inst);
void print_mir_block(MIRBlock *block);
void print_mir_function(MIRFunction *function);

MIROperand mir_op_function(MIRFunction *f);
MIROperand mir_op_block(MIRBlock *block);
MIROperand mir_op_reference(MIRInstruction *inst);
MIROperand mir_op_reference_ir(IRInstruction *inst);
MIROperand mir_op_immediate(int64_t imm);
MIROperand mir_op_name(const char *name);
MIROperand mir_op_register(RegisterDescriptor reg, uint16_t size);

void mir_add_op(MIRInstruction *inst, MIROperand op);
/// Return a pointer to operand at index within instruction.
MIROperand *mir_get_op(MIRInstruction *inst, size_t index);

void mir_push_with_reg(MIRFunction *mir, MIRInstruction *mi, MIRRegister reg);

MIRInstruction *mir_find_by_vreg(MIRFunction *mir, size_t reg);

MIRFunction *mir_function(IRFunction *ir_f);
MIRBlock *mir_block(MIRFunction *function, IRBlock *ir_bb);

#endif /* MACHINE_IR_H */
