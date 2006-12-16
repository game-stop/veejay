/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nelburg@looze.net>
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
#include <linux/videodev.h>
#define VIDEO_PALETTE_YUV420P 15
#define VIDEO_PALETTE_YUV422P 13

#ifdef USE_GDK_PIXBUF
#include <libel/pixbuf.h>
#endif

#ifdef SUPPORT_READ_DV2
#include <libstream/vj-dv1394.h>
#endif
#include <libvjmsg/vj-common.h>
#include <libstream/vj-shm.h>
#include <libel/vj-avformat.h>
#include <libel/vj-avcodec.h>
#include <libvjnet/vj-client.h>
#include <libvjnet/common.h>
#include <veejay/vims.h>
#include <veejay/vj-lib.h>
#include <veejay/vj-misc.h>
#include <libvjmem/vjmem.h>
#include <libvje/internal.h>
#include <libstream/vj-net.h>
#include <libstream/vj-unicap.h>
#include <libvevo/libvevo.h>

static veejay_t *_tag_info = NULL;
static hash_t *TagHash = NULL;
static int this_tag_id = 0;
static vj_tag_data *vj_tag_input;
static int next_avail_tag = 0;
static int avail_tag[SAMPLE_MAX_SAMPLES];
static int last_added_tag = 0;
static void *unicap_data_= NULL;
//forward decl

int _vj_tag_new_net(vj_tag *tag, int stream_nr, int w, int h,int f, char *host, int port, int p, int ty );
int _vj_tag_new_avformat( vj_tag *tag, int stream_nr, editlist *el);
int _vj_tag_new_yuv4mpeg(vj_tag * tag, int stream_nr, editlist * el);

static uint8_t *_temp_buffer[3];
static uint8_t *tag_encoder_buf = NULL; 
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

static inline hash_val_t int_tag_hash(const void *key)
{
    return (hash_val_t) key;
}

static inline int int_tag_compare(const void *key1, const void *key2)
{
    return ((int) key1 < (int) key2 ? -1 :
	    ((int) key1 > (int) key2 ? +1 : 0)
	);

}

vj_tag *vj_tag_get(int id)
{
    if (id <= 0 || id > this_tag_id) {
	return NULL;
    }
    hnode_t *tag_node = hash_lookup(TagHash, (void *) id);
    if (!tag_node) {
	return NULL;
	}
    return (vj_tag *) hnode_get(tag_node);
}

int vj_tag_put(vj_tag * tag)
{
    hnode_t *tag_node;
    if (!tag)
	return 0;
    tag_node = hnode_create(tag);
    if (!tag_node)
	return 0;


    if (!vj_tag_exists(tag->id)) {
	hash_insert(TagHash, tag_node, (void *) tag->id);
    } else {
	hnode_put(tag_node, (void *) tag->id);
	hnode_destroy(tag_node);

    }
    return 1;
}

static int vj_tag_update(vj_tag *tag, int id) {
  if(tag) {
    hnode_t *tag_node = hnode_create(tag);
    if(!tag_node) return -1;
    hnode_put(tag_node, (void*) id);
    hnode_destroy(tag_node);
    return 1;
  }
  return -1;
}

int	vj_tag_num_devices()
{
	return vj_unicap_num_capture_devices( unicap_data_ );
}

char *vj_tag_scan_devices( void )
{
	const char *default_str = "000000";
	char *n = NULL;
	char **device_list = vj_unicap_get_devices(unicap_data_);
	if(!device_list)
	{
			return strdup(default_str);
	}
	int i;
    int len = 6;
	for( i = 0; device_list[i] != NULL ;i++ )
			len += strlen( device_list[i] );
	
	n = (char*) malloc(sizeof(char) * len );
	memset( n, 0, len );

	sprintf(n, "%06d",len-6);	
	for( i = 0; device_list[i] != NULL ;i++ )
	{
		strcat( n, device_list[i] );
	}

	return n;	
}

int vj_tag_init(int width, int height, int pix_fmt)
{
    int i;
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


    if( pix_fmt == FMT_420|| pix_fmt == FMT_420F)
	    vj_tag_input->uv_len = (width*height) / 4;
    else
	    vj_tag_input->uv_len = (width*height) / 2;

    memset( &_tmp, 0, sizeof(VJFrame));
    _tmp.len = width * height;

	unicap_data_= (void*) vj_unicap_init();
   _temp_buffer[0] = (uint8_t*) malloc(sizeof(uint8_t)*width*height);
   _temp_buffer[1] = (uint8_t*) malloc(sizeof(uint8_t)*width*height);
   _temp_buffer[2] = (uint8_t*) malloc(sizeof(uint8_t)*width*height);
		

    if( pix_fmt == FMT_422 || pix_fmt == FMT_422F )
	{
		_tmp.uv_width = width; 
		_tmp.uv_height = height/2;
		_tmp.uv_len = width * (height/2);
	}
	else
	{
		_tmp.uv_width = width / 2;
		_tmp.uv_height= height / 2;
		_tmp.uv_len = (width * height)/4;
	}	

    for(i=0; i < SAMPLE_MAX_SAMPLES; i++) {
	avail_tag[i] = 0;
    }
    return 0;
}

void vj_tag_record_init(int w, int h)
{
  if(tag_encoder_buf) free(tag_encoder_buf);
  tag_encoder_buf = NULL;
  tag_encoder_buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3);
  if(!tag_encoder_buf)
  {
	veejay_msg(VEEJAY_MSG_ERROR,
		   "Error allocating memory for stream recorder\n");
	return;
  }
  memset( tag_encoder_buf, 0 , (w*h) );
}



