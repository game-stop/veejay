/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl> / <nelburg@looze.net>
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
 * 05/03/2003: Included more clip properties in Jeff's code 
 *	       Create is used to write the Clip to XML, Parse is used to load from XML
 
*/


#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <veejay/vj-global.h>
#include <libsample/sampleadm.h>
#include <libvjmsg/vj-common.h>
#include <libvje/vje.h>
//#include <veejay/vj-lib.h>
//#include <veejay/vj-el.h>
//todo: change this into enum
//#define KAZLIB_OPAQUE_DEBUG 1

#ifdef HAVE_XML2

#define XMLTAG_RENDER_ENTRY "render_entry"
#define XMLTAG_CLIPS    "veejay_clips"
#define XMLTAG_CLIP     "clip"
#define XMLTAG_CLIPID   "clipid"
#define XMLTAG_CLIPDESCR "description"
#define XMLTAG_FIRSTFRAME "startframe"
#define XMLTAG_LASTFRAME  "endframe"
#define XMLTAG_EFFECTS    "effects"
#define XMLTAG_VOL	  "volume"
#define XMLTAG_EFFECT     "effect"
#define XMLTAG_EFFECTID   "effectid"
#define XMLTAG_ARGUMENTS  "arguments"
#define XMLTAG_ARGUMENT   "argument"
#define XMLTAG_EFFECTSOURCE "source"
#define XMLTAG_EFFECTCHANNEL "channel"
#define XMLTAG_EFFECTTRIMMER "trimmer"
#define XMLTAG_EFFECTOFFSET "offset"
#define XMLTAG_EFFECTACTIVE "active"
#define XMLTAG_EFFECTAUDIOFLAG "use_audio"
#define XMLTAG_EFFECTAUDIOVOLUME "chain_volume"
#define XMLTAG_SPEED      "speed"
#define XMLTAG_FRAMEDUP   "frameduplicator"
#define XMLTAG_LOOPTYPE   "looptype"
#define XMLTAG_MAXLOOPS   "maxloops"
#define XMLTAG_NEXTCLIP "nextclip"
#define XMLTAG_DEPTH	  "depth"
#define XMLTAG_PLAYMODE   "playmode"
#define XMLTAG_VOLUME	  "volume"
#define XMLTAG_SUBAUDIO	  "subaudio"
#define XMLTAG_MARKERSTART "markerstart"
#define XMLTAG_MARKEREND   "markerend"
#define XMLTAG_EFFECTPOS   "position"
#define XMLTAG_FADER_ACTIVE "chain_fade"
#define XMLTAG_FADER_VAL    "chain_fade_value"
#define XMLTAG_FADER_INC    "chain_fade_increment"
#define XMLTAG_FADER_DIRECTION "chain_direction"
#define XMLTAG_LASTENTRY    "current_entry"
#define XMLTAG_CHAIN_ENABLED "fx"
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

static int this_clip_id = 0;	/* next available clip id */
static int next_avail_num = 0;	/* available clip id */
static int initialized = 0;	/* whether we are initialized or not */
static hash_t *ClipHash;	/* hash of clip information structs */
static int avail_num[CLIP_MAX_CLIPS];	/* an array of freed clip id's */

static int clipadm_state = CLIP_PEEK;	/* default state */




/****************************************************************************************************
 *
 * clip_size
 *
 * returns current clip_id pointer. size is actually this_clip_id - next_avail_num,
 * but people tend to use size as in length.
 *
 ****************************************************************************************************/
int clip_size()
{
    return this_clip_id;
}

int clip_verify() {
   return hash_verify( ClipHash );
}



/****************************************************************************************************
 *
 * int_hash
 *
 * internal usage. returns hash_val_t for key
 *
 ****************************************************************************************************/
static inline hash_val_t int_hash(const void *key)
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
static inline int int_compare(const void *key1, const void *key2)
{
    return ((int) key1 < (int) key2 ? -1 :
	    ((int) key1 > (int) key2 ? +1 : 0));
}

int clip_update(clip_info *clip, int s1) {
  if(s1 <= 0 || s1 >= CLIP_MAX_CLIPS) return 0;
  if(clip) {
    hnode_t *clip_node = hnode_create(clip);
    hnode_put(clip_node, (void*) s1);
    hnode_destroy(clip_node);
    return 1;
  }
  return 0;
}



/****************************************************************************************************
 *
 * clip_init()
 *
 * call before using any other function as clip_skeleton_new
 *
 ****************************************************************************************************/
void clip_init(int len)
{
    if (!initialized) {
	int i;
	for (i = 0; i < CLIP_MAX_CLIPS; i++)
	    avail_num[i] = 0;
	this_clip_id = 1;	/* do not start with zero */
	if (!
	    (ClipHash =
	     hash_create(HASHCOUNT_T_MAX, int_compare, int_hash))) {
	    fprintf(stderr,
		    "--DEBUG: clip_init(): cannot create clipHash\n");
	}
	initialized = 1;
    }
}

int clip_set_state(int new_state)
{
    if (new_state == CLIP_LOAD || new_state == CLIP_RUN
	|| new_state == CLIP_PEEK) {
	clipadm_state = new_state;
    }
    return clipadm_state;
}

int clip_get_state()
{
    return clipadm_state;
}

/****************************************************************************************************
 *
 * clip_skeleton_new(long , long)
 *
 * create a new clip, give start and end of new clip. returns clip info block.
 *
 ****************************************************************************************************/

clip_info *clip_skeleton_new(long startFrame, long endFrame)
{


    clip_info *si;
    int i, j, n, id = 0;

    if (!initialized) {
	fprintf(stderr, "Clip Administrator not initialized! Re-init\n");
    	return NULL;
	}
    si = (clip_info *) vj_malloc(sizeof(clip_info));
    if(startFrame < 0) startFrame = 0;
//    if(endFrame <= startFrame&& (endFrame !=0 && startFrame != 0))
	if(endFrame <= startFrame ) 
   {
	veejay_msg(VEEJAY_MSG_ERROR,"End frame %ld must be greater then start frame %ld", startFrame, endFrame);
	return NULL;
    }

    if (!si) {
	fprintf(stderr, "Unable to allocate memory for new clip\n");
	return NULL;
    }

    /* perhaps we can reclaim a clip id */
    for (n = 0; n <= next_avail_num; n++) {
	if (avail_num[n] != 0) {
	    id = avail_num[n];
	    avail_num[n] = 0;
	    break;
	}
    }
    if (id == 0) {		/* no we cannot not */
	if(this_clip_id==0) this_clip_id = 1; // first clip to create
	si->clip_id = this_clip_id;
	this_clip_id++; // set next number
    } else {			/* yet it is possible */
	si->clip_id = id;
	//this_clip_id++;
    }
    snprintf(si->descr,CLIP_MAX_DESCR_LEN, "%s", "Untitled");
    for(n=0; n < CLIP_MAX_RENDER;n++) {
      si->first_frame[n] = startFrame;
      si->last_frame[n] = endFrame;
	}
    si->speed = 1;
    si->looptype = 1;
    si->max_loops = 0;
    si->next_clip_id = 0;
    si->playmode = 0;
    si->depth = 0;
    si->sub_audio = 0;
    si->audio_volume = 50;
    si->marker_start = 0;
    si->marker_end = 0;
    si->dup = 0;
    si->active_render_entry = 0;
    si->loop_dec = 0;
	si->max_loops2 = 0;
    si->fader_active = 0;
    si->fader_val = 0;
    si->fader_inc = 0;
    si->fader_direction = 0;
    si->rec_total_bytes = 0;
    si->encoder_format = 0;
    si->encoder_base = (char*) vj_malloc(sizeof(char) * 255);
    si->sequence_num = 0;
    si->encoder_duration = 0;
    si->encoder_num_frames = 0;
    si->encoder_destination = (char*) vj_malloc(sizeof(char) * 255);
	si->encoder_succes_frames = 0;
    si->encoder_active = 0;
    si->encoder_total_frames = 0;
    si->rec_total_bytes = 0;
    si->encoder_max_size = 0;
    si->encoder_width = 0;
    si->encoder_height = 0;
    si->encoder_duration = 0;
   // si->encoder_buf = (char*) malloc(sizeof(char) * 10 * 65535 + 16);
    si->auto_switch = 0;
    si->selected_entry = 0;
    si->effect_toggle = 1;
    si->offset = 0;
    si->user_data = NULL;
    sprintf(si->descr, "%s", "Untitled");

    /* the effect chain is initially empty ! */
    for (i = 0; i < CLIP_MAX_EFFECTS; i++) {
	
	si->effect_chain[i] =
	    (clip_eff_chain *) vj_malloc(sizeof(clip_eff_chain));
	if (si->effect_chain[i] == NULL) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error allocating entry %d in Effect Chain for new clip",i);
		return NULL;
		}
	si->effect_chain[i]->is_rendering = 0;
	si->effect_chain[i]->effect_id = -1;
	si->effect_chain[i]->e_flag = 0;
	si->effect_chain[i]->frame_offset = -1;
	si->effect_chain[i]->frame_trimmer = 0;
	si->effect_chain[i]->volume = 50;
	si->effect_chain[i]->a_flag = 0;
	si->effect_chain[i]->source_type = 0;
	si->effect_chain[i]->channel = id;	/* with myself by default */
	/* effect parameters initially 0 */
	for (j = 0; j < CLIP_MAX_PARAMETERS; j++) {
	    si->effect_chain[i]->arg[j] = 0;
	}

    }
    return si;
}

