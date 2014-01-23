/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/** @file gen8_instruction.h
 *
 * A representation of a Gen8+ EU instruction, with helper methods to get
 * and set various fields.  This is the actual hardware format.
 */

#ifndef GEN8_INSTRUCTION_H
#define GEN8_INSTRUCTION_H

#include <stdio.h>
#include <stdint.h>

#include "brw_compat.h"
#include "brw_reg.h"

struct gen8_instruction {
   uint32_t data[4];
};

static inline unsigned gen8_bits(struct gen8_instruction *insn,
				 unsigned high,
				 unsigned low);
static inline void gen8_set_bits(struct gen8_instruction *insn,
				 unsigned high,
				 unsigned low,
				 unsigned value);

#define F(name, high, low) \
   static inline void gen8_set_##name(struct gen8_instruction *insn, unsigned v) \
   { \
      gen8_set_bits(insn, high, low, v); \
   } \
   static inline unsigned gen8_##name(struct gen8_instruction *insn) \
   { \
      return gen8_bits(insn, high, low); \
   }

/**
* Direct addressing only:
*  @{
*/
F(src1_da_reg_nr,      108, 101);
F(src0_da_reg_nr,       76,  69);
F(dst_da1_hstride,      62,  61);
F(dst_da_reg_nr,        60,  53);
F(dst_da16_subreg_nr,   52,  52);
F(dst_da1_subreg_nr,    52,  48);
F(da16_writemask,       51,  48); /* Dst.ChanEn */
/** @} */

F(src1_vert_stride,    120, 117)
F(src1_da1_width,      116, 114)
F(src1_da16_swiz_w,    115, 114)
F(src1_da16_swiz_z,    113, 112)
F(src1_da1_hstride,    113, 112)
F(src1_address_mode,   111, 111)
/** Src1.SrcMod @{ */
F(src1_negate,         110, 110)
F(src1_abs,            109, 109)
/** @} */
F(src1_da16_subreg_nr, 100, 100)
F(src1_da1_subreg_nr,  100,  96)
F(src1_da16_swiz_y,     99,  98)
F(src1_da16_swiz_x,     97,  96)
F(src1_reg_type,        94,  91)
F(src1_reg_file,        90,  89)
F(src0_vert_stride,     88,  85)
F(src0_da1_width,       84,  82)
F(src0_da16_swiz_w,     83,  82)
F(src0_da16_swiz_z,     81,  80)
F(src0_da1_hstride,     81,  80)
F(src0_address_mode,    79,  79)
/** Src0.SrcMod @{ */
F(src0_negate,          78,  78)
F(src0_abs,             77,  77)
/** @} */
F(src0_da16_subreg_nr,  68,  68)
F(src0_da1_subreg_nr,   68,  64)
F(src0_da16_swiz_y,     67,  66)
F(src0_da16_swiz_x,     65,  64)
F(dst_address_mode,     63,  63)
F(src0_reg_type,        46,  43)
F(src0_reg_file,        42,  41)
F(dst_reg_type,         40,  37)
F(dst_reg_file,         36,  35)
F(mask_control,         34,  34)
F(flag_reg_nr,          33,  33)
F(flag_subreg_nr,       32,  32)
F(saturate,             31,  31)
F(branch_control,       30,  30)
F(debug_control,        30,  30)
F(cmpt_control,         29,  29)
F(acc_wr_control,       28,  28)
F(cond_modifier,        27,  24)
F(exec_size,            23,  21)
F(pred_inv,             20,  20)
F(pred_control,         19,  16)
F(thread_control,       15,  14)
F(qtr_control,          13,  12)
F(nib_control,          11,  11)
F(dep_control,          10,   9)
F(access_mode,           8,   8)
/* Bit 7 is Reserve d (for future Opcode expansion) */
F(opcode,                6,   0)

/**
* Three-source instructions:
*  @{
*/
F(src2_3src_reg_nr,    125, 118)
F(src2_3src_subreg_nr, 117, 115)
F(src2_3src_swizzle,   114, 107)
F(src2_3src_rep_ctrl,  106, 106)
F(src1_3src_reg_nr,    104,  97)
F(src1_3src_subreg_hi,  96,  96)
F(src1_3src_subreg_lo,  95,  94)
F(src1_3src_swizzle,    93,  86)
F(src1_3src_rep_ctrl,   85,  85)
F(src0_3src_reg_nr,     83,  76)
F(src0_3src_subreg_nr,  75,  73)
F(src0_3src_swizzle,    72,  65)
F(src0_3src_rep_ctrl,   64,  64)
F(dst_3src_reg_nr,      63,  56)
F(dst_3src_subreg_nr,   55,  53)
F(dst_3src_writemask,   52,  49)
F(dst_3src_type,        48,  46)
F(src_3src_type,        45,  43)
F(src2_3src_negate,     42,  42)
F(src2_3src_abs,        41,  41)
F(src1_3src_negate,     40,  40)
F(src1_3src_abs,        39,  39)
F(src0_3src_negate,     38,  38)
F(src0_3src_abs,        37,  37)
/** @} */

/**
* Fields for SEND messages:
*  @{
*/
F(eot,                 127, 127)
F(mlen,                124, 121)
F(rlen,                120, 116)
F(header_present,      115, 115)
F(function_control,    114,  96)
F(sfid,                 27,  24)
F(math_function,        27,  24)
/** @} */

