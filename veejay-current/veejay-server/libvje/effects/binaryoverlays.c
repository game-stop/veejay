/*
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include "binaryoverlays.h"

#define U8_NOT(a)       ((uint8_t)~((uint8_t)(a)))

#define BOP_NA_NB(a,b)  ((uint8_t)(U8_NOT(a) & U8_NOT(b)))
#define BOP_NA_OB(a,b)  ((uint8_t)(U8_NOT(a) | U8_NOT(b)))
#define BOP_NA_XB(a,b)  ((uint8_t)(U8_NOT(a) ^ U8_NOT(b)))

#define BOP_A_NB(a,b)   ((uint8_t)(((uint8_t)(a)) & U8_NOT(b)))
#define BOP_A_ONB(a,b)  ((uint8_t)(((uint8_t)(a)) | U8_NOT(b)))
#define BOP_A_XNB(a,b)  ((uint8_t)(((uint8_t)(a)) ^ U8_NOT(b)))

#define BOP_NA_B(a,b)   ((uint8_t)(U8_NOT(a) & ((uint8_t)(b))))
#define BOP_NA_OB2(a,b) ((uint8_t)(U8_NOT(a) | ((uint8_t)(b))))
#define BOP_NA_XB2(a,b) ((uint8_t)(U8_NOT(a) ^ ((uint8_t)(b))))

#define BOP_OR(a,b)     ((uint8_t)(((uint8_t)(a)) | ((uint8_t)(b))))
#define BOP_AND(a,b)    ((uint8_t)(((uint8_t)(a)) & ((uint8_t)(b))))
#define BOP_XOR(a,b)    ((uint8_t)(((uint8_t)(a)) ^ ((uint8_t)(b))))

#define BOP_NAND(a,b)   ((uint8_t)~BOP_AND(a,b))
#define BOP_NOR(a,b)    ((uint8_t)~BOP_OR(a,b))
#define BOP_NXOR(a,b)   ((uint8_t)~BOP_XOR(a,b))

static inline uint8_t binary_center_chroma(uint8_t v)
{
    return (uint8_t)(v - 128);
}

static inline uint8_t binary_uncenter_chroma(uint8_t v)
{
    return (uint8_t)(v + 128);
}

vj_effect *binaryoverlay_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->defaults[0] = 0;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 14;

    ve->description = "Binary Overlays";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode");
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][0],
        0,
        "Not A and Not B",
        "Not A or Not B",
        "Not A xor Not B",
        "A and Not B",
        "A or Not B",
        "A xor Not B",
        "Not A and B",
        "Not A or B",
        "Not A xor B",
        "A or B",
        "A and B",
        "A xor B",
        "Not (A and B)",
        "Not (A or B)",
        "Not (A xor B)"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000
    );

    (void) w;
    (void) h;

    return ve;
}

#define APPLY_BINARY_OP(OP) do {                                       \
    _Pragma("omp parallel for num_threads(n_threads) schedule(static)") \
    for(int i = 0; i < len; i++) {                                      \
        const uint8_t ya = Y[i];                                        \
        const uint8_t yb = Y2[i];                                       \
        const uint8_t ua = binary_center_chroma(Cb[i]);                 \
        const uint8_t ub = binary_center_chroma(Cb2[i]);                \
        const uint8_t va = binary_center_chroma(Cr[i]);                 \
        const uint8_t vb = binary_center_chroma(Cr2[i]);                \
        Y[i] = OP(ya, yb);                                              \
        Cb[i] = binary_uncenter_chroma(OP(ua, ub));                     \
        Cr[i] = binary_uncenter_chroma(OP(va, vb));                     \
    }                                                                   \
} while(0)

void binaryoverlay_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void) ptr;

    const int len = frame->len;

    int mode = args[0];

    if(mode < 0)
        mode = 0;
    else if(mode > 14)
        mode = 14;

    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    switch(mode) {
        case 0:  APPLY_BINARY_OP(BOP_NA_NB);  break;
        case 1:  APPLY_BINARY_OP(BOP_NA_OB);  break;
        case 2:  APPLY_BINARY_OP(BOP_NA_XB);  break;
        case 3:  APPLY_BINARY_OP(BOP_A_NB);   break;
        case 4:  APPLY_BINARY_OP(BOP_A_ONB);  break;
        case 5:  APPLY_BINARY_OP(BOP_A_XNB);  break;
        case 6:  APPLY_BINARY_OP(BOP_NA_B);   break;
        case 7:  APPLY_BINARY_OP(BOP_NA_OB2); break;
        case 8:  APPLY_BINARY_OP(BOP_NA_XB2); break;
        case 9:  APPLY_BINARY_OP(BOP_OR);     break;
        case 10: APPLY_BINARY_OP(BOP_AND);    break;
        case 11: APPLY_BINARY_OP(BOP_XOR);    break;
        case 12: APPLY_BINARY_OP(BOP_NAND);   break;
        case 13: APPLY_BINARY_OP(BOP_NOR);    break;
        case 14: APPLY_BINARY_OP(BOP_NXOR);   break;
    }
}

#undef APPLY_BINARY_OP

#undef U8_NOT

#undef BOP_NA_NB
#undef BOP_NA_OB
#undef BOP_NA_XB

#undef BOP_A_NB
#undef BOP_A_ONB
#undef BOP_A_XNB

#undef BOP_NA_B
#undef BOP_NA_OB2
#undef BOP_NA_XB2

#undef BOP_OR
#undef BOP_AND
#undef BOP_XOR

#undef BOP_NAND
#undef BOP_NOR
#undef BOP_NXOR