int _vj_tag_new_net(vj_tag *tag, int stream_nr, int w, int h,int f, char *host, int port, int p, int type )
{
	vj_client *v;
	if( !host  ) return 0;
	if( port <= 0 ) return 0;
	if(stream_nr < 0 || stream_nr > VJ_TAG_MAX_STREAM_IN) return 0;

	vj_tag_input->net[stream_nr] = vj_client_alloc(w,h,f);
	v = vj_tag_input->net[stream_nr];
	if(!v) return 0;

	char tmp[255];
	bzero(tmp,255);
	snprintf(tmp,sizeof(tmp)-1, "%s %d", host, port );
	tag->extra = (void*) strdup(tmp);

	v->planes[0] = w * h;
	int fmt=  vj_tag_input->pix_fmt;
	if( fmt == FMT_420 || fmt == FMT_420F )
	{
		v->planes[1] = v->planes[0] / 4;
		v->planes[2] = v->planes[0] / 4;
	}	
	else
	{	
		v->planes[1] = v->planes[0] / 2;	
		v->planes[2] = v->planes[0] / 2;
	}
	if( tag->socket_ready == 0 )
	{
		tag->socket_frame = (uint8_t*) vj_malloc(sizeof(uint8_t) * v->planes[0] * 4);
		if(!tag->socket_frame) 
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Insufficient error to allocate memory for Network Stream");
			return 0;
		}
		veejay_memset(tag->socket_frame, 0 , (v->planes[0] * 4 ));
		tag->socket_ready = 1;
	}

	return 1;
}

int _vj_tag_new_unicap( vj_tag * tag, int stream_nr, int width, int height,
		    int norm, int palette, int freq, int channel)
{
  	if (stream_nr < 0 || stream_nr > vj_tag_num_devices())
	{
		return 0;
	}
    	vj_tag_input->unicap[stream_nr] = 
	   vj_unicap_new_device( unicap_data_, freq );
    	if(!vj_tag_input->unicap[stream_nr] )
    	{
	    veejay_msg(0,"Unable to open device %d", channel);
	    return 0;
    	}
	
	if(!vj_unicap_configure_device(   vj_tag_input->unicap[stream_nr] ,
			   palette, width,height))
	{
		veejay_msg(0,"Unable to configure device %d",channel);
	   	vj_unicap_free_device( vj_tag_input->unicap[stream_nr] );
	   	return 0;
   	}
	else
		veejay_msg(VEEJAY_MSG_DEBUG, "Configured device %d", channel);


	char **props = vj_unicap_get_list(
			 vj_tag_input->unicap[stream_nr]  );
	int i;
	double v = 0.0;
	for( i = 0; props[i] != NULL ; i ++ )
	{
		if(strncasecmp("video norm", props[i],10 ) == 0 )
		{
			if( norm == 'p' || norm == 'P' )
				vj_unicap_select_value(  vj_tag_input->unicap[tag->index],
					UNICAP_PAL,0);
			else
				vj_unicap_select_value(  vj_tag_input->unicap[tag->index],
					UNICAP_NTSC,0 );	
		}
		else if (strncasecmp( "video source", props[i], 12 ) == 0 )
		{
			int cha =  channel + UNICAP_SOURCE0;
			if(cha <=UNICAP_SOURCE4)
				vj_unicap_select_value( vj_tag_input->unicap[tag->index],
					cha,0 );
		}	
		free(props[i]);
	}
	free(props);
	
	//set channel
	
	return 1;
}

int _vj_tag_new_avformat( vj_tag *tag, int stream_nr, editlist *el)
{
	int stop = 0;
	if(stream_nr < 0 || stream_nr > VJ_TAG_MAX_STREAM_IN) return 0;

	vj_tag_input->avformat[stream_nr] = vj_avformat_open_input( (const char*)tag->source_name );	
	if(vj_tag_input->avformat[stream_nr]==NULL)
	{
		return 0;
	}
	veejay_msg(VEEJAY_MSG_INFO, "Opened [%s] , %d x %d @ %2.2f fps ",
			tag->source_name,
			vj_avformat_get_video_width( vj_tag_input->avformat[stream_nr]),
			vj_avformat_get_video_height( vj_tag_input->avformat[stream_nr]),
			vj_avformat_get_video_fps( vj_tag_input->avformat[stream_nr]  ));

	if( vj_avformat_get_video_width( vj_tag_input->avformat[stream_nr] ) != el->video_width)
		stop = 1;
	if( vj_avformat_get_video_height(vj_tag_input->avformat[stream_nr] ) != el->video_height)
		stop = 1;
	if( vj_avformat_get_video_fps(vj_tag_input->avformat[stream_nr] ) != el->video_fps)
		stop = 1;
	
	if(stop)
	{
		vj_avformat_close_input( vj_tag_input->avformat[stream_nr]);
		return 0;
	}	

	return 1;
}
#ifdef USE_GDK_PIXBUF
int _vj_tag_new_picture( vj_tag *tag, int stream_nr, editlist *el)
{
	int stop = 0;
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
			el->video_width, el->video_height, el->video_fps );

	return 1;
}
#endif

