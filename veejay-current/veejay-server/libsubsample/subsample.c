/*
 * subsample.c:  Routines to do chroma subsampling.  ("Work In Progress")
 *
 *
 *  Copyright (C) 2001 Matthew J. Marjanovic <maddog@mir.com>
 *                2004 Niels Elburg <nwelburg@gmail.com>
 *                2014 added mmx routines
 *                2023 added drop,average,bilinear & mitchell natravali
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
#include <config.h>
#include <stdint.h>
#include <veejaycore/defs.h>
#ifdef HAVE_ASM_MMX
#include <veejaycore/mmx.h>
#include <veejaycore/mmx_macros.h>
#endif
#ifdef HAVE_ASM_SSE2
#include <emmintrin.h>
#endif
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <libsubsample/subsample-arch.h>
#include <veejaycore/mjpeg_types.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <libvje/vje.h>
#include <veejaycore/yuvconv.h>

#ifdef HAVE_TARGET_AVX2
#include <immintrin.h>
#include <libavutil/cpu.h>
#endif

#define BLANK_CRB in0[1]
#define BLANK_CRB_2 (in0[1] << 1)

const char *ssm_id[SSM_COUNT] = {
  "unknown",
  "420_jpeg",
  "420_mpeg2",
#if 0
  "420_dv_pal",
  "411_dv_ntsc"
#endif
};


const char *ssm_description[SSM_COUNT] = {
  "unknown/illegal",
  "4:2:0, JPEG/MPEG-1, interstitial siting",
  "4:2:0, MPEG-2, horizontal cositing",
#if 0
  "4:2:0, DV-PAL, cosited, Cb/Cr line alternating",
  "4:1:1, DV-NTSC"
  "4:2:2",
#endif
};

#define B1 4461

typedef void (*subsample_444_to_422)( uint8_t *restrict U, uint8_t *restrict V, const int width, const int height );
typedef void (*supersample_422_to_444)( uint8_t *restrict chroma, const int width, const int height );
typedef void (*chroma_plane_scaler)(uint8_t *buffer, int width, int height);

/*************************************************************************
 * Chroma Subsampling
 *************************************************************************/


/* vertical/horizontal interstitial siting
 *
 *    Y   Y   Y   Y
 *      C       C
 *    Y   Y   Y   Y
 *
 *    Y   Y   Y   Y
 *      C       C
 *    Y   Y   Y   Y
 *
 */

/*
static void ss_444_to_420jpeg(uint8_t *buffer, int width, int height)
{
  uint8_t *in0, *in1, *out;
  int x, y;

  in0 = buffer;
  in1 = buffer + width;
  out = buffer;
  for (y = 0; y < height; y += 2) {
    for (x = 0; x < width; x += 2) {
      *out = (in0[0] + in0[1] + in1[0] + in1[1]) >> 2;
      in0 += 2;
      in1 += 2;
      out++;
    }
    in0 += width;
    in1 += width;
  }
}
*/

static void ss_444_to_420jpeg(uint8_t *buffer, int width, int height)
{
    if (buffer == NULL || width < 2 || height < 2)
        return;

    uint8_t *out = buffer;
    for (int y = 0; y + 1 < height; y += 2) {
        const uint8_t *top = buffer + (size_t)y * width;
        const uint8_t *bottom = top + width;

        for (int x = 0; x + 1 < width; x += 2) {
            *out++ = (uint8_t)((top[x] +
                                3 * (top[x + 1] + bottom[x]) +
                                9 * bottom[x + 1] + 8) >> 4);
        }
    }
}
 

/* horizontal interstitial siting
 *
 *    Y   Y   Y   Y
 *    C   C   C   C     in0 
 *    Y   Y   Y   Y         
 *    C   C   C   C      
 *           
 *    Y   Y   Y   Y       
 *    C       C         out0  
 *    Y   Y   Y   Y       
 *    C       C  
 *
 *
 */             



 
/* vertical/horizontal interstitial siting
 *
 *    Y   Y   Y   Y
 *      C       C       C      inm
 *    Y   Y   Y   Y           
 *                  
 *    Y   Y   Y - Y           out0
 *      C     | C |     C      in0
 *    Y   Y   Y - Y           out1
 *
 *
 *      C       C       C      inp
 *
 *
 *  Each iteration through the loop reconstitutes one 2x2 block of 
 *   pixels from the "surrounding" 3x3 block of samples...
 *  Boundary conditions are handled by cheap reflection; i.e. the
 *   center sample is simply reused.
 *              
 */             