int clip_store(clip_info * skel)
{
    hnode_t *clip_node;
    if (!skel)
	return -1;
    clip_node = hnode_create(skel);
    if (!clip_node)
	return -1;
    if (!clip_exists(skel->clip_id)) {
	hash_insert(ClipHash, clip_node, (void *) skel->clip_id);
    } else {
	hnode_put(clip_node, (void *) skel->clip_id);
    }
    return 0;
}

/****************************************************************************************************
 *
 * clip_get(int clip_id)
 *
 * returns clip information struct or NULL on error.
 *
 ****************************************************************************************************/
clip_info *clip_get(int clip_id)
{
    clip_info *si;
    //hnode_t *clip_node;
  //  if (!initialized)
//	return NULL;
  //  if (clip_id <= 0)
//	return NULL;
  //  for (i = 0; i <= next_avail_num; i++)
//	if (avail_num[i] == clip_id)
//	    return NULL;
    hnode_t *clip_node = hash_lookup(ClipHash, (void *) clip_id);
    if (clip_node) {
   	 si = (clip_info *) hnode_get(clip_node);
   	 if(si) return si;
	}
    return NULL;
}

/****************************************************************************************************
 *
 * clip_exists(int clip_id)
 *
 * returns 1 if a clip exists in cliphash, or 0 if not.
 *
 ****************************************************************************************************/


int clip_exists(int clip_id) {
	
	hnode_t *clip_node;
	if (!clip_id) return 0;
	
	clip_node = hash_lookup(ClipHash, (void*) clip_id);
	if (!clip_node) {
		return 0;
	}
	
	if(!clip_get(clip_id)) return 0;
	return 1;
}
/*
int clip_exists(int clip_id)
{
    if(clip_id < 1 || clip_id > CLIP_MAX_CLIPS) return 0;
    return (clip_get(clip_id) == NULL ? 0 : 1);
}
*/

int clip_copy(int clip_id)
{
    clip_info *org, *copy;
    int c, i;
    if (!clip_exists(clip_id))
	return -1;
    org = clip_get(clip_id);
    copy = clip_skeleton_new(org->first_frame[org->active_render_entry], org->last_frame[org->active_render_entry]);

    if (clip_store(copy) != 0)
	return -1;

    clip_set_framedup(copy->clip_id, clip_get_framedup(clip_id));
    clip_set_speed(copy->clip_id, clip_get_speed(clip_id));
    clip_set_looptype(copy->clip_id, clip_get_looptype(clip_id));
    clip_set_next(copy->clip_id, clip_get_next(clip_id));
    clip_set_loops(copy->clip_id, clip_get_loops(clip_id));
    clip_set_depth(copy->clip_id, clip_get_depth(clip_id));

    for (c = 0; c < CLIP_MAX_EFFECTS; c++) {
	int effect_id = clip_get_effect(clip_id, c);
	if (effect_id != -1) {
	    clip_chain_add(copy->clip_id, c, effect_id);
	    if (vj_effect_get_extra_frame(effect_id)) {
		int source = clip_get_chain_source(clip_id, c);

		int args[CLIP_MAX_PARAMETERS];
		int *p = &args[0];
		int n_args = vj_effect_get_num_params(effect_id);
		clip_set_chain_source(copy->clip_id, c, source);
		clip_set_chain_channel(copy->clip_id, c, source);

		clip_get_all_effect_arg(clip_id, c, p, n_args, -1);

		for (i = 0; i < CLIP_MAX_PARAMETERS; i++) {
		    clip_set_effect_arg(copy->clip_id, c, i, args[i]
			);
		}
	    }
	}
    }
    return copy->clip_id;
}

/****************************************************************************************************
 *
 * clip_get_startFrame(int clip_id)
 *
 * returns first frame of clip.
 *
 ****************************************************************************************************/
int clip_get_longest(int clip_id)
{
	clip_info *si = clip_get(clip_id);
	if(si)
	{
		int len = (si->last_frame[si->active_render_entry] -
			  si->first_frame[si->active_render_entry] );
		int c = 0;
		int tmp = 0;
		int t=0;
		int _id=0;
		int speed = abs(si->speed);
		int duration = len / speed; //how many frames are played of this clip

		if( si->looptype == 2) duration *= 2; // pingpong loop duration     

		for(c=0; c < CLIP_MAX_EFFECTS; c++)
		{
			_id = clip_get_chain_channel(clip_id,c);
			t   = clip_get_chain_source(clip_id,c);
	
                        if(t==0 && clip_exists(_id))
			{
				tmp = clip_get_endFrame( _id) - clip_get_startFrame(_id);
				if(tmp>0)
				{
					tmp = tmp / clip_get_speed(_id);
					if(tmp < 0) tmp *= -1;
					if(clip_get_looptype(_id)==2) tmp *= 2; //pingpong loop underlying clip
				}
				if(tmp > duration) duration = tmp; //which one is longer ...	
		        }
		}
		veejay_msg(VEEJAY_MSG_WARNING, "Length of clip in video frames: %ld",duration);
		return duration;
	}
	return 0;
}

int clip_get_startFrame(int clip_id)
{
    clip_info *si = clip_get(clip_id);
    if (si) {
   	if (si->marker_start != 0 && si->marker_end != 0)
		return si->marker_start;
    	else
		return si->first_frame[si->active_render_entry];
	}
    return -1;
}

int clip_get_short_info(int clip_id, int *start, int *end, int *loop, int *speed) {
    clip_info *si = clip_get(clip_id);
    if(si) {
	if(si->marker_start != 0 && si->marker_end !=0) {
	   *start = si->marker_start;
	   *end = si->marker_end;
	} 
        else {
	   *start = si->first_frame[si->active_render_entry];
	   *end = si->last_frame[si->active_render_entry];
	}
        *speed = si->speed;
        *loop = si->looptype;
	return 0;
    }
    *start = 0;
    *end = 0;
    *loop = 0;
    *speed = 0;
    return -1;
}

int clip_entry_is_rendering(int s1, int position) {
    clip_info *clip;
    clip = clip_get(s1);
    if (!clip)
	return -1;
    if (position >= CLIP_MAX_EFFECTS || position < 0)
	return -1;
    return clip->effect_chain[position]->is_rendering;
}

