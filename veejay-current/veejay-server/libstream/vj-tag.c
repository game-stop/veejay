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
#include <stdint.h>
#include <veejaycore/defs.h>
#include <libstream/vj-tag.h>
#include <veejaycore/hash.h>
#include <libvje/vje.h>
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
#include <veejaycore/vj-msg.h>
#include <libel/vj-avcodec.h>
#include <veejaycore/vj-client.h>
#include <veejaycore/vims.h>
#include <veejay/vj-lib.h>
#include <veejay/vj-misc.h>
#include <veejaycore/vjmem.h>
#include <libvje/internal.h>
#include <libvje/ctmf/ctmf.h>
#include <libstream/vj-net.h>
#include <libstream/vj-avformat.h>
#include <pthread.h>
#ifdef HAVE_V4L2
#include <libstream/v4l2utils.h>
#endif
#include <veejaycore/libvevo.h>
#include <veejay/vj-misc.h>
#ifdef HAVE_FREETYPE
#include <veejay/vj-font.h>
#endif
#include <libvjxml/vj-xml.h>
#include <veejay/vj-macro.h>
#define SOURCE_NAME_LEN 255

#include <libplugger/plugload.h>
#include <libvje/libvje.h>
#include <libvje/effects/cali.h>

static int recount_hash = 1;
static unsigned int sample_count = 0;
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


extern int  frei0r_get_param_count( void *port);
extern void dummy_rgb_apply(VJFrame *frame, int r, int g, int b);


#define RUP8(num)(((num)+8)&~8)

typedef struct
{
    uint8_t *data;
    uint8_t *bf;
    uint8_t *lf;
    uint8_t *mf;
    int uv_len;
    int len;
    double  mean[3];
    void *ptr;
} cali_tag_t;

#define CALI_DARK 0 
#define CALI_LIGHT 1
#define CALI_FLAT 2
#define CALI_BUF 4
#define CALI_MFLAT 3

static  uint8_t *cali_get(vj_tag *tag, int type, int len, int uv_len ) {
    uint8_t *p = tag->blackframe;
    switch(type) {
        case CALI_DARK:
            return p;             //@ start of dark current
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

static uint8_t *_temp_buffer[4]={NULL,NULL,NULL,NULL};
static VJFrame _tmp;
void    vj_tag_free(void)
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

    if( TagHash ) {
        hash_destroy( TagHash );
        TagHash = NULL;
    }
}




int vj_tag_get_last_tag() {
    return last_added_tag;
}

int vj_tag_highest()
{
    return this_tag_id;
// - next_avail_tag);
}

int vj_tag_highest_valid_id()
{
    int id = this_tag_id;
    while(!vj_tag_exists(id) ) {
        id --;
        if( id <= 0 )
           break;
    }

    return id;
}

unsigned int vj_tag_size()
{
    if(recount_hash) {
        sample_count = (unsigned int) hash_count( TagHash );
        recount_hash = 0;
    }
    return sample_count;
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

    if( tag_cache[ id ] != NULL )
        return (vj_tag*) tag_cache[id];

    hnode_t *tag_node = hash_lookup(TagHash, (void *) tid);
    if (!tag_node) {
        return NULL;
    }
    tag_cache[ id ] = hnode_get(tag_node);
    
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

int vj_tag_num_devices()
{
#ifdef HAVE_V4L2
    return v4l2_num_devices();
#else
    return 0;
#endif
}

char *vj_tag_scan_devices( void )
{
    const char *default_str = "000000";
    int i;
    int len = 0;
    char **device_list = NULL;
#ifdef HAVE_V4L2
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

int vj_tag_get_width() {
    return vj_tag_input->width;
}
int vj_tag_get_height() {
    return vj_tag_input->height;
}
int vj_tag_get_uvlen() {
    return vj_tag_input->uv_len;
}

int vj_tag_init(int width, int height, int pix_fmt, int video_driver)
{
    TagHash = hash_create(HASHCOUNT_T_MAX, int_tag_compare, int_tag_hash);
    if (!TagHash || width <= 0 || height <= 0)
        return -1;
    
    vj_tag_input = (vj_tag_data *) vj_malloc(sizeof(vj_tag_data));

    if (vj_tag_input == NULL) {
        veejay_msg(VEEJAY_MSG_ERROR, "Error Allocating Memory for stream data\n");
        return -1;
    }

    int format = get_ffmpeg_pixfmt(pix_fmt);

    VJFrame *tmp = yuv_yuv_template( NULL,NULL,NULL, width,height, format );
    if( tmp == NULL ) {
        return -1;
    }

    vj_tag_input->width = tmp->width;
    vj_tag_input->height = tmp->height;
    vj_tag_input->depth = 3;
    vj_tag_input->pix_fmt = pix_fmt; 

    video_driver_ = video_driver;
    video_driver_ = 1;

    vj_tag_input->uv_len = tmp->uv_len;

    veejay_memset( &_tmp, 0, sizeof(VJFrame));
    
    veejay_memcpy( &_tmp, tmp, sizeof(VJFrame));
    
   _temp_buffer[0] = (uint8_t*) vj_calloc(sizeof(uint8_t)* tmp->len);
   _temp_buffer[1] = (uint8_t*) vj_calloc(sizeof(uint8_t)* tmp->len);
   _temp_buffer[2] = (uint8_t*) vj_calloc(sizeof(uint8_t)* tmp->len);
    veejay_memset( tag_cache,0,sizeof(tag_cache));
    veejay_memset( avail_tag, 0, sizeof(avail_tag));

    char *v4l2threading = getenv( "VEEJAY_V4L2_NO_THREADING" );
    if( v4l2threading ) {
        no_v4l2_threads_ = atoi(v4l2threading);
    }
    else {
        veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_V4L2_NO_THREADING=[0|1] not set");
    }

    free(tmp);

    return 0;
}

void vj_tag_record_init(int w, int h)
{
}

static int _vj_tag_new_clone(vj_tag *tag, int which_id )
{
    vj_tag *tag2 = vj_tag_get(which_id);
    if( tag2 == NULL ) {
        return 0;   
    }

    char tmp[128];
    snprintf(tmp,sizeof(tmp),"T%d", which_id );
    tag->extra = strdup(tmp);

    tag2->clone ++;

    tag->active = 1;
    tag->video_channel = which_id;
    return 1;
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

    snprintf(tmp,sizeof(tmp), "%s %d", host, port );
    tag->extra = (void*) strdup(tmp);

    return 1;
}

static int _vj_tag_new_v4l2( vj_tag * tag, int stream_nr, int width, int height, int device_num,
            char norm, int palette,int pixfmt, int freq, int channel, int has_composite, int driver)
{
    char refname[100];
    if (stream_nr < 0 || stream_nr > vj_tag_num_devices())
    {
        return 0;
    }
    
    snprintf(refname,sizeof(refname), "/dev/video%d",device_num ); // freq->device_num
#ifdef HAVE_V4L2
    if(  no_v4l2_threads_ ) {
        vj_tag_input->v4l2[stream_nr] = v4l2open( refname, channel, palette,width,height,_tag_info->dummy->fps,_tag_info->dummy->norm );
    } else {
        vj_tag_input->v4l2[stream_nr] = v4l2_thread_new( refname, channel,palette,width,height,_tag_info->dummy->fps,_tag_info->dummy->norm );
    }
    if( !vj_tag_input->v4l2[stream_nr] ) {
        veejay_msg(0, "Unable to open device %d (%s)",device_num, refname );
        return 0;
    }
    snprintf(refname,sizeof(refname), "%d", channel );
    tag->extra = strdup(refname);
    return 1;
#else
    return 0;
#endif
}

#ifdef USE_GDK_PIXBUF
static int _vj_tag_new_picture( vj_tag *tag, int stream_nr, int width, int height, float fps)
{
    if(stream_nr < 0 || stream_nr > VJ_TAG_MAX_STREAM_IN) return 0;
    vj_picture *p = NULL;

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

uint8_t     *vj_tag_get_cali_buffer(int t1, int type, int *total, int *plane, int *planeuv)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return NULL;
    
    int     w   =   vj_tag_input->width;
    int h   =   vj_tag_input->height;
    int len =   (w*h);
    int uv_len  =   vj_tag_input->uv_len;

    *total =    len + (2*uv_len);
    *plane =    len;
    *planeuv=   uv_len;
    return  cali_get(tag,type,w*h,uv_len);
}

static  int cali_write_file( char *file, vj_tag *tag , editlist *el)
{
    FILE *f = fopen( file, "w" );
    if(!f) {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to open '%s' for writing",file );
        return 0;
    }

    char header[256];
    int     w   =   vj_tag_input->width;
    int h   =   vj_tag_input->height;
    int len =   (w*h);
    int uv_len  =   vj_tag_input->uv_len;

    char fileheader[256];

    snprintf(header,sizeof(header),"%08d %08d %08d %08d %g %g %g",
            w,
            h,
            len,
            uv_len,
            tag->mean[0],
            tag->mean[1],
            tag->mean[2]    );

    int offset = 4 + strlen(header);

    snprintf(fileheader,sizeof(fileheader), "%03d %s",offset,header );

    if( fwrite( fileheader,strlen(fileheader),1, f ) <= 0 ) {
        veejay_msg(0 ,"Error while writing file header");
        return 0;
    }   
    int n = 0;

    //@ write dark current frame
    if( (n=fwrite( tag->blackframe,sizeof(uint8_t), len + uv_len + uv_len,  f )) <= 0 ) {
        goto CALIERR;
    }
    if( n != (len+uv_len + uv_len))
        goto CALIERR;

    uint8_t *lightframe =   cali_get(tag,CALI_LIGHT,w*h,uv_len);
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
    veejay_msg(0, "File write error");

    
    return 0;
}

int     vj_tag_cali_write_file( int t1, char *name, editlist *el ) {
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return 0;
    if(tag->source_type != VJ_TAG_TYPE_V4L) {
        veejay_msg(0, "Stream is not of type Video4Linux");
        return 0;
    }
    if(tag->noise_suppression == 0 ) {
        veejay_msg(0, "Stream %d is not yet calibrated", t1 );
        return 0;
    }
    if(tag->noise_suppression != V4L_BLACKFRAME_PROCESS ) {
        veejay_msg(0, "Please finish calibration first");
        return 0;
    }
    if(! cali_write_file( name, tag, el ) ) {
        return 0;
    }
    return 1;
}


static int  cali_read_file( cali_tag_t *p, char *file,int w, int h )
{
    FILE *f = fopen( file , "r" );
    if( f == NULL ) {
        return 0;
    }

    char    buf[256];

    char    *header = fgets( buf, sizeof(buf), f );
    int len = 0;
    int uv_len  = 0;
    int offset  = 0;

    int Euv_len = vj_tag_input->uv_len;

    double  mean[3];

    if(sscanf(header, "%3d %8d %8d %8d %8d %lf %lf %lf",&offset, &w,&h,&len,&uv_len,
            &mean[0],&mean[1],&mean[2] ) != 8  )
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Invalid header");
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
    p->lf = p->data + (len + (2*uv_len));
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

    veejay_msg(VEEJAY_MSG_INFO, "Image calibration data loaded");

    return 1;

CALIREADERR:
    veejay_msg(VEEJAY_MSG_ERROR, "Only got %d bytes",n);
    return 0;
}

static int  _vj_tag_new_cali( vj_tag *tag, int stream_nr, int w, int h )
{
    if(stream_nr < 0 || stream_nr > VJ_TAG_MAX_STREAM_IN) return 0;
    
    cali_tag_t *p = NULL;

    p = (cali_tag_t*) vj_calloc(sizeof(cali_tag_t));
    if(!p)
        return 0;

    if(!cali_read_file( p, tag->source_name,w,h ) ) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to find dark frame '%s'", tag->source_name );
        free(p);
        return 0;
    }

    p->ptr = cali_malloc(0,0);
    if(!p->ptr) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to allocate");
        free(p);
        return 0;
    }

    vj_tag_input->cali[stream_nr] = (void*)p;
    
    veejay_msg(VEEJAY_MSG_INFO, "Image Calibration files ready");
    
    return 1;
}

uint8_t *vj_tag_get_cali_data( int t1, int what ) {
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
int _vj_tag_new_dv1394(vj_tag *tag, int stream_nr, int channel,int quality, editlist *el)
{
   vj_tag_input->dv1394[stream_nr] = vj_dv1394_init( (void*) el, channel,quality);
   if(vj_tag_input->dv1394[stream_nr])
   {
    veejay_msg(VEEJAY_MSG_INFO, "DV1394 ready for capture");
//  vj_dv_decoder_set_audio( vj_tag_input->dv1394[stream_nr], el->has_audio);
    return 1;
   } 
   return 0;
}
#endif

void    *vj_tag_get_dict( int t1 )
{
#ifdef HAVE_FREETYPE
    vj_tag *tag = vj_tag_get(t1);
    if(tag)
        return tag->dict;
#endif
    return NULL;
}

int vj_tag_set_stream_layout( int t1, int stream_id_g, int screen_no_b, int value )
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

int vj_tag_generator_set_arg(int t1, int *values)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return 0;
    if(tag->generator) {
        int i;
        for( i = 0; i < 16; i ++ ) {
            tag->genargs[i] = values[i];
        }
        return 1;
    }
    return 0;
}

int vj_tag_get_transition_shape(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return 0;
    return tag->transition_shape;
}

int vj_tag_get_transition_length(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return 0;

    int transition_length = tag->transition_length;
    if( transition_length > tag->n_frames )
        transition_length = tag->n_frames;

    return transition_length;
}

int vj_tag_get_transition_active(int t1) 
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return 0;
    return tag->transition_active;
}