static void tr_420jpeg_to_444(uint8_t *data, uint8_t *buffer, int width, int height)
{
  uint8_t *inm, *in0, *inp, *out0, *out1;
  uint8_t cmm, cm0, cmp, c0m, c00, c0p, cpm, cp0, cpp;
  int x, y;

  uint8_t *saveme = data;

  veejay_memcpy(saveme, buffer, width);

  in0 = buffer + ( width * height /4) - 2;
  inm = in0 - width/2;
  inp = in0 + width/2;
  out1 = buffer + (width * height) - 1;
  out0 = out1 - width;

  for (y = height; y > 0; y -= 2) {
    if (y == 2) {
      in0 = saveme + width/2 - 2;
      inp = in0 + width/2;
    }
    for (x = width; x > 0; x -= 2) {

      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c00 = in0[1];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];

      inm--;
      in0--;
      inp--;

      *(out1--) = (1*cpp + 3*(cp0+c0p) + 9*c00 + 8) >> 4;
      *(out1--) = (1*cpm + 3*(cp0+c0m) + 9*c00 + 8) >> 4;
      *(out0--) = (1*cmp + 3*(cm0+c0p) + 9*c00 + 8) >> 4;
      *(out0--) = (1*cmm + 3*(cm0+c0m) + 9*c00 + 8) >> 4;
    }
    out1 -= width;
    out0 -= width;
  }
}

static void ss_420jpeg_to_444(uint8_t *buffer, int width, int height)
{
    if (buffer == NULL || width <= 0 || height <= 0)
        return;

    uint8_t *in = buffer + ((size_t)width * height >> 2);

    for (int row = (height >> 1) - 1; row >= 0; row--) {
        uint8_t *out0 = buffer + (size_t)(row << 1) * width;
        uint8_t *out1 = out0 + width;

        for (int column = (width >> 1) - 1; column >= 0; column--) {
            uint8_t value = *--in;
            int output_column = column << 1;
            out0[output_column] = value;
            out0[output_column + 1] = value;
            out1[output_column] = value;
            out1[output_column + 1] = value;
        }
    }
}

void ss_444_to_422_drop(uint8_t *restrict U, uint8_t *restrict V, int width, int height)
{
    const size_t total_dest_pixels = ((size_t)width * height) >> 1;
    for (size_t i = 0; i < total_dest_pixels; i++) {
        size_t src_idx = i << 1;
        U[i] = U[src_idx];
        V[i] = V[src_idx];
    }
}

#ifdef HAVE_TARGET_AVX2
__attribute__((target("avx2")))
void ss_444_to_422_drop_avx2(uint8_t *restrict U, uint8_t *restrict V, int width, int height) {
    const int stride = width >> 1;

    __m256i shuffle_mask = _mm256_setr_epi8(
        0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1,
        0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1
    );

    for (int y = 0; y < height; y++) {
        uint8_t *row_u = &U[y * width];
        uint8_t *row_v = &V[y * width];
        uint8_t *dst_u = &U[y * stride];
        uint8_t *dst_v = &V[y * stride];

        int x = 0;

        for (; x <= width - 32; x += 32) {
            __m256i u_data = _mm256_loadu_si256((__m256i*)&row_u[x]);
            __m256i u_shuf = _mm256_shuffle_epi8(u_data, shuffle_mask);

            __m256i u_packed = _mm256_permute4x64_epi64(u_shuf, _MM_SHUFFLE(3, 1, 2, 0));
            _mm_storeu_si128((__m128i*)&dst_u[x >> 1], _mm256_castsi256_si128(u_packed));

            __m256i v_data = _mm256_loadu_si256((__m256i*)&row_v[x]);
            __m256i v_shuf = _mm256_shuffle_epi8(v_data, shuffle_mask);
            __m256i v_packed = _mm256_permute4x64_epi64(v_shuf, _MM_SHUFFLE(3, 1, 2, 0));
            _mm_storeu_si128((__m128i*)&dst_v[x >> 1], _mm256_castsi256_si128(v_packed));
        }

        for (; x < width - 1; x += 2) {
            dst_u[x >> 1] = row_u[x];
            dst_v[x >> 1] = row_v[x];
        }
    }
}
#endif

