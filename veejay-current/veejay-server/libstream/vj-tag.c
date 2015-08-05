/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nwelburg@gmail.com>
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
 */
#include <config.h>
#include <string.h>
#include <libstream/vj-tag.h>
#include <libhash/hash.h>
#include <libvje/vje.h>
#include <veejay/vj-viewport.h>
#include <veejay/vjkf.h>
#include <veejay/vj-shm.h>
#define VIDEO_PALETTE_YUV420P 15
#define VIDEO_PALETTE_YUV422P 13

#ifdef USE_GDK_PIXBUF
#include <libel/pixbuf.h>
#endif

#ifdef SUPPORT_READ_DV2
#include <libstream/vj-dv1394.h>
#endif
#include <libsubsample/subsample.h>
#include <libvjmsg/vj-msg.h>
#include <libel/vj-avcodec.h>
#include <libvjnet/vj-client.h>
#include <veejay/vims.h>
#include <veejay/vj-lib.h>
#include <veejay/vj-misc.h>
#include <libvjmem/vjmem.h>
#include <libvje/internal.h>
#include <libvje/ctmf/ctmf.h>
#include <libstream/vj-net.h>
#ifdef HAVE_V4L
#include <linux/videodev.h>
#include <libstream/v4lvideo.h>
#endif
#include <pthread.h>
#ifdef HAVE_V4L2
#include <libstream/v4l2utils.h>
#endif
#include <libvevo/libvevo.h>
#include <veejay/vj-misc.h>
#ifdef HAVE_FREETYPE
#include <veejay/vj-font.h>
#endif
#include <veejay/vj-viewport-xml.h>

#include <libplugger/plugload.h>
static veejay_t *_tag_info = NULL;
static hash_t *TagHash = NULL;
static int this_tag_id = 0;
static vj_tag_data *vj_tag_input;
static int next_avail_tag = 0;
static int avail_tag[SAMPLE_MAX_SAMPLES];
static int last_added_tag = 0;
static int video_driver_  = -1; // V4lUtils
//forward decl
static int no_v4l2_threads_ = 0;

static void *tag_cache[SAMPLE_MAX_SAMPLES];

int _vj_tag_new_net(vj_tag *tag, int stream_nr, int w, int h,int f, char *host, int port, int p, int ty );
int _vj_tag_new_yuv4mpeg(vj_tag * tag, int stream_nr, int w, int h, float fps);

extern void dummy_rgb_apply(VJFrame *frame, int width, int height, int r, int g, int b);
extern int   sufficient_space(int max_size, int nframes);
extern char *UTF8toLAT1(unsigned char *in);
extern int cali_prepare( void *ed, double meanY, double meanU, double meanV, uint8_t *data, int len, int uv_len );

#define RUP8(num)(((num)+8)&~8)

typedef struct
{
	uint8_t *data;
	uint8_t *bf;
	uint8_t *lf;
	uint8_t *mf;
	int	uv_len;
	int	len;
	double	mean[3];
} cali_tag_t;

#define CALI_DARK 0 
#define CALI_LIGHT 1
#define CALI_FLAT 2
#define CALI_BUF 4
#define CALI_MFLAT 3

static	uint8_t	*cali_get(vj_tag *tag, int type, int len, int uv_len ) {
	uint8_t *p = tag->blackframe;
	switch(type) {
		case CALI_DARK:
			return p;		      //@ start of dark current
		case CALI_LIGHT:
			return p + (len + (2*uv_len)); //@ start of light frame
		case CALI_FLAT:
			return p + (2*(len + (2*uv_len))); //@ start of master frame
		case CALI_MFLAT:
			return p + (3*(len + (2*uv_len))); //@ processing buffer
		case CALI_BUF:
			return p + (4*(len + (2*uv_len)));
	}
	return NULL;
}

static uint8_t *_temp_buffer[3]={NULL,NULL,NULL};
static VJFrame _tmp;
void	vj_tag_free(void)
{
	int i;
	for( i = 0; i < 3 ; i ++ )
	{
		if( _temp_buffer[i] )
			free( _temp_buffer[i] );
		_temp_buffer[i] = NULL;
	}
	
	vj_tag_close_all();

	if( vj_tag_input)
		free(vj_tag_input);

	if( TagHash )
		hash_destroy( TagHash );
}




int vj_tag_get_last_tag() {
	return last_added_tag;
}

int vj_tag_true_size()
{
    return (this_tag_id - next_avail_tag);
}


int vj_tag_size()
{
    return this_tag_id;
}

void vj_tag_set_veejay_t(void *info) {
	_tag_info = (veejay_t*)info;
}

static hash_val_t int_tag_hash(const void *key)
{
    return (hash_val_t) key;
}

static int int_tag_compare(const void *key1, const void *key2)
{
#ifdef ARCH_X86_64
	return ((uint64_t)key1 < (uint64_t) key2 ? -1 :
			((uint64_t)key1 < (uint64_t) key2 ? 1 : 0 )
	);
#else
    return ((uint32_t) key1 < (uint32_t) key2 ? -1 :
			((uint32_t) key1 > (uint32_t) key2 ? 1 : 0)
	);
#endif
}

vj_tag *vj_tag_get(int id)
{
    if (id <= 0 || id > this_tag_id) {
		return NULL;
    }
#ifdef ARCH_X86_64
	uint64_t tid = (uint64_t) id;
#else
	uint32_t tid = (uint32_t) id;
#endif

	if( tag_cache[ id ] == NULL ) {
		hnode_t *tag_node = hash_lookup(TagHash, (void *) tid);
		if (!tag_node) {
			return NULL;
		}
		tag_cache[ id ] = hnode_get(tag_node);
	}
	return (vj_tag*) tag_cache[id];
}

int vj_tag_put(vj_tag * tag)
{
    hnode_t *tag_node;
    if (!tag)
		return 0;
    tag_node = hnode_create(tag);
    if (!tag_node)
		return 0;
#ifdef ARCH_X86_64
	uint64_t tid = (uint64_t) tag->id;
#else
	uint32_t tid = (uint32_t) tag->id;
#endif

    if (!vj_tag_exists(tag->id)) {
		hash_insert(TagHash, tag_node, (void *) tid);
    } else {
		hnode_put(tag_node, (void *) tid);
    }
    return 1;
}

int	vj_tag_num_devices()
{
#ifdef HAVE_V4L
	return v4lvideo_templ_num_devices();
#elif HAVE_V4L2
	return v4l2_num_devices();
#endif
}

char *vj_tag_scan_devices( void )
{
	const char *default_str = "000000";
	int i;
	int len = 0;
	char **device_list = NULL;
#ifdef HAVE_V4L
	device_list = v4lvideo_templ_get_devices(&num);
#elif HAVE_V4L2
	device_list = v4l2_get_device_list();
#endif
	if(device_list==NULL)
		return strdup(default_str);

	for( i = 0; device_list[i] != NULL ;i++ )
		len += strlen( device_list[i] );

	char *n = (char*) vj_calloc(sizeof(char) * (16 + len) );
	char *p = n + 6;

	sprintf(n, "%06d", len );	
	for( i = 0; device_list[i] != NULL ;i++ )
	{
		char tmp[1024];
		snprintf( tmp, sizeof(tmp)-1, "%s", device_list[i] );
		int str_len = strlen(tmp);
		strncpy( p, tmp, str_len );
		p += str_len;
		free(device_list[i]);
	}
	free(device_list);
	return n;
}

int	vj_tag_get_width() {
	return vj_tag_input->width;
}
int	vj_tag_get_height() {
	return vj_tag_input->height;
}
int	vj_tag_get_uvlen() {
	return vj_tag_input->uv_len;
}

int vj_tag_init(int width, int height, int pix_fmt, int video_driver)
{
    TagHash = hash_create(HASHCOUNT_T_MAX, int_tag_compare, int_tag_hash);
    if (!TagHash || width <= 0 || height <= 0)
	return -1;
    vj_tag_input = (vj_tag_data *) vj_malloc(sizeof(vj_tag_data));
    if (vj_tag_input == NULL) {
	veejay_msg(VEEJAY_MSG_ERROR,
		   "Error Allocating Memory for stream data\n");
	return -1;
    }
    this_tag_id = 0;
    vj_tag_input->width = width;
    vj_tag_input->height = height;
    vj_tag_input->depth = 3;
    vj_tag_input->pix_fmt = pix_fmt;
    video_driver_ = video_driver;
    video_driver_ = 1;

    vj_tag_input->uv_len = (width*height) / 2;

    memset( &_tmp, 0, sizeof(VJFrame));
    _tmp.len = width * height;
   _temp_buffer[0] = (uint8_t*) vj_malloc(sizeof(uint8_t)*width*height);
   _temp_buffer[1] = (uint8_t*) vj_malloc(sizeof(uint8_t)*width*height);
   _temp_buffer[2] = (uint8_t*) vj_malloc(sizeof(uint8_t)*width*height);
		
	memset( tag_cache,0,sizeof(tag_cache));
	memset( avail_tag, 0, sizeof(avail_tag));
	_tmp.uv_width = width; 
	_tmp.uv_height = height/2;
	_tmp.uv_len = width * (height/2);

#ifdef HAVE_V4L 
	v4lvideo_templ_init();
#endif

	char *v4l2threading = getenv( "VEEJAY_V4L2_NO_THREADING" );
	if( v4l2threading ) {
		no_v4l2_threads_ = atoi(v4l2threading);
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_V4L2_NO_THREADING=[0|1] not set");
	}


    return 0;
}

void vj_tag_record_init(int w, int h)
{
}

int _vj_tag_new_net(vj_tag *tag, int stream_nr, int w, int h,int f, char *host, int port, int p, int type )
{
	char tmp[1024];
	if( !host  ) {
		veejay_msg(0, "No hostname given");
		return 0;
	}
	if( port <= 0 ) {
		veejay_msg(0, "Port number %d invalid", port );
		return 0;
	}
	if(stream_nr < 0 || stream_nr > VJ_TAG_MAX_STREAM_IN)
	{
		veejay_msg(0, "Unable to create more network streams (%d reached)",
			VJ_TAG_MAX_STREAM_IN );
		 return 0;
	}

/*	vj_tag_input->net[stream_nr] = vj_client_alloc(w,h,f);
	v = vj_tag_input->net[stream_nr];
	if(!v) 
	{
		veejay_msg(0, "Memory allocation error while creating network stream");
		return 0;
	}*/
	snprintf(tmp,sizeof(tmp), "%s %d", host, port );
	tag->extra = (void*) strdup(tmp);

	if( tag->socket_ready == 0 )
	{
		tag->socket_frame = (uint8_t*) vj_calloc(sizeof(uint8_t) * RUP8( w * h * 3));
		tag->socket_len = w * h * 3;
		if(!tag->socket_frame) 
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Insufficient error to allocate memory for Network Stream");
			return 0;
		}
		tag->socket_ready = 1;
	}

	return 1;
}
#ifdef HAVE_V4L
static struct {
	const char *name;
} video_norm_[] = 
{
	{"pal"},
	{"ntsc"},
	{"auto"},
	{NULL}
};
#endif

int _vj_tag_new_unicap( vj_tag * tag, int stream_nr, int width, int height, int device_num,
		    char norm, int palette,int pixfmt, int freq, int channel, int has_composite, int driver)
{
	char refname[100];
	if (stream_nr < 0 || stream_nr > vj_tag_num_devices())
	{
		return 0;
	}
	
	snprintf(refname,sizeof(refname), "/dev/video%d",device_num ); // freq->device_num
#ifdef HAVE_V4L
	const char *selected_video_norm = video_norm_[2].name
	switch(norm) {
		case 'P':
		case 'p':
			selected_video_norm = video_norm_[0].name; break;
		case 'n': case 'N':	
			selected_video_norm = video_norm_[1].name; break;
		default:
			break;//auto
	}
#endif
	
	tag->capture_type = driver;
	veejay_msg(VEEJAY_MSG_INFO, "Open capture device with %s",
	  ( driver == 1 ? "v4l[x]"  : "Unicap" ) );
	if( tag->capture_type == 1 )  {
#ifdef HAVE_V4L
   		vj_tag_input->unicap[stream_nr] = v4lvideo_init( refname, channel, 
			v4lvideo_templ_get_norm( selected_video_norm ),
			freq,
			width, height, palette );
		v4lvideo_set_composite_status(  vj_tag_input->unicap[stream_nr] ,has_composite );
		if( vj_tag_input->unicap[stream_nr] == NULL ) {
			veejay_msg(0, "Unable to open device %s", refname );
			return 0;
		}
		snprintf(refname,sizeof(refname) "%d",channel );
		tag->extra = strdup(refname);
		veejay_msg(VEEJAY_MSG_DEBUG, "Using V4lutils from EffecTV");
#elif HAVE_V4L2
		if(  no_v4l2_threads_ ) {
			vj_tag_input->unicap[stream_nr] = v4l2open( refname, channel, palette,width,height,
				_tag_info->dummy->fps,_tag_info->dummy->norm );
		} else {
			vj_tag_input->unicap[stream_nr] = v4l2_thread_new( refname, channel,palette,width,height,
			_tag_info->dummy->fps,_tag_info->dummy->norm );
		}
		if( !vj_tag_input->unicap[stream_nr] ) {
			veejay_msg(0, "Unable to open device %d (%s)",device_num, refname );
			return 0;
		}
		snprintf(refname,sizeof(refname), "%d", channel );
		tag->extra = strdup(refname);
#endif
		return 1;
	} 
	
	return 1;
}

#ifdef USE_GDK_PIXBUF
int _vj_tag_new_picture( vj_tag *tag, int stream_nr, int width, int height, float fps)
{
	if(stream_nr < 0 || stream_nr > VJ_TAG_MAX_STREAM_IN) return 0;
	vj_picture *p =	NULL;

	if( vj_picture_probe( tag->source_name ) == 0 )
		return 0;

 	p = (vj_picture*) vj_malloc(sizeof(vj_picture));
	if(!p)
		return 0;
	memset(p, 0, sizeof(vj_picture));

	vj_tag_input->picture[stream_nr] = p;

	veejay_msg(VEEJAY_MSG_INFO, "Opened [%s] , %d x %d @ %2.2f fps ",
			tag->source_name,
			width, height, fps );

	return 1;
}
#endif

uint8_t		*vj_tag_get_cali_buffer(int t1, int type, int *total, int *plane, int *planeuv)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag)
		return NULL;
	
	int 	w	=	vj_tag_input->width;
	int	h	=	vj_tag_input->height;
	int	len	=	(w*h);
	int	uv_len	=	vj_tag_input->uv_len;

	*total =	len + (2*uv_len);
	*plane = 	len;
	*planeuv=	uv_len;
	return 	cali_get(tag,type,w*h,uv_len);
}

static	int	cali_write_file( char *file, vj_tag *tag , editlist *el)
{
	FILE *f = fopen( file, "w" );
	if(!f) {
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to open '%s' for writing",file );
		return 0;
	}

	char header[256];
	int 	w	=	vj_tag_input->width;
	int	h	=	vj_tag_input->height;
	int	len	=	(w*h);
	int	uv_len	=	vj_tag_input->uv_len;

	char fileheader[256];

	snprintf(header,sizeof(header),"%08d %08d %08d %08d %g %g %g",
			w,
			h,
			len,
			uv_len,
	       		tag->mean[0],
			tag->mean[1],
			tag->mean[2]	);

	int offset = 4 + strlen(header);

	snprintf(fileheader,sizeof(fileheader), "%03d %s",offset,header );

	if( fwrite( fileheader,strlen(fileheader),1, f ) <= 0 ) {
		veejay_msg(0 ,"Error while writing file header.");
		return 0;
	}	
	int n = 0;

	//@ write dark current frame
	if( (n=fwrite( tag->blackframe,sizeof(uint8_t), len + uv_len + uv_len,  f )) <= 0 ) {
		goto CALIERR;
	}
	if( n != (len+uv_len + uv_len))
		goto CALIERR;

	uint8_t *lightframe = 	cali_get(tag,CALI_LIGHT,w*h,uv_len);
	if( (n=fwrite( lightframe,sizeof(uint8_t), len + uv_len + uv_len, f )) <= 0 ) {
		goto CALIERR;
	}
	if( n != (len+uv_len+uv_len))
		goto CALIERR;

	uint8_t *masterframe = cali_get(tag,CALI_FLAT,w*h,uv_len);
	if( (n=fwrite( masterframe, sizeof(uint8_t), len + uv_len + uv_len, f )) <= 0 ) {
		goto CALIERR;
	}
	if( n != (len+uv_len+uv_len))
		goto CALIERR;

	fclose(f);

	return 1;
CALIERR:
	fclose(f);
	veejay_msg(0, "File write error.");

	
	return 0;
}