/**
* URB message function control bits:
*  @{
*/
F(urb_per_slot_offset, 113, 113)
F(urb_interleave,      111, 111)
F(urb_global_offset,   110, 100)
F(urb_opcode,           99,  96)
/** @} */

/**
* Sampler message function control bits:
*  @{
*/
F(sampler_simd_mode,   114, 113)
F(sampler_msg_type,    112, 108)
F(sampler,             107, 104)
F(binding_table_index, 103,  96)
/** @} */

/**
 * Data port message function control bits:
 *  @ {
 */
F(dp_category,            114, 114)
F(dp_message_type,        113, 110)
F(dp_message_control,     109, 104)
F(dp_binding_table_index, 103,  96)
/** @} */

/**
 * Thread Spawn message function control bits:
 *  @ {
 */
F(ts_resource_select,     100, 100)
F(ts_request_type,         97,  97)
F(ts_opcode,               96,  96)
/** @} */

/**
 * Video Motion Estimation message function control bits:
 *  @ {
 */
F(vme_message_type,        110, 109)
F(vme_binding_table_index, 103,  96)
/** @} */

/**
 * Check & Refinement Engine message function control bits:
 *  @ {
 */
F(cre_message_type,        110, 109)
F(cre_binding_table_index, 103,  96)
/** @} */

/* Addr Mode */

F(dst_addr_mode,	  63, 63)
F(src0_addr_mode,	  79, 79)
F(src1_addr_mode,	  111, 111)

/* Indirect access mode for Align1. */
F(dst_ida1_sub_nr,        60,  57)
F(src0_ida1_sub_nr,       76,  73)
F(src1_ida1_sub_nr,      108, 105)

/* Imm[8:0] of Immediate addr offset under Indirect mode */
F(dst_ida1_imm8,         56,  48)
F(src0_ida1_imm8,        72,  64)
F(src1_ida1_imm8,        104,  96)

/* Imm Bit9 of Immediate addr offset under Indirect mode */
F(dst_ida1_imm9,         47,  47)
F(src0_ida1_imm9,        95,  95)
F(src1_ida1_imm9,        121, 121)

#undef F

#define IMM8_MASK	0x1FF
#define IMM9_MASK	0x200

/**
* Flow control instruction bits:
*  @{
*/
static inline unsigned gen8_uip(struct gen8_instruction *insn)
{
   return insn->data[2];
}
static inline void gen8_set_uip(struct gen8_instruction *insn, unsigned uip)
{
   insn->data[2] = uip;
}
static inline unsigned gen8_jip(struct gen8_instruction *insn)
{
   return insn->data[3];
}
static inline void gen8_set_jip(struct gen8_instruction *insn, unsigned jip)
{
   insn->data[3] = jip;
}
/** @} */

static inline int gen8_src1_imm_d(struct gen8_instruction *insn)
{
   return insn->data[3];
}
static inline unsigned gen8_src1_imm_ud(struct gen8_instruction *insn)
{
   return insn->data[3];
}
static inline float gen8_src1_imm_f(struct gen8_instruction *insn)
{
   fi_type ft;

   ft.u = insn->data[3];
   return ft.f;
}

void gen8_set_dst(struct gen8_instruction *insn, struct brw_reg reg);
void gen8_set_src0(struct gen8_instruction *insn, struct brw_reg reg);
void gen8_set_src1(struct gen8_instruction *insn, struct brw_reg reg);

void gen8_set_urb_message(struct gen8_instruction *insn,
			  unsigned opcode, unsigned mlen, unsigned rlen,
			  bool eot, unsigned offset, bool interleave);

void gen8_set_sampler_message(struct gen8_instruction *insn,
			      unsigned binding_table_index, unsigned sampler,
			      unsigned msg_type, unsigned rlen, unsigned mlen,
			      bool header_present, unsigned simd_mode);

void gen8_set_dp_message(struct gen8_instruction *insn,
			 enum brw_message_target sfid,
			 unsigned binding_table_index,
			 unsigned msg_type,
			 unsigned msg_control,
			 unsigned msg_length,
			 unsigned response_length,
			 bool header_present,
			 bool end_of_thread);

/** Disassemble the instruction. */
int gen8_disassemble(FILE *file, struct gen8_instruction *insn, int gen);


/**
 * Fetch a set of contiguous bits from the instruction.
 *
 * Bits indexes range from 0..127; fields may not cross 32-bit boundaries.
 */
static inline unsigned
gen8_bits(struct gen8_instruction *insn, unsigned high, unsigned low)
{
   /* We assume the field doesn't cross 32-bit boundaries. */
   const unsigned word = high / 32;
   assert(word == low / 32);

   high %= 32;
   low %= 32;

   const unsigned mask = (((1 << (high - low + 1)) - 1) << low);

   return (insn->data[word] & mask) >> low;
}

/**
 * Set bits in the instruction, with proper shifting and masking.
 *
 * Bits indexes range from 0..127; fields may not cross 32-bit boundaries.
 */
static inline void
gen8_set_bits(struct gen8_instruction *insn,
	      unsigned high,
	      unsigned low,
	      unsigned value)
{
   const unsigned word = high / 32;
   assert(word == low / 32);

   high %= 32;
   low %= 32;

   const unsigned mask = (((1 << (high - low + 1)) - 1) << low);

   insn->data[word] = (insn->data[word] & ~mask) | ((value << low) & mask);
}

void gen9_set_send_extdesc(struct gen8_instruction *insn, unsigned int value);

#endif
