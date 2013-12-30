/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/hir/hir_builder.h>

#include <alloy/hir/block.h>
#include <alloy/hir/instr.h>
#include <alloy/hir/label.h>

using namespace alloy;
using namespace alloy::hir;
using namespace alloy::runtime;


#define ASSERT_ADDRESS_TYPE(value)
#define ASSERT_INTEGER_TYPE(value)
#define ASSERT_FLOAT_TYPE(value)
#define ASSERT_NON_VECTOR_TYPE(value)
#define ASSERT_VECTOR_TYPE(value)
#define ASSERT_TYPES_EQUAL(value1, value2)


HIRBuilder::HIRBuilder() {
  arena_ = new Arena();
  Reset();
}

HIRBuilder::~HIRBuilder() {
  Reset();
  delete arena_;
}

void HIRBuilder::Reset() {
  attributes_ = 0;
  next_label_id_ = 0;
  next_value_ordinal_ = 0;
  block_head_ = block_tail_ = NULL;
  current_block_ = NULL;
#if XE_DEBUG
  arena_->DebugFill();
#endif
  arena_->Reset();
}

int HIRBuilder::Finalize() {
  // Scan blocks in order and add fallthrough branches. These are needed for
  // analysis passes to work. We may have also added blocks out of order and
  // need to ensure they fall through in the right order.
  for (auto block = block_head_; block != NULL; block = block->next) {
    bool needs_branch = false;
    if (block->instr_tail) {
      if (!IsUnconditionalJump(block->instr_tail)) {
        // Add tail branch to block that falls through.
        needs_branch = true;
      }
    } else {
      // Add tail branch to block with no instructions.
      // Hopefully an optimization pass will clean this up later.
      needs_branch = true;
    }
    if (needs_branch) {
      current_block_ = block;
      if (!block->next) {
        // No following block.
        // Sometimes VC++ generates functions with bl at the end even if they
        // will never return. Just add a return to satisfy things.
        XELOGW("Fall-through out of the function.");
        Return();
        current_block_ = NULL;
        break;
      }
      // Add branch.
      Branch(block->next, BRANCH_LIKELY);
      current_block_ = NULL;
    }
  }
  return 0;
}

void HIRBuilder::DumpValue(StringBuffer* str, Value* value) {
  if (value->IsConstant()) {
    switch (value->type) {
    case INT8_TYPE:     str->Append("%X", value->constant.i8);  break;
    case INT16_TYPE:    str->Append("%X", value->constant.i16); break;
    case INT32_TYPE:    str->Append("%X", value->constant.i32); break;
    case INT64_TYPE:    str->Append("%X", value->constant.i64); break;
    case FLOAT32_TYPE:  str->Append("%F", value->constant.f32); break;
    case FLOAT64_TYPE:  str->Append("%F", value->constant.f64); break;
    case VEC128_TYPE:   str->Append("(%F,%F,%F,%F)",
                                    value->constant.v128.x,
                                    value->constant.v128.y,
                                    value->constant.v128.z,
                                    value->constant.v128.w); break;
    default: XEASSERTALWAYS(); break;
    }
  } else {
    static const char* type_names[] = {
      "i8", "i16", "i32", "i64", "f32", "f64", "v128",
    };
    str->Append("v%d.%s", value->ordinal, type_names[value->type]);
  }
}

void HIRBuilder::DumpOp(
    StringBuffer* str, OpcodeSignatureType sig_type, Instr::Op* op) {
  switch (sig_type) {
  case OPCODE_SIG_TYPE_X:
    break;
  case OPCODE_SIG_TYPE_L:
    if (op->label->name) {
      str->Append(op->label->name);
    } else {
      str->Append("label%d", op->label->id);
    }
    break;
  case OPCODE_SIG_TYPE_O:
    str->Append("+%d", op->offset);
    break;
  case OPCODE_SIG_TYPE_S:
    str->Append("<function>");
    break;
  case OPCODE_SIG_TYPE_V:
    DumpValue(str, op->value);
    break;
  }
}

void HIRBuilder::Dump(StringBuffer* str) {
  if (attributes_) {
    str->Append("; attributes = %.8X\n", attributes_);
  }

  uint32_t block_ordinal = 0;
  Block* block = block_head_;
  while (block) {
    if (block == block_head_) {
      str->Append("<entry>:\n");
    } else if (!block->label_head) {
      str->Append("<block%d>:\n", block_ordinal);
    }
    block_ordinal++;

    Label* label = block->label_head;
    while (label) {
      if (label->name) {
        str->Append("%s:\n", label->name);
      } else {
        str->Append("label%d:\n", label->id);
      }
      label = label->next;
    }

    Instr* i = block->instr_head;
    while (i) {
      if (i->opcode->flags & OPCODE_FLAG_HIDE) {
        i = i->next;
        continue;
      }
      if (i->opcode->num == OPCODE_COMMENT) {
        str->Append("  ; %s\n", (char*)i->src1.offset);
        i = i->next;
        continue;
      }

      const OpcodeInfo* info = i->opcode;
      OpcodeSignatureType dest_type = GET_OPCODE_SIG_TYPE_DEST(info->signature);
      OpcodeSignatureType src1_type = GET_OPCODE_SIG_TYPE_SRC1(info->signature);
      OpcodeSignatureType src2_type = GET_OPCODE_SIG_TYPE_SRC2(info->signature);
      OpcodeSignatureType src3_type = GET_OPCODE_SIG_TYPE_SRC3(info->signature);
      str->Append("  ");
      if (dest_type) {
        DumpValue(str, i->dest);
        str->Append(" = ");
      }
      if (i->flags) {
        str->Append("%s.%d", info->name, i->flags);
      } else {
        str->Append("%s", info->name);
      }
      if (src1_type) {
        str->Append(" ");
        DumpOp(str, src1_type, &i->src1);
      }
      if (src2_type) {
        str->Append(", ");
        DumpOp(str, src2_type, &i->src2);
      }
      if (src3_type) {
        str->Append(", ");
        DumpOp(str, src3_type, &i->src3);
      }
      str->Append("\n");
      i = i->next;
    }

    block = block->next;
  }
}