int		vj_tag_cali_write_file( int t1, char *name, editlist *el ) {
	vj_tag *tag = vj_tag_get(t1);
	if(!tag)
		return 0;
	if(tag->source_type != VJ_TAG_TYPE_V4L) {
		veejay_msg(0, "Stream is not of type Video4Linux");
		return 0;
	}
	if(tag->noise_suppression == 0 ) {
		veejay_msg(0, "Stream %d is not yet calibrated.", t1 );
		return 0;
	}
	if(tag->noise_suppression != V4L_BLACKFRAME_PROCESS ) {
		veejay_msg(0, "Please finish calibration first.");
		return 0;
	}
	if(! cali_write_file( name, tag, el ) ) {
		return 0;
	}
	return 1;
}


static int	cali_read_file( cali_tag_t *p, char *file,int w, int h )
{
	FILE *f = fopen( file , "r" );
	if( f == NULL ) {
		return 0;
	}

	char	buf[256];

	char	*header = fgets( buf, sizeof(buf), f );
	int	len	= 0;
	int	uv_len  = 0;
	int	offset  = 0;

	int	Euv_len = vj_tag_input->uv_len;

	double	mean[3];

	if(sscanf(header, "%3d %8d %8d %8d %8d %lf %lf %lf",&offset, &w,&h,&len,&uv_len,
			&mean[0],&mean[1],&mean[2] ) != 8  )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid header.");
		return 0;
	}

	if( len != (w*h)) {
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid length for plane Y");
		fclose(f);
		return 0;
	}

	if( Euv_len != uv_len ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid length for planes UV");
		fclose(f);
		return 0;
	}

	p->data = (uint8_t*) vj_malloc(sizeof(uint8_t) * 3 * (len+uv_len+uv_len));
	p->bf = p->data;
        p->lf =	p->data + (len + (2*uv_len));
	p->mf = p->lf + (len + (2*uv_len));

	p->uv_len = uv_len;
	p->len    = len;
	p->mean[0] = mean[0];
	p->mean[1] = mean[1];
	p->mean[2] = mean[2];

	veejay_memset( p->data,0, 3 * (len+(2*uv_len)));

	int n = 0;

	if( (n=fread( p->bf, 1, (len+2*uv_len), f )) <= 0 ) {
		goto CALIREADERR;
	}

	if( (n=fread( p->lf,1, (len+2*uv_len), f )) <= 0 ) {
		goto CALIREADERR;
	}

	if( (n=fread( p->mf,1, (len+2*uv_len),f)) <= 0 ) {
		goto CALIREADERR;
	}

	veejay_msg(VEEJAY_MSG_INFO, "Image calibration data loaded.");

	return 1;

CALIREADERR:
	veejay_msg(VEEJAY_MSG_ERROR, "Only got %d bytes.",n);
	return 0;
}

int	_vj_tag_new_cali( vj_tag *tag, int stream_nr, int w, int h )
{
	if(stream_nr < 0 || stream_nr > VJ_TAG_MAX_STREAM_IN) return 0;
	
	cali_tag_t *p = NULL;

	p = (cali_tag_t*) vj_malloc(sizeof(cali_tag_t));
	if(!p)
		return 0;
	memset(p, 0, sizeof(cali_tag_t));

	if(!cali_read_file( p, tag->source_name,w,h ) ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to find dark frame '%s'", tag->source_name );
		free(p);
		return 0;
	}
	
	vj_tag_input->cali[stream_nr] = (void*)p;
	
	veejay_msg(VEEJAY_MSG_INFO, "Image Cailbration files ready.");
	
	return 1;
}

uint8_t	*vj_tag_get_cali_data( int t1, int what ) {
	vj_tag *tag = vj_tag_get(t1);
	if(tag == NULL)
		return NULL;
	int w = vj_tag_input->width;
	int h = vj_tag_input->height;
	int uv_len = vj_tag_input->uv_len;
	switch(what) {
		case 0:
			return tag->blackframe;
		case 1:
			return tag->blackframe + ((w*h)+(2*uv_len));
		case 2:
			return tag->blackframe + 2 * ((w*h)+(2*uv_len));
	}
	return NULL;
}

int _vj_tag_new_yuv4mpeg(vj_tag * tag, int stream_nr, int w, int h, float fps)
{
    if (stream_nr < 0 || stream_nr > VJ_TAG_MAX_STREAM_IN)
	return 0;
    vj_tag_input->stream[stream_nr] = vj_yuv4mpeg_alloc(w, h, fps, _tag_info->pixel_format);

    if(vj_tag_input->stream[stream_nr] == NULL) 
	return 0;

    if(vj_yuv_stream_start_read(vj_tag_input->stream[stream_nr],tag->source_name,w,h ) != 0 )
    {
 	veejay_msg(VEEJAY_MSG_ERROR,"Unable to read from %s",tag->source_name);
	vj_yuv4mpeg_free( vj_tag_input->stream[stream_nr] );
	return 0;
    }
    return 1;
}
#ifdef SUPPORT_READ_DV2
int	_vj_tag_new_dv1394(vj_tag *tag, int stream_nr, int channel,int quality, editlist *el)
{
   vj_tag_input->dv1394[stream_nr] = vj_dv1394_init( (void*) el, channel,quality);
   if(vj_tag_input->dv1394[stream_nr])
   {
	veejay_msg(VEEJAY_MSG_INFO, "DV1394 ready for capture");
//	vj_dv_decoder_set_audio( vj_tag_input->dv1394[stream_nr], el->has_audio);
	return 1;
   } 
   return 0;
}
#endif

void	*vj_tag_get_dict( int t1 )
{
#ifdef HAVE_FREETYPE
	vj_tag *tag = vj_tag_get(t1);
	if(tag)
		return tag->dict;
#endif
	return NULL;
}

int	vj_tag_set_stream_layout( int t1, int stream_id_g, int screen_no_b, int value )
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return 0;

	if( screen_no_b >= 0 ) {
		tag->color_b = screen_no_b;
	}

	if( stream_id_g > 0 ) {
		if( vj_tag_exists(stream_id_g) ) {
			tag->color_g = stream_id_g;
		}
	}

	if( value >= 0 ) {
		if( value > 7 )
			value = 7;
		tag->color_r = value;
	}
	return 1;
}

int	vj_tag_set_stream_color(int t1, int r, int g, int b)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
	return 0;
	
	veejay_msg(VEEJAY_MSG_DEBUG,"Set stream %d color %d,%d,%d",t1,
		r,g, b );
 
    tag->color_r = r;
    tag->color_g = g;
    tag->color_b = b;

	if( tag->generator ) {
		plug_set_parameter( tag->generator, 0,1,&r );
		plug_set_parameter( tag->generator, 1,1,&g );
		plug_set_parameter( tag->generator, 2,1,&b );
	}

    return 1;
}

int	vj_tag_get_composite(int t1)
{	
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return 0;
	return tag->composite;
}

int	vj_tag_get_stream_color(int t1, int *r, int *g, int *b )
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
	return 0;
    if(tag->source_type != VJ_TAG_TYPE_COLOR)
	return 0;

    *r = tag->color_r;
    *g = tag->color_g;
    *b = tag->color_b;

	return 1;
}

int	vj_tag_composite(int t1)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return 0;
	if(tag->source_type != VJ_TAG_TYPE_V4L )
		return 0;
	if(tag->capture_type==1) {
#ifdef HAVE_V4L
		return v4lvideo_get_composite_status( vj_tag_input->unicap[tag->index]);
#elif HAVE_V4L2
		return v4l2_get_composite_status( vj_tag_input->unicap[tag->index] );
#endif
	}
	return 0;
}

// for network, filename /channel is passed as host/port num
int vj_tag_new(int type, char *filename, int stream_nr, editlist * el,
	        int pix_fmt, int channel , int extra , int has_composite)
{
    int i, j;
    int palette;
    int id = 0;
    int n;
    int w = _tag_info->effect_frame1->width;
    int h = _tag_info->effect_frame1->height;
    float fps = _tag_info->effect_frame1->fps;

    vj_tag *tag;
  
    if( this_tag_id == 0)
	{
		this_tag_id = 1; // first tag
	}
    n = this_tag_id;
    /* see if we are already using the source */
    if( type == VJ_TAG_TYPE_NET || type == VJ_TAG_TYPE_MCAST )
    {
	if(net_already_opened( filename,n, channel ))
	{
		veejay_msg(0, "There is already a unicast connection to %s: %d", filename, channel );
		return -1;
	}
    }
    tag = (vj_tag *) vj_calloc(sizeof(vj_tag));
  	if(!tag)
	{
		veejay_msg(0, "Memory allocation error");
		return -1; 
	}

    tag->source_name = (char *) vj_calloc(sizeof(char) * 255);
	if (!tag->source_name)
	{
		veejay_msg(0, "Memory allocation error");
		return -1;
	}
#ifdef ARCH_X86_64
	uint64_t tid = (uint64_t) id;
#else
	uint32_t tid = (uint32_t) id;
#endif
     /* see if we can reclaim some id */
    for(i=0; i <= next_avail_tag; i++) {
		if(avail_tag[i] != 0) {
 		  hnode_t *tag_node;
 	 	  tag_node = hnode_create(tag);
   		  if (!tag_node)
		  {
			veejay_msg(0, "Unable to find available ID");
			return -1;
		  }
		  id = avail_tag[i];
		  avail_tag[i] = 0;
		  hash_insert(TagHash, tag_node, (void *) tid);
		  break;
        }
    }

         
    if(id==0) { 
		tag->id = this_tag_id;
	}
    else {
		tag->id = id;
	}
   
	tag->extra = NULL;
    tag->next_id = 0;
    tag->nframes = 0;
    tag->video_channel = channel;
    tag->source_type = type;
    tag->index = stream_nr;
    tag->active = 0;
    tag->n_frames = 50;
    tag->sequence_num = 0;
    tag->encoder_format = 0;
    tag->encoder_active = 0;
    tag->encoder_max_size = 0;
    tag->encoder_width = 0;
    tag->encoder_height = 0;
    tag->method_filename = (filename == NULL ? NULL :strdup(filename));
    tag->encoder_total_frames_recorded = 0;
    tag->encoder_frames_recorded = 0;
    tag->encoder_frames_to_record = 0;
    tag->source = 0;
    tag->fader_active = 0;
    tag->fader_val = 0.0;
    tag->fader_inc = 0.0;
    tag->fader_direction = 0;
    tag->selected_entry = 0;
	tag->depth = 0;
    tag->effect_toggle = 1; /* same as for samples */
    tag->socket_ready = 0;
    tag->socket_frame = NULL;
    tag->socket_len = 0;
    tag->color_r = 0;
    tag->color_g = 0;
    tag->color_b = 0;
    tag->opacity = 0;
	tag->priv = NULL;

	if(type == VJ_TAG_TYPE_MCAST || type == VJ_TAG_TYPE_NET)
	    tag->priv = net_threader(_tag_info->effect_frame1);

#ifdef HAVE_V4L
	palette = v4lvideo_templ_get_palette( pix_fmt );
#elif HAVE_V4L2
	palette = get_ffmpeg_pixfmt( pix_fmt );
#endif
    switch (type) {
	    case VJ_TAG_TYPE_V4L:
		sprintf(tag->source_name, "%s", filename );
		
		veejay_msg(VEEJAY_MSG_DEBUG, "V4l: %s",filename);

/*
int _vj_tag_new_unicap( vj_tag * tag, int stream_nr, int width, int height, int device_num,
		    char norm, int palette,int pixfmt, int freq, int channel, int has_composite)

*/
		if (!_vj_tag_new_unicap( tag,
					 stream_nr,w,h,
					 extra, // device num
					 el->video_norm,
					 palette,
					 pix_fmt,
					 0,
					 channel,
					 has_composite,
					 video_driver_ ))
		{
			veejay_msg(0, "Unable to open capture stream '%dx%d' (norm=%c,format=%x,device=%d,channel=%d)",
				w,h,el->video_norm, pix_fmt, extra,channel );
			return -1;
		}
		break;
	case VJ_TAG_TYPE_MCAST:
	case VJ_TAG_TYPE_NET:
		sprintf(tag->source_name, "%s", filename );
		if( _vj_tag_new_net( tag,stream_nr, w,h,pix_fmt, filename, channel ,palette,type) != 1 )
			return -1;
	break;
    case VJ_TAG_TYPE_DV1394:
#ifdef SUPPORT_READ_DV2
	sprintf(tag->source_name, "/dev/dv1394/%d", channel);
	if( _vj_tag_new_dv1394( tag, stream_nr,channel,1,el ) == 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "error opening dv1394");
		return -1;
	}
	tag->active = 1;
	break;
#else
	veejay_msg(VEEJAY_MSG_DEBUG, "libdv not enabled at compile time");
	return -1;
#endif
#ifdef USE_GDK_PIXBUF
	case VJ_TAG_TYPE_PICTURE:
	sprintf(tag->source_name, "%s", filename);
	if( _vj_tag_new_picture(tag, stream_nr, w, h, fps) != 1 )
		return -1;
	break;
#endif
    case VJ_TAG_TYPE_CALI:
	sprintf(tag->source_name,"%s",filename);
	if(_vj_tag_new_cali( tag,stream_nr,w,h) != 1 ) 
		return -1;
	break;
    case VJ_TAG_TYPE_YUV4MPEG:
	sprintf(tag->source_name, "%s", filename);
	if (_vj_tag_new_yuv4mpeg(tag, stream_nr, w,h,fps) != 1)
	{
	    if(tag->source_name) free(tag->source_name);
	    if(tag) free(tag);
	    return -1;
	}
	tag->active = 1;
	break;
	case VJ_TAG_TYPE_GENERATOR:

	sprintf(tag->source_name, "[GEN]" );

	if( channel == -1 && filename == NULL ) {
		int total = 0;
		int agen = plug_find_generator_plugins( &total, 0 );
		
		if( agen >= 0 ) {
			channel = agen;
		}
	}

	if(channel >= 0 || filename != NULL)  {
		if( filename != NULL ) {
			channel = plug_get_idx_by_so_name( filename );
			if( channel == -1 ) {
				channel = plug_get_idx_by_name( filename );
				if( channel == - 1) {
					veejay_msg(0, "'%s' not found.",filename );
					free(tag->source_name );
					free(tag);
					return -1;
				}
			}
		}

		int foo_arg  = vj_shm_get_id();

		if( extra != 0 ) //@ vj_shm_set_id is a hack 
			vj_shm_set_id( extra );

		tag->generator = plug_activate(channel);
		
		if(tag->generator != NULL) {
			vj_shm_set_id( foo_arg );

			if( plug_get_num_input_channels( channel ) > 0 ||
				plug_get_num_output_channels( channel ) == 0 ) {
					veejay_msg(0, "Plug '%s' is not a generator", filename);
					plug_deactivate(tag->generator);
					free(tag->source_name);
					free(tag);
					return -1;
			}
			if( filename != NULL )
				strcpy( tag->source_name, filename );
		}
		else {
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to initialize generator '%s'",filename);
			free(tag->source_name);
			free(tag);
			return -1;
		}
	}
	break;

	case VJ_TAG_TYPE_COLOR:

	sprintf(tag->source_name, "[%d,%d,%d]",
		tag->color_r,tag->color_g,tag->color_b );
/*	
	if( channel == -1 ) {
		int total = 0;
		int agen = plug_find_generator_plugins( &total, 0 );
		
		if( agen >= 0 ) {
			char *plugname = plug_get_name( agen );
			channel = agen;
			veejay_msg(VEEJAY_MSG_DEBUG, "first available generator '%s' at index %d",plugname, agen );
			free(plugname);
		}
		else {
			veejay_msg(VEEJAY_MSG_WARNING, "No generator plugins found. Using built-in.");
		}
	}
	
	if( channel >= 0 ) {
		tag->generator = plug_activate( channel );
		if( tag->generator ) {
			char *plugname = plug_get_name( channel );
			if( plug_get_num_input_channels( channel ) > 0 ||
				plug_get_num_output_channels( channel ) != 1 ) {
				veejay_msg(0, "Plug '%s' is not a generator", plugname);
				plug_deactivate(tag->generator);
				free(tag->source_name );
				free(tag);
				return -1;
			}
			veejay_msg(VEEJAY_MSG_DEBUG, "Using plug '%s' to generate frames for this stream.",plugname);
			strcpy( tag->source_name, plugname );
			free(plugname);
		} else {
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to initialize generator.");
			free(tag->source_name);
			free(tag);
			return -1;
		}
	}*/

	tag->active = 1;
	break;

    default:
	veejay_msg(0, "Stream type %d invalid", type );
	return -1;
    }

	vj_tag_get_by_type( tag->source_type, tag->descr);

    /* effect chain is empty */
    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++)
	{
		tag->effect_chain[i] =
		    (sample_eff_chain *) vj_calloc(sizeof(sample_eff_chain));
		tag->effect_chain[i]->effect_id = -1;
		tag->effect_chain[i]->e_flag = 0;
		tag->effect_chain[i]->frame_trimmer = 0;
		tag->effect_chain[i]->frame_offset = 0;
		tag->effect_chain[i]->volume = 0;
		tag->effect_chain[i]->a_flag = 0;
		tag->effect_chain[i]->channel = 0;
		tag->effect_chain[i]->source_type = 1;
		tag->effect_chain[i]->is_rendering = 0; 
		for (j = 0; j < SAMPLE_MAX_PARAMETERS; j++) {
		    tag->effect_chain[i]->arg[j] = 0;
		}
		tag->effect_chain[i]->kf_status = 0;
		tag->effect_chain[i]->kf_type = 0;
		tag->effect_chain[i]->kf = vpn( VEVO_ANONYMOUS_PORT );
    }
    if (!vj_tag_put(tag))	
	{
		veejay_msg(0, "Unable to store stream");
		return -1;
	}
    last_added_tag = tag->id; 
	this_tag_id++;

