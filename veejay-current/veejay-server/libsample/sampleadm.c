/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
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
 *         Create is used to write the Sample to XML, Parse is used to load from XML
 
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
#include <veejaycore/defs.h>
#include <veejaycore/vims.h>
#include <libsample/sampleadm.h>
#include <veejaycore/vj-msg.h>
#include <libvje/vje.h>
#include <libvje/libvje.h>
#include <libsubsample/subsample.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vevo.h>
#include <veejaycore/libvevo.h>
#include <veejay/vjkf.h>
#include <veejay/vj-font.h>
#include <assert.h>
#include <libel/elcache.h>
#include <veejay/vj-misc.h>
#include <veejay/vj-misc.h>
#include <libstream/vj-tag.h>
#include <libvjxml/vj-xml.h>
#include <veejay/vj-macro.h>
#include <libvje/internal.h>
//#define KAZLIB_OPAQUE_DEBUG 1

#ifdef HAVE_XML2
#endif

#define FOURCC(a,b,c,d) ( (d<<24) | ((c&0xff)<<16) | ((b&0xff)<<8) | (a&0xff) )

#define FOURCC_RIFF     FOURCC ('R', 'I', 'F', 'F')
#define FOURCC_WAVE     FOURCC ('W', 'A', 'V', 'E')
#define FOURCC_FMT      FOURCC ('f', 'm', 't', ' ')
#define FOURCC_DATA     FOURCC ('d', 'a', 't', 'a')

static int recount_hash = 1;
static unsigned int sample_count = 0;
static int this_sample_id = 0;  /* next available sample id */
static int next_avail_num = 0;  /* available sample id */
static int initialized = 0; /* whether we are initialized or not */
static hash_t *SampleHash;  /* hash of sample information structs */
static int avail_num[SAMPLE_MAX_SAMPLES];   /* an array of freed sample id's */
static void *sample_font_ = NULL;
static int sampleadm_state = SAMPLE_PEEK;   /* default state */
static void *sample_cache[SAMPLE_MAX_SAMPLES];
static editlist *plain_editlist=NULL; 

extern void tagParseStreamFX(char *file, xmlDocPtr doc, xmlNodePtr cur, void *font, void *vp);
extern void   tag_writeStream( char *file, int n, xmlNodePtr node, void *font, void *vp );
extern int vj_tag_highest_valid_id();
extern int    veejay_sprintf( char *s, size_t size, const char *format, ... );

unsigned int sample_size()
{
    if(recount_hash) {
        sample_count = (unsigned int) hash_count( SampleHash );
        recount_hash = 0;
    }
    return sample_count;
}

int sample_highest()
{
    return this_sample_id;
}

int sample_highest_valid_id()
{
    int id = this_sample_id;
    while( !sample_exists( id ) ) {
        id --;
        if( id <= 0 )
            break;
    }

    return id;
}


int sample_verify() {
   return hash_verify( SampleHash );
}



/****************************************************************************************************
 *
 * int_hash (key )
 * \param key TODO
 * \return
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
 * int_compare (key1, key2)
 * \param key1 TODO
 * \param key2
 * \return
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

/****************************************************************************************************
 *
 * sample_default_edl_name (int )
 * \param s1 numeric Index of the sample to create the name with
 * \return a chararacter pointer do sample name (caller must free)
 *
 * create a sample edl filename based on sample index.
 *
 ****************************************************************************************************/
static char *sample_default_edl_name(int s1)
{
    char tmp_file[1024];
    snprintf(tmp_file,sizeof(tmp_file), "sample_%05d.edl", s1 );
    return strdup( tmp_file );
}