Block* HIRBuilder::current_block() const {
  return current_block_;
}

Instr* HIRBuilder::last_instr() const {
  if (current_block_ && current_block_->instr_tail) {
    return current_block_->instr_tail;
  } else if (block_tail_) {
    return block_tail_->instr_tail;
  }
  return NULL;
}

Label* HIRBuilder::NewLabel() {
  Label* label = arena_->Alloc<Label>();
  label->next = label->prev = NULL;
  label->block = NULL;
  label->id = next_label_id_++;
  label->name = NULL;
  return label;
}

void HIRBuilder::MarkLabel(Label* label, Block* block) {
  if (!block) {
    if (current_block_ && current_block_->instr_tail) {
      EndBlock();
    }
    if (!current_block_) {
      AppendBlock();
    }
    block = current_block_;
  }
  label->block = block;
  label->prev = block->label_tail;
  label->next = NULL;
  if (label->prev) {
    label->prev->next = label;
    block->label_tail = label;
  } else {
    block->label_head = block->label_tail = label;
  }
}

void HIRBuilder::InsertLabel(Label* label, Instr* prev_instr) {
  // If we are adding to the end just use the normal path.
  if (prev_instr == last_instr()) {
    MarkLabel(label);
    return;
  }

  // If we are adding at the last instruction in a block just mark
  // the following block with a label.
  if (!prev_instr->next) {
    Block* next_block = prev_instr->block->next;
    if (next_block) {
      label->block = next_block;
      label->next = NULL;
      label->prev = next_block->label_tail;
      next_block->label_tail = label;
      if (label->prev) {
        label->prev->next = label;
      } else {
        next_block->label_head = label;
      }
      return;
    } else {
      // No next block, which means we are at the end.
      MarkLabel(label);
      return;
    }
  }

  // In the middle of a block. Split the block in two and insert
  // the new block in the middle.
  // B1.I, B1.I, <insert> B1.I, B1.I
  // B1.I, B1.I, <insert> BN.I, BN.I
  Block* prev_block = prev_instr->block;
  Block* next_block = prev_instr->block->next;

  Block* new_block = arena_->Alloc<Block>();
  new_block->arena = arena_;
  new_block->prev = prev_block;
  new_block->next = next_block;
  if (prev_block) {
    prev_block->next = new_block;
  } else {
    block_head_ = new_block;
  }
  if (next_block) {
    next_block->prev = new_block;
  } else {
    block_tail_ = new_block;
  }
  new_block->label_head = new_block->label_tail = label;
  label->block = new_block;
  label->prev = label->next = NULL;

  Instr* prev_next = prev_instr->next;
  Instr* old_prev_tail = prev_block->instr_tail;
  if (prev_instr->next) {
    Instr* prev_last = prev_instr->next->prev;
    prev_last->next = NULL;
    prev_block->instr_tail = prev_last;
  }
  new_block->instr_head = prev_next;
  if (new_block->instr_head) {
    new_block->instr_head->prev = NULL;
    new_block->instr_tail = old_prev_tail;
  }

  for (auto instr = new_block->instr_head; instr != new_block->instr_tail;
       instr = instr->next) {
    instr->block = new_block;
  }

  if (current_block_ == prev_block) {
    current_block_ = new_block;
  }
}

void HIRBuilder::ResetLabelTags() {
  // TODO(benvanik): make this faster?
  auto block = block_head_;
  while (block) {
    auto label = block->label_head;
    while (label) {
      label->tag = 0;
      label = label->next;
    }
    block = block->next;
  }
}

Block* HIRBuilder::AppendBlock() {
  Block* block = arena_->Alloc<Block>();
  block->arena = arena_;
  block->next = NULL;
  block->prev = block_tail_;
  if (block_tail_) {
    block_tail_->next = block;
  }
  block_tail_ = block;
  if (!block_head_) {
    block_head_ = block;
  }
  current_block_ = block;
  block->label_head = block->label_tail = NULL;
  block->instr_head = block->instr_tail = NULL;
  return block;
}

void HIRBuilder::EndBlock() {
  if (current_block_ && !current_block_->instr_tail) {
    // Block never had anything added to it. Since it likely has an
    // incoming edge, just keep it around.
    return;
  }
  current_block_ = NULL;
}

bool HIRBuilder::IsUnconditionalJump(Instr* instr) {
  if (instr->opcode == &OPCODE_CALL_info ||
      instr->opcode == &OPCODE_CALL_INDIRECT_info) {
    return (instr->flags & CALL_TAIL) != 0;
  } else if (instr->opcode == &OPCODE_BRANCH_info) {
    return true;
  } else if (instr->opcode == &OPCODE_RETURN_info) {
    return true;
  }
  return false;
}

