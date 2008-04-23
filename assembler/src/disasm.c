/*
 * Copyright © 2008 Keith Packard
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

#include "gen4asm.h"
#include "brw_defines.h"

struct {
    char    *name;
    int	    nsrc;
    int	    ndst;
} opcode[128] = {
    [BRW_OPCODE_MOV] = { .name = "mov", .nsrc = 1, .ndst = 1 },
    [BRW_OPCODE_FRC] = { .name = "frc", .nsrc = 1, .ndst = 1 },
    [BRW_OPCODE_RNDU] = { .name = "rndu", .nsrc = 1, .ndst = 1 },
    [BRW_OPCODE_RNDD] = { .name = "rndd", .nsrc = 1, .ndst = 1 },
    [BRW_OPCODE_RNDE] = { .name = "rnde", .nsrc = 1, .ndst = 1 },
    [BRW_OPCODE_RNDZ] = { .name = "rndz", .nsrc = 1, .ndst = 1 },
    [BRW_OPCODE_NOT] = { .name = "not", .nsrc = 1, .ndst = 1 },
    [BRW_OPCODE_LZD] = { .name = "lzd", .nsrc = 1, .ndst = 1 },

    [BRW_OPCODE_MUL] = { .name = "mul", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_MAC] = { .name = "mac", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_MACH] = { .name = "mach", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_LINE] = { .name = "line", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_SAD2] = { .name = "sad2", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_SADA2] = { .name = "sada2", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_DP4] = { .name = "dp4", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_DPH] = { .name = "dph", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_DP3] = { .name = "dp3", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_DP2] = { .name = "dp2", .nsrc = 2, .ndst = 1 },

    [BRW_OPCODE_AVG] = { .name = "avg", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_ADD] = { .name = "add", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_SEL] = { .name = "sel", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_AND] = { .name = "and", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_OR] = { .name = "or", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_XOR] = { .name = "xor", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_SHR] = { .name = "shr", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_SHL] = { .name = "shl", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_ASR] = { .name = "asr", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_CMP] = { .name = "cmp", .nsrc = 2, .ndst = 1 },
    [BRW_OPCODE_CMPN] = { .name = "cmpn", .nsrc = 2, .ndst = 1 },

    [BRW_OPCODE_SEND] = { .name = "send", .nsrc = 1, .ndst = 1 },
    [BRW_OPCODE_NOP] = { .name = "nop", .nsrc = 0, .ndst = 0 },
    [BRW_OPCODE_JMPI] = { .name = "jmpi", .nsrc = 1, .ndst = 0 },
    [BRW_OPCODE_IF] = { .name = "if", .nsrc = 2, .ndst = 0 },
    [BRW_OPCODE_IFF] = { .name = "iff", .nsrc = 1, .ndst = 01 },
    [BRW_OPCODE_WHILE] = { .name = "while", .nsrc = 1, .ndst = 0 },
    [BRW_OPCODE_ELSE] = { .name = "else", .nsrc = 1, .ndst = 0 },
    [BRW_OPCODE_BREAK] = { .name = "break", .nsrc = 1, .ndst = 0 },
    [BRW_OPCODE_CONTINUE] = { .name = "cont", .nsrc = 1, .ndst = 0 },
    [BRW_OPCODE_HALT] = { .name = "halt", .nsrc = 1, .ndst = 0 },
    [BRW_OPCODE_MSAVE] = { .name = "msave", .nsrc = 1, .ndst = 1 },
    [BRW_OPCODE_PUSH] = { .name = "push", .nsrc = 1, .ndst = 1 },
    [BRW_OPCODE_MRESTORE] = { .name = "mrest", .nsrc = 1, .ndst = 1 },
    [BRW_OPCODE_POP] = { .name = "pop", .nsrc = 2, .ndst = 0 },
    [BRW_OPCODE_WAIT] = { .name = "wait", .nsrc = 1, .ndst = 0 },
    [BRW_OPCODE_DO] = { .name = "do", .nsrc = 0, .ndst = 0 },
    [BRW_OPCODE_ENDIF] = { .name = "endif", .nsrc = 0, .ndst = 0 },
};

char *conditional_modifier[16] = {
    [BRW_CONDITIONAL_NONE] = "",
    [BRW_CONDITIONAL_Z] = ".Z",
    [BRW_CONDITIONAL_NZ] = ".NZ",
    [BRW_CONDITIONAL_EQ] = ".EQ",
    [BRW_CONDITIONAL_NEQ] = ".NEQ",
    [BRW_CONDITIONAL_G] = ".G",
    [BRW_CONDITIONAL_GE] = ".GE",
    [BRW_CONDITIONAL_L] = ".L",
    [BRW_CONDITIONAL_LE] = ".LE",
    [BRW_CONDITIONAL_C] = ".C",
    [BRW_CONDITIONAL_O] = ".O",
    [BRW_CONDITIONAL_U] = ".U",
};

char *negate[2] = {
    [0] = "",
    [1] = "-",
};

char *_abs[2] = {
    [0] = "",
    [1] = "(abs)",
};

char *vert_stride[16] = {
    [0] = "0",
    [1] = "1",
    [2] = "2",
    [3] = "4",
    [4] = "8",
    [5] = "16",
    [6] = "32",
    [15] = "VxH",
};

char *width[8] = {
    [0] = "1",
    [1] = "2",
    [2] = "4",
    [3] = "8",
    [4] = "16",
};

char *horiz_stride[4] = {
    [0] = "0",
    [1] = "1",
    [2] = "2",
    [3] = "4"
};

char *chan_sel[4] = {
    [0] = "x",
    [1] = "y",
    [2] = "z",
    [3] = "w",
};

char *dest_condmod[16] = {
};

char *debug_ctrl[2] = {
    [0] = "",
    [1] = ".breakpoint"
};

char *saturate[2] = {
    [0] = "",
    [1] = ".sat"
};

char *exec_size[8] = {
    [0] = "1",
    [1] = "2",
    [2] = "4",
    [3] = "8",
    [4] = "16",
    [5] = "32"
};

char *pred_inv[2] = {
    [0] = "+",
    [1] = "-"
};

char *pred_ctrl_align16[16] = {
    [0] = "",
    [1] = "sequential",
    [2] = "replication swizzle .x",
    [3] = "replication swizzle .y",
    [4] = "replication swizzle .z",
    [5] = "replication swizzle .w",
    [6] = ".any4h",
    [7] = ".all4h",
};

char *pred_ctrl_align1[16] = {
    [0] = "",
    [1] = "sequential",
    [2] = ".anyv",
    [3] = ".allv",
    [4] = ".any2h",
    [5] = ".all2h",
    [6] = ".any4h",
    [7] = ".all4h",
    [8] = ".any8h",
    [9] = ".all8h",
    [10] = ".any16h",
    [11] = ".all16h",
};

char *thread_ctrl[4] = {
    [0] = "",
    [2] = "switch"
};

char *compr_ctrl[4] = {
    [0] = "",
    [1] = "sechalf",
    [2] = "compr",
};

char *dep_ctrl[4] = {
    [0] = "",
    [1] = "NoDDClr",
    [2] = "NoDDChk",
    [3] = "NoDDClr,NoDDChk",
};

char *mask_ctrl[4] = {
    [0] = "",
    [1] = "nomask",
};

char *access_mode[2] = {
    [0] = "align1",
    [1] = "align16",
};

char *reg_encoding[8] = {
    [0] = "UD",
    [1] = "D",
    [2] = "UW",
    [3] = "W",
    [4] = "UB",
    [5] = "B",
    [7] = "F"
};

char *imm_encoding[8] = {
    [0] = "UD",
    [1] = "D",
    [2] = "UW",
    [3] = "W",
    [5] = "VF",
    [5] = "V",
    [7] = "F"
};

char *reg_file[4] = {
    [0] = "A",
    [1] = "g",
    [2] = "m",
    [3] = "imm",
};

char *end_of_thread[2] = {
    [0] = "",
    [1] = "EOT"
};

char *target_function[16] = {
    [BRW_MESSAGE_TARGET_NULL] = "null",
    [BRW_MESSAGE_TARGET_MATH] = "math",
    [BRW_MESSAGE_TARGET_SAMPLER] = "sampler",
    [BRW_MESSAGE_TARGET_GATEWAY] = "gateway",
    [BRW_MESSAGE_TARGET_DATAPORT_READ] = "read",
    [BRW_MESSAGE_TARGET_DATAPORT_WRITE] = "write",
    [BRW_MESSAGE_TARGET_URB] = "urb",
    [BRW_MESSAGE_TARGET_THREAD_SPAWNER] = "thread_spawner"
};

char *sampler_target_format[4] = {
    [0] = "F",
    [2] = "UD",
    [3] = "D"
};

    
static int column;

static int string (FILE *file, char *string)
{
    fputs (string, file);
    column += strlen (string);
    return 0;
}

static int format (FILE *f, char *format, ...)
{
    char    buf[1024];
    va_list	args;
    va_start (args, format);

    vsnprintf (buf, sizeof (buf) - 1, format, args);
    string (f, buf);
    return 0;
}
    
static int newline (FILE *f)
{
    putc ('\n', f);
    column = 0;
    return 0;
}

static int pad (FILE *f, int c)
{
    while (column < c)
	string (f, " ");
    return 0;
}

static int control (FILE *file, char *name, char *ctrl[], GLuint id, int *space)
{
    if (!ctrl[id]) {
	fprintf (file, "*** invalid %s value %d ",
		 name, id);
	return 1;
    }
    if (ctrl[id][0])
    {
	if (space && *space)
	    string (file, " ");
	string (file, ctrl[id]);
	if (space)
	    *space = 1;
    }
    return 0;
}
		       
static int print_opcode (FILE *file, int id)
{
    if (!opcode[id].name) {
	format (file, "*** invalid opcode value %d ", id);
	return 1;
    }
    string (file, opcode[id].name);
    return 0;
}
		       
static int dest (FILE *file, struct brw_instruction *inst)
{
    int	err = 0;

    if (inst->bits1.da1.dest_reg_file == BRW_ARCHITECTURE_REGISTER_FILE) {
	switch (inst->bits1.da1.dest_reg_nr & 0xf0) {
	case BRW_ARF_NULL:
	    string (file, "null");
	    return 0;
	case BRW_ARF_ADDRESS:
	    format (file, "a%d", inst->bits1.da1.dest_reg_nr & 0x0f);
	    break;
	case BRW_ARF_ACCUMULATOR:
	    format (file, "acc%d", inst->bits1.da1.dest_reg_nr & 0x0f);
	    break;
	case BRW_ARF_MASK:
	    format (file, "mask%d", inst->bits1.da1.dest_reg_nr & 0x0f);
	    break;
	case BRW_ARF_MASK_STACK:
	    format (file, "msd%d", inst->bits1.da1.dest_reg_nr & 0x0f);
	    break;
	case BRW_ARF_STATE:
	    format (file, "sr%d", inst->bits1.da1.dest_reg_nr & 0x0f);
	    break;
	case BRW_ARF_CONTROL:
	    format (file, "cr%d", inst->bits1.da1.dest_reg_nr & 0x0f);
	    break;
	case BRW_ARF_NOTIFICATION_COUNT:
	    format (file, "n%d", inst->bits1.da1.dest_reg_nr & 0x0f);
	    break;
	case BRW_ARF_IP:
	    string (file, "ip");
	    break;
	default:
	    format (file, "ARF%d", inst->bits1.da1.dest_reg_nr);
	    break;
	}
    } else {
	err |= control (file, "dest reg file", reg_file, inst->bits1.da1.dest_reg_file, NULL);
	format (file, "%d", inst->bits1.da1.dest_reg_nr);
    }
    if (inst->bits1.da1.dest_subreg_nr)
	format (file, ".%d", inst->bits1.da1.dest_subreg_nr);
    format (file, "<%d>", inst->bits1.da1.dest_horiz_stride);
    err |= control (file, "dest reg encoding", reg_encoding, inst->bits1.da1.dest_reg_type, NULL);
    
    return 0;
}

static int src (FILE *file, GLuint type, GLuint _reg_file,
		GLuint _vert_stride, GLuint _width, GLuint _horiz_stride,
		GLuint reg_num, GLuint sub_reg_num)
{
    int err = 0;
    err  |= control (file, "src reg file", reg_file, _reg_file, NULL);
    format (file, "%d", reg_num);
    if (sub_reg_num)
	format (file, ".%d", sub_reg_num);
    string (file, "<");
    err |= control (file, "vert stride", vert_stride, _vert_stride, NULL);
    string (file, ",");
    err |= control (file, "width", width, _width, NULL);
    string (file, ",");
    err |= control (file, "horiz_stride", horiz_stride, _horiz_stride, NULL);
    string (file, ">");
    err |= control (file, "src reg encoding", reg_encoding, type, NULL);
    return err;
}

static int imm (FILE *file, GLuint type, struct brw_instruction *inst) {
    switch (type) {
    case BRW_REGISTER_TYPE_UD:
	format (file, "0x%08xUD", inst->bits3.ud);
	break;
    case BRW_REGISTER_TYPE_D:
	format (file, "%dD", inst->bits3.id);
	break;
    case BRW_REGISTER_TYPE_UW:
	format (file, "0x%04xUW", (uint16_t) inst->bits3.ud);
	break;
    case BRW_REGISTER_TYPE_W:
	format (file, "%dW", (int16_t) inst->bits3.id);
	break;
    case BRW_REGISTER_TYPE_UB:
	format (file, "0x%02xUB", (int8_t) inst->bits3.ud);
	break;
    case BRW_REGISTER_TYPE_VF:
	format (file, "Vector Float");
	break;
    case BRW_REGISTER_TYPE_V:
	format (file, "0x%08xV", inst->bits3.ud);
	break;
    case BRW_REGISTER_TYPE_F:
	format (file, "%-gF", inst->bits3.fd);
    }
    return 0;
}

static int src0 (FILE *file, struct brw_instruction *inst)
{
    if (inst->bits1.da1.src0_reg_file == BRW_IMMEDIATE_VALUE)
	return imm (file, inst->bits1.da1.src0_reg_type, 
		    inst);
    else
	return src (file,
		    inst->bits1.da1.src0_reg_type,
		    inst->bits1.da1.src0_reg_file,
		    inst->bits2.da1.src0_vert_stride,
		    inst->bits2.da1.src0_width,
		    inst->bits2.da1.src0_horiz_stride,
		    inst->bits2.da1.src0_reg_nr,
		    inst->bits2.da1.src0_subreg_nr);
}

static int src1 (FILE *file, struct brw_instruction *inst)
{
    if (inst->bits1.da1.src1_reg_file == BRW_IMMEDIATE_VALUE)
	return imm (file, inst->bits1.da1.src1_reg_type, 
		    inst);
    else
	return src (file,
		    inst->bits1.da1.src1_reg_type,
		    inst->bits1.da1.src1_reg_file,
		    inst->bits3.da1.src1_vert_stride,
		    inst->bits3.da1.src1_width,
		    inst->bits3.da1.src1_horiz_stride,
		    inst->bits3.da1.src1_reg_nr,
		    inst->bits3.da1.src1_subreg_nr);
}

int disasm (FILE *file, struct brw_instruction *inst)
{
    int	err = 0;
    int space = 0;

    if (inst->header.predicate_control || inst->header.predicate_inverse) {
	string (file, "(");
	space = 0;
	err |= control (file, "predicate inverse", pred_inv, inst->header.predicate_inverse, &space);
	if (inst->header.access_mode == BRW_ALIGN_1)
	    err |= control (file, "predicate control align1", pred_ctrl_align1,
			    inst->header.predicate_control, &space);
	else
	    err |= control (file, "predicate control align16", pred_ctrl_align16,
			    inst->header.predicate_control, &space);
    }
	
    err |= print_opcode (file, inst->header.opcode);
    err |= control (file, "saturate", saturate, inst->header.saturate, NULL);
    err |= control (file, "debug control", debug_ctrl, inst->header.debug_control, NULL);

    string (file, "("); {
	err |= control (file, "execution size", exec_size, inst->header.execution_size, NULL);
    } string (file, ")");

    if (inst->header.opcode == BRW_OPCODE_SEND) {
	format (file, " %d", inst->header.destreg__conditionalmod);
	space = 1;
	format (file, " mlen %d",
		inst->bits3.generic.msg_length);
	format (file, " rlen %d",
		inst->bits3.generic.response_length);
	err |= control (file, "end of thread", end_of_thread,
			inst->bits3.generic.end_of_thread, &space);
	err |= control (file, "target function", target_function,
			inst->bits3.generic.msg_target, &space);
	switch (inst->bits3.generic.msg_target) {
	case BRW_MESSAGE_TARGET_SAMPLER:
	    format (file, "( %d, %d, ",
		    inst->bits3.sampler.binding_table_index,
		    inst->bits3.sampler.sampler);
	    err |= control (file, "sampler target format", sampler_target_format,
			    inst->bits3.sampler.return_format, NULL);
	    string (file, " )");
	    break;
	case BRW_MESSAGE_TARGET_DATAPORT_WRITE:
	    format (file, "( %d, %d, %d, %d )",
		    inst->bits3.dp_write.binding_table_index,
		    inst->bits3.dp_write.pixel_scoreboard_clear << 3 |
		    inst->bits3.dp_write.msg_control,
		    inst->bits3.dp_write.msg_type,
		    inst->bits3.dp_write.send_commit_msg);
	    break;
	}
    }
    else
	err |= control (file, "conditional modifier", conditional_modifier,
			inst->header.destreg__conditionalmod, NULL);

    if (opcode[inst->header.opcode].ndst > 0) {
	pad (file, 16);
	err |= dest (file, inst);
    }
    if (opcode[inst->header.opcode].nsrc > 0) {
	pad (file, 32);
	err |= src0 (file, inst);
    }
    if (opcode[inst->header.opcode].nsrc > 1) {
	pad (file, 48);
	err |= src1 (file, inst);
    }
    pad (file, 64);
    string (file, "{"); {
	space = 1;
	err |= control(file, "access mode", access_mode, inst->header.access_mode, &space);
	err |= control (file, "mask control", mask_ctrl, inst->header.mask_control, &space);
	err |= control (file, "dependency control", dep_ctrl, inst->header.dependency_control, &space);
	err |= control (file, "compression control", compr_ctrl, inst->header.compression_control, &space);
	err |= control (file, "thread control", thread_ctrl, inst->header.thread_control, &space);
	if (space)
	    string (file, " ");
    } string (file, "};");
    newline (file);
    return err;
}
