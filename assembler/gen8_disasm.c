/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <stdarg.h>

#include "brw_context.h"
#include "brw_defines.h"
#include "gen8_instruction.h"

static const struct opcode_desc *m_opcode = opcode_descs;

static const char *const m_conditional_modifier[16] = {
   /* [0 - BRW_CONDITIONAL_NONE] */ "",
   /* [1 - BRW_CONDITIONAL_Z]    */ ".e",
   /* [2 - BRW_CONDITIONAL_NZ]   */ ".ne",
   /* [3 - BRW_CONDITIONAL_G]    */ ".g",
   /* [4 - BRW_CONDITIONAL_GE]   */ ".ge",
   /* [5 - BRW_CONDITIONAL_L]    */ ".l",
   /* [6 - BRW_CONDITIONAL_LE]   */ ".le",
   /* [7 - Reserved]             */ NULL,
   /* [8 - BRW_CONDITIONAL_O]    */ ".o",
   /* [9 - BRW_CONDITIONAL_U]    */ ".u",
   /* [a-f - Reserved]           */
};

static const char *const m_negate[2] = { "", "-" };

static const char *const m_abs[2] = { "", "(abs)" };

static const char *const m_vert_stride[16] = {
   "0",
   "1",
   "2",
   "4",
   "8",
   "16",
   "32",
};

static const char *const width[8] = {
   "1",
   "2",
   "4",
   "8",
   "16",
};

static const char *const m_horiz_stride[4] = {
   "0",
   "1",
   "2",
   "4"
};

static const char *const m_chan_sel[4] = { "x", "y", "z", "w" };

static const char *const m_debug_ctrl[2] = { "", ".breakpoint" };

static const char *const m_saturate[2] = { "", ".sat" };

static const char *const m_accwr[2] = { "", "AccWrEnable" };

static const char *const m_maskctrl[2] = { "WE_normal", "WE_all" };

static const char *const m_exec_size[8] = {
   "1",
   "2",
   "4",
   "8",
   "16",
   "32",
};

static const char *const m_pred_inv[2] = { "+", "-" };

static const char *const m_pred_ctrl_align16[16] = {
   "",
   "",
   ".x",
   ".y",
   ".z",
   ".w",
   ".any4h",
   ".all4h",
};

static const char *const m_pred_ctrl_align1[16] = {
   "",
   "",
   ".anyv",
   ".allv",
   ".any2h",
   ".all2h",
   ".any4h",
   ".all4h",
   ".any8h",
   ".all8h",
   ".any16h",
   ".all16h",
   ".any32h",
   ".all32h",
};

static const char *const m_thread_ctrl[4] = {
   "",
   "atomic",
   "switch",
};

static const char *const m_dep_ctrl[4] = {
   "",
   "NoDDClr",
   "NoDDChk",
   "NoDDClr,NoDDChk",
};

static const char *const m_mask_ctrl[4] = {
   "",
   "nomask",
};

static const char *const m_access_mode[2] = { "align1", "align16" };

static const char *const m_reg_type[8] = {
   "UD",
   "D",
   "UW",
   "W",
   "UB",
   "B",
   "DF",
   "F",
};

static const int reg_type_size[8] = {
   /* UD */ 4,
   /* D  */ 4,
   /* UW */ 2,
   /* W  */ 2,
   /* UB */ 1,
   /* B  */ 1,
   /* DF */ 8,
   /* F  */ 4,
};

static const char *const m_reg_file[4] = {
   "A",
   "g",
   NULL,
   "imm",
};

static const char *const m_writemask[16] = {
   ".(none)",
   ".x",
   ".y",
   ".xy",
   ".z",
   ".xz",
   ".yz",
   ".xyz",
   ".w",
   ".xw",
   ".yw",
   ".xyw",
   ".zw",
   ".xzw",
   ".yzw",
   "",
};

static const char *const m_eot[2] = { "", "EOT" };