/*
 * subsample YUV 4:4:4 to YUV 4:2:2 using average method
 */
void ss_444_to_422_average(uint8_t *restrict U, uint8_t *restrict V, int width, int height)
{
    const size_t total_dest_pixels = ((size_t)width * height) >> 1;

    for (size_t i = 0; i < total_dest_pixels; i++) {
        size_t src_idx = i << 1;
        U[i] = (uint8_t)((U[src_idx] + U[src_idx + 1] + 1) >> 1);
        V[i] = (uint8_t)((V[src_idx] + V[src_idx + 1] + 1) >> 1);
    }
}

#ifdef HAVE_TARGET_AVX2
__attribute__((target("avx2")))
void ss_444_to_422_average_avx2(uint8_t *restrict U, uint8_t *restrict V, int width, int height)
{
    const size_t total_dest_pixels = ((size_t)width * height) >> 1;
    const __m256i shuf_mask = _mm256_setr_epi8(
        0, 2, 4, 6, 8, 10, 12, 14,
        1, 3, 5, 7, 9, 11, 13, 15,
        0, 2, 4, 6, 8, 10, 12, 14,
        1, 3, 5, 7, 9, 11, 13, 15
    );

    size_t i = 0;

    for (; i + 16 <= total_dest_pixels; i += 16) {
        size_t src_idx = i << 1;
        __m256i u_data = _mm256_loadu_si256((const __m256i*)(U + src_idx));
        __m256i u_shuf = _mm256_shuffle_epi8(u_data, shuf_mask);
        __m256i u_avg  = _mm256_avg_epu8(u_shuf, _mm256_srli_si256(u_shuf, 8));
        __m256i u_fin  = _mm256_permute4x64_epi64(u_avg, _MM_SHUFFLE(3, 1, 2, 0));

        _mm_storeu_si128((__m128i*)(U + i), _mm256_castsi256_si128(u_fin));

        __m256i v_data = _mm256_loadu_si256((const __m256i*)(V + src_idx));
        __m256i v_shuf = _mm256_shuffle_epi8(v_data, shuf_mask);
        __m256i v_avg  = _mm256_avg_epu8(v_shuf, _mm256_srli_si256(v_shuf, 8));
        __m256i v_fin  = _mm256_permute4x64_epi64(v_avg, _MM_SHUFFLE(3, 1, 2, 0));

        _mm_storeu_si128((__m128i*)(V + i), _mm256_castsi256_si128(v_fin));
    }

    for (; i < total_dest_pixels; i++) {
        size_t src_idx = i << 1;
        U[i] = (uint8_t)((U[src_idx] + U[src_idx + 1] + 1) >> 1);
        V[i] = (uint8_t)((V[src_idx] + V[src_idx + 1] + 1) >> 1);
    }
}
#endif

/*
 * subsample YUV 4:4:4 to YUV 4:2:2 by bilinear interpolation
 */
#define B (1.0/ 3.0)
#define WEIGHT_SCALE (1<<16)
static void ss_444_to_422_bilinear(uint8_t *restrict U, uint8_t *restrict V, const int width, const int height) {
    const int dest_width = width >> 1;

    for (int i = 0; i < height; i++) {
#pragma omp simd
        for (int j = 0; j < dest_width; j++) {
            const int src_idx = 2 * j;

            int totalU = 0;
            int totalV = 0;
            int totalWeight = 0;

            const int weight1 = WEIGHT_SCALE;
            const int weight2 = WEIGHT_SCALE * 2;

            const int srcUIdx1 = i * width + src_idx;
            const int srcUIdx2 = i * width + src_idx + 1;
            const int srcVIdx1 = i * width + src_idx;
            const int srcVIdx2 = i * width + src_idx + 1;

            totalU += (U[srcUIdx1] - 128) * weight1;
            totalV += (V[srcVIdx1] - 128) * weight1;
            totalWeight += weight1;

            totalU += (U[srcUIdx2] - 128) * weight2;
            totalV += (V[srcVIdx2] - 128) * weight2;
            totalWeight += weight2;

            U[i * dest_width + j] = (uint8_t)(((totalU + (totalWeight >> 1)) / totalWeight) + 128);
            V[i * dest_width + j] = (uint8_t)(((totalV + (totalWeight >> 1)) / totalWeight) + 128);
        }
    }
}