void vj_tag_set_transition_shape(int t1, int shape) 
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return;
    tag->transition_shape = shape;
}

void vj_tag_set_transition_length(int t1, int length) 
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return;

    int transition_length = length;
    if( transition_length < 0)
        transition_length = 0;

    if( transition_length > tag->n_frames ) {
        transition_length = tag->n_frames;
    }

    tag->transition_length = transition_length;
}

void vj_tag_set_transition_active(int t1, int status)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return;
    tag->transition_active = status;

    if( tag->transition_active == 1 ) {
        if( tag->transition_length <= 0 ) {
            vj_tag_set_transition_length( t1, tag->n_frames );
        }
    }
}

int vj_tag_generator_get_args(int t1, int *args, int *n_args, int *fx_id)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return 0;
    if(tag->generator) {
        vevo_property_get(tag->generator, "HOST_id", 0, fx_id );
        int i;
        int n = plug_instance_get_num_parameters( tag->generator );
        for( i = 0; i < n; i ++ ) {
            args[i] = tag->genargs[i];
        }
        *n_args = n;

        return 1;
    }
    return 0;
}

void	*vj_tag_get_macro(int t1) {
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return NULL;
	return tag->macro;
}	

int vj_tag_set_stream_color(int t1, int r, int g, int b)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
    return 0;
    
    tag->color_r = r;
    tag->color_g = g;
    tag->color_b = b;
/*
    if( tag->generator ) {
        plug_set_parameter( tag->generator, 0,1,&r );
        plug_set_parameter( tag->generator, 1,1,&g );
        plug_set_parameter( tag->generator, 2,1,&b );
    }
*/
    return 1;
}

int vj_tag_get_stream_color(int t1, int *r, int *g, int *b )
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

