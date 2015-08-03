/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
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

#define VJ_TAG_TYPE_SPLITTER 8
#define VJ_TAG_TYPE_GENERATOR 7
#define VJ_TAG_TYPE_CALI 6
#define VJ_TAG_TYPE_PICTURE 5
#define VJ_TAG_TYPE_COLOR 4
#define VJ_TAG_TYPE_VLOOPBACK 3
#define VJ_TAG_TYPE_V4L 2
#define VJ_TAG_TYPE_YUV4MPEG 1
#define VJ_TAG_TYPE_NONE 0
#define VJ_TAG_TYPE_SHM 11
#define VJ_TAG_TYPE_NET 13
#define VJ_TAG_TYPE_MCAST 14
#define VJ_TAG_MAX_V4L 16
#define VJ_TAG_MAX_STREAM_IN 255
#define VJ_TAG_TYPE_DV1394 17
#define VJ_TAG_TYPE_AVFORMAT 12
#define TAG_MAX_DESCR_LEN 150
#include <config.h>
#include <libsample/sampleadm.h>
#include <libstream/vj-yuv4mpeg.h>
#include <libvjnet/vj-client.h>
#include <libstream/vj-dv1394.h>
#ifdef USE_GDK_PIXBUF
typedef struct
{
	void *pic;
} vj_picture;
#endif

typedef struct {
	void	*unicap[VJ_TAG_MAX_STREAM_IN];
    vj_yuv *stream[VJ_TAG_MAX_STREAM_IN];
	vj_client *net[VJ_TAG_MAX_STREAM_IN];
    vj_dv1394 *dv1394[VJ_TAG_MAX_STREAM_IN];
#ifdef USE_GDK_PIXBUF
	vj_picture *picture[VJ_TAG_MAX_STREAM_IN];
#endif 
       void	*cali[VJ_TAG_MAX_STREAM_IN];	
    int width;
    int height;
    int depth;
    int pix_fmt;
    int uv_len;
} vj_tag_data;

typedef struct {
    int id;
//      char description[100];
    sample_eff_chain *effect_chain[SAMPLE_MAX_EFFECTS];
    int next_id;
    int nframes;
    int source_type;
    char *source_name;
	char *method_filename;
    int index;
    int depth;
    int active;
    int source;
    int video_channel;
    int capture_type;
    int encoder_active;
    unsigned long sequence_num;
    char encoder_base[256];
    char encoder_destination[256];
    char descr[TAG_MAX_DESCR_LEN];
    int encoder_format;
    void *encoder;
    lav_file_t *encoder_file;
    long encoder_total_frames_recorded; /* in seconds */
    long encoder_frames_to_record;
    long encoder_frames_recorded;
    int encoder_width;
    int encoder_height;
    int encoder_max_size;
    int color_r;
    int color_g; 
    int color_b;
    int opacity; 
    int fader_active;
    int fader_direction;
    float fader_val;
    float fader_inc;
    int selected_entry;	
    int effect_toggle;
    int socket_ready;
    int socket_len;
    uint8_t *socket_frame;
    int n_frames;
    void *priv;
    void *extra;
    void *dict;
    char padding[4];
    int composite;
    void *viewport_config;
    void *viewport;
    int noise_suppression;
    uint8_t *blackframe;
    int bf_count;
    int median_radius;
    int has_white;
    double *tabmean[3];
    double mean[3];
    double *bf;
    double *bfu;
    double *bfv;
    double *lf;
    double *lfu;
    double *lfv;
    int cali_duration;
	void	*generator;
	int	subrender;
} vj_tag;

#define V4L_BLACKFRAME 1
#define V4L_BLACKFRAME_NEXT 2
#define V4L_BLACKFRAME_PROCESS 3

void	*vj_tag_get_dict( int id );
int	vj_tag_set_composite(void *compiz,int id, int n);
int	vj_tag_get_composite(int t1);
int 	vj_tag_chain_malloc(int e);
int 	vj_tag_chain_free(int e);
int	vj_tag_get_v4l_properties(int t1,int *brightness, int *contrast, int *hue,int *color, int *white );
int 	vj_tag_init(int w, int h, int pix_fmt, int driver);
int	vj_tag_get_n_frames(int t1);
int	vj_tag_set_n_frames(int t1, int n_frames);
int 	vj_tag_get_last_tag();

void	vj_tag_free(void);
/* Change color of solid stream*/
int	vj_tag_set_stream_color(int t1, int r, int g, int b);
int	vj_tag_get_stream_color(int t1, int *r, int *g, int *b );
int	vj_tag_set_stream_layout( int t1, int stream_id_g, int screen_no_b, int value );
/* create a new tag, type is yuv4mpeg or v4l  
   stream_nr indicates which stream to take of the same type
 */
int 	vj_tag_new(int type, char *filename, int stream_nr, editlist * el,
	        int pix_fmt, int channel, int extra, int has_composite);

/* return 1 if tag exists , 0 otherwise*/
int 	vj_tag_exists(int id);

/* return 1 if tag gets deleted, 0 on error */
int 	vj_tag_del(int id);

int	vj_tag_verify_delete(int id, int type );

/* return -1 if there is no effect or if it is disabled, otherwise a positive value */
int 	vj_tag_get_effect(int t1, int position);

void	*vj_tag_get_plugin( int t1, int position, void *ptr );

int		vj_tag_get_subrender(int t1);
void	vj_tag_set_subrender(int t1, int status);

