/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl> / <nwelburg@gmail.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 *
 *
 * 05/03/2003: Added XML code from Jeff Carpenter ( jrc@dshome.net )
 * 05/03/2003: Included more sample properties in Jeff's code 
 *	       Create is used to write the Sample to XML, Parse is used to load from XML
 
*/


#include <config.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <veejay/vims.h>
#include <libsample/sampleadm.h>
#include <libvjmsg/vj-msg.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <libvjmem/vjmem.h>
#include <libvevo/vevo.h>
#include <libvevo/libvevo.h>
#include <veejay/vjkf.h>
#include <veejay/vj-font.h>
#include <assert.h>
#include <libel/elcache.h>
#include <veejay/vj-misc.h>
#include <veejay/vj-viewport-xml.h>
#include <veejay/vj-viewport.h>
#include <veejay/vj-misc.h>
#include <libstream/vj-tag.h>
//#define KAZLIB_OPAQUE_DEBUG 1

#ifdef HAVE_XML2
#endif

#define FOURCC(a,b,c,d) ( (d<<24) | ((c&0xff)<<16) | ((b&0xff)<<8) | (a&0xff) )

#define FOURCC_RIFF     FOURCC ('R', 'I', 'F', 'F')
#define FOURCC_WAVE     FOURCC ('W', 'A', 'V', 'E')
#define FOURCC_FMT      FOURCC ('f', 'm', 't', ' ')
#define FOURCC_DATA     FOURCC ('d', 'a', 't', 'a')


#define VJ_IMAGE_EFFECT_MIN vj_effect_get_min_i()
#define VJ_IMAGE_EFFECT_MAX vj_effect_get_max_i()
#define VJ_VIDEO_EFFECT_MIN vj_effect_get_min_v()
#define VJ_VIDEO_EFFECT_MAX vj_effect_get_max_v()

static int this_sample_id = 0;	/* next available sample id */
static int next_avail_num = 0;	/* available sample id */
static int initialized = 0;	/* whether we are initialized or not */
static hash_t *SampleHash;	/* hash of sample information structs */
static int avail_num[SAMPLE_MAX_SAMPLES];	/* an array of freed sample id's */
static void *sample_font_ = NULL;
static int sampleadm_state = SAMPLE_PEEK;	/* default state */
static void *sample_cache[SAMPLE_MAX_SAMPLES];
static editlist *plain_editlist=NULL; 

extern void tagParseStreamFX(char *file, xmlDocPtr doc, xmlNodePtr cur, void *font, void *vp);
extern void   tag_writeStream( char *file, int n, xmlNodePtr node, void *font, void *vp );
extern int vj_tag_size();
extern int    veejay_sprintf( char *s, size_t size, const char *format, ... );

typedef struct
{
        int   active;
        int   current;
        int   size;
        int     *samples;
} seq_t;

/****************************************************************************************************
 *
 * sample_size
 *
 * returns current sample_id pointer. size is actually this_sample_id - next_avail_num,
 * but people tend to use size as in length.
 *
 ****************************************************************************************************/
int sample_size()
{
    return this_sample_id;
}

int sample_verify() {
   return hash_verify( SampleHash );
}



/****************************************************************************************************
 *
 * int_hash
 *
 * internal usage. returns hash_val_t for key
 *
 ****************************************************************************************************/
static hash_val_t int_hash(const void *key)
{
    return (hash_val_t) key;
}



/****************************************************************************************************
 *
 * int_compare
 *
 * internal usage. compares keys for hash.
 *
 ****************************************************************************************************/
static int int_compare(const void *key1, const void *key2)
{
#ifdef ARCH_X86_64
	return ((uint64_t) key1 < (uint64_t) key2 ? -1 :
		((uint64_t) key1 < (uint64_t) key2 ? + 1 : 0 ));
#else
    return ((uint32_t) key1 < (uint32_t) key2 ? -1 :
	    ((uint32_t) key1 > (uint32_t) key2 ? +1 : 0));
#endif
}

static void sample_close_edl(int s1, editlist *el)
{
	/* check if another sample has same EDL */
	if( el != NULL ) {
		    int end = sample_size() + 1;
			int same = 0;
			int i;

			if( el == plain_editlist ) {
				same = 1;
			}

			if( same == 0 ) {
				for (i = 1; i < end; i++) {
					if (!sample_exists(i) || s1 == i)
						continue;

					sample_info *b = sample_get(i);
					if( b->edit_list == el ) {
						same = 1;
						break;
					}
				}
			}

			if( same == 0 ) {
				vj_el_free(el);
			}
	}
}

/* Evil code for edl saving/restoring */
typedef struct
{
	int fmt;
	int deinterlace;
	int flags;
	int force;
	char norm;
	int width;
	int height;
} sample_setting;

static sample_setting __sample_project_settings;
void	sample_set_project(int fmt, int deinterlace, int flags, int force, char norm, int w, int h )
{
	__sample_project_settings.fmt = fmt;
	__sample_project_settings.deinterlace = deinterlace;
	__sample_project_settings.flags = flags;
	__sample_project_settings.force = force;
	__sample_project_settings.norm = norm;
	__sample_project_settings.width = w;
	__sample_project_settings.height = h;

} 
void	*sample_get_dict( int sample_id )
{
#ifdef HAVE_FREETYPE
	sample_info *si = sample_get(sample_id);
	if(si)
		return si->dict;
#endif
	return NULL;
}

/****************************************************************************************************
 *
 * sample_init()
 *
 * call before using any other function as sample_skeleton_new
 *
 ****************************************************************************************************/
void sample_init(int len, void *font, editlist *pedl)
{
    if (!initialized) {
	int i;
	for (i = 0; i < SAMPLE_MAX_SAMPLES; i++)
	    avail_num[i] = 0;
	this_sample_id = 1;	/* do not start with zero */
	if (!
	    (SampleHash =
	     hash_create(HASHCOUNT_T_MAX, int_compare, int_hash))) {
	}
	initialized = 1;
	veejay_memset( &__sample_project_settings,0,sizeof(sample_setting));
    }

    sample_font_ = font;
	plain_editlist = pedl;
}

void	sample_free(void *edl)
{
	if(!SampleHash)
		return;
	
	sample_del_all(edl);

	hash_destroy( SampleHash );
}

int sample_set_state(int new_state)
{
    if (new_state == SAMPLE_LOAD || new_state == SAMPLE_RUN
	|| new_state == SAMPLE_PEEK) {
	sampleadm_state = new_state;
    }
    return sampleadm_state;
}

int sample_get_state()
{
    return sampleadm_state;
}

/****************************************************************************************************
 *
 * sample_skeleton_new(long , long)
 *
 * create a new sample, give start and end of new sample. returns sample info block.
 *
 ****************************************************************************************************/

static int _new_id()
{
  /* perhaps we can reclaim a sample id */
	int n;
	int id = 0;
	for (n = 0; n <= next_avail_num; n++)
	{
		if (avail_num[n] != 0)	
		{
			id = avail_num[n];
			avail_num[n] = 0;
			break;
		}
	}
	if( id == 0 )
	{
		if(!this_sample_id) this_sample_id = 1;
		id = this_sample_id;
		this_sample_id ++;
	}
	return id;
}

sample_info *sample_skeleton_new(long startFrame, long endFrame)
{
   char tmp_file[128];
   sample_info *si;
   int i;

   if (!initialized) {
    	return NULL;
   }
   si = (sample_info *) vj_calloc(sizeof(sample_info));

   if(startFrame < 0) startFrame = 0;

   if(endFrame <= startFrame ) 
   {
	veejay_msg(VEEJAY_MSG_ERROR,"End frame %ld must be greater then start frame %ld", endFrame, startFrame);
	return NULL;
    }

    if (!si) {
	return NULL;
    }

    si->sample_id = _new_id();
    snprintf(si->descr,SAMPLE_MAX_DESCR_LEN, "Sample %4d", si->sample_id);
    si->first_frame = startFrame;
    si->last_frame = endFrame;
	si->resume_pos = startFrame;
    si->edit_list = NULL;	// clone later
    si->soft_edl = 1;
    si->speed = 1;
    si->looptype = 1; // normal looping
    si->audio_volume = 50;
    si->marker_start = 0;
    si->marker_end = 0;
    si->loopcount = 0;
    si->effect_toggle = 1;
    snprintf(tmp_file,sizeof(tmp_file), "sample_%05d.edl", si->sample_id );
    si->edit_list_file = strdup( tmp_file );

    sample_eff_chain *sec = (sample_eff_chain*) vj_calloc(sizeof(sample_eff_chain) * SAMPLE_MAX_EFFECTS );

    /* the effect chain is initially empty ! */
    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
		si->effect_chain[i] = &sec[i];
		if (si->effect_chain[i] == NULL) {
			veejay_msg(VEEJAY_MSG_ERROR, "Error allocating entry %d in Effect Chain for new sample",i);
			return NULL;
		}
		si->effect_chain[i]->effect_id = -1;
		si->effect_chain[i]->volume = 50;
		si->effect_chain[i]->channel = ( sample_size() <= 0 ? si->sample_id : sample_size()-1);
    }
#ifdef HAVE_FREETYPE
    si->dict = vpn( VEVO_ANONYMOUS_PORT );
#endif

	sample_cache[ si->sample_id ] = (void*) si;

    return si;
}



int sample_store(sample_info * skel)
{
    hnode_t *sample_node;
    if (!skel)
	return -1;
    sample_node = hnode_create(skel);
    if (!sample_node)
	return -1;

    if(skel->edit_list)
	{
		skel->play_length = vj_el_bogus_length( skel->edit_list, 0 );
	}
#ifdef ARCH_X86_64
	uint64_t sid = (uint64_t) skel->sample_id;
#else
	uint32_t sid = (uint32_t) skel->sample_id;
#endif

    if (!sample_exists(skel->sample_id)) {
		hash_insert(SampleHash, sample_node, (const void*) sid);
    } else {
		hnode_put(sample_node, (void *) sid);
    }
    return 0;
}

void 	sample_new_simple( void *el, long start, long end )
{
	sample_info *sample = sample_skeleton_new(start,end);
	if(sample) {
		sample->edit_list = el;
		sample->soft_edl = 1;
		sample_store(sample);
	}
}

/****************************************************************************************************
 *
 * sample_get(int sample_id)
 *
 * returns sample information struct or NULL on error.
 *
 ****************************************************************************************************/

sample_info *sample_get(int sample_id)
{
	if( sample_id < 0 || sample_id > SAMPLE_MAX_SAMPLES)
		return NULL;
#ifdef ARCH_X86_64
	uint64_t sid = (uint64_t) sample_id;
#else
	uint32_t sid = (uint32_t) sample_id;
#endif

	if( sample_cache[sample_id] == NULL ) {
		
		hnode_t *sample_node = hash_lookup(SampleHash, (const void *) sid);
		if(!sample_node)
			return NULL;
		sample_cache[sample_id] = hnode_get(sample_node);
	}

	return (sample_info*) sample_cache[sample_id];
}

/****************************************************************************************************
 *
 * sample_exists(int sample_id)
 *
 * returns 1 if a sample exists in samplehash, or 0 if not.
 *
 ****************************************************************************************************/


int sample_exists(int sample_id) {
	
	hnode_t *sample_node;
	if (!sample_id) return 0;
#ifdef ARCH_X86_64
	uint64_t sid = (uint64_t) sample_id;
#else
	uint32_t sid = (uint32_t) sample_id;
#endif

	sample_node = hash_lookup(SampleHash, (void*) sid);
	if (!sample_node) {
		return 0;
	}
	
	if(!sample_get(sample_id)) return 0;
	return 1;
}
/*
int sample_exists(int sample_id)
{
    if(sample_id < 1 || sample_id > SAMPLE_MAX_SAMPLES) return 0;
    return (sample_get(sample_id) == NULL ? 0 : 1);
}
*/

