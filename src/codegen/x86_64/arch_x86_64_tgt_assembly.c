#include <codegen/x86_64/arch_x86_64_tgt_assembly.h>

#include <codegen.h>
#include <codegen/codegen_forward.h>
#include <codegen/intermediate_representation.h>
#include <codegen/machine_ir.h>
#include <codegen/x86_64/arch_x86_64.h>
#include <codegen/x86_64/arch_x86_64_common.h>
#include <codegen/x86_64/arch_x86_64_isel.h>
#include <utils.h>
#include <vector.h>

static const char *setcc_suffixes_x86_64[COMPARE_COUNT] = {
    "e",
    "ne",
    "l",
    "le",
    "g",
    "ge",
};

static const char *instruction_mnemonic(CodegenContext *context, MIROpcodex86_64 instruction) {
  STATIC_ASSERT(MX64_COUNT == 29, "ERROR: instruction_mnemonic() must exhaustively handle all instructions.");
  // x86_64 instructions that aren't different across syntaxes can go here!
  switch (instruction) {
  default: break;
  case MX64_ADD: return "add";
  case MX64_SUB: return "sub";
    // case MX64_MUL: return "mul";
  case MX64_IMUL: return "imul";
  case MX64_DIV: return "div";
  case MX64_IDIV: return "idiv";
  case MX64_SAL: return "sal";
  case MX64_SAR: return "sar";
  case MX64_SHR: return "shr";
  case MX64_AND: return "and";
  case MX64_OR: return "or";
  case MX64_NOT: return "not";
  case MX64_PUSH: return "push";
  case MX64_POP: return "pop";
  case MX64_XOR: return "xor";
  case MX64_CMP: return "cmp";
  case MX64_CALL: return "call";
  case MX64_JMP: return "jmp";
  case MX64_RET: return "ret";
  case MX64_MOV: return "mov";
  case MX64_MOVSX: return "movsx";
  case MX64_MOVZX: return "movzx";
  case MX64_XCHG: return "xchg";
  case MX64_LEA: return "lea";
  case MX64_SETCC: return "set";
  case MX64_TEST: return "test";
  case MX64_JCC: return "j";
  }

  switch (context->dialect) {

  case CG_ASM_DIALECT_ATT: {
    switch (instruction) {
    case MX64_CWD: return "cwtd";
    case MX64_CDQ: return "cltd";
    case MX64_CQO: return "cqto";
    default: break;
    }
  } break;

  case CG_ASM_DIALECT_INTEL: {
    switch (instruction) {
    case MX64_CWD: return "cwd";
    case MX64_CDQ: return "cdq";
    case MX64_CQO: return "cqo";
    default: break;
    } break;
  } break;

  default: break;
  }
  ICE("instruction_mnemonic(): Unknown instruction.");
}

static void femit_imm_to_reg(CodegenContext *context, MIROpcodex86_64 inst, int64_t immediate, RegisterDescriptor destination_register, enum RegSize size) {
  if ((inst == MX64_SUB || inst == MX64_ADD) && immediate == 0) return;
  // We can get away with smaller (sign extended) moves if immediate is small enough.
  if (size > r32 && (inst == MX64_MOV) && (immediate >= INT32_MIN && immediate <= INT32_MAX)) {
    size = r32;
  }

  const char *mnemonic    = instruction_mnemonic(context, inst);
  const char *destination = regname(destination_register, size);
  switch (context->dialect) {
    case CG_ASM_DIALECT_ATT:
      fprint(context->code, "    %s $%D, %%%s\n",
          mnemonic, immediate, destination);
      break;
    case CG_ASM_DIALECT_INTEL:
      fprint(context->code, "    %s %s, %D\n",
          mnemonic, destination, immediate);
      break;
    default: ICE("ERROR: femit_imm_to_reg(): Unsupported dialect %d", context->dialect);
  }
}

static void femit_imm_to_mem(CodegenContext *context, MIROpcodex86_64 inst, int64_t immediate, RegisterDescriptor address_register, int64_t offset) {
  const char *mnemonic = instruction_mnemonic(context, inst);
  const char *address = register_name(address_register);
  switch (context->dialect) {
    case CG_ASM_DIALECT_ATT:
      fprint(context->code, "    %s $%D, %D(%%%s)\n",
          mnemonic, immediate, offset, address);
      break;
    case CG_ASM_DIALECT_INTEL:
      fprint(context->code, "    %s [%s + %D], %D\n",
          mnemonic, address, offset, immediate);
      break;
    default: ICE("ERROR: femit_imm_to_mem(): Unsupported dialect %d", context->dialect);
  }
}