int 	vj_tag_size();

vj_tag 	*vj_tag_get(int id);
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
			       int arg_len, int n_frame);

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

void 	vj_tag_get_descriptive(int type, char *dst);

int 	vj_tag_by_type(int type);

int 	vj_tag_get_offset(int t1, int entry);

int 	vj_tag_set_offset(int t1, int entry, int offset);

//int vj_tag_record_frame(int t1, uint8_t *buffer[3]);

int 	vj_tag_get_frame(int t1, uint8_t *buffer[3], uint8_t *abuf);

int 	vj_tag_get_audio_frame(int t1, uint8_t *dst );

int 	vj_tag_enable(int t1);

int 	vj_tag_disable(int t1);

int		vj_tag_sprint_status(int tag_id, int cache,int sa, int ca, int r, int f, int m, int t,int curfps, uint32_t lo, uint32_t hi, int macro,char *str );

uint8_t		*vj_tag_get_cali_buffer(int t1, int type, int *total, int *len, int *uvlen);
int 	vj_tag_stop_encoder(int t1);
int 	vj_tag_set_brightness(int t1, int value);
int 	vj_tag_set_contrast(int t1, int value);
int 	vj_tag_set_color(int t1, int value);
int 	vj_tag_set_hue(int t1, int value);
int	vj_tag_set_white(int t1, int value);
int	vj_tag_set_saturation(int t1, int value);
void 	vj_tag_set_veejay_t(void *info);

int 	vj_tag_set_manual_fader(int t1, int value );

int 	vj_tag_get_fader_direction(int t1);
int 	vj_tag_set_fader_active(int t1, int nframes, int direction);
int 	vj_tag_set_fade_to_tag(int t1, int t2);
int 	vj_tag_set_fade_to_sample(int t1, int s1);
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

int	vj_tag_composite(int t1);
int	vj_tag_load_composite_config( void *compiz, int t1 );

int		vj_tag_init_encoder(int t1, char *filename, int format, long nframes);
int		vj_tag_record_frame(int t1, uint8_t *buffer[3], uint8_t *abuff, int audio_size, int pixel_format); 
long 		vj_tag_get_encoded_frames(int t1);
long		vj_tag_get_total_frames(int t1);
int		vj_tag_reset_autosplit(int t1);
long		vj_tag_get_frames_left(int t1);
int 		vj_tag_encoder_active(int t1);
int		vj_tag_get_num_encoded_files(int t1);
int		vj_tag_get_encoder_format(int t1);
int		vj_tag_get_sequenced_file( int t1, char *descr, int num, char *ext);
int		vj_tag_try_filename(int t1, char *filename, int format);
int		vj_tag_get_encoded_file(int t1, char *descr);
void	vj_tag_reset_encoder( int t1 );
void 	vj_tag_record_init(int w, int h);
void vj_tag_get_method_filename(int t1, char *dst);
int		vj_tag_get_last_tag(void);
int		vj_tag_put( vj_tag *tag );
int 	vj_tag_is_deleted(int id);
void 	vj_tag_close_all(); 
int 	vj_tag_continue_record( int t1 );
int 	vj_tag_set_logical_index(int t1, int stream_nr);
int	vj_tag_set_description(int t1, char *descr);
int	vj_tag_get_description(int t1, char *descr);
void	vj_tag_get_by_type( int type, char *descr );
int	vj_tag_get_width();
int	vj_tag_get_height();
int	vj_tag_get_uvlen();
void  vj_tag_cali_prepare( int t1 , int pos, int cali_tag);
void	vj_tag_cali_prepare_now(int a, int b);
int	vj_tag_chain_set_kfs( int s1, int len, unsigned char *data );
unsigned char *	vj_tag_chain_get_kfs( int s1, int entry, int parameter_id, int *len );
int	vj_tag_get_kf_status(int t1, int entry, int *type);
void	vj_tag_set_kf_type(int t1, int entry, int type );
int	vj_tag_chain_set_kf_status( int s1, int entry, int status );
int	vj_tag_chain_reset_kf( int s1, int entry );
int     vj_tag_var(int t1, int *type, int *fader, int *fx_sta , int *rec_sta, int *active );
int vj_tag_true_size();
void    *vj_tag_get_kf_port( int s1, int entry );

char *vj_tag_scan_devices( void );
int    vj_tag_get_kf_tokens( int s1, int entry, int id, int *start,int *end, int *type);
int vj_tag_grab_blackframe(int t1, int duration, int median_radius,int mode );
int vj_tag_drop_blackframe(int t1);
int    vj_tag_num_devices();

void	vj_tag_reload_config( void *compiz, int t1, int mode );

void	*vj_tag_get_composite_view(int t1);
int	vj_tag_set_composite_view(int t1, void *v);
int	vj_tag_cali_write_file( int id, char *name, editlist *el );
int	vj_tag_has_cali_fx( int t1 );
int     vj_tag_cali_write_file( int t1, char *name, editlist *el );
uint8_t *vj_tag_get_cali_data( int t1, int what );


#ifdef HAVE_XML2
void	tag_writeStream( char *file, int n, xmlNodePtr node, void *font, void *vp);
void tagCreateStream(xmlNodePtr node, vj_tag *tag, void *font, void *vp);
void tagCreateStreamFX(xmlNodePtr node, vj_tag *tag);
void tagParseStreamFX(char *file, xmlDocPtr doc, xmlNodePtr cur, void *font,
	void *vp);
#endif
#endif