static const char *const m_sfid[16] = {
   /* [0 - BRW_SFID_NULL] */                     "null",
   /* [1 - Reserved] */                          NULL,
   /* [2 - BRW_SFID_SAMPLER] */                  "sampler",
   /* [3 - BRW_SFID_MESSAGE_GATEWAY] */          "gateway",
   /* [4 - GEN6_SFID_DATAPORT_SAMPLER_CACHE] */  "dp/sampler_cache",
   /* [5 - GEN6_SFID_DATAPORT_RENDER_CACHE] */   "dp/render_cache",
   /* [6 - BRW_SFID_URB] */                      "URB",
   /* [7 - BRW_SFID_THREAD_SPAWNER] */           "thread_spawner",
   /* [8 - BRW_SFID_VME] */                      "vme",
   /* [9 - GEN6_SFID_DATAPORT_CONSTANT_CACHE] */ "dp/constant_cache",
   /* [a - GEN7_SFID_DATAPORT_DATA_CACHE] */     "dp/data_cache",
   /* [b - GEN7_SFID_PI] */                      "pi",
   /* [c - HSW_SFID_DATAPORT_DATA_CACHE_1] */    "dp/data_cache:1",
   /* [d - HSW_SFID_CRE] */                      "cre",
   /* [e-f - Reserved */                         NULL, NULL,
};

#if 0
static const char *const dp_rc_msg_type[16] = {
   [BRW_DATAPORT_READ_MESSAGE_OWORD_BLOCK_READ] = "OWORD block read",
   [GEN6_DATAPORT_READ_MESSAGE_RENDER_UNORM_READ] = "RT UNORM read",
   [GEN6_DATAPORT_READ_MESSAGE_OWORD_DUAL_BLOCK_READ] = "OWORD dual block read",
   [GEN6_DATAPORT_READ_MESSAGE_MEDIA_BLOCK_READ] = "media block read",
   [GEN6_DATAPORT_READ_MESSAGE_OWORD_UNALIGN_BLOCK_READ] = "OWORD unaligned block read",
   [GEN6_DATAPORT_READ_MESSAGE_DWORD_SCATTERED_READ] = "DWORD scattered read",
   [GEN6_DATAPORT_WRITE_MESSAGE_DWORD_ATOMIC_WRITE] = "DWORD atomic write",
   [GEN6_DATAPORT_WRITE_MESSAGE_OWORD_BLOCK_WRITE] = "OWORD block write",
   [GEN6_DATAPORT_WRITE_MESSAGE_OWORD_DUAL_BLOCK_WRITE] = "OWORD dual block write",
   [GEN6_DATAPORT_WRITE_MESSAGE_MEDIA_BLOCK_WRITE] = "media block write",
   [GEN6_DATAPORT_WRITE_MESSAGE_DWORD_SCATTERED_WRITE] = "DWORD scattered write",
   [GEN6_DATAPORT_WRITE_MESSAGE_RENDER_TARGET_WRITE] = "RT write",
   [GEN6_DATAPORT_WRITE_MESSAGE_STREAMED_VB_WRITE] = "streamed VB write",
   [GEN6_DATAPORT_WRITE_MESSAGE_RENDER_TARGET_UNORM_WRITE] = "RT UNORMc write",
};
#endif

static const char *const m_math_function[16] = {
   /* [0 - Reserved]                                         */ NULL,
   /* [1 - BRW_MATH_FUNCTION_INV]                            */ "inv",
   /* [2 - BRW_MATH_FUNCTION_LOG]                            */ "log",
   /* [3 - BRW_MATH_FUNCTION_EXP]                            */ "exp",
   /* [4 - BRW_MATH_FUNCTION_SQRT]                           */ "sqrt",
   /* [5 - BRW_MATH_FUNCTION_RSQ]                            */ "rsq",
   /* [6 - BRW_MATH_FUNCTION_SIN]                            */ "sin",
   /* [7 - BRW_MATH_FUNCTION_COS]                            */ "cos",
   /* [8 - Reserved]                                         */ NULL,
   /* [9 - BRW_MATH_FUNCTION_FDIV]                           */ "fdiv",
   /* [a - BRW_MATH_FUNCTION_POW]                            */ "pow",
   /* [b - BRW_MATH_FUNCTION_INT_DIV_QUOTIENT_AND_REMAINDER] */ "intdivmod",
   /* [c - BRW_MATH_FUNCTION_INT_DIV_QUOTIENT]               */ "intdiv",
   /* [d - BRW_MATH_FUNCTION_INT_DIV_REMAINDER]              */ "intmod",
   /* [e - GEN8_MATH_FUNCTION_INVM]                          */ "invm",
   /* [f - GEN8_MATH_FUNCTION_RSQRTM]                        */ "rsqrtm",
};

static const char *const m_urb_opcode[16] = {
   /* [0] */ "write HWord",
   /* [1] */ "write OWord",
   /* [2] */ "read HWord",
   /* [3] */ "read OWord",
   /* [4] */ "atomic mov",
   /* [5] */ "atomic inc",
   /* [6] */ "atomic add",
   /* [7] */ "SIMD8 write",
   /* [8] */ "SIMD8 read",
   /* [9-15] - reserved */
};

