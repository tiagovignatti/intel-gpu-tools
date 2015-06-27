/*
 * Copyright © 2015 Intel Corporation
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

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "igt_stats.h"

#define U64_MAX         ((uint64_t)~0ULL)
#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))

#define WARN(cond, msg)	printf(msg)

#define KHz(x) (1000 * (x))
#define MHz(x) KHz(1000 * (x))

#define abs_diff(a, b) ({			\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	(void) (&__a == &__b);			\
	__a > __b ? (__a - __b) : (__b - __a); })

static inline uint64_t div64_u64(uint64_t dividend, uint64_t divisor)
{
	return dividend / divisor;
}

static inline uint64_t div_u64(uint64_t dividend, uint32_t divisor)
{
	return dividend / divisor;
}

struct skl_wrpll_params {
	uint32_t        dco_fraction;
	uint32_t        dco_integer;
	uint32_t        qdiv_ratio;
	uint32_t        qdiv_mode;
	uint32_t        kdiv;
	uint32_t        pdiv;
	uint32_t        central_freq;

	/* for this test code only */
	uint64_t central_freq_hz;
	unsigned int p0, p1, p2;
};

static bool
skl_ddi_calculate_wrpll1(int clock /* in Hz */,
			 struct skl_wrpll_params *wrpll_params)
{
	uint64_t afe_clock = clock * 5; /* AFE Clock is 5x Pixel clock */
	uint64_t dco_central_freq[3] = {8400000000ULL,
					9000000000ULL,
					9600000000ULL};
	uint32_t min_dco_pdeviation = 100; /* DCO freq must be within +1%/-6% */
	uint32_t min_dco_ndeviation = 600; /* of the DCO central freq */
	uint32_t min_dco_index = 3;
	uint32_t P0[4] = {1, 2, 3, 7};
	uint32_t P2[4] = {1, 2, 3, 5};
	bool found = false;
	uint32_t candidate_p = 0;
	uint32_t candidate_p0[3] = {0}, candidate_p1[3] = {0};
	uint32_t candidate_p2[3] = {0};
	uint32_t dco_central_freq_deviation[3];
	uint32_t i, P1, k, dco_count;
	bool retry_with_odd = false;

	/* Determine P0, P1 or P2 */
	for (dco_count = 0; dco_count < 3; dco_count++) {
		found = false;
		candidate_p =
			div64_u64(dco_central_freq[dco_count], afe_clock);
		if (retry_with_odd == false)
			candidate_p = (candidate_p % 2 == 0 ?
				candidate_p : candidate_p + 1);

		for (P1 = 1; P1 < candidate_p; P1++) {
			for (i = 0; i < 4; i++) {
				if (!(P0[i] != 1 || P1 == 1))
					continue;

				for (k = 0; k < 4; k++) {
					if (P1 != 1 && P2[k] != 2)
						continue;

					if (candidate_p == P0[i] * P1 * P2[k]) {
						/* Found possible P0, P1, P2 */
						found = true;
						candidate_p0[dco_count] = P0[i];
						candidate_p1[dco_count] = P1;
						candidate_p2[dco_count] = P2[k];
						goto found;
					}

				}
			}
		}

found:
		if (found) {
			uint64_t dco_freq = candidate_p * afe_clock;

#if 0
			printf("Trying with (%d,%d,%d)\n",
			       candidate_p0[dco_count],
			       candidate_p1[dco_count],
			       candidate_p2[dco_count]);
#endif

			dco_central_freq_deviation[dco_count] =
				div64_u64(10000 *
					  abs_diff(dco_freq,
						   dco_central_freq[dco_count]),
					  dco_central_freq[dco_count]);

#if 0
			printf("Deviation %d\n",
			       dco_central_freq_deviation[dco_count]);

			printf("dco_freq: %"PRIu64", "
			       "dco_central_freq %"PRIu64"\n",
			       dco_freq, dco_central_freq[dco_count]);
#endif

			/* positive deviation */
			if (dco_freq > dco_central_freq[dco_count]) {
				if (dco_central_freq_deviation[dco_count] <
				    min_dco_pdeviation) {
					min_dco_pdeviation =
						dco_central_freq_deviation[dco_count];
					min_dco_index = dco_count;
				}
			/* negative deviation */
			} else if (dco_central_freq_deviation[dco_count] <
				   min_dco_ndeviation) {
				min_dco_ndeviation =
					dco_central_freq_deviation[dco_count];
				min_dco_index = dco_count;
			}
		}

		if (min_dco_index > 2 && dco_count == 2) {
			/* oh well, we tried... */
			if (retry_with_odd)
				break;

			retry_with_odd = true;
			dco_count = 0;
		}
	}

