/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef VJ_TAG_H
#define VJ_TAG_H


#define VJ_TAG_TYPE_RED 9
#define VJ_TAG_TYPE_GREEN 8
#define VJ_TAG_TYPE_YELLOW 7
#define VJ_TAG_TYPE_BLUE 6
#define VJ_TAG_TYPE_BLACK 5
#define VJ_TAG_TYPE_WHITE 4
#define VJ_TAG_TYPE_VLOOPBACK 3
#define VJ_TAG_TYPE_V4L 2
#define VJ_TAG_TYPE_YUV4MPEG 1
#define VJ_TAG_TYPE_NONE 0
#define VJ_TAG_TYPE_SHM 11
#define VJ_TAG_TYPE_NET 13
#define VJ_TAG_TYPE_MCAST 14
#define VJ_TAG_MAX_V4L 16
#define VJ_TAG_MAX_STREAM_IN 16
#define VJ_TAG_TYPE_AVFORMAT 12

#include <config.h>
#include <libsample/sampleadm.h>
#include <libstream/vj-yuv4mpeg.h>
#include <libstream/vj-v4lvideo.h>
#include <libvjnet/vj-client.h>
#include <libel/vj-avformat.h>

typedef struct {
    v4l_video *v4l[VJ_TAG_MAX_V4L];
    vj_yuv *stream[VJ_TAG_MAX_STREAM_IN];
    vj_avformat *avformat[VJ_TAG_MAX_STREAM_IN];
	vj_client *net[VJ_TAG_MAX_STREAM_IN];
    int width;
    int height;
    int depth;
    int pix_fmt;
} vj_tag_data;

typedef struct {
    int id;
//      char description[100];
    clip_eff_chain *effect_chain[CLIP_MAX_EFFECTS];
    int next_id;
    int nframes;
    int source_type;
    char *source_name;
    int index;
    int depth;
    int active;
    int source;
    int video_channel;

    int encoder_active;
    unsigned long sequence_num;
    unsigned long rec_total_bytes;
//    char *encoder_base;
    unsigned long encoder_total_frames;
//    char *encoder_destination;
    char encoder_base[256];
    char encoder_destination[256];
    int encoder_format;
    lav_file_t *encoder_file;
    long encoder_duration; /* in seconds */
    long encoder_num_frames;
    long encoder_succes_frames;
    int encoder_width;
    int encoder_height;
    int encoder_max_size;

    int freeze_mode;
    int freeze_nframes;
    int freeze_pframes;
    int fader_active;
    int fader_direction;
    float fader_val;
    float fader_inc;
    int selected_entry;	
    int effect_toggle;
    int socket_ready;
    uint8_t *socket_frame;
//    int video_palette;
} vj_tag;

int 	vj_tag_chain_malloc(int e);
int 	vj_tag_chain_free(int e);

int 	vj_tag_init(int w, int h, int pix_fmt);

int 	vj_tag_get_last_tag();

void	vj_tag_free(void);

/* create a new tag, type is yuv4mpeg or v4l  
   stream_nr indicates which stream to take of the same type
 */
int 	vj_tag_new(int type, char *filename, int stream_nr, editlist * el,
	        int pix_fmt, int channel);

/* return 1 if tag exists , 0 otherwise*/
int 	vj_tag_exists(int id);

/* return 1 if tag gets deleted, 0 on error */
int 	vj_tag_del(int id);

/* return -1 if there is no effect or if it is disabled, otherwise a positive value */
int 	vj_tag_get_effect(int t1, int position);

int 	vj_tag_size();

vj_tag 	*vj_tag_get(int id);

int 	_vj_tag_new_v4l(vj_tag *t, int nr , int w , int h, int n, int p, int f, int chan);

/* always return effect (-1 = empty) */
int 	vj_tag_get_effect_any(int t1, int position);

/* return -1 on error, otherwise argument gets updated */
int 	vj_tag_set_effect(int t1, int position, int effect_id);

/* return -1 on error, or return e_flag (effect enabled/disabled 1/0)*/
int 	vj_tag_get_chain_status(int t1, int position);

/* return -1 on error, otherwise set new status */
int 	vj_tag_set_chain_status(int t1, int position, int new_status);

/* return 0 on error, other value is trimmer (0 = no trim anyway) */
int 	vj_tag_get_trimmer(int t1, int poisition);

/* return -1 on error, or 1 on succes */
int 	vj_tag_set_trimmer(int t1, int position, int value);

//int vj_tag_get_video_palette(int t1);