/*
 * subsample YUV 4:4:4 to YUV 4:2:2 using mitchell netravali
 *
*/
static void ss_444_to_422_in_mitchell_netravali_old(uint8_t *restrict U, uint8_t *restrict V, const int width, const int height) {
    const int output_width = width >> 1;
    int i;
    for( i = 0; i < 1; i ++ ) {
        for (int j = 0; j < output_width; j++) {
            const int src_index = i * width + j * 2;

            U[i * output_width + j] = U[src_index];
            V[i * output_width + j] = V[src_index];
        }
    }

    for (; i < height; i++) {
#pragma omp simd
        for (int j = 0; j < output_width; j++) {
            const int chroma_col = 2 * j;
            const int output_idx = i * output_width + j;
            int totalU = 0, totalV = 0;

            for (int u = 0; u < 4; u++) {
                int source_row = i + u - 1;
                source_row = (source_row < 0) ? 0 : ((source_row >= height) ? height - 1 : source_row);

                const int fu = ((2 * j + u - 1) - chroma_col) * ((2 * j + u - 1) - chroma_col);
                const int weightInt = (((1 << 17) - fu) * B1 + (fu * B1 >> 16)) >> 16;

                const int srcU = U[source_row * width + chroma_col] - 128;
                const int srcV = V[source_row * width + chroma_col] - 128;

                totalU += srcU * weightInt;
                totalV += srcV * weightInt;
            }

            totalU = ( totalU + ( 1 << 14 ) ) >> 15;
            totalV = ( totalV + ( 1 << 14 ) ) >> 15;

            totalU += 128;
            totalV += 128;

            totalU = (totalU < 0 ? 0 : totalU > 0xff ? 0xff: totalU);
            totalV = (totalV < 0 ? 0 : totalV > 0xff ? 0xff: totalV);

            U[output_idx] = (uint8_t) totalU;
            V[output_idx] = (uint8_t) totalV;
        }
    }
}

static inline uint8_t clamp_u8(int v) {
    return (v < 0) ? 0 : ((v > 255) ? 255 : (uint8_t)v);
}

void tr_422_to_444_dup(uint8_t *restrict chromaChannel, const int width,const int height) {
    const int src_width = width >> 1;

    for(int y = height - 1; y >= 0; y--) {
        uint8_t *src = chromaChannel + (size_t)y * src_width;
        uint8_t *dst = chromaChannel + (size_t)y * width;
        for(int x = src_width - 1; x >= 0; x--) {
            const uint8_t pixel = src[x];
            dst[2 * x] = pixel;
            dst[2 * x + 1] = pixel;
        }
    }
}

#ifdef HAVE_TARGET_AVX2
__attribute__((target("avx2")))
void tr_422_to_444_dup_avx2(uint8_t *restrict chromaChannel, const int width, const int height) {
    const int src_width = width >> 1;

    const __m256i dup_mask = _mm256_setr_epi8(
        0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,
        8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15
    );

    for(int y = height - 1; y >= 0; y--) {
        uint8_t *row_src = chromaChannel + (size_t)y * src_width;
        uint8_t *row_dst = chromaChannel + (size_t)y * width;
        int x = src_width;

        while(x >= 16) {
            x -= 16;
            __m128i pixels128 = _mm_loadu_si128((const __m128i*)(row_src + x));
            __m256i pixels256 = _mm256_broadcastsi128_si256(pixels128);
            __m256i duplicated = _mm256_shuffle_epi8(pixels256, dup_mask);
            _mm256_storeu_si256((__m256i*)(row_dst + 2 * x), duplicated);
        }

        while(x > 0) {
            x--;
            const uint8_t pixel = row_src[x];
            row_dst[2 * x] = pixel;
            row_dst[2 * x + 1] = pixel;
        }
    }
}
#endif


#define UTAP_0 -9
#define UTAP_1 111
#define UTAP_2 29
#define UTAP_3 -3