static const char *const m_urb_interleave[2] = { "", "interleaved" };

static int column;

static int
string(FILE *file, const char *string)
{
   fputs(string, file);
   column += strlen(string);
   return 0;
}

static int
format(FILE *f, const char *format, ...)
{
   char buf[1024];
   va_list args;
   va_start(args, format);

   vsnprintf(buf, sizeof(buf) - 1, format, args);
   va_end(args);
   string(f, buf);
   return 0;
}

static int
newline(FILE *f)
{
   putc('\n', f);
   column = 0;
   return 0;
}

static int
pad(FILE *f, int c)
{
   do
      string(f, " ");
   while (column < c);
   return 0;
}

static int
control(FILE *file, const char *name, const char *const ctrl[],
        unsigned id, int *space)
{
   if (!ctrl[id]) {
      fprintf(file, "*** invalid %s value %d ", name, id);
      return 1;
   }
   if (ctrl[id][0])
   {
      if (space && *space)
         string(file, " ");
      string(file, ctrl[id]);
      if (space)
         *space = 1;
   }
   return 0;
}

static int
print_opcode(FILE *file, int id)
{
   if (!m_opcode[id].name) {
      format(file, "*** invalid opcode value %d ", id);
      return 1;
   }
   string(file, m_opcode[id].name);
   return 0;
}

static int
reg(FILE *file, unsigned reg_file, unsigned _reg_nr)
{
   int err = 0;

   if (reg_file == BRW_ARCHITECTURE_REGISTER_FILE) {
      switch (_reg_nr & 0xf0) {
      case BRW_ARF_NULL:
         string(file, "null");
         return -1;
      case BRW_ARF_ADDRESS:
         format(file, "a%d", _reg_nr & 0x0f);
         break;
      case BRW_ARF_ACCUMULATOR:
         format(file, "acc%d", _reg_nr & 0x0f);
         break;
      case BRW_ARF_FLAG:
         format(file, "f%d", _reg_nr & 0x0f);
         break;
      case BRW_ARF_MASK:
         format(file, "mask%d", _reg_nr & 0x0f);
         break;
      case BRW_ARF_MASK_STACK:
         format(file, "msd%d", _reg_nr & 0x0f);
         break;
      case BRW_ARF_STATE:
         format(file, "sr%d", _reg_nr & 0x0f);
         break;
      case BRW_ARF_CONTROL:
         format(file, "cr%d", _reg_nr & 0x0f);
         break;
      case BRW_ARF_NOTIFICATION_COUNT:
         format(file, "n%d", _reg_nr & 0x0f);
         break;
      case BRW_ARF_IP:
         string(file, "ip");
         return -1;
         break;
      default:
         format(file, "ARF%d", _reg_nr);
         break;
      }
   } else {
      err |= control(file, "src reg file", m_reg_file, reg_file, NULL);
      format(file, "%d", _reg_nr);
   }
   return err;
}

static int
dest(FILE *file, struct gen8_instruction *inst)
{
   int err = 0;

   if (gen8_access_mode(inst) == BRW_ALIGN_1)
   {
      assert(gen8_dst_address_mode(inst) == BRW_ADDRESS_DIRECT);
      err |= reg(file, gen8_dst_reg_file(inst), gen8_dst_da_reg_nr(inst));
      if (err == -1)
         return 0;
      if (gen8_dst_da1_subreg_nr(inst))
         format(file, ".%d", gen8_dst_da1_subreg_nr(inst) /
                reg_type_size[gen8_dst_reg_type(inst)]);
      string(file, "<");
      err |= control(file, "horiz stride", m_horiz_stride, gen8_dst_da1_hstride(inst), NULL);
      string(file, ">");
      err |= control(file, "dest reg encoding", m_reg_type, gen8_dst_reg_type(inst), NULL);
   }
   else
   {
      assert(gen8_dst_address_mode(inst) == BRW_ADDRESS_DIRECT);
      err |= reg(file, gen8_dst_reg_file(inst), gen8_dst_da_reg_nr(inst));
      if (err == -1)
         return 0;
      if (gen8_dst_da16_subreg_nr(inst))
         format(file, ".%d", gen8_dst_da16_subreg_nr(inst) /
                reg_type_size[gen8_dst_reg_type(inst)]);
      string(file, "<1>");
      err |= control(file, "writemask", m_writemask, gen8_da16_writemask(inst), NULL);
      err |= control(file, "dest reg encoding", m_reg_type, gen8_dst_reg_type(inst), NULL);
   }

   return 0;
}