static void sample_close_edl(int s1, editlist *el)
{
    /* check if another sample has same EDL */
    if( el != NULL ) {
            int end = sample_highest_valid_id();
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
void    sample_set_project(int fmt, int deinterlace, int flags, int force, char norm, int w, int h )
{
    __sample_project_settings.fmt = fmt;
    __sample_project_settings.deinterlace = deinterlace;
    __sample_project_settings.flags = flags;
    __sample_project_settings.force = force;
    __sample_project_settings.norm = norm;
    __sample_project_settings.width = w;
    __sample_project_settings.height = h;

} 
void    *sample_get_dict( int sample_id )
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
 * \param len TODO
 * \param font
 * \param pedl
 *
 * call before using any other function as sample_skeleton_new
 *
 ****************************************************************************************************/
int sample_init(int len, void *font, editlist *pedl)
{
    if (!initialized) {
        veejay_memset(avail_num, 0, sizeof(avail_num));    
        this_sample_id = 1; /* do not start with zero */
        SampleHash = hash_create(HASHCOUNT_T_MAX, int_compare, int_hash);
        if(!SampleHash) {
            return 0;
        }
        initialized = 1;
        veejay_memset( &__sample_project_settings,0,sizeof(sample_setting));
    }

    sample_font_ = font;
    plain_editlist = pedl;

    return 1;
}

void    sample_free(void *edl)
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

/****************************************************************************************************
 *
 * sample_skeleton_new(long , long)
 * \param startFrame TODO
 * \param endFrame
 *
 * create a new sample, give start and end of new sample. returns sample info block.
 *
 ****************************************************************************************************/
sample_info *sample_skeleton_new(long startFrame, long endFrame)
{
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
    si->resume_pos = -1;
    si->edit_list = NULL;   // clone later
    si->soft_edl = 1;
    si->speed = 1;
    si->looptype = 1; // normal looping
    si->audio_volume = 50;
    si->marker_start = 0;
    si->marker_end = 0;
    si->effect_toggle = 1;
    si->fade_method = 0;
    si->fade_alpha = 0;
    si->fade_entry = -1;
    si->subrender = 1;
    si->transition_length = 25;
    si->edit_list_file = sample_default_edl_name(si->sample_id);

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
	    si->effect_chain[i]->speed = INT_MAX;
        si->effect_chain[i]->is_rendering = 1;
        si->effect_chain[i]->channel = ( sample_highest_valid_id() <= 0 ? si->sample_id : sample_highest_valid_id());
    }
#ifdef HAVE_FREETYPE
    si->dict = vpn( VEVO_ANONYMOUS_PORT );
#endif

    sample_cache[ si->sample_id ] = (void*) si;

	si->macro = vj_macro_new();

    recount_hash = 1;

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

void    sample_new_simple( void *el, long start, long end )
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
 * \param sample_id TODO
 * \return sample information struct or NULL on error.
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
        copy->effect_chain[i] = &b[i];

        if (copy->effect_chain[i] == NULL)
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Error allocating entry %d in Effect Chain for new sample",i);
            return 0;
        }
        veejay_memcpy( copy->effect_chain[i], org->effect_chain[i], sizeof( sample_eff_chain ) );
    }

    copy->sample_id = _new_id();
    snprintf(copy->descr,SAMPLE_MAX_DESCR_LEN, "Sample %4d", copy->sample_id);

    if(org->edit_list)
    {
        copy->edit_list = vj_el_clone( org->edit_list );
        copy->soft_edl = 1;
    }

    if (sample_store(copy) != 0)
        return 0;

    recount_hash = 1;

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
        int len;
        int start,end;
        if( si->marker_start == si->marker_end ) {
           len =  (si->last_frame - si->first_frame );
           start = si->first_frame;
           end = si->last_frame;
        }
        else {
            len = (si->marker_end - si->marker_start );
            start = si->marker_start;
            end = si->marker_end;
        }
        int c = 0;
        int tmp = 0;
        int t=0;
        int _id=0;
        int speed = abs(si->speed);

        si->resume_pos = ( si->speed < 0 ? end : (si->speed > 0 ? start : si->resume_pos ) );

        if( speed == 0 ) {
            veejay_msg(VEEJAY_MSG_WARNING,
                 "Starting paused sample %d at normal speed from position %d",
                    sample_id, si->resume_pos);
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
					int mix_speed = sample_get_speed(_id);
					if(mix_speed != 0) {
						tmp = tmp / mix_speed;
					}
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

int sample_has_cali_fx(int sample_id)
{
    sample_info *si = sample_get(sample_id);
        if(si == NULL)
        return -1;
    int i;
    for( i =0;i < SAMPLE_MAX_EFFECTS; i ++ ) {
        if(si->effect_chain[i]->effect_id == VJ_IMAGE_EFFECT_CALI)
            return i;
    }
    return -1;
}

void    sample_cali_prepare( int sample_id, int slot, int chan )
{
    vj_tag  *tag    = vj_tag_get( chan );
    if( tag == NULL || tag->source_type != VJ_TAG_TYPE_CALI )
        return;

    vj_tag_cali_prepare_now( tag );
    veejay_msg(VEEJAY_MSG_DEBUG, "Prepared calibration data");
}


int sample_get_el_position( int sample_id, int *start, int *end )
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

int sample_get_position(int s1)
{
    sample_info *si = sample_get(s1);

    if(!si) return 0;

    return si->offset;
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
  if(si->effect_toggle == 0) 
      si->effect_toggle = 1;

  return 1;
}

int sample_set_fader_active( int s1, int nframes, int direction ) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  if(nframes <= 0) return -1;
  si->fader_active = 1;

  if(direction<0)
    si->fader_val = 255.0f;
  else
    si->fader_val = 0.0f;

  si->fader_inc = (float) (255.0f / (float) nframes);
  si->fader_direction = direction;
  si->fader_inc *= si->fader_direction;
  /* inconsistency check */
  if(si->effect_toggle == 0)
  {
    si->effect_toggle = 1;
  }
  return 1;
}
int sample_get_fade_alpha(int s1)
{
    sample_info *si = sample_get(s1);
    if(!si) return -1;
    return si->fade_alpha;
}

int sample_get_fade_entry(int s1)
{
    sample_info *si = sample_get(s1);
    if(!si) return -1;
    return si->fade_entry;
}

void sample_set_fade_entry(int s1, int entry)
{
    sample_info *si = sample_get(s1);
    if(!si) return;
    si->fade_entry = entry;
}

void sample_set_fade_method(int s1, int method)
{
    sample_info *si = sample_get(s1);
    if(!si) return;
    si->fade_method = method;
}
void sample_set_fade_alpha(int s1, int alpha)
{
    sample_info *si = sample_get(s1);
    if(!si) return;
    si->fade_alpha = alpha;
}

int sample_reset_fader(int s1) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  si->fader_active = 0;
  si->fader_inc = 0;
  return 1;
}

int sample_get_fader_active(int s1) {
    sample_info *si = sample_get(s1);
    if(!si) return -1;
    return (si->fader_active);
}

int sample_get_fade_method(int s1) {
    sample_info *si = sample_get(s1);
    if(!si) return -1;
    return si->fade_method;
}

float sample_get_fader_val(int s1) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  return si->fader_val;
}

float sample_get_fader_inc(int s1) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  return (si->fader_inc);
}