static void femit_mem_to_reg(CodegenContext *context, MIROpcodex86_64 inst, RegisterDescriptor address_register, int64_t offset, RegisterDescriptor destination_register, RegSize size) {
  const char *mnemonic = instruction_mnemonic(context, inst);
  const char *address = register_name(address_register);
  const char *destination = regname(destination_register, size);
  switch (context->dialect) {
    case CG_ASM_DIALECT_ATT:
      fprint(context->code, "    %s %D(%%%s), %%%s\n",
          mnemonic, offset, address, destination);
      break;
    case CG_ASM_DIALECT_INTEL:
      fprint(context->code, "    %s %s, [%s + %D]\n",
          mnemonic, destination, address, offset);
      break;
    default: ICE("ERROR: femit_mem_to_reg(): Unsupported dialect %d", context->dialect);
  }
}

static void femit_name_to_reg(CodegenContext *context, MIROpcodex86_64 inst, RegisterDescriptor address_register, const char *name, RegisterDescriptor destination_register, enum RegSize size) {
  const char *mnemonic = instruction_mnemonic(context, inst);
  const char *address = register_name(address_register);
  const char *destination = regname(destination_register, size);
  switch (context->dialect) {
    case CG_ASM_DIALECT_ATT:
      fprint(context->code, "    %s (%s)(%%%s), %%%s\n",
          mnemonic, name, address, destination);
      break;
    case CG_ASM_DIALECT_INTEL:
      fprint(context->code, "    %s %s, [%s + %s]\n",
          mnemonic, destination, address, name);
      break;
    default: ICE("ERROR: femit_name_to_reg(): Unsupported dialect %d", context->dialect);
  }
}

static void femit_reg_to_mem(CodegenContext *context, MIROpcodex86_64 inst, RegisterDescriptor source_register, enum RegSize size, RegisterDescriptor address_register, int64_t offset) {
  const char *mnemonic = instruction_mnemonic(context, inst);
  const char *source = regname(source_register, size);
  const char *address = register_name(address_register);
  switch (context->dialect) {
    case CG_ASM_DIALECT_ATT:
      if (offset) {
        fprint(context->code, "    %s %%%s, %D(%%%s)\n",
                mnemonic, source, offset, address);
      } else {
        fprint(context->code, "    %s %%%s, (%%%s)\n",
                mnemonic, source, address);
      }
      break;
    case CG_ASM_DIALECT_INTEL:
      if (offset) {
        fprint(context->code, "    %s [%s + %D], %s\n",
                mnemonic, address, offset, source);
      } else {
        fprint(context->code, "    %s [%s], %s\n",
                mnemonic, address, source);
      }
      break;
    default: ICE("ERROR: femit_reg_to_mem(): Unsupported dialect %d", context->dialect);
  }
}

static void femit_reg_to_reg
(CodegenContext *context,
 MIROpcodex86_64 inst,
 RegisterDescriptor source_register, enum RegSize source_size,
 RegisterDescriptor destination_register, enum RegSize destination_size
 )
{
  // Always optimise away moves from a register to itself
  if (inst == MX64_MOV
      && source_register == destination_register
      && source_size == destination_size)
    {
      fprint(context->code, ";;#; skipping move from self to self\n");
      return;
    }

  const char *mnemonic = instruction_mnemonic(context, inst);
  const char *source = regname(source_register, source_size);
  const char *destination = regname(destination_register, destination_size);

  switch (context->dialect) {
  case CG_ASM_DIALECT_ATT:
    fprint(context->code, "    %s %%%s, %%%s\n",
           mnemonic, source, destination);
    break;
  case CG_ASM_DIALECT_INTEL:
    fprint(context->code, "    %s %s, %s\n",
           mnemonic, destination, source);
    break;
  default: ICE("ERROR: femit_reg_to_reg(): Unsupported dialect %d", context->dialect);
  }
}