	if (min_dco_index > 2) {
		WARN(1, "No valid values found for the given pixel clock\n");
		return false;
	} else {
		uint64_t dco_freq;

		wrpll_params->central_freq = dco_central_freq[min_dco_index];

		switch (dco_central_freq[min_dco_index]) {
		case 9600000000ULL:
			wrpll_params->central_freq = 0;
			break;
		case 9000000000ULL:
			wrpll_params->central_freq = 1;
			break;
		case 8400000000ULL:
			wrpll_params->central_freq = 3;
		}

		switch (candidate_p0[min_dco_index]) {
		case 1:
			wrpll_params->pdiv = 0;
			break;
		case 2:
			wrpll_params->pdiv = 1;
			break;
		case 3:
			wrpll_params->pdiv = 2;
			break;
		case 7:
			wrpll_params->pdiv = 4;
			break;
		default:
			WARN(1, "Incorrect PDiv\n");
		}

		switch (candidate_p2[min_dco_index]) {
		case 5:
			wrpll_params->kdiv = 0;
			break;
		case 2:
			wrpll_params->kdiv = 1;
			break;
		case 3:
			wrpll_params->kdiv = 2;
			break;
		case 1:
			wrpll_params->kdiv = 3;
			break;
		default:
			WARN(1, "Incorrect KDiv\n");
		}

		wrpll_params->qdiv_ratio = candidate_p1[min_dco_index];
		wrpll_params->qdiv_mode =
			(wrpll_params->qdiv_ratio == 1) ? 0 : 1;

		dco_freq = candidate_p0[min_dco_index] *
			candidate_p1[min_dco_index] *
			candidate_p2[min_dco_index] * afe_clock;

		/*
		 * Intermediate values are in Hz.
		 * Divide by MHz to match bsepc
		 */
		wrpll_params->dco_integer = div_u64(dco_freq, (24 * MHz(1)));
		wrpll_params->dco_fraction =
			div_u64(((div_u64(dco_freq, 24) -
				  wrpll_params->dco_integer * MHz(1)) * 0x8000), MHz(1));

	}

	/* for this unit test only */
	wrpll_params->central_freq_hz = dco_central_freq[min_dco_index];
	wrpll_params->p0 = candidate_p0[min_dco_index];
	wrpll_params->p1 = candidate_p1[min_dco_index];
	wrpll_params->p2 = candidate_p2[min_dco_index];

	return true;
}

struct skl_wrpll_context {
	uint64_t min_deviation;		/* current minimal deviation */
	uint64_t central_freq;		/* chosen central freq */
	uint64_t dco_freq;		/* chosen dco freq */
	unsigned int p;			/* chosen divider */
};

static void skl_wrpll_context_init(struct skl_wrpll_context *ctx)
{
	memset(ctx, 0, sizeof(*ctx));

	ctx->min_deviation = U64_MAX;
}

/* DCO freq must be within +1%/-6%  of the DCO central freq */
#define SKL_MAX_PDEVIATION	100
#define SKL_MAX_NDEVIATION	600


/*
 * Returns true if we're sure to have found the definitive divider (ie
 * deviation == 0).
 */
static bool skl_wrpll_try_divider(struct skl_wrpll_context *ctx,
				  uint64_t central_freq,
				  uint64_t dco_freq,
				  unsigned int divider)
{
	uint64_t deviation;
	bool found = false;

	deviation = div64_u64(10000 * abs_diff(dco_freq, central_freq),
			      central_freq);

	/* positive deviation */
	if (dco_freq >= central_freq) {
		if (deviation < SKL_MAX_PDEVIATION &&
		    deviation < ctx->min_deviation) {
			ctx->min_deviation = deviation;
			ctx->central_freq = central_freq;
			ctx->dco_freq = dco_freq;
			ctx->p = divider;
#if 0
			found = true;
#endif
		}

		/* we can't improve a 0 deviation */
		if (deviation == 0)
			return true;
	/* negative deviation */
	} else if (deviation < SKL_MAX_NDEVIATION &&
		   deviation < ctx->min_deviation) {
		ctx->min_deviation = deviation;
		ctx->central_freq = central_freq;
		ctx->dco_freq = dco_freq;
		ctx->p = divider;
#if 0
		found = true;
#endif
	}

	if (found) {
		printf("Divider %d\n", divider);
		printf("Deviation %"PRIu64"\n", deviation);
		printf("dco_freq: %"PRIu64", dco_central_freq %"PRIu64"\n",
		       dco_freq, central_freq);
	}

	return false;
}