void ss_422_to_444_mitchell(uint8_t *restrict chroma, const int width, const int h)
{
    const int in_w = width >> 1;
    const int out_w = width;

    for(int y = h - 1; y >= 0; y--) {
        uint8_t *src = chroma + (size_t)y * in_w;
        uint8_t *dst = chroma + (size_t)y * out_w;
        uint8_t right1 = src[in_w - 1];
        uint8_t right2 = right1;

        for(int j = in_w - 1; j >= 0; j--) {
            const uint8_t current = src[j];
            const uint8_t left1 = src[j > 0 ? j - 1 : 0];
            const uint8_t left2 = src[j > 1 ? j - 2 : 0];
            int value;

            if(j == 0) {
                value = UTAP_0 * current + UTAP_1 * current +
                        UTAP_2 * right1 + UTAP_3 * right2;
            }
            else if(j == in_w - 1) {
                value = UTAP_0 * left2 + UTAP_1 * left1 +
                        UTAP_2 * current + UTAP_3 * current;
            }
            else {
                value = UTAP_0 * left1 + UTAP_1 * current +
                        UTAP_2 * right1 + UTAP_3 * right2;
            }

            dst[2 * j] = current;
            dst[2 * j + 1] = clamp_u8((value + 64) >> 7);
            right2 = right1;
            right1 = current;
        }
    }
}

/* vertical intersitial siting; horizontal cositing
 *
 *    Y   Y   Y   Y
 *    C       C
 *    Y   Y   Y   Y
 *
 *    Y   Y   Y   Y
 *    C       C
 *    Y   Y   Y   Y
 *
 * [1,2,1] kernel for horizontal subsampling:
 *
 *    inX[0] [1] [2]
 *        |   |   |
 *    C   C   C   C
 *         \  |  /
 *          \ | /
 *            C
 */

static void ss_444_to_420mpeg2(uint8_t *buffer, int width, int height)
{
  uint8_t *in0, *in1, *out;
  int x, y;

  in0 = buffer;          /* points to */
  in1 = buffer + width;  /* second of pair of lines */
  out = buffer;
  for (y = 0; y < height; y += 2) {
    /* first column boundary condition -- just repeat it to right */
    *out = (in0[0] + (2 * in0[0]) + in0[1] +
        in1[0] + (2 * in1[0]) + in1[1]) >> 3;
    out++;
    in0++;
    in1++;
    /* rest of columns just loop */
    for (x = 2; x < width; x += 2) {
      *out = (in0[0] + (2 * in0[1]) + in0[2] +
          in1[0] + (2 * in1[1]) + in1[2]) >> 3;
      in0 += 2;
      in1 += 2;
      out++;
    }
    in0 += width + 1;
    in1 += width + 1;
  }
}


static subsample_444_to_422 subsample_444_to_422_in;
static supersample_422_to_444 supersample_422_to_444_out;
static chroma_plane_scaler subsample_444_to_420_in = ss_444_to_420jpeg;
static chroma_plane_scaler supersample_420_to_444_out = ss_420jpeg_to_444;

