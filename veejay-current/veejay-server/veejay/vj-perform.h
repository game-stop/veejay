#ifndef VJ_PERFORM_H
#define VJ_PERFORM_H

#include "vj-lib.h"

/* multithreaded code, what starts in queue belongs to playback_thread */
uint8_t *vj_perform_get_preview_buffer();
int vj_perform_preview_max_width();
int vj_perform_preview_max_height();

void vj_perform_init_output_frame( veejay_t *info, uint8_t **frame,
                                int dst_w, int dst_h );

void	vj_perform_update_plugin_frame(VJFrame *frame);         

VJFrame	*vj_perform_init_plugin_frame(veejay_t *info);
VJFrameInfo *vj_perform_init_plugin_frame_info(veejay_t *info);

int vj_perform_init(veejay_t * info);

int vj_perform_init_audio(veejay_t * info);

void vj_perform_free(veejay_t *info);

void	vj_perform_done_s2( veejay_t *info );

int vj_perform_audio_start(veejay_t * info);

void vj_perform_audio_status(struct timeval tmpstmp, unsigned int nb_out,
			     unsigned int nb_err);

void vj_perform_audio_stop(veejay_t * info);

void vj_perform_get_primary_frame(veejay_t * info, uint8_t ** frame );

void	vj_perform_get_primary_frame_420p(veejay_t *info, uint8_t **frame ); 

int vj_perform_fill_audio_buffers(veejay_t *info, uint8_t *audio_buf, uint8_t *temporary_buffer, int *temporary_sum);

int vj_perform_tag_decode_buffers(veejay_t * info, int entry);

int vj_perform_queue_frame(veejay_t * info, int skip);

int vj_perform_queue_audio_frame(veejay_t * info);

int vj_perform_queue_video_frame(veejay_t * info, int skip);

void vj_perform_clear_frame_info();

void vj_perform_record_stop(veejay_t *info);

void vj_perform_record_sample_frame(veejay_t *info, int sample); 

void vj_perform_record_tag_frame(veejay_t *info ); 
void	vj_perform_get_output_frame_420p( veejay_t *info, uint8_t **frame, int w, int h );
void       vj_perform_get_output_frame( uint8_t **frame );

int	vj_perform_get_cropped_frame( veejay_t *info, uint8_t **frame, int crop );
int	vj_perform_init_cropped_output_frame(veejay_t *info, VJFrame *src, int *dw, int *dh );
void	vj_perform_get_crop_dimensions(veejay_t *info, int *w, int *h);
int	vj_perform_rand_update(veejay_t *info);
void	vj_perform_randomize(veejay_t *info);

void       vj_perform_free_plugin_frame(VJFrameInfo *f );

uint8_t	*vj_perform_get_a_work_buffer( );

int        vj_perform_send_primary_frame_s2(veejay_t *info, int mcast, int dst_link);
void       vj_perform_get_backstore( uint8_t **frame );
int        vj_perform_get_sampling();

int	vj_perform_get_width( veejay_t *info );
int	vj_perform_get_height( veejay_t *info );

void	vj_perform_follow_fade(int status);
size_t	vj_perform_fx_chain_size();

void	vj_perform_record_video_frame(veejay_t *info);

#endif
