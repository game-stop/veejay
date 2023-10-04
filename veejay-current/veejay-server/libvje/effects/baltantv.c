/* 
 * Linux VeeJay
 *
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * BaltanTV - like StreakTV, but following for a long time
 * Copyright (C) 2001-2002 FUKUCHI Kentaro
 * Ported to veejay by Niels Elburg 
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
#include <veejaycore/vjmem.h>
#include "baltantv.h"

#define PLANES 50


//Inspired by BaltanTV
vj_effect *baltantv_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = PLANES;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = PLANES/2;
    ve->defaults[0] = 4;
    ve->defaults[1] = 1;
    ve->description = "BaltanTV (EffecTV)";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Stride", "Shift" );
    return ve;
}


typedef struct {
    unsigned int	plane_ ;
    uint8_t	*planetableY_;
    int8_t	*planetableU_;
    int8_t	*planetableV_;

	uint8_t *currentY;
	int8_t *currentU;
	int8_t *currentV;

	int stride;
} baltantv_t;

void *baltantv_malloc(int w, int h)
{
    baltantv_t *b = (baltantv_t*) vj_calloc(sizeof(baltantv_t));
    if(!b) {
        return NULL;
    }

	b->planetableY_ = (uint8_t*) vj_calloc( sizeof(uint8_t*) * PLANES * (w * h));
    if(!b->planetableY_) {
        free(b);
        return NULL;
    }
	b->planetableU_ = (int8_t*) vj_malloc( sizeof(int8_t*) * PLANES * (w * h));
    if(!b->planetableU_) {
        free(b->planetableY_);
        free(b);
        return NULL;
    }
	b->planetableV_ = (int8_t*) vj_malloc( sizeof(int8_t*) * PLANES * (w * h));
    if(!b->planetableV_) {
        free(b->planetableY_);
        free(b->planetableU_);
		free(b);
        return NULL;
    }

	b->currentY = (uint8_t*) vj_calloc(sizeof(uint8_t) * w * h );
	b->currentU = (int8_t*) vj_malloc(sizeof(int8_t) * w * h );
	b->currentV = (int8_t*) vj_malloc(sizeof(int8_t) * w * h );

	veejay_memset( b->planetableU_, 128, w * h );
	veejay_memset( b->planetableV_, 128, w * h );

	return (void*) b;
}

void	baltantv_free(void *ptr)
{
    baltantv_t *b = (baltantv_t*) ptr;
    free(b->planetableY_);
    free(b->planetableU_);
    free(b->planetableV_);
	free(b->currentY);
	free(b->currentU);
	free(b->currentV);
    free(b);
}

void baltantv_apply( void *ptr, VJFrame *frame, int *args) {
    int stride = args[0];
    int shift = args[1];

	unsigned int i,cf;
	const int len = frame->len;
    const int uv_len = frame->uv_len;
	uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];

    baltantv_t *b = (baltantv_t*) ptr;

	uint8_t *pDstY = b->planetableY_ + (b->plane_ * len);
    int8_t *pDstU = b->planetableU_ + (b->plane_ * uv_len);
    int8_t *pDstV = b->planetableV_ + (b->plane_ * uv_len);

	uint8_t *cY = b->currentY;
	int8_t *cU = b->currentU;
	int8_t *cV = b->currentV;

    uint32_t y;
    int32_t u,v;


	if( b->stride != stride ) {
		b->plane_ = 0;
		b->stride = stride;
	}

	for( i = 0; i < len ; i ++ ) {
		pDstY[i] = Y[i];
		cY[i] = Y[i];
    }
    for( i = 0; i < uv_len; i ++ ) {
        pDstU[i] = (U[i]-128);
        pDstV[i] = (V[i]-128);
    }

	uint8_t *pSrcY[4];
    int8_t *pSrcU[4];
    int8_t *pSrcV[4];
    for (int i = 0; i < 4; ++i) {
        int offset = (b->plane_ - i + stride) % PLANES;
        offset = (offset + PLANES) % PLANES;
        pSrcY[i] = b->planetableY_ + (offset * len);
        pSrcU[i] = b->planetableU_ + (offset * uv_len);
        pSrcV[i] = b->planetableV_ + (offset * uv_len);
    }
	
	uint32_t ySum, uSum, vSum;
    for (int i = 0; i < len; ++i) {
        ySum = cY[i] + pSrcY[0][i] + pSrcY[1][i] + pSrcY[2][i] + pSrcY[3][i];
        Y[i] = (uint8_t)(ySum / 5); 

        uSum = pSrcU[0][i] + pSrcU[1][i] + pSrcU[2][i] + pSrcU[3][i];
        U[i] = 128 + (uSum / 4);

        vSum = pSrcV[0][i] + pSrcV[1][i] + pSrcV[2][i] + pSrcV[3][i];
        V[i] = 128 + (vSum / 4);
	}

	int offset = (b->plane_ + shift + PLANES) % PLANES;
    for (int i = 0; i < len; ++i) {
        b->planetableY_[offset * len + i] = Y[i];
    }
    for (int i = 0; i < uv_len; ++i) {
        b->planetableU_[offset * uv_len + i] = U[i] - 128;
        b->planetableV_[offset * uv_len + i] = V[i] - 128;
    }

	b->plane_ ++;
	b->plane_ = b->plane_ % stride;
}