int clip_entry_set_is_rendering(int s1, int position, int value) {
    clip_info *si = clip_get(s1);
    if (!si)
	return -1;
    if( position >= CLIP_MAX_EFFECTS || position < 0) return -1;

    si->effect_chain[position]->is_rendering = value;
    return ( clip_update( si,s1 ));
}


int clip_update_offset(int s1, int n_frame)
{
	int len;
	clip_info *si = clip_get(s1);

	if(!si) return -1;
	si->offset = (n_frame - si->first_frame[si->active_render_entry]);
	len = si->last_frame[si->active_render_entry] - si->first_frame[si->active_render_entry];
	if(si->offset < 0) 
	{	
		veejay_msg(VEEJAY_MSG_WARNING,"Clip bounces outside clip by %d frames",
			si->offset);
		si->offset = 0;
	}
	if(si->offset > len) 
	{
		 veejay_msg(VEEJAY_MSG_WARNING,"Clip bounces outside clip with %d frames",
			si->offset);
		 si->offset = len;
	}
	return ( clip_update(si,s1));
}	

int clip_set_manual_fader( int s1, int value)
{
  clip_info *si = clip_get(s1);
  if(!si) return -1;
  si->fader_active = 2;
  si->fader_val = (float) value;
  si->fader_inc = 0.0;
  si->fader_direction = 0.0;

  /* inconsistency check */
  if(si->effect_toggle == 0) si->effect_toggle = 1;
  return (clip_update(si,s1));

}

int clip_set_fader_active( int s1, int nframes, int direction ) {
  clip_info *si = clip_get(s1);
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
  return (clip_update(si,s1));
}


int clip_reset_fader(int s1) {
  clip_info *si = clip_get(s1);
  if(!si) return -1;
  si->fader_active = 0;
  si->fader_val = 0;
  si->fader_inc = 0;
  return (clip_update(si,s1));
}

int clip_get_fader_active(int s1) {
  clip_info *si = clip_get(s1);
  if(!si) return -1;
  return (si->fader_active);
}

float clip_get_fader_val(int s1) {
  clip_info *si = clip_get(s1);
  if(!si) return -1;
  return (si->fader_val);
}

float clip_get_fader_inc(int s1) {
  clip_info *si = clip_get(s1);
  if(!si) return -1;
  return (si->fader_inc);
}

int clip_get_fader_direction(int s1) {
  clip_info *si = clip_get(s1);
  if(!si) return -1;
  return si->fader_direction;
}

int clip_set_fader_val(int s1, float val) {
  clip_info *si = clip_get(s1);
  if(!si) return -1;
  si->fader_val = val;
  return (clip_update(si,s1));
}

int clip_apply_fader_inc(int s1) {
  clip_info *si = clip_get(s1);
  if(!si) return -1;
  si->fader_val += si->fader_inc;
  if(si->fader_val > 255.0 ) si->fader_val = 255.0;
  if(si->fader_val < 0.0 ) si->fader_val = 0.0;
  clip_update(si,s1);
  return (int) (si->fader_val+0.5);
}



int clip_set_fader_inc(int s1, float inc) {
  clip_info *si = clip_get(s1);
  if(!si) return -1;
  si->fader_inc = inc;
  return (clip_update(si,s1));
}

int clip_marker_clear(int clip_id) {
    clip_info *si = clip_get(clip_id);
    if (!si)
	return -1;
    si->marker_start = 0;
    si->marker_end = 0;
    if(si->marker_speed) {
	si->speed = (si->speed * -1);
	si->marker_speed=0;
	}
    veejay_msg(VEEJAY_MSG_INFO, "Marker cleared (%d - %d) - (speed=%d)",
	si->marker_start, si->marker_end, si->speed);
    return ( clip_update(si,clip_id));
}

int clip_set_marker_start(int clip_id, int marker)
{
    clip_info *si = clip_get(clip_id);
    if (!si)
	return -1;
    if(si->marker_end != 0) {
	/* the end frame is set, so it may not be > marker_end */
	if(marker > si->marker_end) return -1;
	}
    si->marker_start = marker;
    return ( clip_update(si,clip_id));
}

int clip_set_marker(int clip_id, int start, int end) {
    clip_info *si = clip_get(clip_id);
    int tmp;
    if(!si) return -1;
    
    if(end == -1 || end > si->last_frame[si->active_render_entry] ) 
	end = si->last_frame[si->active_render_entry];
    if(start < 0) start = 0;
    if(start <= end) {
      tmp = si->last_frame[si->active_render_entry] - end;
      if( (si->first_frame[si->active_render_entry] + start) > si->last_frame[si->active_render_entry]) {
	 veejay_msg(VEEJAY_MSG_ERROR, "Invalid start parameter");
	 return -1;
	}
      if( (si->last_frame[si->active_render_entry]-tmp) < si->first_frame[si->active_render_entry]) {
	veejay_msg(VEEJAY_MSG_ERROR, "Invalid end parameter");
	return -1;
      }
      si->marker_start = si->first_frame[si->active_render_entry] + start;
      si->marker_end = si->last_frame[si->active_render_entry] - tmp;
      si->marker_speed = 0; 
      veejay_msg(VEEJAY_MSG_INFO, "Clip marker at %d - %d",si->marker_start,
		si->marker_end);
    }
    return ( clip_update( si , clip_id ) );	
}

int clip_set_marker_end(int clip_id, int marker)
{
    clip_info *si = clip_get(clip_id);
    if (!si)
	return -1;
    if( si->marker_start == 0) return -1;

  /*  if (si->speed < 0) {
	si->marker_end = si->marker_start;
	si->marker_start = marker;
    } else {
*/

    if(si->marker_start > marker) return -1;
    si->marker_end = marker;
    if(si->speed < 0) {
	int swap = si->marker_start;
	si->marker_start = marker;
	si->marker_end = swap;
	}

//    }
    return (clip_update(si,clip_id));
}

int clip_set_description(int clip_id, char *description)
{
    clip_info *si = clip_get(clip_id);
    if (!si)
	return -1;
    if (!description || strlen(description) <= 0) {
	snprintf(si->descr, CLIP_MAX_DESCR_LEN, "%s", "Untitled");
    } else {
	snprintf(si->descr, CLIP_MAX_DESCR_LEN, "%s", description);
    }
    return ( clip_update(si, clip_id)==1 ? 0 : 1);
}

int clip_get_description(int clip_id, char *description)
{
    clip_info *si;
    si = clip_get(clip_id);
    if (!si)
	return -1;
    snprintf(description, CLIP_MAX_DESCR_LEN,"%s", si->descr);
    return 0;
}

/****************************************************************************************************
 *
 * clip_get_endFrame(int clip_id)
 *
 * returns last frame of clip.
 *
 ****************************************************************************************************/
int clip_get_endFrame(int clip_id)
{
    clip_info *si = clip_get(clip_id);
    if (si) {
   	if (si->marker_end != 0 && si->marker_start != 0)
		return si->marker_end;
   	 else {
		return si->last_frame[si->active_render_entry];
	}
    }
    return -1;
}
/****************************************************************************************************
 *
 * clip_del( clip_nr )
 *
 * deletes a clip from the hash. returns -1 on error, 1 on success.
 *
 ****************************************************************************************************/
int clip_del(int clip_id)
{
    hnode_t *clip_node;
    clip_info *si;
    si = clip_get(clip_id);
    if (!si)
	return -1;

    clip_node = hash_lookup(ClipHash, (void *) si->clip_id);
    if (clip_node) {
    int i;
    for(i=0; i < CLIP_MAX_EFFECTS; i++) 
    {
		if (si->effect_chain[i])
			free(si->effect_chain[i]);
    }
    if (si)
      free(si);
    /* store freed clip_id */
    avail_num[next_avail_num] = clip_id;
    next_avail_num++;
    hash_delete(ClipHash, clip_node);

    return 1;
    }

    return -1;
}


