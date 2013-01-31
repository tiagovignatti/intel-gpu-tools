/*
 * Copyright © 2013 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * To share code with mesa without having to do big modifications and still be
 * able to sync files together at a later point, this file stubs the fields
 * of struct brw_context used by the code we import.
 */

#ifndef __BRW_CONTEXT_H__
#define __BRW_CONTEXT_H__

#include <stdbool.h>
#include <stdio.h>

#include "brw_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef INTEL_DEBUG
#define INTEL_DEBUG (0)
#endif

struct intel_context
{
   int gen;
   int gt;
   bool is_haswell;
   bool is_g4x;
   bool needs_ff_sync;
};

struct brw_context
{
   struct intel_context intel;
};

bool
brw_init_context(struct brw_context *brw, int gen);

/* brw_disasm.c */
struct opcode_desc {
    char    *name;
    int	    nsrc;
    int	    ndst;
};

extern const struct opcode_desc opcode_descs[128];

int brw_disasm (FILE *file, struct brw_instruction *inst, int gen);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* __BRW_CONTEXT_H__ */