int sample_copy(int sample_id)
{
	int i;
	sample_info *org, *copy;
	if (!sample_exists(sample_id))
		return 0;
	org = sample_get(sample_id);
	copy = (sample_info*) vj_malloc(sizeof(sample_info));
	veejay_memcpy( copy,org,sizeof(sample_info));\

	sample_eff_chain *b = vj_malloc(sizeof(sample_eff_chain) * SAMPLE_MAX_EFFECTS );

	for (i = 0; i < SAMPLE_MAX_EFFECTS; i++)
	{
//		copy->effect_chain[i] =
//			(sample_eff_chain *) vj_malloc(sizeof(sample_eff_chain));
//

		copy->effect_chain[i] = &b[i];

		if (copy->effect_chain[i] == NULL)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Error allocating entry %d in Effect Chain for new sample",i);
			return 0;
		}
		veejay_memcpy( copy->effect_chain[i], org->effect_chain[i], sizeof( sample_eff_chain ) );
	}

	copy->sample_id = _new_id(); 

	if(org->edit_list)
	{
		copy->edit_list = vj_el_clone( org->edit_list );
		copy->soft_edl = 1;
	}

	if (sample_store(copy) != 0)
		return 0;

	return copy->sample_id;
}

/****************************************************************************************************
 *
 * sample_get_startFrame(int sample_id)
 *
 * returns first frame of sample.
 *
 ****************************************************************************************************/
int sample_get_longest(int sample_id)
{
	sample_info *si = sample_get(sample_id);
	if(si)
	{
		int len = (si->last_frame -
			  si->first_frame );
		int c = 0;
		int tmp = 0;
		int t=0;
		int _id=0;
		int speed = abs(si->speed);
		if( speed == 0 ) {
			veejay_msg(VEEJAY_MSG_WARNING,
				 "Starting paused sample %d at normal speed",
					sample_id);
			speed = 1;
		}
		int duration = len / speed; //how many frames are played of this sample

		if( si->looptype == 2) duration *= 2; // pingpong loop duration     

		if( sample_get_framedup(sample_id) > 0 )
			duration *= sample_get_framedup(sample_id);

		for(c=0; c < SAMPLE_MAX_EFFECTS; c++)
		{
			_id = sample_get_chain_channel(sample_id,c);
			t   = sample_get_chain_source(sample_id,c);
	
                        if(t==0 && sample_exists(_id))
			{
				tmp = sample_get_endFrame( _id) - sample_get_startFrame(_id);
				if(tmp>0)
				{
					tmp = tmp / sample_get_speed(_id);
					if(tmp < 0) tmp *= -1;
					if(sample_get_looptype(_id)==2) tmp *= 2; //pingpong loop underlying sample
				}
				if(tmp > duration) duration = tmp; //which one is longer ...	
		        }
		}
		veejay_msg(VEEJAY_MSG_DEBUG, "Length of sample in video frames: %ld (slow=%d, loop=%d, speed=%d)",duration,
			sample_get_framedup(sample_id), si->looptype, si->speed );
		

		return duration;
	}
	return 0;
}

int sample_get_startFrame(int sample_id)
{
    sample_info *si = sample_get(sample_id);
    if (si) {
   	if (si->marker_start >= 0 || si->marker_end > 0)
		return si->marker_start;
    	else
		return si->first_frame;
	}
    return -1;
}

int	sample_has_cali_fx(int sample_id)
{
	sample_info *si = sample_get(sample_id);
    	if(si == NULL)
		return -1;
	int i;
	for( i =0;i < SAMPLE_MAX_EFFECTS; i ++ ) {
		if(si->effect_chain[i]->effect_id == 190)
			return i;
	}
	return -1;
}

void	sample_cali_prepare( int sample_id, int slot, int chan )
{
	sample_info *si = sample_get(sample_id);
    	if(si == NULL)
		return;
	vj_tag	*tag	= vj_tag_get( chan );
	if( tag == NULL || tag->source_type != VJ_TAG_TYPE_CALI )
		return;
	int fx_id = vj_effect_real_to_sequence(
			si->effect_chain[slot]->effect_id );
	if( fx_id >= 0 ) {
		vj_tag_cali_prepare_now( chan, fx_id );
		veejay_msg(VEEJAY_MSG_DEBUG, "Prepared calibration data.");
	}
}



int	sample_get_el_position( int sample_id, int *start, int *end )
{
	sample_info *si = sample_get(sample_id);
	if(si)
	{
		*start = si->first_frame;
		*end   = si->last_frame;
		return 1;
	}
	return -1;
}




int sample_get_short_info(int sample_id, int *start, int *end, int *loop, int *speed) {
    sample_info *si = sample_get(sample_id);
    if(si) {
	if(si->marker_start >= 0 && si->marker_end > 0) {
	   *start = si->marker_start;
	   *end = si->marker_end;
	} 
    else {
	   *start = si->first_frame;
	   *end = si->last_frame;
	}
    *speed = si->speed;
    *loop = si->looptype;
	return 0;
    }
    
    return -1;
}

int sample_entry_is_rendering(int s1, int position) {
    sample_info *sample;
    sample = sample_get(s1);
    if (!sample)
	return -1;
    if (position >= SAMPLE_MAX_EFFECTS || position < 0)
	return -1;
    return sample->effect_chain[position]->is_rendering;
}

int sample_entry_set_is_rendering(int s1, int position, int value) {
    sample_info *si = sample_get(s1);
    if (!si)
	return -1;
    if( position >= SAMPLE_MAX_EFFECTS || position < 0) return -1;

    si->effect_chain[position]->is_rendering = value;
    return 1;
}


int sample_update_offset(int s1, int n_frame)
{
	int len;
	sample_info *si = sample_get(s1);

	if(!si) return -1;
	si->offset = (n_frame - si->first_frame);
	len = si->last_frame - si->first_frame;
	if(si->offset < 0) 
	{	
		veejay_msg(VEEJAY_MSG_WARNING,"Sample bounces outside sample by %d frames",
			si->offset);
		si->offset = 0;
	}
	if(si->offset > len) 
	{
		 veejay_msg(VEEJAY_MSG_WARNING,"Sample bounces outside sample with %d frames",
			si->offset);
		 si->offset = len;
	}
	return 1;
}	

int sample_set_manual_fader( int s1, int value)
{
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  si->fader_active = 2;
  si->fader_val = (float) value;
  si->fader_inc = 0.0;
  si->fader_direction = 0.0;

  /* inconsistency check */
  if(si->effect_toggle == 0) si->effect_toggle = 1;

  return 1;
}

int sample_set_fader_active( int s1, int nframes, int direction ) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  if(nframes <= 0) return -1;
  si->fader_active = 1;

  if(direction<0)
	si->fader_val = 255.0;
  else
	si->fader_val = 0.0;

  si->fader_inc = (float) (255.0 / (float) nframes);
  si->fader_direction = direction;
  si->fader_inc *= si->fader_direction;
  /* inconsistency check */
  if(si->effect_toggle == 0)
	{
	si->effect_toggle = 1;
	}
  return 1;
}


int sample_reset_fader(int s1) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  si->fader_active = 0;
  si->fader_val = 0;
  si->fader_inc = 0;
  return 1;
}

int sample_get_fader_active(int s1) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  return (si->fader_active);
}

float sample_get_fader_val(int s1) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  return (si->fader_val);
}

float sample_get_fader_inc(int s1) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  return (si->fader_inc);
}

int sample_get_fader_direction(int s1) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  return si->fader_direction;
}

int sample_set_fader_val(int s1, float val) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  si->fader_val = val;
  return 1;
}

int sample_apply_fader_inc(int s1) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  si->fader_val += si->fader_inc;
  if(si->fader_val > 255.0 ) si->fader_val = 255.0;
  if(si->fader_val < 0.0 ) si->fader_val = 0.0;
  return (int) (si->fader_val+0.5);
}



int sample_set_fader_inc(int s1, float inc) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  si->fader_inc = inc;
  return 1;
}

int sample_marker_clear(int sample_id) {
    sample_info *si = sample_get(sample_id);
    if (!si)
	return -1;
    si->marker_start = 0;
    si->marker_end   = 0;
    veejay_msg(VEEJAY_MSG_INFO, "Marker cleared (%d - %d) - (speed=%d)",
	si->marker_start, si->marker_end, si->speed);
    return 1;
}

int sample_set_marker_start(int sample_id, int marker)
{
    sample_info *si = sample_get(sample_id);
    if (!si)
		return -1;
	if(si->speed < 0 )
	{
		int swap = si->marker_end;
		si->marker_end = marker;
		si->marker_start = swap;
	}
	else
	{
		si->marker_start = marker;
	}
    return 1;
}

int sample_set_marker(int sample_id, int start, int end)
{
    sample_info *si = sample_get(sample_id);
    if(!si) return 0;
    
    if( start < si->first_frame )
		return 0;
	if( start > si->last_frame )
		return 0;
	if( end < si->first_frame )
		return 0;
	if( end > si->last_frame )
		return 0; 

    si->marker_start	= start;
    si->marker_end	= end;
    
    return 1;
}

int sample_set_marker_end(int sample_id, int marker)
{
    sample_info *si = sample_get(sample_id);
    if (!si)
		return -1;

	if(si->speed < 0 )
	{
		// mapping in reverse!
		int swap			= si->marker_start;
		si->marker_start	= marker;
		si->marker_end		= swap;
	}
	else
	{
		si->marker_end 		= marker;
	}
	
    return 1;
}

int sample_set_description(int sample_id, char *description)
{
    sample_info *si = sample_get(sample_id);
    if (!si)
	return -1;
    if (!description || strlen(description) <= 0) {
    	snprintf(si->descr, SAMPLE_MAX_DESCR_LEN, "Sample%04d", si->sample_id );
    } else {
	snprintf(si->descr, SAMPLE_MAX_DESCR_LEN, "%s", description);
    }
    return 1;
}

int sample_get_description(int sample_id, char *description)
{
    sample_info *si;
    si = sample_get(sample_id);
    if (!si)
	return -1;
    snprintf(description, SAMPLE_MAX_DESCR_LEN,"%s", si->descr);
    return 0;
}

/****************************************************************************************************
 *
 * sample_get_endFrame(int sample_id)
 *
 * returns last frame of sample.
 *
 ****************************************************************************************************/
int sample_get_endFrame(int sample_id)
{
    sample_info *si = sample_get(sample_id);
    if (si) {
   	if (si->marker_end > 0 && si->marker_start >= 0)
		return si->marker_end;
   	 else {
		return si->last_frame;
	}
    }
    return -1;
}
/****************************************************************************************************
 *
 * sample_del( sample_nr )
 *
 * deletes a sample from the hash. returns -1 on error, 1 on success.
 *
 ****************************************************************************************************/
int sample_verify_delete( int sample_id, int sample_type )
{
	int i,j;
	for( i = 1; i < sample_size()-1; i ++ )
	{
		sample_info *s = sample_get(i);
		if(s)
		{
			for( j = 0 ; j < SAMPLE_MAX_EFFECTS; j ++ )
			{
				if(s->effect_chain[j]->channel == sample_id &&
				   s->effect_chain[j]->source_type == sample_type )
				{
					s->effect_chain[j]->channel = i;
					s->effect_chain[j]->source_type = 0;
				}
			}
		}
	}
	return 1;
}