#if 0
static int
dest_3src(FILE *file, gen8_instruction *inst)
{
   int      err = 0;
   uint32_t reg_file;

   if (inst->bits1.da3src.dest_reg_file)
      reg_file = BRW_MESSAGE_REGISTER_FILE;
   else
      reg_file = BRW_GENERAL_REGISTER_FILE;

   err |= reg(file, reg_file, inst->bits1.da3src.dest_reg_nr);
   if (err == -1)
      return 0;
   if (inst->bits1.da3src.dest_subreg_nr)
      format(file, ".%d", inst->bits1.da3src.dest_subreg_nr);
   string(file, "<1>");
   err |= control(file, "writemask", m_writemask, inst->bits1.da3src.dest_writemask, NULL);
   err |= control(file, "dest reg encoding", m_reg_type, BRW_REGISTER_TYPE_F, NULL);

   return 0;
}
#endif

static int
src_align1_region(FILE *file, unsigned vert_stride, unsigned _width,
                  unsigned horiz_stride)
{
   int err = 0;
   string(file, "<");
   err |= control(file, "vert stride", m_vert_stride, vert_stride, NULL);
   string(file, ",");
   err |= control(file, "width", width, _width, NULL);
   string(file, ",");
   err |= control(file, "horiz_stride", m_horiz_stride, horiz_stride, NULL);
   string(file, ">");
   return err;
}

static int
src_da1(FILE *file, unsigned type, unsigned reg_file,
        unsigned vert_stride, unsigned _width, unsigned horiz_stride,
        unsigned reg_num, unsigned sub_reg_num, unsigned _abs, unsigned negate)
{
   int err = 0;
   err |= control(file, "negate", m_negate, negate, NULL);
   err |= control(file, "abs", m_abs, _abs, NULL);

   err |= reg(file, reg_file, reg_num);
   if (err == -1)
      return 0;
   if (sub_reg_num)
      format(file, ".%d", sub_reg_num / reg_type_size[type]); /* use formal style like spec */
   src_align1_region(file, vert_stride, _width, horiz_stride);
   err |= control(file, "src reg encoding", m_reg_type, type, NULL);
   return err;
}

static int
src_da16(FILE *file,
         unsigned _reg_type,
         unsigned reg_file,
         unsigned vert_stride,
         unsigned _reg_nr,
         unsigned _subreg_nr,
         unsigned _abs,
         unsigned negate,
         unsigned swz_x,
         unsigned swz_y,
         unsigned swz_z,
         unsigned swz_w)
{
   int err = 0;
   err |= control(file, "negate", m_negate, negate, NULL);
   err |= control(file, "abs", m_abs, _abs, NULL);

   err |= reg(file, reg_file, _reg_nr);
   if (err == -1)
      return 0;
   if (_subreg_nr)
      /* bit4 for subreg number byte addressing. Make this same meaning as
         in da1 case, so output looks consistent. */
      format(file, ".%d", 16 / reg_type_size[_reg_type]);
   string(file, "<");
   err |= control(file, "vert stride", m_vert_stride, vert_stride, NULL);
   string(file, ",4,1>");
   /*
    * Three kinds of swizzle display:
    *  identity - nothing printed
    *  1->all       - print the single channel
    *  1->1    - print the mapping
    */
   if (swz_x == BRW_CHANNEL_X &&
      swz_y == BRW_CHANNEL_Y &&
      swz_z == BRW_CHANNEL_Z &&
      swz_w == BRW_CHANNEL_W)
   {
      ;
   }
   else if (swz_x == swz_y && swz_x == swz_z && swz_x == swz_w)
   {
      string(file, ".");
      err |= control(file, "channel select", m_chan_sel, swz_x, NULL);
   }
   else
   {
      string(file, ".");
      err |= control(file, "channel select", m_chan_sel, swz_x, NULL);
      err |= control(file, "channel select", m_chan_sel, swz_y, NULL);
      err |= control(file, "channel select", m_chan_sel, swz_z, NULL);
      err |= control(file, "channel select", m_chan_sel, swz_w, NULL);
   }
   err |= control(file, "src da16 reg type", m_reg_type, _reg_type, NULL);
   return err;
}