Instr* HIRBuilder::AppendInstr(
    const OpcodeInfo& opcode_info, uint16_t flags, Value* dest) {
  if (!current_block_) {
    AppendBlock();
  }
  Block* block = current_block_;

  Instr* instr = arena_->Alloc<Instr>();
  instr->next = NULL;
  instr->prev = block->instr_tail;
  if (block->instr_tail) {
    block->instr_tail->next = instr;
  }
  block->instr_tail = instr;
  if (!block->instr_head) {
    block->instr_head = instr;
  }
  instr->block = block;
  instr->opcode = &opcode_info;
  instr->flags = flags;
  instr->dest = dest;
  instr->src1_use = instr->src2_use = instr->src3_use = NULL;
  if (dest) {
    dest->def = instr;
  }
  // Rely on callers to set src args.
  // This prevents redundant stores.
  return instr;
}

Value* HIRBuilder::AllocValue(TypeName type) {
  Value* value = arena_->Alloc<Value>();
  value->ordinal = next_value_ordinal_++;
  value->type = type;
  value->flags = 0;
  value->def = NULL;
  value->use_head = NULL;
  value->tag = NULL;
  return value;
}

Value* HIRBuilder::CloneValue(Value* source) {
  Value* value = arena_->Alloc<Value>();
  value->ordinal = next_value_ordinal_++;
  value->type = source->type;
  value->flags = source->flags;
  value->constant.i64 = source->constant.i64;
  value->def = NULL;
  value->use_head = NULL;
  value->tag = NULL;
  return value;
}

void HIRBuilder::Comment(const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  xevsnprintfa(buffer, 1024, format, args);
  va_end(args);
  size_t len = xestrlena(buffer);
  if (!len) {
    return;
  }
  void* p = arena_->Alloc(len + 1);
  xe_copy_struct(p, buffer, len + 1);
  Instr* i = AppendInstr(OPCODE_COMMENT_info, 0);
  i->src1.offset = (uint64_t)p;
  i->src2.value = i->src3.value = NULL;
}

void HIRBuilder::Nop() {
  Instr* i = AppendInstr(OPCODE_NOP_info, 0);
  i->src1.value = i->src2.value = i->src3.value = NULL;
}

void HIRBuilder::SourceOffset(uint64_t offset) {
  Instr* i = AppendInstr(OPCODE_SOURCE_OFFSET_info, 0);
  i->src1.offset = offset;
  i->src2.value = i->src3.value = NULL;
}