#ifdef HAVE_FREETYPE
    tag->dict = vpn(VEVO_ANONYMOUS_PORT );
#endif

	tag_cache[ tag->id ] = (void*) tag;

   return (int)(tag->id);
}


int vj_tag_is_deleted(int id) {
	int i;
  for (i = 0; i < next_avail_tag; i++) {
	if (avail_tag[i] == id)
	    return 1;
    }
	return 0;
}

int vj_tag_exists(int id)
{
    if (!id)
	return 0;
    if (!vj_tag_get(id))
	return 0;
    return 1;
}

int vj_tag_clear_chain(int id)
{
    int i = 0;
    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
	if (vj_tag_chain_remove(id, i) == -1)
	    return -1;
    }
    return 1;
}

int	vj_tag_verify_delete(int id, int type )
{
	int i,j;
	for( i = 1; i < vj_tag_size()-1; i ++ )
	{
		vj_tag *s = vj_tag_get(i);
		if(s)
		{
			for( j = 0 ; j < SAMPLE_MAX_EFFECTS; j ++ )
			{
				if(s->effect_chain[j]->channel == id &&
				   s->effect_chain[j]->source_type == type )
				{
					s->effect_chain[j]->channel = i;
					s->effect_chain[j]->source_type = 1;
				}
			}
		}
	}
	return 1;
}

int vj_tag_del(int id)
{
    hnode_t *tag_node;
    vj_tag *tag;
    int i;
    tag = vj_tag_get(id);
    if(!tag)
	return 0;
#ifdef HAVE_FREETYPE
	vj_font_dictionary_destroy(_tag_info->font ,tag->dict);
#endif
    
    if(tag->extra)
 	free(tag->extra);

    /* stop streaming in first */
    switch(tag->source_type) {
	case VJ_TAG_TYPE_V4L: 
		if(tag->capture_type==1) { 
#ifdef HAVE_V4L2
			if( no_v4l2_threads_ ) {
				v4l2_close( vj_tag_input->unicap[tag->index]);
			} else {
				v4l2_thread_stop( v4l2_thread_info_get(vj_tag_input->unicap[tag->index]));
			}
#elif HAVE_V4L2
			v4lvideo_destroy( vj_tag_input->unicap[tag->index] );
#endif
		}
		if(tag->blackframe)free(tag->blackframe);
		if( tag->bf ) free(tag->bf);
		if( tag->bfu ) free(tag->bfu);
		if( tag->bfv ) free(tag->bfv);
		if( tag->lf ) free(tag->lf);
		if( tag->lfu ) free(tag->lfu);
		if( tag->lfv ) free(tag->lfv);

		break;
     case VJ_TAG_TYPE_YUV4MPEG: 
		veejay_msg(VEEJAY_MSG_INFO,"Closing yuv4mpeg file %s (Stream %d)",
			tag->source_name,id);
		vj_yuv_stream_stop_read(vj_tag_input->stream[tag->index]);
//		vj_yuv4mpeg_free( vj_tag_input->stream[tag->index]);
	 break;	
#ifdef SUPPORT_READ_DV2
      case VJ_TAG_TYPE_DV1394:
		vj_dv1394_close( vj_tag_input->dv1394[tag->index] );
		break;
#endif
#ifdef USE_GDK_PIXBUF
	case VJ_TAG_TYPE_PICTURE:
		veejay_msg(VEEJAY_MSG_INFO, "Closing picture stream %s", tag->source_name);
		vj_picture *pic = vj_tag_input->picture[tag->index];
		if(pic)
		{
			vj_picture_cleanup( pic->pic );
			free( pic );
		}
		vj_tag_input->picture[tag->index] = NULL;
		break;
#endif
	case VJ_TAG_TYPE_CALI:
		{
		cali_tag_t *pic = (cali_tag_t*) vj_tag_input->cali[tag->index];
		if(pic) {
			if(pic->lf) free(pic->data);
			free(pic);
		}
		vj_tag_input->cali[tag->index] = NULL;
		}
		break;
	case VJ_TAG_TYPE_MCAST:
	case VJ_TAG_TYPE_NET:
		net_thread_stop(tag);
		if(tag->priv) free(tag->priv);	
		break;
	case VJ_TAG_TYPE_COLOR:
		break;
	case VJ_TAG_TYPE_GENERATOR:
		if( tag->generator ) {
			plug_deactivate( tag->generator );
		}
		tag->generator = NULL;
		break;
    }
  
	vj_tag_chain_free( tag->id );

	if(tag->encoder_active)
		vj_tag_stop_encoder( tag->id );	
    if(tag->source_name) 
		free(tag->source_name);
	
	if(tag->method_filename) 
	{
		free(tag->method_filename);
    	tag->method_filename = NULL;
	}

	for (i = 0; i < SAMPLE_MAX_EFFECTS; i++)  {
		if (tag->effect_chain[i]) {
		 	if( tag->effect_chain[i]->kf )
				vpf(tag->effect_chain[i]->kf);
			free(tag->effect_chain[i]);
		}
		tag->effect_chain[i] = NULL;
	}

	if(tag->socket_frame)
	{
		free(tag->socket_frame);
		tag->socket_frame = NULL;
	}

	if(tag->viewport)
		viewport_destroy(tag->viewport);
#ifdef ARCH_X86_64
	uint64_t tid = (uint64_t) tag->id;
#else
	uint32_t tid = (uint32_t) tag->id;
#endif
   	tag_node = hash_lookup(TagHash, (void *) tid);

	if(tag_node)
	{
	    hash_delete(TagHash, tag_node);
		hnode_destroy(tag_node);
	}
    
	free(tag);
	tag = NULL;
    avail_tag[ next_avail_tag] = id;
    next_avail_tag++;

	tag_cache[ id ] = NULL;

	return 1;
}

void vj_tag_close_all() {
   int n=vj_tag_size();
   int i;
   vj_tag *tag;

   for(i=1; i < n; i++) {
    tag = vj_tag_get(i);
    if(tag) {
    	if(vj_tag_del(i)) veejay_msg(VEEJAY_MSG_DEBUG, "Deleted stream %d", i);
	}
   }
	
   if( TagHash )
	   hash_free_nodes( TagHash );
}

int	vj_tag_get_n_frames(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    return tag->n_frames;	
}

int	vj_tag_set_n_frames( int t1, int n )
{
  vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
  tag->n_frames = n;
  return 1;
}

int	vj_tag_load_composite_config( void *compiz, int t1 )
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag)
		return -1;

	int val = 0;
	void *temp = composite_load_config( compiz, tag->viewport_config, &val );
	if(temp == NULL || val == -1 )
		return 0;

	tag->composite = val;
	tag->viewport  = temp;
	return tag->composite; 
}

void	vj_tag_reload_config( void *compiz, int t1, int mode )
{
	vj_tag *tag = vj_tag_get(t1);
	if(tag) {
		if(tag->viewport_config) {
			free(tag->viewport_config);
			tag->viewport_config = NULL;
		}
		if(!tag->viewport_config) {
			tag->viewport_config = composite_get_config( compiz, mode );
		}
		tag->composite = mode;
	}
}
void	*vj_tag_get_composite_view( int t1 )
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return NULL;
	return tag->viewport;
}
int	vj_tag_set_composite_view( int t1, void *vp )
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	tag->viewport = vp;
	return 1;
}

int	vj_tag_set_composite( void *compiz,int t1, int n )
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	tag->composite = n;
	if( tag->viewport_config == NULL ) {
		tag->composite = 1;
		return 1;
	}
	composite_add_to_config( compiz, tag->viewport_config, n );
	
	return 1;
}

int vj_tag_get_effect(int t1, int position)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    if (position >= SAMPLE_MAX_EFFECTS)
	return -1;
    if (tag->effect_chain[position]->e_flag == 0)
	return -1;
    return tag->effect_chain[position]->effect_id;
}
void *vj_tag_get_plugin(int t1, int position, void *instance)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return NULL;
    if (position >= SAMPLE_MAX_EFFECTS)
	return NULL;

    if( instance != NULL )
	    tag->effect_chain[position]->fx_instance = instance;

    return tag->effect_chain[position]->fx_instance;
}


float vj_tag_get_fader_val(int t1) {
   vj_tag *tag = vj_tag_get(t1);
   if(!tag) return -1;
   return ( tag->fader_val );
}


int	vj_tag_set_description(int t1, char *description)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return 0;
	if(!description || strlen(description) <= 0 )
		snprintf( tag->descr, TAG_MAX_DESCR_LEN, "%s","Untitled"); 
	else
		snprintf( tag->descr, TAG_MAX_DESCR_LEN, "%s", description );
	return 1;
}

int	vj_tag_get_description( int t1, char *description )
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return 0;
	snprintf( description ,TAG_MAX_DESCR_LEN, "%s", tag->descr );
	return 1;
}

int vj_tag_set_manual_fader(int t1, int value )
{
  vj_tag *tag = vj_tag_get(t1);
  if(!tag) return -1;
  tag->fader_active = 2;
  tag->fader_inc = 0.0;
  tag->fader_val = (float)value;
  return 1;
}

float vj_tag_get_fader_inc(int t1) {
  vj_tag *tag = vj_tag_get(t1);
  if(!tag) return -1;
  return (tag->fader_inc);
}

int vj_tag_reset_fader(int t1) {
  vj_tag *tag = vj_tag_get(t1);
  if(!tag) return -1;
  tag->fader_active = 0;
  tag->fader_inc = 0.0;
  tag->fader_val = 0.0;
  return 1;
}

int vj_tag_set_fader_val(int t1, float val) {
  vj_tag *tag = vj_tag_get(t1);
  if(!tag) return -1;
  tag->fader_val = val;
  return 1;
}

int vj_tag_get_fader_active(int t1) {
   vj_tag *tag = vj_tag_get(t1);
   if(!tag) return -1;
   return (tag->fader_active);
}

int vj_tag_get_fader_direction(int t1) {
   vj_tag *tag = vj_tag_get(t1);
   if(!tag) return -1;
   return (tag->fader_direction);
}

int vj_tag_apply_fader_inc(int t1) {
  vj_tag *tag = vj_tag_get(t1);
  if(!tag) return -1;
  tag->fader_val += tag->fader_inc;
  if(tag->fader_val > 255.0 ) tag->fader_val = 255.0;
  if(tag->fader_val < 0.0) tag->fader_val = 0.0;
  if(tag->fader_direction) return tag->fader_val;
  return (255-tag->fader_val);
}

int vj_tag_set_fader_active(int t1, int nframes , int direction) {
  vj_tag *tag = vj_tag_get(t1);
  if(!tag) return -1;
  if(nframes <= 0) return -1;
  tag->fader_active = 1;
  if(direction<0)
	tag->fader_val = 255.0;
  else
	tag->fader_val = 0.0;
  tag->fader_val = 0.0;
  tag->fader_inc = (float) (255.0 / (float)nframes );
  tag->fader_direction = direction;
  tag->fader_inc *= direction;
  if(tag->effect_toggle == 0 )
	tag->effect_toggle = 1;
  return 1;
}

int vj_tag_stop_encoder(int t1) {
   vj_tag *tag = vj_tag_get(t1);
   if(!tag)
   {
	 veejay_msg(VEEJAY_MSG_ERROR, "Tag %d does not exist", t1);
	 return -1;
   }
   if(tag->encoder_active) {
	if(tag->encoder_file)
	       lav_close(tag->encoder_file);
        if(tag->encoder)
	       vj_avcodec_stop( tag->encoder, tag->encoder_format );
  	tag->encoder = NULL;
	tag->encoder_file = NULL; 
 	tag->encoder_active = 0;
	return 1;
   }

   return 0;
}

void vj_tag_reset_encoder(int t1)
{
   vj_tag *tag = vj_tag_get(t1);
   if(!tag) return;
   tag->encoder_format = 0;
   tag->encoder_width = 0;
   tag->encoder_height = 0;
   tag->encoder_max_size = 0;
   tag->encoder_active = 0;
   tag->encoder_total_frames_recorded = 0;
   tag->encoder_frames_to_record = 0;
   tag->encoder_frames_recorded = 0;
}

int vj_tag_get_encoded_file(int t1, char *description)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return 0;
	sprintf(description, "%s", tag->encoder_destination );
	return 1;
}

int	vj_tag_get_num_encoded_files(int t1)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	return tag->sequence_num;
}

int	vj_tag_get_encoder_format(int t1)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	return tag->encoder_format;
}

int	vj_tag_get_sequenced_file(int t1, char *descr, int num, char *ext)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	sprintf(descr, "%s-%05d.%s", tag->encoder_destination,num,ext );
	return 1;
}


int	vj_tag_try_filename(int t1, char *filename, int format)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) 
	{
		return 0;
	}
	if(filename != NULL)
	{
		snprintf(tag->encoder_base, 255, "%s", filename);
	}
	char ext[5];
	switch(format)
	{
		case ENCODER_QUICKTIME_DV:
		case ENCODER_QUICKTIME_MJPEG:
			sprintf(ext, "mov");
			break;
		case ENCODER_YUV4MPEG:
			sprintf(ext, "yuv");
			break;
		case ENCODER_DVVIDEO:
			sprintf(ext,"dv");
			break;
		default:
			sprintf(ext,"avi");
			break;
	}	
	
	sprintf(tag->encoder_destination, "%s-%04d.%s", tag->encoder_base, (int)tag->sequence_num, ext);
	return 1;
}