void chroma_subsample_init(void) {
    const char *mode = getenv("VEEJAY_SUBSAMPLE_MODE");
    subsample_444_to_422 f = ss_444_to_422_drop;
#if defined(SUBSAMPLE_HAVE_ESP32)
    f = ss_444_to_422_drop_esp32;
    subsample_444_to_420_in = ss_444_to_420jpeg_esp32;
#elif defined(SUBSAMPLE_HAVE_NEON)
    f = ss_444_to_422_drop_neon;
    subsample_444_to_420_in = ss_444_to_420jpeg_neon;
#elif defined(SUBSAMPLE_HAVE_ALTIVEC)
    f = ss_444_to_422_drop_altivec;
    subsample_444_to_420_in = ss_444_to_420jpeg_altivec;
#elif defined(HAVE_TARGET_AVX2)
    const int use_avx2 = (av_get_cpu_flags() & AV_CPU_FLAG_AVX2) != 0;
    if (use_avx2)
        f = ss_444_to_422_drop_avx2;
#endif
    const char *selected = "drop";

    if (mode == NULL) {
        veejay_msg(VEEJAY_MSG_INFO, "Chroma subsampling: defaulting to 'drop' (set VEEJAY_SUBSAMPLE_MODE=drop|average|bilinear|mitchell)");
#if defined(SUBSAMPLE_HAVE_ESP32)
        veejay_msg(VEEJAY_MSG_DEBUG, "ESP32 routines available for chroma subsampling");
#elif defined(SUBSAMPLE_HAVE_NEON)
        veejay_msg(VEEJAY_MSG_DEBUG, "NEON available for chroma subsampling");
#elif defined(SUBSAMPLE_HAVE_ALTIVEC)
    veejay_msg(VEEJAY_MSG_DEBUG, "AltiVec available for chroma subsampling");
#elif defined(HAVE_TARGET_AVX2)
        if (use_avx2)
            veejay_msg(VEEJAY_MSG_DEBUG, "AVX2 available for subsampling");
#endif
    }
    else if (strcmp(mode, "drop") == 0) {
        selected = "drop";
        f = ss_444_to_422_drop;
#if defined(SUBSAMPLE_HAVE_ESP32)
        f = ss_444_to_422_drop_esp32;
#elif defined(SUBSAMPLE_HAVE_NEON)
        f = ss_444_to_422_drop_neon;
#elif defined(SUBSAMPLE_HAVE_ALTIVEC)
    f = ss_444_to_422_drop_altivec;
#elif defined(HAVE_TARGET_AVX2)
        if (use_avx2)
            f = ss_444_to_422_drop_avx2;
#endif
    }
    else if (strcmp(mode, "average") == 0) {
        selected = "average";
        f = ss_444_to_422_average;
#if defined(HAVE_TARGET_AVX2) && !defined(SUBSAMPLE_HAVE_ESP32) && \
    !defined(SUBSAMPLE_HAVE_NEON)
        if (use_avx2)
            f = ss_444_to_422_average_avx2;
#endif
    }
    else if (strcmp(mode, "bilinear") == 0) {
        f = ss_444_to_422_bilinear;
        selected = "bilinear";
    }
    else if (strcmp(mode, "mitchell") == 0) {
        f = ss_444_to_422_in_mitchell_netravali_old;
        selected = "mitchell";
    }
    else {
        veejay_msg(VEEJAY_MSG_WARNING, "Invalid VEEJAY_SUBSAMPLE_MODE='%s', falling back to 'drop'", mode);
        f = ss_444_to_422_drop;
#if defined(SUBSAMPLE_HAVE_ESP32)
        f = ss_444_to_422_drop_esp32;
#elif defined(SUBSAMPLE_HAVE_NEON)
        f = ss_444_to_422_drop_neon;
#elif defined(SUBSAMPLE_HAVE_ALTIVEC)
    f = ss_444_to_422_drop_altivec;
#elif defined(HAVE_TARGET_AVX2)
        if (use_avx2)
            f = ss_444_to_422_drop_avx2;
#endif
    }

    subsample_444_to_422_in = f;

    veejay_msg(VEEJAY_MSG_INFO, "Chroma subsampling method: %s", selected);
}