// for network, filename /channel is passed as host/port num
int vj_tag_new(int type, char *filename, int stream_nr, editlist * el, int pix_fmt, int channel , int extra , int has_composite)
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

    tag->source_name = (char *) vj_calloc(sizeof(char) * SOURCE_NAME_LEN);
    if (!tag->source_name)
    {
        free(tag);
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
            free(tag->source_name);
            free(tag);
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
    tag->fade_method = 0;
    tag->fade_alpha = 0;
    tag->fade_entry = -1;
    tag->fader_inc = 0.0;
    tag->fader_direction = 0;
    tag->selected_entry = 0;
    tag->depth = 0;
    tag->effect_toggle = 1; /* same as for samples */
    tag->color_r = 0;
    tag->color_g = 0;
    tag->color_b = 0;
    tag->opacity = 0;
    tag->priv = NULL;
    tag->subrender = 1;
    tag->transition_length = 25;

	if(type == VJ_TAG_TYPE_AVFORMAT )
		tag->priv = avformat_thread_allocate(_tag_info->effect_frame1);

    palette = get_ffmpeg_pixfmt( pix_fmt );
    
    switch (type) {
        case VJ_TAG_TYPE_V4L:
            snprintf(tag->source_name,SOURCE_NAME_LEN, "%s", filename );
        
            if (!_vj_tag_new_v4l2( tag,
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
                veejay_msg(0, "Unable to open capture stream '%dx%d' (norm=%c,format=%x,device=%d,channel=%d)", w,h,el->video_norm, pix_fmt, extra,channel );
                free(tag->source_name);
                if(tag->method_filename) 
                  free(tag->method_filename);
                free(tag);
                
                return -1;
            }
            break;
        case VJ_TAG_TYPE_MCAST:
        case VJ_TAG_TYPE_NET:
            snprintf(tag->source_name,SOURCE_NAME_LEN, "%s", filename );
            if( _vj_tag_new_net( tag,stream_nr, w,h,pix_fmt, filename, channel ,palette,type) != 1 ) {
                free(tag->source_name);
                if(tag->method_filename) 
                  free(tag->method_filename);
                free(tag);
                
                return -1;
            }
    break;
	case VJ_TAG_TYPE_AVFORMAT:
		snprintf(tag->source_name,SOURCE_NAME_LEN, "%s", filename );
		if(!avformat_thread_start(tag, _tag_info->effect_frame1)) {
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to start thread");
			free(tag->source_name);
            if(tag->method_filename) 
                  free(tag->method_filename);
			free(tag);
			return -1;
		}
		break;
    case VJ_TAG_TYPE_DV1394:
#ifdef SUPPORT_READ_DV2
    snprintf(tag->source_name, SOURCE_NAME_LEN,"dv1394 %d", channel);
    if( _vj_tag_new_dv1394( tag, stream_nr,channel,1,el ) == 0 )
    {
        veejay_msg(VEEJAY_MSG_ERROR, "error opening dv1394 %d", channel);
        free(tag->source_name);
        free(tag);
        return -1;
    }
    tag->active = 1;
    break;
#else
    veejay_msg(VEEJAY_MSG_DEBUG, "libdv not enabled at compile time");
    free(tag->source_name);
    if(tag->method_filename) 
        free(tag->method_filename);
    free(tag);
    return -1;
#endif
#ifdef USE_GDK_PIXBUF
    case VJ_TAG_TYPE_PICTURE:
    snprintf(tag->source_name,SOURCE_NAME_LEN, "%s", filename);
    if( _vj_tag_new_picture(tag, stream_nr, w, h, fps) != 1 ) {
        free(tag->source_name);
        if(tag->method_filename) 
            free(tag->method_filename);
        free(tag);
        return -1;
    }
    break;
#endif
    case VJ_TAG_TYPE_CALI:
    snprintf(tag->source_name,SOURCE_NAME_LEN,"%s",filename);
    if(_vj_tag_new_cali( tag,stream_nr,w,h) != 1 ) {
        free(tag->source_name);
        if(tag->method_filename) 
            free(tag->method_filename);
        free(tag);
        return -1;
    }
    break;
    case VJ_TAG_TYPE_YUV4MPEG:
    snprintf(tag->source_name,SOURCE_NAME_LEN, "%s", filename);
    if (_vj_tag_new_yuv4mpeg(tag, stream_nr, w,h,fps) != 1)
    {
        free(tag->source_name);
        if(tag->method_filename) 
            free(tag->method_filename);
        free(tag);
        return -1;
    }
    tag->active = 1;
    break;
    case VJ_TAG_TYPE_GENERATOR:

    snprintf(tag->source_name,SOURCE_NAME_LEN, "[GEN %d]", channel);

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
                    veejay_msg(0, "'%s' not found",filename );
                    free(tag->source_name);
                    if(tag->method_filename) 
                        free(tag->method_filename);
                    free(tag);
                    return -1;
                }
            }
        }

        int foo_arg  = vj_shm_get_id();

        if( extra != 0 ) //@ vj_shm_set_id is a hack 
            vj_shm_set_id( extra );

        tag->generator = plug_activate(channel);
        if( tag->generator == NULL ) {
            veejay_msg(0, "Unable to load selected generator");
            free(tag->source_name);
            if(tag->method_filename) 
                free(tag->method_filename);
            free(tag);
            return -1;
        }

        int vj_plug_id = 500 + channel; 
        vevo_property_set(tag->generator, "HOST_id",VEVO_ATOM_TYPE_INT, 1, &vj_plug_id );

        if(tag->generator != NULL) {
            vj_shm_set_id( foo_arg );

            if( plug_get_num_input_channels( channel ) > 0 ||
                plug_get_num_output_channels( channel ) == 0 ) {
                    veejay_msg(0, "Plug '%s' is not a generator", filename);
                    plug_deactivate(tag->generator);
                    free(tag->source_name);
                    if(tag->method_filename) 
                        free(tag->method_filename);
                    free(tag);
                    return -1;
            }
            int tmp = 0;
            
            plug_get_parameters( tag->generator, tag->genargs, &tmp);   
            
            if( filename != NULL )
                strcpy( tag->source_name, filename );
        }
        else {
            free(tag->source_name);
            if(tag->method_filename) 
                free(tag->method_filename);
            free(tag);
            return -1;
        }
    }
    break;

    case VJ_TAG_TYPE_COLOR:

    snprintf(tag->source_name, SOURCE_NAME_LEN, "[solid %d]", tag->id );
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
            veejay_msg(VEEJAY_MSG_WARNING, "No generator plugins found. Using built-in");
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
            veejay_msg(VEEJAY_MSG_DEBUG, "Using plug '%s' to generate frames for this stream",plugname);
            strcpy( tag->source_name, plugname );
            free(plugname);
        } else {
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to initialize generator");
            free(tag->source_name);
            free(tag);
            return -1;
        }
    }*/

    tag->active = 1;
    break;
    case VJ_TAG_TYPE_CLONE:
        snprintf(tag->source_name, SOURCE_NAME_LEN, "[clone %d]", tag->id );

        if( _vj_tag_new_clone(tag,channel) == 0 ) {
            free(tag->source_name);
            if(tag->method_filename) 
                free(tag->method_filename);
            free(tag);
            return -1;
        }
        break;
    default:
        veejay_msg(0, "Stream type %d invalid", type );
        free(tag->source_name);
        if(tag->method_filename) 
            free(tag->method_filename);
        free(tag);
        return -1;
    }

    vj_tag_get_by_type( tag->id, tag->source_type, tag->descr);

    /* effect chain is empty */
    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++)
    {
        tag->effect_chain[i] =
            (sample_eff_chain *) vj_calloc(sizeof(sample_eff_chain));
        tag->effect_chain[i]->effect_id = -1;
        tag->effect_chain[i]->e_flag = 0;
        tag->effect_chain[i]->frame_trimmer = 0;
        tag->effect_chain[i]->frame_offset = 0;
	    tag->effect_chain[i]->speed = INT_MAX;
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
        tag->effect_chain[i]->is_rendering = 1;
        tag->effect_chain[i]->kf = vpn( VEVO_ANONYMOUS_PORT );
    }
    if (!vj_tag_put(tag))   
    {
        veejay_msg(0, "Unable to store stream %d - Internal Error", tag->id);
        free(tag->source_name);
        if(tag->method_filename) 
            free(tag->method_filename);
        free(tag);
        return -1;
    }
    last_added_tag = tag->id; 
    this_tag_id++;

#ifdef HAVE_FREETYPE
    tag->dict = vpn(VEVO_ANONYMOUS_PORT );
#endif

    tag->macro = vj_macro_new();

    tag_cache[ tag->id ] = (void*) tag;
    recount_hash = 1;

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
    if (id <= 0)
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

int vj_tag_verify_delete(int id, int type )
{
    int i,j;
    int n = vj_tag_highest();

    for( i = 1; i <= n; i ++ )
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

    n = sample_highest();
    for( i = 1; i <= n; i ++ )
    {
        sample_info *s = sample_get(i);
        if(s)
        {
            for( j = 0 ; j < SAMPLE_MAX_EFFECTS; j ++ )
            {
                if(s->effect_chain[j]->channel == id &&
                   s->effect_chain[j]->source_type == type )
                {
                    s->effect_chain[j]->channel = i;
                    s->effect_chain[j]->source_type = 0;
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
    case VJ_TAG_TYPE_CLONE:
        {
            int t2 = tag->video_channel;
            vj_tag *tag2 = vj_tag_get( t2 );
            if( tag2 ) {
                if(tag2->clone > 0 )
                    tag2->clone --;
            }
        }
    case VJ_TAG_TYPE_V4L: 
#ifdef HAVE_V4L2
        if( no_v4l2_threads_ ) {
            v4l2_close( vj_tag_input->v4l2[tag->index]);
        } else {
            v4l2_thread_stop( v4l2_thread_info_get(vj_tag_input->v4l2[tag->index]));
        }
#endif
    
        break;
     case VJ_TAG_TYPE_YUV4MPEG: 
        veejay_msg(VEEJAY_MSG_INFO,"Closing yuv4mpeg file %s (Stream %d)",
            tag->source_name,id);
        vj_yuv_stream_stop_read(vj_tag_input->stream[tag->index]);
//      vj_yuv4mpeg_free( vj_tag_input->stream[tag->index]);
     break;
	 case VJ_TAG_TYPE_AVFORMAT:
		avformat_thread_stop(tag);
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
        cali_tag_t *calpic = (cali_tag_t*) vj_tag_input->cali[tag->index];
        if(calpic) {
            if(calpic->data) free(calpic->data);
            if(calpic->ptr) cali_free(calpic->ptr);
            free(calpic);
        }
        vj_tag_input->cali[tag->index] = NULL;
        }
        break;
    case VJ_TAG_TYPE_MCAST:
    case VJ_TAG_TYPE_NET:
        net_thread_stop(tag);
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
  
    vj_tag_chain_free( tag->id,1 );

    if(tag->blackframe)free(tag->blackframe);
    if( tag->bf ) free(tag->bf);
    if( tag->bfu ) free(tag->bfu);
    if( tag->bfv ) free(tag->bfv);
    if( tag->lf ) free(tag->lf);
    if( tag->lfu ) free(tag->lfu);
    if( tag->lfv ) free(tag->lfv);

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

    vj_macro_free( tag->macro );

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
    recount_hash = 1;

    return 1;
}

void vj_tag_close_all() {
   int n=vj_tag_highest();
   int i;
   vj_tag *tag;

   for(i=1; i <= n; i++) {
    tag = vj_tag_get(i);
    if(tag) {
        if(vj_tag_del(i)) veejay_msg(VEEJAY_MSG_DEBUG, "Deleted stream %d", i);
    }
   }
    
   if( TagHash ) {
       hash_free_nodes( TagHash );
       TagHash = NULL;
   }
}

int vj_tag_get_n_frames(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
        return 0;
    return tag->n_frames;   
}

int vj_tag_set_n_frames( int t1, int n )
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
        return 0;
    tag->n_frames = n;
    return 1;
}

sample_eff_chain    **vj_tag_get_effect_chain(int t1)
{
    vj_tag * tag = vj_tag_get(t1);
    if(tag == NULL)
        return NULL;
    return tag->effect_chain;
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
   return tag->fader_val;
}
void vj_tag_set_fade_method(int t1, int method)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return;
    tag->fade_method = method;
}
void vj_tag_set_fade_alpha(int t1, int alpha)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return;
    tag->fade_alpha = alpha;
}