int _vj_tag_new_yuv4mpeg(vj_tag * tag, int stream_nr, editlist * el)
{
    if (stream_nr < 0 || stream_nr > VJ_TAG_MAX_STREAM_IN)
	return 0;
    vj_tag_input->stream[stream_nr] = vj_yuv4mpeg_alloc(el,el->video_width,el->video_height);

    if(vj_tag_input->stream[stream_nr] == NULL) 
	return 0;

    if(vj_yuv_stream_start_read(vj_tag_input->stream[stream_nr],tag->source_name,
			 el->video_width, el->video_height) != 0) 
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

int	vj_tag_set_stream_color(int t1, int r, int g, int b)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
	return 0;
    if(tag->source_type != VJ_TAG_TYPE_COLOR)
	return 0;

	veejay_msg(VEEJAY_MSG_DEBUG,"Set stream %d color %d,%d,%d",t1,
		r,g, b );
 
    tag->color_r = r;
    tag->color_g = g;
    tag->color_b = b;

    return (vj_tag_update(tag,t1));
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
// for network, filename /channel is passed as host/port num
int vj_tag_new(int type, char *filename, int stream_nr, editlist * el,
	        int pix_fmt, int channel , int extra)
{
    int i, j;
    int palette;
    int w = el->video_width;
    int h = el->video_height;	/* FIXME */
    int id = 0;
    int n;
    char sourcename[255];
 
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
		    return -1;
    }
    tag = (vj_tag *) vj_malloc(sizeof(vj_tag));
	if(!tag)
		return -1; 

    tag->source_name = (char *) malloc(sizeof(char) * 255);
    if (!tag->source_name)
		return -1;
    bzero(tag->source_name, 255 );

     /* see if we can reclaim some id */
    for(i=0; i <= next_avail_tag; i++) {
	if(avail_tag[i] != 0) {
 	  hnode_t *tag_node;
  	  tag_node = hnode_create(tag);
   	  if (!tag_node)
		return -1;

	  id = avail_tag[i];
	  avail_tag[i] = 0;
	  hash_insert(TagHash, tag_node, (void *) id);
	  break;
        }
    }

         
    if(id==0) { 
		tag->id = this_tag_id;
		this_tag_id++;
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
	tag->encoder_succes_frames = 0;
    tag->encoder_duration = 0;
    tag->encoder_width = 0;
    tag->encoder_height = 0;
    tag->encoder_num_frames = 0;
    tag->method_filename = (filename == NULL ? NULL :strdup(filename));
    tag->rec_total_bytes = 0;
    tag->encoder_total_frames = 0;
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
    tag->color_r = 0;
    tag->color_g = 0;
    tag->color_b = 0;
    tag->opacity = 0; 
	tag->priv = NULL;

	if(type == VJ_TAG_TYPE_MCAST || type == VJ_TAG_TYPE_NET)
	    tag->priv = net_threader();
	
    palette = ( (pix_fmt == FMT_420||pix_fmt == FMT_420F) ? VIDEO_PALETTE_YUV420P : VIDEO_PALETTE_YUV422P);
    
 
    switch (type) {
	    case VJ_TAG_TYPE_V4L:
		sprintf(tag->source_name, "%s", filename );

		if (!_vj_tag_new_unicap
		    (tag, stream_nr, w, h,el->video_norm, pix_fmt, extra,channel ))
			    return -1;
		
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
//	FIXME: dev fs
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
    case VJ_TAG_TYPE_AVFORMAT:
	sprintf(tag->source_name, "%s", filename);
	if( _vj_tag_new_avformat( tag,stream_nr, el ) != 1 )
		return -1;
	tag->active = 1;
	break;
#ifdef USE_GDK_PIXBUF
	case VJ_TAG_TYPE_PICTURE:
	sprintf(tag->source_name, "%s", filename);
	if( _vj_tag_new_picture(tag, stream_nr, el) != 1 )
		return -1;
	break;
#endif
    case VJ_TAG_TYPE_YUV4MPEG:
	sprintf(tag->source_name, "%s", filename);
	if (_vj_tag_new_yuv4mpeg(tag, stream_nr, el) != 1)
	{
	    if(tag->source_name) free(tag->source_name);
	    if(tag) free(tag);
	    return -1;
	}
	tag->active = 1;
	break;
    case VJ_TAG_TYPE_SHM:
	sprintf(tag->source_name, "%s", "SHM");  
	veejay_msg(VEEJAY_MSG_INFO, "Opened SHM as Stream");
	break;
/*
    case VJ_TAG_TYPE_WHITE:
    case VJ_TAG_TYPE_BLACK:
    case VJ_TAG_TYPE_RED:
    case VJ_TAG_TYPE_GREEN:
    case VJ_TAG_TYPE_YELLOW:
    case VJ_TAG_TYPE_BLUE:
*/
	case VJ_TAG_TYPE_COLOR:
	sprintf(tag->source_name, "[%d,%d,%d]",
		tag->color_r,tag->color_g,tag->color_b );
	tag->active = 1;	
	break;

    default:
	return -1;
    }

	vj_tag_get_by_type( tag->source_type, tag->descr);

    /* effect chain is empty */
    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++)
	{
		tag->effect_chain[i] =
		    (sample_eff_chain *) vj_malloc(sizeof(sample_eff_chain));
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
    }
    if (!vj_tag_put(tag))
		return -1;
    last_added_tag = tag->id; 

#ifdef HAVE_FREETYPE
    tag->dict = vpn(VEVO_ANONYMOUS_PORT );
#endif
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
	veejay_msg(VEEJAY_MSG_INFO, "Dereferenced mix entry %d of Stream %d",
		j, i );
					vj_tag_update( s, i );
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
   
#ifdef HAVE_TRUETYPE
	vj_font_dictionary_destroy(tag->dict);
#endif
    
    if (!tag)
	return 0;
	if(tag->extra)
		free(tag->extra);

    /* stop streaming in first */
    switch(tag->source_type) {
	case VJ_TAG_TYPE_V4L: 
		veejay_msg(VEEJAY_MSG_INFO, "Closing unicap device");
	   	vj_unicap_free_device(  vj_tag_input->unicap[tag->index] );
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
     case VJ_TAG_TYPE_AVFORMAT:
		veejay_msg(VEEJAY_MSG_INFO, "Closing avformat stream %s", tag->source_name);
		vj_avformat_close_input( vj_tag_input->avformat[tag->index]);
	break;
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
	case VJ_TAG_TYPE_SHM:
		veejay_msg(VEEJAY_MSG_INFO, "huh ?");
		break;
	case VJ_TAG_TYPE_MCAST:
	case VJ_TAG_TYPE_NET:
		net_thread_stop(tag);	
		if(vj_tag_input->net[tag->index])
		{
			vj_client_close( vj_tag_input->net[tag->index] );
			vj_tag_input->net[tag->index] = NULL;
		}
		break;
    }

    tag_node = hash_lookup(TagHash, (void *) tag->id);
    if(tag_node)
	{
        if(tag->encoder_active)
		vj_tag_stop_encoder( tag->id );	
        if(tag->source_name) free(tag->source_name);
	if(tag->method_filename) free(tag->method_filename);
      	for (i = 0; i < SAMPLE_MAX_EFFECTS; i++) 
		if (tag->effect_chain[i])
		    free(tag->effect_chain[i]);

      	if(tag) free(tag);
      	avail_tag[ next_avail_tag] = id;
      	next_avail_tag++;
      	hash_delete(TagHash, tag_node);
      return 1;
  }
  return -1;
}