#if 0
static int
src0_3src(FILE *file, gen8_instruction *inst)
{
   int err = 0;
   unsigned swz_x = (inst->bits2.da3src.src0_swizzle >> 0) & 0x3;
   unsigned swz_y = (inst->bits2.da3src.src0_swizzle >> 2) & 0x3;
   unsigned swz_z = (inst->bits2.da3src.src0_swizzle >> 4) & 0x3;
   unsigned swz_w = (inst->bits2.da3src.src0_swizzle >> 6) & 0x3;

   err |= control(file, "negate", m_negate, inst->bits1.da3src.src0_negate, NULL);
   err |= control(file, "abs", m_abs, inst->bits1.da3src.src0_abs, NULL);

   err |= reg(file, BRW_GENERAL_REGISTER_FILE, inst->bits2.da3src.src0_reg_nr);
   if (err == -1)
      return 0;
   if (inst->bits2.da3src.src0_subreg_nr)
      format(file, ".%d", inst->bits2.da3src.src0_subreg_nr);
   string(file, "<4,1,1>");
   err |= control(file, "src da16 reg type", m_reg_type,
               BRW_REGISTER_TYPE_F, NULL);
   /*
    * Three kinds of swizzle display:
    *  identity - nothing printed
    *  1->all       - print the single channel
    *  1->1    - print the mapping
    */
   if (swz_x == BRW_CHANNEL_X &&
      swz_y == BRW_CHANNEL_Y &&
      swz_z == BRW_CHANNEL_Z &&
      swz_w == BRW_CHANNEL_W)
   {
      ;
   }
   else if (swz_x == swz_y && swz_x == swz_z && swz_x == swz_w)
   {
      string(file, ".");
      err |= control(file, "channel select", m_chan_sel, swz_x, NULL);
   }
   else
   {
      string(file, ".");
      err |= control(file, "channel select", m_chan_sel, swz_x, NULL);
      err |= control(file, "channel select", m_chan_sel, swz_y, NULL);
      err |= control(file, "channel select", m_chan_sel, swz_z, NULL);
      err |= control(file, "channel select", m_chan_sel, swz_w, NULL);
   }
   return err;
}

static int
src1_3src(FILE *file, gen8_instruction *inst)
{
   int err = 0;
   unsigned swz_x = (inst->bits2.da3src.src1_swizzle >> 0) & 0x3;
   unsigned swz_y = (inst->bits2.da3src.src1_swizzle >> 2) & 0x3;
   unsigned swz_z = (inst->bits2.da3src.src1_swizzle >> 4) & 0x3;
   unsigned swz_w = (inst->bits2.da3src.src1_swizzle >> 6) & 0x3;
   unsigned src1_subreg_nr = (inst->bits2.da3src.src1_subreg_nr_low |
                      (inst->bits3.da3src.src1_subreg_nr_high << 2));

   err |= control(file, "negate", m_negate, inst->bits1.da3src.src1_negate,
               NULL);
   err |= control(file, "abs", m_abs, inst->bits1.da3src.src1_abs, NULL);

   err |= reg(file, BRW_GENERAL_REGISTER_FILE,
            inst->bits3.da3src.src1_reg_nr);
   if (err == -1)
      return 0;
   if (src1_subreg_nr)
      format(file, ".%d", src1_subreg_nr);
   string(file, "<4,1,1>");
   err |= control(file, "src da16 reg type", m_reg_type,
               BRW_REGISTER_TYPE_F, NULL);
   /*
    * Three kinds of swizzle display:
    *  identity - nothing printed
    *  1->all       - print the single channel
    *  1->1    - print the mapping
    */
   if (swz_x == BRW_CHANNEL_X &&
      swz_y == BRW_CHANNEL_Y &&
      swz_z == BRW_CHANNEL_Z &&
      swz_w == BRW_CHANNEL_W)
   {
      ;
   }
   else if (swz_x == swz_y && swz_x == swz_z && swz_x == swz_w)
   {
      string(file, ".");
      err |= control(file, "channel select", m_chan_sel, swz_x, NULL);
   }
   else
   {
      string(file, ".");
      err |= control(file, "channel select", m_chan_sel, swz_x, NULL);
      err |= control(file, "channel select", m_chan_sel, swz_y, NULL);
      err |= control(file, "channel select", m_chan_sel, swz_z, NULL);
      err |= control(file, "channel select", m_chan_sel, swz_w, NULL);
   }
   return err;
}