void vj_tag_set_fade_entry(int t1, int entry)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return;
    tag->fade_entry = entry;
}


int vj_tag_set_description(int t1, char *description)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return 0;
    if(!description || strlen(description) <= 0 )
        snprintf( tag->descr, TAG_MAX_DESCR_LEN, "%s","Untitled"); 
    else
        snprintf( tag->descr, TAG_MAX_DESCR_LEN, "%s", description );
    return 1;
}

int vj_tag_get_description( int t1, char *description )
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return 0;
    snprintf( description ,TAG_MAX_DESCR_LEN, "%s", tag->descr );
    return 1;
}
int vj_tag_get_fade_alpha( int t1, int alpha)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return 0;
    return tag->fade_alpha;
}

int vj_tag_set_manual_fader(int t1, int value )
{
  vj_tag *tag = vj_tag_get(t1);
  if(!tag) return -1;
  tag->fader_active = 2;
  tag->fader_inc = 0.0;
  tag->fader_val = (float)value;
  if(tag->effect_toggle == 0) 
      tag->effect_toggle = 1;
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
int vj_tag_get_fade_entry(int t1) {
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return -1;
    return tag->fade_entry;
}

int vj_tag_get_fade_method(int t1) {
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return -1;
    return tag->fade_method;
}
int vj_tag_get_fader_direction(int t1) {
   vj_tag *tag = vj_tag_get(t1);
   if(!tag) return -1;
   if( tag->fader_active == 0 )
       return 0; // no direction
   return (tag->fader_direction);
}

int vj_tag_apply_fader_inc(int t1) {
  vj_tag *tag = vj_tag_get(t1);
  if(!tag) return -1;
  tag->fader_val += tag->fader_inc;
  if(tag->fader_val > 255.0f ) tag->fader_val = 255.0f;
  if(tag->fader_val < 0.0f) tag->fader_val = 0.0f;
  return (int) tag->fader_val;
}

int vj_tag_set_fader_active(int t1, int nframes , int direction) {
  vj_tag *tag = vj_tag_get(t1);
  if(!tag) return -1;
  if(nframes <= 0) return -1;
  tag->fader_active = 1;
  if(direction<0)
    tag->fader_val = 255.0f;
  else
    tag->fader_val = 0.0f;
  tag->fader_inc = (float) (255.0f / (float)nframes );
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

int vj_tag_get_num_encoded_files(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return -1;
    return tag->sequence_num;
}

int vj_tag_get_encoder_format(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return -1;
    return tag->encoder_format;
}

int vj_tag_get_sequenced_file(int t1, char *descr, int num, char *ext)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return -1;
    sprintf(descr, "%s-%05d.%s", tag->encoder_destination,num,ext );
    return 1;
}


int vj_tag_try_filename(int t1, char *filename, int format)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) 
    {
        return 0;
    }
    if(filename != NULL)
    {
        snprintf(tag->encoder_base, sizeof(tag->encoder_base), "%s", filename);
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
    
    snprintf(tag->encoder_destination,sizeof(tag->encoder_destination), "%s-%04d.%s", tag->encoder_base, (int)tag->sequence_num, ext);
    return 1;
}



static int vj_tag_start_encoder(vj_tag *tag, int format, long nframes)
{
    char cformat = vj_avcodec_find_lav( format );
    
    tag->encoder =  vj_avcodec_start( _tag_info->effect_frame1, format, tag->encoder_destination );
    if(!tag->encoder)
    {
        veejay_msg(0, "Unable to use selected encoder, please choose another");
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
            "Auto splitting file, %ld frames left to record",
            (tag->encoder_frames_to_record - tag->encoder_total_frames_recorded ));
        tag->encoder_frames_recorded=0;

        return 2;
    }
    
    if( tag->encoder_total_frames_recorded >= tag->encoder_frames_to_record)
    {
        veejay_msg(VEEJAY_MSG_INFO, "Recorded %ld frames",
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
#ifdef HAVE_V4L2
    v4l2_set_brightness( vj_tag_input->v4l2[tag->index],value);
#endif
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
#ifdef HAVE_V4L2
    v4l2_set_gamma( vj_tag_input->v4l2[tag->index],value);
#endif
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
#ifdef HAVE_V4L2
    v4l2_set_hue( vj_tag_input->v4l2[tag->index],value );     
#endif
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
#ifdef HAVE_V4L2
    v4l2_set_contrast( vj_tag_input->v4l2[tag->index], value );
#endif
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
#ifdef HAVE_V4L2
    v4l2_set_whiteness( vj_tag_input->v4l2[tag->index], value );
#endif
    return 1;
}


int vj_tag_get_v4l_properties(int t1, int *values )
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return -1;
    if(tag->source_type!=VJ_TAG_TYPE_V4L)
    {
        return 0;
    }
    
#ifdef HAVE_V4L2
    values[0] = v4l2_get_brightness(  vj_tag_input->v4l2[tag->index] );
    values[1] = v4l2_get_contrast( vj_tag_input->v4l2[tag->index] );
    values[2] = v4l2_get_hue( vj_tag_input->v4l2[tag->index] );
    values[3] = v4l2_get_saturation( vj_tag_input->v4l2[tag->index] );
    values[4] = v4l2_get_temperature( vj_tag_input->v4l2[tag->index] );
    values[5] = v4l2_get_gamma( vj_tag_input->v4l2[tag->index] );
    values[6] = v4l2_get_sharpness( vj_tag_input->v4l2[tag->index] );
    values[7] = v4l2_get_gain( vj_tag_input->v4l2[tag->index] );
    values[8] = v4l2_get_red_balance( vj_tag_input->v4l2[tag->index] );
    values[9] = v4l2_get_blue_balance( vj_tag_input->v4l2[tag->index] );
    values[10]= -1;
    values[11]= v4l2_get_gain(vj_tag_input->v4l2[tag->index]);
    values[12]= v4l2_get_backlight_compensation( vj_tag_input->v4l2[tag->index] );
    values[13]= v4l2_get_whiteness( vj_tag_input->v4l2[tag->index] );
    values[14]= v4l2_get_black_level( vj_tag_input->v4l2[tag->index]);
    values[15]= v4l2_get_exposure(vj_tag_input->v4l2[tag->index] );
    values[16]= v4l2_get_auto_white_balance( vj_tag_input->v4l2[tag->index] );
    values[17]= v4l2_get_autogain( vj_tag_input->v4l2[tag->index] );
    values[18]= v4l2_get_hue_auto(vj_tag_input->v4l2[tag->index] );
    values[19]= v4l2_get_hflip(vj_tag_input->v4l2[tag->index] );
    values[20]= v4l2_get_vflip(vj_tag_input->v4l2[tag->index]);
 
    return 1;   
#else

    return 0;
#endif
}

int vj_tag_v4l_set_control( int t1, uint32_t id, int value )
{
    vj_tag *tag = vj_tag_get(t1);
    if(tag == NULL)
        return 0;
    if(tag->source_type != VJ_TAG_TYPE_V4L )
        return 0;
#ifdef HAVE_V4L2
    v4l2_set_control( vj_tag_input->v4l2[tag->index], id, value );
#endif
    return 1;   
}

int vj_tag_get_effect_any(int t1, int position) {
    vj_tag *tag = vj_tag_get(t1);
    if(!tag )
       return 0;
    return tag->effect_chain[position]->effect_id;
}

int vj_tag_chain_free(int t1, int global)
{
    vj_tag *tag = vj_tag_get(t1);
    int i=0;
    int sum = 0;
   
    for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
    {
        if( tag->effect_chain[i]->effect_id == -1 )
            continue;

        vjert_del_fx( tag->effect_chain[i],0,i,0);
        sum++;
            
        if( tag->effect_chain[i]->source_type == 1 && 
            vj_tag_get_active( tag->effect_chain[i]->channel ) && 
            vj_tag_get_type( tag->effect_chain[i]->channel ) == VJ_TAG_TYPE_NET ) { //FIXME: check if this behaviour is correct
            vj_tag_disable( tag->effect_chain[i]->channel );
        
        }   
    } 

    return sum;
}

int vj_tag_chain_reset_kf( int s1, int entry )
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

int vj_tag_get_kf_status(int s1, int entry, int *type )
{
   vj_tag *tag = vj_tag_get(s1);
   if (!tag)
    return 0;
   if(type != NULL)
       *type = tag->effect_chain[entry]->kf_type;

   return tag->effect_chain[entry]->kf_status;
}

void    vj_tag_set_kf_type(int s1, int entry, int type )
{
   vj_tag *tag = vj_tag_get(s1);
   if (!tag)
    return;
   tag->effect_chain[entry]->kf_type = type;
}


int vj_tag_get_kf_tokens( int s1, int entry, int id, int *start,int *end, int *type, int *status)
{
  vj_tag *tag = vj_tag_get(s1);
   if (!tag)
    return 0;
   return keyframe_get_tokens( tag->effect_chain[entry]->kf,id, start,end,type,status);
}


int vj_tag_chain_set_kf_status( int s1, int entry, int status )
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