void vj_tag_close_all() {
   int n=vj_tag_size()-1;
   int i;
   vj_tag *tag;

   for(i=1; i < n; i++) {
    tag = vj_tag_get(i);
    if(tag) {
    	if(vj_tag_del(i)) veejay_msg(VEEJAY_MSG_DEBUG, "Deleted stream %d", i);
	}
   }
  
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
  return ( vj_tag_update(tag, t1)); 
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
	return ( vj_tag_update(tag, t1)) ;
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
  return (vj_tag_update(tag,t1));
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
  return (vj_tag_update(tag,t1));
}

int vj_tag_set_fader_val(int t1, float val) {
  vj_tag *tag = vj_tag_get(t1);
  if(!tag) return -1;
  tag->fader_val = val;
  return ( vj_tag_update(tag,t1));
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
  vj_tag_update(tag,t1);
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
  return ( vj_tag_update(tag, t1));
}

int vj_tag_stop_encoder(int t1) {
   vj_tag *tag = vj_tag_get(t1);
   if(!tag)
   {
	 veejay_msg(VEEJAY_MSG_ERROR, "Tag %d does not exist", t1);
	 return -1;
   }
   if(tag->encoder_active) {

	       lav_close(tag->encoder_file);
	       vj_avcodec_stop( tag->encoder, tag->encoder_format );
   
	/* free memory now, it is not needed anymore */
	/* clean up internal variables */
 	tag->encoder_active = 0;
	return (vj_tag_update(tag,t1));
   }

   return 0;
}

void vj_tag_reset_encoder(int t1)
{
   vj_tag *tag = vj_tag_get(t1);
   if(!tag) return;
   tag->encoder_format = 0;
   tag->encoder_succes_frames = 0;
   tag->encoder_num_frames = 0;
   tag->encoder_width = 0;
   tag->encoder_height = 0;
   tag->encoder_max_size = 0;
   tag->encoder_active = 0;
   tag->rec_total_bytes = 0;

   vj_tag_update(tag,t1);
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
		case ENCODER_DVVIDEO:
			sprintf(ext,"dv");
			break;
		default:
			sprintf(ext,"avi");
			break;
	}	
	
	sprintf(tag->encoder_destination, "%s-%03d.%s", tag->encoder_base, (int)tag->sequence_num, ext);
	return (vj_tag_update(tag,t1));
}



static int vj_tag_start_encoder(vj_tag *tag, int format, long nframes)
{
	char descr[100];
	char cformat = 'Y';
	int sample_id = tag->id;

	switch(format)
	{
		case ENCODER_QUICKTIME_DV:sprintf(descr,"DV2"); cformat='Q'; break;
		case ENCODER_QUICKTIME_MJPEG:	sprintf(descr, "MJPEG"); cformat='q'; break;
		case ENCODER_DVVIDEO: sprintf(descr, "DV2"); cformat='b';break;
		case ENCODER_MJPEG: sprintf(descr,"MJPEG"); cformat='a'; break;
		case ENCODER_YUV420: sprintf(descr, "YUV 4:2:0 YV12"); cformat='Y'; break;
		case ENCODER_YUV422: sprintf(descr, "YUV 4:2:2 Planar"); cformat='P'; break;
		case ENCODER_MPEG4: sprintf(descr, "MPEG4"); cformat='M'; break;
		case ENCODER_DIVX: sprintf(descr, "DIVX"); cformat='D'; break;
		case ENCODER_LZO:  sprintf(descr, "LZO YUV"); cformat = 'L'; break;
	
		default:
		   veejay_msg(VEEJAY_MSG_ERROR, "Unsupported video codec");
		   return 0;
                break;
	}
	
	tag->encoder =  vj_avcodec_start( _tag_info->edit_list , format );
	if(!tag->encoder)
		return 0;

	tag->encoder_active = 1;
	tag->encoder_format = format;
	if(format==ENCODER_DVVIDEO)
		tag->encoder_max_size = ( _tag_info->edit_list->video_height == 480 ? 120000: 144000);
	else
		switch(format)
		{
			case ENCODER_YUV420:
			tag->encoder_max_size = (_tag_info->edit_list->video_width * _tag_info->edit_list->video_height  * 2 );break;
			case ENCODER_YUV422:
			tag->encoder_max_size = (_tag_info->edit_list->video_width * _tag_info->edit_list->video_height * 2); break;
			case ENCODER_LZO:
			tag->encoder_max_size = (_tag_info->edit_list->video_width * _tag_info->edit_list->video_height * 3); break;
			default:
			tag->encoder_max_size = ( 4 * 65535 );
			break;
		}

	if(tag->encoder_total_frames == 0)
	{
		tag->encoder_duration = nframes ; 
		tag->encoder_num_frames = 0;
	}
	else
	{
		tag->encoder_duration = tag->encoder_duration - tag->encoder_num_frames;
	}

	if( sufficient_space( tag->encoder_max_size, tag->encoder_num_frames ) == 0 )
	{
		vj_avcodec_close_encoder(tag->encoder );
		tag->encoder_active = 0;
		return 0;
	}

	
	tag->encoder_file = lav_open_output_file(
			tag->encoder_destination,
			cformat,
			_tag_info->edit_list->video_width,
			_tag_info->edit_list->video_height,
			_tag_info->edit_list->video_inter,
			_tag_info->edit_list->video_fps,
			0,
			0,
			0
		);


	if(!tag->encoder_file)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Cannot write to %s (%s)",tag->encoder_destination,
		lav_strerror());
		vj_avcodec_close_encoder( tag->encoder );
		return 0;
	}
	else
		veejay_msg(VEEJAY_MSG_INFO, "Recording to %s file [%s] %ldx%ld@%2.2f %d/%d/%d >%09ld<",
		    descr,
		    tag->encoder_destination, 
		    _tag_info->edit_list->video_width,
		    _tag_info->edit_list->video_height,
		    (float) _tag_info->edit_list->video_fps,
			0,0,0,
			(long)( tag->encoder_duration - tag->encoder_total_frames)
		);


	tag->rec_total_bytes= 0;
	tag->encoder_succes_frames = 0;
	tag->encoder_width = _tag_info->edit_list->video_width;
	tag->encoder_height = _tag_info->edit_list->video_height;
	
	return vj_tag_update(tag,sample_id);
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
  /* todo: clean this mess up */

  return vj_tag_start_encoder( tag,format, nframes );
}