int sample_get_fader_direction(int s1) {
  sample_info *si = sample_get(s1);
  if(!si) return -1;
  if(si->fader_active == 0)
      return 0; // no direction
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
    if(si->fader_val > 255.0f ) si->fader_val = 255.0f;
    if(si->fader_val < 0.0f ) si->fader_val = 0.0f;
    return (int) si->fader_val;
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

    si->marker_start    = start;
    si->marker_end  = end;
    
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
        int swap            = si->marker_start;
        si->marker_start    = marker;
        si->marker_end      = swap;
    }
    else
    {
        si->marker_end      = marker;
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
    int n = sample_highest();

    for( i = 1; i <= n; i ++ )
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

    n = vj_tag_highest();
    for( i = 1; i <= n; i ++ )
    {
        vj_tag *s = vj_tag_get(i);
        if(s)
        {
            for( j = 0 ; j < SAMPLE_MAX_EFFECTS; j ++ )
            {
                if(s->effect_chain[j]->channel == sample_id &&
                   s->effect_chain[j]->source_type == sample_type )
                {
                    s->effect_chain[j]->channel = i;
                    s->effect_chain[j]->source_type = 1;
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

        sample_chain_free( sample_id,1 );

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
        if(si->edit_list) {
                /* check if another sample has same EDL */
                sample_close_edl( sample_id, si->edit_list );
        }

        /* store freed sample_id */
        avail_num[next_avail_num] = sample_id;
        next_avail_num++;
        hash_delete_free(SampleHash, sample_node);

        sample_cache[ sample_id ] = NULL;
    
		vj_macro_free( si->macro );

        free(si);

        recount_hash = 1;

        return 1;
    }

    return 0;
}

void sample_del_all(void *edl)
{
    int end = sample_highest();
    int i;

    for (i = 1; i <= end; i++) {
        if (!sample_exists(i))
            continue;
    
        sample_chain_clear(i);
        sample_del(i);
    }
     
    veejay_memset( avail_num, 0, sizeof(avail_num) );
    next_avail_num = 0;
    this_sample_id = 0;

    hash_free_nodes( SampleHash );
}
sample_eff_chain **sample_get_effect_chain(int s1)
{
    sample_info *sample = sample_get(s1);
    if(sample == NULL)
        return NULL;
    return sample->effect_chain;
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

void    *sample_get_plugin( int s1, int position, void *ptr)
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
    //  if(position >= SAMPLE_MAX_EFFECTS || position < 0 ) return -1;
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

int sample_get_first_mix_offset(int s1, int *parent, int look_for)
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

void	sample_update_ascociated_samples(int s1)
{
    sample_info *sample = sample_get(s1);
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

int sample_set_resume(int s1,long position)
{
    sample_info *sample = sample_get(s1);
    if(!sample)
        return -1;

    if(position == -1) {
        int start = sample_get_startFrame(s1);
        int end = sample_get_endFrame(s1);

        if( sample->speed < 0) {
            if(sample->resume_pos <= start) {
                sample->speed = sample->speed * -1;
                sample->resume_pos = start;
            }
            else {
                sample->resume_pos = end;
            }
        }
        else if(sample->speed >= 0) {
            if(sample->resume_pos >= end) {
                sample->speed = sample->speed * -1;
                sample->resume_pos = end;
            }
            else {
                sample->resume_pos = start;
            }
        }

        if(sample->offset > 0) {
            sample->resume_pos = sample->offset;
        }

        sample->loop_pp = 0;
    }
    else {
        sample->resume_pos = position;
    }
    return 1;
}
long    sample_get_resume(int s1)
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

int sample_get_frame_length(int s1)
{
	sample_info *sample = sample_get(s1);
    if (!sample)
		return 0;

	int len = 1 + abs( sample->last_frame - sample->first_frame );
	int speed = abs( sample->speed );

	len = len / speed;

	if( sample->dup > 0) {
		len = len * sample->dup;
	}

	if(sample->looptype == 2) {
		len *= 2;
	}

	return len;
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

int sample_var( int s1, int *type, int *fader, int *fx, int *rec, int *active, int *method, int *entry, int *alpha )
{
    sample_info *si = sample_get(s1);
    if(!si) return 0;
    *type  = 0;
    *fader = si->fader_active;
    *fx    = si->effect_toggle;
    *rec   = si->encoder_active;
    *active= 1;
    *method = si->fade_method;
    *entry = si->fade_entry;
    *alpha = si->fade_alpha;
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
        return 0;

    if (position >= SAMPLE_MAX_EFFECTS)
        return 0;
    if (arg_len < 0 || arg_len > SAMPLE_MAX_PARAMETERS)
        return 0;

    if( sample->effect_chain[position]->kf )
    {
     for( i = 0; i < arg_len; i ++ )
     {
        if( sample->effect_chain[position]->kf_status ) {
            int tmp = 0;
            if(!get_keyframe_value( sample->effect_chain[position]->kf, n_frame, i, &tmp ) ) {
                args[i] = sample->effect_chain[position]->arg[i];
            }
            else {
                args[i] = tmp;
            }
        }
        else {
            args[i] = sample->effect_chain[position]->arg[i];
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
    if (vje_get_extra_frame
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
    sample->effect_chain[position]->clear = 1;

    return 1;
}

static  int sample_sample_used(sample_info *a, int b )
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

int sample_stop_playing(int s1, int new_s1)
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

int sample_cache_used( int s1 )
{
    return cache_avail_mb();
}

int sample_start_playing(int s1, int no_cache)
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
    sample->effect_chain[position]->clear = 1;
    
    return 1;
}

void	*sample_get_macro(int s1) {
	sample_info *sample = sample_get(s1);
	if(!sample) return NULL;
	return sample->macro;
}

int	sample_get_loop_stat_stop(int s1) {
	sample_info *sample = sample_get(s1);
		if (!sample) return 0;
	return sample->loop_stat_stop;
}
void sample_set_loop_stat_stop(int s1, int loop_stop) {
	sample_info *sample = sample_get(s1);
	if(!sample) return;
	sample->loop_stat_stop = loop_stop;
}

int	sample_get_loop_stats(int s1) {
	sample_info *sample = sample_get(s1);
		if (!sample) return 0;
	return sample->loop_stat;
}
void sample_set_loop_stats(int s1, int loops) {
	sample_info *sample = sample_get(s1);
	if(!sample) return;
	if( loops == -1) {
		sample->loop_stat = (sample->loop_stat_stop > 0 ? (sample->loop_stat + 1 ) % sample->loop_stat_stop : sample->loop_stat + 1);
	}
	else
		sample->loop_stat = loops;
}

int	sample_get_loops(int s1) {
	sample_info *sample = sample_get(s1);
	if (!sample) return 0;

	return sample->loops;
}

void sample_set_loops(int s1, int loops) {
	sample_info *sample = sample_get(s1);
	if(!sample) return;
	if(loops==-1) {
        int lss = sample->loop_stat_stop;
		sample->loops = (lss > 0 ? lss: 0);
		return;
	}
	sample->loops = loops;
}

int	sample_loop_dec(int s1)
{
	sample_info *sample = sample_get(s1);
	if(!sample) return 0;
    
    if(sample->looptype == 2)
    {
        sample->loop_pp = (sample->loop_pp + 1 ) % 2;
        if( sample->loop_pp == 1 ) {
            return 0;
        }
    }

    if(sample->loops > 0 ) {
	    sample->loops --;
    }

    return (sample->loops == 0 ? 1: 0);
}

int sample_at_next_loop(int s1)
{
    sample_info *sample = sample_get(s1);
    if(!sample)
        return 0;

    int pp = sample->loop_pp;
    int lo = sample->loops;

    if(sample->looptype == 2) {
        pp = ( pp + 1 ) % 2;
        if( pp == 1 )
            return 0;
    }

    if( lo > 0 ) {
        lo --;
    }

    return (lo == 0 ? 1: 0);
}


#define DEFAULT_TRANSITION_LENGTH 25
int sample_get_transition_shape(int s1) {
	sample_info *sample = sample_get(s1);
	if(!sample) return 0;
    return sample->transition_shape;
}

int sample_get_transition_length(int s1) {
    sample_info *sample = sample_get(s1);
	if(!sample) return 0;
    int transition_length = sample->transition_length;
    if (sample->marker_end > 0 && sample->marker_start >= 0) {
        if( transition_length > ( sample->marker_end - sample->marker_start ) )
            transition_length = sample->marker_end - sample->marker_start;
    }
    else {
        if( transition_length > ( sample->last_frame - sample->first_frame ) ) 
            transition_length = sample->last_frame - sample->first_frame;
    }

    return transition_length;
}

void sample_set_transition_shape(int s1, int shape) {
    sample_info *sample = sample_get(s1);
	if(!sample) return;
    sample->transition_shape = shape;
}

void sample_set_transition_length(int s1, int length) {
    sample_info *sample = sample_get(s1);
	if(!sample) return;
    int transition_length = length;
    
    if(transition_length <= 0) {
        transition_length = 0;
    }

    if (sample->marker_end > 0 && sample->marker_start >= 0) {
        if( transition_length > ( sample->marker_end - sample->marker_start ) )
            transition_length = sample->marker_end - sample->marker_start;
    }
    else {
        if( transition_length > ( sample->last_frame - sample->first_frame ) ) 
            transition_length = sample->last_frame - sample->first_frame;
    }
    sample->transition_length = transition_length;
}

int sample_get_transition_active( int s1 ) {
    sample_info *sample = sample_get(s1);
    if(!sample) return 0;
    return sample->transition_active;
}

void sample_set_transition_active(int s1, int status) {
    sample_info *sample = sample_get(s1);
    if(!sample) return;
    sample->transition_active = status;

    if( sample->transition_length <= 0 ) {
        sample_set_transition_length( s1, DEFAULT_TRANSITION_LENGTH);
    }
}

/****************************************************************************************************
 *
 * sample_set_speed
 *
 * store playback speed in the sample.
 * returns -1  on error.
 *
 ****************************************************************************************************/
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
    sample->dups = 0;
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

    if( looptype >= 0 && looptype < 5 ) {
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

int sample_usable_edl( int s1 )
{
    sample_info *sample = sample_get(s1);
    if(!sample) return 0;
    if( sample->play_length )
        return 0;
    if( sample->edit_list )
        return 1;
    return 0;
}

int sample_max_video_length(int s1)
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

int sample_video_length( int s1 )
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

int sample_get_subrender(int s1, int position, int *do_subrender)
{
    sample_info *sample = sample_get(s1);
    if(!sample)
        return 0;
    *do_subrender = sample->effect_chain[position]->is_rendering;
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
    if(sample == NULL)
        return -1;
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

int sample_chain_free(int s1, int global)
{
    sample_info *sample = sample_get(s1);
    int i=0;
    int e_id = 0; 
    int sum = 0;
    if (!sample)
        return -1;
    for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
    {
        if( sample->effect_chain[i]->effect_id == -1 )
            continue;

        vjert_del_fx( sample->effect_chain[i] ,0,i,0);
        sum ++; 
        
        if( sample->effect_chain[i]->source_type == 1 && 
            vj_tag_get_active( sample->effect_chain[i]->channel ) &&
            vj_tag_get_type( sample->effect_chain[i]->channel ) == VJ_TAG_TYPE_NET ) {
                 vj_tag_disable( sample->effect_chain[i]->channel );
        }
    }

    return sum;
}

int sample_chain_reset_kf( int s1, int entry )
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

int sample_get_kf_tokens( int s1, int entry, int id, int *start, int *end, int *type, int *status )
{
    sample_info *sample = sample_get(s1);
    if(!sample) return 0;
    if( sample->effect_chain[entry]->kf == NULL )
        return 0;
    return keyframe_get_tokens( sample->effect_chain[entry]->kf, id, start,end,type, status );
}

void    *sample_get_kf_port( int s1, int entry )
{
    sample_info *sample = sample_get(s1);
        if(!sample) return NULL;
    return sample->effect_chain[entry]->kf;
}

int sample_get_kf_status( int s1, int entry, int *type )
{
        sample_info *sample = sample_get(s1);
        if(!sample) return 0;
    if(type != NULL)
        *type = sample->effect_chain[entry]->kf_type;

    return sample->effect_chain[entry]->kf_status;
}

void    sample_set_kf_type(int s1, int entry, int type )
{
        sample_info *sample = sample_get(s1);
        if(!sample) return;

    sample->effect_chain[entry]->kf_type = type;
}


int sample_chain_set_kf_status( int s1, int entry, int status )
{
   sample_info *sample = sample_get(s1);
   if (!sample)
    return -1;
   sample->effect_chain[entry]->kf_status = status;
   return 1;    
}

void sample_set_offline_recorder( int s1, int duration, int linked_id, int rec_format )
{
    sample_info *sample = sample_get(s1);
    if(sample) {
        sample->offline_record.duration = duration;
        sample->offline_record.linked_id = linked_id;
        sample->offline_record.rec_format = rec_format;
    }
}

int sample_get_offline_recorder( int s1, int *duration, int *linked_id, int *rec_format )
{
    sample_info *sample = sample_get(s1);
    if(!sample)
        return 0;
    
    *duration = sample->offline_record.duration;
    *linked_id = sample->offline_record.linked_id;
    *rec_format = sample->offline_record.rec_format;

    return 1;
}


unsigned char * sample_chain_get_kfs( int s1, int entry, int parameter_id, int *len )
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

int sample_chain_set_kfs( int s1, int len, char *data )
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

    if(!vje_is_valid(effect_nr))
        return 0;

    if( sample->effect_chain[c]->effect_id == -1 ) {
        sample->effect_chain[c]->effect_id = effect_nr;
    }
    else if ( sample->effect_chain[c]->effect_id != effect_nr ) {
        vjert_del_fx( sample->effect_chain[c],0,c,1 );
        sample->effect_chain[c]->effect_id = effect_nr;
    }

    effect_params = vje_get_num_params(effect_nr);
    for (i = 0; i < effect_params; i++)
    {
        sample->effect_chain[c]->arg[i] = vje_get_param_default(effect_nr, i);
    }

    sample->effect_chain[c]->e_flag = 1;
    sample->effect_chain[c]->kf_status = 0;
    sample->effect_chain[c]->kf_type = 0;

    if (vje_get_extra_frame(effect_nr))
    {
        sample->effect_chain[c]->frame_trimmer = 0;
        if(s1 > 1)
            s1 = s1 - 1;
        if(!sample_exists(s1)) s1 = s1 + 1;

        if(sample->effect_chain[c]->channel <= 0)
            sample->effect_chain[c]->channel = sample_highest_valid_id(); // follow newest
        if(sample->effect_chain[c]->source_type < 0)
            sample->effect_chain[c]->source_type = 0;

        veejay_msg(VEEJAY_MSG_DEBUG,"Effect %s on entry %d overlaying with sample %d",
            vje_get_description(sample->effect_chain[c]->effect_id),c,sample->effect_chain[c]->channel);

    }
    else
    {
        if( c == sample->fade_entry ) {
            if( sample->fade_method == 4 )
                sample->fade_method = 2; /* auto switch */
            else if (sample->fade_method == 3 )
                sample->fade_method = 1;
        }
    }
    return 1;           /* return position on which it was added */
}

void	sample_set_chain_paused( int s1, int paused )
{
	sample_info *sample = sample_get(s1);
	if(!sample)
		return;
	int entry;
	for( entry = 0; entry < SAMPLE_MAX_EFFECTS; entry ++ ) {
		if( sample->effect_chain[entry]->source_type != 0 ||
				sample->effect_chain[entry]->channel <= 0 )
			continue;

		if( paused == 1 ) {
			int speed = sample_get_speed( sample->effect_chain[entry]->channel );
			if( speed != 0 ) {
				sample->effect_chain[entry]->speed = sample_get_speed( sample->effect_chain[entry]->channel );
				sample_set_speed( sample->effect_chain[entry]->channel, 0 );
			}
		} 
		else {
				if( sample->effect_chain[entry]->speed == 0 ) {
					sample->effect_chain[entry]->speed = sample_get_speed( sample->effect_chain[entry]->channel );
					if( sample->effect_chain[entry]->speed == 0 ) {
						veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d on mixing entry %d is paused. Please set speed manually",
								sample->effect_chain[entry]->channel, entry);
					}	
				}


				if( sample->effect_chain[entry]->speed != INT_MAX ) {
					sample_set_speed( sample->effect_chain[entry]->channel, sample->effect_chain[entry]->speed );
					veejay_msg(VEEJAY_MSG_DEBUG, "Restoring speed %d for sample %d on mixing entry %d",
									sample->effect_chain[entry]->speed, sample->effect_chain[entry]->channel, entry );
				}
		}
	}
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
    
    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++)
    {
        if( sample->effect_chain[i]->effect_id != - 1 ) {
            vjert_del_fx( sample->effect_chain[i],0,i,1 );
        }

        sample->effect_chain[i]->effect_id = -1;
        sample->effect_chain[i]->frame_offset = 0;
        sample->effect_chain[i]->frame_trimmer = 0;
        sample->effect_chain[i]->volume = 0;
        sample->effect_chain[i]->a_flag = 0;
        sample->effect_chain[i]->is_rendering = 1;
    
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
        for (j = 0; j < SAMPLE_MAX_PARAMETERS; j++) {
            sample->effect_chain[i]->arg[j] = 0;
		}
    }

    sample->fade_entry = -1;

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
        vjert_del_fx( sample->effect_chain[position],0,position,1 );
    }

    sample->effect_chain[position]->effect_id = -1;
    sample->effect_chain[position]->frame_offset = 0;
    sample->effect_chain[position]->frame_trimmer = 0;
    sample->effect_chain[position]->volume = 0;
    sample->effect_chain[position]->a_flag = 0;
    sample->effect_chain[position]->is_rendering = 1;

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

    for (j = 0; j < SAMPLE_MAX_PARAMETERS; j++) {
        sample->effect_chain[position]->arg[j] = 0;
	}

    if( position == sample->fade_entry )
        sample->fade_entry = -1;

    return 1;
}

editlist *sample_get_editlist(int s1)
{
    sample_info *sample = sample_get(s1);
    if(!sample) return NULL;
    return sample->edit_list;
}

//@ is sample k in fx chain ?
int sample_cached(sample_info *s, int b_sample )
{
    int i = 0;
    for( i = 0; i < SAMPLE_MAX_EFFECTS ;i++ ) {
      if( s->effect_chain[i]->source_type == 0 && s->effect_chain[i]->channel == b_sample) {
        return 1;
      }
    }
    
    return 0;
}

void    sample_chain_alloc_kf( int s1, int entry )
{
    sample_info *sample = sample_get(s1);
        if(!sample) return;
    sample->effect_chain[entry]->kf = vpn( VEVO_ANONYMOUS_PORT );
}

int sample_set_editlist(int s1, editlist *edl)
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

/* print sample status information into an allocated string str*/
//int sample_chain_sprint_status(int s1, int entry, int changed, int r_changed,char *str,
//                 int frame)
int sample_chain_sprint_status( int s1,int tags,int cache,int sa,int ca, int pfps, int frame, int mode,int total_slots, int seq_rec,int curfps, uint32_t lo, uint32_t hi,int macro,char *str, int feedback )
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
    ptr = vj_sprintf( ptr, pfps ); 
    ptr = vj_sprintf( ptr, frame );
    ptr = vj_sprintf( ptr, mode ); 
    ptr = vj_sprintf( ptr, s1 ); 
    ptr = vj_sprintf( ptr, sample->effect_toggle );
    ptr = vj_sprintf( ptr, sample->first_frame ); 
    ptr = vj_sprintf( ptr, sample->last_frame ); 
    ptr = vj_sprintf( ptr, sample->speed ); 
    ptr = vj_sprintf( ptr, sample->looptype );
    ptr = vj_sprintf( ptr, e_a);
    ptr = vj_sprintf( ptr, e_d );
    ptr = vj_sprintf( ptr, e_s );
    ptr = vj_sprintf( ptr, sample_size() );
    ptr = vj_sprintf( ptr, sample->marker_start );
    ptr = vj_sprintf( ptr, sample->marker_end );
    ptr = vj_sprintf( ptr, sample->selected_entry );
    ptr = vj_sprintf( ptr, total_slots );
    ptr = vj_sprintf( ptr, cache );
    ptr = vj_sprintf( ptr, curfps );
    ptr = vj_sprintf( ptr, lo );
    ptr = vj_sprintf( ptr, hi );
    ptr = vj_sprintf( ptr, sa );
    ptr = vj_sprintf( ptr, ca );
    ptr = vj_sprintf( ptr, (int) sample->fader_val );
    ptr = vj_sprintf( ptr, sample->dup );
    ptr = vj_sprintf( ptr, macro );
    ptr = vj_sprintf( ptr, sample->subrender );
    ptr = vj_sprintf( ptr, sample->fade_method );
    ptr = vj_sprintf( ptr, sample->fade_entry );
    ptr = vj_sprintf( ptr, sample->fade_alpha );
    ptr = vj_sprintf( ptr, sample->loop_stat);
    ptr = vj_sprintf( ptr, sample->loop_stat_stop);
    ptr = vj_sprintf( ptr, sample->transition_active);
    ptr = vj_sprintf( ptr, sample->transition_length);
    ptr = vj_sprintf( ptr, sample->transition_shape);
    ptr = vj_sprintf( ptr, feedback);
    ptr = vj_sprintf( ptr, tags );
    
    return 0;
}


#ifdef HAVE_XML2
/*************************************************************************************************
 *
 * ParseArguments()
 *
 * Parse the effect arguments using libxml2
 *
 ****************************************************************************************************/
void ParseArguments(xmlDocPtr doc, xmlNodePtr cur, int *arg)
{
    int argIndex = 0;
    if (cur == NULL)
    return;

    while (cur != NULL && argIndex < SAMPLE_MAX_PARAMETERS) {
    if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_ARGUMENT))
    {
        arg[argIndex] = get_xml_int( doc, cur );
        argIndex ++;
    }
    cur = cur->next;
    }
}

static void ParseKeys( xmlDocPtr doc, xmlNodePtr cur, void *port )
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

    while (cur != NULL) {
    if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTID)) {
        effect_id = get_xml_int( doc, cur );
    }

    if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTPOS)) {
        chain_index = get_xml_int( doc, cur );
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

    if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTACTIVE)) {
        e_flag = get_xml_int( doc, cur );
    }

    if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTAUDIOFLAG)) {
        a_flag = get_xml_int( doc, cur );
    }

    if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECTAUDIOVOLUME)) {
        volume = get_xml_int( doc, cur );
    }
    
    if(!xmlStrcmp( cur->name, (const xmlChar*) "kf_status" )) {
        kf_status = get_xml_int( doc, cur );
    }

    if(!xmlStrcmp( cur->name, (const xmlChar*) "kf_type" )) {
        kf_type = get_xml_int( doc, cur );
    }

    cur = cur->next;
    }


    if (effect_id != -1) {
        int j;
        if (!sample_chain_add(dst_sample, chain_index, effect_id)) {
            veejay_msg(VEEJAY_MSG_ERROR, "Error parsing effect %d (pos %d)", effect_id, chain_index);
        }
        else {
            /* load the parameter values */
            for (j = 0; j < vje_get_num_params(effect_id); j++) {
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
static void ParseCalibration( xmlDocPtr doc, xmlNodePtr cur, sample_info *skel , void *vp)
{
}

static void LoadCurrentPlaying( xmlDocPtr doc, xmlNodePtr cur , int *id, int *mode )
{
        while (cur != NULL)
        {
                if (!xmlStrcmp(cur->name, (const xmlChar *) "PLAYING_ID")) {
            *id = get_xml_int( doc, cur );
        }
    
        if( !xmlStrcmp(cur->name, (const xmlChar*) "PLAYING_MODE" ))
        {
            *mode = get_xml_int( doc, cur);
        }

        cur = cur->next;
    }

}

static void LoadSequences( xmlDocPtr doc, xmlNodePtr cur, void *seq, int n_samples )
{
    sequencer_t *s = (sequencer_t*) seq;
    seq_sample_t tmp_seq[MAX_SEQUENCES];

    int i;
    int tmp_idx = 0;

    veejay_memset( &tmp_seq, 0, sizeof(tmp_seq));  

    while (cur != NULL)
    {
        if (!xmlStrcmp(cur->name, (const xmlChar *) "TYPE")) {
            tmp_seq[ tmp_idx ].type = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) "SEQ_ID")) {
            tmp_seq[ tmp_idx ].sample_id = get_xml_int( doc, cur );
            tmp_idx ++;
        }
        cur = cur->next;
    }

    if( tmp_idx == 0 )
        return;

    if( s->size == 0 ) {
        for( i = 0; i < tmp_idx; i ++ ) {
            s->samples[i].sample_id = tmp_seq[i].sample_id;
            s->samples[i].type = tmp_seq[i].type;
        }
        s->size = tmp_idx;
        return;
    } 

    if( (s->size + tmp_idx ) < MAX_SEQUENCES ) {
        for( i = 0; i < tmp_idx; i ++ ) {
            s->samples[ s->size + i ].sample_id = tmp_seq[ i ].sample_id + n_samples;
            s->samples[ s->size + i ].type = tmp_seq[i].type;
        }
        s->size = s->size + tmp_idx;
    } else {
        veejay_msg(VEEJAY_MSG_DEBUG, "Can't load this sequence, sequence bank is full");
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
    xmlNodePtr subs = NULL;

    int original_id = 0;

    int marker_start = 0, marker_end = 0;

    while (cur != NULL)
    {
        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_SAMPLEID)) {
            original_id = get_xml_int( doc, cur );
            skel->sample_id = original_id + start_at;
            sample_store(skel);
        }
    
        if( !xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_BOGUSVIDEO ) ) {
            skel->play_length = get_xml_int( doc, cur );
        }
    
        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EDIT_LIST_FILE)) {

            skel->edit_list_file = get_xml_str( doc, cur );
            if( start_at > 0 ) {
                free(skel->edit_list_file);
                skel->edit_list_file = sample_default_edl_name( original_id );
            }
            
            if(!sample_read_edl( skel ))
                skel->edit_list = NULL;
    
            if(!skel->edit_list)
            {
                veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d is using EDL from plain mode", skel->sample_id );
                skel->edit_list = el;
                skel->soft_edl = 1;
            }
            else
            {
                skel->soft_edl = 0;
                if( start_at == 0 ) {
                    veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d has its own EDL", skel->sample_id, el );
                }
                else {
                    veejay_msg(VEEJAY_MSG_DEBUG, "Sample %d is using Sample's %d EDL", skel->sample_id, original_id );
                }
            }
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_CHAIN_ENABLED)) {
            skel->effect_toggle = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_SAMPLEDESCR) && start_at == 0) {
            get_xml_str_n( doc, cur, skel->descr, sizeof(skel->descr) );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_FIRSTFRAME)) {
            skel->first_frame = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_VOL)) {
            skel->audio_volume = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_LASTFRAME)) {
            skel->last_frame = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_SPEED)) {
            skel->speed = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_FRAMEDUP)) {
            skel->dup = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_LOOPTYPE)) {
            skel->looptype = get_xml_int( doc, cur );
        }
    
        if (!xmlStrcmp(cur->name, (const xmlChar *) "subrender")) {
            skel->subrender = get_xml_int( doc, cur );
        }

		if( !xmlStrcmp(cur->name, (const xmlChar*) "loop_stat_stop")) {
			skel->loop_stat_stop = get_xml_int(doc,cur);
		}

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_MAXLOOPS)) {
            skel->max_loops = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_NEXTSAMPLE)) {
            skel->next_sample_id = get_xml_int( doc,cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_DEPTH)) {
            skel->depth = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_PLAYMODE)) {
            skel->playmode = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name,(const xmlChar *) XMLTAG_FADER_ACTIVE)) {
            skel->fader_active = get_xml_int( doc, cur );
        }
    
        if (!xmlStrcmp(cur->name,(const xmlChar *) XMLTAG_FADE_METHOD)) {
            skel->fade_method = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name,(const xmlChar *) XMLTAG_FADE_ALPHA)) {
            skel->fade_alpha = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name,(const xmlChar *) XMLTAG_FADE_ENTRY)) {
            skel->fade_entry = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name,(const xmlChar *) XMLTAG_FADER_VAL)) {
            skel->fader_val = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name,(const xmlChar*) XMLTAG_FADER_INC)) {
            skel->fader_inc = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name,(const xmlChar*) XMLTAG_FADER_DIRECTION)) {
            skel->fader_direction = get_xml_int( doc, cur );
        }

        if(!xmlStrcmp(cur->name,(const xmlChar*) XMLTAG_LASTENTRY)) {
            skel->selected_entry = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_MARKERSTART)) {
            marker_start = get_xml_int( doc, cur );
        }

        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_MARKEREND)) {
            marker_end = get_xml_int( doc, cur );
        }

        if(!xmlStrcmp(cur->name, (const xmlChar*) "SUBTITLES" ))
        {
            subs = cur->xmlChildrenNode;
        //  vj_font_xml_unpack( doc, cur->xmlChildrenNode, font );
        }

        ParseEffects(doc, cur->xmlChildrenNode, skel, start_at);

        if( !xmlStrcmp( cur->name, (const xmlChar*) "calibration" ) ) {
            ParseCalibration( doc, cur->xmlChildrenNode, skel ,vp);
        }

	if( !xmlStrcmp( cur->name, (const xmlChar*) XMLTAG_MACRO )) {
		vj_macro_load( skel->macro, doc, cur->xmlChildrenNode );
		int lss = vj_macro_get_loop_stat_stop(skel->macro);
		if( lss > skel->loop_stat_stop ) {
			skel->loop_stat_stop = lss;
		}
	}

        cur = cur->next;
    }

    if( marker_end != marker_start || marker_end != 0 )
    {
        //check if marker is sane
        if( marker_start > marker_end && skel->speed == 0 ) {
            int tmp = marker_start;
            marker_start = marker_end;
            marker_end = tmp;
        }

        skel->marker_start = marker_start;
        skel->marker_end = marker_end;
    }

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
    veejay_msg(VEEJAY_MSG_DEBUG, "Loading '%s' from current working directory" , files[0] );

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
        veejay_msg(VEEJAY_MSG_ERROR, "Error loading '%s' from current working directory", files[0] );
    }

    return res;
}

