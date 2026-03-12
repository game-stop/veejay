#ifndef VJ_PERFORM_H
#define VJ_PERFORM_H

#include "vj-lib.h"

/* multithreaded code, what starts in queue belongs to playback_thread */
uint8_t *vj_perform_get_preview_buffer(veejay_t *info);
int vj_perform_preview_max_width(veejay_t *info);
int vj_perform_preview_max_height(veejay_t *info);

void	vj_perform_update_plugin_frame(VJFrame *frame);         

VJFrame	*vj_perform_init_plugin_frame(veejay_t *info);
VJFrameInfo *vj_perform_init_plugin_frame_info(veejay_t *info);

int vj_perform_init(veejay_t * info);

int vj_perform_init_audio(veejay_t * info, int aorb);

void vj_perform_free(veejay_t *info);

int vj_perform_audio_start(veejay_t * info);

void vj_perform_audio_status(struct timeval tmpstmp, unsigned int nb_out,
			     unsigned int nb_err);
int vj_perform_transition_sample( veejay_t *info, VJFrame *a, VJFrame *b );

void vj_perform_audio_stop(veejay_t * info);

void vj_perform_get_primary_frame(veejay_t * info, uint8_t ** frame );

void	vj_perform_get_primary_frame_420p(veejay_t *info, uint8_t **frame );

int vj_perform_tag_decode_buffers(veejay_t * info, int entry);

int vj_perform_queue_frame(veejay_t * info, int skip);

void vj_perform_inc_frame(veejay_t *info, int incr_val);

int vj_perform_queue_audio_frame(veejay_t * info, void *ptr,uint8_t *abuf, int speed, long long target_frame, int sample_id);

int vj_perform_queue_video_frame(veejay_t * info, VJFrame *dst);

int vj_perform_queue_audio_chunk(veejay_t *info, int client_frames_to_write, int audio_slice);

double vj_perform_initial_audio_queue(veejay_t *info);

void vj_perform_record_stop(veejay_t *info);

void vj_perform_record_sample_frame(veejay_t *info, int sample, int type); 

void vj_perform_record_tag_frame(veejay_t *info ); 

int	vj_perform_get_cropped_frame( veejay_t *info, uint8_t **frame, int crop );
void	vj_perform_get_crop_dimensions(veejay_t *info, int *w, int *h);
int	vj_perform_rand_update(veejay_t *info);
void	vj_perform_randomize(veejay_t *info);

void       vj_perform_free_plugin_frame(VJFrameInfo *f );

void       vj_perform_send_primary_frame_s2(veejay_t *info, int mcast, int dst_link, VJFrame *frame);
void       vj_perform_get_backstore( uint8_t **frame );
int        vj_perform_get_sampling();

int	vj_perform_get_width( veejay_t *info );
int	vj_perform_get_height( veejay_t *info );

void	vj_perform_follow_fade(veejay_t *info, int status);
size_t	vj_perform_fx_chain_size();

void	vj_perform_record_video_frame(veejay_t *info);

void vj_perform_start_offline_recorder(veejay_t *v, int rec_format, int stream_id, int duration, int autoplay, int sample_id);
int vj_perform_commit_offline_recording(veejay_t *info, int id, char *recording);

int vj_perform_try_sequence(veejay_t *info);

int vj_perform_get_next_sequence_id(veejay_t *info, int *type, int current, int *new_current);

int vj_perform_next_sequence( veejay_t *info, int *type, int *next_slot );

void vj_perform_setup_transition(veejay_t *info, int next_sample_id, int next_type, int sample_id, int current_type, int next_seq_idx );

void    vj_perform_reset_transition(veejay_t *info);

void	vj_tag_update_ascociated_samples(int s1);
int vj_perform_seq_setup_transition(veejay_t *info);

void vj_perform_initiate_edge_change(veejay_t *info, int edge_type, int prev_dir, int cur_dir);

int vj_perform_play_audio( video_playback_setup *settings, uint8_t *source, int len, uint8_t *silence );

int vj_perform_queue_audio_chunk_ext(veejay_t *info, int client_frames_to_write, long long target_frame, int fade_in, uint8_t *dst_buf);
int vj_perform_queue_audio_chunk_crossfade(
    veejay_t *info,
    int client_frames_to_write,
    long long target_frame_a,
    long long target_frame_b,
    uint8_t *audio_payload_chunk,
    int sample_b,
    long long trans_start_frame,
    long long trans_end_frame
);
void vj_produce_audio_chain(veejay_t *info, int sample_id);
void vj_audio_consume_chain(veejay_t *info, uint8_t *audio_chunk, int num_samples);

long vj_calc_next_subframe(veejay_t *info, int b);
#endif