int vj_tag_continue_record( int t1 )
{
	vj_tag *si = vj_tag_get(t1);
	if(!si) return -1;

	if( si->rec_total_bytes == 0) return 0;

	if(si->encoder_num_frames >= si->encoder_duration)
	{
		veejay_msg(VEEJAY_MSG_INFO, "Ready recording %ld frames", si->encoder_succes_frames);
		si->encoder_total_frames  = 0;
		vj_tag_update(si, t1 );
		return 1;
	}

	// 2 GB barrier
	if (si->rec_total_bytes  >= VEEJAY_FILE_LIMIT)
	{
		veejay_msg(VEEJAY_MSG_INFO, "Auto splitting files (reached internal 2GB barrier)");
		si->sequence_num ++;
		si->rec_total_bytes = 0;
	//	si->encoder_duration
	//	reset some variables

		printf(" %d %ld %d (%ld)%ld \n",
			(int)si->sequence_num,
			si->rec_total_bytes,
			si->encoder_num_frames,
			si->encoder_total_frames,
			si->encoder_duration);

		si->encoder_total_frames = 0;
		vj_tag_update(si,t1);
		return 2;
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
		vj_unicap_select_value( vj_tag_input->unicap[tag->index],UNICAP_BRIGHTNESS,(double)value);
	}
	return 1;
}
int vj_tag_set_saturation(int t1, int value)
{
	vj_tag *tag = vj_tag_get(t1);

	if(!tag) return -1;
	if(value < 0 || value > 65535)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Saturation valid range is 0 - 65535");
		return -1;
	}
	else
	{
		vj_unicap_select_value( vj_tag_input->unicap[tag->index],UNICAP_SATURATION,(double) value );
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
		vj_unicap_select_value( vj_tag_input->unicap[tag->index],UNICAP_WHITE,(double) value );
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
	else
	{
		vj_unicap_select_value( vj_tag_input->unicap[tag->index],UNICAP_HUE, (double)value );
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
		vj_unicap_select_value( vj_tag_input->unicap[tag->index],UNICAP_CONTRAST, (double) value);
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
		vj_unicap_select_value( vj_tag_input->unicap[tag->index], UNICAP_COLOR, (double)value);
	}
	return 1;
}


int	vj_tag_get_v4l_properties(int t1,
		int *brightness, int *contrast, int *hue,int *saturation, int *color, int *white )
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	if(tag->source_type!=VJ_TAG_TYPE_V4L)
	{
		return -1;
	}
    	char **props = vj_unicap_get_list( vj_tag_input->unicap[tag->index] );
        int i;
        for( i = 0; props[i] != NULL ; i ++ )
        {
                double dvalue = 0.0;
                int n = vj_unicap_get_value( vj_tag_input->unicap[tag->index] ,
				props[i], VEVO_ATOM_TYPE_DOUBLE, &dvalue );
		if(strncasecmp( props[i], "brightness",8) == 0 ) {
			*brightness = (int) dvalue;
		} else if(strncasecmp( props[i], "hue",3 ) == 0 ) {
			*hue = (int) dvalue;
		} else if( strncasecmp( props[i], "contrast", 8) == 0 ){
			*contrast = (int) dvalue;
		} else if (strncasecmp( props[i], "white", 5 ) == 0 ){
			*white = (int) dvalue;
		} else if (strncasecmp( props[i], "saturation", 10)  == 0 ) {
		  	*saturation = (int) dvalue;     
		} else if (strncasecmp( props[i], "color",5 ) == 0 ){
			*color = (int) dvalue;
		}
		free(props[i]);
	}
	free(props);
	return 0;
}


int vj_tag_get_effect_any(int t1, int position) {
	vj_tag *tag = vj_tag_get(t1);
	if (!tag) return -1;
#ifdef STRICT_CHECKING
	assert( position >= 0 && position < SAMPLE_MAX_EFFECTS );
#endif
//	if( position >= SAMPLE_MAX_EFFECTS) return -1;
	return tag->effect_chain[position]->effect_id;
}

int vj_tag_chain_malloc(int t1)
{
    vj_tag  *tag = vj_tag_get(t1);
    int i=0;
    int e_id = 0; 
    int sum =0;
    if (!tag)
	return -1;

    for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
    {
	e_id = tag->effect_chain[i]->effect_id;
	if(e_id!=-1)
	{
		if(!vj_effect_initialized(e_id))
		{
			sum ++;
			vj_effect_activate(e_id);
		}
	}
    } 
    return sum; 
}

int vj_tag_chain_free(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    int i=0;
    int e_id = 0; 
    int sum = 0;
    if (!tag)
	return -1;
    for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
    {
	e_id = tag->effect_chain[i]->effect_id;
	if(e_id!=-1)
	{
		if(vj_effect_initialized(e_id))
		{
			vj_effect_deactivate(e_id);
			sum++;
		}
	  }
	}  
    return sum;
}


int vj_tag_set_effect(int t1, int position, int effect_id)
{
    int params, i;
    vj_tag *tag = vj_tag_get(t1);

    if (!tag)
		return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
		return -1;

	if( tag->effect_chain[position]->effect_id != -1 && tag->effect_chain[position]->effect_id != effect_id )
    {
		//verify if the effect should be discarded
		if(vj_effect_initialized( tag->effect_chain[position]->effect_id ))
		{
			// it is using some memory, see if we can free it ...
			int ok = 1;
			for(i=(position+1); i < SAMPLE_MAX_EFFECTS; i++)
			{
				if( tag->effect_chain[i]->effect_id == tag->effect_chain[position]->effect_id) ok = 0;
			}
			// ok, lets get rid of it.
			if( ok ) vj_effect_deactivate( tag->effect_chain[position]->effect_id );
		}
    }

    if (!vj_effect_initialized(effect_id))
	{
		if(vj_effect_activate( effect_id ) == -1) return -1;
	}

    tag->effect_chain[position]->effect_id = effect_id;
    tag->effect_chain[position]->e_flag = 1;

    params = vj_effect_get_num_params(effect_id);
    if (params > 0) {
	for (i = 0; i < params; i++) {
	    int val = 0;
	    val = vj_effect_get_default(effect_id, i);
	    tag->effect_chain[position]->arg[i] = val;
	}
    }
    if (vj_effect_get_extra_frame(effect_id)) {
	if(tag->effect_chain[position]->source_type < 0)
	 tag->effect_chain[position]->source_type = tag->source_type;
	if(tag->effect_chain[position]->channel <= 0 )
	 tag->effect_chain[position]->channel = t1;
    }

    if (!vj_tag_update(tag,t1))
	return -1;
    return position;
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
    if (!vj_tag_update(tag,t1))
	return -1;
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
    if (!vj_tag_update(tag,t1))
	return -1;
    return 1;
}