static void skl_wrpll_get_multipliers(unsigned int p,
				      unsigned int *p0 /* out */,
				      unsigned int *p1 /* out */,
				      unsigned int *p2 /* out */)
{
	/* even dividers */
	if (p % 2 == 0) {
		unsigned int half = p / 2;

		if (half == 1 || half == 2 || half == 3 || half == 5) {
			*p0 = 2;
			*p1 = 1;
			*p2 = half;
		} else if (half % 2 == 0) {
			*p0 = 2;
			*p1 = half / 2;
			*p2 = 2;
		} else if (half % 3 == 0) {
			*p0 = 3;
			*p1 = half / 3;
			*p2 = 2;
		} else if (half % 7 == 0) {
			*p0 = 7;
			*p1 = half / 7;
			*p2 = 2;
		}
	} else if (p == 3 || p == 9) {  /* 3, 5, 7, 9, 15, 21, 35 */
		*p0 = 3;
		*p1 = 1;
		*p2 = p / 3;
	} else if (p == 5 || p == 7) {
		*p0 = p;
		*p1 = 1;
		*p2 = 1;
	} else if (p == 15) {
		*p0 = 3;
		*p1 = 1;
		*p2 = 5;
	} else if (p == 21) {
		*p0 = 7;
		*p1 = 1;
		*p2 = 3;
	} else if (p == 35) {
		*p0 = 7;
		*p1 = 1;
		*p2 = 5;
	}
}

static void test_multipliers(void)
{
	static const int even_dividers[] = {  4,  6,  8, 10, 12, 14, 16, 18, 20,
					     24, 28, 30, 32, 36, 40, 42, 44,
					     48, 52, 54, 56, 60, 64, 66, 68,
					     70, 72, 76, 78, 80, 84, 88, 90,
					     92, 96, 98 };
	static const int odd_dividers[] = { 3, 5, 7, 9, 15, 21, 35 };
	static const struct {
		const int *list;
		int n_dividers;
	} dividers[] = {
		{ even_dividers, ARRAY_SIZE(even_dividers) },
		{ odd_dividers, ARRAY_SIZE(odd_dividers) },
	};
	unsigned int d, i;

	for (d = 0; d < ARRAY_SIZE(dividers); d++) {
		for (i = 0; i < dividers[d].n_dividers; i++) {
			unsigned int p = dividers[d].list[i];
			unsigned p0, p1, p2;

			p0 = p1 = p2 = 0;

			skl_wrpll_get_multipliers(p, &p0, &p1, &p2);

			assert(p0);
			assert(p1);
			assert(p2);
			assert(p == p0 * p1 * p2);
		}
	}
}

static bool
skl_ddi_calculate_wrpll2(int clock /* in Hz */,
			 struct skl_wrpll_params *wrpll_params)
{
	uint64_t afe_clock = clock * 5; /* AFE Clock is 5x Pixel clock */
	uint64_t dco_central_freq[3] = {8400000000ULL,
					9000000000ULL,
					9600000000ULL};
	static const int even_dividers[] = {  4,  6,  8, 10, 12, 14, 16, 18, 20,
					     24, 28, 30, 32, 36, 40, 42, 44,
					     48, 52, 54, 56, 60, 64, 66, 68,
					     70, 72, 76, 78, 80, 84, 88, 90,
					     92, 96, 98 };
	static const int odd_dividers[] = { 3, 5, 7, 9, 15, 21, 35 };
	static const struct {
		const int *list;
		int n_dividers;
	} dividers[] = {
		{ even_dividers, ARRAY_SIZE(even_dividers) },
		{ odd_dividers, ARRAY_SIZE(odd_dividers) },
	};
	struct skl_wrpll_context ctx;
	unsigned int dco, d, i;
	unsigned int p0, p1, p2;

