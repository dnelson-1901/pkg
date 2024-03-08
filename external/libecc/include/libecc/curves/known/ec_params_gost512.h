/*
 *  Copyright (C) 2017 - This file is part of libecc project
 *
 *  Authors:
 *      Ryad BENADJILA <ryadbenadjila@gmail.com>
 *      Arnaud EBALARD <arnaud.ebalard@ssi.gouv.fr>
 *      Jean-Pierre FLORI <jean-pierre.flori@ssi.gouv.fr>
 *
 *  Contributors:
 *      Nicolas VIVET <nicolas.vivet@ssi.gouv.fr>
 *      Karim KHALFALLAH <karim.khalfallah@ssi.gouv.fr>
 *
 *  This software is licensed under a dual BSD and GPL v2 license.
 *  See LICENSE file at the root folder of the project.
 */
#include <libecc/lib_ecc_config.h>
#ifdef WITH_CURVE_GOST512

#ifndef __EC_PARAMS_GOST512_H__
#define __EC_PARAMS_GOST512_H__
#include "ec_params_external.h"

static const u8 GOST_512bits_curve_p[] = {
	0x45, 0x31, 0xAC, 0xD1, 0xFE, 0x00, 0x23, 0xC7,
	0x55, 0x0D, 0x26, 0x7B, 0x6B, 0x2F, 0xEE, 0x80,
	0x92, 0x2B, 0x14, 0xB2, 0xFF, 0xB9, 0x0F, 0x04,
	0xD4, 0xEB, 0x7C, 0x09, 0xB5, 0xD2, 0xD1, 0x5D,
	0xF1, 0xD8, 0x52, 0x74, 0x1A, 0xF4, 0x70, 0x4A,
	0x04, 0x58, 0x04, 0x7E, 0x80, 0xE4, 0x54, 0x6D,
	0x35, 0xB8, 0x33, 0x6F, 0xAC, 0x22, 0x4D, 0xD8,
	0x16, 0x64, 0xBB, 0xF5, 0x28, 0xBE, 0x63, 0x73
};

TO_EC_STR_PARAM(GOST_512bits_curve_p);

#define CURVE_GOST512_P_BITLEN 512
static const u8 GOST_512bits_curve_p_bitlen[] = { 0x01, 0xff };

TO_EC_STR_PARAM(GOST_512bits_curve_p_bitlen);

static const u8 GOST_512bits_curve_p_normalized[] = {
	0x8a, 0x63, 0x59, 0xa3, 0xfc, 0x00, 0x47, 0x8e,
	0xaa, 0x1a, 0x4c, 0xf6, 0xd6, 0x5f, 0xdd, 0x01,
	0x24, 0x56, 0x29, 0x65, 0xff, 0x72, 0x1e, 0x09,
	0xa9, 0xd6, 0xf8, 0x13, 0x6b, 0xa5, 0xa2, 0xbb,
	0xe3, 0xb0, 0xa4, 0xe8, 0x35, 0xe8, 0xe0, 0x94,
	0x08, 0xb0, 0x08, 0xfd, 0x01, 0xc8, 0xa8, 0xda,
	0x6b, 0x70, 0x66, 0xdf, 0x58, 0x44, 0x9b, 0xb0,
	0x2c, 0xc9, 0x77, 0xea, 0x51, 0x7c, 0xc6, 0xe6
};

TO_EC_STR_PARAM(GOST_512bits_curve_p_normalized);

static const u8 GOST_512bits_curve_r[] = {
	0x30, 0x6a, 0xf9, 0x8a, 0x05, 0xff, 0x94, 0xaa,
	0x00, 0xd8, 0x8c, 0x8d, 0xbe, 0x70, 0x34, 0x7e,
	0x49, 0x7e, 0xc1, 0xe7, 0x00, 0xd4, 0xd2, 0xf1,
	0x81, 0x3d, 0x8b, 0xe2, 0xde, 0x87, 0x8b, 0xe6,
	0x2a, 0x77, 0x08, 0xa3, 0xaf, 0x22, 0xaf, 0x21,
	0xf2, 0xf7, 0xf2, 0x84, 0x7d, 0x53, 0x02, 0xb8,
	0x5e, 0xd7, 0x65, 0xb0, 0xfb, 0x99, 0x16, 0x77,
	0xbc, 0xd1, 0xcc, 0x20, 0x85, 0xc4, 0xd5, 0xa7
};