int vj_tag_chain_set_kfs( int s1, int len, unsigned char *data )
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

    if (!tag) {
        return 0;
    }
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS) {
        return 0;
    }
    if (!vje_is_valid(effect_id)) {
        return 0;
    }

    if( tag->effect_chain[position]->effect_id == -1 ) {
        tag->effect_chain[position]->effect_id = effect_id;
    }
    else if( tag->effect_chain[position]->effect_id != effect_id ) {
        vjert_del_fx( tag->effect_chain[position],0,position,1);
        tag->effect_chain[position]->effect_id = effect_id;
    }

    if( tag->effect_chain[position]->source_type == 1 && 
        vj_tag_get_active( tag->effect_chain[position]->channel ) && 
        tag->effect_chain[position]->channel != t1 &&
        vj_tag_get_type( tag->effect_chain[position]->channel ) == VJ_TAG_TYPE_NET ) 
    { //FIXME: test if this behaviour is correct
            vj_tag_disable( tag->effect_chain[position]->channel );
    }

    params = vje_get_num_params(effect_id);
    for (i = 0; i < params; i++) {
        tag->effect_chain[position]->arg[i] = vje_get_param_default(effect_id, i);
    }
    tag->effect_chain[position]->e_flag = 1; 
    tag->effect_chain[position]->kf_status = 0;
    tag->effect_chain[position]->kf_type = 0;
    
    if (vje_get_extra_frame(effect_id))
    {
        if(tag->effect_chain[position]->source_type < 0)
            tag->effect_chain[position]->source_type = 1;
        if(tag->effect_chain[position]->channel <= 0 )
            tag->effect_chain[position]->channel = t1;
    }
    else 
    {
        if( position == tag->fade_entry) { 
            if( tag->fade_method == 4 ) 
                tag->fade_method = 2; /* auto switch */
            else if(tag->fade_method == 3 ) 
                tag->fade_method = 1;
        }
    }
    return 1;
}

int vj_tag_has_cali_fx( int t1 ) {
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

void    vj_tag_cali_prepare_now( vj_tag *tag  ) {
    if(tag->source_type != VJ_TAG_TYPE_CALI )
        return;
    cali_tag_t *p = (cali_tag_t*) vj_tag_input->cali[tag->index];
    if( p == NULL )
        return;

    cali_prepare( p->ptr, //@ ptr to cali instance
              p->mean[0],
              p->mean[1],
              p->mean[2],
              p->data,
              vj_tag_input->width * vj_tag_input->height,
              vj_tag_input->uv_len );

}

void    vj_tag_cali_prepare( int t1 , int pos, int cali_tag) {
    vj_tag *tagc = vj_tag_get(cali_tag);
    if(!tagc)
        return;
    if(tagc->source_type != VJ_TAG_TYPE_CALI)
        return;
    vj_tag_cali_prepare_now( tagc );
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
        return 0;
    if (arg_len == 0 )
        return 1;
    if (!args)
        return 0;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
        return 0;
    if (arg_len < 0 || arg_len > SAMPLE_MAX_PARAMETERS)
        return 0;

    if( tag->effect_chain[position]->kf_status )
    {
        for( i =0;i <arg_len; i ++ )
        {
            if( tag->effect_chain[position]->kf_status) {
                int tmp = 0;
                if(!get_keyframe_value( tag->effect_chain[position]->kf, n_frame, i ,&tmp ) )
                    args[i] = tag->effect_chain[position]->arg[i];
                else {
                    args[i] = tmp;
                }
            }
            else {
                args[i] = tag->effect_chain[position]->arg[i];
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

	if(tag->source_type == VJ_TAG_TYPE_AVFORMAT ) {
		avformat_thread_set_state( tag,0 );
	}

    if(tag->source_type == VJ_TAG_TYPE_V4L && !tag->clone )
    {
#ifdef HAVE_V4L2
       if( no_v4l2_threads_ ) {
           v4l2_set_status( vj_tag_input->v4l2[tag->index],1);
       } else {
           v4l2_thread_set_status( v4l2_thread_info_get(vj_tag_input->v4l2[tag->index]),0 );
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
#ifdef HAVE_V4L2
        if( no_v4l2_threads_ ) {
            v4l2_set_status( vj_tag_input->v4l2[tag->index],1);
        } else {
            v4l2_thread_set_status( v4l2_thread_info_get( vj_tag_input->v4l2[tag->index] ), 1 );
        }
        tag->active = 1;
#endif
        return 1;
    }

    if(tag->source_type == VJ_TAG_TYPE_NET || tag->source_type == VJ_TAG_TYPE_MCAST )
    {
        if(!net_thread_start(tag, _tag_info->effect_frame1))
        {
            veejay_msg(VEEJAY_MSG_ERROR,
                    "Unable to start thread");
            return -1;
        }
    }

	if(tag->source_type == VJ_TAG_TYPE_AVFORMAT )
	{
		if(!avformat_thread_set_state(tag,1)) {
			veejay_msg(VEEJAY_MSG_ERROR, "Stream is not yet ready to start playing");
			return -1;
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

#ifdef HAVE_V4L2
        if( no_v4l2_threads_ ) {
            v4l2_set_status( vj_tag_input->v4l2[tag->index],active);
        } else {
            v4l2_thread_set_status( v4l2_thread_info_get( vj_tag_input->v4l2[tag->index]), active );
        }
        tag->active = active;
#endif
        break;
    case VJ_TAG_TYPE_YUV4MPEG:
         if(active==0)
        {
             tag->active = 0;
             vj_yuv_stream_stop_read( vj_tag_input->stream[tag->index]);
        }
    break;
	case VJ_TAG_TYPE_AVFORMAT:
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

int vj_tag_get_subrender(int t1, int position, int *do_subrender)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return -1;
    *do_subrender = tag->effect_chain[position]->is_rendering;
    return tag->subrender;
}

void    vj_tag_set_subrender(int t1, int status)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return;
    tag->subrender = status;
}

void    vj_tag_entry_set_is_rendering(int t1, int position, int state)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
        return;

    tag->effect_chain[position]->is_rendering = state;
}

int    vj_tag_entry_is_rendering(int t1, int position)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return -1; 
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
        return -1;

    return tag->effect_chain[position]->is_rendering;
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

int vj_tag_chain_remove(int t1, int index)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
        return -1;

    if( tag->effect_chain[index]->effect_id != -1 ) {
        vjert_del_fx( tag->effect_chain[index],0,index,1);
    }
    
    tag->effect_chain[index]->e_flag = 0;
    tag->effect_chain[index]->is_rendering = 1;

    if( tag->effect_chain[index]->source_type == 1 && 
        vj_tag_get_active( tag->effect_chain[index]->channel ) && 
        tag->effect_chain[index]->channel != t1 &&
        vj_tag_get_type( tag->effect_chain[index]->channel ) == VJ_TAG_TYPE_NET )
    { //FIXME test if this behaviour is correct
        vj_tag_disable( tag->effect_chain[index]->channel );
    }

    tag->effect_chain[index]->source_type = 1;
    tag->effect_chain[index]->channel     = t1; //set to self

    int j;
    for (j = 0; j < SAMPLE_MAX_PARAMETERS; j++) {
        tag->effect_chain[index]->arg[j] = 0;
	}

    if( index == tag->fade_entry )
        tag->fade_entry = -1;

    return 1;
}

void	vj_tag_set_chain_paused(int t1, int paused)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag)
		return;
	int entry;
	for( entry = 0; entry < SAMPLE_MAX_EFFECTS; entry ++ ) {
		if( tag->effect_chain[entry]->source_type != 0 ||
	     		tag->effect_chain[entry]->channel <= 0 )
			continue;

		if( paused == 1 ) {
			int speed = sample_get_speed( tag->effect_chain[entry]->channel );
			if( speed != 0) {
				tag->effect_chain[entry]->speed = speed;
				sample_set_speed( tag->effect_chain[entry]->channel, 0 );
			}
		} 
		else {
			if( tag->effect_chain[entry]->speed == 0 ) {
				tag->effect_chain[entry]->speed = sample_get_speed( tag->effect_chain[entry]->channel );
				if( tag->effect_chain[entry]->speed == 0 ) {
					veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d on mixing entry %d is paused. Please set speed manually",
							tag->effect_chain[entry]->channel, entry);
				}	
			}


			if( tag->effect_chain[entry]->speed != INT_MAX ) {
				sample_set_speed( tag->effect_chain[entry]->channel, tag->effect_chain[entry]->speed );
				veejay_msg(VEEJAY_MSG_DEBUG, "Restoring speed %d for sample %d on mixing entry %d",
					tag->effect_chain[entry]->speed, tag->effect_chain[entry]->channel, entry );
			}
		}
	}
}

int	vj_tag_get_loop_stat_stop(int s1) {
	vj_tag *tag = vj_tag_get(s1);
    	if (!tag) return 0;
	return tag->loop_stat_stop;
}
void 	vj_tag_set_loop_stat_stop(int s1, int loop_stop) {
	vj_tag *tag = vj_tag_get(s1);
	if(!tag) return;
	tag->loop_stat_stop = loop_stop;
}

int	vj_tag_get_loop_stats(int s1) {
	vj_tag *tag = vj_tag_get(s1);
    	if (!tag) return 0;
	return tag->loop_stat;
}
void 	vj_tag_set_loop_stats(int s1, int loops) {
	vj_tag *tag = vj_tag_get(s1);
	if(!tag) return;
	if( loops == -1) {
		tag->loop_stat = (tag->loop_stat_stop > 0 ? (tag->loop_stat + 1 ) % tag->loop_stat_stop : tag->loop_stat + 1);
	}
	else
		tag->loop_stat = loops;
}

int     vj_tag_get_loops(int t1) {
    vj_tag *tag = vj_tag_get(t1);
    if(tag) {
        return tag->loops;
    }
    return 0;
}

void     vj_tag_set_loops(int t1, int loops) {
    vj_tag *tag = vj_tag_get(t1);
    if(tag) {
        if(loops == -1) {
            tag->loops = tag->loop_stat_stop;
            return;
        }
        tag->loops = loops;
    }
}


int     vj_tag_loop_dec(int t1) {
    vj_tag *tag = vj_tag_get(t1);
    if(tag && tag->loops > 0) {
        tag->loops --;
    }
    return tag->loops;
}

int     vj_tag_at_next_loop(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return 0;
    int lo = tag->loops;

    if(lo > 0)
        lo --;

    return (lo == 0 ? 1: 0);
}

// very old code, 2 callers; 150 and 255 size of dst
void vj_tag_get_source_name(int t1, char *dst)
{
    vj_tag *tag = vj_tag_get(t1);
    if (tag) {
        snprintf(dst,150, "%s", tag->source_name);
    } 
}

void    vj_tag_get_by_type(int id,int type, char *description )
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
	case VJ_TAG_TYPE_AVFORMAT:
	sprintf(description, "%s", "AVFormat stream reader");
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
    case VJ_TAG_TYPE_CLONE:
    sprintf(description, "%s", "Clone" );
    break;
    default:
    sprintf(description ,"T%d", id );
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
        vj_tag_get_by_type( id, tag->source_type, description );
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

int vj_tag_reset_offset(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
        return -1;
    int i;
    for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
    {
        tag->effect_chain[i]->frame_offset = 0;
    }
    return 1;
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


void	vj_tag_update_ascociated_samples(int s1)
{
    vj_tag *sample = vj_tag_get(s1);
    if(!sample) {
        return;
    }
	
	int p = 0;
    for( p = 0; p < SAMPLE_MAX_EFFECTS; p ++ ) {
		if( sample->effect_chain[p]->source_type != 0  ) 
			continue;
		if( !sample_exists(sample->effect_chain[p]->channel) )
			continue;

		int pos = sample->effect_chain[p]->frame_offset;
		if(pos == 0 )
			continue;	

		sample_set_resume( sample->effect_chain[p]->channel, pos );
		veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d will resume playback from position %d",
					sample->effect_chain[p]->channel, pos );
	}
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

int vj_tag_var(int t1, int *type, int *fader, int *fx_sta , int *rec_sta, int *active, int *method, int *entry, int *alpha )
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag) return 0;
    *fader  = tag->fader_active;
    *fx_sta = tag->effect_toggle;
    *rec_sta = tag->encoder_active;
    *type = tag->source_type;
    *active = tag->active;
    *method = tag->fade_method;
    *entry = tag->fade_entry;
    *alpha = tag->fade_alpha;
    return 1;
}