int vj_tag_get_all_effect_args(int t1, int position, int *args,
			       int arg_len)
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
    for (i = 0; i < arg_len; i++)
	args[i] = tag->effect_chain[position]->arg[i];
   
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
    if (!vj_tag_update(tag,t1))
	return -1;
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
    if (!vj_tag_update(tag,t1))
	return -1;
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
	if(!vj_tag_update(tag,t1)) return -1;
	return 1;
}

int vj_tag_enable(int t1) {
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;

	veejay_msg(VEEJAY_MSG_INFO, "Enable stream %d", t1 );

	if(tag->active ) 
	{
		veejay_msg(VEEJAY_MSG_INFO, "Already active");
		return 1;
	}
	if(tag->source_type == VJ_TAG_TYPE_NET || tag->source_type == VJ_TAG_TYPE_MCAST )
	{
		if(!net_thread_start(vj_tag_input->net[tag->index], tag))
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
	if( tag->source_type == VJ_TAG_TYPE_V4L )
	{
	    vj_unicap_start_capture( vj_tag_input->unicap[tag->index]);
	}

	tag->active = 1;
	if(!vj_tag_update(tag,t1)) return -1;
	return 1;
}

int vj_tag_set_depth(int t1, int depth)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    tag->depth = depth;
    if (!vj_tag_update(tag,t1))
	return -1;
    return 1;
}


int vj_tag_set_active(int t1, int active)
{
    vj_tag *tag;
    tag  = vj_tag_get(t1);
    if (!tag)
	return -1;

    if(active == tag->active)
    {
	    veejay_msg(0, "Tag already %s", active ? "On" : "Off" );
	    return 1;
 	}
    veejay_msg(0, "%s: %d, active=%d", __FUNCTION__, tag->source_type, active );
    switch (tag->source_type) {
	   case VJ_TAG_TYPE_V4L:
		if(active)
		    vj_unicap_start_capture( vj_tag_input->unicap[tag->index]);
		else
		    vj_unicap_stop_capture( vj_tag_input->unicap[ tag->index] );
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

    if (!vj_tag_update(tag,t1))
	return -1;

    return 1;
}

int vj_tag_get_active(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    return tag->active;
}

int vj_tag_set_chain_channel(int t1, int position, int channel)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;
    tag->effect_chain[position]->channel = channel;

    if (!vj_tag_update(tag,t1))
	return -1;
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
    tag->effect_chain[position]->source_type = source;
    if (!vj_tag_update(tag,t1))
	return -1;
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
		return ( vj_tag_update(tag,t1));
	}
	return -1;
}

int vj_tag_set_selected_entry(int t1, int position) 
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return -1;
	if(position < 0 || position >= SAMPLE_MAX_EFFECTS) return -1;
	tag->selected_entry = position;
	return (vj_tag_update(tag,t1));
}

static int vj_tag_chain_can_delete(vj_tag *tag, int s_pos, int e_id)
{
	int i;
	for(i=0; i < SAMPLE_MAX_EFFECTS;i++)
	{
		// effect is on chain > 1
		if(e_id == tag->effect_chain[i]->effect_id && i != s_pos)
		{
			return 0;
		}	
	}
	return 1;
}
int vj_tag_chain_remove(int t1, int index)
{
    int i;
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;
    if( tag->effect_chain[index]->effect_id != -1)
    {
	if( vj_effect_initialized( tag->effect_chain[index]->effect_id ) && 
	    vj_tag_chain_can_delete(tag, index, tag->effect_chain[index]->effect_id))
		vj_effect_deactivate( tag->effect_chain[index]->effect_id );
    }

    if (tag->effect_chain[index]->effect_id != -1) {
	tag->effect_chain[index]->effect_id = -1;
	tag->effect_chain[index]->e_flag = 0;
	for (i = 0; i < SAMPLE_MAX_PARAMETERS; i++) {
	    vj_tag_set_effect_arg(t1, index, i, 0);
	}
    }
    if (!vj_tag_update(tag,t1))
	return -1;
    return 1;
}


void vj_tag_get_source_name(int t1, char *dst)
{
    vj_tag *tag = vj_tag_get(t1);
    if (tag) {
	sprintf(dst, tag->source_name);
    } else {
	vj_tag_get_description( tag->source_type, dst );
    }
}

void vj_tag_get_method_filename(int t1, char *dst)
{
    vj_tag *tag = vj_tag_get(t1);
    if (tag) {
	if(tag->method_filename != NULL) sprintf(dst, tag->method_filename);
    }
}