static int vj_tag_start_encoder(vj_tag *tag, int format, long nframes)
{
	char cformat = vj_avcodec_find_lav( format );
	
	tag->encoder =  vj_avcodec_start( _tag_info->effect_frame1, format, tag->encoder_destination );
	if(!tag->encoder)
	{
		veejay_msg(0, "Unable to use selected encoder, please choose another.");
		return 0;
	}
	tag->encoder_active = 1;
	tag->encoder_format = format;

	int tmp = _tag_info->effect_frame1->len;

	if(format==ENCODER_DVVIDEO)
		tag->encoder_max_size = ( _tag_info->video_output_height == 480 ? 120000: 144000);
	else
		switch(format)
		{
			case ENCODER_YUV420:
			case ENCODER_YUV420F:
			tag->encoder_max_size = 2048 + tmp + (tmp/4) + (tmp/4);break;
			case ENCODER_YUV422:
			case ENCODER_YUV422F:
			case ENCODER_YUV4MPEG:
			tag->encoder_max_size = 2048 + tmp + (tmp/2) + (tmp/2);break;
			case ENCODER_LZO:
			tag->encoder_max_size = tmp * 3; break;
			default:
			tag->encoder_max_size = ( 4 * 65535 );
			break;
		}

	if(tag->encoder_total_frames_recorded == 0)
	{
		tag->encoder_frames_to_record = nframes ; 
		tag->encoder_frames_recorded = 0;
	}
	else
	{
		tag->encoder_frames_recorded = 0;
	}

	if( sufficient_space( tag->encoder_max_size, nframes ) == 0 )
	{
		vj_avcodec_close_encoder(tag->encoder );
		tag->encoder_active = 0;
		tag->encoder = NULL;
		return 0;
	}

	if( cformat != 'S' ) {
		tag->encoder_file = lav_open_output_file(
			tag->encoder_destination,
			cformat,
			_tag_info->effect_frame1->width,
			_tag_info->effect_frame1->height,
			0,
			_tag_info->effect_frame1->fps,
			0,
			0,
			0
		);


		if(!tag->encoder_file)
		{
			veejay_msg(VEEJAY_MSG_ERROR,"Cannot write to %s (No permissions?)",tag->encoder_destination);
			if(tag->encoder)
				vj_avcodec_close_encoder( tag->encoder );
			tag->encoder = NULL;
			tag->encoder_active = 0;
			return 0;
		}
	}

	veejay_msg(VEEJAY_MSG_INFO, "Recording to file [%s] %ldx%ld@%2.2f %d/%d/%d >%09ld<",
		    tag->encoder_destination, 
		    _tag_info->effect_frame1->width,
		    _tag_info->effect_frame1->height,
		    (float) _tag_info->effect_frame1->fps,
			0,0,0,
			(long)( tag->encoder_frames_to_record)
		);


	tag->encoder_width = _tag_info->effect_frame1->width;
	tag->encoder_height = _tag_info->effect_frame1->height;
	
	return 1;
}


int vj_tag_init_encoder(int t1, char *filename, int format, long nframes ) {
  vj_tag *tag = vj_tag_get(t1);
  if(!tag) return 0;

  if(tag->encoder_active)
  {
	veejay_msg(VEEJAY_MSG_ERROR, "Already recording Stream %d to [%s]",t1, tag->encoder_destination);
  return 0;
  }

  if(!vj_tag_try_filename( t1,filename,format))
  {
	return 0;
  }
  if(nframes <= 0)
  {
	veejay_msg(VEEJAY_MSG_ERROR, "It makes no sense to encode for %ld frames", nframes);
	return 0;
  }

  return vj_tag_start_encoder( tag,format, nframes );
}

int vj_tag_continue_record( int t1 )
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;

	long bytesRemaining = lav_bytes_remain( tag->encoder_file );
	if( bytesRemaining >= 0 && bytesRemaining < (512 * 1024) ) {
		tag->sequence_num ++;
		veejay_msg(VEEJAY_MSG_WARNING,
			"Auto splitting file, %ld frames left to record.",
			(tag->encoder_frames_to_record - tag->encoder_total_frames_recorded ));
		tag->encoder_frames_recorded=0;

		return 2;
	}
	
	if( tag->encoder_total_frames_recorded >= tag->encoder_frames_to_record)
	{
		veejay_msg(VEEJAY_MSG_INFO, "Recorded %ld frames.",
			tag->encoder_total_frames_recorded );
		return 1;
	}
	
	return 0;

}
int vj_tag_set_brightness(int t1, int value)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return 0;
	if(value < 0 || value > 65535)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Brightness valid range is 0 - 65535");
		return 0;
	}
	else	
	{
		if(tag->capture_type==1) {
#ifdef HAVE_V4L2
			v4l2_set_brightness( vj_tag_input->unicap[tag->index],value);
#elif HAVE_V4L
			v4lvideo_set_brightness( vj_tag_input->unicap[tag->index], value );
#endif
		}
	}
	return 1;
}
int vj_tag_set_white(int t1, int value)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	if(value < 0 || value > 65535)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"White valid range is 0 - 65535");
		return -1;
	}
	else
	{
		if(tag->capture_type==1) {
#ifdef HAVE_V4L
			v4lvideo_set_white( vj_tag_input->unicap[tag->index],value );
#elif HAVE_V4L2
			v4l2_set_temperature( vj_tag_input->unicap[tag->index],value);
#endif
		}				
	}
	return 1;
}

int vj_tag_set_hue(int t1, int value)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	if(value < 0 || value > 65535)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Hue valid range is 0 - 65535");
		return -1;
	}
	if( tag->capture_type==1) {
#ifdef HAVE_V4L
		v4lvideo_set_hue( vj_tag_input->unicap[tag->index], value ); 
#elif HAVE_V4L2
		v4l2_set_hue( vj_tag_input->unicap[tag->index],value ); 	
#endif
	}
	return 1;
}
int vj_tag_set_contrast(int t1,int value)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	if(value < 0 || value > 65535)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Contrast valid range is 0 - 65535");
		return -1;
	}
	else
	{
		if(tag->capture_type==1) {
#ifdef HAVE_V4L
			v4lvideo_set_contrast( vj_tag_input->unicap[tag->index], value );
#elif HAVE_V4L2
			v4l2_set_contrast( vj_tag_input->unicap[tag->index], value );
#endif
		}
	}
	return 1;
}
int vj_tag_set_color(int t1, int value)
{
	vj_tag *tag = vj_tag_get(t1);

	if(!tag) return -1;
	if(value < 0 || value > 65535)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Contrast valid range is 0 - 65535");
		return -1;
	}
	else
	{
		if(tag->capture_type==1) {
#ifdef HAVE_V4L
			v4lvideo_set_colour( vj_tag_input->unicap[tag->index], value );
#elif HAVE_V4L2
			v4l2_set_contrast( vj_tag_input->unicap[tag->index],value);
#endif
		}
	}
	return 1;
}


int	vj_tag_get_v4l_properties(int t1,
		int *brightness, int *contrast, int *hue, int *color, int *white )
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	if(tag->source_type!=VJ_TAG_TYPE_V4L)
	{
		return -1;
	}

	if(tag->capture_type == 1 ) {
#ifdef HAVE_V4L
		*brightness = v4lvideo_get_brightness(  vj_tag_input->unicap[tag->index] );
		*contrast  = v4lvideo_get_contrast( vj_tag_input->unicap[tag->index] );
		*hue	    = v4lvideo_get_hue( vj_tag_input->unicap[tag->index] );
		*color	    = v4lvideo_get_colour( vj_tag_input->unicap[tag->index] );
		*white     =  v4lvideo_get_white( vj_tag_input->unicap[tag->index] );
#elif HAVE_V4L2
		*brightness = v4l2_get_brightness(  vj_tag_input->unicap[tag->index] );
		*contrast  = v4l2_get_contrast( vj_tag_input->unicap[tag->index] );
		*hue	    = v4l2_get_hue( vj_tag_input->unicap[tag->index] );
		*color	    = v4l2_get_saturation( vj_tag_input->unicap[tag->index] );
		*white     =  v4l2_get_temperature( vj_tag_input->unicap[tag->index] );
#endif
		return 0;
	}

	return 0;
}

int vj_tag_get_effect_any(int t1, int position) {
	vj_tag *tag = vj_tag_get(t1);
	if(!tag )
	   return 0;
	return tag->effect_chain[position]->effect_id;
}

int vj_tag_chain_malloc(int t1)
{
    vj_tag  *tag = vj_tag_get(t1);
    int i=0;
    int e_id = 0; 
    int sum =0;

    for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
    {
		e_id = tag->effect_chain[i]->effect_id;
		if(e_id)
		{
			int res = 0;
			tag->effect_chain[i]->fx_instance = vj_effect_activate(e_id, &res);
			if( res )
				   sum ++;
		}
	}
    veejay_msg(VEEJAY_MSG_DEBUG, "Allocated %d effects",sum);
	return sum; 
}

int vj_tag_chain_free(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    int i=0;
    int e_id = 0; 
    int sum = 0;
   
    for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
    {
		e_id = tag->effect_chain[i]->effect_id;
		if(e_id!=-1)
		{
			if(vj_effect_initialized(e_id, tag->effect_chain[i]->fx_instance) )
			{
				vj_effect_deactivate(e_id, tag->effect_chain[i]->fx_instance);
				tag->effect_chain[i]->fx_instance = NULL;
				if(tag->effect_chain[i]->kf)
					vpf(tag->effect_chain[i]->kf );
				tag->effect_chain[i]->kf = vpn(VEVO_ANONYMOUS_PORT );

				sum++;
			}
			if( tag->effect_chain[i]->source_type == 1 && 
				vj_tag_get_active( tag->effect_chain[i]->channel ) && 
				vj_tag_get_type( tag->effect_chain[i]->channel ) == VJ_TAG_TYPE_NET ) {
				vj_tag_disable( tag->effect_chain[i]->channel );
			}

	  	}
	}  
    return sum;
}

int	vj_tag_chain_reset_kf( int s1, int entry )
{
   vj_tag *tag = vj_tag_get(s1);
   if (!tag) return -1;
   tag->effect_chain[entry]->kf_status = 0;
   tag->effect_chain[entry]->kf_type = 0;
   if(tag->effect_chain[entry]->kf)
     vpf( tag->effect_chain[entry]->kf);
   tag->effect_chain[entry]->kf = vpn( VEVO_ANONYMOUS_PORT );
   return 1;	
}

int	vj_tag_get_kf_status(int s1, int entry, int *type )
{
   vj_tag *tag = vj_tag_get(s1);
   if (!tag)
	return 0;
   if(type != NULL)
	   *type = tag->effect_chain[entry]->kf_type;

   return tag->effect_chain[entry]->kf_status;
}

void	vj_tag_set_kf_type(int s1, int entry, int type )
{
   vj_tag *tag = vj_tag_get(s1);
   if (!tag)
	return;
   tag->effect_chain[entry]->kf_type = type;
}


int	vj_tag_get_kf_tokens( int s1, int entry, int id, int *start,int *end, int *type)
{
  vj_tag *tag = vj_tag_get(s1);
   if (!tag)
	return 0;
   return keyframe_get_tokens( tag->effect_chain[entry]->kf,id, start,end,type);
}


int	vj_tag_chain_set_kf_status( int s1, int entry, int status )
{
   vj_tag *tag = vj_tag_get(s1);
   if (!tag) return -1;
   tag->effect_chain[entry]->kf_status = status;
   return 1;	
}

unsigned char *vj_tag_chain_get_kfs( int s1, int entry, int parameter_id, int *len )
{
   vj_tag *tag = vj_tag_get(s1);
   if (!tag)
	return NULL;
   if ( entry < 0 || entry > SAMPLE_MAX_EFFECTS )
        return NULL;
   if( parameter_id < 0 || parameter_id > 9 )
	return NULL;

   unsigned char *data = keyframe_pack( tag->effect_chain[entry]->kf, parameter_id,entry, len );
   if( data )
	return data;
   return NULL;
}

void    *vj_tag_get_kf_port( int s1, int entry )
{
	vj_tag *tag = vj_tag_get(s1);
	if(!tag) return NULL;
        return tag->effect_chain[entry]->kf;
}


int	vj_tag_chain_set_kfs( int s1, int len, unsigned char *data )
{
	vj_tag *tag = vj_tag_get(s1);
	if (!tag)  return -1;
	if( len <= 0 )
	{
		veejay_msg(0, "Invalid keyframe packet length");
		return -1;
	}

	int entry = 0;
 	if(!keyframe_unpack( data, len, &entry,s1,0) )
   	{
		veejay_msg(0, "Unable to unpack keyframe packet");
		return -1;
   	}

	if ( entry < 0 || entry > SAMPLE_MAX_EFFECTS )
	{
		veejay_msg(0, "Invalid FX entry in KF packet");
		return -1;
	}

   	return 1;
}



int vj_tag_set_effect(int t1, int position, int effect_id)
{
    int params, i;
    vj_tag *tag = vj_tag_get(t1);

    if (!tag)
		return 0;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
		return 0;

    if( tag->effect_chain[position]->effect_id != -1 && tag->effect_chain[position]->effect_id != effect_id )
    {
		//verify if the effect should be discarded
		if(vj_effect_initialized( tag->effect_chain[position]->effect_id, tag->effect_chain[position]->fx_instance ))
		{
			if(!vj_effect_is_plugin( tag->effect_chain[position]->effect_id )) {
					int i = 0;
					int frm = 1;
					for( i =0; i < SAMPLE_MAX_EFFECTS; i ++ ) {
							if( i == position )
									continue;
							if( tag->effect_chain[i]->effect_id == effect_id )
									frm = 0;
					}
					if( frm == 1 ) {
						vj_effect_deactivate( tag->effect_chain[position]->effect_id, tag->effect_chain[position]->fx_instance );
						tag->effect_chain[position]->fx_instance = NULL;
					}
			} else {
					vj_effect_deactivate( tag->effect_chain[position]->effect_id, tag->effect_chain[position]->fx_instance );
					tag->effect_chain[position]->fx_instance = NULL;
			}

		}
		if( tag->effect_chain[position]->source_type == 1 && 
			vj_tag_get_active( tag->effect_chain[position]->channel ) && 
			tag->effect_chain[position]->channel != t1 &&
			vj_tag_get_type( tag->effect_chain[position]->channel ) == VJ_TAG_TYPE_NET ) {
			vj_tag_disable( tag->effect_chain[position]->channel );
		}

    }

    if (!vj_effect_initialized(effect_id, tag->effect_chain[position]->fx_instance ))
    {
 		int res = 0;
		tag->effect_chain[position]->fx_instance = vj_effect_activate( effect_id, &res );
		if(!res) {
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot activate FX %d", effect_id );
			tag->effect_chain[position]->effect_id = -1;
			tag->effect_chain[position]->e_flag = 1;
			int i;
			for( i = 0; i < SAMPLE_MAX_PARAMETERS; i ++ ) 
				tag->effect_chain[position]->arg[i] = 0;

			tag->effect_chain[position]->frame_trimmer = 0;
			return 0;
		}
    }

	if( tag->effect_chain[position]->effect_id != effect_id )
	{
		params = vj_effect_get_num_params(effect_id);
		for (i = 0; i < params; i++) {
			tag->effect_chain[position]->arg[i] = vj_effect_get_default(effect_id, i);
		}
		tag->effect_chain[position]->e_flag = 1; 
			tag->effect_chain[position]->kf_status = 0;
		tag->effect_chain[position]->kf_type = 0;
		if(tag->effect_chain[position]->kf)
			vpf(tag->effect_chain[position]->kf );
		// tag does not have chain_alloc_kf
		tag->effect_chain[position]->kf = vpn(VEVO_ANONYMOUS_PORT );
	}

    tag->effect_chain[position]->effect_id = effect_id;
    

	if (vj_effect_get_extra_frame(effect_id)) {
		if(tag->effect_chain[position]->source_type < 0)
		 tag->effect_chain[position]->source_type = 1;
		if(tag->effect_chain[position]->channel <= 0 )
		 tag->effect_chain[position]->channel = t1;
    }

    return 1;
}