int vj_tag_record_frame(int t1, uint8_t *buffer[4], uint8_t *abuff, int audio_size,int pixel_format) {
   vj_tag *tag = vj_tag_get(t1);
   int buf_len = 0;
   if(!tag) return -1;

   if(!tag->encoder_active) return -1;

    long nframe = tag->encoder_frames_recorded; 

    uint8_t *dst = vj_avcodec_get_buf(tag->encoder);

    buf_len =    vj_avcodec_encode_frame( tag->encoder, nframe, tag->encoder_format, buffer,dst, tag->encoder_max_size, pixel_format);
    if(buf_len <= 0 ) {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to encode frame" ); 
        return -1;
    }

    if(tag->encoder_file ) {
        if(lav_write_frame(tag->encoder_file, vj_avcodec_get_buf(tag->encoder), buf_len,1))
        {
            veejay_msg(VEEJAY_MSG_ERROR, "%s", lav_strerror());
            if( tag->encoder_frames_recorded > 1 ) {
                veejay_msg(VEEJAY_MSG_WARNING, "Recorded only %ld frames", tag->encoder_frames_recorded);
                return 1; //@ auto commit sample even if it did not write all frames
            }
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

static uint8_t  *blackframe_new( int w, int h, int uv_len, uint8_t *Y, uint8_t *U, uint8_t *V, int median_radius, vj_tag *tag ) {
    uint8_t *buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * 5 * ((w*h) + 2 * uv_len ));
    if(buf == NULL) {
        veejay_msg(0,"Insufficient memory to initialize calibration");
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

static void blackframe_process(  uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int uv_len, int median_radius, vj_tag *tag )
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
static  void    whiteframe_new(uint8_t *buf, int w, int h, int uv_len, uint8_t *Y, uint8_t *U, uint8_t *V, int median_radius, vj_tag *tag ) {
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
    mean_of_u = mean_of_u / uv_len;
    mean_of_v = mean_of_v / uv_len;

}

static void whiteframe_process( uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int uv_len, int median_radius, vj_tag *tag)
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
}

static  void    master_lightframe(int w,int h, int uv_len, vj_tag *tag)
{
    int i;
    int duration =tag->cali_duration;
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
static  void    master_blackframe(int w, int h, int uv_len, vj_tag *tag )
{
    int i;
    int duration =tag->cali_duration;
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

static  void    master_flatframe(int w, int h, int uv_len,vj_tag *tag )
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

static void blackframe_subtract( vj_tag *tag, uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int uv_len,int use_light,const double mean_y,const double mean_u,const double mean_v )
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
        veejay_msg(VEEJAY_MSG_WARNING, "Calibration source is not a video device");
    }
    
    if( duration < 1 )
        return 0;

    if( median_radius <= 0 ) {
        median_radius = 0;
    }

    tag->cali_duration = duration;

    veejay_msg(VEEJAY_MSG_INFO,"Setup per-pixel camera calibration:");
    veejay_msg(VEEJAY_MSG_INFO,"  This method attempts to fix the inhomogenous brightness distribution. Each little pixel in your CCD camera is slightly different.");
    veejay_msg(VEEJAY_MSG_INFO,"  This method may also help reduce the effect of dust collected on the lens.");
    veejay_msg(VEEJAY_MSG_INFO,"  Processing starts immediately after taking the dark and light frames.");
    veejay_msg(VEEJAY_MSG_INFO,"  Take the dark frame while having the cap on the lens.");
    veejay_msg(VEEJAY_MSG_INFO,"  Take the light frame while illuminating the lens with a light source. The image should be white.");
    veejay_msg(VEEJAY_MSG_INFO,"  You can save the dark,light and flattened image frame using reloaded.");
    veejay_msg(VEEJAY_MSG_INFO,"\tMode: %s", (mode == 0 ? "Darkframe" : "Lightframe" ) );
    veejay_msg(VEEJAY_MSG_INFO,"\tMedian radius: %d", median_radius );
    veejay_msg(VEEJAY_MSG_INFO,"\tDuration: %d", duration );

    tag->noise_suppression = (mode == 0 ? V4L_BLACKFRAME : V4L_WHITEFRAME );
    tag->median_radius     = median_radius;
    tag->bf_index          = 0;
    tag->has_white         = (mode == 1 ? 1 :0);
    
    return 1;
}

int vj_tag_drop_blackframe(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return 0;

    if( tag->blackframe ) {
        tag->noise_suppression = -1;
        free(tag->blackframe);
        tag->blackframe = NULL;
    }

    return 1;
}

int vj_tag_get_frame(int t1, VJFrame *dst, uint8_t * abuffer)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return -1;

    const int width = dst->width;
    const int height = dst->height;
    const int uv_len = dst->uv_len;
    const int len = dst->len;

    int res = 0;
    uint8_t **buffer = dst->data;
    
    switch (tag->source_type)
    {
    case VJ_TAG_TYPE_V4L:
#ifdef HAVE_V4L2
        if( no_v4l2_threads_ ) {
            res = v4l2_pull_frame( vj_tag_input->v4l2[tag->index],v4l2_get_dst(vj_tag_input->v4l2[tag->index],buffer[0],buffer[1],buffer[2],buffer[3]) );
        } else {
            res = v4l2_thread_pull( v4l2_thread_info_get( vj_tag_input->v4l2[tag->index]),
                        v4l2_get_dst( vj_tag_input->v4l2[tag->index], buffer[0],buffer[1],buffer[2],buffer[3]));
        }
#endif
         break;
    case VJ_TAG_TYPE_CALI:
        {
            cali_tag_t *p = (cali_tag_t*)vj_tag_input->cali[tag->index];
            if(p && p->mf) {
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
        if(!net_thread_get_frame( tag,dst )) {
		return 0; //failed to get frame
	}
        break;
    case VJ_TAG_TYPE_YUV4MPEG:
        res = vj_yuv_get_frame(vj_tag_input->stream[tag->index],buffer);
        if( res == -1 )
        {
            vj_tag_set_active(t1,0);
            return -1;
        }
        return 1;
        break;
#ifdef SUPPORT_READ_DV2
    case VJ_TAG_TYPE_DV1394:
        vj_dv1394_read_frame( vj_tag_input->dv1394[tag->index], buffer , abuffer,vj_tag_input->pix_fmt);
        break;
#endif
    case VJ_TAG_TYPE_GENERATOR:
        if( tag->generator ) {
            plug_push_frame( tag->generator, 1, 0, dst );
            plug_set_parameters( tag->generator, plug_instance_get_num_parameters(tag->generator),tag->genargs );
            plug_process( tag->generator, -1.0 ); 
        }
        break;
	case VJ_TAG_TYPE_AVFORMAT:
		if(!tag->active)
			return 0; // not allowed to enter get_frame
		if(!avformat_thread_get_frame( tag,dst,_tag_info->real_fps )) //TODO: net and avformat seem to be the same, just like all other types. use a modular structure
		{
			return 0; // failed to get frame
		}
		break;
    case VJ_TAG_TYPE_COLOR:
        dummy_rgb_apply( dst, tag->color_r,tag->color_g,tag->color_b );
        break;
    case VJ_TAG_TYPE_CLONE:
        {
            int t2 = tag->video_channel;
            vj_tag *tag2 = vj_tag_get(t2);
            if(tag2 && tag2->clone == 0)
                tag2->clone ++; //@ auto restore

            if( vj_tag_get_frame(t2, dst, NULL) <= 0 ) {
                dummy_rgb_apply( dst, 0,0,0 );
            }
        }
        break;
    case VJ_TAG_TYPE_NONE:
        break;
        default:
        break;
    }


    switch( tag->noise_suppression ) {
                    
        case V4L_BLACKFRAME_PROCESS:
            blackframe_subtract( tag,buffer[0],buffer[1],buffer[2],width,height,uv_len, tag->has_white , tag->mean[0],tag->mean[1],tag->mean[2]);
            break;

        case V4L_BLACKFRAME:
            blackframe_new(width,height,uv_len,buffer[0],buffer[1],buffer[2],tag->median_radius,tag);
            tag->noise_suppression = V4L_BLACKFRAME_NEXT;
            tag->bf_index ++;
            veejay_msg(VEEJAY_MSG_INFO,"Processed dark frame %d/%d", tag->bf_index, tag->cali_duration);
            break;

        case V4L_BLACKFRAME_NEXT:
            blackframe_process( buffer[0],buffer[1],buffer[2],width,height,uv_len, tag->median_radius,tag );
            tag->bf_index ++;
            if(tag->bf_index == tag->cali_duration) {
                tag->noise_suppression = 0;
                master_blackframe(width,height,uv_len,tag);
                veejay_msg(VEEJAY_MSG_INFO, "Please create a lightframe now");
            }
            veejay_msg(VEEJAY_MSG_INFO, "Processed darkframe %d/%d", tag->bf_index, tag->cali_duration); 
            
            break;
        case V4L_WHITEFRAME:
            if(!tag->blackframe) {
                veejay_msg(0, "Please start with a dark frame first (Put cap on lens)");
                tag->noise_suppression = 0;
                break;
            }
            
            whiteframe_new( tag->blackframe,width,height,uv_len,buffer[0],buffer[1],buffer[2],tag->median_radius, tag);
            tag->bf_index ++;
            tag->noise_suppression = V4L_WHITEFRAME_NEXT;
            veejay_msg(VEEJAY_MSG_INFO,"Processed light frame %d/%d", tag->bf_index, tag->cali_duration);

            break;
        case V4L_WHITEFRAME_NEXT:
            whiteframe_process(buffer[0],buffer[1],buffer[2],width,height,uv_len,tag->median_radius,tag );
            tag->bf_index ++;
            if( tag->bf_index == tag->cali_duration ) {
                tag->noise_suppression = V4L_BLACKFRAME_PROCESS; // actual processing 
                master_lightframe( width,height,uv_len,tag);    
                master_flatframe( width,height,uv_len, tag );
                veejay_msg(VEEJAY_MSG_DEBUG, "Mastered flat frame. Ready for processing. Mean is %g,%g,%g", tag->mean[0], tag->mean[1], tag->mean[2]);
            }
            veejay_msg(VEEJAY_MSG_INFO, "Processed light frame %d/%d", tag->bf_index, tag->cali_duration );
            
            break;
        case -1:
            if( tag->blackframe ) { free(tag->blackframe); tag->blackframe = NULL; }
            if( tag->bf ) { free(tag->bf); tag->bf = NULL; }
            if( tag->bfu ) { free(tag->bfu); tag->bfu = NULL; }
            if( tag->bfv ) { free(tag->bfv); tag->bfv = NULL; }
            if( tag->lf ) { free(tag->lf); tag->lf = NULL; }
            if( tag->lfu ) { free(tag->lfu); tag->lfu = NULL; }
            if( tag->lfv ) { free(tag->lfv); tag->lfv = NULL; }
            
            veejay_msg(VEEJAY_MSG_INFO, "Calibration data cleaned up");
            break;
        default:    
           break;
    }

    return 1;
}


//int vj_tag_sprint_status(int tag_id, int entry, int changed, char *str)
int vj_tag_sprint_status( int tag_id,int samples,int cache,int sa, int ca, int pfps,int frame,int mode,int ts,int seq_rec, int curfps, uint32_t lo, uint32_t hi, int macro, char *str, int feedback )
{
    vj_tag *tag;
    tag = vj_tag_get(tag_id);
    if(!tag)
    return 0;

    int e_a, e_d, e_s;
    //@ issue #60
    if( sa && seq_rec)
    {
        sample_info *rs = sample_get( seq_rec );
        e_a = rs->encoder_active;
        e_d = rs->encoder_frames_to_record;
        e_s = rs->encoder_total_frames_recorded;
    }
    else
    {
        e_a = tag->encoder_active;
        e_d = tag->encoder_frames_to_record;
        e_s = tag->encoder_total_frames_recorded;
    }

    char *ptr = str;
    ptr = vj_sprintf( ptr, pfps ); 
    ptr = vj_sprintf( ptr, frame );
    ptr = vj_sprintf( ptr, mode );
    ptr = vj_sprintf( ptr, tag_id );
    ptr = vj_sprintf( ptr, tag->effect_toggle );
    ptr = vj_sprintf( ptr, tag->color_r );
    ptr = vj_sprintf( ptr, tag->color_g );
    ptr = vj_sprintf( ptr, tag->color_b );
    *ptr++ = '0';
    *ptr++ = ' ';
    
    ptr = vj_sprintf( ptr, e_a );
    ptr = vj_sprintf( ptr, e_d );
    ptr = vj_sprintf( ptr, e_s );
    ptr = vj_sprintf( ptr, vj_tag_size() );
    ptr = vj_sprintf( ptr, tag->source_type ); 
    ptr = vj_sprintf( ptr, tag->n_frames ); 
    ptr = vj_sprintf( ptr, tag->selected_entry );
    ptr = vj_sprintf( ptr, ts );
    ptr = vj_sprintf( ptr, cache );
    ptr = vj_sprintf( ptr, curfps );
    ptr = vj_sprintf( ptr, (int) lo );
    ptr = vj_sprintf( ptr, (int) hi );
    ptr = vj_sprintf( ptr, sa );
    ptr = vj_sprintf( ptr, ca );
    ptr = vj_sprintf( ptr, (int) tag->fader_val );
    *ptr++ = '0'; 
    *ptr++ = ' ';
    ptr = vj_sprintf( ptr, macro ); 
    ptr = vj_sprintf( ptr, tag->subrender);
    ptr = vj_sprintf( ptr, tag->fade_method);
    ptr = vj_sprintf( ptr, tag->fade_entry );
    ptr = vj_sprintf( ptr, tag->fade_alpha );
    ptr = vj_sprintf( ptr, tag->loop_stat );
    ptr = vj_sprintf( ptr, tag->loop_stat_stop);
    ptr = vj_sprintf( ptr, tag->transition_active);
    ptr = vj_sprintf( ptr, tag->transition_length);
    ptr = vj_sprintf( ptr, tag->transition_shape);
    ptr = vj_sprintf( ptr, feedback );
    ptr = vj_sprintf( ptr, samples );

    return 0;
}

#ifdef HAVE_XML2
static void tagParseArguments(xmlDocPtr doc, xmlNodePtr cur, int *arg)
{
    int argIndex = 0;
    if (cur == NULL)
        return;

    while (cur != NULL && argIndex < SAMPLE_MAX_PARAMETERS)
    {
        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_ARGUMENT))
        {
            arg[argIndex] = get_xml_int( doc, cur );
            argIndex++;
        }
        cur = cur->next;
    }
}
static  int tagParseKeys( xmlDocPtr doc, xmlNodePtr cur, void *port )
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

    veejay_memset( arg, 0, sizeof(arg));

    if (cur == NULL)
        return;

    xmlNodePtr curarg = cur;

    while (cur != NULL)
    {
        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTID)) {
            effect_id = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTPOS)) {
            chain_index = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTSOURCE)) {
            source_type = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTCHANNEL)) {
            channel = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTTRIMMER)) {
            frame_trimmer = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTOFFSET)) {
            frame_offset = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) "kf_status")) {
            anim = get_xml_int( doc, cur );
        }
        
        if (!xmlStrcmp(cur->name, (const xmlChar *) "kf_type")) {
            anim_type = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTACTIVE)) {
            e_flag = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_ARGUMENTS)) {
            tagParseArguments(doc, cur->xmlChildrenNode, arg );
        }

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
            for (j = 0; j < vje_get_num_params(effect_id); j++) {
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

static void tagParseCalibration( xmlDocPtr doc, xmlNodePtr cur, int dst_sample , void *vp)
{
}

/*************************************************************************************************
 *
 * ParseSample()
 *
 * Parse a sample
 *
 ****************************************************************************************************/
void tagParseStreamFX(char *sampleFile, xmlDocPtr doc, xmlNodePtr cur, void *font, void *vp)
{
    int fx_on=0, id=0, source_id=0, source_type=0;
    char *source_file = NULL;
    char *extra_data = NULL;
    int col[3] = {0,0,0};
    int fader_active=0, fader_val=0, fader_dir=0, fade_method=0,fade_alpha = 0,fade_entry = -1,opacity=0, nframes=0, loop_stat_stop=0;
    xmlNodePtr fx[32];
    veejay_memset( fx, 0, sizeof(fx));
    int k = 0;
    int subrender = 0;
    xmlNodePtr subs = NULL;
    xmlNodePtr cali = NULL;
    xmlNodePtr macro = NULL;
    void *d = vj_font_get_dict( font );

    while (cur != NULL)
    {
        if( !xmlStrcmp(cur->name, (const xmlChar*) XMLTAG_SAMPLEID ))
            id = get_xml_int( doc, cur );
        if( !xmlStrcmp(cur->name, (const xmlChar*) "source_id" ) )
            source_id = get_xml_int(doc,cur);
        if( !xmlStrcmp(cur->name, (const xmlChar*) "source_type" ) )
            source_type = get_xml_int(doc,cur);
        if( !xmlStrcmp(cur->name, (const xmlChar*) "source_file" ) )
            source_file = get_xml_str(doc,cur);
        if( !xmlStrcmp(cur->name, (const xmlChar*) "extra_data" ))
            extra_data = get_xml_str(doc,cur);

        if(! xmlStrcmp(cur->name, (const xmlChar*) "red" ) )
            col[0] = get_xml_int(doc,cur);
        if(! xmlStrcmp(cur->name, (const xmlChar*) "green" ) )
            col[1] = get_xml_int(doc,cur);
        if(! xmlStrcmp(cur->name, (const xmlChar*) "blue" ))
            col[2] = get_xml_int(doc,cur);
        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_CHAIN_ENABLED))
            fx_on = get_xml_int(doc,cur);
        if (!xmlStrcmp(cur->name,(const xmlChar *) XMLTAG_FADER_ACTIVE)) 
            fader_active = get_xml_int(doc,cur);
        if (!xmlStrcmp(cur->name,(const xmlChar *) XMLTAG_FADER_VAL)) 
            fader_val = get_xml_int(doc,cur);
        if (!xmlStrcmp(cur->name,(const xmlChar *) XMLTAG_FADE_METHOD)) 
            fade_method = get_xml_int(doc,cur);
        if (!xmlStrcmp(cur->name,(const xmlChar *) XMLTAG_FADE_ALPHA)) 
            fade_alpha = get_xml_int(doc,cur);
        if (!xmlStrcmp(cur->name,(const xmlChar *) XMLTAG_FADE_METHOD)) 
            fade_entry = get_xml_int(doc,cur);
        if (!xmlStrcmp(cur->name,(const xmlChar*) XMLTAG_FADER_DIRECTION)) 
            fader_dir = get_xml_int(doc,cur);
        if (!xmlStrcmp(cur->name,(const xmlChar*) "opacity" ) )
            opacity   = get_xml_int(doc,cur);
        if (!xmlStrcmp(cur->name,(const xmlChar*) "nframes" ) )
            nframes   = get_xml_int(doc,cur);

        if (!xmlStrcmp(cur->name, (const xmlChar*) "SUBTITLES" ))
            subs = cur->xmlChildrenNode;

        if (!xmlStrcmp(cur->name, (const xmlChar*) "calibration" ))
            cali = cur->xmlChildrenNode;

	if (!xmlStrcmp(cur->name, (const xmlChar*) XMLTAG_MACRO ))
	    macro = cur->xmlChildrenNode;

        if (!xmlStrcmp(cur->name, (const xmlChar*) "subrender" ))
            subrender = get_xml_int(doc,cur);
	if( !xmlStrcmp(cur->name, (const xmlChar*) "loop_stat_stop"))
	    loop_stat_stop = get_xml_int(doc,cur);

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

        //vj_tag_del( id );
    
        int n_id = id;

        vj_tag *cur_tag = vj_tag_get( id );
        int identity = 0;
        if( cur_tag ) {
            char *cur_src_file = cur_tag->method_filename;
            int   type = cur_tag->source_type;
            if(cur_src_file && source_file) {
                if(strncasecmp(cur_tag->method_filename, source_file,strlen(cur_tag->method_filename)) == 0 && type == source_type) {
                    identity = 1;
                }
            }
        }

        if(!identity) {
            if(cur_tag)
                vj_tag_del(id);
            n_id = vj_tag_new( source_type, source_file, _tag_info->nstreams,_tag_info->current_edit_list,
                _tag_info->pixel_format, source_id,zer, _tag_info->settings->composite );
        }

        if(n_id > 0 )
        {
            vj_tag *tag = vj_tag_get( n_id );
            tag->id = id;
            tag->effect_toggle = fx_on;
            tag->fader_active = fader_active;
            tag->fader_val = fader_val;
            tag->fade_method = fade_method;
            tag->fade_alpha = fade_alpha;
            tag->fade_entry = fade_entry;
            tag->fader_direction = fader_dir;
            tag->opacity = opacity;
            tag->nframes = nframes;
            tag->subrender = subrender;
	    tag->loop_stat_stop = loop_stat_stop;

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

	    if( macro ) 
	    {
		vj_macro_load( tag->macro, doc, macro );
	    	int lss = vj_macro_get_loop_stat_stop(tag->macro);
		if( lss > tag->loop_stat_stop ) {
			tag->loop_stat_stop = lss;
		}
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
    for (i = 0; i < argcount; i++) {
        put_xml_int( node, XMLTAG_ARGUMENT, arg[i] );
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
    xmlNodePtr childnode;

    put_xml_int( node, XMLTAG_EFFECTPOS, position );
    put_xml_int( node, XMLTAG_EFFECTID, effect->effect_id );
    put_xml_int( node, XMLTAG_EFFECTACTIVE, effect->e_flag );
    put_xml_int( node, XMLTAG_EFFECTSOURCE, effect->source_type );
    put_xml_int( node, XMLTAG_EFFECTCHANNEL, effect->channel );
    put_xml_int( node, XMLTAG_EFFECTOFFSET, effect->frame_offset );
    put_xml_int( node, XMLTAG_EFFECTTRIMMER, effect->frame_trimmer );
    put_xml_int( node, XMLTAG_EFFECTAUDIOFLAG, effect->a_flag );
    put_xml_int( node, XMLTAG_EFFECTAUDIOVOLUME, effect->volume );
    put_xml_int( node, "kf_status", effect->kf_status );
    put_xml_int( node, "kf_type", effect->kf_type );

    childnode = xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_ARGUMENTS, NULL);
    tagCreateArguments(childnode, effect->arg,vje_get_num_params(effect->effect_id));

    childnode = xmlNewChild(node, NULL, (const xmlChar*) "ANIM", NULL );
    tagCreateKeys( childnode, vje_get_num_params(effect->effect_id), effect->kf ); 
}

static void tagCreateEffects(xmlNodePtr node, sample_eff_chain ** effects)
{
    int i;
    xmlNodePtr childnode;

    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
        if (effects[i]->effect_id != -1) {
            childnode = xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECT,NULL);
            tagCreateEffect(childnode, effects[i], i);
        }
    }
}
    