int sample_del(int sample_id)
{
    hnode_t *sample_node;
    sample_info *si;
    si = sample_get(sample_id);
    if (!si)
		return 0;
#ifdef ARCH_X86_64
	uint64_t sid = (uint64_t) sample_id;
#else
	uint32_t sid = (uint32_t) sample_id;
#endif

    sample_node = hash_lookup(SampleHash, (void *) sid);
    if (sample_node) {
	    int i;

	    sample_chain_free( sample_id );

	    if(si->soft_edl == 0 && si->edit_list != NULL)
 	 	   vj_el_break_cache( si->edit_list ); //@ destroy cache, if any

	    for(i=0; i < SAMPLE_MAX_EFFECTS; i++) 
	    {
		if( si->effect_chain[i]->kf )
			vpf( si->effect_chain[i]->kf );
	    }
	    if( si->effect_chain[0] )
			free(si->effect_chain[0]);
  
	    if(si->encoder_destination )
			free(si->encoder_destination );
	   	
		if(si->edit_list_file)
			free( si->edit_list_file );
#ifdef HAVE_FREETYPE
	    if( si->dict )
			vj_font_dictionary_destroy( sample_font_,si->dict );
#endif
		if(si->viewport) {	
			viewport_destroy(si->viewport);
			si->viewport = NULL;
		}

		if(si->edit_list) {
				/* check if another sample has same EDL */
				sample_close_edl( sample_id, si->edit_list );
		}

		/* store freed sample_id */
  	 	avail_num[next_avail_num] = sample_id;
  		next_avail_num++;
   		hash_delete_free(SampleHash, sample_node);

		sample_cache[ sample_id ] = NULL;
	
		free(si);

    	return 1;
    }

    return 0;
}

void sample_del_all(void *edl)
{
    int end = sample_size() + 1;
    int i;

    for (i = 1; i < end; i++) {
		if (!sample_exists(i))
			continue;
	
		sample_chain_clear(i);
		sample_del(i);
	}
     
	veejay_memset( avail_num, 0, sizeof(int) * SAMPLE_MAX_SAMPLES );
	next_avail_num = 0;
	this_sample_id = 0;

	hash_free_nodes( SampleHash );
}

/****************************************************************************************************
 *
 * sample_get_effect( sample_nr , position)
 *
 * returns effect in effect_chain on position X , -1 on error.
 *
 ****************************************************************************************************/
int sample_get_effect(int s1, int position)
{
    sample_info *sample = sample_get(s1);
    if(position >= SAMPLE_MAX_EFFECTS || position < 0 ) return -1;
    if(sample) {
	if(sample->effect_chain[position]->e_flag==0) return -1;
   	return sample->effect_chain[position]->effect_id;
    }
    return -1;
}

void	*sample_get_plugin( int s1, int position, void *ptr)
{
	sample_info *sample = sample_get(s1);
    if(position >= SAMPLE_MAX_EFFECTS || position < 0 ) return NULL;
    
	if(sample) {

		if( ptr != NULL )
			sample->effect_chain[position]->fx_instance = ptr;
   		return sample->effect_chain[position]->fx_instance;
    }

    return NULL;
}

int sample_get_effect_any(int s1, int position) {
	sample_info *sample = sample_get(s1);
	//	if(position >= SAMPLE_MAX_EFFECTS || position < 0 ) return -1;
	if(sample) {
		return sample->effect_chain[position]->effect_id;
	}
	return -1;
}

int sample_get_chain_status(int s1, int position)
{
    sample_info *sample;
    sample = sample_get(s1);
    if (!sample)
	return -1;
    if (position >= SAMPLE_MAX_EFFECTS)
	return -1;
    return sample->effect_chain[position]->e_flag;
}

int	sample_get_first_mix_offset(int s1, int *parent, int look_for)
{
	sample_info *sample = sample_get(s1);
	if(!sample)
		return 0;
	int p = 0;
	for( p = 0; p < SAMPLE_MAX_EFFECTS; p ++ ) {
	  if( sample->effect_chain[p]->source_type == 0 && look_for == sample->effect_chain[p]->channel)
	  {	 
		return sample->effect_chain[p]->frame_offset;
 	  }

	}
	return 0;
}


int	sample_set_resume(int s1,long position)
{
	sample_info *sample = sample_get(s1);
	if(!sample)
		return -1;
	sample->resume_pos = position;
	return 1;
}
long	sample_get_resume(int s1)
{
	sample_info *sample = sample_get(s1);
	if(!sample)
		return -1;
	if( sample->resume_pos < sample->first_frame )
		sample->resume_pos = sample->first_frame;
	else if ( sample->resume_pos > sample->last_frame )
		sample->resume_pos = sample->last_frame;	
	
	if( sample->marker_start >= 0 && sample->marker_end > 0  ) {
		if( sample->resume_pos < sample->marker_start )
			sample->resume_pos = sample->marker_start;
		else if ( sample->resume_pos > sample->marker_end ) 
			sample->resume_pos = sample->marker_end;
	}
	
	return sample->resume_pos;
}

int sample_get_offset(int s1, int position)
{
    sample_info *sample;
    sample = sample_get(s1);
    if (!sample)
		return -1;
    if (position >= SAMPLE_MAX_EFFECTS)
		return -1;

    return sample->effect_chain[position]->frame_offset;
}

int sample_get_trimmer(int s1, int position)
{
    sample_info *sample;
    sample = sample_get(s1);
    if (!sample)
	return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;
    return sample->effect_chain[position]->frame_trimmer;
}

int sample_get_chain_volume(int s1, int position)
{
    sample_info *sample;
    sample = sample_get(s1);
    if (!sample)
	return -1;
    if (position >= SAMPLE_MAX_EFFECTS)
	return -1;
    return sample->effect_chain[position]->volume;
}

int sample_get_chain_audio(int s1, int position)
{
    sample_info *sample = sample_get(s1);
    if (sample) {
     return sample->effect_chain[position]->a_flag;
    }
    return -1;
}

/****************************************************************************************************
 *
 * sample_get_looptype
 *
 * returns the type of loop set on the sample. 0 on no loop, 1 on ping pong
 * returns -1  on error.
 *
 ****************************************************************************************************/

int sample_get_looptype(int s1)
{
    sample_info *sample = sample_get(s1);
    if (sample) {
    	return sample->looptype;
    }
    return 0;
}

int sample_get_playmode(int s1)
{
   sample_info *sample = sample_get(s1);
   if (sample) {
   	 return sample->playmode;
   }
   return -1;
}

/********************
 * get depth: 1 means do what is in underlying sample.
 *******************/
int sample_get_depth(int s1)
{
    sample_info *sample = sample_get(s1);
    if (sample)
      return sample->depth;
    return 0;
}

int sample_set_depth(int s1, int n)
{
    sample_info *sample;
    hnode_t *sample_node;

    if (n == 0 || n == 1) {
	sample = sample_get(s1);
	if (!sample)
	    return -1;
	if (sample->depth == n)
	    return 1;
	sample->depth = n;
	sample_node = hnode_create(sample);
	if (!sample_node) {
	    return -1;
	}
	return 1;
    }
    return -1;
}
int sample_set_chain_status(int s1, int position, int status)
{
    sample_info *sample;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;
    sample = sample_get(s1);
    if (!sample)
	return -1;
    sample->effect_chain[position]->e_flag = status;
    return 1;
}

/****************************************************************************************************
 *
 * sample_get_speed
 *
 * returns the playback speed set on the sample.
 * returns -1  on error.
 *
 ****************************************************************************************************/

int sample_get_speed(int s1)
{
    sample_info *sample = sample_get(s1);
    if (sample)
   	 return sample->speed;
    return 0;
}

int sample_get_framedup(int s1) {
	sample_info *sample = sample_get(s1);
	if(sample) return sample->dup;
	return 0;
}
int sample_get_framedups(int s1) {
	sample_info *sample = sample_get(s1);
	if(sample) return sample->dups;
	return 0;

}

int sample_get_effect_status(int s1)
{
	sample_info *sample = sample_get(s1);
	if(sample) return sample->effect_toggle;
	return 0;
}

int	sample_var( int s1, int *type, int *fader, int *fx, int *rec, int *active )
{
	sample_info *si = sample_get(s1);
	if(!si) return 0;
	*type  = 0;
    	*fader = si->fader_active;
	*fx    = si->effect_toggle;
	*rec   = si->encoder_active;
	*active= 1;
	return 1;
}

/****************************************************************************************************
 *
 * sample_get_effect_arg( sample_nr, position, argnr )
 *
 * returns the required argument set on position X in the effect_chain of sample Y.
 * returns -1 on error.
 ****************************************************************************************************/
int sample_get_effect_arg(int s1, int position, int argnr)
{
    sample_info *sample;
    sample = sample_get(s1);
    if (!sample)
	return -1;
    if (position >= SAMPLE_MAX_EFFECTS)
	return -1;
    if (argnr < 0 || argnr > SAMPLE_MAX_PARAMETERS)
	return -1;
    return sample->effect_chain[position]->arg[argnr];
}

int sample_get_selected_entry(int s1) 
{
	sample_info *sample;
	sample = sample_get(s1);
	if(!sample) return -1;
	return sample->selected_entry;
}	

int sample_get_all_effect_arg(int s1, int position, int *args, int arg_len, int n_frame)
{
    int i;
    sample_info *sample;
    sample = sample_get(s1);
    if( arg_len == 0)
	return 1;
    if (!sample)
	return -1;
    if (position >= SAMPLE_MAX_EFFECTS)
	return -1;
    if (arg_len < 0 || arg_len > SAMPLE_MAX_PARAMETERS)
	return -1;

    if( sample->effect_chain[position]->kf )
    {
	 for( i = 0; i < arg_len; i ++ )
	 {
		int tmp = 0;
		if(!get_keyframe_value( sample->effect_chain[position]->kf, n_frame, i, &tmp ) ) {
			args[i] = sample->effect_chain[position]->arg[i];
		}
		else {
			args[i] = tmp;
		}
	 }
    }
    else
    {
   	 for (i = 0; i < arg_len; i++) {
		args[i] = sample->effect_chain[position]->arg[i];
    	}
    }
    return i;
}

/********************************************
 * sample_has_extra_frame.
 * return 1 if an effect on the given chain entry 
 * requires another frame, -1 otherwise.
 */
int sample_has_extra_frame(int s1, int position)
{
    sample_info *sample;
    sample = sample_get(s1);
    if (!sample)
	return -1;
    if (position >= SAMPLE_MAX_EFFECTS)
	return -1;
    if (sample->effect_chain[position]->effect_id == -1)
	return -1;
    if (vj_effect_get_extra_frame
	(sample->effect_chain[position]->effect_id) == 1)
	return 1;
    return -1;
}

/****************************************************************************************************
 *
 * sample_set_effect_arg
 *
 * sets an argument ARGNR in the chain on position X of sample Y
 * returns -1  on error.
 *
 ****************************************************************************************************/

int sample_set_effect_arg(int s1, int position, int argnr, int value)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
	return -1;
    if (position >= SAMPLE_MAX_EFFECTS)
	return -1;
    if (argnr < 0 || argnr > SAMPLE_MAX_PARAMETERS)
	return -1;
    sample->effect_chain[position]->arg[argnr] = value;
    return 1;
}

int sample_set_selected_entry(int s1, int position) 
{
	sample_info *sample = sample_get(s1);
	if(!sample) return -1;
	if(position< 0 || position >= SAMPLE_MAX_EFFECTS) return -1;
	sample->selected_entry = position;
	return 1;
}

int sample_set_effect_status(int s1, int status)
{
	sample_info *sample = sample_get(s1);
	if(!sample) return -1;
	if(status == 1 || status == 0 )
	{
		sample->effect_toggle = status;
		return 1;
	}
	return -1;
}