int	vj_tag_has_cali_fx( int t1 ) {
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    int i;
    for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++ ) {
	if( tag->effect_chain[i]->effect_id == VJ_IMAGE_EFFECT_CALI )
		return i;
    }
    return -1;
}

void	vj_tag_cali_prepare_now( int t1, int fx_id ) {
	vj_tag *tag = vj_tag_get(t1);
	if(tag==NULL)
		return;
	if(tag->source_type != VJ_TAG_TYPE_CALI )
		return;
	cali_tag_t *p = (cali_tag_t*) vj_tag_input->cali[tag->index];
	if( p == NULL )
		return;
	if( fx_id <=  0)
		return;

	cali_prepare( vj_effect_get_data(fx_id),
		      p->mean[0],
		      p->mean[1],
		      p->mean[2],
		      p->data,
		      vj_tag_input->width * vj_tag_input->height,
		      vj_tag_input->uv_len );

}

void	vj_tag_cali_prepare( int t1 , int pos, int cali_tag) {
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return;
    vj_tag *tagc = vj_tag_get(cali_tag);
    if(!tagc)
	    return;
    if(tagc->source_type != VJ_TAG_TYPE_CALI)
	    return;
    int fx_id = vj_effect_real_to_sequence( tag->effect_chain[pos]->effect_id );
    if (fx_id >= 0 ) {
	    vj_tag_cali_prepare_now( cali_tag, fx_id );
    }
}

int vj_tag_get_chain_status(int t1, int position)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    if (position >= SAMPLE_MAX_EFFECTS)
	return -1;
    return tag->effect_chain[position]->e_flag;
}

int vj_tag_set_chain_status(int t1, int position, int status)
{
    vj_tag *tag = vj_tag_get(t1);
    
    if (position >= SAMPLE_MAX_EFFECTS)
	return -1;
    tag->effect_chain[position]->e_flag = status;
    return 1;
}

int vj_tag_get_trimmer(int t1, int position)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return 0;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return 0;
    return tag->effect_chain[position]->frame_trimmer;
}

int vj_tag_set_trimmer(int t1, int position, int trim)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;
    tag->effect_chain[position]->frame_trimmer = trim;
    return 1;
}

int vj_tag_get_all_effect_args(int t1, int position, int *args,
			       int arg_len, int n_frame)
{
    int i = 0;
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    if (arg_len == 0 )
	return 1;
    if (!args)
	return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;
    if (arg_len < 0 || arg_len > SAMPLE_MAX_PARAMETERS)
	return -1;

	if( tag->effect_chain[position]->kf_status )
	{
		for( i =0;i <arg_len; i ++ )
		{
			int tmp = 0;
			if(!get_keyframe_value( tag->effect_chain[position]->kf, n_frame, i ,&tmp ) )
				args[i] = tag->effect_chain[position]->arg[i];
			else {
				args[i] = tmp;
			}
		}
	}
	else
	{
		for( i = 0; i < arg_len; i ++ )
			args[i] = tag->effect_chain[position]->arg[i];
	}

        return 1;
}

int vj_tag_get_effect_arg(int t1, int position, int argnr)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;
    if (argnr < 0 || argnr > SAMPLE_MAX_PARAMETERS)
	return -1;

    return tag->effect_chain[position]->arg[argnr];
}

int vj_tag_set_effect_arg(int t1, int position, int argnr, int value)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;
    if (argnr < 0 || argnr > SAMPLE_MAX_PARAMETERS)
	return -1;

    tag->effect_chain[position]->arg[argnr] = value;
    return 1;
}

int vj_tag_get_type(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    return tag->source_type;
}

int vj_tag_get_logical_index(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    return tag->index;
}

int vj_tag_set_logical_index(int t1, int stream_nr)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    tag->index = stream_nr;
    return 1;
}

int vj_tag_get_depth(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    return tag->index;
}

int vj_tag_disable(int t1) {
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	if(tag->active == 0)
	{
		veejay_msg(VEEJAY_MSG_INFO, "Already inactive");
		return 1;
	}
	if(tag->source_type == VJ_TAG_TYPE_NET || tag->source_type == VJ_TAG_TYPE_MCAST)
	{
		net_thread_stop( tag );
	}
	if(tag->source_type == VJ_TAG_TYPE_V4L )
	{
#ifdef HAVE_V4L
		if( tag->capture_type == 1 ) {
			if(v4lvideo_is_active(vj_tag_input->unicap[tag->index] ) )
				v4lvideo_set_paused( vj_tag_input->unicap[tag->index] , 1 );
		}
#elif HAVE_V4L2
		if(tag->capture_type==1) {
			if( no_v4l2_threads_ ) {
				v4l2_set_status( vj_tag_input->unicap[tag->index],1);
			} else {
				v4l2_thread_set_status( v4l2_thread_info_get(vj_tag_input->unicap[tag->index]),0 );
			}
		}
#endif

	}

#ifdef USE_GDK_PIXBUF
	if(tag->source_type == VJ_TAG_TYPE_PICTURE )
	{
		vj_picture *pic = vj_tag_input->picture[tag->index];
		if(pic)
		{
			vj_picture_cleanup( pic->pic );
		}
		vj_tag_input->picture[tag->index] = pic;
	}
#endif
	tag->active = 0;
	return 1;
}

int vj_tag_enable(int t1) {
	vj_tag *tag = vj_tag_get(t1);
	if( tag->source_type == VJ_TAG_TYPE_V4L )
	{
		if(tag->capture_type == 1 ) {
#ifdef HAVE_V4L
			if(!v4lvideo_is_active(  vj_tag_input->unicap[tag->index] ) ) {
				if(v4lvideo_grabstart(  vj_tag_input->unicap[tag->index] ) == 0 )
				{
					tag->active = 1;
					veejay_msg(VEEJAY_MSG_INFO,"Started continues capturing");
				} else {
					veejay_msg(VEEJAY_MSG_ERROR, "Failed to start grabbing.");
				}
			} else {
				if(v4lvideo_is_paused( vj_tag_input->unicap[tag->index] ) )
					v4lvideo_set_paused( vj_tag_input->unicap[tag->index],0);
				tag->active = 1;
			}
#elif HAVE_V4L2
			if( no_v4l2_threads_ ) {
				v4l2_set_status( vj_tag_input->unicap[tag->index],1);
			} else {
				v4l2_thread_set_status( v4l2_thread_info_get( vj_tag_input->unicap[tag->index] ), 1 );
			}
#endif
		}
		return 1;
	}
	if(tag->source_type == VJ_TAG_TYPE_NET || tag->source_type == VJ_TAG_TYPE_MCAST )
	{
		if(!net_thread_start(tag, vj_tag_input->width , vj_tag_input->height,  vj_tag_input->pix_fmt))
		{
			veejay_msg(VEEJAY_MSG_ERROR,
					"Unable to start thread");
			return 1;
		}
	}
#ifdef USE_GDK_PIXBUF
	if( tag->source_type == VJ_TAG_TYPE_PICTURE )
	{
		vj_picture *p = vj_tag_input->picture[ tag->index ];
		p->pic = vj_picture_open( tag->source_name, 
			vj_tag_input->width, vj_tag_input->height,
			vj_tag_input->pix_fmt );

		if(!p->pic)
			return -1;

		vj_tag_input->picture[tag->index] = p;
		veejay_msg(VEEJAY_MSG_DEBUG, "Streaming from picture '%s'", tag->source_name );
	}
#endif
	tag->active = 1;

	return 1;
}

int vj_tag_set_depth(int t1, int depth)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    tag->depth = depth;
    return 1;
}


int vj_tag_set_active(int t1, int active)
{
    vj_tag *tag;
    tag  = vj_tag_get(t1);
    if (!tag)
	return -1;

    if(active == tag->active)
	    return 1;
 	
    switch (tag->source_type) {
	   case VJ_TAG_TYPE_V4L:

		if(tag->capture_type == 1 ) {
#ifdef HAVE_V4L
			if(active) {
			     if( !v4lvideo_is_active( vj_tag_input->unicap[tag->index] ) ) {
					if(v4lvideo_grabstart( vj_tag_input->unicap[tag->index] ) < 0 ) {
						active = 1;
						veejay_msg(VEEJAY_MSG_INFO, "Started grabbing ");
					} else {
						veejay_msg(VEEJAY_MSG_ERROR, "Unable to start grabbing.");
						active = 0;
					}
			     } else {
				if( v4lvideo_is_paused( vj_tag_input->unicap[tag->index] )  )
					v4lvideo_set_paused( vj_tag_input->unicap[tag->index], 0 );
				}
			}
			else {
				if( !v4lvideo_is_paused( vj_tag_input->unicap[tag->index] )  )
					v4lvideo_set_paused( vj_tag_input->unicap[tag->index], 1 );
			}
#elif HAVE_V4L2
			if( no_v4l2_threads_ ) {
				v4l2_set_status( vj_tag_input->unicap[tag->index],1);

			} else {
				v4l2_thread_set_status( v4l2_thread_info_get( vj_tag_input->unicap[tag->index]), active );
			}
#endif
		} 
		tag->active = active;
		break;
	case VJ_TAG_TYPE_YUV4MPEG:
	     if(active==0)
		{
		     tag->active = 0;
		     vj_yuv_stream_stop_read( vj_tag_input->stream[tag->index]);
		}
	break;
	case VJ_TAG_TYPE_MCAST:
	case VJ_TAG_TYPE_NET:
	case VJ_TAG_TYPE_PICTURE:
		if(active == 1 )
			vj_tag_enable( t1 );
		else
			vj_tag_disable( t1 );
		break;
    default:
	tag->active = active;
	break;
    }

    return 1;
}

int vj_tag_get_active(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    return tag->active;
}

int	vj_tag_get_subrender(int t1)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag)
		return -1;
	return tag->subrender;
}

void	vj_tag_set_subrender(int t1, int status)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag)
		return;
	tag->subrender = status;
}

int vj_tag_set_chain_channel(int t1, int position, int channel)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;

    if( tag->effect_chain[position]->source_type == 1 && 
	vj_tag_get_active( tag->effect_chain[position]->channel ) && 
	tag->effect_chain[position]->channel != t1 &&
	vj_tag_get_type( tag->effect_chain[position]->channel ) == VJ_TAG_TYPE_NET ) {
	vj_tag_disable( tag->effect_chain[position]->channel );
    }


    tag->effect_chain[position]->channel = channel;

    return 1;
}

int vj_tag_get_chain_channel(int t1, int position)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;
    return tag->effect_chain[position]->channel;
}
int vj_tag_set_chain_source(int t1, int position, int source)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
		return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
		return -1;

	if( tag->effect_chain[position]->source_type == 1 && 
		vj_tag_get_active( tag->effect_chain[position]->channel ) && 
		tag->effect_chain[position]->channel != t1 &&
		vj_tag_get_type( tag->effect_chain[position]->channel ) == VJ_TAG_TYPE_NET ) {
		vj_tag_disable( tag->effect_chain[position]->channel );
	}

    tag->effect_chain[position]->source_type = source;
    return 1;
}

int vj_tag_get_chain_source(int t1, int position)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;
    return tag->effect_chain[position]->source_type;
}

int vj_tag_chain_size(int t1)
{
    int i;
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    for (i = SAMPLE_MAX_EFFECTS - 1; i != 0; i--) {
	if (tag->effect_chain[i]->effect_id != -1)
	    return i;
    }
    return 0;
}

int vj_tag_get_effect_status(int t1) {
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	return tag->effect_toggle;	
}
int vj_tag_get_selected_entry(int t1) {
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	return tag->selected_entry;
}

int vj_tag_set_effect_status(int t1, int status)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	if(status==1 || status==0) 
	{
		tag->effect_toggle = status;
	}
	return 1;
}

int vj_tag_set_selected_entry(int t1, int position) 
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	if(position < 0 || position >= SAMPLE_MAX_EFFECTS) return -1;
	tag->selected_entry = position;
	return 1;
}

static int vj_tag_chain_can_delete(vj_tag *tag, int reserved, int effect_id)
{
	int i;

	if( vj_effect_is_plugin(effect_id ) )
		return 1;

	for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
	{
		if(i != reserved && effect_id == tag->effect_chain[i]->effect_id) 
			return 0;
	}
	
	return 1;
}

int vj_tag_chain_remove(int t1, int index)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
		return -1;
    if( tag->effect_chain[index]->effect_id != -1)
    {
		if( vj_effect_initialized( tag->effect_chain[index]->effect_id, tag->effect_chain[index]->fx_instance ) && vj_tag_chain_can_delete( tag, index, tag->effect_chain[index]->effect_id ) )
		{
			vj_effect_deactivate( tag->effect_chain[index]->effect_id, tag->effect_chain[index]->fx_instance );
			tag->effect_chain[index]->fx_instance = NULL;
		}
    }

    tag->effect_chain[index]->effect_id = -1;
    tag->effect_chain[index]->e_flag = 0;

    if( tag->effect_chain[index]->kf )
		vpf(tag->effect_chain[index]->kf );
	
	tag->effect_chain[index]->kf = vpn(VEVO_ANONYMOUS_PORT);

	if( tag->effect_chain[index]->source_type == 1 && 
		vj_tag_get_active( tag->effect_chain[index]->channel ) && 
		tag->effect_chain[index]->channel != t1 &&
		vj_tag_get_type( tag->effect_chain[index]->channel ) == VJ_TAG_TYPE_NET ) {
		vj_tag_disable( tag->effect_chain[index]->channel );
	}


    tag->effect_chain[index]->source_type = 1;
    tag->effect_chain[index]->channel     = t1; //set to self

	int j;
    for (j = 0; j < SAMPLE_MAX_PARAMETERS; j++)
		tag->effect_chain[index]->arg[j] = 0;

    return 1;
}


void vj_tag_get_source_name(int t1, char *dst)
{
    vj_tag *tag = vj_tag_get(t1);
    if (tag) {
	sprintf(dst, "%s", tag->source_name);
    } else {
	vj_tag_get_description( tag->source_type, dst );
    }
}

void vj_tag_get_method_filename(int t1, char *dst)
{
    vj_tag *tag = vj_tag_get(t1);
    if (tag) {
	if(tag->method_filename != NULL) sprintf(dst, "%s", tag->method_filename);
    }
}


void	vj_tag_get_by_type(int type, char *description )
{
 	switch (type) {
	case VJ_TAG_TYPE_GENERATOR:
	sprintf(description, "Generator");
	break;
    case VJ_TAG_TYPE_COLOR:
	sprintf(description, "Solid" );
	break;
    case VJ_TAG_TYPE_NONE:
	sprintf(description, "%s", "EditList");
	break;
	case VJ_TAG_TYPE_MCAST:
	sprintf(description, "%s", "Multicast");
	break;
	case VJ_TAG_TYPE_NET:
	sprintf(description, "%s", "Unicast");
	break;
#ifdef USE_GDK_PIXBUF
	case VJ_TAG_TYPE_PICTURE:
	sprintf(description, "%s", "GdkPixbuf");
	break;
#endif
    case VJ_TAG_TYPE_V4L:
	sprintf(description, "%s", "Video4Linux");
	break;
#ifdef SUPPORT_READ_DV2
	case VJ_TAG_TYPE_DV1394:
	sprintf(description, "%s", "DV1394");
	break;
#endif
    case VJ_TAG_TYPE_YUV4MPEG:
	sprintf(description, "%s", "YUV4MPEG");
	break;
    case VJ_TAG_TYPE_CALI:
	sprintf(description, "%s", "Image Calibration");
	break;
    }

}

void vj_tag_get_descriptive(int id, char *description)
{
    vj_tag *tag = vj_tag_get(id);
    if(!tag)
	{
		sprintf(description, "invalid");
	}
    else	
	{
		vj_tag_get_by_type( tag->source_type, description );
	}
}

/* this method tries to find a tag of given type */
int vj_tag_by_type(int type)
{
    int min;
    for (min = 1; min < this_tag_id; ++min) {
	if (vj_tag_get_type(min) == type)
	    return min;
    }
    return 0;
}