	skl_wrpll_context_init(&ctx);

	for (d = 0; d < ARRAY_SIZE(dividers); d++) {
		for (dco = 0; dco < ARRAY_SIZE(dco_central_freq); dco++) {
			for (i = 0; i < dividers[d].n_dividers; i++) {
				unsigned int p = dividers[d].list[i];
				uint64_t dco_freq = p * afe_clock;

				if (skl_wrpll_try_divider(&ctx,
							  dco_central_freq[dco],
							  dco_freq,
							  p))
					goto skip_remaining_dividers;
			}
		}

skip_remaining_dividers:
		/*
		 * If a solution is found with an even divider, prefer
		 * this one.
		 */
		if (d == 0 && ctx.p)
			break;
	}

	if (!ctx.p)
		return false;

	skl_wrpll_get_multipliers(ctx.p, &p0, &p1, &p2);

	/* for this unit test only */
	wrpll_params->central_freq_hz = ctx.central_freq;
	wrpll_params->p0 = p0;
	wrpll_params->p1 = p1;
	wrpll_params->p2 = p2;

	return true;
}

static const struct {
	uint32_t clock; /* in Hz */
} modes[] = {
	{ 19750000 },
	{ 20000000 },
	{ 21000000 },
	{ 21912000 },
	{ 22000000 },
	{ 23000000 },
	{ 23500000 },
	{ 23750000 },
	{ 24000000 },
	{ 25000000 },
	{ 25175000 },
	{ 25200000 },
	{ 26000000 },
	{ 27000000 },
	{ 27027000 },
	{ 27500000 },
	{ 28000000 },
	{ 28320000 },
	{ 28322000 },
	{ 28750000 },
	{ 29000000 },
	{ 29750000 },
	{ 30000000 },
	{ 30750000 },
	{ 31000000 },
	{ 31500000 },
	{ 32000000 },
	{ 32500000 },
	{ 33000000 },
	{ 34000000 },
	{ 35000000 },
	{ 35500000 },
	{ 36000000 },
	{ 36750000 },
	{ 37000000 },
	{ 37762500 },
	{ 37800000 },
	{ 38000000 },
	{ 38250000 },
	{ 39000000 },
	{ 40000000 },
	{ 40500000 },
	{ 40541000 },
	{ 40750000 },
	{ 41000000 },
	{ 41500000 },
	{ 41540000 },
	{ 42000000 },
	{ 42500000 },
	{ 43000000 },
	{ 43163000 },
	{ 44000000 },
	{ 44900000 },
	{ 45000000 },
	{ 45250000 },
	{ 46000000 },
	{ 46750000 },
	{ 47000000 },
	{ 48000000 },
	{ 49000000 },
	{ 49500000 },
	{ 50000000 },
	{ 50500000 },
	{ 51000000 },
	{ 52000000 },
	{ 52406000 },
	{ 53000000 },
	{ 54000000 },
	{ 54054000 },
	{ 54500000 },
	{ 55000000 },
	{ 56000000 },
	{ 56250000 },
	{ 56750000 },
	{ 57000000 },
	{ 58000000 },
	{ 58250000 },
	{ 58750000 },
	{ 59000000 },
	{ 59341000 },
	{ 59400000 },
	{ 60000000 },
	{ 60500000 },
	{ 61000000 },
	{ 62000000 },
	{ 62250000 },
	{ 63000000 },
	{ 63500000 },
	{ 64000000 },
	{ 65000000 },
	{ 65250000 },
	{ 65500000 },
	{ 66000000 },
	{ 66667000 },
	{ 66750000 },
	{ 67000000 },
	{ 67750000 },
	{ 68000000 },
	{ 68179000 },
	{ 68250000 },
	{ 69000000 },
	{ 70000000 },
	{ 71000000 },
	{ 72000000 },
	{ 73000000 },
	{ 74000000 },
	{ 74176000 },
	{ 74250000 },
	{ 74481000 },
	{ 74500000 },
	{ 75000000 },
	{ 75250000 },
	{ 76000000 },
	{ 77000000 },
	{ 78000000 },
	{ 78750000 },
	{ 79000000 },
	{ 79500000 },
	{ 80000000 },
	{ 81000000 },
	{ 81081000 },
	{ 81624000 },
	{ 82000000 },
	{ 83000000 },
	{ 83950000 },
	{ 84000000 },
	{ 84750000 },
	{ 85000000 },
	{ 85250000 },
	{ 85750000 },
	{ 86000000 },
	{ 87000000 },
	{ 88000000 },
	{ 88500000 },
	{ 89000000 },
	{ 89012000 },
	{ 89100000 },
	{ 90000000 },
	{ 91000000 },
	{ 92000000 },
	{ 93000000 },
	{ 94000000 },
	{ 94500000 },
	{ 95000000 },
	{ 95654000 },
	{ 95750000 },
	{ 96000000 },
	{ 97000000 },
	{ 97750000 },
	{ 98000000 },
	{ 99000000 },
	{ 99750000 },
	{ 100000000 },
	{ 100500000 },
	{ 101000000 },
	{ 101250000 },
	{ 102000000 },
	{ 102250000 },
	{ 103000000 },
	{ 104000000 },
	{ 105000000 },
	{ 106000000 },
	{ 107000000 },
	{ 107214000 },
	{ 108000000 },
	{ 108108000 },
	{ 109000000 },
	{ 110000000 },
	{ 110013000 },
	{ 110250000 },
	{ 110500000 },
	{ 111000000 },
	{ 111264000 },
	{ 111375000 },
	{ 112000000 },
	{ 112500000 },
	{ 113100000 },
	{ 113309000 },
	{ 114000000 },
	{ 115000000 },
	{ 116000000 },
	{ 117000000 },
	{ 117500000 },
	{ 118000000 },
	{ 119000000 },
	{ 119500000 },
	{ 119651000 },
	{ 120000000 },
	{ 121000000 },
	{ 121250000 },
	{ 121750000 },
	{ 122000000 },
	{ 122614000 },
	{ 123000000 },
	{ 123379000 },
	{ 124000000 },
	{ 125000000 },
	{ 125250000 },
	{ 125750000 },
	{ 126000000 },
	{ 127000000 },
	{ 127250000 },
	{ 128000000 },
	{ 129000000 },
	{ 129859000 },
	{ 130000000 },
	{ 130250000 },
	{ 131000000 },
	{ 131500000 },
	{ 131850000 },
	{ 132000000 },
	{ 132750000 },
	{ 133000000 },
	{ 133330000 },
	{ 134000000 },
	{ 135000000 },
	{ 135250000 },
	{ 136000000 },
	{ 137000000 },
	{ 138000000 },
	{ 138500000 },
	{ 138750000 },
	{ 139000000 },
	{ 139050000 },
	{ 139054000 },
	{ 140000000 },
	{ 141000000 },
	{ 141500000 },
	{ 142000000 },
	{ 143000000 },
	{ 143472000 },
	{ 144000000 },
	{ 145000000 },
	{ 146000000 },
	{ 146250000 },
	{ 147000000 },
	{ 147891000 },
	{ 148000000 },
	{ 148250000 },
	{ 148352000 },
	{ 148500000 },
	{ 149000000 },
	{ 150000000 },
	{ 151000000 },
	{ 152000000 },
	{ 152280000 },
	{ 153000000 },
	{ 154000000 },
	{ 155000000 },
	{ 155250000 },
	{ 155750000 },
	{ 156000000 },
	{ 157000000 },
	{ 157500000 },
	{ 158000000 },
	{ 158250000 },
	{ 159000000 },
	{ 159500000 },
	{ 160000000 },
	{ 161000000 },
	{ 162000000 },
	{ 162162000 },
	{ 162500000 },
	{ 163000000 },
	{ 164000000 },
	{ 165000000 },
	{ 166000000 },
	{ 167000000 },
	{ 168000000 },
	{ 169000000 },
	{ 169128000 },
	{ 169500000 },
	{ 170000000 },
	{ 171000000 },
	{ 172000000 },
	{ 172750000 },
	{ 172800000 },
	{ 173000000 },
	{ 174000000 },
	{ 174787000 },
	{ 175000000 },
	{ 176000000 },
	{ 177000000 },
	{ 178000000 },
	{ 178500000 },
	{ 179000000 },
	{ 179500000 },
	{ 180000000 },
	{ 181000000 },
	{ 182000000 },
	{ 183000000 },
	{ 184000000 },
	{ 184750000 },
	{ 185000000 },
	{ 186000000 },
	{ 187000000 },
	{ 188000000 },
	{ 189000000 },
	{ 190000000 },
	{ 190960000 },
	{ 191000000 },
	{ 192000000 },
	{ 192250000 },
	{ 193000000 },
	{ 193250000 },
	{ 194000000 },
	{ 194208000 },
	{ 195000000 },
	{ 196000000 },
	{ 197000000 },
	{ 197750000 },
	{ 198000000 },
	{ 198500000 },
	{ 199000000 },
	{ 200000000 },
	{ 201000000 },
	{ 202000000 },
	{ 202500000 },
	{ 203000000 },
	{ 204000000 },
	{ 204750000 },
	{ 205000000 },
	{ 206000000 },
	{ 207000000 },
	{ 207500000 },
	{ 208000000 },
	{ 208900000 },
	{ 209000000 },
	{ 209250000 },
	{ 210000000 },
	{ 211000000 },
	{ 212000000 },
	{ 213000000 },
	{ 213750000 },
	{ 214000000 },
	{ 214750000 },
	{ 215000000 },
	{ 216000000 },
	{ 217000000 },
	{ 218000000 },
	{ 218250000 },
	{ 218750000 },
	{ 219000000 },
	{ 220000000 },
	{ 220640000 },
	{ 220750000 },
	{ 221000000 },
	{ 222000000 },
	{ 222525000 },
	{ 222750000 },
	{ 227000000 },
	{ 230250000 },
	{ 233500000 },
	{ 235000000 },
	{ 238000000 },
	{ 241500000 },
	{ 245250000 },
	{ 247750000 },
	{ 253250000 },
	{ 256250000 },
	{ 262500000 },
	{ 267250000 },
	{ 268500000 },
	{ 270000000 },
	{ 272500000 },
	{ 273750000 },
	{ 280750000 },
	{ 281250000 },
	{ 286000000 },
	{ 291750000 },
	{ 296703000 },
	{ 297000000 },
	{ 298000000 },
};