void	vj_tag_get_by_type(int type, char *description )
{
 	switch (type) {
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
	sprintf(description, "%s", "AVFormat");
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
    case VJ_TAG_TYPE_SHM:
	sprintf(description, "%s","SHM");
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
    if (!vj_tag_update(tag,t1))
	return 0;
    return 1;
}

int vj_tag_get_offset(int t1, int chain_entry)
{
    vj_tag *tag = vj_tag_get(t1);
    if (!tag)
	return -1;

    return tag->effect_chain[chain_entry]->frame_offset;
}

int vj_tag_get_encoded_frames(int s1) {
  vj_tag *si = vj_tag_get(s1);
  if(!si) return -1;
  return ( si->encoder_succes_frames );
  //return ( si->encoder_total_frames );
}

long vj_tag_get_duration(int s1)
{
	vj_tag *t = vj_tag_get(s1);
	if(!t) return -1;
	return ( t->encoder_duration );
}


long vj_tag_get_total_frames( int s1 )
{
  vj_tag *si = vj_tag_get(s1);
  if(!si) return -1;
  return ( si->encoder_total_frames );
//	return si->encoder_succes_frames;
}

int vj_tag_reset_autosplit(int s1)
{
  vj_tag *si = vj_tag_get(s1);
  if(!si) return -1;
  bzero( si->encoder_base, 255 );
  bzero( si->encoder_destination , 255 );
  si->sequence_num = 0;
  return (vj_tag_update(si,s1));  
}

int vj_tag_get_frames_left(int s1)
{
	vj_tag *si= vj_tag_get(s1);
	if(!si) return 0;
	return ( si->encoder_duration - si->encoder_total_frames );
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

int vj_tag_record_frame(int t1, uint8_t *buffer[3], uint8_t *abuff, int audio_size) {
   vj_tag *tag = vj_tag_get(t1);
   int buf_len = 0;
   if(!tag) return -1;

   if(!tag->encoder_active) return -1;

		/*
	int buf_len = encode_jpeg_raw( tag->encoder_buf, tag->encoder_max_size, 100, 0,0,tag->encoder_width,
		tag->encoder_height, buffer[0], buffer[1], buffer[2]);
		*/

   buf_len =	vj_avcodec_encode_frame( tag->encoder, tag->encoder_total_frames ++, tag->encoder_format, buffer, tag_encoder_buf, tag->encoder_max_size);
   if(buf_len <= 0 )
	{
		return -1;
	}

	if(lav_write_frame(tag->encoder_file, tag_encoder_buf, buf_len,1))
	{
			veejay_msg(VEEJAY_MSG_ERROR, "writing frame, giving up :[%s]", lav_strerror());
			return -1;
	}
	tag->rec_total_bytes += buf_len;
	
	if(audio_size > 0)
	{
		if(lav_write_audio(tag->encoder_file, abuff, audio_size))
		{
	 	    veejay_msg(VEEJAY_MSG_ERROR, "Error writing output audio [%s]",lav_strerror());
		}
		tag->rec_total_bytes += ( audio_size * _tag_info->edit_list->audio_bps);
	}
	/* write OK */
	tag->encoder_succes_frames ++;
	tag->encoder_num_frames ++;

	vj_tag_update(tag,t1);

	return (vj_tag_continue_record(t1));
}

int vj_tag_get_audio_frame(int t1, uint8_t *dst_buffer)
{
	vj_tag *tag = vj_tag_get(t1);
	if(!tag) return 0;

#ifdef SUPPORT_READ_DV2
	if(tag->source_type == VJ_TAG_TYPE_DV1394)
	{ // this is never tested ...
		vj_dv_decoder_get_audio( vj_tag_input->dv1394[tag->index], dst_buffer );	
	}
#endif
	if(tag->source_type == VJ_TAG_TYPE_AVFORMAT)
		return (vj_avformat_get_audio( vj_tag_input->avformat[tag->index], dst_buffer, -1 ));

	return 0;    
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
		vj_unicap_grab_frame( vj_tag_input->unicap[tag->index], buffer, width,height );
		return 1;
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
			uint8_t *address = vj_picture_get( p->pic );
			veejay_memcpy(buffer[0],address, len);
			veejay_memcpy(buffer[1],address + len, uv_len);
			veejay_memcpy(buffer[2],address + len + uv_len, uv_len);
		}
		break;
#endif

    	case VJ_TAG_TYPE_AVFORMAT:
		if(!vj_avformat_get_video_frame( vj_tag_input->avformat[tag->index], buffer, -1,vj_tag_input->pix_fmt )) 
	 		return -1;
		break;
		
	case VJ_TAG_TYPE_MCAST:
	case VJ_TAG_TYPE_NET:
		if(!net_thread_get_frame( tag,buffer ))
			return 0;
		return 1;
		break;
	case VJ_TAG_TYPE_YUV4MPEG:
		if( vj_tag_input->pix_fmt == FMT_420 || vj_tag_input->pix_fmt == FMT_420F)
		{
			if (vj_yuv_get_frame(vj_tag_input->stream[tag->index], buffer) != 0)
		    	{
		  		veejay_msg(VEEJAY_MSG_ERROR, "Error reading frame trom YUV4MPEG stream. (Stopping)");
		    	 	vj_tag_set_active(t1,0);
		    		return -1;
			}
		}
		else
		{
			if(vj_yuv_get_frame(vj_tag_input->stream[tag->index], _temp_buffer) != 0)
			{
				vj_tag_set_active(t1,0);
				return -1;
			}
			yuv420p_to_yuv422p2( _temp_buffer[0],_temp_buffer[1],_temp_buffer[2],buffer,width,height);
		}
		return 1;
		
		break;
#ifdef SUPPORT_READ_DV2
		case VJ_TAG_TYPE_DV1394:
			vj_dv1394_read_frame( vj_tag_input->dv1394[tag->index], buffer , abuffer,vj_tag_input->pix_fmt);
			break;
#endif

	case VJ_TAG_TYPE_COLOR:
		_tmp.len     = len;
		_tmp.uv_len  = uv_len;
		_tmp.data[0] = buffer[0];
		_tmp.data[1] = buffer[1];
		_tmp.data[2] = buffer[2];
		dummy_rgb_apply( &_tmp, width, height, 
			tag->color_r,tag->color_g,tag->color_b );
		break;

    	case VJ_TAG_TYPE_NONE:
		break;
		default:
		break;
    	}
    	return 1;
}


