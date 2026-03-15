#include "common.h"
#include <veejaycore/vjmem.h>

typedef struct {
    uint32_t *trace_buffer[3];
    int n_threads;
} echotrace_t;

#define MAX_OLD_FRAMES 256
#define FP_SHIFT 8

vj_effect *echotrace_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = MAX_OLD_FRAMES;
    
    ve->defaults[0] = 255;
    ve->defaults[1] = 16;
    
    ve->description = "Frame Echo";
    ve->param_description = vje_build_param_list(ve->num_params, "Intensity", "Decay");
    return ve;
}

void *echotrace_malloc(int w, int h)
{
    echotrace_t *t = (echotrace_t*) vj_calloc(sizeof(echotrace_t));
    if(!t) return NULL;

    const int len = (w * h * 3);
    t->trace_buffer[0] = (uint32_t *) vj_calloc(sizeof(uint32_t) * len);
    t->trace_buffer[1] = t->trace_buffer[0] + ( w * h );
    t->trace_buffer[2] = t->trace_buffer[1] + ( w * h );
    
    t->n_threads = vje_advise_num_threads(len);
    return (void*) t;
}

void echotrace_free(void *ptr) {
    echotrace_t *t = (echotrace_t*) ptr;
    if(t) {
        if(t->trace_buffer[0])
            free(t->trace_buffer[0]);
        free(t);
    }
}

static inline int CLAMP(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
} 

void echotrace_apply(void *ptr, VJFrame *frame, int *args)
{
    echotrace_t *t = (echotrace_t*) ptr;
    const int intensity = args[0];
    const int decay_val = args[1];

    if (intensity <= 0) return;

    const int len = frame->len; 
    const int d_m = (decay_val > 1) ? (decay_val - 1) : 0;
    const int rounding = (1 << (FP_SHIFT - 1));

    #pragma omp parallel num_threads(t->n_threads)
    {
        uint8_t *restrict Y = frame->data[0];
        uint32_t *restrict accY = t->trace_buffer[0];
        #pragma omp for
        for(int x = 0; x < len; x++) {
            uint32_t fp_new = ((Y[x] * intensity) >> 8) << FP_SHIFT;
            accY[x] = ((accY[x] * d_m) + fp_new) / decay_val;
            Y[x] = (uint8_t)((accY[x] + rounding) >> FP_SHIFT);
        }

        for(int c = 1; c < 3; c++) {
            uint8_t *restrict C = frame->data[c];
            int32_t *restrict accC = (int32_t*)t->trace_buffer[c];
            
            #pragma omp for
            for(int x = 0; x < len; x++) {
                int32_t signed_in = (int32_t)C[x] - 128;
                
                int32_t fp_new = ((signed_in * intensity) >> 8) << FP_SHIFT;
                
                accC[x] = ((accC[x] * d_m) + fp_new) / decay_val;
                
                int32_t res = (accC[x] + rounding) >> FP_SHIFT;
                C[x] = (uint8_t)CLAMP(res + 128, 0, 255);
            }
        }
    }
}