static int
src2_3src(FILE *file, gen8_instruction *inst)
{
   int err = 0;
   unsigned swz_x = (inst->bits3.da3src.src2_swizzle >> 0) & 0x3;
   unsigned swz_y = (inst->bits3.da3src.src2_swizzle >> 2) & 0x3;
   unsigned swz_z = (inst->bits3.da3src.src2_swizzle >> 4) & 0x3;
   unsigned swz_w = (inst->bits3.da3src.src2_swizzle >> 6) & 0x3;

   err |= control(file, "negate", m_negate, inst->bits1.da3src.src2_negate,
               NULL);
   err |= control(file, "abs", m_abs, inst->bits1.da3src.src2_abs, NULL);

   err |= reg(file, BRW_GENERAL_REGISTER_FILE,
            inst->bits3.da3src.src2_reg_nr);
   if (err == -1)
      return 0;
   if (inst->bits3.da3src.src2_subreg_nr)
      format(file, ".%d", inst->bits3.da3src.src2_subreg_nr);
   string(file, "<4,1,1>");
   err |= control(file, "src da16 reg type", m_reg_type,
               BRW_REGISTER_TYPE_F, NULL);
   /*
    * Three kinds of swizzle display:
    *  identity - nothing printed
    *  1->all       - print the single channel
    *  1->1    - print the mapping
    */
   if (swz_x == BRW_CHANNEL_X &&
      swz_y == BRW_CHANNEL_Y &&
      swz_z == BRW_CHANNEL_Z &&
      swz_w == BRW_CHANNEL_W)
   {
      ;
   }
   else if (swz_x == swz_y && swz_x == swz_z && swz_x == swz_w)
   {
      string(file, ".");
      err |= control(file, "channel select", m_chan_sel, swz_x, NULL);
   }
   else
   {
      string(file, ".");
      err |= control(file, "channel select", m_chan_sel, swz_x, NULL);
      err |= control(file, "channel select", m_chan_sel, swz_y, NULL);
      err |= control(file, "channel select", m_chan_sel, swz_z, NULL);
      err |= control(file, "channel select", m_chan_sel, swz_w, NULL);
   }
   return err;
}
#endif

static int
imm(FILE *file, unsigned type, struct gen8_instruction *inst)
{
   switch (type) {
   case BRW_REGISTER_TYPE_UD:
      format(file, "0x%08xUD", gen8_src1_imm_ud(inst));
      break;
   case BRW_REGISTER_TYPE_D:
      format(file, "%dD", (int) gen8_src1_imm_d(inst));
      break;
   case BRW_REGISTER_TYPE_UW:
      format(file, "0x%04xUW", (uint16_t) gen8_src1_imm_ud(inst));
      break;
   case BRW_REGISTER_TYPE_W:
      format(file, "%dW", (int16_t) gen8_src1_imm_d(inst));
      break;
   case BRW_REGISTER_TYPE_UB:
      format(file, "0x%02xUB", (int8_t) gen8_src1_imm_ud(inst));
      break;
   case BRW_REGISTER_TYPE_VF:
      format(file, "Vector Float");
      break;
   case BRW_REGISTER_TYPE_V:
      format(file, "0x%08xV", gen8_src1_imm_ud(inst));
      break;
   case BRW_REGISTER_TYPE_F:
      format(file, "%-gF", gen8_src1_imm_f(inst));
   }
   return 0;
}

static int
src0(FILE *file, struct gen8_instruction *inst)
{
   if (gen8_src0_reg_file(inst) == BRW_IMMEDIATE_VALUE)
      return imm(file, gen8_src0_reg_type(inst), inst);

   if (gen8_access_mode(inst) == BRW_ALIGN_1)
   {
      assert(gen8_src0_address_mode(inst) == BRW_ADDRESS_DIRECT);
      return src_da1(file,
                     gen8_src0_reg_type(inst),
                     gen8_src0_reg_file(inst),
                     gen8_src0_vert_stride(inst),
                     gen8_src0_da1_width(inst),
                     gen8_src0_da1_hstride(inst),
                     gen8_src0_da_reg_nr(inst),
                     gen8_src0_da1_subreg_nr(inst),
                     gen8_src0_abs(inst),
                     gen8_src0_negate(inst));
   }
   else
   {
      assert(gen8_src0_address_mode(inst) == BRW_ADDRESS_DIRECT);
      return src_da16(file,
                      gen8_src0_reg_type(inst),
                      gen8_src0_reg_file(inst),
                      gen8_src0_vert_stride(inst),
                      gen8_src0_da_reg_nr(inst),
                      gen8_src0_da16_subreg_nr(inst),
                      gen8_src0_abs(inst),
                      gen8_src0_negate(inst),
                      gen8_src0_da16_swiz_x(inst),
                      gen8_src0_da16_swiz_y(inst),
                      gen8_src0_da16_swiz_z(inst),
                      gen8_src0_da16_swiz_w(inst));
   }
}