static void LoadSubtitles( sample_info *skel, char *file, void *font )
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
    if (doc == NULL) 
        return 0;

    /*
     * Check the document is of the right kind
     */

    int start_at = sample_size();
    if( start_at <= 0 )
        start_at = 0;

    if( start_at != 0 )
        veejay_msg(VEEJAY_MSG_INFO, "Merging %s into current samplelist, auto number starts at %d", sampleFile, start_at );

    cur = xmlDocGetRootElement(doc);
    if (cur == NULL) {
        veejay_msg(VEEJAY_MSG_ERROR,"Empty samplelist. Nothing to do.\n");
        xmlFreeDoc(doc);
        return 0;
    }

    if (xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_SAMPLES)) {
        veejay_msg(VEEJAY_MSG_ERROR, "This is not a samplelist: %s",XMLTAG_SAMPLES);
        xmlFreeDoc(doc);
        return 0;
    }

    cur = cur->xmlChildrenNode;
    while (cur != NULL)
    {
        if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_SAMPLE)) {
            skel = sample_skeleton_new(0, 1);
            if( skel == NULL )
                continue;

            void *d = vj_font_get_dict(font);

            xmlNodePtr subs = ParseSample( doc, cur->xmlChildrenNode, skel, el, font, start_at ,vp );   
            if(subs)
            {
                LoadSubtitles( skel, sampleFile, font );
                vj_font_xml_unpack( doc, subs, font );
            }
    
            vj_font_set_dict(font,d);
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

    return 1;
}