int sample_set_chain_channel(int s1, int position, int input)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
	return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;
 
    
 
    //sample->effect_chain[position]->channel = input;
    int src_type =  sample->effect_chain[position]->source_type;
    // now, reset cache and setup
   	if( src_type == 0)
   	{
		if( sample->effect_chain[position]->channel != input && sample->effect_chain[position]->effect_id > 0)
		{
			sample_info *new = sample_get(input);
		    sample_info *old = sample_get( sample->effect_chain[position]->channel );
		    if(old)
			    vj_el_break_cache( old->edit_list ); // no longer needed
	    	
		    if(new)
				vj_el_setup_cache( new->edit_list ); // setup new cache
		}
	 }
	if( src_type == 1 && 
		vj_tag_get_active( sample->effect_chain[position]->channel ) &&
		vj_tag_get_type( sample->effect_chain[position]->channel ) == VJ_TAG_TYPE_NET ) {
		vj_tag_disable( sample->effect_chain[position]->channel );
	}

	sample->effect_chain[position]->channel = input;

    return 1;
}

static	int sample_sample_used(sample_info *a, int b )
{
	int i;
	for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++ )
	{
		int src_type =  a->effect_chain[i]->source_type;
		int id       =  a->effect_chain[i]->channel;
		if( src_type == 0 && id == b )
			return 1;
	}
	return 0;
}

int	sample_stop_playing(int s1, int new_s1)
{
	sample_info *sample = sample_get(s1);
	sample_info *newsample = NULL;
	if( new_s1 )
		newsample = sample_get(new_s1);
	if (!sample)
		return 0;
	if (new_s1 && !newsample)
		return 0;
	unsigned int i;
	
	//@ stop playing, if new_s1

	if( new_s1 == s1 )
		return 1;

	int destroy_s1 = 1;

	if( new_s1 )
	{
		for( i = 0; i < SAMPLE_MAX_EFFECTS ; i ++ )
		{
			int src_type =  newsample->effect_chain[i]->source_type;
			int id       =  newsample->effect_chain[i]->channel;
			if( src_type == 0 && id == s1 )
				destroy_s1 = 0; // no need to destroy cache, used by newsample
		}			
	}

	if(destroy_s1)
		vj_el_break_cache( sample->edit_list ); // break the cache

	
	if( new_s1 )
	{
		for( i = 0; i < SAMPLE_MAX_EFFECTS;i++ )
		{
			int src_type =  sample->effect_chain[i]->source_type;
			int id       =   sample->effect_chain[i]->channel;
			if( src_type == 0 && id > 0 )
			{
				//@ if ID is not in newsample,
				if( !sample_sample_used( newsample, id ))
				{
					sample_info *second = sample_get( id );
		       			if(second) //@ get and destroy its cache
						vj_el_break_cache( second->edit_list );
		    		}
			}
    		}
	}

	return 1;
}

int	sample_cache_used( int s1 )
{
	return cache_avail_mb();
}

int	sample_start_playing(int s1, int no_cache)
{
	sample_info *sample = sample_get(s1);
	if (!sample)
	return -1;
   	int i;

	if(!no_cache)
		vj_el_setup_cache( sample->edit_list );

	for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++ )
	{
		int src_type =  sample->effect_chain[i]->source_type;
		int id       =   sample->effect_chain[i]->channel;
		if( src_type == 0 && id > 0 && sample->effect_chain[i]->effect_id > 0 )
		{
			sample_info *second = sample_get( id );
	       		if(second)
				vj_el_setup_cache( second->edit_list );
	    	}
    	}

	return 1;
}

int sample_is_deleted(int s1)
{
    int i;
    for (i = 0; i < next_avail_num; i++) {
	if (avail_num[i] == s1)
	    return 1;
    }
    return 0;
}

int sample_set_chain_source(int s1, int position, int input)
{
    sample_info *sample;
    sample = sample_get(s1);
    if (!sample)
	return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;

	if( sample->effect_chain[position]->source_type == 0 &&
		    sample->effect_chain[position]->channel > 0 &&
			sample->effect_chain[position]->effect_id > 0 &&
			input != sample->effect_chain[position]->source_type )
	{
		sample_info *second = sample_get( sample->effect_chain[position]->channel );
		if(second && second->edit_list)
			vj_el_clear_cache( second->edit_list );
	}
	if( sample->effect_chain[position]->source_type == 0 &&
		sample->effect_chain[position]->effect_id > 0 &&
		sample->effect_chain[position]->channel > 0 &&
		input != sample->effect_chain[position]->source_type)
	{
	    sample_info *new = sample_get( input );
	    if(new)
	  		vj_el_setup_cache( new->edit_list );
	}

	if( sample->effect_chain[position]->source_type == 1 && 
			vj_tag_get_active( sample->effect_chain[position]->channel ) &&
		 	vj_tag_get_type( sample->effect_chain[position]->channel ) == VJ_TAG_TYPE_NET  &&
			input != 1) {
			vj_tag_disable( sample->effect_chain[position]->channel );
		}

    sample->effect_chain[position]->source_type = input;
    
	return 1;
}


int	sample_load_composite_config( void *compiz, int s1 )
{
	sample_info *sample = sample_get(s1);
	if(!sample) return -1;

	int val = 0;
	void *temp = composite_load_config( compiz, sample->viewport_config , &val );
	if( temp == NULL || val == -1 ) {
		return 0;
	}
	
	sample->composite = val;
	sample->viewport  = temp;
	return sample->composite;
}

void		*sample_get_composite_view(int s1)
{
	sample_info *sample = sample_get(s1);
	if(!sample) return NULL;
	return sample->viewport;
}
int	sample_set_composite_view(int s1, void *vp)
{
	sample_info *sample = sample_get(s1);
	if(!sample) return -1;
	sample->viewport = vp;
	return 1;
}

int	sample_set_composite(void *compiz, int s1, int composite)
{
	sample_info *sample = sample_get(s1);
	if(!sample) return -1;
	sample->composite = composite;
	if(sample->viewport_config == NULL) { 
		sample->composite = 1; 
		return 1;
	}
	composite_add_to_config( compiz, sample->viewport_config, composite );

	return 1;
}

int	sample_get_composite(int s1)
{
	sample_info *sample = sample_get(s1);
	if(!sample) return 0;
	return sample->composite;	
}
/****************************************************************************************************
 *
 * sample_set_speed
 *
 * store playback speed in the sample.
 * returns -1  on error.
 *
 ****************************************************************************************************/
void sample_loopcount(int s1)
{
    sample_info *sample = sample_get(s1);
    if (!sample) return;
    sample->loopcount ++;
    if(sample->loopcount > 1000000 )
	sample->loopcount = 0;
}
int	sample_get_loopcount(int s1)
{
  sample_info *sample = sample_get(s1);
  if (!sample) return 0;

  return sample->loopcount;
}
void	sample_reset_loopcount(int s1)
{
    sample_info *sample = sample_get(s1);
    if (!sample) return;
    sample->loopcount = 0;
}

int sample_set_speed(int s1, int speed)
{
    sample_info *sample = sample_get(s1);
    if (!sample) return -1;
	int len = sample->last_frame -
			sample->first_frame;
    if( (speed < -(MAX_SPEED) ) || (speed > MAX_SPEED))
	return -1;
    if( speed > len )
	return -1;
    if( speed < -(len))
	return -1;
    sample->speed = speed;
    return 1;
}
int sample_set_framedups(int s1, int n) {
	sample_info *sample = sample_get(s1);
	if(!sample) return -1;
	sample->dups = n;
	return 1;
}


int sample_set_framedup(int s1, int n) {
	sample_info *sample = sample_get(s1);
	if(!sample) return -1;
	sample->dup = n;
	return 1;
}

int sample_get_chain_channel(int s1, int position)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
	return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;
    return sample->effect_chain[position]->channel;
}

int sample_get_chain_source(int s1, int position)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
	return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;
    return sample->effect_chain[position]->source_type;
}

int sample_get_loops(int s1)
{
    sample_info *sample = sample_get(s1);
    if (sample) {
    	return sample->max_loops;
	}
    return -1;
}
int sample_get_loops2(int s1)
{
    sample_info *sample;
    sample = sample_get(s1);
    if (!sample)
	return -1;
    return sample->max_loops2;
}

/****************************************************************************************************
 *
 * sample_set_looptype
 *
 * store looptype in the sample.
 * returns -1  on error.
 *
 ****************************************************************************************************/

int sample_set_looptype(int s1, int looptype)
{
    sample_info *sample = sample_get(s1);
    if(!sample) return -1;

    if (looptype == 3 || looptype == 0 || looptype == 1 || looptype == 2) {
		sample->looptype = looptype;
		return 1;
    }
    return 0;
}

int sample_set_playmode(int s1, int playmode)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
		return -1;

    sample->playmode = playmode;
    return 1;
}



/*************************************************************************************************
 * update start frame
 *
 *************************************************************************************************/
int sample_set_startframe(int s1, long frame_num)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
	return 0;

    if( frame_num < 0 )
	return 0;

    if(sample->play_length )
	 return 1; //@ simpler to lie

    if(sample->edit_list)
	if( frame_num > sample->edit_list->total_frames  )
		frame_num = sample->edit_list->total_frames;
  
    sample->first_frame = frame_num;
    if(sample->first_frame >= sample->last_frame )
	sample->first_frame = sample->last_frame-1;

	if( sample->resume_pos < frame_num )
		sample->resume_pos = frame_num;
	

    return 1;
}

int	sample_usable_edl( int s1 )
{
	sample_info *sample = sample_get(s1);
	if(!sample) return 0;
	if( sample->play_length )
		return 0;
	if( sample->edit_list )
		return 1;
	return 0;
}

int	sample_max_video_length(int s1)
{
	sample_info *sample = sample_get(s1);
	float fps = 25.0;
	if(!sample) return 0;
	if(sample->edit_list)
		fps = sample->edit_list->video_fps;

	if( sample->play_length )
		return (60 * fps * 6); // 6 minutes
	
	if( sample->edit_list )
		return (int) sample->edit_list->total_frames;
	return 0;
}

int	sample_video_length( int s1 )
{
	sample_info *sample = sample_get(s1);
	if(!sample) return 0;
	if( sample->play_length )
		return sample->play_length;
	if( sample->edit_list )
		return sample->edit_list->total_frames;
	return 0;
}

int sample_set_endframe(int s1, long frame_num)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
	return 0;
    if(frame_num < 0)
	return 0;

    if(sample->play_length)
    {
	int new_len = ( frame_num - sample->first_frame );
	if( new_len <= 1 )
		new_len = 1;
	sample->last_frame = sample->first_frame + new_len;

	if( sample->resume_pos > frame_num )
		sample->resume_pos = frame_num;

	if( vj_el_set_bogus_length( sample->edit_list, 0, new_len ) )
	{
		sample->play_length = new_len;
		return 1;
	}
	return 0;
    }

    if(sample->edit_list)
	if( frame_num > sample->edit_list->total_frames )
		frame_num = sample->edit_list->total_frames;

    sample->last_frame = frame_num;
	if( sample->resume_pos > frame_num )
		sample->resume_pos = frame_num;

    return 1;
}

int sample_get_next(int s1)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
	return -1;
    return sample->next_sample_id;
}
int sample_set_loops(int s1, int nr_of_loops)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
	return -1;
    sample->max_loops = nr_of_loops;
    return 1;
}
int sample_set_loops2(int s1, int nr_of_loops)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
	return -1;
    sample->max_loops2 = nr_of_loops;
    return 1;
}