static void femit_reg_to_name(CodegenContext *context, MIROpcodex86_64 inst, RegisterDescriptor source_register, enum RegSize size, RegisterDescriptor address_register, const char *name) {
  const char *mnemonic = instruction_mnemonic(context, inst);
  const char *source = regname(source_register, size);
  const char *address = register_name(address_register);
  switch (context->dialect) {
    case CG_ASM_DIALECT_ATT:
      fprint(context->code, "    %s %%%s, (%s)(%%%s)\n",
          mnemonic, source, name, address);
      break;
    case CG_ASM_DIALECT_INTEL:
      fprint(context->code, "    %s [%s + %s], %s\n",
          mnemonic, address, name, source);
      break;
    default: ICE("ERROR: femit_reg_to_name(): Unsupported dialect %d", context->dialect);
  }
}

static void femit_reg_to_offset_name(CodegenContext *context, MIROpcodex86_64 inst, RegisterDescriptor source_register, enum RegSize size, RegisterDescriptor address_register, const char *name, usz offset) {
  const char *mnemonic = instruction_mnemonic(context, inst);
  const char *source = regname(source_register, size);
  const char *address = register_name(address_register);
  switch (context->dialect) {
    case CG_ASM_DIALECT_ATT:
      fprint(context->code, "    %s %%%s, (%s+%Z)(%%%s)\n",
          mnemonic, source, name, offset, address);
      break;
    case CG_ASM_DIALECT_INTEL:
      fprint(context->code, "    %s %Z[%s + %s], %s\n",
          mnemonic, offset, name, address, source);
      break;
    default: ICE("ERROR: femit_reg_to_name(): Unsupported dialect %d", context->dialect);
  }
}

static void femit_mem(CodegenContext *context, MIROpcodex86_64 inst, int64_t offset, RegisterDescriptor address_register) {
  const char *mnemonic = instruction_mnemonic(context, inst);
  const char *address = register_name(address_register);
  switch (context->dialect) {
    case CG_ASM_DIALECT_ATT:
      fprint(context->code, "    %s %D(%%%s)\n",
          mnemonic, offset, address);
      break;
    case CG_ASM_DIALECT_INTEL:
      fprint(context->code, "    %s [%s + %D]\n",
          mnemonic, address, offset);
      break;
    default: ICE("ERROR: femit_mem(): Unsupported dialect %d", context->dialect);
  }
}

static void femit_reg_shift(CodegenContext *context, MIROpcodex86_64 inst, RegisterDescriptor register_to_shift) {
  const char *mnemonic = instruction_mnemonic(context, inst);
  const char *cl = register_name_8(REG_RCX);
  switch (context->dialect) {
  case CG_ASM_DIALECT_ATT:
    fprint(context->code, "    %s %%%s, %%%s\n",
           mnemonic, cl, register_name(register_to_shift));
    break;
  case CG_ASM_DIALECT_INTEL:
    fprint(context->code, "    %s %s, %s\n",
           mnemonic, register_name(register_to_shift), cl);
    break;
  default: ICE("ERROR: femit_reg_shift(): Unsupported dialect %d for shift instruction", context->dialect);
  }
}

/// You should probably use `femit_reg`
static void femit_indirect_branch(CodegenContext *context, MIROpcodex86_64 inst, RegisterDescriptor address_register) {
  const char *mnemonic = instruction_mnemonic(context, inst);
  const char *address = register_name(address_register);
  switch (context->dialect) {
    case CG_ASM_DIALECT_ATT:
      fprint(context->code, "    %s *%%%s\n",
          mnemonic, address);
      break;
    case CG_ASM_DIALECT_INTEL:
      fprint(context->code, "    %s %s\n",
          mnemonic, address);
      break;
    default: ICE("ERROR: femit_indirect_branch(): Unsupported dialect %d", context->dialect);
  }
}

static void femit_reg(CodegenContext *context, MIROpcodex86_64 inst, RegisterDescriptor reg, enum RegSize size) {
  if (inst == MX64_JMP || inst == MX64_CALL) {
    femit_indirect_branch(context, inst, reg);
    return;
  }
  if (inst == MX64_SAL || inst == MX64_SAR || inst == MX64_SHL || inst == MX64_SHR) {
    femit_reg_shift(context, inst, reg);
    return;
  }

  const char *mnemonic = instruction_mnemonic(context, inst);
  const char *source = regname(reg, size);
  switch (context->dialect) {
    case CG_ASM_DIALECT_ATT:
      fprint(context->code, "    %s %%%s\n",
          mnemonic, source);
      break;
    case CG_ASM_DIALECT_INTEL:
      fprint(context->code, "    %s %s\n",
          mnemonic, source);
      break;
    default: ICE("ERROR: femit_reg(): Unsupported dialect %d", context->dialect);
  }
}