void CreateArguments(xmlNodePtr node, int *arg, int argcount)
{
    int i;
    for (i = 0; i < argcount; i++) {
        put_xml_int( node, XMLTAG_ARGUMENT, arg[i] );
    }
}

void    CreateKeys( xmlNodePtr node, int argcount, void *port )
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
    CreateArguments(childnode, effect->arg, vje_get_num_params(effect->effect_id));

    if( effect->kf != NULL ) {
     childnode = xmlNewChild(node,NULL,(const xmlChar*) "ANIM", NULL );
     CreateKeys( childnode, vje_get_num_params(effect->effect_id), effect->kf );
    }
}


void CreateEffects(xmlNodePtr node, sample_eff_chain ** effects)
{
    int i;
    xmlNodePtr childnode;

    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
        if (effects[i]->effect_id != -1) {
            childnode = xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECT, NULL);
            CreateEffect(childnode, effects[i], i);
        }
    }
}

static void SaveSequences( xmlNodePtr node, void *seq )
{
    int i = 0;
    sequencer_t *s = (sequencer_t*) seq;
    for( i = 0; i < MAX_SEQUENCES; i ++ )
    {
        put_xml_int( node, "TYPE", s->samples[i].type );
        put_xml_int( node, "SEQ_ID", s->samples[i].sample_id );
    }   
        
}

