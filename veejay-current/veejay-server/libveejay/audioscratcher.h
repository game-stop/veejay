#ifndef VJ_SCRATCH_H
#define VJ_SCRATCH_H

#include <stdint.h>
#include <stddef.h>

void* vj_scratch_init(int channels, int buffer_frames, float fps);
int vj_scratch_process(void *ptr,
                       short *output,
                       int dst_frames,
                       const short *input,
                       int src_frames,
                       double speed);
                       
#endif // VJ_SCRATCH_H