static void femit_imm(CodegenContext *context, MIROpcodex86_64 inst, int64_t immediate) {
  const char *mnemonic = instruction_mnemonic(context, inst);
  switch (context->dialect) {
    case CG_ASM_DIALECT_ATT:
      fprint(context->code, "    %s $%D\n",
          mnemonic, immediate);
      break;
    case CG_ASM_DIALECT_INTEL:
      fprint(context->code, "    %s %D\n",
          mnemonic, immediate);
      break;
    default: ICE("ERROR: femit_imm(): Unsupported dialect %d", context->dialect);
  }
}

static void femit_name(CodegenContext *context, MIROpcodex86_64 inst, const char *name) {
  ASSERT(name, "NAME must not be NULL.");

  const char *mnemonic = instruction_mnemonic(context, inst);
  switch (context->dialect) {
  case CG_ASM_DIALECT_ATT:
    fprint(context->code, "    %s (%s)\n",
           mnemonic, name);
    break;
  case CG_ASM_DIALECT_INTEL:
    fprint(context->code, "    %s %s\n",
           mnemonic, name);
    break;
  default: ICE("ERROR: femit_name(): Unsupported dialect %d for CALL/JMP instruction", context->dialect);
  }
}

static void femit_setcc(CodegenContext *context, enum ComparisonType comparison_type, RegisterDescriptor value_register) {
  const char *mnemonic = instruction_mnemonic(context, MX64_SETCC);
  const char *value = register_name_8(value_register);
  switch (context->dialect) {
  case CG_ASM_DIALECT_ATT:
    fprint(context->code, "    %s%s %%%s\n",
           mnemonic,
           setcc_suffixes_x86_64[comparison_type], value);
    break;
  case CG_ASM_DIALECT_INTEL:
    fprint(context->code, "    %s%s %s\n",
           mnemonic,
           setcc_suffixes_x86_64[comparison_type], value);
    break;
  default: ICE("ERROR: femit_setcc(): Unsupported dialect %d", context->dialect);
  }

}



static void femit_jcc(CodegenContext *context, IndirectJumpType type, const char *label) {
      const char *mnemonic = instruction_mnemonic(context, MX64_JCC);

      switch (context->dialect) {
        case CG_ASM_DIALECT_ATT:
        case CG_ASM_DIALECT_INTEL:
          fprint(context->code, "    %s%s %s\n",
              mnemonic, jump_type_names_x86_64[type], label);
          break;
        default: ICE("ERROR: femit_direct_branch(): Unsupported dialect %d", context->dialect);
      }
}

static void femit_none(CodegenContext *context, MIROpcodex86_64 instruction) {
  switch (instruction) {
    case MX64_RET:
    case MX64_CWD:
    case MX64_CDQ:
    case MX64_CQO: {
      const char *mnemonic = instruction_mnemonic(context, instruction);
      fprint(context->code, "    %s\n", mnemonic);
    } break;

    default:
      ICE("Unhandled instruction in femit_none(): %d (%s)\n"
          "  Consider using femit_x() or femit_x_to_x()",
          instruction, instruction_mnemonic(context, instruction));
  }
}