int vj_tag_set_offset(int t1, int chain_entry, int frame_offset)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    /* set to zero if frame_offset is greater than sample length */
    if (frame_offset < 0)
	frame_offset = 0;

    tag->effect_chain[chain_entry]->frame_offset = frame_offset;
    return 1;
}

int vj_tag_get_offset(int t1, int chain_entry)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;

    return tag->effect_chain[chain_entry]->frame_offset;
}

long vj_tag_get_encoded_frames(int s1) {
  vj_tag *si = vj_tag_get(s1);
  if(!si) return -1;
  return ( si->encoder_total_frames_recorded );
}


long vj_tag_get_total_frames( int s1 )
{
  vj_tag *si = vj_tag_get(s1);
  if(!si) return -1;
  return ( si->encoder_frames_to_record );
}

int vj_tag_reset_autosplit(int s1)
{
  vj_tag *si = vj_tag_get(s1);
  if(!si) return -1;
  veejay_memset( si->encoder_base, 0,sizeof(si->encoder_base) );
  veejay_memset( si->encoder_destination,0 , sizeof(si->encoder_destination) );
  si->sequence_num = 0;
  return 1;
}

long vj_tag_get_frames_left(int s1)
{
	vj_tag *si= vj_tag_get(s1);
	if(!si) return 0;
	return ( si->encoder_frames_to_record - si->encoder_total_frames_recorded );
}

int vj_tag_encoder_active(int s1)
{
	vj_tag *si = vj_tag_get(s1);
	if(!si)return 0;
	return si->encoder_active;
}

int	vj_tag_var(int t1, int *type, int *fader, int *fx_sta , int *rec_sta, int *active )
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return 0;
	*fader  = tag->fader_active;
	*fx_sta = tag->effect_toggle;
	*rec_sta = tag->encoder_active;
	*type = tag->source_type;
	*active = tag->active;
	return 1;
}

int vj_tag_record_frame(int t1, uint8_t *buffer[4], uint8_t *abuff, int audio_size,int pixel_format) {
   vj_tag *tag = vj_tag_get(t1);
   int buf_len = 0;
   if(!tag) return -1;

   if(!tag->encoder_active) return -1;

	long nframe = tag->encoder_frames_recorded;	

   buf_len =	vj_avcodec_encode_frame( tag->encoder, nframe, tag->encoder_format, buffer, vj_avcodec_get_buf(tag->encoder), tag->encoder_max_size, pixel_format);
   if(buf_len <= 0 ) {
	veejay_msg(VEEJAY_MSG_ERROR, "unable to encode frame" );
   	return -1;
   }

   	if(tag->encoder_file ) {
		if(lav_write_frame(tag->encoder_file, vj_avcodec_get_buf(tag->encoder), buf_len,1))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "writing frame, giving up :[%s]", lav_strerror());
				return -1;
		}
	
		if(audio_size > 0)
		{
			if(lav_write_audio(tag->encoder_file, abuff, audio_size))
			{
		 	    veejay_msg(VEEJAY_MSG_ERROR, "Error writing output audio [%s]",lav_strerror());
			}
		}
	}
	/* write OK */
	tag->encoder_frames_recorded ++;
	tag->encoder_total_frames_recorded ++;


	return (vj_tag_continue_record(t1));
}

int vj_tag_get_audio_frame(int t1, uint8_t *dst_buffer)
{
	return 0;    
}


/* ccd image calibration
 */

static uint8_t	*blackframe_new( int w, int h, int uv_len, uint8_t *Y, uint8_t *U, uint8_t *V, int median_radius, vj_tag *tag ) {
	uint8_t *buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * 5 * ((w*h) + 2 * uv_len ));
	if(buf == NULL) {
		veejay_msg(0,"Insufficient memory to initialize calibration.");
		return NULL;
	}
	veejay_memset( buf, 0, sizeof(uint8_t) * 5 * ((w*h)+2*uv_len));
	tag->blackframe = buf;
	const int chroma=127;

	tag->lf = (double*) vj_malloc(sizeof(double) * (w*h));
	tag->lfu= (double*) vj_malloc(sizeof(double) *uv_len);
	tag->lfv= (double*) vj_malloc(sizeof(double) *uv_len);

	tag->bf = (double*) vj_malloc(sizeof(double) * (w*h));
	tag->bfu= (double*) vj_malloc(sizeof(double) * uv_len);
	tag->bfv= (double*) vj_malloc(sizeof(double) * uv_len);

	if(median_radius== 0 ) {
		int i;
		for(i = 0; i < (w*h); i ++ ) {
			tag->lf[i] = 0.0f;
			tag->bf[i] = 0.0f + (double) Y[i];
		}
		for(i = 0; i < uv_len; i ++ ) {
			tag->lfu[i] = 0.0f;
			tag->lfv[i] = 0.0f;
			tag->bfu[i] = 0.0f + (double) (U[i] - chroma);
			tag->bfv[i] = 0.0f + (double) (V[i] - chroma);
		}

	} else {
		uint8_t *ptr = cali_get(tag,CALI_BUF,w*h,uv_len);
		ctmf( Y, ptr, w,h,w,w,median_radius,1,512*1024);
		ctmf( U, ptr + (w*h),w/2,h,w/2,w/2,median_radius,1,512*1024);
		ctmf( V, ptr + (w*h)+uv_len,w/2,h,w/2,w/2,median_radius,1,512*1024);
		int i;
		for(i = 0; i < (w*h); i ++ ) {
			tag->lf[i] = 0.0f;
			tag->bf[i] = 0.0f + (double) ptr[i];
		}
		uint8_t *ptru = ptr + (w*h);
		uint8_t *ptrv = ptru + uv_len;
		for(i = 0; i < uv_len; i ++ ) {
			tag->lfu[i] = 0.0f;
			tag->lfv[i] = 0.0f;
			tag->bfu[i] = 0.0f + (double) (ptru[i] - chroma);
			tag->bfv[i] = 0.0f + (double) (ptrv[i] - chroma);
		}

	}

	return buf;
}

static void	blackframe_process(  uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int uv_len, int median_radius, vj_tag *tag )
{
	int i;
	uint8_t *bf = cali_get(tag,CALI_DARK,w*h,uv_len);
	const int chroma = 127;
	double *blackframe = tag->bf;
	double *blackframeu= tag->bfu;
	double *blackframev= tag->bfv;

	//@ YUV = input frame
	if( median_radius > 0 ) {
		bf = cali_get(tag,CALI_BUF,w*h,uv_len);
	}
	
	uint8_t *bu = bf + (w*h);
	uint8_t *bv = bu + uv_len;

	uint8_t *srcY = Y;
	uint8_t *srcU = U;
	uint8_t *srcV = V;

	if( median_radius > 0 ) {
		ctmf( Y, bf, w,h,w,w,median_radius,1,512*1024);
		ctmf( U, bu, w/2,h,w/2,w/2,median_radius,1,512*1024);
		ctmf( V, bv, w/2,h,w/2,w/2,median_radius,1,512*1024);
		srcY = bf;
		srcU = bu;
		srcV = bv;
	}
	
	for( i = 0; i < (w*h); i ++ ) {
		blackframe[i] += srcY[i];
	}
	for( i =0 ; i < uv_len; i ++ ) {
		blackframeu[i] += (double) ( srcU[i] - chroma );
		blackframev[i] += (double) ( srcV[i] - chroma );
	}
}
static	void	whiteframe_new(uint8_t *buf, int w, int h, int uv_len, uint8_t *Y, uint8_t *U, uint8_t *V, int median_radius, vj_tag *tag ) {
	int i;
	uint8_t *bf = cali_get( tag,CALI_DARK,w*h,uv_len);
	uint8_t *bu = bf + (w*h);
	uint8_t *bv = bu + uv_len;
	int p;
	const int chroma = 127;

	double mean_of_y = 0.0;
	double mean_of_u = 0.0f;
	double mean_of_v = 0.0f;


	if(median_radius > 0 ) {
		uint8_t *ptr = cali_get(tag,CALI_BUF,w*h,uv_len);
		ctmf( Y, ptr, w,h,w,w,median_radius,1,512*1024);
		ctmf( U, ptr + (w*h),w/2,h,w/2,w/2,median_radius,1,512*1024);
		ctmf( V, ptr + (w*h)+uv_len,w/2,h,w/2,w/2,median_radius,1,512*1024);
		int i;
		for(i = 0; i < (w*h); i ++ ) {
			tag->lf[i] = 0.0f + (double) ptr[i] - bf[i];
			mean_of_y += tag->lf[i];
		}
		uint8_t *ptru = ptr + (w*h);
		uint8_t *ptrv = ptru + uv_len;
		for(i = 0; i < uv_len; i ++ ) {
			tag->lfu[i] = 0.0f + (double) (ptru[i] - chroma) - (bu[i]-chroma);
			mean_of_u += tag->lfu[i];
			tag->lfv[i] = 0.0f + (double) (ptrv[i] - chroma) - (bv[i]-chroma);
			mean_of_v += tag->lfv[i];
		}
	} else {
		for(i = 0; i < (w*h); i ++ ) { //@TODO subtract dark current
			p = Y[i] - bf[i];
			if( p < 0 ) p = 0;
			tag->lf[i] = 0.0f + (double)p;
			mean_of_y += p;
		}
		for(i = 0; i < uv_len; i ++ ) {
			p = (  (U[i]-chroma) - (bu[i]-chroma));
			tag->lfu[i] = 0.0f + (double)p;
			mean_of_u += p;
			p = ( (V[i]-chroma) - (bv[i]-chroma));
			tag->lfv[i] = 0.0f + (double) p;
			mean_of_v += p;
		}

	}

	mean_of_y = mean_of_y / (w*h);

	tag->tabmean[0][ tag->bf_count - 1 ] = mean_of_y;

	mean_of_u = mean_of_u / uv_len;
	mean_of_v = mean_of_v / uv_len;

}

static void	whiteframe_process( uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int uv_len, int median_radius, vj_tag *tag)
{
	int i;
	const int chroma = 127;

	double *lightframe = tag->lf;
	double *lightframe_u = tag->lfu;
	double *lightframe_v = tag->lfv;
	
	uint8_t *bf = cali_get( tag,CALI_DARK,w*h,uv_len);
	uint8_t *bu = bf + (w*h);
	uint8_t *bv = bu + uv_len;
	double mean_of_y = 0.0;
	double mean_of_u = 0.0f;
	double mean_of_v = 0.0f;


	//@ YUV = input frame
	if( median_radius > 0 ) {
		uint8_t *dbf = cali_get( tag,CALI_BUF,w*h,uv_len );
		uint8_t *dbu = dbf + (w*h);
		uint8_t *dbv = dbu + uv_len;
		ctmf( Y,  dbf, w,h,w,w,median_radius,1,512*1024);
		for( i = 0; i < (w*h); i ++ ) {
			lightframe[i] += (double)(dbf[i] - bf[i]);
			mean_of_y += lightframe[i];
		}
		ctmf( U, dbu, w/2,h,w/2,w/2,median_radius,1,512*1024);
		ctmf( V, dbv, w/2,h,w/2,w/2,median_radius,1,512*1024);
		for( i =0 ; i < uv_len; i ++ ) {
			lightframe_u[i] += (double) ( dbu[i]-chroma ) - ( bu[i]-chroma);
			mean_of_u += lightframe_u[i];
			lightframe_v[i] += (double) ( dbv[i]-chroma ) - ( bv[i]-chroma);
			mean_of_v += lightframe_v[i];
		}

	} else {
		int p;
		//@ should subtract dark current, TODO
		for( i = 0; i < (w*h); i ++ ) {
			p = Y[i] - bf[i];
			if( p < 0 )
				p = 0;
			lightframe[i] += (double) p;
			mean_of_y += p;
		}
		for( i =0 ; i < uv_len; i ++ ) {
			p = ((U[i]-chroma)-(bu[i]-chroma));
			lightframe_u[i] += (double) p;
			mean_of_u += p;
			p = ((V[i]-chroma)-(bv[i]-chroma));
			lightframe_v[i] += (double) p;
			mean_of_v += p;
		}
	}
	
	mean_of_y = mean_of_y / (w*h);
	mean_of_u = mean_of_u / uv_len;
	mean_of_v = mean_of_v / uv_len;
	tag->tabmean[0][ tag->bf_count - 1 ] = mean_of_y;
}

static	void	master_lightframe(int w,int h, int uv_len, vj_tag *tag)
{
	int i;
	int duration =tag->cali_duration -1;
	uint8_t *bf = cali_get(tag,CALI_LIGHT,w*h,uv_len);
	uint8_t *bu = bf + (w*h);
	uint8_t *bv = bu + uv_len;
	
	int len = w*h;
	
	double  *sY = tag->lf;
	double  *sU = tag->lfu;
	double  *sV = tag->lfv;
	const int chroma = 127;	
	

	double sum = 0.0;

	for( i = 0; i < len; i ++ ) {
		if( sY[i] <= 0 )
			bf[i] = 0;
		else
		{
			bf[i] = (uint8_t) ( sY[i] / duration );
		}
		sum += bf[i];
	}

	sum = sum / len;

	for( i = 0; i <uv_len; i ++ ) {
		if( sU[i] <= 0 )
			bu[i] = chroma;
		else 
		{
			bu[i] =  (uint8_t) chroma + (  sU[i] / duration );
		}
		if( sV[i] <= 0 )
			bv[i] = chroma;
		else 
		{
			bv[i] = (uint8_t) chroma + (  sV[i] / duration );
		}
	}


}
static	void	master_blackframe(int w, int h, int uv_len, vj_tag *tag )
{
	int i;
	int duration =tag->cali_duration - 1;
	uint8_t *bf = cali_get(tag,CALI_DARK,w*h,uv_len);
	uint8_t *bu = bf + (w*h);
	uint8_t *bv = bu + uv_len;
	int len = w*h;
	double  *sY = tag->bf;
	double  *sU = tag->bfu;
	double  *sV = tag->bfv;
	const int chroma = 127;	
	for( i = 0; i < len; i ++ ) {
		if( sY[i] <= 0 )
			bf[i] = 0;
		else
		{
			bf[i] = (uint8_t) ( sY[i] / duration );
		}
	}

	for( i = 0; i <uv_len; i ++ ) {
		if( sU[i] <= 0 )
			bu[i] = chroma;
		else 
		{
			bu[i] = (uint8_t) chroma + ( sU[i] / duration );
		}
		if( sV[i] <= 0 )
			bv[i] = chroma;
		else 
		{
			bv[i] = (uint8_t) chroma + ( sV[i] / duration );
		}
	}

}

static	void	master_flatframe(int w, int h, int uv_len,vj_tag *tag )
{
	int i;
	uint8_t *wy = cali_get(tag,CALI_LIGHT,w*h,uv_len);
	uint8_t *wu = wy + (w*h);
	uint8_t *wv = wu + uv_len;

	uint8_t *by = cali_get(tag,CALI_DARK,w*h,uv_len);
	uint8_t *bu = wy + (w*h);
	uint8_t *bv = wu + uv_len;

	uint8_t *my = cali_get(tag,CALI_FLAT,w*h,uv_len);
	uint8_t *mu = my + (w*h);
	uint8_t *mv = mu + uv_len;

	uint8_t *sy = cali_get(tag,CALI_MFLAT,w*h,uv_len);
	uint8_t *su = my + (w*h);
	uint8_t *sv = mu + uv_len;

	const int chroma = 127;	
	double sum = 0;
	double sum_u=0,sum_v=0;

	const int len = (w*h);
	for( i = 0; i < len; i ++ ) {
		sy[i] = wy[i] - by[i];
		my[i] = sy[i];
		sum += wy[i];
	}
	for( i = 0; i < uv_len; i ++ ) {
		su[i] = ( chroma + (  (wu[i]-chroma) - (bu[i]-chroma)));
		sv[i] = ( chroma + (  (wv[i]-chroma) - (bv[i]-chroma)));
		mu[i] = su[i];
		mv[i] = sv[i];
		sum_u += wu[i];
		sum_v += wv[i];
	}
	double mean_u = ( sum_u / uv_len);
	double mean_v = ( sum_v / uv_len);
	double mean_y = ( sum / (w*h));		
	
	//@ store
	tag->mean[0] = mean_y;
	tag->mean[1] = mean_u;
	tag->mean[2] = mean_v;	

}