//int vj_tag_set_video_palette(int t1, int video_palette);

/* return -1 on error or 1 on sucess. tag's effect parameters get copied into args
   args must be initialized.
 */
int 	vj_tag_get_all_effect_args(int t1, int position, int *args,
			       int arg_len);

int 	vj_tag_get_effect_arg(int t1, int p, int arg);

/* return -1 on error, 1 on success */
int 	vj_tag_set_effect_arg(int t1, int position, int argnr, int value);

/* return -1 on error, 1 on sucess */
int 	vj_tag_get_type(int t1);

/* returns number of tags */

int 	vj_tag_get_logical_index(int t1);

int 	vj_tag_clear_chain(int id);

int 	vj_tag_get_depth(int t1);

int 	vj_tag_set_depth(int t1, int depth);

int 	vj_tag_set_active(int t1, int active);

int 	vj_tag_get_active(int t1);

int 	vj_tag_chain_size(int t1);

int 	vj_tag_chain_remove(int t1, int index);

int 	vj_tag_set_chain_channel(int t1, int position, int channel);

int 	vj_tag_get_chain_channel(int t1, int position);

void 	vj_tag_get_source_name(int t1, char *dst);

int 	vj_tag_get_chain_source(int t1, int position);

int 	vj_tag_set_chain_source(int t1, int position, int source);

void 	vj_tag_get_description(int type, char *dst);

int 	vj_tag_by_type(int type);

int 	vj_tag_get_offset(int t1, int entry);

int 	vj_tag_set_offset(int t1, int entry, int offset);

//int vj_tag_record_frame(int t1, uint8_t *buffer[3]);

int 	vj_tag_get_frame(int t1, uint8_t *buffer[3], uint8_t *abuf);

int 	vj_tag_get_audio_frame(int t1, uint8_t *dst );

int 	vj_tag_enable(int t1);

int 	vj_tag_disable(int t1);

int		vj_tag_sprint_status(int tag_id, int r, int f, int m, char *str );

//int vj_tag_init_encoder(int t1, char *filename, int format,
//	int w, int h, double fps, long seconds, int autoplay);

int 	vj_tag_stop_encoder(int t1);
int 	vj_tag_set_brightness(int t1, int value);
int 	vj_tag_set_contrast(int t1, int value);
int 	vj_tag_set_color(int t1, int value);
int 	vj_tag_set_hue(int t1, int value);

void 	vj_tag_set_veejay_t(void *info);

int 	vj_tag_set_manual_fader(int t1, int value );

int 	vj_tag_get_fader_direction(int t1);
int 	vj_tag_set_fader_active(int t1, int nframes, int direction);
int 	vj_tag_set_fade_to_tag(int t1, int t2);
int 	vj_tag_set_fade_to_clip(int t1, int s1);
int 	vj_tag_set_fader_val(int t1, float val);
int 	vj_tag_apply_fader_inc(int t1);
int 	vj_tag_get_fader_active(int t1);
float 	vj_tag_get_fader_val(int t1);
float 	vj_tag_get_fader_inc(int t1);
int 	vj_tag_reset_fader(int t1);

int 	vj_tag_get_effect_status(int s1);
int 	vj_tag_get_selected_entry(int s1);

int 	vj_tag_set_effect_status(int s1, int status);
int 	vj_tag_set_selected_entry(int s1, int position);
void 	vj_tag_close_all();


int		vj_tag_init_encoder(int t1, char *filename, int format, long nframes);
int		vj_tag_record_frame(int t1, uint8_t *buffer[3], uint8_t *abuff, int audio_size); 
int 	vj_tag_get_encoded_frames(int t1);
int		vj_tag_get_total_frames(int t1);
int		vj_tag_reset_autosplit(int t1);
int		vj_tag_get_frames_left(int t1);
int 	vj_tag_encoder_active(int t1);
int		vj_tag_get_num_encoded_files(int t1);
int		vj_tag_get_encoder_format(int t1);
int		vj_tag_get_sequenced_file( int t1, char *descr, int num);
int		vj_tag_try_filename(int t1, char *filename);
int		vj_tag_get_encoded_file(int t1, char *descr);
void	vj_tag_reset_encoder( int t1 );
void 	vj_tag_record_init(int w, int h);

int		vj_tag_get_last_tag(void);
int		vj_tag_put( vj_tag *tag );
int 	vj_tag_is_deleted(int id);
void 	vj_tag_close_all(); 
int 	vj_tag_continue_record( int t1 );
int 	vj_tag_set_logical_index(int t1, int stream_nr);


#endif