static void SaveCurrentPlaying( xmlNodePtr node, int id, int mode )
{
    put_xml_int( node, "PLAYING_ID", id );
    put_xml_int( node, "PLAYING_MODE", mode );
}

void CreateSample(xmlNodePtr node, sample_info * sample, void *font)
{
    xmlNodePtr childnode;

    put_xml_int( node, XMLTAG_SAMPLEID, sample->sample_id );
    put_xml_int( node, XMLTAG_CHAIN_ENABLED, sample->effect_toggle );

    if(sample->edit_list_file)
    {
        put_xml_str( node, XMLTAG_EDIT_LIST_FILE, sample->edit_list_file );
    }

    put_xml_str( node, XMLTAG_SAMPLEDESCR, sample->descr );
    put_xml_int( node, XMLTAG_FIRSTFRAME, sample->first_frame );
    put_xml_int( node, XMLTAG_LASTFRAME, sample->last_frame );
    put_xml_int( node, XMLTAG_BOGUSVIDEO, sample->play_length );
    put_xml_int( node, XMLTAG_SPEED, sample->speed );
    put_xml_int( node, XMLTAG_FRAMEDUP, sample->dup );
    put_xml_int( node, XMLTAG_LOOPTYPE, sample->looptype );
    put_xml_int( node, XMLTAG_MAXLOOPS, sample->max_loops );
    put_xml_int( node, XMLTAG_NEXTSAMPLE, sample->next_sample_id );
    put_xml_int( node, XMLTAG_DEPTH, sample->depth );
    put_xml_int( node, XMLTAG_PLAYMODE, sample->playmode );
    put_xml_int( node, XMLTAG_VOL, sample->audio_volume );
    put_xml_int( node, XMLTAG_MARKERSTART, sample->marker_start );
    put_xml_int( node, XMLTAG_MARKEREND, sample->marker_end );
    put_xml_int( node, XMLTAG_FADER_ACTIVE, sample->fader_active );
    put_xml_int( node, XMLTAG_FADE_METHOD, sample->fade_method );
    put_xml_int( node, XMLTAG_FADE_ALPHA, sample->fade_alpha );
    put_xml_int( node, XMLTAG_FADE_ENTRY, sample->fade_entry );
    put_xml_int( node, XMLTAG_FADER_INC, sample->fader_inc );
    put_xml_int( node, XMLTAG_FADER_VAL, sample->fader_val );
    put_xml_int( node, XMLTAG_FADER_DIRECTION, sample->fader_direction );
    put_xml_int( node, XMLTAG_LASTENTRY, sample->selected_entry );
    put_xml_int( node, "subrender", sample->subrender );
	put_xml_int( node, "loop_stat_stop", sample->loop_stat_stop);

    vj_font_xml_pack( node, font );

    childnode = xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTS, NULL);
    CreateEffects(childnode, sample->effect_chain);

	childnode = xmlNewChild(node, NULL, (const xmlChar*) XMLTAG_MACRO, NULL );
	vj_macro_store( sample->macro, childnode );
} 