void tagCreateStream(xmlNodePtr node, vj_tag *tag, void *font, void *vp)
{   
    put_xml_int( node, XMLTAG_SAMPLEID, tag->id );
    put_xml_int( node, XMLTAG_CHAIN_ENABLED, tag->effect_toggle );
    put_xml_int( node, "source_type", tag->source_type );
    put_xml_int( node, "source_id", tag->video_channel );
    put_xml_str( node, "source_file", tag->source_name );
    put_xml_int( node, "subrender", tag->subrender );
    put_xml_int( node, "loop_stat_stop", tag->loop_stat_stop );
    if(tag->extra )
    {
        put_xml_str( node, "extra_data", tag->extra );
    }
    
    put_xml_int( node, "red", tag->color_r );
    put_xml_int( node, "green", tag->color_g );
    put_xml_int( node, "blue", tag->color_b );

    put_xml_int( node, "nframes", tag->nframes );
    put_xml_int( node, "opacity", tag->opacity );

    vj_font_xml_pack( node, font );

    xmlNodePtr childnode =  xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTS, NULL);

    tagCreateEffects(childnode, tag->effect_chain);

    childnode = xmlNewChild( node, NULL, (const xmlChar*) XMLTAG_MACRO, NULL );

    vj_macro_store( tag->macro, childnode );

}

void    tag_writeStream( char *file, int n, xmlNodePtr node, void *font, void *vp )
{
    vj_tag *tag = vj_tag_get(n);
    if(!tag) {
         veejay_msg(VEEJAY_MSG_ERROR, "Stream %d does not exist", n);
         return;
    }
    char tmp[512];
    void *d = vj_font_get_dict( font );
    snprintf(tmp,sizeof(tmp), "%s-SUB-s%d.srt", file,tag->id );

    if( tag->dict )
    {
        vj_font_set_dict( font, tag->dict );
        vj_font_save_srt( font, tmp );

        vj_font_set_dict( font, d );
    }

    tagCreateStream(node,  tag , font,vp);
}

#endif