TO_EC_STR_PARAM(GOST_512bits_curve_r);

static const u8 GOST_512bits_curve_r_square[] = {
	0x1d, 0x88, 0x7d, 0xcd, 0x9c, 0xd1, 0x9c, 0x10,
	0x01, 0xfd, 0xe9, 0xca, 0x99, 0xde, 0x08, 0x52,
	0xfc, 0xd9, 0x83, 0xcf, 0xb7, 0xc6, 0x63, 0xd9,
	0xca, 0x0b, 0xc8, 0xaf, 0x77, 0xc8, 0x69, 0x0a,
	0x08, 0x15, 0xb9, 0xeb, 0x1e, 0x7d, 0xd3, 0x00,
	0xe4, 0x17, 0xd5, 0x8d, 0x20, 0x0c, 0x2a, 0xa0,
	0x4b, 0x90, 0x7a, 0x71, 0xe6, 0x47, 0xee, 0x63,
	0x00, 0x1c, 0x10, 0xbc, 0x2d, 0x00, 0x5b, 0x65
};

TO_EC_STR_PARAM(GOST_512bits_curve_r_square);

static const u8 GOST_512bits_curve_mpinv[] = {
	0xd6, 0x41, 0x2f, 0xf7, 0xc2, 0x9b, 0x86, 0x45
};

TO_EC_STR_PARAM(GOST_512bits_curve_mpinv);

static const u8 GOST_512bits_curve_p_shift[] = { 0x01 };

TO_EC_STR_PARAM(GOST_512bits_curve_p_shift);

#if (WORD_BYTES == 8)		/* 64-bit words */
static const u8 GOST_512bits_curve_p_reciprocal[] = {
	0xd9, 0x91, 0x18, 0x3c, 0xe8, 0xbf, 0x25, 0x61
};
#elif (WORD_BYTES == 4)		/* 32-bit words */
static const u8 GOST_512bits_curve_p_reciprocal[] = {
	0xd9, 0x91, 0x18, 0x3c
};
#elif (WORD_BYTES == 2)		/* 16-bit words */
static const u8 GOST_512bits_curve_p_reciprocal[] = {
	0xd9, 0x91
};
#else /* unknown word size */
#error "Unsupported word size"
#endif
TO_EC_STR_PARAM(GOST_512bits_curve_p_reciprocal);

static const u8 GOST_512bits_curve_a[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07
};

TO_EC_STR_PARAM(GOST_512bits_curve_a);

static const u8 GOST_512bits_curve_b[] = {
	0x1C, 0xFF, 0x08, 0x06, 0xA3, 0x11, 0x16, 0xDA,
	0x29, 0xD8, 0xCF, 0xA5, 0x4E, 0x57, 0xEB, 0x74,
	0x8B, 0xC5, 0xF3, 0x77, 0xE4, 0x94, 0x00, 0xFD,
	0xD7, 0x88, 0xB6, 0x49, 0xEC, 0xA1, 0xAC, 0x43,
	0x61, 0x83, 0x40, 0x13, 0xB2, 0xAD, 0x73, 0x22,
	0x48, 0x0A, 0x89, 0xCA, 0x58, 0xE0, 0xCF, 0x74,
	0xBC, 0x9E, 0x54, 0x0C, 0x2A, 0xDD, 0x68, 0x97,
	0xFA, 0xD0, 0xA3, 0x08, 0x4F, 0x30, 0x2A, 0xDC
};

TO_EC_STR_PARAM(GOST_512bits_curve_b);