int	sample_get_subrender(int s1)
{
	sample_info *sample = sample_get(s1);
	if(!sample)
		return 0;
	return sample->subrender;
}

void sample_set_subrender(int s1, int status )
{
	sample_info *sample = sample_get(s1);
	if(sample)
		sample->subrender = status;
}	

int sample_get_sub_audio(int s1)
{
    sample_info *sample;
    sample = sample_get(s1);
    if (!sample)
		return -1;
    return sample->sub_audio;
}

int sample_set_sub_audio(int s1, int audio)
{
    sample_info *sample = sample_get(s1);
    if(!sample) return -1;
    if (audio < 0 && audio > 1)
	return -1;
    sample->sub_audio = audio;
    return 1;
}

int sample_get_audio_volume(int s1)
{
    sample_info *sample = sample_get(s1);
    if (sample) {
   	 return sample->audio_volume;
	}
   return -1;
}

int sample_set_audio_volume(int s1, int volume)
{
    sample_info *sample = sample_get(s1);
    if (volume < 0)
	volume = 0;
    if (volume > 100)
	volume = 100;
    sample->audio_volume = volume;
    return 1;
}


int sample_set_next(int s1, int next_sample_id)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
	return -1;
    /* just add, do not verify 
       on module generation, next sample may not yet be created.
       checks in parameter set in libveejayvj.c
     */
    sample->next_sample_id = next_sample_id;
    return 1;
}

/****************************************************************************************************
 *
 *
 * add a new effect to the chain, returns chain index number on success or -1  if  
 * the requested sample does not exist.
 *
 ****************************************************************************************************/

int sample_chain_malloc(int s1)
{
    sample_info *sample = sample_get(s1);
    int i=0;
    int e_id = 0; 
    int sum =0;
    if (!sample)
	return -1;
    for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
    {
	e_id = sample->effect_chain[i]->effect_id;
	if(e_id)
	{
		int res = 0;
		sample->effect_chain[i]->fx_instance = vj_effect_activate(e_id, &res );
		if(res)	sum++;
	}
    } 
//    veejay_msg(VEEJAY_MSG_DEBUG, "Allocated %d effects",sum);
    return sum; 
}

int sample_chain_free(int s1)
{
    sample_info *sample = sample_get(s1);
    int i=0;
    int e_id = 0; 
    int sum = 0;
    if (!sample)
	return -1;
    for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
    {
	e_id = sample->effect_chain[i]->effect_id;
	if(e_id!=-1)
	{
		if(vj_effect_initialized(e_id, sample->effect_chain[i]->fx_instance))
		{
			vj_effect_deactivate(e_id, sample->effect_chain[i]->fx_instance);
			sample->effect_chain[i]->fx_instance = NULL;
			sum++;
  		}
		if( sample->effect_chain[i]->source_type == 1 && 
			vj_tag_get_active( sample->effect_chain[i]->channel ) &&
		 	vj_tag_get_type( sample->effect_chain[i]->channel ) == VJ_TAG_TYPE_NET ) {
			vj_tag_disable( sample->effect_chain[i]->channel );
		}
	 }
   } 

// 	veejay_msg(VEEJAY_MSG_DEBUG, "Freed %d effects",sum);

    return sum;
}

int	sample_chain_reset_kf( int s1, int entry )
{
	sample_info *sample = sample_get(s1);
        if(!sample) return 0;
	sample->effect_chain[entry]->kf_status = 0;
	sample->effect_chain[entry]->kf_type = 0;
	if(sample->effect_chain[entry]->kf)
	  vpf(sample->effect_chain[entry]->kf );
	sample->effect_chain[entry]->kf = NULL;
	return 1;
}
/*
int	sample_get_kf_tokens( int s1, int entry, int id, int *start, int *end, int *type )
{
	sample_info *sample = sample_get(s1);
	if(!sample) return 0;
	if( sample->effect_chain[entry]->kf == NULL )
		return 0;
	return keyframe_get_tokens( sample->effect_chain[entry]->kf, id, start,end,type );
}
*/
void	*sample_get_kf_port( int s1, int entry )
{
	sample_info *sample = sample_get(s1);
        if(!sample) return NULL;
	return sample->effect_chain[entry]->kf;
}

int	sample_get_kf_status( int s1, int entry, int *type )
{
        sample_info *sample = sample_get(s1);
        if(!sample) return 0;
	if(type != NULL)
		*type = sample->effect_chain[entry]->kf_type;

	return sample->effect_chain[entry]->kf_status;
}

void	sample_set_kf_type(int s1, int entry, int type )
{
        sample_info *sample = sample_get(s1);
        if(!sample) return;

	sample->effect_chain[entry]->kf_type = type;
}


int	sample_chain_set_kf_status( int s1, int entry, int status )
{
   sample_info *sample = sample_get(s1);
   if (!sample)
	return -1;
   sample->effect_chain[entry]->kf_status = status;
   return 1;	
}

unsigned char *	sample_chain_get_kfs( int s1, int entry, int parameter_id, int *len )
{
   sample_info *sample = sample_get(s1);
   if (!sample)
	return NULL;
   if ( entry < 0 || entry > SAMPLE_MAX_EFFECTS )
        return NULL;
   if( parameter_id < 0 || parameter_id > 9 )
	return NULL;
   if( sample->effect_chain[entry]->kf == NULL )
	   return NULL;

   unsigned char *data = keyframe_pack( sample->effect_chain[entry]->kf, parameter_id, entry,len );
   if( data )
	return data;
   return NULL;
}

int	sample_chain_set_kfs( int s1, int len, char *data )
{
   sample_info *sample = sample_get(s1);
   if (!sample)
	return -1;
   if( len <= 0 )
	return 0;

   int entry = 0;
   if(!keyframe_unpack( (unsigned char*) data, len, &entry,s1,1 ))
   {
	veejay_msg(0, "Unable to unpack keyframe packet");
	return -1;
   }
   return 1;
}
int sample_chain_add(int s1, int c, int effect_nr)
{
    int effect_params = 0, i;
    sample_info *sample = sample_get(s1);
    if (!sample)
		return 0;
    if (c < 0 || c >= SAMPLE_MAX_EFFECTS)
		return 0;

/*	if ( effect_nr < VJ_IMAGE_EFFECT_MIN ) return -1;

	if ( effect_nr > VJ_IMAGE_EFFECT_MAX && effect_nr < VJ_VIDEO_EFFECT_MIN )
		return -1;
*/

    if( sample->effect_chain[c]->effect_id != -1 && sample->effect_chain[c]->effect_id != effect_nr )
    {
	//verify if the effect should be discarded
	if(vj_effect_initialized( sample->effect_chain[c]->effect_id, sample->effect_chain[c]->fx_instance )  ) 
	{
		if(!vj_effect_is_plugin( sample->effect_chain[c]->effect_id ) ) {
			int i  = 0;
			int frm = 1;
			
			for( i = 0; i < SAMPLE_MAX_EFFECTS ; i ++ ) {
			        if( i == c )
			       		continue;
			 	if( sample->effect_chain[i]->effect_id == effect_nr )
		 			frm = 0;
			}
			
			if( frm == 1 ) {
				vj_effect_deactivate( sample->effect_chain[c]->effect_id, sample->effect_chain[c]->fx_instance );
				sample->effect_chain[c]->fx_instance = NULL;
			}
		} else {
			vj_effect_deactivate( sample->effect_chain[c]->effect_id, sample->effect_chain[c]->fx_instance );
			sample->effect_chain[c]->fx_instance = NULL;
		}
	}
	
    }


    if(!vj_effect_initialized(effect_nr, sample->effect_chain[c]->fx_instance) )
    {
		int res = 0;
		sample->effect_chain[c]->fx_instance = vj_effect_activate( effect_nr, &res );
		if(!res)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot activate %d", effect_nr);
			//@ clear
			sample->effect_chain[c]->effect_id = -1;
			sample->effect_chain[c]->e_flag = 1;
			int i;
			for( i = 0; i < SAMPLE_MAX_PARAMETERS; i ++ )
				sample->effect_chain[c]->arg[i] = 0;
	
			sample->effect_chain[c]->frame_trimmer = 0;
			return 0;
		}
    }

    effect_params = vj_effect_get_num_params(effect_nr);
	if( sample->effect_chain[c]->effect_id != effect_nr ) {
		if( effect_params > 0 ) {
			/* there are parameters, set default values */
			for (i = 0; i < effect_params; i++)
		    {
			    int val = vj_effect_get_default(effect_nr, i);
			    sample->effect_chain[c]->arg[i] = val;
			}
		}
		/* effect enabled standard */
		sample->effect_chain[c]->e_flag = 1;
		//clear fx anim
		sample->effect_chain[c]->kf_status = 0;
		sample->effect_chain[c]->kf_type = 0;
		if(sample->effect_chain[c]->kf)
			vpf(sample->effect_chain[c]->kf );
		sample->effect_chain[c]->kf = NULL;
    }

    sample->effect_chain[c]->effect_id = effect_nr;

    if (vj_effect_get_extra_frame(effect_nr))
    {
		//sample->effect_chain[c]->frame_offset = 0;
		sample->effect_chain[c]->frame_trimmer = 0;

		if(s1 > 1)
			s1 = s1 - 1;
		if(!sample_exists(s1)) s1 = s1 + 1;

		if(sample->effect_chain[c]->channel <= 0)
			sample->effect_chain[c]->channel = sample_size()-1; // follow newest
		if(sample->effect_chain[c]->source_type < 0)
			sample->effect_chain[c]->source_type = 0;

        veejay_msg(VEEJAY_MSG_DEBUG,"Effect %s on entry %d overlaying with sample %d",
			vj_effect_get_description(sample->effect_chain[c]->effect_id),c,sample->effect_chain[c]->channel);
    }


    return 1;			/* return position on which it was added */
}

int sample_reset_offset(int s1)
{
	sample_info *sample = sample_get(s1);
	int i;
	if(!sample) return -1;
	for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
	{
		sample->effect_chain[i]->frame_offset = 0;
	}
	return 1;
}

int sample_set_offset(int s1, int chain_entry, int frame_offset)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
	return -1;
    /* set to zero if frame_offset is greater than sample length */
    //if(frame_offset > (sample->last_frame - sample->first_frame)) frame_offset=0;
    sample->effect_chain[chain_entry]->frame_offset = frame_offset;
    return 1;
}

int sample_set_trimmer(int s1, int chain_entry, int trimmer)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
	return -1;
    /* set to zero if frame_offset is greater than sample length */
    if (chain_entry < 0 || chain_entry >= SAMPLE_MAX_PARAMETERS)
	return -1;
    if (trimmer > (sample->last_frame - sample->first_frame))
	trimmer = 0;
    if (trimmer < 0 ) trimmer = 0;
    sample->effect_chain[chain_entry]->frame_trimmer = trimmer;

    return 1;
}
int sample_set_chain_audio(int s1, int chain_entry, int val)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
	return -1;
    if (chain_entry < 0 || chain_entry >= SAMPLE_MAX_PARAMETERS)
	return -1;
    sample->effect_chain[chain_entry]->a_flag = val;
    return 1;
}

int sample_set_chain_volume(int s1, int chain_entry, int volume)
{
    sample_info *sample = sample_get(s1);
    if (!sample)
	return -1;
    /* set to zero if frame_offset is greater than sample length */
    if (volume < 0)
	volume = 100;
    if (volume > 100)
	volume = 0;
    sample->effect_chain[chain_entry]->volume = volume;
    return 1;
}




/****************************************************************************************************
 *
 * sample_chain_clear( sample_nr )
 *
 * clear the entire effect chain.
 * 
 ****************************************************************************************************/