static void	blackframe_subtract( vj_tag *tag, uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int uv_len,int use_light,const double mean_y,const double mean_u,const double mean_v )
{
	int i;
	uint8_t *bf = cali_get(tag, CALI_DARK,w*h,uv_len);
	uint8_t *wy = cali_get(tag, CALI_FLAT,w*h,uv_len);
	uint8_t *wu = wy + (w*h);
	uint8_t *wv = wu + uv_len;
	uint8_t *bu = bf + (w*h);
	uint8_t *bv = bu + uv_len;
	const int chroma = 127;	

	int d=0;
	double p = 0.0;
	const double dmean_y = (double) mean_y;
	const double dmean_u = (double) mean_u;
	const double dmean_v = (double) mean_v;
	//@ process master flat image

	if( use_light ) {
		for( i = 0; i <(w*h); i ++ ) {
			p = (double) wy[i] /dmean_y;
			if( p != 0.0 )
				d = (int) ( ( (double)Y[i] - bf[i]) / p);
			else
				d = 0.0f; //Y[i] - bf[i];
			
			if( d < 0 ) d = 0; else if ( d > 255 ) d = 255;

			Y[i] = (uint8_t) d;

		}
		
		int d1=0;
		for( i =0 ; i < uv_len; i ++ )
		{
			p = (double) wu[i] / dmean_u;
			d1 = (double) ( chroma + ((U[i] - chroma) - (bu[i]-chroma)));
			if( d1 == 0 )
				d = chroma;
			else
				d  = (int) ( d1 / p );
	
			if ( d < 0 ) { d = 0; } else if ( d > 255 ) { d = 255;}	
		
			U[i] = (uint8_t) d;
		
			p = ( wv[i] / dmean_v );
			d1 = ( double) (chroma + ((V[i] - chroma) - (bv[i]-chroma)));
			if( d1 == 0 )
				d= chroma;
			else
				d = (int) ( d1 / p );
			if( d < 0 ) { d = 0; } else if ( d > 255 ) { d= 255; }
			V[i] = (uint8_t) d;
		}

	} else {
		//@ just show result of frame - dark current
		for( i = 0; i <(w*h); i ++ ) {
			p = ( Y[i] - bf[i] );
			if( p < 0 )
				Y[i] = 0;
			else
				Y[i] = p;
		}
		for( i = 0; i < uv_len; i ++ ) {
			p = U[i] - bu[i];
			if( p < 0 )
				U[i] = chroma;
			else
				U[i] = p;

			p = V[i] - bv[i];
			if( p < 0 )
				V[i] = chroma;
			else
				V[i] = p;
		}
	}




}

#define V4L_WHITEFRAME 4
#define V4L_WHITEFRAME_NEXT 5
#define V4L_WHITEFRAME_PROCESS 6
int vj_tag_grab_blackframe(int t1, int duration, int median_radius , int mode) 
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
	    return 0;
    if( tag->source_type != VJ_TAG_TYPE_V4L ) {
	veejay_msg(VEEJAY_MSG_INFO, "Source is not a video device.");
	return 0;
    }
    
    if( duration <= 0 )
	    return 0;

    if( median_radius <= 0 ) {
	    median_radius = 0;
    }

    veejay_msg(VEEJAY_MSG_INFO, "Creating %s (%d frames) median=%d",(mode==0?"Blackframe":"Lightframe"),duration, median_radius );    

    tag->noise_suppression = (mode == 0 ? V4L_BLACKFRAME : V4L_WHITEFRAME );
    tag->median_radius     = median_radius;
    tag->bf_count	   = duration;
    tag->has_white	   = (mode == 1 ? 1 :0);
    return 1;
}
int vj_tag_drop_blackframe(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
	return 0;
    if( tag->source_type != VJ_TAG_TYPE_V4L ) {
	veejay_msg(VEEJAY_MSG_INFO, "Source is not a video device.");
	return 0;
    }
    if( tag->blackframe ) {
	tag->noise_suppression = -1;
	veejay_msg(VEEJAY_MSG_INFO, "Black Frame dropped.");
    }

    return 1;
}



int vj_tag_get_frame(int t1, uint8_t *buffer[3], uint8_t * abuffer)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
	    return -1;

    const int width = vj_tag_input->width;
    const int height = vj_tag_input->height;
    const int uv_len = vj_tag_input->uv_len;
    const int len = (width * height);

    
	switch (tag->source_type)
	{
	case VJ_TAG_TYPE_V4L:
		if( tag->capture_type == 1 ) {
			int res = 0;
#ifdef HAVE_V4L
			res = v4lvideo_copy_framebuffer_to(vj_tag_input->unicap[tag->index],buffer[0],buffer[1],buffer[2]);
#endif

#ifdef HAVE_V4L2
			if( no_v4l2_threads_ ) {
			 res = v4l2_pull_frame( vj_tag_input->unicap[tag->index],v4l2_get_dst(vj_tag_input->unicap[tag->index],buffer[0],buffer[1],buffer[2]) );
			} else {
			 res = v4l2_thread_pull( v4l2_thread_info_get( vj_tag_input->unicap[tag->index]),
						v4l2_get_dst( vj_tag_input->unicap[tag->index], buffer[0],buffer[1],buffer[2]));
			}
#endif
			if( res <= 0 ) {
				veejay_memset( buffer[0], 0, len );
				veejay_memset( buffer[1], 128, uv_len );
				veejay_memset( buffer[2], 128, uv_len );
			}
		}
		switch( tag->noise_suppression ) {
			case V4L_BLACKFRAME:
				tag->cali_duration = tag->bf_count;
				blackframe_new(width,height,uv_len,buffer[0],buffer[1],buffer[2],tag->median_radius,tag);
				tag->tabmean[0]    = (double*) vj_malloc(sizeof(double)* tag->cali_duration);
				if(tag->blackframe == NULL ) {
					tag->noise_suppression = 0;
				} else {
					tag->bf_count --;
					tag->noise_suppression = V4L_BLACKFRAME_NEXT;
					if(tag->bf_count==0) {
						master_blackframe(width,height,uv_len,tag);
						tag->noise_suppression = V4L_BLACKFRAME_PROCESS;
						veejay_msg(VEEJAY_MSG_INFO, "Please create a lightframe (white)");
					}
				}
				//@ grab black frame
				break;
			case V4L_WHITEFRAME:
				tag->cali_duration = tag->bf_count;
				if(!tag->blackframe) {
					veejay_msg(0, "Please start with a black frame first (Put cap on lens)");
					tag->noise_suppression = 0;
					break;
				}
				whiteframe_new( tag->blackframe,width,height,uv_len,buffer[0],buffer[1],buffer[2],tag->median_radius, tag);
				tag->bf_count --;
				tag->noise_suppression = V4L_WHITEFRAME_NEXT;
				if(tag->bf_count <= 0 ) {
					tag->noise_suppression = V4L_BLACKFRAME_PROCESS;
					master_lightframe( width,height,uv_len,tag );
					master_flatframe( width,height,uv_len, tag );
					veejay_msg(VEEJAY_MSG_DEBUG, "Master flat frame.");
				}
				break;
			case V4L_WHITEFRAME_NEXT:
				whiteframe_process(buffer[0],buffer[1],buffer[2],width,height,uv_len,tag->median_radius,tag );
				tag->bf_count --;
				if( tag->bf_count <= 0 ) {
					tag->noise_suppression = V4L_BLACKFRAME_PROCESS;
					master_lightframe( width,height,uv_len,tag);	
					master_flatframe( width,height,uv_len, tag );
					veejay_msg(VEEJAY_MSG_DEBUG, "Master flat frame");
				} else {
					veejay_msg(VEEJAY_MSG_DEBUG, "Whiteframe %d",tag->bf_count );
				}
				break;
			case V4L_BLACKFRAME_NEXT:
				blackframe_process( buffer[0],buffer[1],buffer[2],width,height,uv_len, tag->median_radius,tag );
				if( tag->bf_count <= 0 ) {
					tag->noise_suppression = 0;
					master_blackframe(width,height,uv_len,tag);
					veejay_msg(VEEJAY_MSG_INFO, "Please create a lightframe.");
				} else {
				    veejay_msg(VEEJAY_MSG_DEBUG, "Blackframe %d", tag->bf_count );
				    tag->bf_count --;
				}
				break;
			case V4L_BLACKFRAME_PROCESS:
				blackframe_subtract( tag,buffer[0],buffer[1],buffer[2],width,height,uv_len, tag->has_white , tag->mean[0],tag->mean[1],tag->mean[2]);
				break;
			case -1:
				if( tag->blackframe ) {
					free(tag->blackframe);
					tag->blackframe = NULL;
					tag->noise_suppression = 0;
				}
				if( tag->bf ) free(tag->bf);
				if( tag->bfu ) free(tag->bfu);
				if( tag->bfv ) free(tag->bfv);
				if( tag->lf ) free(tag->lf);
				if( tag->lfu ) free(tag->lfu);
				if( tag->lfv ) free(tag->lfv);
				if( tag->tabmean[0]) free(tag->tabmean[0]);
				break;
				//@ process black frame
			default:	
				break;
		}
		if( tag->noise_suppression !=  0 && tag->noise_suppression != 6 && tag->noise_suppression != 3 )
			veejay_msg(VEEJAY_MSG_DEBUG, "Calibration step %d of %d", tag->bf_count, tag->cali_duration );

		return 1;
		break;
	case VJ_TAG_TYPE_CALI:
		{
			cali_tag_t *p = (cali_tag_t*)vj_tag_input->cali[tag->index];
			if(p) {
				veejay_memcpy(buffer[0], p->mf, len );
				veejay_memcpy(buffer[1], p->mf + len, uv_len );
				veejay_memcpy(buffer[2], p->mf + len  + uv_len, uv_len);
			}
		}	
		break;
#ifdef USE_GDK_PIXBUF
	case VJ_TAG_TYPE_PICTURE:
		{
			vj_picture *p = vj_tag_input->picture[tag->index];
			if(!p)
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Picture never opened");
				vj_tag_disable(t1);
				return -1;
			}
			VJFrame *pframe = vj_picture_get( p->pic );
			veejay_memcpy(buffer[0],pframe->data[0], len);
			veejay_memcpy(buffer[1],pframe->data[1], uv_len);
			veejay_memcpy(buffer[2],pframe->data[2], uv_len);
		}
		break;
#endif
	case VJ_TAG_TYPE_MCAST:
	case VJ_TAG_TYPE_NET:
		if(!net_thread_get_frame( tag,buffer ))
			return 0;
		return 1;
		break;
	case VJ_TAG_TYPE_YUV4MPEG:
		if(vj_yuv_get_frame(vj_tag_input->stream[tag->index], _temp_buffer) != 0)
		{
			vj_tag_set_active(t1,0);
			return -1;
		}
		
		yuv420to422planar( _temp_buffer, buffer, width,height );
		veejay_memcpy( buffer[0],_temp_buffer[0],width * height );
		return 1;
		
		break;
#ifdef SUPPORT_READ_DV2
	case VJ_TAG_TYPE_DV1394:
		vj_dv1394_read_frame( vj_tag_input->dv1394[tag->index], buffer , abuffer,vj_tag_input->pix_fmt);
		break;
#endif
	case VJ_TAG_TYPE_GENERATOR:
		_tmp.len     = len;
		_tmp.uv_len  = uv_len;
		_tmp.data[0] = buffer[0];
		_tmp.data[1] = buffer[1];
		_tmp.data[2] = buffer[2];
		_tmp.width   = width;
		_tmp.height  = height;
		_tmp.format  = PIX_FMT_YUVJ422P;
		if( tag->generator ) {
			plug_push_frame( tag->generator, 1, 0, &_tmp );
			plug_set_parameter( tag->generator, 0,1,&(tag->color_r) );
			plug_set_parameter( tag->generator, 1,1,&(tag->color_g) );
			plug_set_parameter( tag->generator, 2,1,&(tag->color_b) );
			plug_process( tag->generator, -1.0 );
		}
		break;
	case VJ_TAG_TYPE_COLOR:
		_tmp.len     = len;
		_tmp.uv_len  = uv_len;
		_tmp.data[0] = buffer[0];
		_tmp.data[1] = buffer[1];
		_tmp.data[2] = buffer[2];
		_tmp.width   = width;
		_tmp.height  = height;
		_tmp.format  = PIX_FMT_YUVJ422P;
		dummy_rgb_apply( &_tmp, width, height, tag->color_r,tag->color_g,tag->color_b );
		break;

    case VJ_TAG_TYPE_NONE:
		break;
		default:
		break;
    	}
    	return 1;
}


//int vj_tag_sprint_status(int tag_id, int entry, int changed, char *str)
int vj_tag_sprint_status( int tag_id,int cache,int sa, int ca, int pfps,int frame,int mode,int ts,int curfps, uint32_t lo, uint32_t hi, int macro, char *str )
{
    vj_tag *tag;
    tag = vj_tag_get(tag_id);

	char *ptr = str;
	ptr = vj_sprintf( ptr, pfps ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, frame ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, mode ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, tag_id ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, tag->effect_toggle ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, tag->color_r ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, tag->color_g ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, tag->color_b ); *ptr++ = ' ';
	*ptr++ = '0'; *ptr++ = ' ';
	ptr = vj_sprintf( ptr, tag->encoder_active ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, tag->encoder_frames_to_record ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, tag->encoder_total_frames_recorded ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, vj_tag_size() - 1 ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, tag->source_type ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, tag->n_frames ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, tag->selected_entry ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, ts ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, cache ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, curfps ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, (int) lo ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, (int) hi ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, sa ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, ca ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, (int) tag->fader_val ); *ptr++ = ' ';
	*ptr++ = '0'; *ptr++ = ' ';
	ptr = vj_sprintf( ptr, macro );

    return 0;
}

#ifdef HAVE_XML2
static void tagParseArguments(xmlDocPtr doc, xmlNodePtr cur, int *arg)
{
    xmlChar *xmlTemp = NULL;
    char *chTemp = NULL;
    int argIndex = 0;
    if (cur == NULL)
	return;

    while (cur != NULL && argIndex < SAMPLE_MAX_PARAMETERS) {
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_ARGUMENT))
	{
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		arg[argIndex] = atoi(chTemp);
		argIndex++;
	    }

	    if (xmlTemp)
	   	 xmlFree(xmlTemp);
   	    if (chTemp)
	    	free(chTemp);
	
	}
	// xmlTemp and chTemp should be freed after use
	xmlTemp = NULL;
	chTemp = NULL;
	cur = cur->next;
    }
}
static	int	tagParseKeys( xmlDocPtr doc, xmlNodePtr cur, void *port )
{
	if(!cur)
		return 0;

	while (cur != NULL)
	{
		if( !xmlStrcmp( cur->name, (const xmlChar*) "KEYFRAMES" ))
		{
			keyframe_xml_unpack( doc, cur->xmlChildrenNode, port );
		}
		cur = cur->next;
    	}
	return 1;
}