#define CURVE_GOST512_CURVE_ORDER_BITLEN 511
static const u8 GOST_512bits_curve_order[] = {
	0x45, 0x31, 0xac, 0xd1, 0xfe, 0x00, 0x23, 0xc7,
	0x55, 0x0d, 0x26, 0x7b, 0x6b, 0x2f, 0xee, 0x80,
	0x92, 0x2b, 0x14, 0xb2, 0xff, 0xb9, 0x0f, 0x04,
	0xd4, 0xeb, 0x7c, 0x09, 0xb5, 0xd2, 0xd1, 0x5d,
	0xa8, 0x2f, 0x2d, 0x7e, 0xcb, 0x1d, 0xba, 0xc7,
	0x19, 0x90, 0x5c, 0x5e, 0xec, 0xc4, 0x23, 0xf1,
	0xd8, 0x6e, 0x25, 0xed, 0xbe, 0x23, 0xc5, 0x95,
	0xd6, 0x44, 0xaa, 0xf1, 0x87, 0xe6, 0xe6, 0xdf
};

TO_EC_STR_PARAM(GOST_512bits_curve_order);

static const u8 GOST_512bits_curve_gx[] = {
	0x24, 0xD1, 0x9C, 0xC6, 0x45, 0x72, 0xEE, 0x30,
	0xF3, 0x96, 0xBF, 0x6E, 0xBB, 0xFD, 0x7A, 0x6C,
	0x52, 0x13, 0xB3, 0xB3, 0xD7, 0x05, 0x7C, 0xC8,
	0x25, 0xF9, 0x10, 0x93, 0xA6, 0x8C, 0xD7, 0x62,
	0xFD, 0x60, 0x61, 0x12, 0x62, 0xCD, 0x83, 0x8D,
	0xC6, 0xB6, 0x0A, 0xA7, 0xEE, 0xE8, 0x04, 0xE2,
	0x8B, 0xC8, 0x49, 0x97, 0x7F, 0xAC, 0x33, 0xB4,
	0xB5, 0x30, 0xF1, 0xB1, 0x20, 0x24, 0x8A, 0x9A
};

TO_EC_STR_PARAM(GOST_512bits_curve_gx);

static const u8 GOST_512bits_curve_gy[] = {
	0x2B, 0xB3, 0x12, 0xA4, 0x3B, 0xD2, 0xCE, 0x6E,
	0x0D, 0x02, 0x06, 0x13, 0xC8, 0x57, 0xAC, 0xDD,
	0xCF, 0xBF, 0x06, 0x1E, 0x91, 0xE5, 0xF2, 0xC3,
	0xF3, 0x24, 0x47, 0xC2, 0x59, 0xF3, 0x9B, 0x2C,
	0x83, 0xAB, 0x15, 0x6D, 0x77, 0xF1, 0x49, 0x6B,
	0xF7, 0xEB, 0x33, 0x51, 0xE1, 0xEE, 0x4E, 0x43,
	0xDC, 0x1A, 0x18, 0xB9, 0x1B, 0x24, 0x64, 0x0B,
	0x6D, 0xBB, 0x92, 0xCB, 0x1A, 0xDD, 0x37, 0x1E
};

TO_EC_STR_PARAM(GOST_512bits_curve_gy);

static const u8 GOST_512bits_curve_gz[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};

TO_EC_STR_PARAM(GOST_512bits_curve_gz);

static const u8 GOST_512bits_curve_gen_order[] = {
	0x45, 0x31, 0xac, 0xd1, 0xfe, 0x00, 0x23, 0xc7,
	0x55, 0x0d, 0x26, 0x7b, 0x6b, 0x2f, 0xee, 0x80,
	0x92, 0x2b, 0x14, 0xb2, 0xff, 0xb9, 0x0f, 0x04,
	0xd4, 0xeb, 0x7c, 0x09, 0xb5, 0xd2, 0xd1, 0x5d,
	0xa8, 0x2f, 0x2d, 0x7e, 0xcb, 0x1d, 0xba, 0xc7,
	0x19, 0x90, 0x5c, 0x5e, 0xec, 0xc4, 0x23, 0xf1,
	0xd8, 0x6e, 0x25, 0xed, 0xbe, 0x23, 0xc5, 0x95,
	0xd6, 0x44, 0xaa, 0xf1, 0x87, 0xe6, 0xe6, 0xdf
};

TO_EC_STR_PARAM(GOST_512bits_curve_gen_order);

#define CURVE_GOST512_Q_BITLEN 511
static const u8 GOST_512bits_curve_gen_order_bitlen[] = { 0x01, 0xff };

TO_EC_STR_PARAM(GOST_512bits_curve_gen_order_bitlen);