void clip_del_all()
{
    int end = clip_size();
    int i;
    for (i = 0; i < end; i++) {
	if (clip_exists(i)) {
	    clip_del(i);
	}
    }
    this_clip_id = 0;
}

/****************************************************************************************************
 *
 * clip_get_effect( clip_nr , position)
 *
 * returns effect in effect_chain on position X , -1 on error.
 *
 ****************************************************************************************************/
int clip_get_effect(int s1, int position)
{
    clip_info *clip = clip_get(s1);
    if(position >= CLIP_MAX_EFFECTS || position < 0 ) return -1;
    if(clip) {
	if(clip->effect_chain[position]->e_flag==0) return -1;
   	return clip->effect_chain[position]->effect_id;
    }
    return -1;
}

int clip_get_effect_any(int s1, int position) {
	clip_info *clip = clip_get(s1);
	if(position >= CLIP_MAX_EFFECTS || position < 0 ) return -1;
	if(clip) {
		return clip->effect_chain[position]->effect_id;
	}
	return -1;
}

int clip_get_chain_status(int s1, int position)
{
    clip_info *clip;
    clip = clip_get(s1);
    if (!clip)
	return -1;
    if (position >= CLIP_MAX_EFFECTS)
	return -1;
    return clip->effect_chain[position]->e_flag;
}


int clip_get_offset(int s1, int position)
{
    clip_info *clip;
    clip = clip_get(s1);
    if (!clip)
	return -1;
    if (position >= CLIP_MAX_EFFECTS)
	return -1;
    return clip->effect_chain[position]->frame_offset;
}

int clip_get_trimmer(int s1, int position)
{
    clip_info *clip;
    clip = clip_get(s1);
    if (!clip)
	return -1;
    if (position < 0 || position >= CLIP_MAX_EFFECTS)
	return -1;
    return clip->effect_chain[position]->frame_trimmer;
}

int clip_get_chain_volume(int s1, int position)
{
    clip_info *clip;
    clip = clip_get(s1);
    if (!clip)
	return -1;
    if (position >= CLIP_MAX_EFFECTS)
	return -1;
    return clip->effect_chain[position]->volume;
}

int clip_get_chain_audio(int s1, int position)
{
    clip_info *clip = clip_get(s1);
    if (clip) {
     return clip->effect_chain[position]->a_flag;
    }
    return -1;
}

/****************************************************************************************************
 *
 * clip_get_looptype
 *
 * returns the type of loop set on the clip. 0 on no loop, 1 on ping pong
 * returns -1  on error.
 *
 ****************************************************************************************************/

int clip_get_looptype(int s1)
{
    clip_info *clip = clip_get(s1);
    if (clip) {
    	return clip->looptype;
    }
    return 0;
}

int clip_get_playmode(int s1)
{
   clip_info *clip = clip_get(s1);
   if (clip) {
   	 return clip->playmode;
   }
   return -1;
}

/********************
 * get depth: 1 means do what is in underlying clip.
 *******************/
int clip_get_depth(int s1)
{
    clip_info *clip = clip_get(s1);
    if (clip)
      return clip->depth;
    return 0;
}

int clip_set_depth(int s1, int n)
{
    clip_info *clip;
    hnode_t *clip_node;

    if (n == 0 || n == 1) {
	clip = clip_get(s1);
	if (!clip)
	    return -1;
	if (clip->depth == n)
	    return 1;
	clip->depth = n;
	clip_node = hnode_create(clip);
	if (!clip_node) {
	    return -1;
	}
	return 1;
    }
    return -1;
}
int clip_set_chain_status(int s1, int position, int status)
{
    clip_info *clip;
    if (position < 0 || position >= CLIP_MAX_EFFECTS)
	return -1;
    clip = clip_get(s1);
    if (!clip)
	return -1;
    clip->effect_chain[position]->e_flag = status;
    clip_update(clip,s1);
    return 1;
}

/****************************************************************************************************
 *
 * clip_get_speed
 *
 * returns the playback speed set on the clip.
 * returns -1  on error.
 *
 ****************************************************************************************************/
int clip_get_render_entry(int s1)
{
    clip_info *clip = clip_get(s1);
    if (clip)
   	 return clip->active_render_entry;
    return 0;
}

int clip_get_speed(int s1)
{
    clip_info *clip = clip_get(s1);
    if (clip)
   	 return clip->speed;
    return 0;
}

int clip_get_framedup(int s1) {
	clip_info *clip = clip_get(s1);
	if(clip) return clip->dup;
	return 0;
}


int clip_get_effect_status(int s1)
{
	clip_info *clip = clip_get(s1);
	if(clip) return clip->effect_toggle;
	return 0;
}

/****************************************************************************************************
 *
 * clip_get_effect_arg( clip_nr, position, argnr )
 *
 * returns the required argument set on position X in the effect_chain of clip Y.
 * returns -1 on error.
 ****************************************************************************************************/
int clip_get_effect_arg(int s1, int position, int argnr)
{
    clip_info *clip;
    clip = clip_get(s1);
    if (!clip)
	return -1;
    if (position >= CLIP_MAX_EFFECTS)
	return -1;
    if (argnr < 0 || argnr > CLIP_MAX_PARAMETERS)
	return -1;
    return clip->effect_chain[position]->arg[argnr];
}

int clip_get_selected_entry(int s1) 
{
	clip_info *clip;
	clip = clip_get(s1);
	if(!clip) return -1;
	return clip->selected_entry;
}	

int clip_get_all_effect_arg(int s1, int position, int *args, int arg_len, int n_frame)
{
    int i;
    clip_info *clip;
    clip = clip_get(s1);
    if( arg_len == 0)
	return 1;
    if (!clip)
	return -1;
    if (position >= CLIP_MAX_EFFECTS)
	return -1;
    if (arg_len < 0 || arg_len > CLIP_MAX_PARAMETERS)
	return -1;
    for (i = 0; i < arg_len; i++) {
		args[i] = clip->effect_chain[position]->arg[i];
    }
    return i;
}

/********************************************
 * clip_has_extra_frame.
 * return 1 if an effect on the given chain entry 
 * requires another frame, -1 otherwise.
 */
int clip_has_extra_frame(int s1, int position)
{
    clip_info *clip;
    clip = clip_get(s1);
    if (!clip)
	return -1;
    if (position >= CLIP_MAX_EFFECTS)
	return -1;
    if (clip->effect_chain[position]->effect_id == -1)
	return -1;
    if (vj_effect_get_extra_frame
	(clip->effect_chain[position]->effect_id) == 1)
	return 1;
    return -1;
}

/****************************************************************************************************
 *
 * clip_set_effect_arg
 *
 * sets an argument ARGNR in the chain on position X of clip Y
 * returns -1  on error.
 *
 ****************************************************************************************************/

int clip_set_effect_arg(int s1, int position, int argnr, int value)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;
    if (position >= CLIP_MAX_EFFECTS)
	return -1;
    if (argnr < 0 || argnr > CLIP_MAX_PARAMETERS)
	return -1;
    clip->effect_chain[position]->arg[argnr] = value;
    return ( clip_update(clip,s1));
}

int clip_set_selected_entry(int s1, int position) 
{
	clip_info *clip = clip_get(s1);
	if(!clip) return -1;
	if(position< 0 || position >= CLIP_MAX_EFFECTS) return -1;
	clip->selected_entry = position;
	return (clip_update(clip,s1));
}

int clip_set_effect_status(int s1, int status)
{
	clip_info *clip = clip_get(s1);
	if(!clip) return -1;
	if(status == 1 || status == 0 )
	{
		clip->effect_toggle = status;
		return ( clip_update(clip,s1));
	}
	return -1;
}

int clip_set_chain_channel(int s1, int position, int input)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;
    if (position < 0 || position >= CLIP_MAX_EFFECTS)
	return -1;
    clip->effect_chain[position]->channel = input;
    return ( clip_update(clip,s1));
}