int sample_chain_clear(int s1)
{
    int i, j;
    sample_info *sample = sample_get(s1);

    if (!sample)
	return -1;
    /* the effect chain is gonna be empty! */
    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
	if(sample->effect_chain[i]->effect_id != -1)
	{
		if(vj_effect_initialized( sample->effect_chain[i]->effect_id, sample->effect_chain[i]->fx_instance )) {
			vj_effect_deactivate( sample->effect_chain[i]->effect_id, sample->effect_chain[i]->fx_instance ); 
			sample->effect_chain[i]->fx_instance = NULL;
		}
		
	}
	sample->effect_chain[i]->effect_id = -1;
	sample->effect_chain[i]->frame_offset = 0;
	sample->effect_chain[i]->frame_trimmer = 0;
	sample->effect_chain[i]->volume = 0;
	sample->effect_chain[i]->a_flag = 0;
	if( sample->effect_chain[i]->kf )	
		vpf( sample->effect_chain[i]->kf );
	sample->effect_chain[i]->kf = NULL;
	int src_type = sample->effect_chain[i]->source_type;
	int id       = sample->effect_chain[i]->channel;
	if( src_type == 0 && id > 0 )
	{
		sample_info *old = sample_get( id );
		if(old && old->edit_list)
		{
			vj_el_clear_cache(old->edit_list);
		}
	}
	if( sample->effect_chain[i]->source_type == 1 && 
		vj_tag_get_active( sample->effect_chain[i]->channel ) &&
	 	vj_tag_get_type( sample->effect_chain[i]->channel ) == VJ_TAG_TYPE_NET ) {
		vj_tag_disable( sample->effect_chain[i]->channel );
	}

	sample->effect_chain[i]->source_type = 0;
	sample->effect_chain[i]->channel = s1;
	for (j = 0; j < SAMPLE_MAX_PARAMETERS; j++)
	    sample->effect_chain[i]->arg[j] = 0;
    }

    return 1;
}


/****************************************************************************************************
 *
 * sample_chain_size( sample_nr )
 *
 * returns the number of effects in the effect_chain 
 *
 ****************************************************************************************************/
int sample_chain_size(int s1)
{
    int i, e;

    sample_info *sample;
    sample = sample_get(s1);
    if (!sample)
	return -1;
    e = 0;
    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++)
	if (sample->effect_chain[i]->effect_id != -1)
	    e++;
    return e;
}

/****************************************************************************************************
 *
 * sample_chain_get_free_entry( sample_nr )
 *
 * returns last available entry 
 *
 ****************************************************************************************************/
int sample_chain_get_free_entry(int s1)
{
    int i;
    sample_info *sample;
    sample = sample_get(s1);
    if (!sample)
	return -1;
    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++)
	if (sample->effect_chain[i]->effect_id == -1)
	    return i;
    return -1;
}


/****************************************************************************************************
 *
 * sample_chain_remove( sample_nr, position )
 *
 * Removes an Effect from the chain of sample <sample_nr> on entry <position>
 *
 ****************************************************************************************************/


static int _sample_can_free(sample_info *sample, int reserved, int effect_id)
{
	int i;

	if( vj_effect_is_plugin(effect_id ) )
		return 1;

	for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
	{
		if(i != reserved && effect_id == sample->effect_chain[i]->effect_id) 
			return 0;
	}
	return 1;
}


int sample_chain_remove(int s1, int position)
{
    int j;
    sample_info *sample;
    sample = sample_get(s1);
    if (!sample)
	return -1;
    if (position < 0 || position >= SAMPLE_MAX_EFFECTS)
	return -1;
    if(sample->effect_chain[position]->effect_id != -1)
    {
	if(vj_effect_initialized( sample->effect_chain[position]->effect_id, sample->effect_chain[position]->fx_instance ) && _sample_can_free( sample, position, sample->effect_chain[position]->effect_id) ) { 
		vj_effect_deactivate( sample->effect_chain[position]->effect_id, sample->effect_chain[position]->fx_instance);    
		sample->effect_chain[position]->fx_instance = NULL;
	}
    }
    sample->effect_chain[position]->effect_id = -1;
    sample->effect_chain[position]->frame_offset = 0;
    sample->effect_chain[position]->frame_trimmer = 0;
    sample->effect_chain[position]->volume = 0;
    sample->effect_chain[position]->a_flag = 0;

	if( sample->effect_chain[position]->kf )
		vpf( sample->effect_chain[position]->kf );
	sample->effect_chain[position]->kf = NULL;

 	int src_type = sample->effect_chain[position]->source_type;
	int id       = sample->effect_chain[position]->channel;
	if( src_type == 0 && id > 0 )
	{
		sample_info *old = sample_get( id );
		if(old)
			vj_el_clear_cache(old->edit_list);
	}

	if( sample->effect_chain[position]->source_type == 1 && 
		vj_tag_get_active( sample->effect_chain[position]->channel ) &&
	 	vj_tag_get_type( sample->effect_chain[position]->channel ) == VJ_TAG_TYPE_NET ) {
		vj_tag_disable( sample->effect_chain[position]->channel );
	}

    sample->effect_chain[position]->source_type = 0;
    sample->effect_chain[position]->channel = 0;
    for (j = 0; j < SAMPLE_MAX_PARAMETERS; j++)
		sample->effect_chain[position]->arg[j] = 0;

    return 1;
}

int sample_set_loop_dec(int s1, int active) {
    sample_info *sample = sample_get(s1);
    if(!sample) return -1;
    sample->loop_dec = active;
    return 1;
}

int sample_get_loop_dec(int s1) {
    sample_info *sample = sample_get(s1);
    if(!sample) return -1;
    return sample->loop_dec;
}

editlist *sample_get_editlist(int s1)
{
	sample_info *sample = sample_get(s1);
	if(!sample) return NULL;
	return sample->edit_list;
}

//@ is sample k in fx chain ?
int	sample_cached(sample_info *s, int b_sample )
{
	int i = 0;
	for( i = 0; i < SAMPLE_MAX_EFFECTS ;i++ )
	  if( s->effect_chain[i]->source_type == 0 && s->effect_chain[i]->channel == b_sample)
		return 1;
        return 0;
}

void	sample_chain_alloc_kf( int s1, int entry )
{
	sample_info *sample = sample_get(s1);
    	if(!sample) return;
	sample->effect_chain[entry]->kf = vpn( VEVO_ANONYMOUS_PORT );
}

int	sample_set_editlist(int s1, editlist *edl)
{
	sample_info *sample = sample_get(s1);
	if(!sample) return -1;
	if(sample->edit_list)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sample %d already has EDL", s1 );
		return 0;
	}
	sample->edit_list = edl;
	sample->soft_edl = 1;
	return 1;
}

int sample_apply_loop_dec(int s1, double fps) {
    sample_info *sample = sample_get(s1);
    if(!sample) return -1;
/*    if(sample->loop_dec==1) {
	if( (sample->first_frame + inc) >= sample->last_frame) {
		sample->first_frame = sample->last_frame-1;
		sample->loop_dec = 0;
	}
	else {
		sample->first_frame += (inc / sample->loop_periods);
	}
	veejay_msg(VEEJAY_MSG_DEBUG, "New starting postions are %ld - %ld",
		sample->first_frame, sample->last_frame);
	return ( sample_update(sample, s1));
    }*/

	sample->loop_dec ++;
    
    return 1;
}


/* print sample status information into an allocated string str*/
//int sample_chain_sprint_status(int s1, int entry, int changed, int r_changed,char *str,
//			       int frame)
int	sample_chain_sprint_status( int s1,int cache,int sa,int ca, int pfps, int frame, int mode,int total_slots, int seq_rec,int curfps, uint32_t lo, uint32_t hi,int macro,char *str )
{
	sample_info *sample;
	sample = sample_get(s1);

    if (!sample)
    {
		return -1;
	}
	int e_a, e_d, e_s;
	if( sa && seq_rec)
	{
		sample_info *rs = sample_get( seq_rec );
		e_a = rs->encoder_active;
		e_d = rs->encoder_frames_to_record;
		e_s = rs->encoder_total_frames_recorded;
	}
	else
	{
		e_a = sample->encoder_active;
		e_d = sample->encoder_frames_to_record;
		e_s = sample->encoder_total_frames_recorded;
	}

	char *ptr = str;
	ptr = vj_sprintf( ptr, pfps ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, frame ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, mode ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, s1 ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, sample->effect_toggle ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, sample->first_frame ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, sample->last_frame ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, sample->speed ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, sample->looptype ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, e_a); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, e_d ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, e_s ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, sample_size() ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, sample->marker_start ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, sample->marker_end ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, sample->selected_entry ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, total_slots ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, cache ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, curfps ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, lo ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, hi ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, sa ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, ca ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, (int) sample->fader_val ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, sample->dup ); *ptr++ = ' ';
	ptr = vj_sprintf( ptr, macro );

	return 0;
}


#ifdef HAVE_XML2
/*************************************************************************************************
 *
 * UTF8toLAT1()
 *
 * convert an UTF8 string to ISO LATIN 1 string 
 *
 ****************************************************************************************************/
char *UTF8toLAT1(unsigned char *in)
{
    if (in == NULL)
		return NULL;

	int in_size = strlen( (char*) in ) + 1;
	int out_size = in_size;
    unsigned char *out = malloc((size_t) out_size);

    if (out == NULL) {
		return NULL;
    }

    if (UTF8Toisolat1(out, &out_size, in, &in_size) != 0)
	{
		veejay_memcpy( out, in, out_size );
    }

    out = realloc(out, out_size + 1);
    out[out_size] = 0;		/*null terminating out */

    return (char*) out;
}

/*************************************************************************************************
 *
 * ParseArguments()
 *
 * Parse the effect arguments using libxml2
 *
 ****************************************************************************************************/
void ParseArguments(xmlDocPtr doc, xmlNodePtr cur, int *arg)
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
	cur = cur->next;
    }
}

static void	ParseKeys( xmlDocPtr doc, xmlNodePtr cur, void *port )
{
	while( cur != NULL )
	{
		if(!xmlStrcmp( cur->name, (const xmlChar*) "KEYFRAMES" ))
		{
			keyframe_xml_unpack( doc, cur->xmlChildrenNode, port );
		}
		cur = cur->next;
	}
}


/*************************************************************************************************
 *
 * ParseEffect()
 *
 * Parse an effect using libxml2
 *
 ****************************************************************************************************/
