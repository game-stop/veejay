#include <config.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <libavcodec/avcodec.h>
#include <veejaycore/avcommon.h>

#define LUT_SIZE 1024

typedef struct {
	FILE *fd;
	char *filename;
    int len;
    int uvlen;
    int clamped;
    int is420;
    int w;
    int uw;
    int h;
    float *lut;
} raw_io_t;


static inline float sin_lut_f(raw_io_t *ctx, float angle_rad) {
    while (angle_rad < 0.0f) angle_rad += 6.2831853f;
    while (angle_rad >= 6.2831853f) angle_rad -= 6.2831853f;
    int idx = (int)(angle_rad / 6.2831853f * LUT_SIZE) % LUT_SIZE;
    return ctx->lut[idx];
}

static inline float cos_lut_f(raw_io_t *ctx, float angle_rad) {
    return sin_lut_f(ctx, angle_rad + 1.5707963f);
}

void    raw_io_close(void *ptr) {
    raw_io_t *ctx = (raw_io_t*) ptr;
    if(!ctx) return;
    fclose(ctx->fd);
    free(ctx->lut);
    free(ctx);
    ctx = NULL;
}

void *raw_io_open(const char *filename, int w, int h, int fmt) {
    int i;
    raw_io_t *ctx = (raw_io_t*) vj_calloc(sizeof(raw_io_t));
    if(!ctx)
        return NULL;
    ctx->lut = (float*) vj_malloc(sizeof(float) * LUT_SIZE );
    if(!ctx->lut) {
        free(ctx);
        return NULL;
    }
    ctx->fd = fopen(filename, "rb");
    if(!ctx->fd) {
        veejay_msg(0,"[rawio] failed to open %s",filename);
        free(ctx);
        return NULL;
    }

    ctx->filename = strdup(filename);
    ctx->len = (w * h);
    ctx->clamped = 0;
    ctx->w = w;
    ctx->h = h;
    switch(fmt) {
        case PIX_FMT_YUVJ422P:
            ctx->uvlen = ctx->len/2;
            break;
        case PIX_FMT_YUVJ420P:
            ctx->uvlen = ctx->len/4;
            ctx->is420 = 1;
            break;
        case PIX_FMT_YUV422P:
            ctx->uvlen = ctx->len/2;
            break;
        case PIX_FMT_YUV420P:
            ctx->uvlen = ctx->len/4;
            ctx->is420 = 1;
            break;
        default:
            veejay_msg(0, "[rawio] failed to target output format %x", fmt );
            fclose(ctx->fd);
            free(ctx);
            return NULL;
    }

     
    for(i = 0; i < LUT_SIZE ; i ++ ) {
        ctx->lut[ i ]  = sinf( (float) i / LUT_SIZE * 2.0f * M_PI );
    }

    return (void*) ctx;
}
static int raw_io_read_loop(raw_io_t *ptr, uint8_t *dst) {
    size_t bytes_need = ptr->len;
    size_t bytes_read = 0;
    while( bytes_read < bytes_need ) {
        size_t n = fread( dst + bytes_read, 1, bytes_need - bytes_read, ptr->fd );
        if( n == 0 ) {
            if(feof(ptr->fd)) {
                fseek( ptr->fd, 0, SEEK_SET ); 
            } else if ( ferror(ptr->fd ) ) {
                veejay_msg(0, "[rawio] read failed");
                return -1;
            }
            continue; 
        }
        bytes_read += n;
    }
    return bytes_read;
}

#define FP_SCALE 256
#define FP_DIFF_U_MUL (int)(0.8f * FP_SCALE) // ~204
#define FP_DIFF_V_MUL (int)(1.0f * FP_SCALE) // 256

int raw_io_read_frame(void *ptr, uint8_t *dstptr) {

    raw_io_t *ctx = (raw_io_t*) ptr;
    
    int y_len = raw_io_read_loop( ctx, dstptr );
    if(y_len==-1)
       return -1;

    uint8_t *dst[3] = { dstptr, dstptr + ctx->len, dstptr + ctx->len + ctx->uvlen };

    int uv_w = ctx->w / 2;
    int uv_h = ctx->is420 ? ctx->h / 2 : ctx->h;

    for (int y = 0; y < uv_h; y++) {
        int y_y_plane = ctx->is420 ? y * 2 : y;
        int row_offset = y_y_plane * ctx->w;

    	for (int x = 0; x < uv_w; x++) {
    	    int idx_uv = y * uv_w + x;
    	    int idx_y1 = row_offset + 2 * x;
    	    int idx_y2 = idx_y1 + 1;

    	    uint8_t y1 = dst[0][idx_y1];
    	    uint8_t y2 = dst[0][idx_y2];
    	    int diff = (int)y1 - (int)y2;
            
            float yf = (float)(y1 + y2) * 0.5f / 255.0f; 

            float wave_u = sin_lut_f(ctx, (float)(x + y_y_plane * 0.5f) * 0.05f + yf * (2.0f * M_PI));
            float wave_v = cos_lut_f(ctx, (float)(x - y_y_plane * 0.3f) * 0.07f + yf * M_PI);

            int u_diff_term_fp = (diff * FP_DIFF_U_MUL) >> 8;
            float u_val_f = 128.0f + (float)u_diff_term_fp + wave_u * 100.0f;
            
            int u_val = (int)u_val_f;
            if (u_val < 0) u_val = 0;
            if (u_val > 255) u_val = 255;
            dst[1][idx_uv] = (uint8_t)u_val;

            int v_diff_term_fp = (diff * FP_DIFF_V_MUL) >> 8;
            float v_val_f = 128.0f - (float)v_diff_term_fp + wave_v * 120.0f;
            
            int v_val = (int)v_val_f;
            if (v_val < 0) v_val = 0;
            if (v_val > 255) v_val = 255;
            dst[2][idx_uv] = (uint8_t)v_val;
    	}
    }
    
    return ctx->len + ctx->uvlen + ctx->uvlen;
}