int clip_is_deleted(int s1)
{
    int i;
    for (i = 0; i < next_avail_num; i++) {
	if (avail_num[i] == s1)
	    return 1;
    }
    return 0;
}

int clip_set_chain_source(int s1, int position, int input)
{
    clip_info *clip;
    clip = clip_get(s1);
    if (!clip)
	return -1;
    if (position < 0 || position >= CLIP_MAX_EFFECTS)
	return -1;
    clip->effect_chain[position]->source_type = input;
    return (clip_update(clip,s1));
}

/****************************************************************************************************
 *
 * clip_set_speed
 *
 * store playback speed in the clip.
 * returns -1  on error.
 *
 ****************************************************************************************************/

int clip_set_user_data(int s1, void *data)
{
	clip_info *clip = clip_get(s1);
	if(!clip) return -1;
	clip->user_data = data;
	return ( clip_update(clip, s1) );
}

void *clip_get_user_data(int s1)
{
	clip_info *clip = clip_get(s1);
	if(!clip) return NULL;
	return clip->user_data;
}

int clip_set_speed(int s1, int speed)
{
    clip_info *clip = clip_get(s1);
    if (!clip) return -1;
    clip->speed = speed;
    return ( clip_update(clip,s1));
}

int clip_set_render_entry(int s1, int entry)
{
    clip_info *clip = clip_get(s1);
    if (!clip) return -1;
    if( entry < 0 || entry >= CLIP_MAX_RENDER) return -1;
    clip->active_render_entry = entry;
    return ( clip_update(clip,s1));
}

int clip_set_framedup(int s1, int n) {
	clip_info *clip = clip_get(s1);
	if(!clip) return -1;
	clip->dup = n;
	return ( clip_update(clip,s1));
}

int clip_get_chain_channel(int s1, int position)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;
    if (position < 0 || position >= CLIP_MAX_EFFECTS)
	return -1;
    return clip->effect_chain[position]->channel;
}

int clip_get_chain_source(int s1, int position)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;
    if (position < 0 || position >= CLIP_MAX_EFFECTS)
	return -1;
    return clip->effect_chain[position]->source_type;
}

int clip_get_loops(int s1)
{
    clip_info *clip = clip_get(s1);
    if (clip) {
    	return clip->max_loops;
	}
    return -1;
}
int clip_get_loops2(int s1)
{
    clip_info *clip;
    clip = clip_get(s1);
    if (!clip)
	return -1;
    return clip->max_loops2;
}

/****************************************************************************************************
 *
 * clip_set_looptype
 *
 * store looptype in the clip.
 * returns -1  on error.
 *
 ****************************************************************************************************/

int clip_set_looptype(int s1, int looptype)
{
    clip_info *clip = clip_get(s1);
    if(!clip) return -1;

    if (looptype == 0 || looptype == 1 || looptype == 2) {
	clip->looptype = looptype;
	return ( clip_update(clip,s1));
    }
    return -1;
}

int clip_set_playmode(int s1, int playmode)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;

    clip->playmode = playmode;
    return ( clip_update(clip,s1));
}



/*************************************************************************************************
 * update start frame
 *
 *************************************************************************************************/
int clip_set_startframe(int s1, long frame_num)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;
    if(frame_num < 0) return frame_num = 0;
    clip->first_frame[clip->active_render_entry] = frame_num;
    return (clip_update(clip,s1));
}

int clip_set_endframe(int s1, long frame_num)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;
    if(frame_num < 0) return -1;
    clip->last_frame[clip->active_render_entry] = frame_num;
    return (clip_update(clip,s1));
}
int clip_get_next(int s1)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;
    return clip->next_clip_id;
}
int clip_set_loops(int s1, int nr_of_loops)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;
    clip->max_loops = nr_of_loops;
    return (clip_update(clip,s1));
}
int clip_set_loops2(int s1, int nr_of_loops)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;
    clip->max_loops2 = nr_of_loops;
    return (clip_update(clip,s1));
}

int clip_get_sub_audio(int s1)
{
    clip_info *clip;
    clip = clip_get(s1);
    if (!clip)
	return -1;
    return clip->sub_audio;
}

int clip_set_sub_audio(int s1, int audio)
{
    clip_info *clip = clip_get(s1);
    if(!clip) return -1;
    if (audio < 0 && audio > 1)
	return -1;
    clip->sub_audio = audio;
    return (clip_update(clip,s1));
}

int clip_get_audio_volume(int s1)
{
    clip_info *clip = clip_get(s1);
    if (clip) {
   	 return clip->audio_volume;
	}
   return -1;
}

int clip_set_audio_volume(int s1, int volume)
{
    clip_info *clip = clip_get(s1);
    if (volume < 0)
	volume = 0;
    if (volume > 100)
	volume = 100;
    clip->audio_volume = volume;
    return (clip_update(clip,s1));
}


int clip_set_next(int s1, int next_clip_id)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;
    /* just add, do not verify 
       on module generation, next clip may not yet be created.
       checks in parameter set in libveejayvj.c
     */
    clip->next_clip_id = next_clip_id;
    return (clip_update(clip,s1));
}

/****************************************************************************************************
 *
 *
 * add a new effect to the chain, returns chain index number on success or -1  if  
 * the requested clip does not exist.
 *
 ****************************************************************************************************/

int clip_chain_malloc(int s1)
{
    clip_info *clip = clip_get(s1);
    int i=0;
    int e_id = 0; 
    int sum =0;
    if (!clip)
	return -1;
    for(i=0; i < CLIP_MAX_EFFECTS; i++)
    {
	e_id = clip->effect_chain[i]->effect_id;
	if(e_id)
	{
		if(vj_effect_activate(e_id))
			sum++;
	}
    } 
    veejay_msg(VEEJAY_MSG_INFO, "Allocated %d effects",sum);
    return sum; 
}