static int
src1(FILE *file, struct gen8_instruction *inst)
{
   if (gen8_src1_reg_file(inst) == BRW_IMMEDIATE_VALUE)
      return imm(file, gen8_src1_reg_type(inst), inst);

   if (gen8_access_mode(inst) == BRW_ALIGN_1)
   {
      assert(gen8_src1_address_mode(inst) == BRW_ADDRESS_DIRECT);
      return src_da1(file,
                     gen8_src1_reg_type(inst),
                     gen8_src1_reg_file(inst),
                     gen8_src1_vert_stride(inst),
                     gen8_src1_da1_width(inst),
                     gen8_src1_da1_hstride(inst),
                     gen8_src1_da_reg_nr(inst),
                     gen8_src1_da1_subreg_nr(inst),
                     gen8_src1_abs(inst),
                     gen8_src1_negate(inst));
   }
   else
   {
      assert(gen8_src1_address_mode(inst) == BRW_ADDRESS_DIRECT);
      return src_da16(file,
                      gen8_src1_reg_type(inst),
                      gen8_src1_reg_file(inst),
                      gen8_src1_vert_stride(inst),
                      gen8_src1_da_reg_nr(inst),
                      gen8_src1_da16_subreg_nr(inst),
                      gen8_src1_abs(inst),
                      gen8_src1_negate(inst),
                      gen8_src1_da16_swiz_x(inst),
                      gen8_src1_da16_swiz_y(inst),
                      gen8_src1_da16_swiz_z(inst),
                      gen8_src1_da16_swiz_w(inst));
   }
}

static int esize[6] = { 1, 2, 4, 8, 16, 32 };

static int
qtr_ctrl(FILE *file, struct gen8_instruction *inst)
{
   int qtr_ctl = gen8_qtr_control(inst);
   int exec_size = esize[gen8_exec_size(inst)];

   if (exec_size == 8) {
      switch (qtr_ctl) {
      case 0:
         string(file, " 1Q");
         break;
      case 1:
         string(file, " 2Q");
         break;
      case 2:
         string(file, " 3Q");
         break;
      case 3:
         string(file, " 4Q");
         break;
      }
   } else if (exec_size == 16) {
      if (qtr_ctl < 2)
         string(file, " 1H");
      else
         string(file, " 2H");
   }
   return 0;
}

