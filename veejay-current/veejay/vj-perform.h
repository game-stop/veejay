#ifndef VJ_PERFORM_H
#define VJ_PERFORM_H

#include "vj-lib.h"

/* multithreaded code, what starts in queue belongs to playback_thread */

void	vj_perform_update_plugin_frame(VJFrame *frame);         

void	vj_perform_unlock_primary_frame( void );

VJFrame	*vj_perform_init_plugin_frame(veejay_t *info);
VJFrameInfo *vj_perform_init_plugin_frame_info(veejay_t *info);

int vj_perform_init(veejay_t * info);
int vj_perform_init_audio(veejay_t * info);
void vj_perform_free(veejay_t *info);
int vj_perform_audio_start(veejay_t * info);
void vj_perform_audio_status(struct timeval tmpstmp, unsigned int nb_out,
			     unsigned int nb_err);
void vj_perform_audio_stop(veejay_t * info);
void vj_perform_get_primary_frame(veejay_t * info, uint8_t ** frame,
				  int entry);
int	vj_perform_send_primary_frame_s(veejay_t *info, int mcast);
int vj_perform_tag_render_buffers(veejay_t * info, int processed_entry);

void	vj_perform_get_primary_frame_420p(veejay_t *info, uint8_t **frame ); 

int vj_perform_clip_render_buffers(veejay_t * info,
				     int processed_entry);

int vj_perform_decode_primary(veejay_t * info, int entry);

int vj_perform_clip_decode_buffers(veejay_t * info, int entry);

int vj_perform_fill_audio_buffers(veejay_t *info, uint8_t *audio_buf);

int vj_perform_tag_decode_buffers(veejay_t * info, int entry);

int vj_perform_queue_frame(veejay_t * info, int skip_incr, int frame);

int vj_perform_queue_audio_frame(veejay_t * info, int frame);

int vj_perform_queue_video_frame(veejay_t * info, int frame, int ks);

int vj_perform_pattern_decode_buffers(veejay_t * info, int entry);

int vj_perform_pattern_render_buffers(veejay_t * info, int entry);

void vj_perform_pre_chain(veejay_t *info, VJFrame *frame) ;

void vj_perform_post_chain(veejay_t *info, VJFrame *frame );

int vj_perform_tag_is_cached(int chain_entry, int entry, int tag_id);
int vj_perform_clip_is_cached(int nframe, int chain_entry);

void vj_perform_clear_frame_info(int entry);

void vj_perform_clear_cache(int entry);

int vj_perform_increase_tag_frame(veejay_t * info, long num);

int vj_perform_increase_plain_frame(veejay_t * info, long num);
int vj_perform_tag_fill_buffer(veejay_t * info, int entry);

int vj_perform_increase_tag_frame(veejay_t * info, long num);
void vj_perform_use_cached_ycbcr_frame(int entry, int centry, int width,
				       int height, int chain_entry);


int vj_perform_apply_secundary_tag(veejay_t * info, int clip_id,
				   int type, int chain_entry, int entry, const int a);


int vj_perform_tag_fill_buffer(veejay_t * info, int entry);


void vj_perform_plain_fill_buffer(veejay_t * info, int entry);


int vj_perform_tag_complete_buffers(veejay_t * info, int entry, const int skip);

void vj_perform_plain_fill_buffer(veejay_t * info, int entry);

int vj_perform_tag_fill_buffer(veejay_t * info, int entry);

int vj_perform_increase_clip_frame(veejay_t * info, long num);

int vj_perform_clip_complete_buffers(veejay_t * info, int entry, int skip_incr);

void vj_perform_use_cached_ycbcr_frame(int entry, int centry, int width,
				       int height, int chain_entry);


int vj_perform_clip_complete_buffers(veejay_t * info, int entry, int skip_incr);

void vj_perform_use_cached_encoded_frame(veejay_t * info, int entry,
					 int centry, int chain_entry);

int vj_perform_apply_secundary_tag(veejay_t * info, int clip_id,
				   int type, int chain_entry, int entry, const int skip_incr);


int vj_perform_clip_complete_buffers(veejay_t * info, int entry, const int skip_incr);

int vj_perform_decode_tag_secundary(veejay_t * info, int entry,
				    int chain_entry, int type,
				    int clip_id);


int vj_perform_decode_secundary(veejay_t * info, int entry,
				int chain_entry, int type, int clip_id);



void vj_perform_increase_pattern_frame(veejay_t * info, int num);

int	vj_perform_apply_first(veejay_t *info, vjp_kf *todo_info, VJFrame **frames, VJFrameInfo *frameinfo, int e, int c, int n_frames );


void vj_perform_reverse_audio_frame(veejay_t * info, int len, uint8_t *buf );

int vj_perform_get_subtagframe(veejay_t * info, int sub_clip, int chain_entry );


int vj_perform_get_subframe(veejay_t * info, int sub_clip,int chain_entyr, const int skip_incr );

int vj_perform_get_subframe_tag(veejay_t * info, int sub_clip, int chain_entry, const int skip_incr );

int vj_perform_render_clip_frame(veejay_t *info, uint8_t *frame[3]);

int vj_perform_apply_secundary(veejay_t * info, int clip_id, int type, int chain_entry, int entry, const int skip_incr );

int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[3]);

void vj_perform_record_commit_clip(veejay_t *info,long start_el_pos,int len);

long vj_perform_record_commit_single(veejay_t *info, int entry);

void vj_perform_record_stop(veejay_t *info);

void vj_perform_record_clip_frame(veejay_t *info, int entry); 

void vj_perform_record_tag_frame(veejay_t *info, int entry); 
void	vj_perform_get_output_frame_420p( veejay_t *info, uint8_t **frame, int w, int h );


int	vj_perform_get_cropped_frame( veejay_t *info, uint8_t **frame, int crop );
int	vj_perform_init_cropped_output_frame(veejay_t *info, VJFrame *src, int *dw, int *dh );
void	vj_perform_get_crop_dimensions(veejay_t *info, int *w, int *h);
int	vj_perform_rand_update(veejay_t *info);
int	vj_perform_randomize(veejay_t *info);

#endif