//int vj_tag_sprint_status(int tag_id, int entry, int changed, char *str)
int vj_tag_sprint_status( int tag_id,int cache, int pfps,int frame, int mode,int ts, char *str )
{
    vj_tag *tag;
    tag = vj_tag_get(tag_id);

    if (!tag)
	return -1;
    /*
    sprintf(str,
	    "%d %d %d %d %d %d %d %d %d %d %d %ld %ld %d %d %d %d %d %d %d %d %d %d %d",
	    tag->id,
	    tag->selected_entry,
	    tag->effect_toggle,
	    tag->next_id,
	    tag->source_type,
	    tag->index,
	    tag->depth,
	    tag->active,
	    tag->source,
	    tag->video_channel,
	    tag->encoder_active,
	    tag->encoder_duration,
	    tag->encoder_succes_frames,
	    entry,
	    changed,
	    vj_effect_real_to_sequence(tag->effect_chain[entry]->
				       effect_id),
	    tag->effect_chain[entry]->e_flag,
	    tag->effect_chain[entry]->frame_offset,
	    tag->effect_chain[entry]->frame_trimmer,
	    tag->effect_chain[entry]->source_type,
	    tag->effect_chain[entry]->channel,
	    tag->effect_chain[entry]->a_flag,
	    tag->effect_chain[entry]->volume,
		this_tag_id-1);
	*/

	sprintf(str,
			"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
			pfps,
			frame,
			mode,
			tag_id,
			tag->effect_toggle,
			tag->color_r, // no start, but color
			tag->color_g, // no end,
			tag->color_b, // no speed,
			0, // no looping
			tag->encoder_active,
			tag->encoder_duration,
			tag->encoder_succes_frames,
			vj_tag_size()-1,
			tag->source_type, // no markers
			tag->n_frames, // no markers
			tag->selected_entry, 
			ts,
			cache,
			0,
			0);
    return 0;
}

#ifdef HAVE_XML2
static void tagParseArguments(xmlDocPtr doc, xmlNodePtr cur, int *arg)
{
    xmlChar *xmlTemp = NULL;
    unsigned char *chTemp = NULL;
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


static void tagParseEffect(xmlDocPtr doc, xmlNodePtr cur, int dst_sample)
{
    xmlChar *xmlTemp = NULL;
    unsigned char *chTemp = NULL;
    int effect_id = -1;
    int arg[SAMPLE_MAX_PARAMETERS];
    int i;
    int source_type = 0;
    int channel = 0;
    int frame_trimmer = 0;
    int frame_offset = 0;
    int e_flag = 0;
    int volume = 0;
    int a_flag = 0;
    int chain_index = 0;

    for (i = 0; i < SAMPLE_MAX_PARAMETERS; i++) {
	arg[i] = 0;
    }

    if (cur == NULL)
	return;


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

	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_ARGUMENTS)) {
	    tagParseArguments(doc, cur->xmlChildrenNode, arg);
	}

	/* add source,channel,trimmer,e_flag */
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
	// xmlTemp and chTemp should be freed after use
	xmlTemp = NULL;
	chTemp = NULL;
	cur = cur->next;
    }

    if (effect_id != -1) {
	int j;
	int res = vj_tag_set_effect( dst_sample, chain_index, effect_id );

	if(res < 0 )
	veejay_msg(VEEJAY_MSG_ERROR, "Error parsing effect %d (pos %d) to stream %d\n",
		    effect_id, chain_index, dst_sample);
	

	/* load the parameter values */
	for (j = 0; j < vj_effect_get_num_params(effect_id); j++) {
	    vj_tag_set_effect_arg(dst_sample, chain_index, j, arg[j]);
	}
	vj_tag_set_chain_channel(dst_sample, chain_index, channel);
	vj_tag_set_chain_source(dst_sample, chain_index, source_type);

	vj_tag_set_chain_status(dst_sample, chain_index, e_flag);

	vj_tag_set_offset(dst_sample, chain_index, frame_offset);
	vj_tag_set_trimmer(dst_sample, chain_index, frame_trimmer);
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
/*************************************************************************************************
 *
 * ParseSample()
 *
 * Parse a sample
 *
 ****************************************************************************************************/
void tagParseStreamFX(xmlDocPtr doc, xmlNodePtr cur, vj_tag *skel)
{

    xmlChar *xmlTemp = NULL;
    unsigned char *chTemp = NULL;

    while (cur != NULL) {
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_CHAIN_ENABLED))
	{
		xmlTemp = xmlNodeListGetString( doc, cur->xmlChildrenNode,1);
		chTemp = UTF8toLAT1( xmlTemp );
		if(chTemp)
		{
			skel->effect_toggle = atoi(chTemp);
			free(chTemp);
		}
		if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name,(const xmlChar *) XMLTAG_FADER_ACTIVE)) {
		xmlTemp = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
		chTemp = UTF8toLAT1(xmlTemp);
		if(chTemp) {
			skel->fader_active = atoi(chTemp);
		        free(chTemp);
		}
		if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name,(const xmlChar *) XMLTAG_FADER_VAL)) {
		xmlTemp = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
		chTemp = UTF8toLAT1(xmlTemp);
		if(chTemp){
			skel->fader_val = atoi(chTemp);
			free(chTemp);
		}
		if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name,(const xmlChar*) XMLTAG_FADER_INC)) {
		xmlTemp = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
		chTemp = UTF8toLAT1(xmlTemp);
		if(chTemp) {
			skel->fader_inc = atof(chTemp);
			free(chTemp);
		}
		if(xmlTemp) xmlFree(xmlTemp);
	}

	if (!xmlStrcmp(cur->name,(const xmlChar*) XMLTAG_FADER_DIRECTION)) {
		xmlTemp = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
		chTemp = UTF8toLAT1(xmlTemp);
		if(chTemp) {
			skel->fader_inc = atoi(chTemp);
			free(chTemp);
		}
		if(xmlTemp) xmlFree(xmlTemp);
	}

	tagParseEffects(doc, cur->xmlChildrenNode, skel->id);

	// xmlTemp and chTemp should be freed after use
	xmlTemp = NULL;
	chTemp = NULL;

	cur = cur->next;
    }
    return;
}



static void tagCreateArguments(xmlNodePtr node, int *arg, int argcount)
{
    int i;
    char buffer[100];
    argcount = SAMPLE_MAX_PARAMETERS;
    for (i = 0; i < argcount; i++) {
	//if (arg[i]) {
	    sprintf(buffer, "%d", arg[i]);
	    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_ARGUMENT,
			(const xmlChar *) buffer);
	//}
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


    childnode =
	xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_ARGUMENTS, NULL);
    tagCreateArguments(childnode, effect->arg,
		    vj_effect_get_num_params(effect->effect_id));

    
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
    
void tagCreateStreamFX(xmlNodePtr node, vj_tag *tag)
{   
   xmlNodePtr childnode =
	xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTS, NULL);

   tagCreateEffects(childnode, tag->effect_chain);
}

#endif