static void tagParseEffect(xmlDocPtr doc, xmlNodePtr cur, int dst_sample)
{
    xmlChar *xmlTemp = NULL;
    char *chTemp = NULL;
    int effect_id = -1;
    int arg[SAMPLE_MAX_PARAMETERS];
    int source_type = 0;
    int channel = 0;
    int frame_trimmer = 0;
    int frame_offset = 0;
    int e_flag = 0;
    int anim= 0;
    int anim_type = 0;
    int chain_index = 0;
	int a_flag = 0;
	int volume = 0;

	veejay_memset( arg, 0, sizeof(arg));

    if (cur == NULL)
		return;

    xmlNodePtr curarg = cur;

    while (cur != NULL) {
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTID)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		effect_id = atoi(chTemp);
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}

	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTPOS)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		chain_index = atoi(chTemp);
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}

	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTSOURCE)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		source_type = atoi(chTemp);
		free(chTemp);
	    }
            if(xmlTemp) xmlFree(xmlTemp);
	}

	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTCHANNEL)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		channel = atoi(chTemp);
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}

	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTTRIMMER)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		frame_trimmer = atoi(chTemp);
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}

	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTOFFSET)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		frame_offset = atoi(chTemp);
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}

	if (!xmlStrcmp(cur->name, (const xmlChar *) "kf_status")) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		anim = atoi(chTemp);
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) "kf_type")) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		anim_type = atoi(chTemp);
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}

	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTACTIVE)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		e_flag = atoi(chTemp);
		free(chTemp);
	    } 
	    if(xmlTemp) xmlFree(xmlTemp);
	
	}

	if (!xmlStrcmp
	    (cur->name, (const xmlChar *) XMLTAG_EFFECTAUDIOFLAG)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
			a_flag = atoi(chTemp);
			free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	
	}

	if (!xmlStrcmp
	    (cur->name, (const xmlChar *) XMLTAG_EFFECTAUDIOVOLUME)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		volume = atoi(chTemp);
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_ARGUMENTS)) {
		    tagParseArguments(doc, cur->xmlChildrenNode, arg );
		}

	// xmlTemp and chTemp should be freed after use
	xmlTemp = NULL;
	chTemp = NULL;
	cur = cur->next;
    }

    if (effect_id != -1) {
		int j;
		int res = vj_tag_set_effect( dst_sample, chain_index, effect_id );

		if(res < 0 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Error parsing effect %d (pos %d) to stream %d",
			    effect_id, chain_index, dst_sample);
		}
		else {

			/* load the parameter values */
			for (j = 0; j < vj_effect_get_num_params(effect_id); j++) {
			    vj_tag_set_effect_arg(dst_sample, chain_index, j, arg[j]);
			}
			vj_tag_set_chain_channel(dst_sample, chain_index, channel);
			vj_tag_set_chain_source(dst_sample, chain_index, source_type);
	
			vj_tag_set_chain_status(dst_sample, chain_index, e_flag);
	
			vj_tag_set_offset(dst_sample, chain_index, frame_offset);
			vj_tag_set_trimmer(dst_sample, chain_index, frame_trimmer);
	
			j = 0;
			vj_tag *t = vj_tag_get( dst_sample );
			while (curarg != NULL) 
			{
					if(!xmlStrcmp(curarg->name,(const xmlChar*)"ANIM"))
					{
						if(t->effect_chain[chain_index]->effect_id > 0)
						{
							if(tagParseKeys( doc, curarg->xmlChildrenNode, t->effect_chain[chain_index]->kf))
							{
								veejay_msg(VEEJAY_MSG_INFO, "Animating FX %d on entry %d (status=%d)", t->effect_chain[chain_index]->effect_id, j,anim);
								vj_tag_chain_set_kf_status(dst_sample, chain_index, anim );
								vj_tag_set_kf_type( dst_sample,chain_index,anim_type);
							}
							j++;
						}
					}
					curarg = curarg->next;
			}

		}
	}

}

/*************************************************************************************************
 *
 * ParseEffect()
 *
 * Parse the effects array 
 *
 ****************************************************************************************************/
static void tagParseEffects(xmlDocPtr doc, xmlNodePtr cur, int dst_stream)
{
    int effectIndex = 0;
    while (cur != NULL && effectIndex < SAMPLE_MAX_EFFECTS) {
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECT)) {
	    tagParseEffect(doc, cur->xmlChildrenNode, dst_stream);
		effectIndex++;
	}
	//effectIndex++;
	cur = cur->next;
    }
}

void	tagParseCalibration( xmlDocPtr doc, xmlNodePtr cur, int dst_sample , void *vp)
{
	vj_tag *t = vj_tag_get( dst_sample );
	void *tmp = viewport_load_xml( doc, cur, vp );
	if( tmp ) 
		t->viewport_config = tmp;
}

/*************************************************************************************************
 *
 * ParseSample()
 *
 * Parse a sample
 *
 ****************************************************************************************************/

static	int tag_get_int_xml( xmlDocPtr doc, xmlNodePtr cur, const xmlChar *key )
{
	xmlChar *xmlTemp = xmlNodeListGetString( doc, cur->xmlChildrenNode,1);
	char *chTemp = UTF8toLAT1( xmlTemp );
	int res = 0;
	if(chTemp)
	{
		res = atoi(chTemp);
		free(chTemp);
	}
	if(xmlTemp) xmlFree(xmlTemp);
	return res;
}

static	char *tag_get_char_xml( xmlDocPtr doc, xmlNodePtr cur, const xmlChar *key )
{
	xmlChar *xmlTemp = xmlNodeListGetString( doc, cur->xmlChildrenNode,1);
	char *chTemp = UTF8toLAT1( xmlTemp );
	if(xmlTemp) xmlFree(xmlTemp);
	return chTemp;
}

void tagParseStreamFX(char *sampleFile, xmlDocPtr doc, xmlNodePtr cur, void *font, void *vp)
{
	int fx_on=0, id=0, source_id=0, source_type=0;
	char *source_file = NULL;
	char *extra_data = NULL;
	int col[3] = {0,0,0};
	int fader_active=0, fader_val=0, fader_dir=0, opacity=0, nframes=0;
	int subrender = 0;
	xmlNodePtr fx[32];
	veejay_memset( fx, 0, sizeof(fx));
	int k = 0;

	xmlNodePtr subs = NULL;
	xmlNodePtr cali = NULL;
	void *d = vj_font_get_dict( font );
	void *viewport_config = NULL;

	while (cur != NULL)
	{
		if( !xmlStrcmp(cur->name, (const xmlChar*) XMLTAG_SAMPLEID ))
			id = tag_get_int_xml(doc,cur,(const xmlChar*) XMLTAG_SAMPLEID );
		if( !xmlStrcmp(cur->name, (const xmlChar*) "source_id" ) )
			source_id = tag_get_int_xml(doc,cur,(const xmlChar*) "source_id" );
		if( !xmlStrcmp(cur->name, (const xmlChar*) "source_type" ) )
			source_type = tag_get_int_xml(doc,cur,(const xmlChar*) "source_type" );
		if( !xmlStrcmp(cur->name, (const xmlChar*) "source_file" ) )
			source_file = tag_get_char_xml(doc,cur,(const xmlChar*) "source_file");
		if( !xmlStrcmp(cur->name, (const xmlChar*) "extra_data" ))
			extra_data = tag_get_char_xml(doc,cur, (const xmlChar*)"extra_data");

		if(! xmlStrcmp(cur->name, (const xmlChar*) "red" ) )
			col[0] = tag_get_int_xml( doc,cur, (const xmlChar*)"red" );
		if(! xmlStrcmp(cur->name, (const xmlChar*) "green" ) )
			col[1] = tag_get_int_xml( doc, cur, (const xmlChar*)"green" );
		if(! xmlStrcmp(cur->name, (const xmlChar*) "blue" ))
			col[2] = tag_get_int_xml( doc, cur, (const xmlChar*) "blue" );
		if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_CHAIN_ENABLED))
			fx_on = tag_get_int_xml(doc,cur, (const xmlChar*) XMLTAG_CHAIN_ENABLED );
		if (!xmlStrcmp(cur->name,(const xmlChar *) XMLTAG_FADER_ACTIVE)) 
			fader_active = tag_get_int_xml(doc,cur, (const xmlChar*) XMLTAG_FADER_ACTIVE);
		if (!xmlStrcmp(cur->name,(const xmlChar *) XMLTAG_FADER_VAL)) 
			fader_val = tag_get_int_xml( doc,cur, (const xmlChar*) XMLTAG_FADER_VAL );
		if (!xmlStrcmp(cur->name,(const xmlChar*) XMLTAG_FADER_DIRECTION)) 
			fader_dir = tag_get_int_xml( doc, cur, (const xmlChar*) XMLTAG_FADER_DIRECTION );
		if (!xmlStrcmp(cur->name,(const xmlChar*) "opacity" ) )
			opacity   = tag_get_int_xml( doc, cur, (const xmlChar*) "opacity");
		if (!xmlStrcmp(cur->name,(const xmlChar*) "nframes" ) )
			nframes   = tag_get_int_xml(doc, cur, (const xmlChar*) "nframes" );

		if (!xmlStrcmp(cur->name, (const xmlChar*) "SUBTITLES" ))
			subs = cur->xmlChildrenNode;

		if (!xmlStrcmp(cur->name, (const xmlChar*) "calibration" ))
			cali = cur->xmlChildrenNode;
		if (!xmlStrcmp(cur->name, (const xmlChar*) "subrender" ))
			subrender = tag_get_int_xml(doc,cur,(const xmlChar*) "subrender" );

		if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTS)) {
			fx[k] = cur->xmlChildrenNode;
			k++;
		}

		cur = cur->next;
    	}

	if( id > 0 )
	{
		int zer = 0;

		if( source_type == VJ_TAG_TYPE_V4L && extra_data )	
			sscanf( extra_data, "%d",&zer );	

		vj_tag_del( id );

		int n_id = vj_tag_new( source_type, source_file, _tag_info->nstreams,_tag_info->current_edit_list,
				_tag_info->pixel_format, source_id,zer, _tag_info->settings->composite );

		if(n_id > 0 )
		{
			vj_tag *tag = vj_tag_get( n_id );
			tag->id = id;
			tag->effect_toggle = fx_on;
			tag->fader_active = fader_active;
			tag->fader_val = fader_val;
			tag->fader_direction = fader_dir;
			tag->opacity = opacity;
			tag->nframes = nframes;
			tag->viewport_config = viewport_config;
		
			switch( source_type )
			{
				case VJ_TAG_TYPE_COLOR:
					vj_tag_set_stream_color( id, col[0],col[1],col[2] );
					break;
			}

			if( subs )
			{
				char tmp[512];
				sprintf(tmp, "%s-SUB-s%d.srt", sampleFile, id );
				vj_font_set_dict( font, tag->dict );

				vj_font_load_srt( font, tmp );

				vj_font_xml_unpack( doc,subs, font );
			}

			if( cali ) 
			{
				tagParseCalibration( doc, cali, id, vp );
			}

			int q;
			for( q = 0; q < k ; q ++ )
			{
				if(fx[q] )
					tagParseEffects(doc, fx[q], id );
			}
		}
	}

	vj_font_set_dict( font, d );

}



static void tagCreateArguments(xmlNodePtr node, int *arg, int argcount)
{
    int i;
    char buffer[100];
    for (i = 0; i < argcount; i++) {
	    sprintf(buffer, "%d", arg[i]);
		xmlNewChild( node, NULL, (const xmlChar*) XMLTAG_ARGUMENT,
				(const xmlChar*) buffer );
    }
}

static void tagCreateKeys(xmlNodePtr node, int argcount, void *port )
{
	int i;
	for ( i = 0; i < argcount ; i ++ )
	{
		xmlNodePtr childnode = xmlNewChild( node, NULL,(const xmlChar*) "KEYFRAMES",NULL );
		keyframe_xml_pack( childnode, port, i );
	}
}
	
static void tagCreateEffect(xmlNodePtr node, sample_eff_chain * effect, int position)
{
    char buffer[100];
    xmlNodePtr childnode;

    sprintf(buffer, "%d", position);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTPOS,
		(const xmlChar *) buffer);

    sprintf(buffer, "%d", effect->effect_id);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTID,
		(const xmlChar *) buffer);

    sprintf(buffer, "%d", effect->e_flag);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTACTIVE,
		(const xmlChar *) buffer);

    sprintf(buffer, "%d", effect->source_type);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTSOURCE,
		(const xmlChar *) buffer);

    sprintf(buffer, "%d", effect->channel);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTCHANNEL,
		(const xmlChar *) buffer);

    sprintf(buffer, "%d", effect->frame_offset);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTOFFSET,
		(const xmlChar *) buffer);

    sprintf(buffer, "%d", effect->frame_trimmer);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTTRIMMER,
		(const xmlChar *) buffer);

    sprintf(buffer, "%d", effect->a_flag);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTAUDIOFLAG,
		(const xmlChar *) buffer);

    sprintf(buffer, "%d", effect->volume);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTAUDIOVOLUME,
		(const xmlChar *) buffer);

	sprintf(buffer, "%d", effect->kf_status );
	xmlNewChild(node,NULL,(const xmlChar*) "kf_status", (const xmlChar*)buffer);

	sprintf(buffer, "%d", effect->kf_type );
	xmlNewChild(node,NULL,(const xmlChar*) "kf_type", (const xmlChar*)buffer);



    childnode =
	xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_ARGUMENTS, NULL);
    tagCreateArguments(childnode, effect->arg,
		    vj_effect_get_num_params(effect->effect_id));

    childnode =
	xmlNewChild(node, NULL, (const xmlChar*) "ANIM", NULL );
	tagCreateKeys( childnode, vj_effect_get_num_params(effect->effect_id), effect->kf ); 
}

static void tagCreateEffects(xmlNodePtr node, sample_eff_chain ** effects)
{
    int i;
    xmlNodePtr childnode;

    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
	if (effects[i]->effect_id != -1) {
	    childnode =
		xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECT,
			    NULL);
	    tagCreateEffect(childnode, effects[i], i);
	}
    }
    
}
    
void tagCreateStream(xmlNodePtr node, vj_tag *tag, void *font, void *vp)
{   
    char buffer[100];

    sprintf(buffer, "%d", tag->id);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_SAMPLEID,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", tag->effect_toggle);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_CHAIN_ENABLED,
		(const xmlChar *) buffer);

	sprintf(buffer, "%d", tag->source_type );
	xmlNewChild(node,NULL,(const xmlChar*) "source_type", (const xmlChar*) buffer );

	sprintf(buffer, "%d", tag->video_channel );
	xmlNewChild(node,NULL,(const xmlChar*) "source_id", (const xmlChar*) buffer );

	sprintf(buffer, "%s", tag->source_name );
	xmlNewChild(node,NULL,(const xmlChar*) "source_file", (const xmlChar*) buffer );

	sprintf(buffer, "%d" ,tag->subrender);
	xmlNewChild(node,NULL,(const xmlChar*) "subrender", (const xmlChar*) buffer );

	if(tag->extra )
	{
		sprintf(buffer, "%s", (char*)tag->extra );
		xmlNewChild(node, NULL,(const xmlChar*) "extra_data", (const xmlChar*) buffer );
	}
	sprintf(buffer, "%d", tag->color_r );
	xmlNewChild(node,NULL,(const xmlChar*) "red", (const xmlChar*) buffer );
	sprintf(buffer, "%d", tag->color_g );
	xmlNewChild(node,NULL,(const xmlChar*) "green", (const xmlChar*) buffer );
	sprintf(buffer, "%d", tag->color_b );
	xmlNewChild(node,NULL,(const xmlChar*) "blue", (const xmlChar*) buffer );
	
    	sprintf(buffer, "%d", tag->nframes );
	xmlNewChild(node, NULL, (const xmlChar*) "nframes", (const xmlChar*) buffer );

	sprintf(buffer, "%d", tag->opacity );
	xmlNewChild( node, NULL, (const xmlChar*) "opacity", (const xmlChar*) buffer );

	vj_font_xml_pack( node, font );

	viewport_save_xml( node, tag->viewport_config );

 	xmlNodePtr childnode =
		xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTS, NULL);

	tagCreateEffects(childnode, tag->effect_chain);

}

void	tag_writeStream( char *file, int n, xmlNodePtr node, void *font, void *vp )
{
	vj_tag *tag = vj_tag_get(n);
	if(!tag) {
		 veejay_msg(VEEJAY_MSG_ERROR, "Stream %d does not exist", n);
		 return;
	}
	char tmp[512];
	void *d = vj_font_get_dict( font );
	sprintf(tmp, "%s-SUB-s%d.srt", file,tag->id );

	if( tag->dict )
	{
		vj_font_set_dict( font, tag->dict );
		vj_font_save_srt( font, tmp );

		vj_font_set_dict( font, d );
	}

	tagCreateStream(node,  tag , font,vp);
}




#endif