void chroma_supersample_init(void)
{
    const char *mode = getenv("VEEJAY_SUPERSAMPLE_MODE");
    supersample_422_to_444 f = tr_422_to_444_dup;
#if defined(SUBSAMPLE_HAVE_ESP32)
    f = tr_422_to_444_dup_esp32;
    supersample_420_to_444_out = ss_420jpeg_to_444_esp32;
#elif defined(SUBSAMPLE_HAVE_NEON)
    f = tr_422_to_444_dup_neon;
    supersample_420_to_444_out = ss_420jpeg_to_444_neon;
#elif defined(SUBSAMPLE_HAVE_ALTIVEC)
    f = tr_422_to_444_dup_altivec;
    supersample_420_to_444_out = ss_420jpeg_to_444_altivec;
#elif defined(HAVE_TARGET_AVX2)
    const int use_avx2 = (av_get_cpu_flags() & AV_CPU_FLAG_AVX2) != 0;
    if (use_avx2)
        f = tr_422_to_444_dup_avx2;
#endif
    const char *selected = "dup";   // default

    if (mode == NULL) {
        veejay_msg(VEEJAY_MSG_INFO, "Chroma supersampling: defaulting to 'dup' (set VEEJAY_SUPERSAMPLE_MODE=dup|mitchell)");
#if defined(SUBSAMPLE_HAVE_ESP32)
        veejay_msg(VEEJAY_MSG_DEBUG, "ESP32 routines available for chroma supersampling");
#elif defined(SUBSAMPLE_HAVE_NEON)
        veejay_msg(VEEJAY_MSG_DEBUG, "NEON available for chroma supersampling");
#elif defined(SUBSAMPLE_HAVE_ALTIVEC)
    veejay_msg(VEEJAY_MSG_DEBUG, "AltiVec available for chroma supersampling");
#elif defined(HAVE_TARGET_AVX2)
        if (use_avx2)
            veejay_msg(VEEJAY_MSG_DEBUG, "AVX2 available for supersampling");
#endif
    }
    else if (strcmp(mode, "dup") == 0) {
        selected = "dup";
        f = tr_422_to_444_dup;
#if defined(SUBSAMPLE_HAVE_ESP32)
        f = tr_422_to_444_dup_esp32;
#elif defined(SUBSAMPLE_HAVE_NEON)
        f = tr_422_to_444_dup_neon;
#elif defined(SUBSAMPLE_HAVE_ALTIVEC)
    f = tr_422_to_444_dup_altivec;
#elif defined(HAVE_TARGET_AVX2)
        if (use_avx2)
            f = tr_422_to_444_dup_avx2;
#endif
    }
    else if (strcmp(mode, "mitchell") == 0) {
        f = ss_422_to_444_mitchell;
        selected = "mitchell";
    }
    else {
        veejay_msg(VEEJAY_MSG_WARNING, "Invalid VEEJAY_SUPERSAMPLE_MODE='%s', falling back to 'dup'", mode);
        f = tr_422_to_444_dup;
#if defined(SUBSAMPLE_HAVE_ESP32)
        f = tr_422_to_444_dup_esp32;
#elif defined(SUBSAMPLE_HAVE_NEON)
        f = tr_422_to_444_dup_neon;
#elif defined(SUBSAMPLE_HAVE_ALTIVEC)
    f = tr_422_to_444_dup_altivec;
#elif defined(HAVE_TARGET_AVX2)
        if (use_avx2)
            f = tr_422_to_444_dup_avx2;
#endif
    }

    supersample_422_to_444_out = f;

    veejay_msg(VEEJAY_MSG_INFO, "Chroma supersampling method: %s", selected);
}

void chroma_subsample(subsample_mode_t mode, VJFrame *frame, uint8_t *ycbcr[] )
{
    switch (mode) {
        // optimized path
        case SSM_422_444:
            subsample_444_to_422_in(ycbcr[1],ycbcr[2],frame->width,frame->height);
            break;
        case SSM_420_JPEG_BOX:
        case SSM_420_JPEG_TR: 
            subsample_444_to_420_in(ycbcr[1], frame->width, frame->height);
            subsample_444_to_420_in(ycbcr[2], frame->width, frame->height);
            break;
        case SSM_420_MPEG2:
            ss_444_to_420mpeg2(ycbcr[1], frame->width, frame->height);
            ss_444_to_420mpeg2(ycbcr[2], frame->width, frame->height);
            break;
        default:
            break;
    }
}


void chroma_supersample(subsample_mode_t mode,VJFrame *frame, uint8_t *ycbcr[] )
{
    uint8_t *_chroma_supersample_data = NULL;

    if( mode == SSM_420_JPEG_TR ) {
        _chroma_supersample_data = (uint8_t*) vj_malloc( sizeof(uint8_t) * (frame->width * 2) );
    }

    switch (mode) {
        // optimized path
        case SSM_422_444:
            supersample_422_to_444_out(ycbcr[1], frame->width, frame->height);
            supersample_422_to_444_out(ycbcr[2], frame->width, frame->height);
        break;
        case SSM_420_JPEG_BOX:
            supersample_420_to_444_out(ycbcr[1], frame->width, frame->height);
            supersample_420_to_444_out(ycbcr[2], frame->width, frame->height);
        break;
        case SSM_420_JPEG_TR:
            tr_420jpeg_to_444(_chroma_supersample_data,ycbcr[1], frame->width, frame->height);
            tr_420jpeg_to_444(_chroma_supersample_data,ycbcr[2], frame->width, frame->height);
        break;
        default:
            break;
     }

     if( _chroma_supersample_data != NULL )
        free( _chroma_supersample_data );
}