int clip_chain_free(int s1)
{
    clip_info *clip = clip_get(s1);
    int i=0;
    int e_id = 0; 
    int sum = 0;
    if (!clip)
	return -1;
    for(i=0; i < CLIP_MAX_EFFECTS; i++)
    {
	e_id = clip->effect_chain[i]->effect_id;
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

int clip_chain_add(int s1, int c, int effect_nr)
{
    int effect_params = 0, i;
    clip_info *clip = clip_get(s1);
    if (!clip)
		return -1;
    if (c < 0 || c >= CLIP_MAX_EFFECTS)
		return -1;
	
	if ( effect_nr < VJ_IMAGE_EFFECT_MIN ) return -1;

	if ( effect_nr > VJ_IMAGE_EFFECT_MAX && effect_nr < VJ_VIDEO_EFFECT_MIN )
		return -1;

	if ( effect_nr > VJ_VIDEO_EFFECT_MAX )
		return -1;	
	
/*
    if(clip->effect_chain[c]->effect_id != -1 &&
		clip->effect_chain[c]->effect_id != effect_nr &&
	  vj_effect_initialized( clip->effect_chain[c]->effect_id ))
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Effect %s must be freed??", vj_effect_get_description(
			clip->effect_chain[c]->effect_id));
		vj_effect_deactivate( clip->effect_chain[c]->effect_id );
	}
*/
    if( clip->effect_chain[c]->effect_id != -1 && clip->effect_chain[c]->effect_id != effect_nr )
    {
	//verify if the effect should be discarded
	if(vj_effect_initialized( clip->effect_chain[c]->effect_id ))
	{
		// it is using some memory, see if we can free it ...
		int i;
		int ok = 1;
		for(i=(c+1); i < CLIP_MAX_EFFECTS; i++)
		{
			if( clip->effect_chain[i]->effect_id == clip->effect_chain[c]->effect_id) ok = 0;
		}
		// ok, lets get rid of it.
		if( ok ) vj_effect_deactivate( clip->effect_chain[c]->effect_id );
	}
    }


    if(!vj_effect_initialized(effect_nr))
    {
	veejay_msg(VEEJAY_MSG_DEBUG, "Effect %s must be initialized now",
		vj_effect_get_description(effect_nr));
	if(!vj_effect_activate( effect_nr ))	return -1;
    }

    clip->effect_chain[c]->effect_id = effect_nr;
    clip->effect_chain[c]->e_flag = 1;	/* effect enabled standard */
    effect_params = vj_effect_get_num_params(effect_nr);
    if (effect_params > 0)
    {
	/* there are parameters, set default values */
	for (i = 0; i < effect_params; i++)
        {
	    int val = vj_effect_get_default(effect_nr, i);
	    clip->effect_chain[c]->arg[i] = val;
	}
    }
    if (vj_effect_get_extra_frame(effect_nr))
   {
    clip->effect_chain[c]->frame_offset = 0;
    clip->effect_chain[c]->frame_trimmer = 0;

    if(s1 > 1)
	 s1 = s1 - 1;
    if(!clip_exists(s1)) s1 = s1 + 1;

	if(clip->effect_chain[c]->channel <= 0)
		clip->effect_chain[c]->channel = s1;
    if(clip->effect_chain[c]->source_type < 0)
		clip->effect_chain[c]->source_type = 0;

        veejay_msg(VEEJAY_MSG_DEBUG,"Effect %s on entry %d overlaying with clip %d",
			vj_effect_get_description(clip->effect_chain[c]->effect_id),c,clip->effect_chain[c]->channel);
    }
    clip_update(clip,s1);

    return c;			/* return position on which it was added */
}

int clip_reset_offset(int s1)
{
	clip_info *clip = clip_get(s1);
	int i;
	if(!clip) return -1;
	for(i=0; i < CLIP_MAX_EFFECTS; i++)
	{
		clip->effect_chain[i]->frame_offset = 0;
	}
	return ( clip_update(clip,s1));
}

int clip_set_offset(int s1, int chain_entry, int frame_offset)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;
    /* set to zero if frame_offset is greater than clip length */
    //if(frame_offset > (clip->last_frame - clip->first_frame)) frame_offset=0;
    clip->effect_chain[chain_entry]->frame_offset = frame_offset;
    return (clip_update(clip,s1));
}

int clip_set_trimmer(int s1, int chain_entry, int trimmer)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;
    /* set to zero if frame_offset is greater than clip length */
    if (chain_entry < 0 || chain_entry >= CLIP_MAX_PARAMETERS)
	return -1;
    if (trimmer > (clip->last_frame[clip->active_render_entry] - clip->first_frame[clip->active_render_entry]))
	trimmer = 0;
    if (trimmer < 0 ) trimmer = 0;
    clip->effect_chain[chain_entry]->frame_trimmer = trimmer;

    return (clip_update(clip,s1));
}
int clip_set_chain_audio(int s1, int chain_entry, int val)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;
    if (chain_entry < 0 || chain_entry >= CLIP_MAX_PARAMETERS)
	return -1;
    clip->effect_chain[chain_entry]->a_flag = val;
    return ( clip_update(clip,s1));
}

int clip_set_chain_volume(int s1, int chain_entry, int volume)
{
    clip_info *clip = clip_get(s1);
    if (!clip)
	return -1;
    /* set to zero if frame_offset is greater than clip length */
    if (volume < 0)
	volume = 100;
    if (volume > 100)
	volume = 0;
    clip->effect_chain[chain_entry]->volume = volume;
    return (clip_update(clip,s1));
}




/****************************************************************************************************
 *
 * clip_chain_clear( clip_nr )
 *
 * clear the entire effect chain.
 * 
 ****************************************************************************************************/

int clip_chain_clear(int s1)
{
    int i, j;
    clip_info *clip = clip_get(s1);

    if (!clip)
	return -1;
    /* the effect chain is gonna be empty! */
    for (i = 0; i < CLIP_MAX_EFFECTS; i++) {
	if(clip->effect_chain[i]->effect_id != -1)
	{
		if(vj_effect_initialized( clip->effect_chain[i]->effect_id ))
			vj_effect_deactivate( clip->effect_chain[i]->effect_id ); 
	}
	clip->effect_chain[i]->effect_id = -1;
	clip->effect_chain[i]->frame_offset = -1;
	clip->effect_chain[i]->frame_trimmer = 0;
	clip->effect_chain[i]->volume = 0;
	clip->effect_chain[i]->a_flag = 0;
	clip->effect_chain[i]->source_type = 0;
	clip->effect_chain[i]->channel = s1;
	for (j = 0; j < CLIP_MAX_PARAMETERS; j++)
	    clip->effect_chain[i]->arg[j] = 0;
    }

    return (clip_update(clip,s1));
}


/****************************************************************************************************
 *
 * clip_chain_size( clip_nr )
 *
 * returns the number of effects in the effect_chain 
 *
 ****************************************************************************************************/
int clip_chain_size(int s1)
{
    int i, e;

    clip_info *clip;
    clip = clip_get(s1);
    if (!clip)
	return -1;
    e = 0;
    for (i = 0; i < CLIP_MAX_EFFECTS; i++)
	if (clip->effect_chain[i]->effect_id != -1)
	    e++;
    return e;
}

/****************************************************************************************************
 *
 * clip_chain_get_free_entry( clip_nr )
 *
 * returns last available entry 
 *
 ****************************************************************************************************/
int clip_chain_get_free_entry(int s1)
{
    int i;
    clip_info *clip;
    clip = clip_get(s1);
    if (!clip)
	return -1;
    for (i = 0; i < CLIP_MAX_EFFECTS; i++)
	if (clip->effect_chain[i]->effect_id == -1)
	    return i;
    return -1;
}


/****************************************************************************************************
 *
 * clip_chain_remove( clip_nr, position )
 *
 * Removes an Effect from the chain of clip <clip_nr> on entry <position>
 *
 ****************************************************************************************************/

static int _clip_can_free(clip_info *clip, int reserved, int effect_id)
{
	int i;
	for(i=0; i < CLIP_MAX_EFFECTS; i++)
	{
		if(i != reserved && effect_id == clip->effect_chain[i]->effect_id) return 0;
	}
	return 1;
}

int clip_chain_remove(int s1, int position)
{
    int j;
    clip_info *clip;
    clip = clip_get(s1);
    if (!clip)
	return -1;
    if (position < 0 || position >= CLIP_MAX_EFFECTS)
	return -1;
    if(clip->effect_chain[position]->effect_id != -1)
    {
	if(vj_effect_initialized( clip->effect_chain[position]->effect_id) && 
	 _clip_can_free(clip,position, clip->effect_chain[position]->effect_id))
		vj_effect_deactivate( clip->effect_chain[position]->effect_id);    
    }
    clip->effect_chain[position]->effect_id = -1;
    clip->effect_chain[position]->frame_offset = -1;
    clip->effect_chain[position]->frame_trimmer = 0;
    clip->effect_chain[position]->volume = 0;
    clip->effect_chain[position]->a_flag = 0;
    clip->effect_chain[position]->source_type = 0;
    clip->effect_chain[position]->channel = 0;
    for (j = 0; j < CLIP_MAX_PARAMETERS; j++)
	clip->effect_chain[position]->arg[j] = 0;

    return (clip_update(clip,s1));
}

int clip_set_loop_dec(int s1, int active, int periods) {
    clip_info *clip = clip_get(s1);
    if(!clip) return -1;
    if(periods <=0) return -1;
    if(periods > 25) return -1;
    clip->loop_dec = active;
    clip->loop_periods = periods;
    return (clip_update(clip,s1));
}

int clip_get_loop_dec(int s1) {
    clip_info *clip = clip_get(s1);
    if(!clip) return -1;
    return clip->loop_dec;
}