/****************************************************************************************************
 *
 * sample_writeToFile( filename )
 *
 * writes all sample info to a file. 
 *
 ****************************************************************************************************/
static  int sample_write_edl(sample_info *sample)
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

static void WriteSubtitles( sample_info *next_sample, void *font, char *file )
{
    char tmp[512];

    void *d = vj_font_get_dict( font );

    sprintf(tmp, "%s-SUB-%d.srt", file,next_sample->sample_id );
    
    vj_font_set_dict( font, next_sample->dict );

    vj_font_save_srt( font, tmp );

    vj_font_set_dict( font, d );
}

int sample_writeToFile(char *sampleFile, void *vp,void *seq, void *font, int id, int mode)
{
    int i;
    const char *encoding = "UTF-8";
    xmlChar *version = xmlCharStrdup("1.0");    
    sample_info *next_sample;
    xmlDocPtr doc;
    xmlNodePtr rootnode, childnode;

    doc = xmlNewDoc(version);
    rootnode = xmlNewDocNode(doc, NULL, (const xmlChar *) XMLTAG_SAMPLES, NULL);
    xmlDocSetRootElement(doc, rootnode);

    childnode = xmlNewChild( rootnode, NULL,(const xmlChar*) "SEQUENCE", NULL );
    SaveSequences( childnode, seq );
  
    childnode = xmlNewChild( rootnode, NULL, (const xmlChar*) "CURRENT" , NULL );
    
    SaveCurrentPlaying( childnode , id, mode );

    int n= sample_highest();
    for (i = 1; i <= n; i++) {
        next_sample = sample_get(i);
        if (next_sample) {
            if(sample_write_edl( next_sample ))
                veejay_msg(VEEJAY_MSG_DEBUG ,"Saved sample %d EDL '%s'", next_sample->sample_id,next_sample->edit_list_file );  
          
            childnode = xmlNewChild(rootnode, NULL, (const xmlChar *) XMLTAG_SAMPLE, NULL);

            WriteSubtitles( next_sample,font, sampleFile );

            CreateSample(childnode, next_sample, font);
    
        }
    }

    int max = vj_tag_highest_valid_id();
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