static const u8 GOST_512bits_curve_cofactor[] = { 0x01 };

TO_EC_STR_PARAM(GOST_512bits_curve_cofactor);

static const u8 GOST_512bits_curve_alpha_montgomery[] = {
	0x00,
};

TO_EC_STR_PARAM_FIXED_SIZE(GOST_512bits_curve_alpha_montgomery, 0);

static const u8 GOST_512bits_curve_gamma_montgomery[] = {
	0x00,
};

TO_EC_STR_PARAM_FIXED_SIZE(GOST_512bits_curve_gamma_montgomery, 0);

static const u8 GOST_512bits_curve_alpha_edwards[] = {
	0x00,
};

TO_EC_STR_PARAM_FIXED_SIZE(GOST_512bits_curve_alpha_edwards, 0);

static const u8 GOST_512bits_curve_oid[] = "unknown2";

TO_EC_STR_PARAM(GOST_512bits_curve_oid);

static const u8 GOST_512bits_curve_name[] = "GOST512";

TO_EC_STR_PARAM(GOST_512bits_curve_name);

static const ec_str_params GOST_512bits_curve_str_params = {
	.p = &GOST_512bits_curve_p_str_param,
	.p_bitlen = &GOST_512bits_curve_p_bitlen_str_param,
	.r = &GOST_512bits_curve_r_str_param,
	.r_square = &GOST_512bits_curve_r_square_str_param,
	.mpinv = &GOST_512bits_curve_mpinv_str_param,
	.p_shift = &GOST_512bits_curve_p_shift_str_param,
	.p_normalized = &GOST_512bits_curve_p_normalized_str_param,
	.p_reciprocal = &GOST_512bits_curve_p_reciprocal_str_param,
	.a = &GOST_512bits_curve_a_str_param,
	.b = &GOST_512bits_curve_b_str_param,
	.curve_order = &GOST_512bits_curve_order_str_param,
	.gx = &GOST_512bits_curve_gx_str_param,
	.gy = &GOST_512bits_curve_gy_str_param,
	.gz = &GOST_512bits_curve_gz_str_param,
	.gen_order = &GOST_512bits_curve_gen_order_str_param,
	.gen_order_bitlen = &GOST_512bits_curve_gen_order_bitlen_str_param,
	.cofactor = &GOST_512bits_curve_cofactor_str_param,
	.alpha_montgomery = &GOST_512bits_curve_alpha_montgomery_str_param,
	.gamma_montgomery = &GOST_512bits_curve_gamma_montgomery_str_param,
	.alpha_edwards = &GOST_512bits_curve_alpha_edwards_str_param,
	.oid = &GOST_512bits_curve_oid_str_param,
	.name = &GOST_512bits_curve_name_str_param,
};

/*
 * Compute max bit length of all curves for p and q
 */
#ifndef CURVES_MAX_P_BIT_LEN
#define CURVES_MAX_P_BIT_LEN    0
#endif
#if (CURVES_MAX_P_BIT_LEN < CURVE_GOST512_P_BITLEN)
#undef CURVES_MAX_P_BIT_LEN
#define CURVES_MAX_P_BIT_LEN CURVE_GOST512_P_BITLEN
#endif
#ifndef CURVES_MAX_Q_BIT_LEN
#define CURVES_MAX_Q_BIT_LEN    0
#endif
#if (CURVES_MAX_Q_BIT_LEN < CURVE_GOST512_Q_BITLEN)
#undef CURVES_MAX_Q_BIT_LEN
#define CURVES_MAX_Q_BIT_LEN CURVE_GOST512_Q_BITLEN
#endif
#ifndef CURVES_MAX_CURVE_ORDER_BIT_LEN
#define CURVES_MAX_CURVE_ORDER_BIT_LEN    0
#endif
#if (CURVES_MAX_CURVE_ORDER_BIT_LEN < CURVE_GOST512_CURVE_ORDER_BITLEN)
#undef CURVES_MAX_CURVE_ORDER_BIT_LEN
#define CURVES_MAX_CURVE_ORDER_BIT_LEN CURVE_GOST512_CURVE_ORDER_BITLEN
#endif

#endif /* __EC_PARAMS_GOST512_H__ */

#endif /* WITH_CURVE_GOST512 */