int clip_apply_loop_dec(int s1, double fps) {
    clip_info *clip = clip_get(s1);
    int inc = (int) fps;
    if(!clip) return -1;
    if(clip->loop_dec==1) {
	if( (clip->first_frame[clip->active_render_entry] + inc) >= clip->last_frame[clip->active_render_entry]) {
		clip->first_frame[clip->active_render_entry] = clip->last_frame[clip->active_render_entry]-1;
		clip->loop_dec = 0;
	}
	else {
		clip->first_frame[clip->active_render_entry] += (inc / clip->loop_periods);
	}
	veejay_msg(VEEJAY_MSG_DEBUG, "New starting postions are %ld - %ld",
		clip->first_frame[clip->active_render_entry], clip->last_frame[clip->active_render_entry]);
	return ( clip_update(clip, s1));
    }
    return -1;
}


/* print clip status information into an allocated string str*/
//int clip_chain_sprint_status(int s1, int entry, int changed, int r_changed,char *str,
//			       int frame)
int	clip_chain_sprint_status( int s1,int pfps, int frame, int mode, char *str )
{
    clip_info *clip;
    clip = clip_get(s1);
    if (!clip)
	return -1;
	/*
	fprintf(stderr,
      "%d %d %d %d %d %d %ld %ld %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
	    frame,
	    clip->active_render_entry,
	    r_changed,
	    s1,
	    clip->first_frame[clip->active_render_entry],
	    clip->last_frame[clip->active_render_entry],
	    clip->speed,
	    clip->looptype,
	    clip->max_loops,
	    clip->max_loops2,
	    clip->next_clip_id,
	    clip->depth,
	    clip->playmode,
	    clip->audio_volume,
	    clip->selected_entry,
 	    clip->effect_toggle,
	    changed,
	    vj_effect_real_to_sequence(clip->effect_chain[entry]->effect_id),
				      // effect_id),
	    clip->effect_chain[entry]->e_flag,
	    clip->effect_chain[entry]->frame_offset,
	    clip->effect_chain[entry]->frame_trimmer,
	    clip->effect_chain[entry]->source_type,
	    clip->effect_chain[entry]->channel,
	    this_clip_id - 1);
	*/
/*
	
    sprintf(str,
	    "%d %d %d %d %d %d %ld %ld %d %d %d %d %d %d %d %d %d %d %d %d %d %ld %ld %d %d %d %d %d %d %d %d %d %d %d",
/	    frame,
	    clip->active_render_entry,
	    r_changed,
	    clip->selected_entry,
	    clip->effect_toggle,
	    s1,
	    clip->first_frame[clip->active_render_entry],
	    clip->last_frame[clip->active_render_entry],
	    clip->speed,
	    clip->looptype,
	    clip->max_loops,
	    clip->max_loops2,
	    clip->next_clip_id,
	    clip->depth,
	    clip->playmode,
	    clip->dup,
	    clip->audio_volume,
	    0, 
	    0, 
 	   0,
	    clip->encoder_active,
	    clip->encoder_duration,
	    clip->encoder_succes_frames,
	    clip->auto_switch,
	    changed,
	    vj_effect_real_to_sequence(clip->effect_chain[entry]->effect_id),
				      // effect_id),
	    clip->effect_chain[entry]->e_flag,
	    clip->effect_chain[entry]->frame_offset,
	    clip->effect_chain[entry]->frame_trimmer,
	    clip->effect_chain[entry]->source_type,
	    clip->effect_chain[entry]->channel,
	    clip->effect_chain[entry]->a_flag,
	    clip->effect_chain[entry]->volume,
	    this_clip_id );
    */

	sprintf(str,
		"%d %d %d %d %d %d %d %d %d %d %d %d %d",
		pfps,
		frame,
		mode,
		s1,
		clip->effect_toggle,
		clip->first_frame[ clip->active_render_entry ],
		clip->last_frame[ clip->active_render_entry ],
		clip->speed,
		clip->looptype,
		clip->encoder_active,
		clip->encoder_duration,
		clip->encoder_succes_frames,
		clip_size());
		
		
 
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
unsigned char *UTF8toLAT1(unsigned char *in)
{
    int in_size, out_size;
    unsigned char *out;

    if (in == NULL)
	return (NULL);

    out_size = in_size = (int) strlen(in) + 1;
    out = malloc((size_t) out_size);

    if (out == NULL) {
	return (NULL);
    }

    if (UTF8Toisolat1(out, &out_size, in, &in_size) != 0)
	{
		//veejay_msg(VEEJAY_MSG_ERROR, "Cannot convert '%s'", in );
		//free(out);
		//return (NULL);
		strncpy( out, in, out_size );
    }

    out = realloc(out, out_size + 1);
    out[out_size] = 0;		/*null terminating out */

    return (out);
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
    unsigned char *chTemp = NULL;
    int argIndex = 0;
    if (cur == NULL)
	fprintf(stderr, "error parsing arguments\n");
    while (cur != NULL && argIndex < CLIP_MAX_PARAMETERS) {
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


/*************************************************************************************************
 *
 * ParseEffect()
 *
 * Parse an effect using libxml2
 *
 ****************************************************************************************************/
void ParseEffect(xmlDocPtr doc, xmlNodePtr cur, int dst_clip)
{
    xmlChar *xmlTemp = NULL;
    unsigned char *chTemp = NULL;
    int effect_id = -1;
    int arg[CLIP_MAX_PARAMETERS];
    int i;
    int source_type = 0;
    int channel = 0;
    int frame_trimmer = 0;
    int frame_offset = 0;
    int e_flag = 0;
    int volume = 0;
    int a_flag = 0;
    int chain_index = 0;

    for (i = 0; i < CLIP_MAX_PARAMETERS; i++) {
	arg[i] = 0;
    }

    if (cur == NULL)
	fprintf(stderr, "Error in parseEffect\n");


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
	    ParseArguments(doc, cur->xmlChildrenNode, arg);
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
	if (clip_chain_add(dst_clip, chain_index, effect_id) == -1) {
	    fprintf(stderr, "Error parsing effect %d (pos %d)\n",
		    effect_id, chain_index);
	}

	/* load the parameter values */
	for (j = 0; j < vj_effect_get_num_params(effect_id); j++) {
	    clip_set_effect_arg(dst_clip, chain_index, j, arg[j]);
	}
	fprintf(stderr, "clip %d %d - E:%d p:%d, source %d channel %d\n",
		dst_clip,chain_index,effect_id,j,source_type,channel); 
	clip_set_chain_channel(dst_clip, chain_index, channel);
	clip_set_chain_source(dst_clip, chain_index, source_type);

	/* set other parameters */
	if (a_flag) {
	    clip_set_chain_audio(dst_clip, chain_index, a_flag);
	    clip_set_chain_volume(dst_clip, chain_index, volume);
	}

	clip_set_chain_status(dst_clip, chain_index, e_flag);

	clip_set_offset(dst_clip, chain_index, frame_offset);
	clip_set_trimmer(dst_clip, chain_index, frame_trimmer);
    }

}

/*************************************************************************************************
 *
 * ParseEffect()
 *
 * Parse the effects array 
 *
 ****************************************************************************************************/
void ParseEffects(xmlDocPtr doc, xmlNodePtr cur, clip_info * skel)
{
    int effectIndex = 0;
    while (cur != NULL && effectIndex < CLIP_MAX_EFFECTS) {
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_EFFECT)) {
	    ParseEffect(doc, cur->xmlChildrenNode, skel->clip_id);
		effectIndex++;
	}
	//effectIndex++;
	cur = cur->next;
    }
}

/*************************************************************************************************
 *
 * ParseClip()
 *
 * Parse a clip
 *
 ****************************************************************************************************/