void ParseEffect(xmlDocPtr doc, xmlNodePtr cur, int dst_sample, int start_at)
{
    xmlChar *xmlTemp = NULL;
    char *chTemp = NULL;
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
    int kf_status = 0;
    int kf_type = 0;
    xmlNodePtr anim = NULL;

    for (i = 0; i < SAMPLE_MAX_PARAMETERS; i++) {
	arg[i] = 0;
    }

    if (cur == NULL)
	return;

    int k = 0;

    while (cur != NULL) {
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTID)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		effect_id = atoi(chTemp);
		free(chTemp);
		k ++;
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

	    ParseArguments(doc, cur->xmlChildrenNode, arg);
	}

	if( !xmlStrcmp(cur->name, (const xmlChar*) "ANIM" ))
	{
		anim = cur->xmlChildrenNode;
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
		channel = ( atoi(chTemp) ) + start_at;
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
	
	if(!xmlStrcmp( cur->name, (const xmlChar*) "kf_status" )) {
	   xmlTemp = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
  	   chTemp = UTF8toLAT1(xmlTemp);
	   if(chTemp)
		{  kf_status = atoi(chTemp); free(chTemp); }
	   if(xmlTemp) xmlFree(xmlTemp);
	}
	if(!xmlStrcmp( cur->name, (const xmlChar*) "kf_type" )) {
	   xmlTemp = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
  	   chTemp = UTF8toLAT1(xmlTemp);
	   if(chTemp) {
		   kf_type = atoi(chTemp); free(chTemp);
	   }
	   if(xmlTemp) xmlFree(xmlTemp);
	}

	xmlTemp = NULL;
	chTemp = NULL;
	cur = cur->next;
    }


    if (effect_id != -1) {
		int j;
		if (!sample_chain_add(dst_sample, chain_index, effect_id)) {
			veejay_msg(VEEJAY_MSG_ERROR, "Error parsing effect %d (pos %d)", effect_id, chain_index);
		}
		else {
			/* load the parameter values */
			for (j = 0; j < vj_effect_get_num_params(effect_id); j++) {
			    sample_set_effect_arg(dst_sample, chain_index, j, arg[j]);
			}
			sample_set_chain_channel(dst_sample, chain_index, channel);
			sample_set_chain_source(dst_sample, chain_index, source_type);

			/* set other parameters */
			if (a_flag) {
			    sample_set_chain_audio(dst_sample, chain_index, a_flag);
			    sample_set_chain_volume(dst_sample, chain_index, volume);
			}
	
			if( effect_id != -1 ) {
				sample_set_chain_status(dst_sample, chain_index, e_flag);
				sample_set_offset(dst_sample, chain_index, frame_offset);
				sample_set_trimmer(dst_sample, chain_index, frame_trimmer);
			}
		
			sample_info *skel = sample_get(dst_sample);
			if(anim)
			{
				sample_chain_alloc_kf( dst_sample, chain_index );
				ParseKeys( doc, anim, skel->effect_chain[ chain_index ]->kf );
				sample_chain_set_kf_status( dst_sample, chain_index, kf_status );
				sample_set_kf_type(dst_sample,chain_index,kf_type);
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
void ParseEffects(xmlDocPtr doc, xmlNodePtr cur, sample_info * skel, int start_at)
{
    int effectIndex = 0;
    while (cur != NULL && effectIndex < SAMPLE_MAX_EFFECTS) {
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECT)) {
	    ParseEffect(doc, cur->xmlChildrenNode, skel->sample_id, start_at);
		effectIndex++;
	}
	//effectIndex++;
	cur = cur->next;
    }
}
void	ParseCalibration( xmlDocPtr doc, xmlNodePtr cur, sample_info *skel , void *vp)
{
	void *tmp = viewport_load_xml( doc, cur, vp );
	if( tmp ) 
		skel->viewport_config = tmp;
}

void	LoadCurrentPlaying( xmlDocPtr doc, xmlNodePtr cur , int *id, int *mode )
{
	char *chTemp = NULL;
	unsigned char *xmlTemp = NULL;
        while (cur != NULL)
        {
                if (!xmlStrcmp(cur->name, (const xmlChar *) "PLAYING_ID")) {
			xmlTemp = xmlNodeListGetString(doc,cur->xmlChildrenNode,1 );
			chTemp = UTF8toLAT1(xmlTemp);
			if(chTemp) {
				int s_id = atoi(chTemp );
				*id = s_id;
				xmlFree(chTemp);
			}
			if( xmlTemp ) free(xmlTemp);
		}
	
		if( !xmlStrcmp(cur->name, (const xmlChar*) "PLAYING_MODE" ))
		{
			xmlTemp = xmlNodeListGetString( doc,cur->xmlChildrenNode,1 );
			chTemp = UTF8toLAT1( xmlTemp );
			if(chTemp ) {
				int s_pm = atoi( chTemp );
				*mode = s_pm;
				xmlFree(chTemp);
			}
			if( xmlTemp ) free(xmlTemp);
		}

		cur = cur->next;
	}

}

void	LoadSequences( xmlDocPtr doc, xmlNodePtr cur, void *seq, int n_samples )
{
	seq_t *s = (seq_t*) seq;

	int i;
	xmlChar *xmlTemp = NULL;
	char *chTemp = NULL;

	int tmp_seq[MAX_SEQUENCES];
	int tmp_idx = 0;

	veejay_memset( &tmp_seq, 0, sizeof(tmp_seq));  

	while (cur != NULL)
	{
		if (!xmlStrcmp(cur->name, (const xmlChar *) "SEQ_ID")) {
			xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    		chTemp = UTF8toLAT1(xmlTemp);
	    		if (chTemp) {
				int id = atoi( chTemp );
				if( tmp_idx < MAX_SEQUENCES && id > 0 ) {
					tmp_seq[ tmp_idx ] = id;
					tmp_idx ++;
				}
				free(chTemp);
	    		}
			if( xmlTemp )
				xmlFree(xmlTemp);
		}
		cur = cur->next;
	}

	if( tmp_idx == 0 )
		return;

	if( s->size == 0 ) {
		veejay_memcpy( s->samples, &tmp_seq, tmp_idx * sizeof(int));
		s->size = tmp_idx;
		return;
	} 

	if( (s->size + tmp_idx ) < MAX_SEQUENCES ) {
		for( i = 0; i < tmp_idx; i ++ ) {
			s->samples[ s->size + i ] = tmp_seq[ i ] + n_samples;
		}
		s->size = s->size + tmp_idx;
	} else {
		veejay_msg(VEEJAY_MSG_DEBUG, "Can't load this sequence, sequence bank is full.");
	}
}

/*************************************************************************************************
 *
 * ParseSample()
 *
 * Parse a sample
 *
 ****************************************************************************************************/
xmlNodePtr ParseSample(xmlDocPtr doc, xmlNodePtr cur, sample_info * skel,void *el, void  *font, int start_at, void *vp )
{

    xmlChar *xmlTemp = NULL;
    char *chTemp = NULL;
    xmlNodePtr subs = NULL;

    if(!sample_read_edl( skel )) {
	skel->edit_list = NULL;
    }

    if(!skel->edit_list)
    {
	veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d has inherited EDL from plain mode", skel->sample_id );
    	skel->edit_list = el;
	skel->soft_edl = 1;
    } else {
	skel->soft_edl = 0;
	veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d has own EDL (%p)", skel->sample_id, el );
    }
    while (cur != NULL) {
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_SAMPLEID)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		skel->sample_id = ( atoi(chTemp) ) + start_at;
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if( !xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_BOGUSVIDEO ) )
	{
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
            if (chTemp) {
                skel->play_length = atoi(chTemp);
                free(chTemp);
            }
            if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EDIT_LIST_FILE)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		skel->edit_list_file = strdup(chTemp);
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}

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
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_SAMPLEDESCR)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		snprintf(skel->descr, SAMPLE_MAX_DESCR_LEN,"%s", chTemp);
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}

	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_FIRSTFRAME)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		sample_set_startframe(skel->sample_id, atol(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_VOL)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		sample_set_audio_volume(skel->sample_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);

	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_LASTFRAME)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		sample_set_endframe(skel->sample_id, atol(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_SPEED)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		sample_set_speed(skel->sample_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_FRAMEDUP)) {
		xmlTemp = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
		chTemp = UTF8toLAT1(xmlTemp);
		if(chTemp)
		{
			sample_set_framedup(skel->sample_id, atoi(chTemp));
			free(chTemp);
		}
		if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_LOOPTYPE)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		sample_set_looptype(skel->sample_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	
	if (!xmlStrcmp(cur->name, (const xmlChar *) "subrender")) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		sample_set_subrender(skel->sample_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}

	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_MAXLOOPS)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		sample_set_loops(skel->sample_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_NEXTSAMPLE)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		sample_set_next(skel->sample_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_DEPTH)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		sample_set_depth(skel->sample_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_PLAYMODE)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		sample_set_playmode(skel->sample_id, atoi(chTemp));
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
	if(!xmlStrcmp(cur->name,(const xmlChar*) XMLTAG_LASTENTRY)) {
		xmlTemp = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
		chTemp = UTF8toLAT1(xmlTemp);
		if(chTemp) {
			skel->selected_entry = atoi(chTemp);
			free(chTemp);
		}
		if(xmlTemp) xmlFree(xmlTemp);
	}	
	/*
	   if (!xmlStrcmp(cur->name, (const xmlChar *)XMLTAG_VOLUME)) {
	   xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	   chTemp = UTF8toLAT1( xmlTemp );
	   if( chTemp ){
	   //sample_set_volume(skel->sample_id, atoi(chTemp ));
	   }
	   }
	   if (!xmlStrcmp(cur->name, (const xmlChar *)XMLTAG_SUBAUDIO)) {
	   xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	   chTemp = UTF8toLAT1( xmlTemp );
	   if( chTemp ){
	   sample_set_sub_audio(skel->sample_id, atoi(chTemp ));
	   }
	   }
	 */
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_MARKERSTART)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		sample_set_marker_start(skel->sample_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_MARKEREND)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		sample_set_marker_end(skel->sample_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);

	}

	if(!xmlStrcmp(cur->name, (const xmlChar*) "SUBTITLES" ))
	{
		subs = cur->xmlChildrenNode;
	//	vj_font_xml_unpack( doc, cur->xmlChildrenNode, font );
	}

	ParseEffects(doc, cur->xmlChildrenNode, skel, start_at);

	if( !xmlStrcmp( cur->name, (const xmlChar*) "calibration" ) ) {
		ParseCalibration( doc, cur->xmlChildrenNode, skel ,vp);
	}



	// xmlTemp and chTemp should be freed after use
	xmlTemp = NULL;
	chTemp = NULL;

	cur = cur->next;
    }
  //  if(!sample_read_edl( skel ))
//	veejay_msg(VEEJAY_MSG_ERROR, "No EDL '%s' for sample %d", skel->edit_list_file, skel->sample_id );



    return subs;
}


/****************************************************************************************************
 *
 * sample_readFromFile( filename )
 *
 * load samples and effect chain from an xml file. 
 *
 ****************************************************************************************************/
int sample_read_edl( sample_info *sample )
{
	char *files[1] = {0};
	int res = 0;

	files[0] = sample->edit_list_file;

	void *old = sample->edit_list;

	//EDL is stored in CWD, samplelist file can be anywhere. Cannot always load samplelists due to
	//    missing EDL files in CWD.
	veejay_msg(VEEJAY_MSG_DEBUG, "Loading '%s' from video sample from current working directory" , files[0] );

	sample->edit_list = vj_el_init_with_args( files,1,
			__sample_project_settings.flags,
			__sample_project_settings.deinterlace,
			__sample_project_settings.force,
			__sample_project_settings.norm,
			__sample_project_settings.fmt,
		    __sample_project_settings.width,
			__sample_project_settings.height);

	if(sample->edit_list)
	{
		res = 1;
		sample->soft_edl = 0;

		if( old ) {
			sample_close_edl( sample->sample_id, old );
		}
	}
	else 
	{
		sample->edit_list = old;
	}

	return res;
}

int	is_samplelist(char *filename)
{
	xmlDocPtr doc;
	xmlNodePtr cur;
   	doc = xmlParseFile(filename);
   	if (doc == NULL) 
		return (0);
    	
    	cur = xmlDocGetRootElement(doc);
    	if (cur == NULL)
	{
		xmlFreeDoc(doc);
		return (0);
	}

    	if (xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_SAMPLES))
	{
		xmlFreeDoc(doc);
		return (0);
    	}

   	xmlFreeDoc(doc);

	return 1;
}

void	LoadSubtitles( sample_info *skel, char *file, void *font )
{
	char tmp[512];

	snprintf(tmp,sizeof(tmp), "%s-SUB-%d.srt", file,skel->sample_id );
	vj_font_load_srt( font, tmp );
}