int
gen8_disassemble(FILE *file, struct gen8_instruction *insn, int gen)
{
   int err = 0;
   int space = 0;

   const int opcode = gen8_opcode(insn);

   if (gen8_pred_control(insn)) {
      string(file, "(");
      err |= control(file, "predicate inverse", m_pred_inv, gen8_pred_inv(insn), NULL);
      format(file, "f%d", gen8_flag_reg_nr(insn));
      if (gen8_flag_subreg_nr(insn))
         format(file, ".%d", gen8_flag_subreg_nr(insn));
      if (gen8_access_mode(insn) == BRW_ALIGN_1) {
         err |= control(file, "predicate control align1", m_pred_ctrl_align1,
                        gen8_pred_control(insn), NULL);
      } else {
         err |= control(file, "predicate control align16", m_pred_ctrl_align16,
                        gen8_pred_control(insn), NULL);
      }
      string(file, ") ");
   }

   err |= print_opcode(file, opcode);
   err |= control(file, "saturate", m_saturate, gen8_saturate(insn), NULL);
   err |= control(file, "debug control", m_debug_ctrl, gen8_debug_control(insn), NULL);

   if (opcode == BRW_OPCODE_MATH) {
      string(file, " ");
      err |= control(file, "function", m_math_function, gen8_math_function(insn),
                     NULL);
   } else if (opcode != BRW_OPCODE_SEND && opcode != BRW_OPCODE_SENDC) {
      err |= control(file, "conditional modifier", m_conditional_modifier,
                     gen8_cond_modifier(insn), NULL);

      /* If we're using the conditional modifier, print the flag reg used. */
      if (gen8_cond_modifier(insn) && opcode != BRW_OPCODE_SEL) {
         format(file, ".f%d", gen8_flag_reg_nr(insn));
         if (gen8_flag_subreg_nr(insn))
            format(file, ".%d", gen8_flag_subreg_nr(insn));
      }
   }

   if (opcode != BRW_OPCODE_NOP) {
      string(file, "(");
      err |= control(file, "execution size", m_exec_size, gen8_exec_size(insn), NULL);
      string(file, ")");
   }

   if (m_opcode[opcode].nsrc == 3) {
      string(file, "XXX: 3-src");
      #if 0
      pad(file, 16);
      err |= dest_3src(file, this);

      pad(file, 32);
      err |= src0_3src(file, this);

      pad(file, 48);
      err |= src1_3src(file, this);

      pad(file, 64);
      err |= src2_3src(file, this);
      #endif
   } else {
      if (m_opcode[opcode].ndst > 0) {
         pad(file, 16);
         err |= dest(file, insn);
      } else if (opcode == BRW_OPCODE_ENDIF) {
         format(file, " %d", gen8_jip(insn));
      } else if (opcode == BRW_OPCODE_IF ||
                 opcode == BRW_OPCODE_ELSE ||
                 opcode == BRW_OPCODE_WHILE ||
                 opcode == BRW_OPCODE_BREAK ||
                 opcode == BRW_OPCODE_CONTINUE ||
                 opcode == BRW_OPCODE_HALT) {
         format(file, " %d %d", gen8_jip(insn), gen8_uip(insn));
      }

      if (m_opcode[opcode].nsrc > 0) {
         pad(file, 32);
         err |= src0(file, insn);
      }
      if (m_opcode[opcode].nsrc > 1) {
         pad(file, 48);
         err |= src1(file, insn);
      }
   }

   if (opcode == BRW_OPCODE_SEND || opcode == BRW_OPCODE_SENDC) {
      const int sfid = gen8_sfid(insn);

      newline(file);
      pad(file, 16);
      space = 0;

      err |= control(file, "SFID", m_sfid, sfid, &space);

      switch (sfid) {
      case BRW_SFID_SAMPLER:
         format(file, " (%d, %d, %d, %d)",
                gen8_binding_table_index(insn),
                gen8_sampler(insn),
                gen8_sampler_msg_type(insn),
                gen8_sampler_simd_mode(insn));
         break;

      case BRW_SFID_URB:
         space = 1;
         err |= control(file, "urb opcode", m_urb_opcode,
                        gen8_urb_opcode(insn), &space);
         err |= control(file, "urb interleave", m_urb_interleave,
                        gen8_urb_interleave(insn), &space);
         format(file, " %d %d",
                gen8_urb_global_offset(insn), gen8_urb_per_slot_offset(insn));
         break;

      case GEN6_SFID_DATAPORT_SAMPLER_CACHE:
      case GEN6_SFID_DATAPORT_RENDER_CACHE:
      case GEN6_SFID_DATAPORT_CONSTANT_CACHE:
      case GEN7_SFID_DATAPORT_DATA_CACHE:
         format(file, " (%d, 0x%x)",
               gen8_binding_table_index(insn),
               gen8_function_control(insn));
         break;

      default:
         format(file, "unsupported shared function ID (%d)", sfid);
         break;
      }
      if (space)
         string(file, " ");
      format(file, "mlen %d", gen8_mlen(insn));
      format(file, " rlen %d", gen8_rlen(insn));
   }
   pad(file, 64);
   if (opcode != BRW_OPCODE_NOP) {
      string(file, "{");
      space = 1;
      err |= control(file, "access mode", m_access_mode, gen8_access_mode(insn), &space);
      err |= control(file, "mask control", m_maskctrl, gen8_mask_control(insn), &space);
      err |= control(file, "dependency control", m_dep_ctrl, gen8_dep_control(insn), &space);

      err |= qtr_ctrl(file, insn);

      err |= control(file, "thread control", m_thread_ctrl, gen8_thread_control(insn), &space);
      err |= control(file, "acc write control", m_accwr, gen8_acc_wr_control(insn), &space);
      if (opcode == BRW_OPCODE_SEND || opcode == BRW_OPCODE_SENDC)
         err |= control(file, "end of thread", m_eot, gen8_eot(insn), &space);
      if (space)
         string(file, " ");
      string(file, "}");
   }
   string(file, ";");
   newline(file);
   return err;
}
