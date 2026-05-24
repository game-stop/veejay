#ifndef VJ_SCRATCH_H
#define VJ_SCRATCH_H

#include <stdint.h>
#include <stddef.h>

enum {
    AUDIO_PATH_SILENCE = 0,
    AUDIO_PATH_DIRECT  = 1,
    AUDIO_PATH_FAST    = 2,
    AUDIO_PATH_SLOW    = 3
};

typedef enum {
    AUDIO_EDGE_NONE = 0,
    AUDIO_EDGE_DIRECTION, //1
    AUDIO_EDGE_JUMP, // 2
    AUDIO_EDGE_RESET,     // frame 0 / start
    AUDIO_EDGE_SILENCE,   // crossfade from zero
} audio_edge_type_t;


typedef struct {
	int    buflen;
	int16_t *fwdL;
	int16_t *fwdR;


	int16_t *silenceR;
	int16_t *silenceL;

	int16_t *history;
	int history_len;

	int heuristic_applied;

	volatile int    last_direction;
	int    xfade_active;
	
	volatile int pending_edge;
	int edge_linger;
	
	volatile int	fwd_history_valid;
	volatile int	rev_history_valid;

	float *fade_lut;

	int16_t last_sample[8];
	int16_t last_output_sample[8];
	int16_t prev_output_sample[8];

	int     last_sample_valid;
	int		last_best_offset;
	long long		ticks_since_last_flip;

} audio_edge_t;


void* vj_scratch_init(int channels, int sample_rate, float fps);
void  vj_scratch_free(void *ptr);
void  vj_scratch_reset(void *ptr);

int vj_scratch_process(void *ptr,
                       short *output,
                       int max_out_frames,
                       const short *input,
                       int src_frames,
                       double speed);

int  vj_audio_edge_is_hard(int edge_type);
void vj_audio_clear_edge(audio_edge_t *edge, int cur_dir);
void vj_audio_declick_observe(const void *owner, const uint8_t *buf, int samples,
                              int frame_bytes, int path, int speed, int dir);
void vj_audio_declick_apply(const void *owner,
                            uint8_t *buf,
                            int samples,
                            int frame_bytes,
                            int path,
                            int speed,
                            int dir,
                            int edge_type,
                            int direction_flipped);

void vj_audio_declick_forget_owner(const void *owner);

void vj_scratch_soft_reset(void *ptr);

void vj_audio_reverse_buffer(uint8_t *buf, int n_samples, int frame_bytes);

int  vj_audio_scratch_process_exact(void *scratcher,
                                    uint8_t *dst,
                                    int expected_samples,
                                    const uint8_t *src,
                                    int src_samples,
                                    double speed,
                                    int frame_bytes);

int  vj_audio_resample_block_s16(uint8_t *dst,
                                 int expected_samples,
                                 const uint8_t *src,
                                 int src_samples,
                                 double speed,
                                 int frame_bytes);

int  vj_audio_frame_delta_s16(const uint8_t *a,
                              const uint8_t *b,
                              int frame_bytes);

int  vj_audio_peak_s16(const uint8_t *buf,
                       int samples,
                       int frame_bytes);

void vj_audio_copy_last_frame(uint8_t *dst,
                              int dst_bytes,
                              const uint8_t *buf,
                              int samples,
                              int frame_bytes);

int  vj_audio_stretch_block_s16(uint8_t *dst,
                                int expected_samples,
                                const uint8_t *src,
                                int src_samples,
                                double speed,
                                int frame_bytes);

int  vj_audio_stretch_continuous_s16(uint8_t *dst,
                                     int expected_samples,
                                     const uint8_t *src,
                                     int src_samples,
                                     int context_samples,
                                     int slice_count,
                                     int frame_bytes);

int  vj_audio_render_slow_stream_s16(uint8_t *dst,
                                      int dst_samples,
                                      const uint8_t *src,
                                      int source_base_sample,
                                      int context_samples,
                                      int slice_count,
                                      int start_stretched_sample,
                                      int frame_bytes);
#endif // VJ_SCRATCH_H