void ParseClip(xmlDocPtr doc, xmlNodePtr cur, clip_info * skel)
{

    xmlChar *xmlTemp = NULL;
    unsigned char *chTemp = NULL;

    while (cur != NULL) {
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_CLIPID)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		skel->clip_id = atoi(chTemp);
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
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_CLIPDESCR)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		snprintf(skel->descr, CLIP_MAX_DESCR_LEN,"%s", chTemp);
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}

	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_FIRSTFRAME)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		clip_set_startframe(skel->clip_id, atol(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_VOL)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		clip_set_audio_volume(skel->clip_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);

	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_LASTFRAME)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		clip_set_endframe(skel->clip_id, atol(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_SPEED)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		clip_set_speed(skel->clip_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_FRAMEDUP)) {
		xmlTemp = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
		chTemp = UTF8toLAT1(xmlTemp);
		if(chTemp)
		{
			clip_set_framedup(skel->clip_id, atoi(chTemp));
			free(chTemp);
		}
		if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_LOOPTYPE)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		clip_set_looptype(skel->clip_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_MAXLOOPS)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		clip_set_loops(skel->clip_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_NEXTCLIP)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		clip_set_next(skel->clip_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_DEPTH)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		clip_set_depth(skel->clip_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_PLAYMODE)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		clip_set_playmode(skel->clip_id, atoi(chTemp));
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
	   //clip_set_volume(skel->clip_id, atoi(chTemp ));
	   }
	   }
	   if (!xmlStrcmp(cur->name, (const xmlChar *)XMLTAG_SUBAUDIO)) {
	   xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	   chTemp = UTF8toLAT1( xmlTemp );
	   if( chTemp ){
	   clip_set_sub_audio(skel->clip_id, atoi(chTemp ));
	   }
	   }
	 */
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_MARKERSTART)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		clip_set_marker_start(skel->clip_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);
	}
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_MARKEREND)) {
	    xmlTemp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    chTemp = UTF8toLAT1(xmlTemp);
	    if (chTemp) {
		clip_set_marker_end(skel->clip_id, atoi(chTemp));
		free(chTemp);
	    }
	    if(xmlTemp) xmlFree(xmlTemp);

	}

	ParseEffects(doc, cur->xmlChildrenNode, skel);

	// xmlTemp and chTemp should be freed after use
	xmlTemp = NULL;
	chTemp = NULL;

	cur = cur->next;
    }
    return;
}


/****************************************************************************************************
 *
 * clip_readFromFile( filename )
 *
 * load clips and effect chain from an xml file. 
 *
 ****************************************************************************************************/
int clip_readFromFile(char *clipFile)
{
    xmlDocPtr doc;
    xmlNodePtr cur;
    clip_info *skel;

    /*
     * build an XML tree from a the file;
     */
    doc = xmlParseFile(clipFile);
    if (doc == NULL) {
	return (0);
    }

    /*
     * Check the document is of the right kind
     */

    cur = xmlDocGetRootElement(doc);
    if (cur == NULL) {
	fprintf(stderr, "Empty cliplist. Nothing to do.\n");
	xmlFreeDoc(doc);
	return (0);
    }

    if (xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_CLIPS)) {
	fprintf(stderr, "This is not a cliplist: %s",
		XMLTAG_CLIPS);
	xmlFreeDoc(doc);
	return (0);
    }

    cur = cur->xmlChildrenNode;
    while (cur != NULL) {
	if (!xmlStrcmp(cur->name, (const xmlChar *) XMLTAG_CLIP)) {
	    skel = clip_skeleton_new(0, 1);
	    clip_store(skel);
	    if (skel != NULL) {
		ParseClip(doc, cur->xmlChildrenNode, skel);
	    }
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
    argcount = CLIP_MAX_PARAMETERS;
    for (i = 0; i < argcount; i++) {
	//if (arg[i]) {
	    sprintf(buffer, "%d", arg[i]);
	    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_ARGUMENT,
			(const xmlChar *) buffer);
	//}
    }
}


void CreateEffect(xmlNodePtr node, clip_eff_chain * effect, int position)
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
    CreateArguments(childnode, effect->arg,
		    vj_effect_get_num_params(effect->effect_id));

    
}




void CreateEffects(xmlNodePtr node, clip_eff_chain ** effects)
{
    int i;
    xmlNodePtr childnode;

    for (i = 0; i < CLIP_MAX_EFFECTS; i++) {
	if (effects[i]->effect_id != -1) {
	    childnode =
		xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECT,
			    NULL);
	    CreateEffect(childnode, effects[i], i);
	}
    }
    
}

void CreateClip(xmlNodePtr node, clip_info * clip)
{
    char buffer[100];
    xmlNodePtr childnode;

    sprintf(buffer, "%d", clip->clip_id);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_CLIPID,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", clip->effect_toggle);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_CHAIN_ENABLED,
		(const xmlChar *) buffer);


    sprintf(buffer,"%d", clip->active_render_entry);
    xmlNewChild(node,NULL,(const xmlChar*) XMLTAG_RENDER_ENTRY,(const xmlChar*)buffer);
    sprintf(buffer, "%s", clip->descr);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_CLIPDESCR,
		(const xmlChar *) buffer);
    sprintf(buffer, "%ld", clip->first_frame[clip->active_render_entry]);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_FIRSTFRAME,
		(const xmlChar *) buffer);
    sprintf(buffer, "%ld", clip->last_frame[clip->active_render_entry]);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_LASTFRAME,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", clip->speed);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_SPEED,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", clip->dup);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_FRAMEDUP,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", clip->looptype);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_LOOPTYPE,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", clip->max_loops);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_MAXLOOPS,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", clip->next_clip_id);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_NEXTCLIP,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", clip->depth);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_DEPTH,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", clip->playmode);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_PLAYMODE,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", clip->audio_volume);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_VOL,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", clip->marker_start);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_MARKERSTART,
		(const xmlChar *) buffer);
    sprintf(buffer, "%d", clip->marker_end);
    xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_MARKEREND,
		(const xmlChar *) buffer);

	sprintf(buffer,"%d",clip->fader_active);
	xmlNewChild(node,NULL,(const xmlChar *) XMLTAG_FADER_ACTIVE,
		(const xmlChar *) buffer);
	sprintf(buffer,"%f",clip->fader_inc);
	xmlNewChild(node,NULL,(const xmlChar *) XMLTAG_FADER_INC,
		(const xmlChar *) buffer);
	sprintf(buffer,"%f",clip->fader_val);
	xmlNewChild(node,NULL,(const xmlChar *) XMLTAG_FADER_VAL,
		(const xmlChar *) buffer);
	sprintf(buffer,"%d",clip->fader_direction);
	xmlNewChild(node,NULL,(const xmlChar *) XMLTAG_FADER_DIRECTION,
		(const xmlChar *) buffer);
	sprintf(buffer,"%d",clip->selected_entry);
	xmlNewChild(node,NULL,(const xmlChar *) XMLTAG_LASTENTRY,
		(const xmlChar *)buffer);
    childnode =
	xmlNewChild(node, NULL, (const xmlChar *) XMLTAG_EFFECTS, NULL);

    
    
    CreateEffects(childnode, clip->effect_chain);

}

/****************************************************************************************************
 *
 * clip_writeToFile( filename )
 *
 * writes all clip info to a file. 
 *
 ****************************************************************************************************/
int clip_writeToFile(char *clipFile)
{
    int i;
	char *encoding = "UTF-8";	
    clip_info *next_clip;
    xmlDocPtr doc;
    xmlNodePtr rootnode, childnode;

    doc = xmlNewDoc("1.0");
    rootnode =
	xmlNewDocNode(doc, NULL, (const xmlChar *) XMLTAG_CLIPS, NULL);
    xmlDocSetRootElement(doc, rootnode);
    for (i = 1; i < clip_size(); i++) {
	next_clip = clip_get(i);
	if (next_clip) {
	    childnode =
		xmlNewChild(rootnode, NULL,
			    (const xmlChar *) XMLTAG_CLIP, NULL);
	    CreateClip(childnode, next_clip);
	}
    }
    //xmlSaveFormatFile(clipFile, doc, 1);
	xmlSaveFormatFileEnc( clipFile, doc, encoding, 1 );
    xmlFreeDoc(doc);

    return 1;
}
#endif