struct test_ops {
	bool (*compute)(int clock, struct skl_wrpll_params *params);
} tests[] = {
	{ .compute = skl_ddi_calculate_wrpll1 },
	{ .compute = skl_ddi_calculate_wrpll2 },
};

static void test_run(struct test_ops *test)
{
	unsigned int m;
	unsigned p_odd_even[2] = { 0, 0 };
	igt_stats_t stats;

	igt_stats_init_with_size(&stats, ARRAY_SIZE(modes));
	igt_stats_set_population(&stats, true);

	for (m = 0; m < ARRAY_SIZE(modes); m++) {
		struct skl_wrpll_params params = {};
		int clock = modes[m].clock;
		unsigned int p;

		if (!test->compute(clock, &params)) {
			fprintf(stderr, "Couldn't compute divider for %dHz\n",
				clock);
			continue;
		}

		p = params.p0 * params.p1 * params.p2;

		/*
		 * make sure we respect the +1%/-6% contraint around the
		 * central frequency
		 */
		{
			uint64_t dco_freq = (uint64_t)p * clock * 5;
			uint64_t central_freq = params.central_freq_hz;
			uint64_t deviation;
			uint64_t diff;

			diff = abs_diff(dco_freq, central_freq);
			deviation = div64_u64(10000 * diff, central_freq);

			igt_stats_push(&stats, deviation);

			if (dco_freq > central_freq) {
				if (deviation > 100)
					printf("failed constraint for %dHz "
					       "deviation=%"PRIu64"\n", clock,
					       deviation);
			} else if (deviation > 600)
				printf("failed constraint for %dHz "
				       "deviation=%"PRIu64"\n", clock,
				       deviation);
		}

		/*
		 * count how many even/odd dividers we have through the whole
		 * list of tested frequencies
		 */
		{
			p_odd_even[p % 2]++;
		}
	}

	printf("even/odd dividers: %d/%d\n", p_odd_even[0], p_odd_even[1]);
	printf("mean central freq deviation: %.2lf\n",
	       igt_stats_get_mean(&stats));

	igt_stats_fini(&stats);
}

int main(int argc, char **argv)
{
	unsigned int t;

	test_multipliers();

	for (t = 0; t < ARRAY_SIZE(tests); t++) {
		printf("=== Testing algorithm #%d\n", t + 1);
		test_run(&tests[t]);
	}


	return 0;
}