void emit_x86_64_assembly(CodegenContext *context, MIRFunctionVector machine_instructions) {
  { // Emit entry
    fprint(context->code,
           "%s"
           ".section .text\n",
           context->dialect == CG_ASM_DIALECT_INTEL ? ".intel_syntax noprefix\n" : "");

    fprint(context->code, "\n");
    foreach_ptr (MIRFunction*, function, machine_instructions) {
      if (!function->origin->attr_global) continue;
      fprint(context->code, ".global %S\n", function->name);
    }
  }
  foreach_ptr (MIRFunction*, function, machine_instructions) {
    // Generate function entry label if function has definition.
    if (!function->origin->is_extern)
      fprint(context->code, "\n%s:\n", function->name.data);

    // Calculate stack offsets
    isz frame_offset = 0;
    foreach (MIRFrameObject, fo, function->frame_objects) {
      frame_offset -= fo->size;
      fo->offset = frame_offset;
    }

    foreach_ptr (MIRBlock*, block, function->blocks) {
      /// Emit block symbol if it is used.
      if (block->name.size)
        fprint(context->code, "%s:\n", block->name.data);

      foreach_ptr (MIRInstruction*, instruction, block->instructions) {
        switch (instruction->opcode) {

        case MX64_LEA: {
          if (mir_operand_kinds_match(instruction, 2, MIR_OP_LOCAL_REF, MIR_OP_REGISTER)) {
            MIROperand *local = mir_get_op(instruction, 0);
            MIROperand *destination = mir_get_op(instruction, 1);
            ASSERT(local->kind == MIR_OP_LOCAL_REF && local->value.local_ref != (usz)-1,
                   "LEA expected second operand to be frame object reference");
            ASSERT(destination->kind == MIR_OP_REGISTER,
                   "LEA requires third operand to be a destination register");
            femit_mem_to_reg(context, MX64_LEA, REG_RBP, mir_get_frame_object(function, local->value.local_ref)->offset, destination->value.reg.value, destination->value.reg.size);
          } else {
            print("\n\nUNHANDLED INSTRUCTION:\n");
            print_mir_instruction_with_mnemonic(instruction, mir_x86_64_opcode_mnemonic);
            ICE("[x86_64/CodeEmission]: Unhandled instruction, sorry");
          }
        } break; // case MX64_LEA

        case MX64_CALL: {
          MIROperand *dst = mir_get_op(instruction, 0);

          switch (dst->kind) {

          case MIR_OP_NAME: {
            print("|> call %s\n", dst->value.name);
            femit_name(context, MX64_CALL, dst->value.name);
          } break;

          case MIR_OP_BLOCK: {
            print("|> call %s\n", dst->value.block->name.data);
            femit_name(context, MX64_CALL, dst->value.block->name.data);
          } break;

          case MIR_OP_FUNCTION: {
            print("|> call %s\n", dst->value.function->name.data);
            femit_name(context, MX64_CALL, dst->value.function->name.data);
          } break;

          default: ICE("Unhandled operand kind in CALL: %d (%s)", dst->kind, mir_operand_kind_string(dst->kind));

          } // switch (dst->kind)

        } break; // case MX64_CALL

        case MX64_MOV: {
          if (mir_operand_kinds_match(instruction, 2, MIR_OP_IMMEDIATE, MIR_OP_REGISTER)) {
            // imm to reg | imm, dst
            MIROperand *imm = mir_get_op(instruction, 0);
            MIROperand *reg = mir_get_op(instruction, 1);
            if (!reg->value.reg.size) {
              putchar('\n');
              print_mir_instruction_with_mnemonic(instruction, mir_x86_64_opcode_mnemonic);
              print("%35WARNING%m: Zero sized register, assuming 64-bit...\n");
              putchar('\n');
              reg->value.reg.size = r64;
            }
            femit_imm_to_reg(context, MX64_MOV, imm->value.imm, reg->value.reg.value, reg->value.reg.size);
          } else if (mir_operand_kinds_match(instruction, 2, MIR_OP_IMMEDIATE, MIR_OP_LOCAL_REF)) {
            // imm to mem (local) | imm, local
            MIROperand *imm = mir_get_op(instruction, 0);
            MIROperand *local = mir_get_op(instruction, 1);
            ASSERT(local->value.local_ref < function->frame_objects.size,
                   "MX64_MOV(imm, local): local index %d is greater than amount of frame objects in function: %Z",
                   (int)local->value.local_ref, function->frame_objects.size);
            femit_imm_to_mem(context, MX64_MOV, imm->value.imm, REG_RBP, function->frame_objects.data[local->value.local_ref].offset);
          } else if (mir_operand_kinds_match(instruction, 2, MIR_OP_REGISTER, MIR_OP_REGISTER)) {
            // reg to reg | src, dst
            MIROperand *src = mir_get_op(instruction, 0);
            MIROperand *dst = mir_get_op(instruction, 1);
            if (!src->value.reg.size) {
              putchar('\n');
              print_mir_instruction_with_mnemonic(instruction, mir_x86_64_opcode_mnemonic);
              print("%35WARNING%m: Source is a zero sized register, assuming 64-bit...\n");
              putchar('\n');
              src->value.reg.size = r64;
            }
            if (!dst->value.reg.size) {
              putchar('\n');
              print_mir_instruction_with_mnemonic(instruction, mir_x86_64_opcode_mnemonic);
              print("%35WARNING%m: Source is a zero sized register, assuming 64-bit...\n");
              putchar('\n');
              dst->value.reg.size = r64;
            }
            femit_reg_to_reg(context, MX64_MOV,
                             src->value.reg.value, src->value.reg.size,
                             dst->value.reg.value, dst->value.reg.size);
          } else if (mir_operand_kinds_match(instruction, 2, MIR_OP_REGISTER, MIR_OP_LOCAL_REF)) {
            // reg to mem (local) | src, local
            MIROperand *reg = mir_get_op(instruction, 0);
            MIROperand *local = mir_get_op(instruction, 1);

            ASSERT(function->frame_objects.size,
                   "Cannot reference local at index %Z when there are no frame objects in this function",
                   local->value.local_ref);
            ASSERT(local->value.local_ref < function->frame_objects.size,
                   "Local reference index %Z is larger than maximum possible local index %Z",
                   local->value.local_ref, function->frame_objects.size - 1);

            femit_reg_to_mem(context, MX64_MOV, reg->value.reg.value, reg->value.reg.size,
                             REG_RBP, function->frame_objects.data[local->value.local_ref].offset);
          } else if (mir_operand_kinds_match(instruction, 2, MIR_OP_LOCAL_REF, MIR_OP_REGISTER)) {
            // mem (local) to reg | local, src
            MIROperand *local = mir_get_op(instruction, 0);
            MIROperand *reg = mir_get_op(instruction, 1);

            ASSERT(function->frame_objects.size,
                   "Cannot reference local at index %Z when there are no frame objects in this function",
                   local->value.local_ref);
            ASSERT(local->value.local_ref < function->frame_objects.size,
                   "Local reference index %Z is larger than maximum possible local index %Z",
                   local->value.local_ref, function->frame_objects.size - 1);

            femit_mem_to_reg(context, MX64_MOV,
                             REG_RBP, function->frame_objects.data[local->value.local_ref].offset,
                             reg->value.reg.value, reg->value.reg.size);
          } else if (mir_operand_kinds_match(instruction, 3, MIR_OP_IMMEDIATE, MIR_OP_REGISTER, MIR_OP_IMMEDIATE)) {
            // imm to mem | imm, addr, offset
            MIROperand *imm = mir_get_op(instruction, 0);
            MIROperand *reg_address = mir_get_op(instruction, 1);
            MIROperand *offset = mir_get_op(instruction, 2);
            femit_imm_to_mem(context, MX64_MOV, imm->value.imm, reg_address->value.reg.value, offset->value.imm);
          } else if (mir_operand_kinds_match(instruction, 3, MIR_OP_REGISTER, MIR_OP_REGISTER, MIR_OP_IMMEDIATE)) {
            // reg to mem | src, addr, offset
            MIROperand *reg_source = mir_get_op(instruction, 0);
            MIROperand *reg_address = mir_get_op(instruction, 1);
            MIROperand *offset = mir_get_op(instruction, 2);
            femit_reg_to_mem(context, MX64_MOV, reg_source->value.reg.value, reg_source->value.reg.size, reg_address->value.reg.value, offset->value.imm);
          } else if (mir_operand_kinds_match(instruction, 3, MIR_OP_REGISTER, MIR_OP_IMMEDIATE, MIR_OP_REGISTER)) {
            // mem to reg | addr, offset, dst
            MIROperand *reg_address = mir_get_op(instruction, 0);
            MIROperand *offset = mir_get_op(instruction, 1);
            MIROperand *reg_dst = mir_get_op(instruction, 2);
            femit_mem_to_reg(context, MX64_MOV, reg_address->value.reg.value, offset->value.imm, reg_dst->value.reg.value, reg_dst->value.reg.size);
          } else {
            print("\n\nUNHANDLED INSTRUCTION:\n");
            print_mir_instruction_with_mnemonic(instruction, mir_x86_64_opcode_mnemonic);
            ICE("[x86_64/CodeEmission]: Unhandled instruction, sorry");
          }

        } break; // case MX64_MOV

        case MX64_ADD: {
          if (mir_operand_kinds_match(instruction, 2, MIR_OP_IMMEDIATE, MIR_OP_REGISTER)) {
            // imm to reg | imm, dst
            MIROperand *imm = mir_get_op(instruction, 0);
            MIROperand *reg = mir_get_op(instruction, 1);
            if (!reg->value.reg.size) {
              putchar('\n');
              print_mir_instruction_with_mnemonic(instruction, mir_x86_64_opcode_mnemonic);
              print("%35WARNING%m: Zero sized register, assuming 64-bit...\n");
              putchar('\n');
              reg->value.reg.size = r64;
            }
            femit_imm_to_reg(context, MX64_ADD, imm->value.imm, reg->value.reg.value, reg->value.reg.size);
          } else if (mir_operand_kinds_match(instruction, 2, MIR_OP_REGISTER, MIR_OP_REGISTER)) {
            // reg to reg | src, dst
            MIROperand *src = mir_get_op(instruction, 0);
            MIROperand *dst = mir_get_op(instruction, 1);
            femit_reg_to_reg(context, MX64_ADD, src->value.reg.value, src->value.reg.size, dst->value.reg.value, dst->value.reg.size);
          } else {
            print("\n\nUNHANDLED INSTRUCTION:\n");
            print_mir_instruction_with_mnemonic(instruction, mir_x86_64_opcode_mnemonic);
            ICE("[x86_64/CodeEmission]: Unhandled instruction, sorry");
          }
        } break; // case MX64_ADD

        case MX64_SUB: {
          if (mir_operand_kinds_match(instruction, 2, MIR_OP_IMMEDIATE, MIR_OP_REGISTER)) {
            // imm to reg | imm, dst
            MIROperand *imm = mir_get_op(instruction, 0);
            MIROperand *reg = mir_get_op(instruction, 1);
            if (!reg->value.reg.size) {
              putchar('\n');
              print_mir_instruction_with_mnemonic(instruction, mir_x86_64_opcode_mnemonic);
              print("%35WARNING%m: Zero sized register, assuming 64-bit...\n");
              putchar('\n');
              reg->value.reg.size = r64;
            }
            femit_imm_to_reg(context, MX64_SUB, imm->value.imm, reg->value.reg.value, reg->value.reg.size);
          } else if (mir_operand_kinds_match(instruction, 2, MIR_OP_REGISTER, MIR_OP_REGISTER)) {
            // reg to reg | src, dst
            MIROperand *src = mir_get_op(instruction, 0);
            MIROperand *dst = mir_get_op(instruction, 1);
            femit_reg_to_reg(context, MX64_SUB, src->value.reg.value, src->value.reg.size, dst->value.reg.value, dst->value.reg.size);
          } else {
            print("\n\nUNHANDLED INSTRUCTION:\n");
            print_mir_instruction_with_mnemonic(instruction, mir_x86_64_opcode_mnemonic);
            ICE("[x86_64/CodeEmission]: Unhandled instruction, sorry");
          }
        } break; // case MX64_ADD

        case MX64_RET: {
          femit_none(context, MX64_RET);
        } break;

        case MX64_SHL: FALLTHROUGH;
        case MX64_SAR: FALLTHROUGH;
        case MX64_SHR: {
          if (mir_operand_kinds_match(instruction, 1, MIR_OP_REGISTER)) {
            MIROperand *reg = mir_get_op(instruction, 0);
            femit_reg(context, instruction->opcode, reg->value.reg.value, reg->value.reg.size);
          } else {
            print("\n\nUNHANDLED INSTRUCTION:\n");
            print_mir_instruction_with_mnemonic(instruction, mir_x86_64_opcode_mnemonic);
            ICE("[x86_64/CodeEmission]: Unhandled instruction, sorry");
          }
        } break;

        case MX64_POP: FALLTHROUGH;
        case MX64_PUSH: {
          if (mir_operand_kinds_match(instruction, 1, MIR_OP_REGISTER)) {
            MIROperand *reg = mir_get_op(instruction, 0);
            femit_reg(context, instruction->opcode, reg->value.reg.value, reg->value.reg.size);
          } else {
            print("\n\nUNHANDLED INSTRUCTION:\n");
            print_mir_instruction_with_mnemonic(instruction, mir_x86_64_opcode_mnemonic);
            ICE("[x86_64/CodeEmission]: Unhandled instruction, sorry");
          }
        } break;

        default: {
          print("Unhandled opcode: %d (%s)\n", instruction->opcode, mir_x86_64_opcode_mnemonic(instruction->opcode));
        } break;
        }
      }
    }
  }
}