void HIRBuilder::DebugBreak() {
  Instr* i = AppendInstr(OPCODE_DEBUG_BREAK_info, 0);
  i->src1.value = i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void HIRBuilder::DebugBreakTrue(Value* cond) {
  if (cond->IsConstant()) {
    if (cond->IsConstantTrue()) {
      DebugBreak();
    }
    return;
  }

  Instr* i = AppendInstr(OPCODE_DEBUG_BREAK_TRUE_info, 0);
  i->set_src1(cond);
  i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void HIRBuilder::Trap() {
  Instr* i = AppendInstr(OPCODE_TRAP_info, 0);
  i->src1.value = i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void HIRBuilder::TrapTrue(Value* cond) {
  if (cond->IsConstant()) {
    if (cond->IsConstantTrue()) {
      Trap();
    }
    return;
  }

  Instr* i = AppendInstr(OPCODE_TRAP_TRUE_info, 0);
  i->set_src1(cond);
  i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void HIRBuilder::Call(
    FunctionInfo* symbol_info, uint32_t call_flags) {
  Instr* i = AppendInstr(OPCODE_CALL_info, call_flags);
  i->src1.symbol_info = symbol_info;
  i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void HIRBuilder::CallTrue(
    Value* cond, FunctionInfo* symbol_info, uint32_t call_flags) {
  if (cond->IsConstant()) {
    if (cond->IsConstantTrue()) {
      Call(symbol_info, call_flags);
    }
    return;
  }

  Instr* i = AppendInstr(OPCODE_CALL_TRUE_info, call_flags);
  i->set_src1(cond);
  i->src2.symbol_info = symbol_info;
  i->src3.value = NULL;
  EndBlock();
}

void HIRBuilder::CallIndirect(
    Value* value, uint32_t call_flags) {
  ASSERT_ADDRESS_TYPE(value);
  Instr* i = AppendInstr(OPCODE_CALL_INDIRECT_info, call_flags);
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void HIRBuilder::CallIndirectTrue(
    Value* cond, Value* value, uint32_t call_flags) {
  if (cond->IsConstant()) {
    if (cond->IsConstantTrue()) {
      CallIndirect(value, call_flags);
    }
    return;
  }

  ASSERT_ADDRESS_TYPE(value);
  Instr* i = AppendInstr(OPCODE_CALL_INDIRECT_TRUE_info, call_flags);
  i->set_src1(cond);
  i->set_src2(value);
  i->src3.value = NULL;
  EndBlock();
}

void HIRBuilder::Return() {
  Instr* i = AppendInstr(OPCODE_RETURN_info, 0);
  i->src1.value = i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void HIRBuilder::SetReturnAddress(Value* value) {
  Instr* i = AppendInstr(OPCODE_SET_RETURN_ADDRESS_info, 0);
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
}

void HIRBuilder::Branch(Label* label, uint32_t branch_flags) {
  Instr* i = AppendInstr(OPCODE_BRANCH_info, branch_flags);
  i->src1.label = label;
  i->src2.value = i->src3.value = NULL;
  EndBlock();
}

void HIRBuilder::Branch(Block* block, uint32_t branch_flags) {
  if (!block->label_head) {
    // Block needs a label.
    Label* label = NewLabel();
    MarkLabel(label, block);
  }
  Branch(block->label_head, branch_flags);
}

void HIRBuilder::BranchTrue(
    Value* cond, Label* label, uint32_t branch_flags) {
  if (cond->IsConstant()) {
    if (cond->IsConstantTrue()) {
      Branch(label, branch_flags);
    }
    return;
  }

  Instr* i = AppendInstr(OPCODE_BRANCH_TRUE_info, branch_flags);
  i->set_src1(cond);
  i->src2.label = label;
  i->src3.value = NULL;
  EndBlock();
}

void HIRBuilder::BranchFalse(
    Value* cond, Label* label, uint32_t branch_flags) {
  if (cond->IsConstant()) {
    if (cond->IsConstantFalse()) {
      Branch(label, branch_flags);
    }
    return;
  }

  Instr* i = AppendInstr(OPCODE_BRANCH_FALSE_info, branch_flags);
  i->set_src1(cond);
  i->src2.label = label;
  i->src3.value = NULL;
  EndBlock();
}

// phi type_name, Block* b1, Value* v1, Block* b2, Value* v2, etc

Value* HIRBuilder::Assign(Value* value) {
  if (value->IsConstant()) {
    return value;
  }

  Instr* i = AppendInstr(
      OPCODE_ASSIGN_info, 0,
      AllocValue(value->type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Cast(Value* value, TypeName target_type) {
  if (value->type == target_type) {
    return value;
  } else if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->Cast(target_type);
    return dest;
  }

  Instr* i = AppendInstr(
      OPCODE_CAST_info, 0,
      AllocValue(target_type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::ZeroExtend(Value* value, TypeName target_type) {
  if (value->type == target_type) {
    return value;
  } else if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->ZeroExtend(target_type);
    return dest;
  }

  Instr* i = AppendInstr(
      OPCODE_ZERO_EXTEND_info, 0,
      AllocValue(target_type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::SignExtend(Value* value, TypeName target_type) {
  if (value->type == target_type) {
    return value;
  } else if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->SignExtend(target_type);
    return dest;
  }

  Instr* i = AppendInstr(
      OPCODE_SIGN_EXTEND_info, 0,
      AllocValue(target_type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Truncate(Value* value, TypeName target_type) {
  ASSERT_INTEGER_TYPE(value->type);
  ASSERT_INTEGER_TYPE(target_type);

  if (value->type == target_type) {
    return value;
  } else if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->Truncate(target_type);
    return dest;
  }

  Instr* i = AppendInstr(
      OPCODE_TRUNCATE_info, 0,
      AllocValue(target_type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Convert(Value* value, TypeName target_type,
                                RoundMode round_mode) {
  if (value->type == target_type) {
    return value;
  } else if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->Convert(target_type, round_mode);
    return dest;
  }

  Instr* i = AppendInstr(
      OPCODE_CONVERT_info, round_mode,
      AllocValue(target_type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Round(Value* value, RoundMode round_mode) {
  ASSERT_FLOAT_TYPE(value);

  if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->Round(round_mode);
    return dest;
  }

  Instr* i = AppendInstr(
      OPCODE_ROUND_info, round_mode,
      AllocValue(value->type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::VectorConvertI2F(Value* value) {
  ASSERT_VECTOR_TYPE(value);

  Instr* i = AppendInstr(
      OPCODE_VECTOR_CONVERT_I2F_info, 0,
      AllocValue(value->type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::VectorConvertF2I(Value* value, RoundMode round_mode) {
  ASSERT_VECTOR_TYPE(value);

  Instr* i = AppendInstr(
      OPCODE_VECTOR_CONVERT_F2I_info, round_mode,
      AllocValue(value->type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::LoadZero(TypeName type) {
  // TODO(benvanik): cache zeros per block/fn? Prevents tons of dupes.
  Value* dest = AllocValue();
  dest->set_zero(type);
  return dest;
}

Value* HIRBuilder::LoadConstant(int8_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* HIRBuilder::LoadConstant(uint8_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* HIRBuilder::LoadConstant(int16_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* HIRBuilder::LoadConstant(uint16_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* HIRBuilder::LoadConstant(int32_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* HIRBuilder::LoadConstant(uint32_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* HIRBuilder::LoadConstant(int64_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* HIRBuilder::LoadConstant(uint64_t value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* HIRBuilder::LoadConstant(float value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* HIRBuilder::LoadConstant(double value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* HIRBuilder::LoadConstant(const vec128_t& value) {
  Value* dest = AllocValue();
  dest->set_constant(value);
  return dest;
}

Value* HIRBuilder::LoadVectorShl(Value* sh) {
  XEASSERT(sh->type == INT8_TYPE);
  Instr* i = AppendInstr(
      OPCODE_LOAD_VECTOR_SHL_info, 0,
      AllocValue(VEC128_TYPE));
  i->set_src1(sh);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::LoadVectorShr(Value* sh) {
  XEASSERT(sh->type == INT8_TYPE);
  Instr* i = AppendInstr(
      OPCODE_LOAD_VECTOR_SHR_info, 0,
      AllocValue(VEC128_TYPE));
  i->set_src1(sh);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::LoadContext(size_t offset, TypeName type) {
  Instr* i = AppendInstr(
      OPCODE_LOAD_CONTEXT_info, 0,
      AllocValue(type));
  i->src1.offset = offset;
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

void HIRBuilder::StoreContext(size_t offset, Value* value) {
  Instr* i = AppendInstr(OPCODE_STORE_CONTEXT_info, 0);
  i->src1.offset = offset;
  i->set_src2(value);
  i->src3.value = NULL;
}

Value* HIRBuilder::Load(
    Value* address, TypeName type, uint32_t load_flags) {
  ASSERT_ADDRESS_TYPE(address);
  Instr* i = AppendInstr(
      OPCODE_LOAD_info, load_flags,
      AllocValue(type));
  i->set_src1(address);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::LoadAcquire(
    Value* address, TypeName type, uint32_t load_flags) {
  ASSERT_ADDRESS_TYPE(address);
  Instr* i = AppendInstr(
      OPCODE_LOAD_ACQUIRE_info, load_flags,
      AllocValue(type));
  i->set_src1(address);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

void HIRBuilder::Store(
    Value* address, Value* value, uint32_t store_flags) {
  ASSERT_ADDRESS_TYPE(address);
  Instr* i = AppendInstr(OPCODE_STORE_info, store_flags);
  i->set_src1(address);
  i->set_src2(value);
  i->src3.value = NULL;
}

Value* HIRBuilder::StoreRelease(
    Value* address, Value* value, uint32_t store_flags) {
  ASSERT_ADDRESS_TYPE(address);
  Instr* i = AppendInstr(OPCODE_STORE_RELEASE_info, store_flags,
      AllocValue(INT8_TYPE));
  i->set_src1(address);
  i->set_src2(value);
  i->src3.value = NULL;
  return i->dest;
}

void HIRBuilder::Prefetch(
    Value* address, size_t length, uint32_t prefetch_flags) {
  ASSERT_ADDRESS_TYPE(address);
  Instr* i = AppendInstr(OPCODE_PREFETCH_info, prefetch_flags);
  i->set_src1(address);
  i->src2.offset = length;
  i->src3.value = NULL;
}

Value* HIRBuilder::Max(Value* value1, Value* value2) {
  ASSERT_TYPES_EQUAL(value1, value2);

  if (value1->type != VEC128_TYPE &&
      value1->IsConstant() && value2->IsConstant()) {
    return value1->Compare(OPCODE_COMPARE_SLT, value2) ? value2 : value1;
  }

  Instr* i = AppendInstr(
      OPCODE_MAX_info, 0,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Min(Value* value1, Value* value2) {
  ASSERT_TYPES_EQUAL(value1, value2);

  if (value1->type != VEC128_TYPE &&
      value1->IsConstant() && value2->IsConstant()) {
    return value1->Compare(OPCODE_COMPARE_SLT, value2) ? value1 : value2;
  }

  Instr* i = AppendInstr(
      OPCODE_MIN_info, 0,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Select(Value* cond, Value* value1, Value* value2) {
  XEASSERT(cond->type == INT8_TYPE); // for now
  ASSERT_TYPES_EQUAL(value1, value2);

  if (cond->IsConstant()) {
    return cond->IsConstantTrue() ? value1 : value2;
  }

  Instr* i = AppendInstr(
      OPCODE_SELECT_info, 0,
      AllocValue(value1->type));
  i->set_src1(cond);
  i->set_src2(value1);
  i->set_src3(value2);
  return i->dest;
}

Value* HIRBuilder::IsTrue(Value* value) {
  if (value->IsConstant()) {
    return LoadConstant(value->IsConstantTrue() ? 1 : 0);
  }

  Instr* i = AppendInstr(
      OPCODE_IS_TRUE_info, 0,
      AllocValue(INT8_TYPE));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::IsFalse(Value* value) {
  if (value->IsConstant()) {
    return LoadConstant(value->IsConstantFalse() ? 1 : 0);
  }

  Instr* i = AppendInstr(
      OPCODE_IS_FALSE_info, 0,
      AllocValue(INT8_TYPE));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::CompareXX(
    const OpcodeInfo& opcode, Value* value1, Value* value2) {
  ASSERT_TYPES_EQUAL(value1, value2);
  if (value1->IsConstant() && value2->IsConstant()) {
    return LoadConstant(value1->Compare(opcode.num, value2) ? 1 : 0);
  }

  Instr* i = AppendInstr(
      opcode, 0,
      AllocValue(INT8_TYPE));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::CompareEQ(Value* value1, Value* value2) {
  return CompareXX(OPCODE_COMPARE_EQ_info, value1, value2);
}

Value* HIRBuilder::CompareNE(Value* value1, Value* value2) {
  return CompareXX(OPCODE_COMPARE_NE_info, value1, value2);
}

Value* HIRBuilder::CompareSLT(Value* value1, Value* value2) {
  return CompareXX(OPCODE_COMPARE_SLT_info, value1, value2);
}

Value* HIRBuilder::CompareSLE(Value* value1, Value* value2) {
  return CompareXX(OPCODE_COMPARE_SLE_info, value1, value2);
}

Value* HIRBuilder::CompareSGT(Value* value1, Value* value2) {
  return CompareXX(OPCODE_COMPARE_SGT_info, value1, value2);
}

Value* HIRBuilder::CompareSGE(Value* value1, Value* value2) {
  return CompareXX(OPCODE_COMPARE_SGE_info, value1, value2);
}

Value* HIRBuilder::CompareULT(Value* value1, Value* value2) {
  return CompareXX(OPCODE_COMPARE_ULT_info, value1, value2);
}

Value* HIRBuilder::CompareULE(Value* value1, Value* value2) {
  return CompareXX(OPCODE_COMPARE_ULE_info, value1, value2);
}

Value* HIRBuilder::CompareUGT(Value* value1, Value* value2) {
  return CompareXX(OPCODE_COMPARE_UGT_info, value1, value2);
}

Value* HIRBuilder::CompareUGE(Value* value1, Value* value2) {
  return CompareXX(OPCODE_COMPARE_UGE_info, value1, value2);
}

Value* HIRBuilder::DidCarry(Value* value) {
  Instr* i = AppendInstr(
      OPCODE_DID_CARRY_info, 0,
      AllocValue(INT8_TYPE));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::DidOverflow(Value* value) {
  Instr* i = AppendInstr(
      OPCODE_DID_OVERFLOW_info, 0,
      AllocValue(INT8_TYPE));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::VectorCompareXX(
    const OpcodeInfo& opcode, Value* value1, Value* value2,
    TypeName part_type) {
  ASSERT_TYPES_EQUAL(value1, value2);

  // TODO(benvanik): check how this is used - sometimes I think it's used to
  //     load bitmasks and may be worth checking constants on.

  Instr* i = AppendInstr(
      opcode, part_type,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::VectorCompareEQ(
    Value* value1, Value* value2, TypeName part_type) {
  return VectorCompareXX(
      OPCODE_VECTOR_COMPARE_EQ_info, value1, value2, part_type);
}

Value* HIRBuilder::VectorCompareSGT(
    Value* value1, Value* value2, TypeName part_type) {
  return VectorCompareXX(
      OPCODE_VECTOR_COMPARE_SGT_info, value1, value2, part_type);
}

Value* HIRBuilder::VectorCompareSGE(
    Value* value1, Value* value2, TypeName part_type) {
  return VectorCompareXX(
      OPCODE_VECTOR_COMPARE_SGE_info, value1, value2, part_type);
}

Value* HIRBuilder::VectorCompareUGT(
    Value* value1, Value* value2, TypeName part_type) {
  return VectorCompareXX(
      OPCODE_VECTOR_COMPARE_UGT_info, value1, value2, part_type);
}

Value* HIRBuilder::VectorCompareUGE(
    Value* value1, Value* value2, TypeName part_type) {
  return VectorCompareXX(
      OPCODE_VECTOR_COMPARE_UGE_info, value1, value2, part_type);
}

Value* HIRBuilder::Add(
    Value* value1, Value* value2, uint32_t arithmetic_flags) {
  ASSERT_TYPES_EQUAL(value1, value2);

  // TODO(benvanik): optimize when flags set.
  if (!arithmetic_flags) {
    if (value1->IsConstantZero()) {
      return value2;
    } else if (value2->IsConstantZero()) {
      return value1;
    } else if (value1->IsConstant() && value2->IsConstant()) {
      Value* dest = CloneValue(value1);
      dest->Add(value2);
      return dest;
    }
  }

  Instr* i = AppendInstr(
      OPCODE_ADD_info, arithmetic_flags,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::AddWithCarry(
    Value* value1, Value* value2, Value* value3,
    uint32_t arithmetic_flags) {
  ASSERT_TYPES_EQUAL(value1, value2);
  XEASSERT(value3->type == INT8_TYPE);

  Instr* i = AppendInstr(
      OPCODE_ADD_CARRY_info, arithmetic_flags,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->set_src3(value3);
  return i->dest;
}

Value* HIRBuilder::Sub(
    Value* value1, Value* value2, uint32_t arithmetic_flags) {
  ASSERT_TYPES_EQUAL(value1, value2);

  Instr* i = AppendInstr(
      OPCODE_SUB_info, arithmetic_flags,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Mul(Value* value1, Value* value2) {
  ASSERT_TYPES_EQUAL(value1, value2);

  Instr* i = AppendInstr(
      OPCODE_MUL_info, 0,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Div(Value* value1, Value* value2) {
  ASSERT_TYPES_EQUAL(value1, value2);

  Instr* i = AppendInstr(
      OPCODE_DIV_info, 0,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Rem(Value* value1, Value* value2) {
  ASSERT_TYPES_EQUAL(value1, value2);

  Instr* i = AppendInstr(
      OPCODE_REM_info, 0,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::MulAdd(Value* value1, Value* value2, Value* value3) {
  ASSERT_TYPES_EQUAL(value1, value2);
  ASSERT_TYPES_EQUAL(value1, value3);

  bool c1 = value1->IsConstant();
  bool c2 = value2->IsConstant();
  if (c1 && c2) {
    Value* dest = CloneValue(value1);
    dest->Mul(value2);
    return Add(dest, value3);
  }

  Instr* i = AppendInstr(
      OPCODE_MULADD_info, 0,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->set_src3(value3);
  return i->dest;
}

Value* HIRBuilder::MulSub(Value* value1, Value* value2, Value* value3) {
  ASSERT_TYPES_EQUAL(value1, value2);
  ASSERT_TYPES_EQUAL(value1, value3);

  bool c1 = value1->IsConstant();
  bool c2 = value2->IsConstant();
  if (c1 && c2) {
    Value* dest = CloneValue(value1);
    dest->Mul(value2);
    return Sub(dest, value3);
  }

  Instr* i = AppendInstr(
      OPCODE_MULSUB_info, 0,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->set_src3(value3);
  return i->dest;
}

Value* HIRBuilder::Neg(Value* value) {
  ASSERT_NON_VECTOR_TYPE(value);

  Instr* i = AppendInstr(
      OPCODE_NEG_info, 0,
      AllocValue(value->type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Abs(Value* value) {
  ASSERT_NON_VECTOR_TYPE(value);

  Instr* i = AppendInstr(
      OPCODE_ABS_info, 0,
      AllocValue(value->type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Sqrt(Value* value) {
  ASSERT_FLOAT_TYPE(value);

  Instr* i = AppendInstr(
      OPCODE_SQRT_info, 0,
      AllocValue(value->type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::RSqrt(Value* value) {
  ASSERT_FLOAT_TYPE(value);

  Instr* i = AppendInstr(
      OPCODE_RSQRT_info, 0,
      AllocValue(value->type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::DotProduct3(Value* value1, Value* value2) {
  ASSERT_VECTOR_TYPE(value1);
  ASSERT_VECTOR_TYPE(value2);
  ASSERT_TYPES_EQUAL(value1, value2);

  Instr* i = AppendInstr(
      OPCODE_DOT_PRODUCT_3_info, 0,
      AllocValue(FLOAT32_TYPE));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::DotProduct4(Value* value1, Value* value2) {
  ASSERT_VECTOR_TYPE(value1);
  ASSERT_VECTOR_TYPE(value2);
  ASSERT_TYPES_EQUAL(value1, value2);

  Instr* i = AppendInstr(
      OPCODE_DOT_PRODUCT_4_info, 0,
      AllocValue(FLOAT32_TYPE));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::And(Value* value1, Value* value2) {
  ASSERT_INTEGER_TYPE(value1);
  ASSERT_INTEGER_TYPE(value2);
  ASSERT_TYPES_EQUAL(value1, value2);

  if (value1 == value2) {
    return value1;
  } else if (value1->IsConstantZero()) {
    return value1;
  } else if (value2->IsConstantZero()) {
    return value2;
  }

  Instr* i = AppendInstr(
      OPCODE_AND_info, 0,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Or(Value* value1, Value* value2) {
  ASSERT_INTEGER_TYPE(value1);
  ASSERT_INTEGER_TYPE(value2);
  ASSERT_TYPES_EQUAL(value1, value2);

  if (value1 == value2) {
    return value1;
  } else if (value1->IsConstantZero()) {
    return value2;
  } else if (value2->IsConstantZero()) {
    return value1;
  }

  Instr* i = AppendInstr(
      OPCODE_OR_info, 0,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Xor(Value* value1, Value* value2) {
  ASSERT_INTEGER_TYPE(value1);
  ASSERT_INTEGER_TYPE(value2);
  ASSERT_TYPES_EQUAL(value1, value2);

  if (value1 == value2) {
    return LoadZero(value1->type);
  }

  Instr* i = AppendInstr(
      OPCODE_XOR_info, 0,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Not(Value* value) {
  ASSERT_INTEGER_TYPE(value);

  if (value->IsConstant()) {
    Value* dest = CloneValue(value);
    dest->Not();
    return dest;
  }

  Instr* i = AppendInstr(
      OPCODE_NOT_info, 0,
      AllocValue(value->type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Shl(Value* value1, Value* value2) {
  ASSERT_INTEGER_TYPE(value1);
  ASSERT_INTEGER_TYPE(value2);

  // NOTE AND value2 with 0x3F for 64bit, 0x1F for 32bit, etc..

  if (value2->IsConstantZero()) {
    return value1;
  }
  if (value2->type != INT8_TYPE) {
    value2 = Truncate(value2, INT8_TYPE);
  }

  Instr* i = AppendInstr(
      OPCODE_SHL_info, 0,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}
Value* HIRBuilder::Shl(Value* value1, int8_t value2) {
  return Shl(value1, LoadConstant(value2));
}

Value* HIRBuilder::VectorShl(Value* value1, Value* value2,
                                  TypeName part_type) {
  ASSERT_VECTOR_TYPE(value1);
  ASSERT_VECTOR_TYPE(value2);

  Instr* i = AppendInstr(
      OPCODE_VECTOR_SHL_info, part_type,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Shr(Value* value1, Value* value2) {
  ASSERT_INTEGER_TYPE(value1);
  ASSERT_INTEGER_TYPE(value2);

  if (value2->IsConstantZero()) {
    return value1;
  }
  if (value2->type != INT8_TYPE) {
    value2 = Truncate(value2, INT8_TYPE);
  }

  Instr* i = AppendInstr(
      OPCODE_SHR_info, 0,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}
Value* HIRBuilder::Shr(Value* value1, int8_t value2) {
  return Shr(value1, LoadConstant(value2));
}

Value* HIRBuilder::Sha(Value* value1, Value* value2) {
  ASSERT_INTEGER_TYPE(value1);
  ASSERT_INTEGER_TYPE(value2);

  if (value2->IsConstantZero()) {
    return value1;
  }
  if (value2->type != INT8_TYPE) {
    value2 = Truncate(value2, INT8_TYPE);
  }

  Instr* i = AppendInstr(
      OPCODE_SHA_info, 0,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}
Value* HIRBuilder::Sha(Value* value1, int8_t value2) {
  return Sha(value1, LoadConstant(value2));
}

Value* HIRBuilder::RotateLeft(Value* value1, Value* value2) {
  ASSERT_INTEGER_TYPE(value1);
  ASSERT_INTEGER_TYPE(value2);

  if (value2->IsConstantZero()) {
    return value1;
  }

  if (value2->type != INT8_TYPE) {
    value2 = Truncate(value2, INT8_TYPE);
  }

  Instr* i = AppendInstr(
      OPCODE_ROTATE_LEFT_info, 0,
      AllocValue(value1->type));
  i->set_src1(value1);
  i->set_src2(value2);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::ByteSwap(Value* value) {
  if (value->type == INT8_TYPE) {
    return value;
  }

  Instr* i = AppendInstr(
      OPCODE_BYTE_SWAP_info, 0,
      AllocValue(value->type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::CountLeadingZeros(Value* value) {
  ASSERT_INTEGER_TYPE(value);

  if (value->IsConstantZero()) {
    const static uint8_t zeros[] = { 8, 16, 32, 64, };
    return LoadConstant(zeros[value->type]);
  }

  Instr* i = AppendInstr(
      OPCODE_CNTLZ_info, 0,
      AllocValue(INT8_TYPE));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Insert(Value* value, Value* index, Value* part) {
  // TODO(benvanik): could do some of this as constants.

  Instr* i = AppendInstr(
      OPCODE_INSERT_info, 0,
      AllocValue(value->type));
  i->set_src1(value);
  i->set_src2(ZeroExtend(index, INT64_TYPE));
  i->set_src3(part);
  return i->dest;
}

Value* HIRBuilder::Insert(Value* value, uint64_t index, Value* part) {
  return Insert(value, LoadConstant(index), part);
}

Value* HIRBuilder::Extract(Value* value, Value* index,
                                TypeName target_type) {
  // TODO(benvanik): could do some of this as constants.

  Instr* i = AppendInstr(
      OPCODE_EXTRACT_info, 0,
      AllocValue(target_type));
  i->set_src1(value);
  i->set_src2(ZeroExtend(index, INT64_TYPE));
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Extract(Value* value, uint64_t index,
                                TypeName target_type) {
  return Extract(value, LoadConstant(index), target_type);
}

Value* HIRBuilder::Splat(Value* value, TypeName target_type) {
  // TODO(benvanik): could do some of this as constants.

  Instr* i = AppendInstr(
      OPCODE_SPLAT_info, 0,
      AllocValue(target_type));
  i->set_src1(value);
  i->src2.value = i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::Permute(
    Value* control, Value* value1, Value* value2, TypeName part_type) {
  ASSERT_TYPES_EQUAL(value1, value2);

  // TODO(benvanik): could do some of this as constants.

  Instr* i = AppendInstr(
      OPCODE_PERMUTE_info, part_type,
      AllocValue(value1->type));
  i->set_src1(control);
  i->set_src2(value1);
  i->set_src3(value2);
  return i->dest;
}

Value* HIRBuilder::Swizzle(
    Value* value, TypeName part_type, uint32_t swizzle_mask) {
  // For now.
  XEASSERT(part_type == INT32_TYPE || part_type == FLOAT32_TYPE);

  if (swizzle_mask == SWIZZLE_XYZW_TO_XYZW) {
    return Assign(value);
  }

  // TODO(benvanik): could do some of this as constants.

  Instr* i = AppendInstr(
      OPCODE_SWIZZLE_info, part_type,
      AllocValue(value->type));
  i->set_src1(value);
  i->src2.offset = swizzle_mask;
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::CompareExchange(
    Value* address, Value* compare_value, Value* exchange_value)  {
  ASSERT_ADDRESS_TYPE(address);
  ASSERT_INTEGER_TYPE(compare_value);
  ASSERT_INTEGER_TYPE(exchange_value);
  ASSERT_TYPES_EQUAL(compare_value, exchange_value);
  Instr* i = AppendInstr(
      OPCODE_COMPARE_EXCHANGE_info, 0,
      AllocValue(exchange_value->type));
  i->set_src1(address);
  i->set_src2(compare_value);
  i->set_src3(exchange_value);
  return i->dest;
}

Value* HIRBuilder::AtomicAdd(Value* address, Value* value) {
  ASSERT_ADDRESS_TYPE(address);
  ASSERT_INTEGER_TYPE(value);
  Instr* i = AppendInstr(
      OPCODE_ATOMIC_ADD_info, 0,
      AllocValue(value->type));
  i->set_src1(address);
  i->set_src2(value);
  i->src3.value = NULL;
  return i->dest;
}

Value* HIRBuilder::AtomicSub(Value* address, Value* value) {
  ASSERT_ADDRESS_TYPE(address);
  ASSERT_INTEGER_TYPE(value);
  Instr* i = AppendInstr(
      OPCODE_ATOMIC_SUB_info, 0,
      AllocValue(value->type));
  i->set_src1(address);
  i->set_src2(value);
  i->src3.value = NULL;
  return i->dest;
}