int sample_readFromFile(char *sampleFile, void *vp, void *seq, void *font, void *el,int *id, int *mode)
{
    xmlDocPtr doc;
    xmlNodePtr cur;
    sample_info *skel;

    /*
     * build an XML tree from a the file;
     */
    doc = xmlParseFile(sampleFile);
    if (doc == NULL) {
	return (0);
    }

    /*
     * Check the document is of the right kind
     */

    int start_at = sample_size()-1;
    if( start_at != 0 )
	veejay_msg(VEEJAY_MSG_INFO, "Merging %s into current samplelist, auto number starts at %d", sampleFile, start_at );
    if( vj_tag_size()-1 > 0 )
	veejay_msg(VEEJAY_MSG_INFO, "Existing streams will be deleted (samplelist overrides active streams)");

	if( start_at <= 0 )
		start_at = 0;

    cur = xmlDocGetRootElement(doc);
    if (cur == NULL) {
	veejay_msg(VEEJAY_MSG_ERROR,"Empty samplelist. Nothing to do.\n");
	xmlFreeDoc(doc);
	return (0);
    }

    if (xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_SAMPLES)) {
	veejay_msg(VEEJAY_MSG_ERROR, "This is not a samplelist: %s",
		XMLTAG_SAMPLES);
	xmlFreeDoc(doc);
	return (0);
    }

    cur = cur->xmlChildrenNode;
    while (cur != NULL) {
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_SAMPLE)) {
	    skel = sample_skeleton_new(0, 1);
	    sample_store(skel);
	    if (skel != NULL) {
		void *d = vj_font_get_dict(font);

		xmlNodePtr subs = ParseSample( doc, cur->xmlChildrenNode, skel, el, font, start_at ,vp );	
		if(subs)
	    	{
			LoadSubtitles( skel, sampleFile, font );
			vj_font_xml_unpack( doc, subs, font );
		}
	
		vj_font_set_dict(font,d);

	    }
	}
	if( !xmlStrcmp( cur->name, (const xmlChar*) "CURRENT" )) {
		LoadCurrentPlaying( doc, cur->xmlChildrenNode, id, mode );
	}

        if( !xmlStrcmp( cur->name, (const xmlChar *) "SEQUENCE" )) {
		LoadSequences( doc, cur->xmlChildrenNode,seq, start_at );
	}

	if( !xmlStrcmp( cur->name, (const xmlChar*) "stream" )) {
		tagParseStreamFX( sampleFile, doc, cur->xmlChildrenNode, font,vp );
	}

	cur = cur->next;
    }
    xmlFreeDoc(doc);

    return (1);
}

void CreateArguments(xmlNodePtr node, int *arg, int argcount)
{
    int i;
    char buffer[100];
    for (i = 0; i < argcount; i++) {
	    sprintf(buffer, "%d", arg[i]);
	    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_ARGUMENT,
			(const xmlChar *) buffer);
    }
}

void	CreateKeys( xmlNodePtr node, int argcount, void *port )
{
	int i;
	for( i = 0; i < argcount ; i++ )
	{
		xmlNodePtr childnode = xmlNewChild(node, NULL, (const xmlChar*) "KEYFRAMES", NULL);
		keyframe_xml_pack( childnode, port, i );
	}
}

void CreateEffect(xmlNodePtr node, sample_eff_chain * effect, int position)
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
	xmlNewChild(node,NULL,(const xmlChar*) "kf_status", (const xmlChar*) buffer );
   
	sprintf(buffer, "%d", effect->kf_type );
	xmlNewChild(node,NULL,(const xmlChar*) "kf_type", (const xmlChar*) buffer );

    childnode =
	xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_ARGUMENTS, NULL);
    CreateArguments(childnode, effect->arg,
		    vj_effect_get_num_params(effect->effect_id));

    if( effect->kf != NULL ) {
   	 childnode =
		xmlNewChild(node,NULL,(const xmlChar*) "ANIM", NULL );
   	 CreateKeys( childnode, vj_effect_get_num_params(effect->effect_id), effect->kf );
    }
}


void CreateEffects(xmlNodePtr node, sample_eff_chain ** effects)
{
    int i;
    xmlNodePtr childnode;

    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
	if (effects[i]->effect_id != -1) {
	    childnode =
		xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECT,
			    NULL);
	    CreateEffect(childnode, effects[i], i);
	}
    }
    
}

void	SaveSequences( xmlNodePtr node, void *seq )
{
    char buffer[100];
    int i = 0;
    seq_t *s = (seq_t*) seq;
    for( i = 0; i < MAX_SEQUENCES; i ++ )
    {
	sprintf(buffer, "%d", s->samples[i] );
	xmlNewChild(node, NULL, (const xmlChar*) "SEQ_ID",
		(const xmlChar*) buffer );
    }	
		
}

void	SaveCurrentPlaying( xmlNodePtr node, int id, int mode )
{
	char buffer[100];
	sprintf(buffer, "%d", id );
	xmlNewChild(node, NULL, (const xmlChar*) "PLAYING_ID",
		(const xmlChar*) buffer );
	sprintf(buffer, "%d", mode );
	xmlNewChild(node,NULL, (const xmlChar*) "PLAYING_MODE",
		(const xmlChar*) buffer );

}

void CreateSample(xmlNodePtr node, sample_info * sample, void *font)
{
    char buffer[1024];
    xmlNodePtr childnode;

    sprintf(buffer, "%d", sample->sample_id);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_SAMPLEID,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", sample->effect_toggle);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_CHAIN_ENABLED,
		(const xmlChar *) buffer);

	if(sample->edit_list_file)
	{
    		sprintf(buffer, "%s", sample->edit_list_file);
     		xmlNewChild(node, NULL, (const xmlChar*) XMLTAG_EDIT_LIST_FILE,
			(const xmlChar*) buffer );
	}

    sprintf(buffer, "%s", sample->descr);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_SAMPLEDESCR,
		(const xmlChar *) buffer);
    sprintf(buffer, "%ld", sample->first_frame);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_FIRSTFRAME,
		(const xmlChar *) buffer);
    sprintf(buffer, "%ld", sample->last_frame);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_LASTFRAME,
		(const xmlChar *) buffer);

    sprintf(buffer, "%d", sample->play_length ); 
    xmlNewChild(node, NULL, (const xmlChar*) XMLTAG_BOGUSVIDEO,
		(const xmlChar*) buffer );
    sprintf(buffer, "%d", sample->speed);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_SPEED,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", sample->dup);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_FRAMEDUP,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", sample->looptype);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_LOOPTYPE,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", sample->max_loops);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_MAXLOOPS,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", sample->next_sample_id);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_NEXTSAMPLE,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", sample->depth);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_DEPTH,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", sample->playmode);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_PLAYMODE,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", sample->audio_volume);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_VOL,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", sample->marker_start);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_MARKERSTART,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", sample->marker_end);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_MARKEREND,
		(const xmlChar *) buffer);

	sprintf(buffer,"%d",sample->fader_active);
	xmlNewChild(node,NULL,(const xmlChar *) XMLTAG_FADER_ACTIVE,
		(const xmlChar *) buffer);
	sprintf(buffer,"%f",sample->fader_inc);
	xmlNewChild(node,NULL,(const xmlChar *) XMLTAG_FADER_INC,
		(const xmlChar *) buffer);
	sprintf(buffer,"%f",sample->fader_val);
	xmlNewChild(node,NULL,(const xmlChar *) XMLTAG_FADER_VAL,
		(const xmlChar *) buffer);
	sprintf(buffer,"%d",sample->fader_direction);
	xmlNewChild(node,NULL,(const xmlChar *) XMLTAG_FADER_DIRECTION,
		(const xmlChar *) buffer);
	sprintf(buffer,"%d",sample->selected_entry);
	xmlNewChild(node,NULL,(const xmlChar *) XMLTAG_LASTENTRY,
		(const xmlChar *)buffer);

    sprintf(buffer, "%d", sample->subrender ); 
    xmlNewChild(node, NULL, (const xmlChar*) "subrender",
		(const xmlChar*) buffer );

    vj_font_xml_pack( node, font );


    childnode =
	xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTS, NULL);

    CreateEffects(childnode, sample->effect_chain);

} 

/****************************************************************************************************
 *
 * sample_writeToFile( filename )
 *
 * writes all sample info to a file. 
 *
 ****************************************************************************************************/
static	int sample_write_edl(sample_info *sample)
{
	editlist *edl = sample->edit_list;
	if(edl)
	{
		if(vj_el_write_editlist( sample->edit_list_file,
			sample->first_frame,
			sample->last_frame,
			edl ))
			veejay_msg(VEEJAY_MSG_DEBUG, "Saved EDL '%s' of sample %d",
				sample->edit_list_file, sample->sample_id );
		return 1;
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Sample %d has no EDL", sample->sample_id );
	}
	return 0;
}

void	WriteSubtitles( sample_info *next_sample, void *font, char *file )
{
	char tmp[512];

	void *d = vj_font_get_dict( font );

	sprintf(tmp, "%s-SUB-%d.srt", file,next_sample->sample_id );
	
	vj_font_set_dict( font, next_sample->dict );

	vj_font_save_srt( font, tmp );

	vj_font_set_dict( font, d );
}

void	sample_reload_config(void *compiz, int s1, int mode )
{
	sample_info *sample = sample_get(s1);
	if(sample) {
		if(sample->viewport_config) {
			free(sample->viewport_config);
			sample->viewport_config = NULL;
		}
		if(!sample->viewport_config) {
			veejay_msg(VEEJAY_MSG_DEBUG, "Calibrated sample %d",s1);
			sample->viewport_config = composite_get_config(compiz,mode);
		}	
		sample->composite = mode;
	}
}

int sample_writeToFile(char *sampleFile, void *vp,void *seq, void *font, int id, int mode)
{
    int i;
	char *encoding = "UTF-8";
	xmlChar *version = xmlCharStrdup("1.0");	
    sample_info *next_sample;
    xmlDocPtr doc;
    xmlNodePtr rootnode, childnode;

    doc = xmlNewDoc(version);
    rootnode =
	xmlNewDocNode(doc, NULL, (const xmlChar *) XMLTAG_SAMPLES, NULL);
    xmlDocSetRootElement(doc, rootnode);

    childnode = xmlNewChild( rootnode, NULL,
			(const xmlChar*) "SEQUENCE", NULL );
    SaveSequences( childnode, seq );

  
    childnode = xmlNewChild( rootnode, NULL, (const xmlChar*) "CURRENT" , NULL );
	
    SaveCurrentPlaying( childnode , id, mode );

    for (i = 1; i < sample_size(); i++) {
	next_sample = sample_get(i);
	if (next_sample) {
	    	if(sample_write_edl( next_sample ))
			veejay_msg(VEEJAY_MSG_DEBUG ,"Saved sample %d EDL '%s'", next_sample->sample_id,
				next_sample->edit_list_file );	
	      
	    childnode =
		xmlNewChild(rootnode, NULL,
			    (const xmlChar *) XMLTAG_SAMPLE, NULL);

            WriteSubtitles( next_sample,font, sampleFile );

	    CreateSample(childnode, next_sample, font);
	
	    viewport_save_xml( childnode, next_sample->viewport_config );
	}
    }

    int max = vj_tag_size()-1;
    i = 0; 
    for( i = 1; i <= max; i ++ )
    {
        childnode = xmlNewChild(rootnode,NULL,(const xmlChar*) "stream", NULL );
	tag_writeStream( sampleFile, i, childnode, font ,vp);
    }

    xmlSaveFormatFileEnc( sampleFile, doc, encoding, 1 );
    xmlFreeDoc(doc);
	xmlFree(version);

    return 1;
}
#endif
