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
 * GNU General Public License for more details//.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
/*


	This file contains code-snippets from the mjpegtools' EditList
	(C) The Mjpegtools project

	http://mjpeg.sourceforge.net
*/
#include <config.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>
#include <veejaycore/defs.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vims.h>
#include <libel/lav_io.h>
#include <libel/vj-el.h>
#include <libel/vj-ffmpeg-input.h>
#include <libel/vj-nvjpeg.h>
#include <libvje/vje.h>
#include <libel/vj-avcodec.h>
#include <libel/elcache.h>
#include <libel/pixbuf.h>
#include <veejaycore/avcommon.h>
#include <limits.h>
#include <veejaycore/mpegconsts.h>
#include <veejaycore/mpegtimecode.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/yuvconv.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <veejaycore/avhelper.h>
#include <libel/av.h>
#include <veejaycore/vj-task.h>
#include <veejaycore/lzo.h>
#include <libel/qoi.h>
#include <math.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#ifdef SUPPORT_READ_DV2
#include "rawdv.h"
#include "vj-dv.h"
#endif
#define MAX_CODECS 50
#define CODEC_ID_YUV420 999
#define CODEC_ID_YUV422 998
#define CODEC_ID_YUV422F 997
#define CODEC_ID_YUV420F 996
#define CODEC_ID_QOIY 993
#define CODEC_ID_ANYTHING 990
#define CODEC_ID_YUVLZO 900
#define DUMMY_FRAMES 2

static struct
{
	const char *name;
} _chroma_str[] = 
{
	{	"Unknown"	}, // CHROMAUNKNOWN
	{	"4:2:0"		},
	{	"4:2:2"		},
	{	"4:4:4"		},
	{	"4:1:1"		},
	{	"4:2:0 full range" },
	{	"4:2:2 full range" }
};

static int el_pixel_format_org = 1;
static int el_pixel_format_ = 1;
static int el_width_ = 0;
static float el_fps_ = 30;
static int el_height_ = 0;
static int el_switch_jpeg_ = 0;

static VJFrame *el_out_ = NULL;

static int require_same_resolution = 0;

typedef struct
{
        const AVCodec *codec;
        AVCodecContext *codec_ctx;
        AVFormatContext *avformat_ctx;
        AVPacket pkt;
        int pixfmt;
        int codec_id;
} el_decoder_t;

extern void sample_new_simple( void *el, long start, long end );

typedef struct
{
        const AVCodec *codec;
        AVFrame *frame;
        AVCodecContext  *context;
        uint8_t *tmp_buffer;
		int fmt;
        int ref;
#ifdef SUPPORT_READ_DV2
	vj_dv_decoder *dv_decoder;
#endif
	void	      *lzo_decoder;
	vj_nvjpeg_decoder *nvjpeg_decoder;
	char          nvjpeg_reason[256];
	int           nvjpeg_forced;
	int           nvjpeg_probe_pending;
	int           nvjpeg_444_logged;
	int           nvjpeg_444_fallback_logged;
} vj_decoder;

extern int el_width_;
extern int el_height_;
extern int el_pixel_format_;

#define MAX_PLANES 4

typedef struct el_cache_owner_t el_cache_owner_t;

typedef struct raw_frame_node_t {
    uint64_t source_id;
    uint64_t source_frame_ref;
    uint8_t key_kind;
    uint8_t *planes[MAX_PLANES];
    el_cache_owner_t *owner;
    struct raw_frame_node_t *hash_next;
    struct raw_frame_node_t *prev;
    struct raw_frame_node_t *next;
    struct raw_frame_node_t *owner_prev;
    struct raw_frame_node_t *owner_next;
} raw_frame_node_t;

enum {
    EL_CACHE_PROBATION = 0,
    EL_CACHE_ACTIVE = 1,
    EL_CACHE_STREAMING = 2
};

struct el_cache_owner_t {
    uint64_t source_id;
    uint64_t source_frames;
    uint64_t last_frame;
    uint64_t run_min;
    uint64_t run_max;
    uint64_t probe_min;
    uint64_t probe_max;
    uint64_t window_min;
    uint64_t window_max;
    uint64_t requests;
    uint64_t hits;
    uint64_t misses;
    uint64_t admissions;
    uint64_t bypasses;
    uint64_t promotions;
    uint64_t demotions;
    long resident;
    long probation_admitted;
    long streaming_run;
    int direction;
    uint8_t key_kind;
    uint8_t state;
    uint8_t has_last;
    uint8_t probe_valid;
    uint8_t window_valid;
    uint8_t fixed_fit;
    raw_frame_node_t *resident_head;
    raw_frame_node_t *resident_tail;
    el_cache_owner_t *next;
};

typedef struct {
    el_cache_owner_t *owner;
    int lookup;
    int admit;
    int admit_preroll;
    int exhaust_on_miss;
    long preroll_limit;
} el_cache_decision_t;

typedef struct {
    long capacity;
    long usable_capacity;
    long probe_limit;
    long size;
    long borrowed;
    el_cache_owner_t *borrow_owner;
    uint64_t requests;
    uint64_t hits;
    uint64_t misses;
    uint64_t admissions;
    uint64_t bypasses;
    uint64_t promotions;
    uint64_t demotions;
    uint64_t purges;
    uint64_t purged_frames;
    uint64_t evictions;
    uint64_t generic_requests;
    uint64_t generic_hits;
    uint64_t generic_misses;
    uint64_t generic_inserts;
    uint64_t generic_updates;
    uint64_t generic_evictions;
    uint64_t generic_preroll_stores;
    uint64_t generic_borrow_events;
    long generic_max_borrowed;
    long owner_count;
    el_cache_owner_t *owners;
    size_t index_bucket_count;
    raw_frame_node_t **index_buckets;
    raw_frame_node_t *head;
    raw_frame_node_t *tail;
} global_raw_frame_cache_t;

enum {
    EL_CACHE_KEY_LEGACY = 0,
    EL_CACHE_KEY_MEDIA = 1
};

static global_raw_frame_cache_t *global_cache = NULL;

static long el_cache_purge_owner(global_raw_frame_cache_t *cache,
                                 el_cache_owner_t *owner);

static int memory_threshold = 30;

static inline uint64_t editlist_source_hash(const editlist *e)
{
    uint64_t h = 1469598103934665603ULL;

    if (!e) return 0;

    for (int i = 0; i < e->num_video_files; i++)
    {
        const char *s = e->video_file_list[i];
        if (!s) continue;

        while (*s)
        {
            h ^= (unsigned char)*s++;
            h *= 1099511628211ULL;
        }
    }

    h ^= (uint64_t)(uintptr_t)e;
    h *= 1099511628211ULL;

    return h;
}

static inline uint64_t el_hash_u64(uint64_t h, uint64_t value)
{
    for(int i = 0; i < 8; i++) {
        h ^= (unsigned char)(value & 0xffu);
        h *= 1099511628211ULL;
        value >>= 8;
    }
    return h;
}

static uint64_t media_source_hash(const char *filename)
{
    uint64_t h = 1469598103934665603ULL;
    char canonical[PATH_MAX];
    const char *identity = filename;
    if(filename && realpath(filename, canonical))
        identity = canonical;

    const unsigned char *s = (const unsigned char *)identity;
    while(s && *s) {
        h ^= *s++;
        h *= 1099511628211ULL;
    }

    struct stat st;
    if(identity && stat(identity, &st) == 0) {
        h = el_hash_u64(h, (uint64_t)st.st_dev);
        h = el_hash_u64(h, (uint64_t)st.st_ino);
        h = el_hash_u64(h, (uint64_t)st.st_size);
        h = el_hash_u64(h, (uint64_t)st.st_mtime);
    }
    return h;
}

static inline int editlist_same_source(const editlist *a, const editlist *b)
{
    if (a == b) return 1;
    if (!a || !b) return 0;

    return a->source_hash == b->source_hash;
}

void el_cache_configure(int t) {

	if( t < 0 )
		t = 0;
	else if ( t > 80 ) {
		t = 80;
		veejay_msg(VEEJAY_MSG_WARNING, "You still need memory for the FX chain and the system, capping to 80 percent");
	}

	memory_threshold = t;
	veejay_msg(VEEJAY_MSG_DEBUG,
	           "[ELCACHE] configured memory_percent=%d",
	           memory_threshold);
}

static long get_available_cache_capacity(void) {
    long page_size = sysconf(_SC_PAGESIZE);
    long available_pages = sysconf(_SC_AVPHYS_PAGES);

    if (page_size == -1 || available_pages == -1) {
		veejay_msg(VEEJAY_MSG_WARNING, "Unable to query free memory, assuming you have room for 250 video frames");
        return 250;
    }

    unsigned long available_bytes = (unsigned long)available_pages * (unsigned long)page_size;
    unsigned long cache_budget = available_bytes * memory_threshold / 100;
    
    size_t frame_size = sizeof(raw_frame_node_t) + el_out_->len + (2 * el_out_->uv_len);
    return (long)(cache_budget / frame_size);
}

static size_t el_cache_index_size(long capacity)
{
    if(capacity <= 0)
        return 0;

    size_t target = (size_t)capacity;
    if(target <= SIZE_MAX / 2)
        target *= 2;

    size_t buckets = 256;
    while(buckets < target && buckets <= SIZE_MAX / 2)
        buckets <<= 1;

    if(buckets < target || buckets > SIZE_MAX / sizeof(raw_frame_node_t *))
        return 0;
    return buckets;
}

static global_raw_frame_cache_t *get_global_cache(void) {

	if(memory_threshold == 0)
		return NULL;

    if (global_cache == NULL) {
        global_cache = (global_raw_frame_cache_t *)vj_calloc(sizeof(global_raw_frame_cache_t));
		if(global_cache == NULL) {
			veejay_msg(VEEJAY_MSG_ERROR, "Out of memory");
			return NULL;
		}
        global_cache->capacity = get_available_cache_capacity();
        long reserve = global_cache->capacity / 16;
        if(reserve < 32)
            reserve = 32;
        global_cache->usable_capacity = global_cache->capacity > reserve ?
                                        global_cache->capacity - reserve :
                                        global_cache->capacity;
        global_cache->size = 0;
        global_cache->borrowed = 0;
        global_cache->borrow_owner = NULL;
        global_cache->owners = NULL;
        global_cache->head = NULL;
        global_cache->tail = NULL;

        global_cache->index_bucket_count = el_cache_index_size(global_cache->capacity);
        if(global_cache->index_bucket_count > 0)
            global_cache->index_buckets = (raw_frame_node_t **)vj_calloc(
                global_cache->index_bucket_count * sizeof(raw_frame_node_t *));
        if(global_cache->capacity > 0 && !global_cache->index_buckets) {
            veejay_msg(VEEJAY_MSG_WARNING,
                       "Unable to allocate raw frame cache index; disabling raw frame cache");
            free(global_cache);
            global_cache = NULL;
            return NULL;
        }

        size_t frame_size = sizeof(raw_frame_node_t) + el_out_->len + (2 * el_out_->uv_len);
        long probe_mb = 32;
        const char *probe_setting = getenv("VEEJAY_CACHE_PROBE_MB");
        if(probe_setting && *probe_setting) {
            char *end = NULL;
            long configured = strtol(probe_setting, &end, 10);
            if(end != probe_setting && *end == '\0' &&
               configured >= 4 && configured <= 512)
                probe_mb = configured;
            else
                veejay_msg(VEEJAY_MSG_WARNING,
                           "[ELCACHE] ignoring invalid VEEJAY_CACHE_PROBE_MB='%s' (expected 4..512)",
                           probe_setting);
        }
        const size_t probe_bytes = (size_t)probe_mb * 1024u * 1024u;
        long probe_limit = frame_size > 0 ? (long)(probe_bytes / frame_size) : 0;
        if(probe_limit < 8)
            probe_limit = 8;
        if(probe_limit > 128)
            probe_limit = 128;
        if(probe_limit > global_cache->usable_capacity)
            probe_limit = global_cache->usable_capacity;
        global_cache->probe_limit = probe_limit;

        double total_mb = ((double)global_cache->capacity * (double)frame_size) /
                          (1024.0 * 1024.0);
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[ELCACHE] initialized memory_percent=%d capacity=%ld usable=%ld probe=%ld probe_budget=%ldMB buckets=%zu frame_bytes=%zu budget=%.2fMB borrow_limit=%ld",
                   memory_threshold,
                   global_cache->capacity,
                   global_cache->usable_capacity,
                   global_cache->probe_limit,
                   probe_mb,
                   global_cache->index_bucket_count,
                   frame_size,
                   total_mb,
                   global_cache->capacity > 1 ? global_cache->capacity / 2 : global_cache->capacity);

    }
    return global_cache;
}

void vj_cache_print_status(void) {
    global_raw_frame_cache_t *cache = get_global_cache();
    if (cache == NULL) return;

    size_t frame_size = sizeof(raw_frame_node_t) + el_out_->len + (2 * el_out_->uv_len);
    float total_mb = (cache->capacity * frame_size) / (1024.0f * 1024.0f);
    long probation = 0;
    long active = 0;
    long streaming = 0;

    for(el_cache_owner_t *owner = cache->owners; owner; owner = owner->next) {
        if(owner->state == EL_CACHE_ACTIVE)
            active++;
        else if(owner->state == EL_CACHE_STREAMING)
            streaming++;
        else
            probation++;
    }

    veejay_msg(VEEJAY_MSG_INFO,
               "[ELCACHE] capacity=%ld usable=%ld probe=%ld used=%ld memory_budget=%.2fMB owners=%ld probation=%ld active=%ld streaming=%ld hits=%" PRIu64 " misses=%" PRIu64 " admissions=%" PRIu64 " bypasses=%" PRIu64 " promotions=%" PRIu64 " demotions=%" PRIu64 " evictions=%" PRIu64 " purges=%" PRIu64 " purged_frames=%" PRIu64,
               cache->capacity,
               cache->usable_capacity,
               cache->probe_limit,
               cache->size,
               total_mb,
               cache->owner_count,
               probation,
               active,
               streaming,
               cache->hits,
               cache->misses,
               cache->admissions,
               cache->bypasses,
               cache->promotions,
               cache->demotions,
               cache->evictions,
               cache->purges,
               cache->purged_frames);
}

int vj_cache_get_stats(vj_el_cache_stats *stats)
{
    global_raw_frame_cache_t *cache;

    if(!stats)
        return 0;

    memset(stats, 0, sizeof(*stats));
    cache = global_cache;
    if(!cache)
        return 0;

    stats->capacity = cache->capacity;
    stats->usable_capacity = cache->usable_capacity;
    stats->size = cache->size;
    stats->owner_count = cache->owner_count;
    stats->requests = cache->requests;
    stats->hits = cache->hits;
    stats->misses = cache->misses;
    stats->admissions = cache->admissions;
    stats->bypasses = cache->bypasses;
    stats->promotions = cache->promotions;
    stats->demotions = cache->demotions;
    stats->evictions = cache->evictions;
    stats->purges = cache->purges;
    stats->purged_frames = cache->purged_frames;
    return 1;
}

static int el_cache_span_fits(global_raw_frame_cache_t *cache,
                              uint64_t minimum,
                              uint64_t maximum)
{
    if(!cache || cache->usable_capacity <= 0 || maximum < minimum)
        return 0;
    return maximum - minimum < (uint64_t)cache->usable_capacity;
}

static el_cache_owner_t *el_cache_owner_get(global_raw_frame_cache_t *cache,
                                             uint8_t key_kind,
                                             uint64_t source_id,
                                             long source_frames)
{
    for(el_cache_owner_t *owner = cache->owners; owner; owner = owner->next)
        if(owner->key_kind == key_kind && owner->source_id == source_id)
            return owner;

    el_cache_owner_t *owner = (el_cache_owner_t *)vj_calloc(sizeof(el_cache_owner_t));
    if(!owner)
        return NULL;

    owner->key_kind = key_kind;
    owner->source_id = source_id;
    owner->source_frames = source_frames > 0 ? (uint64_t)source_frames : 0;
    owner->state = EL_CACHE_PROBATION;

    if(owner->source_frames > 0 &&
       owner->source_frames <= (uint64_t)cache->usable_capacity) {
        owner->state = EL_CACHE_ACTIVE;
        owner->fixed_fit = 1;
        owner->window_valid = 1;
        owner->window_min = 0;
        owner->window_max = owner->source_frames - 1;
    }

    owner->next = cache->owners;
    cache->owners = owner;
    cache->owner_count++;
    return owner;
}

static void el_cache_owner_set_sequence(el_cache_owner_t *owner,
                                        uint64_t source_frame)
{
    owner->last_frame = source_frame;
    owner->run_min = source_frame;
    owner->run_max = source_frame;
    owner->direction = 0;
    owner->has_last = 1;
}

static void el_cache_owner_begin_probation(global_raw_frame_cache_t *cache,
                                           el_cache_owner_t *owner,
                                           uint64_t source_frame)
{
    if(owner->state == EL_CACHE_ACTIVE) {
        owner->demotions++;
        cache->demotions++;
    }
    owner->state = EL_CACHE_PROBATION;
    owner->fixed_fit = 0;
    owner->window_valid = 0;
    owner->probe_valid = 0;
    owner->probation_admitted = 0;
    owner->streaming_run = 0;
    el_cache_owner_set_sequence(owner, source_frame);
}

static void el_cache_owner_activate(global_raw_frame_cache_t *cache,
                                    el_cache_owner_t *owner,
                                    uint64_t minimum,
                                    uint64_t maximum)
{
    if(!el_cache_span_fits(cache, minimum, maximum))
        return;
    if(owner->state == EL_CACHE_ACTIVE && owner->window_valid &&
       minimum >= owner->window_min && maximum <= owner->window_max)
        return;

    if(owner->state != EL_CACHE_ACTIVE) {
        owner->promotions++;
        cache->promotions++;
    }
    owner->state = EL_CACHE_ACTIVE;
    owner->window_valid = 1;
    owner->fixed_fit = 0;
    owner->window_min = minimum;
    owner->window_max = maximum;
    owner->streaming_run = 0;
}

static void el_cache_owner_begin_streaming(global_raw_frame_cache_t *cache,
                                           el_cache_owner_t *owner)
{
    if(owner->state != EL_CACHE_STREAMING) {
        owner->demotions++;
        cache->demotions++;
    }
    owner->state = EL_CACHE_STREAMING;
    owner->fixed_fit = 0;
    owner->window_valid = 0;
    owner->streaming_run = 0;
}

static int el_cache_frame_direction(uint64_t previous,
                                    uint64_t current,
                                    int *contiguous)
{
    *contiguous = 0;
    if(current == previous)
        return 0;
    if(current > previous) {
        if(current - previous == 1)
            *contiguous = 1;
        return 1;
    }
    if(previous - current == 1)
        *contiguous = 1;
    return -1;
}

static el_cache_decision_t el_cache_prepare_request(global_raw_frame_cache_t *cache,
                                                     uint8_t key_kind,
                                                     uint64_t source_id,
                                                     uint64_t source_frame,
                                                     long source_frames)
{
    el_cache_decision_t decision;
    memset(&decision, 0, sizeof(decision));

    if(!cache || cache->capacity <= 0)
        return decision;

    el_cache_owner_t *owner = el_cache_owner_get(cache,
                                                 key_kind,
                                                 source_id,
                                                 source_frames);
    if(!owner)
        return decision;

    decision.owner = owner;
    owner->requests++;
    cache->requests++;
    if(key_kind == EL_CACHE_KEY_MEDIA)
        cache->generic_requests++;

    if(owner->state == EL_CACHE_ACTIVE && owner->window_valid &&
       (source_frame < owner->window_min || source_frame > owner->window_max)) {
        el_cache_purge_owner(cache, owner);
        el_cache_owner_begin_probation(cache, owner, source_frame);
    }
    else if(!owner->has_last) {
        el_cache_owner_set_sequence(owner, source_frame);
    }
    else {
        uint64_t previous = owner->last_frame;
        uint64_t old_run_min = owner->run_min;
        uint64_t old_run_max = owner->run_max;
        int old_direction = owner->direction;
        int contiguous = 0;
        int direction = el_cache_frame_direction(previous,
                                                 source_frame,
                                                 &contiguous);
        int fitting_boundary = 0;
        int oversized_boundary = 0;
        uint64_t boundary_min = source_frame;
        uint64_t boundary_max = source_frame;

        if(direction == 0) {
            fitting_boundary = 1;
        }
        else if(old_direction != 0 && direction != old_direction) {
            boundary_min = old_run_min < source_frame ? old_run_min : source_frame;
            boundary_max = old_run_max > source_frame ? old_run_max : source_frame;
            if(el_cache_span_fits(cache, boundary_min, boundary_max))
                fitting_boundary = 1;
            else
                oversized_boundary = 1;
        }

        if(fitting_boundary)
            el_cache_owner_activate(cache, owner, boundary_min, boundary_max);
        else if(oversized_boundary) {
            el_cache_purge_owner(cache, owner);
            el_cache_owner_begin_streaming(cache, owner);
        }

        if(direction == 0) {
            owner->run_min = source_frame;
            owner->run_max = source_frame;
            owner->direction = 0;
        }
        else if(contiguous && (old_direction == 0 || direction == old_direction)) {
            owner->run_min = old_run_min < source_frame ? old_run_min : source_frame;
            owner->run_max = old_run_max > source_frame ? old_run_max : source_frame;
            owner->direction = direction;
        }
        else if(contiguous) {
            owner->run_min = previous < source_frame ? previous : source_frame;
            owner->run_max = previous > source_frame ? previous : source_frame;
            owner->direction = direction;
        }
        else {
            owner->run_min = source_frame;
            owner->run_max = source_frame;
            owner->direction = 0;
        }
        owner->last_frame = source_frame;

        if(owner->state == EL_CACHE_STREAMING && !fitting_boundary) {
            int sequential = contiguous &&
                             (old_direction == 0 || direction == old_direction);
            if(oversized_boundary) {
                owner->streaming_run = 0;
            }
            else if(sequential) {
                owner->streaming_run++;
            }
            else if(owner->streaming_run >= cache->probe_limit) {
                el_cache_purge_owner(cache, owner);
                el_cache_owner_begin_probation(cache, owner, source_frame);
            }
            else {
                decision.lookup = 1;
                owner->streaming_run = 0;
            }
        }
    }

    if(owner->state == EL_CACHE_ACTIVE) {
        decision.lookup = 1;
        decision.admit = 1;
        if(owner->window_valid && source_frame > owner->window_min) {
            uint64_t available = source_frame - owner->window_min;
            decision.preroll_limit = available > (uint64_t)LONG_MAX ?
                                     LONG_MAX : (long)available;
            decision.admit_preroll = decision.preroll_limit > 0;
        }
    }
    else if(owner->state == EL_CACHE_PROBATION) {
        decision.lookup = 1;
        long remaining = cache->probe_limit - owner->probation_admitted;
        if(remaining > 0) {
            decision.admit = 1;
            if(remaining > 1) {
                decision.admit_preroll = 1;
                decision.preroll_limit = remaining - 1;
            }
        }
        else {
            decision.exhaust_on_miss = 1;
        }
    }
    else {
        owner->bypasses++;
        cache->bypasses++;
    }

    return decision;
}

static void el_cache_note_admission(global_raw_frame_cache_t *cache,
                                    el_cache_owner_t *owner,
                                    uint64_t source_frame)
{
    owner->admissions++;
    cache->admissions++;

    if(owner->state != EL_CACHE_PROBATION)
        return;

    owner->probation_admitted++;
    if(!owner->probe_valid) {
        owner->probe_min = source_frame;
        owner->probe_max = source_frame;
        owner->probe_valid = 1;
    }
    else {
        if(source_frame < owner->probe_min)
            owner->probe_min = source_frame;
        if(source_frame > owner->probe_max)
            owner->probe_max = source_frame;
    }
}

static void el_cache_note_result(global_raw_frame_cache_t *cache,
                                 el_cache_decision_t *decision,
                                 uint64_t source_frame,
                                 int hit)
{
    el_cache_owner_t *owner = decision->owner;
    if(!cache || !owner)
        return;

    if(hit) {
        owner->hits++;
        cache->hits++;
        if(owner->key_kind == EL_CACHE_KEY_MEDIA)
            cache->generic_hits++;

        if(owner->state != EL_CACHE_ACTIVE) {
            uint64_t minimum = source_frame;
            uint64_t maximum = source_frame;
            if(owner->probe_valid) {
                uint64_t candidate_min = owner->probe_min < source_frame ?
                                         owner->probe_min : source_frame;
                uint64_t candidate_max = owner->probe_max > source_frame ?
                                         owner->probe_max : source_frame;
                if(el_cache_span_fits(cache, candidate_min, candidate_max)) {
                    minimum = candidate_min;
                    maximum = candidate_max;
                }
            }
            el_cache_owner_activate(cache, owner, minimum, maximum);
        }
        return;
    }

    owner->misses++;
    cache->misses++;
    if(owner->key_kind == EL_CACHE_KEY_MEDIA)
        cache->generic_misses++;

    if(decision->exhaust_on_miss && owner->state == EL_CACHE_PROBATION)
        el_cache_owner_begin_streaming(cache, owner);
}

static void unlink_cache_node(global_raw_frame_cache_t *cache, raw_frame_node_t *node)
{
    if(node->prev)
        node->prev->next = node->next;
    else
        cache->head = node->next;

    if(node->next)
        node->next->prev = node->prev;
    else
        cache->tail = node->prev;
}

static void el_cache_owner_detach(el_cache_owner_t *owner,
                                  raw_frame_node_t *node,
                                  int update_resident)
{
    if(!owner || node->owner != owner)
        return;

    if(node->owner_prev)
        node->owner_prev->owner_next = node->owner_next;
    else
        owner->resident_head = node->owner_next;

    if(node->owner_next)
        node->owner_next->owner_prev = node->owner_prev;
    else
        owner->resident_tail = node->owner_prev;

    node->owner_prev = NULL;
    node->owner_next = NULL;

    if(update_resident && owner->resident > 0)
        owner->resident--;
}

static void el_cache_owner_attach_head(el_cache_owner_t *owner,
                                       raw_frame_node_t *node,
                                       int update_resident)
{
    if(!owner)
        return;

    node->owner = owner;
    node->owner_prev = NULL;
    node->owner_next = owner->resident_head;

    if(owner->resident_head)
        owner->resident_head->owner_prev = node;

    owner->resident_head = node;

    if(!owner->resident_tail)
        owner->resident_tail = node;

    if(update_resident)
        owner->resident++;
}

static void el_cache_owner_move_to_head(el_cache_owner_t *owner,
                                        raw_frame_node_t *node)
{
    if(!owner || node->owner != owner || node == owner->resident_head)
        return;

    el_cache_owner_detach(owner, node, 0);
    el_cache_owner_attach_head(owner, node, 0);
}

static void move_cache_node_to_head(global_raw_frame_cache_t *cache,
                                    raw_frame_node_t *node)
{
    if(!cache || !node)
        return;

    if(node != cache->head) {
        unlink_cache_node(cache, node);

        node->prev = NULL;
        node->next = cache->head;

        if(cache->head)
            cache->head->prev = node;

        cache->head = node;

        if(!cache->tail)
            cache->tail = node;
    }

    if(node->owner)
        el_cache_owner_move_to_head(node->owner, node);
}

static uint64_t el_cache_mix64(uint64_t value)
{
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return value;
}

static size_t el_cache_bucket(global_raw_frame_cache_t *cache,
                              uint8_t key_kind,
                              uint64_t source_id,
                              uint64_t source_frame_ref)
{
    uint64_t key = source_id ^
                   el_cache_mix64(source_frame_ref + UINT64_C(0x9e3779b97f4a7c15)) ^
                   ((uint64_t)key_kind << 56);
    return (size_t)(el_cache_mix64(key) &
                    (uint64_t)(cache->index_bucket_count - 1));
}

static void el_cache_index_insert(global_raw_frame_cache_t *cache,
                                  raw_frame_node_t *node)
{
    if(!cache->index_buckets)
        return;
    size_t bucket = el_cache_bucket(cache,
                                    node->key_kind,
                                    node->source_id,
                                    node->source_frame_ref);
    node->hash_next = cache->index_buckets[bucket];
    cache->index_buckets[bucket] = node;
}

static void el_cache_index_remove(global_raw_frame_cache_t *cache,
                                  raw_frame_node_t *node)
{
    if(!cache->index_buckets)
        return;
    size_t bucket = el_cache_bucket(cache,
                                    node->key_kind,
                                    node->source_id,
                                    node->source_frame_ref);
    raw_frame_node_t **link = &cache->index_buckets[bucket];
    while(*link && *link != node)
        link = &(*link)->hash_next;
    if(*link == node)
        *link = node->hash_next;
    node->hash_next = NULL;
}

static raw_frame_node_t *find_cache_node(global_raw_frame_cache_t *cache,
                                         uint8_t key_kind,
                                         uint64_t source_id,
                                         uint64_t source_frame_ref)
{
    if(cache->index_buckets) {
        size_t bucket = el_cache_bucket(cache,
                                        key_kind,
                                        source_id,
                                        source_frame_ref);
        for(raw_frame_node_t *node = cache->index_buckets[bucket];
            node;
            node = node->hash_next)
            if(node->key_kind == key_kind &&
               node->source_id == source_id &&
               node->source_frame_ref == source_frame_ref)
                return node;
        return NULL;
    }

    for(raw_frame_node_t *node = cache->head; node; node = node->next)
        if(node->key_kind == key_kind &&
           node->source_id == source_id &&
           node->source_frame_ref == source_frame_ref)
            return node;
    return NULL;
}

static void el_cache_drop_node(global_raw_frame_cache_t *cache,
                               raw_frame_node_t *node,
                               int count_eviction)
{
    el_cache_owner_t *owner;

    if(!cache || !node)
        return;

    owner = node->owner;

    if(count_eviction && node->key_kind == EL_CACHE_KEY_MEDIA)
        cache->generic_evictions++;

    el_cache_index_remove(cache, node);
    unlink_cache_node(cache, node);

    if(owner)
        el_cache_owner_detach(owner, node, 1);

    if(cache->size > 0)
        cache->size--;

    if(count_eviction)
        cache->evictions++;

    node->owner = NULL;
    node->hash_next = NULL;
    node->prev = NULL;
    node->next = NULL;
    node->owner_prev = NULL;
    node->owner_next = NULL;

    node->planes[0] = NULL;
    node->planes[1] = NULL;
    node->planes[2] = NULL;
    node->planes[3] = NULL;

    free(node);
}

static void evict_oldest_frame(global_raw_frame_cache_t *cache)
{
    raw_frame_node_t *node;

    if(!cache)
        return;

    node = cache->tail;

    if(!node)
        return;

    el_cache_drop_node(cache, node, 1);
}

static void evict_oldest_frame_for_owner(global_raw_frame_cache_t *cache,
                                         el_cache_owner_t *owner)
{
    raw_frame_node_t *node = NULL;

    if(owner)
        node = owner->resident_tail;

    if(node) {
        el_cache_drop_node(cache, node, 1);
        return;
    }

    evict_oldest_frame(cache);
}

static long el_cache_purge_owner(global_raw_frame_cache_t *cache,
                                 el_cache_owner_t *owner)
{
    long removed = 0;
    raw_frame_node_t *node;

    if(!cache || !owner || owner->resident <= 0)
        return 0;

    node = owner->resident_head;

    while(node) {
        raw_frame_node_t *next = node->owner_next;

        el_cache_drop_node(cache, node, 0);

        removed++;

        node = next;
    }

    owner->resident = 0;
    owner->resident_head = NULL;
    owner->resident_tail = NULL;

    if(removed > 0) {
        cache->purges++;
        cache->purged_frames += (uint64_t)removed;
    }

    return removed;
}

static long el_cache_borrow(global_raw_frame_cache_t *cache,
                            el_cache_owner_t *owner,
                            long requested)
{
    if(!cache || requested <= 0 || cache->capacity <= 0)
        return 0;

    if(owner && owner->state == EL_CACHE_PROBATION) {
        long free_slots = cache->capacity - cache->size;
        if(free_slots <= 1)
            return 0;
        if(requested >= free_slots)
            requested = free_slots - 1;

        cache->borrowed = requested;
        cache->borrow_owner = owner;
        cache->generic_borrow_events++;
        if(requested > cache->generic_max_borrowed)
            cache->generic_max_borrowed = requested;
        return requested;
    }

    long max_borrow = cache->capacity > 1 ? cache->capacity / 2 : 1;
    long granted = requested < max_borrow ? requested : max_borrow;
    if(granted < 1)
        granted = 1;

    cache->borrowed = granted;
    cache->borrow_owner = owner;
    cache->generic_borrow_events++;
    if(granted > cache->generic_max_borrowed)
        cache->generic_max_borrowed = granted;
    long normal_limit = cache->capacity - granted;

    while(cache->size > normal_limit && cache->size > 0) {
        raw_frame_node_t *candidate = NULL;

        if(owner && owner->resident_tail)
            candidate = owner->resident_tail;
        else
            candidate = cache->tail;

        if(!candidate)
            break;

        evict_oldest_frame_for_owner(cache, owner);
    }
    return granted;
}

static void el_cache_release_borrow(global_raw_frame_cache_t *cache)
{
    if(!cache || cache->borrowed <= 0)
        return;
    cache->borrowed = 0;
    cache->borrow_owner = NULL;
}

static int el_cache_frame(global_raw_frame_cache_t *cache,
                          el_cache_owner_t *owner,
                          uint8_t key_kind,
                          uint64_t source_id,
                          uint64_t source_frame_ref,
                          uint8_t *src[4],
                          int update_existing) {
    if(!cache || cache->capacity <= 0)
        return 0;

    size_t plane_sizes[3] = {el_out_->len, el_out_->uv_len, el_out_->uv_len};
    raw_frame_node_t *existing = update_existing ?
                                 find_cache_node(cache,
                                                 key_kind,
                                                 source_id,
                                                 source_frame_ref) : NULL;
    if(existing) {
        if(key_kind == EL_CACHE_KEY_MEDIA)
            cache->generic_updates++;
        for(int i = 0; i < 3; i++)
            veejay_memcpy(existing->planes[i], src[i], plane_sizes[i]);
        move_cache_node_to_head(cache, existing);
        return 0;
    }

    if (cache->size >= cache->capacity) {
        if(owner && owner->state == EL_CACHE_PROBATION)
            return 0;
        if(cache->borrowed > 0)
            evict_oldest_frame_for_owner(cache, cache->borrow_owner);
        else
            evict_oldest_frame(cache);
    }

    size_t frame_bytes = plane_sizes[0] + plane_sizes[1] + plane_sizes[2];

    raw_frame_node_t *new_node =
        (raw_frame_node_t *)vj_malloc(sizeof(raw_frame_node_t) + frame_bytes);

    if(!new_node) {
        veejay_msg(VEEJAY_MSG_ERROR, "Cannot add frame to cache, memory full");
        return 0;
    }

    new_node->key_kind = key_kind;
    new_node->source_id = source_id;
    new_node->source_frame_ref = source_frame_ref;
    new_node->owner = owner;
    new_node->hash_next = NULL;
    new_node->owner_prev = NULL;
    new_node->owner_next = NULL;

    uint8_t *pixels = (uint8_t *)(new_node + 1);

    for(int i = 0; i < 3; i++) {
        new_node->planes[i] = pixels;
        veejay_memcpy(new_node->planes[i], src[i], plane_sizes[i]);
        pixels += plane_sizes[i];
    }

    new_node->planes[3] = NULL;

    el_cache_index_insert(cache, new_node);

    new_node->next = cache->head;
    new_node->prev = NULL;

    if(cache->head)
        cache->head->prev = new_node;

    cache->head = new_node;

    if(cache->tail == NULL)
        cache->tail = new_node;

    if(owner)
        el_cache_owner_attach_head(owner, new_node, 1);

    cache->size++;

    if(key_kind == EL_CACHE_KEY_MEDIA)
        cache->generic_inserts++;

    if(owner)
        el_cache_note_admission(cache, owner, source_frame_ref);

    return 1;

}

static int find_cached_frame(global_raw_frame_cache_t *cache,
                             uint8_t key_kind,
                             uint64_t source_id,
                             uint64_t source_frame_ref,
                             uint8_t *dst[4]) {
    size_t plane_sizes[4] = {el_out_->len, el_out_->uv_len, el_out_->uv_len, 0};
    raw_frame_node_t *node = find_cache_node(cache, key_kind, source_id, source_frame_ref);
    if(!node)
        return 0;

    for (int i = 0; i < 3; i++)
        if(node->planes[i] && dst[i])
            veejay_memcpy(dst[i], node->planes[i], plane_sizes[i]);

    move_cache_node_to_head(cache, node);
    return 1;
}

typedef struct
{
    global_raw_frame_cache_t *cache;
    el_cache_owner_t *owner;
    uint64_t source_id;
    long remaining;
} ffmpeg_cache_sink_t;

static void el_cache_store_ffmpeg_preroll(void *opaque,
                                          int64_t frame_number,
                                          uint8_t *planes[4])
{
    ffmpeg_cache_sink_t *sink = (ffmpeg_cache_sink_t *)opaque;
    if(!sink || !sink->cache || !sink->owner ||
       sink->remaining <= 0 || frame_number < 0)
        return;
    if(sink->owner->state == EL_CACHE_ACTIVE &&
       sink->owner->window_valid &&
       ((uint64_t)frame_number < sink->owner->window_min ||
        (uint64_t)frame_number > sink->owner->window_max))
        return;

    if(el_cache_frame(sink->cache,
                      sink->owner,
                      EL_CACHE_KEY_MEDIA,
                      sink->source_id,
                      (uint64_t)frame_number,
                      planes,
                      1)) {
        sink->remaining--;
        sink->cache->generic_preroll_stores++;
    }
}


char	vj_el_get_default_norm( float fps )
{
	if( fps == 25.0f )
		return 'p';
	if( fps > 23.0f && fps < 24.0f )
		return 's';
	if( fps > 29.0f && fps <= 30.0f )
		return 'n';
	return 'p';
}

float	vj_el_get_default_framerate( int norm )
{
	switch( norm ) {
		case VIDEO_MODE_PAL:
			return 25.0f;
		case VIDEO_MODE_SECAM:
			return 23.976f;
		case VIDEO_MODE_NTSC:
			return 29.97f;
		default:
			veejay_msg(VEEJAY_MSG_WARNING, "Unknown video norm! Use 'p' (PAL), 'n' (NTSC) or 's' (SECAM)");
	}
	return 30.0f;
}

int	vj_el_get_usec_per_frame( float video_fps ) 
{
	return (int)(1000000 / video_fps);
}

static void	_el_free_decoder( vj_decoder *d )
{
	if(d)
	{
		if(d->nvjpeg_decoder)
			vj_nvjpeg_decoder_destroy(d->nvjpeg_decoder);
		if(d->tmp_buffer)
			free( d->tmp_buffer );
#ifdef SUPPORT_READ_DV2
		if( d->dv_decoder ) {
			vj_dv_free_decoder( d->dv_decoder );
		}
#endif
		if(d->frame) {
			av_free(d->frame);
		}
		if( d->lzo_decoder )
			lzo_free(d->lzo_decoder);

		free(d);
	}
	d = NULL;
}


static int should_enable_drop_frame_timecode(float fps)
{
    return (
        fabs(fps - 29.97f) < 0.01f ||
        fabs(fps - 59.94f) < 0.01f ||
        fabs(fps - 119.88) < 0.01f ||
        fabs(fps - 23.976f) < 0.01f
    );
}

void	vj_el_init(int pf, int switch_jpeg, int dw, int dh, float fps)
{
	el_pixel_format_org = pf;
	el_pixel_format_ = get_ffmpeg_pixfmt( pf );
	el_width_ = dw;
	el_height_ = dh;
	el_fps_ = fps;

	el_switch_jpeg_ = switch_jpeg;

	lav_set_project( dw,dh, fps, pf );

	el_out_ = yuv_yuv_template( NULL,NULL,NULL, dw,dh, get_ffmpeg_pixfmt(pf) );

	char *maxFileSize = getenv( "VEEJAY_MAX_FILESIZE" );
	if( maxFileSize != NULL ) {
		uint64_t mfs = atol( maxFileSize );
		if( mfs > AVI_get_MAX_LEN() )
			mfs = AVI_get_MAX_LEN();
		if( mfs > 0 ) {
			AVI_set_MAX_LEN( mfs );
			veejay_msg(VEEJAY_MSG_INFO, "Changed maximum file size" );
		}
	}

	if( has_env_setting( "VEEJAY_RUN_MODE", "CLASSIC" ) )
	{
		require_same_resolution = 1;
	}

	if(should_enable_drop_frame_timecode(fps)) {
        setenv("MJPEG_DROP_FRAME_TIME_CODE", "1",1 );
		veejay_msg(VEEJAY_MSG_INFO,
		           "Fractional-rate timecode correction enabled (%.6f fps)",
		           fps);
    } else {
        setenv("MJPEG_DROP_FRAME_TIME_CODE", "0",1 );
		veejay_msg(VEEJAY_MSG_INFO, "Timecode is linear (frame accurate)");
    }

	veejay_msg(VEEJAY_MSG_DEBUG,"Initialized EDL, processing video in %d x %d", el_width_, el_height_ );
}

int	vj_el_is_dv(editlist *el)
{
#ifdef SUPPORT_READ_DV2
	return is_dv_resolution(el->video_width, el->video_height);
#else
	return 0;
#endif
}


#ifndef GREMLIN_GUARDIAN
#define GREMLIN_GUARDIAN (128*1024)-1
#endif

enum {
	VJ_MJPEG_DECODER_AUTO = 0,
	VJ_MJPEG_DECODER_NVJPEG,
	VJ_MJPEG_DECODER_SOFTWARE
};

static int vj_mjpeg_decoder_mode(void)
{
	const char *setting = getenv("VEEJAY_MJPEG_DECODER");

	if(!setting || !setting[0] || strcasecmp(setting, "auto") == 0)
		return VJ_MJPEG_DECODER_AUTO;
	if(strcasecmp(setting, "nvjpeg") == 0)
		return VJ_MJPEG_DECODER_NVJPEG;
	if(strcasecmp(setting, "software") == 0 ||
	   strcasecmp(setting, "legacy") == 0)
		return VJ_MJPEG_DECODER_SOFTWARE;

	veejay_msg(VEEJAY_MSG_WARNING,
	           "Unknown VEEJAY_MJPEG_DECODER value '%s'; using auto",
	           setting);
	return VJ_MJPEG_DECODER_AUTO;
}

static const char *vj_el_pixfmt_name(int pixfmt)
{
	switch(pixfmt) {
		case PIX_FMT_YUVJ422P: return "yuvj422p";
		case PIX_FMT_YUVA422P: return "yuva422p";
		case PIX_FMT_YUV422P: return "yuv422p";
		case PIX_FMT_YUVJ420P: return "yuvj420p";
		case PIX_FMT_YUV420P: return "yuv420p";
		case PIX_FMT_YUV444P: return "yuv444p";
		default: return "unknown";
	}
}

static int vj_nvjpeg_decoder_eligible(int source_width,
	                                  int source_height,
	                                  int output_pixfmt,
	                                  char *reason,
	                                  size_t reason_size)
{
	int output_width = el_width_ > 0 ? el_width_ : source_width;
	int output_height = el_height_ > 0 ? el_height_ : source_height;

	if(output_pixfmt != PIX_FMT_YUVJ422P &&
	   output_pixfmt != PIX_FMT_YUVA422P) {
		snprintf(reason, reason_size,
		         "internal format %s is not a supported planar 4:2:2 layout",
		         vj_el_pixfmt_name(output_pixfmt));
		return 0;
	}
	if(source_width != output_width || source_height != output_height) {
		snprintf(reason, reason_size,
		         "source geometry %dx%d requires scaling to %dx%d",
		         source_width, source_height, output_width, output_height);
		return 0;
	}
	if((source_width & 1) != 0) {
		snprintf(reason, reason_size,
		         "odd source width %d is incompatible with VeeJay 4:2:2 buffers",
		         source_width);
		return 0;
	}

	return 1;
}

static vj_decoder *_el_new_decoder( void *ctx, int id , int width, int height, float fps, int out_fmt, long max_frame_size)
{
   vj_decoder *d = (vj_decoder*) vj_calloc(sizeof(vj_decoder));
   if(!d) {
		return NULL;
   }
#ifdef SUPPORT_READ_DV2
	if( id == CODEC_ID_DVVIDEO )
		d->dv_decoder = vj_dv_decoder_init(1, width, height, out_fmt );
#endif	

	if( id == CODEC_ID_YUVLZO )
	{
		d->lzo_decoder = lzo_new( el_pixel_format_, el_width_, el_height_ , 1);
	}
	else if ( id == CODEC_ID_YUV422 || id == CODEC_ID_YUV420 || id == CODEC_ID_YUV420F || id == CODEC_ID_YUV422F || id == CODEC_ID_QOIY || id == CODEC_ID_ANYTHING ) {
		
	}
	else if( ctx )
        {
		d->codec = avhelper_get_codec(ctx);
		d->context = avhelper_get_codec_ctx(ctx);
		d->frame = avhelper_alloc_frame();
	}

	if(id == CODEC_ID_MJPEG && ctx) {
		int mode = vj_mjpeg_decoder_mode();
		d->nvjpeg_forced = (mode == VJ_MJPEG_DECODER_NVJPEG);

		if(mode == VJ_MJPEG_DECODER_SOFTWARE) {
			snprintf(d->nvjpeg_reason, sizeof(d->nvjpeg_reason),
			         "disabled by VEEJAY_MJPEG_DECODER=software");
		}
		else if(vj_nvjpeg_decoder_eligible(width,
		                                   height,
		                                   out_fmt,
		                                   d->nvjpeg_reason,
		                                   sizeof(d->nvjpeg_reason))) {
			d->nvjpeg_decoder = vj_nvjpeg_decoder_create(
				width,
				height,
				d->nvjpeg_reason,
				sizeof(d->nvjpeg_reason));
			d->nvjpeg_probe_pending = d->nvjpeg_decoder != NULL;
		}
	}

	
	size_t safe_max_frame_size = ( AV_INPUT_BUFFER_PADDING_SIZE  + max_frame_size + GREMLIN_GUARDIAN );
	
	veejay_msg(VEEJAY_MSG_DEBUG, "\tDecoder buffer:   %d bytes" , safe_max_frame_size);
	

	d->tmp_buffer = (uint8_t*) vj_malloc( sizeof(uint8_t) * safe_max_frame_size );
	if(!d->tmp_buffer) {
		_el_free_decoder(d);
		return NULL;
	}
	d->fmt = id;

	return d;
}

static int vj_el_decode_mjpeg(vj_decoder *decoder,
	                          void *software_context,
	                          const uint8_t *data,
	                          int data_size,
	                          uint8_t *dst[4],
	                          const char *source,
	                          vj_el_chroma requested_chroma,
	                          vj_el_chroma *actual_chroma)
{
	int request_444 = requested_chroma == VJ_EL_CHROMA_444 &&
		vj_nvjpeg_decoder_supports_444(decoder ? decoder->nvjpeg_decoder : NULL);
	vj_nvjpeg_output requested_output = request_444
		? VJ_NVJPEG_OUTPUT_444
		: VJ_NVJPEG_OUTPUT_422;
	vj_nvjpeg_output actual_output = VJ_NVJPEG_OUTPUT_422;

	if(actual_chroma)
		*actual_chroma = VJ_EL_CHROMA_422;
	if(!decoder || !data || data_size <= 0)
		return -1;

	if(vj_nvjpeg_decoder_is_active(decoder->nvjpeg_decoder)) {
		size_t dst_pitch[3] = {
			(size_t)el_width_,
			request_444
				? (size_t)el_width_
				: (size_t)((el_width_ + 1) / 2),
			request_444
				? (size_t)el_width_
				: (size_t)((el_width_ + 1) / 2)
		};
		int nvjpeg_result = vj_nvjpeg_decoder_decode(
			decoder->nvjpeg_decoder,
			data,
			(size_t)data_size,
			requested_output,
			&actual_output,
			dst,
			dst_pitch);

		if(nvjpeg_result == 1) {
			if(actual_chroma)
				*actual_chroma = actual_output == VJ_NVJPEG_OUTPUT_444
					? VJ_EL_CHROMA_444
					: VJ_EL_CHROMA_422;
			if(decoder->nvjpeg_probe_pending) {
				veejay_msg(VEEJAY_MSG_INFO,
				           "[VIDEO-DECODE] source='%s' mode=accelerated backend=nvjpeg engine=%s codec=mjpeg sourcefmt=%s outputfmt=%s conversion=%s range=full",
				           source ? source : "unknown",
				           vj_nvjpeg_decoder_engine(decoder->nvjpeg_decoder),
				           vj_nvjpeg_decoder_source_format(decoder->nvjpeg_decoder),
				           actual_output == VJ_NVJPEG_OUTPUT_444
				               ? "planar-yuv444"
				               : "planar-yuv422",
				           vj_nvjpeg_decoder_conversion(decoder->nvjpeg_decoder));
				decoder->nvjpeg_probe_pending = 0;
			}
			if(actual_output == VJ_NVJPEG_OUTPUT_444 &&
			   !decoder->nvjpeg_444_logged) {
				veejay_msg(VEEJAY_MSG_INFO,
				           "[VIDEO-DECODE] source='%s' backend=nvjpeg output=planar-yuv444 upsample=gpu/%s",
				           source ? source : "unknown",
				           vj_nvjpeg_decoder_upsampler(decoder->nvjpeg_decoder));
				decoder->nvjpeg_444_logged = 1;
			}
			else if(requested_chroma == VJ_EL_CHROMA_444 &&
			        actual_output == VJ_NVJPEG_OUTPUT_422 &&
			        !decoder->nvjpeg_444_fallback_logged) {
				veejay_msg(VEEJAY_MSG_INFO,
				           "[VIDEO-DECODE] source='%s' backend=nvjpeg output=planar-yuv422 gpu444=unavailable; FX chroma supersampling remains on CPU",
				           source ? source : "unknown");
				decoder->nvjpeg_444_fallback_logged = 1;
			}
			return 1;
		}

		char failure_reason[256];
		snprintf(failure_reason, sizeof(failure_reason), "%s",
		         vj_nvjpeg_decoder_last_error(decoder->nvjpeg_decoder));
		veejay_msg(VEEJAY_MSG_WARNING,
		           "[VIDEO-DECODE] source='%s' backend=nvjpeg retired reason='%s'; retrying with software backend=legacy",
		           source ? source : "unknown",
		           failure_reason);
		vj_nvjpeg_decoder_retire(decoder->nvjpeg_decoder);
	}

	if(!software_context)
		return -1;

	int result = avhelper_decode_video_direct(software_context,
	                                          (uint8_t *)data,
	                                          data_size,
	                                          dst,
	                                          el_pixel_format_,
	                                          el_width_,
	                                          el_height_);
	avhelper_decode_finish(software_context);
	if(actual_chroma)
		*actual_chroma = VJ_EL_CHROMA_422;
	return result;
}

int	get_ffmpeg_pixfmt( int pf )
{
	switch( pf )
	{
		case FMT_420:
			return PIX_FMT_YUV420P;
		case FMT_420F:
			return PIX_FMT_YUVJ420P;
		case FMT_422:
			return PIX_FMT_YUV422P;
		case FMT_422F:
			return PIX_FMT_YUVJ422P;
		case 4:
			return PIX_FMT_YUV444P;
		
	}
	return PIX_FMT_YUV422P;
}

static long get_max_frame_size( lav_file_t *fd )
{
	long total_frames = lav_video_frames( fd );
	long i;
	long res = 0;
	for (i = 0; i < total_frames; i++)
	{
		long tmp = lav_frame_size( fd, i );
		if( tmp > res ) {
			res = tmp;
		} 
   	}
	return (((res)+8)&~8);
}

static int open_video_file_legacy(char *filename, editlist * el, int preserve_pathname, int deinter, int force, char override_norm, int out_format, int width, int height )
{
	int i, n, nerr;
	int chroma=0;
	int _fc;
	int decoder_id = 0;
	const char *compr_type;
	char *realname = NULL;	

 	if( filename == NULL ) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No files give to open");
		return -1;
	}

	if (preserve_pathname)
		realname = vj_strdup(filename);
	else
		realname = canonicalize_file_name( filename );

	if(realname == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot get full path of '%s'", filename);
		return -1;
	}

	for (i = 0; i < el->num_video_files; i++)
	{
		if (strncmp(realname, el->video_file_list[i], strlen( el->video_file_list[i])) == 0)
		{
		    veejay_msg(VEEJAY_MSG_ERROR, "File %s is already in editlist", realname);
		    if(realname) free(realname);
		    return -1;
		}
	}

	if (el->num_video_files >= MAX_EDIT_LIST_FILES)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Maximum number of video files exceeded\n");
        if(realname) free(realname);
		return -1;
    }

    if (el->num_video_files >= 1)
		chroma = el->MJPG_chroma;
      
    n = el->num_video_files;
	
	int pixfmt = -1;

	size_t suggested_map_len = mmap_file_suggest_size(filename, NULL);
	lav_file_t *elfd = lav_open_input_file(filename,suggested_map_len );
	el->lav_fd[n] = NULL;
	if (elfd == NULL)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Unable to load video '%s'", realname);
	        veejay_msg(VEEJAY_MSG_ERROR,"\t%s",lav_strerror());
	 	if(realname) free(realname);
		return -1;
	}

#ifdef USE_GDK_PIXBUF
	if( !elfd->picture )
#endif
		el->ctx[n] = avhelper_get_decoder( filename, out_format, width, height );

	if( el->ctx[n] == NULL ) {
		pixfmt = test_video_frame( el, n, elfd, el_pixel_format_ );
		if( pixfmt == -1 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to determine format of %s", filename );
			lav_close(elfd);
			if(realname) free(realname);
			return -1;
		}
		el->pixfmt[n] = pixfmt;
	}
	else {
		el_decoder_t *x = (el_decoder_t*) el->ctx[n];
		el->pixfmt[n] = x->pixfmt;
	}


	if(lav_video_frames(elfd) < 1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cowardly refusing to load empty video files");
		if(realname) free(realname);
		lav_close(elfd);
		if( el->ctx[n] ) avhelper_close_decoder( el->ctx[n] );
		return -1;
	}

	
	_fc = lav_video_MJPG_chroma(elfd);

	if( !(_fc == CHROMA422 || _fc == CHROMA420 || _fc == CHROMA444 || _fc == CHROMAUNKNOWN || _fc == CHROMA411 || _fc == CHROMA422F || _fc == CHROMA420F))
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Input file %s is not in a valid format (%d)",filename,_fc);
	   	if(realname) free(realname);
		lav_close( elfd );
		if( el->ctx[n] ) avhelper_close_decoder( el->ctx[n] );
		return -1;

	}

	if(chroma == CHROMAUNKNOWN)
	{ /* set chroma */
  	  el->MJPG_chroma = _fc;
	  chroma = _fc; //FIXME
	}

	el->lav_fd[n] = elfd;
    	el->num_frames[n] = lav_video_frames(el->lav_fd[n]);
    	el->video_file_list[n] = vj_strndup(realname, strlen(realname));
	el->media_id[n] = media_source_hash(realname);
	
	    /* Debug Output */
	if(n == 0 )
	{
	    veejay_msg(VEEJAY_MSG_DEBUG,"\tFull name:       %s", filename, realname);
	    veejay_msg(VEEJAY_MSG_DEBUG,"\tFrames:          %ld", lav_video_frames(el->lav_fd[n]));
	    veejay_msg(VEEJAY_MSG_DEBUG,"\tWidth:           %d", lav_video_width(el->lav_fd[n]));
	    veejay_msg(VEEJAY_MSG_DEBUG,"\tHeight:          %d", lav_video_height(el->lav_fd[n]));
    	
		if( deinter == 1 && (lav_video_interlacing(el->lav_fd[n]) != LAV_NOT_INTERLACED))
			el->auto_deinter = 1;

		veejay_msg(VEEJAY_MSG_DEBUG,"\tFrames/sec:       %f", lav_frame_rate(el->lav_fd[n]));
		veejay_msg(VEEJAY_MSG_DEBUG,"\tSampling format:  %s", _chroma_str[ lav_video_MJPG_chroma(el->lav_fd[n])].name);
		veejay_msg(VEEJAY_MSG_DEBUG,"\tVideo compressor: %s",lav_video_compressor(el->lav_fd[n]));
		veejay_msg(VEEJAY_MSG_DEBUG,"\tAudio samps:      %ld", lav_audio_clips(el->lav_fd[n]));
		veejay_msg(VEEJAY_MSG_DEBUG,"\tAudio chans:      %d", lav_audio_channels(el->lav_fd[n]));
		veejay_msg(VEEJAY_MSG_DEBUG,"\tAudio bits:       %d", lav_audio_bits(el->lav_fd[n]));
		veejay_msg(VEEJAY_MSG_DEBUG,"\tAudio rate:       %ld", lav_audio_rate(el->lav_fd[n]));
	}
	else
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "\tFull name	%s",realname);	
		veejay_msg(VEEJAY_MSG_DEBUG, "\tFrames	        %d", lav_video_frames(el->lav_fd[n]));
		veejay_msg(VEEJAY_MSG_DEBUG, "\tDecodes into    %s", _chroma_str[ lav_video_MJPG_chroma( el->lav_fd[n]) ]);
	}

    nerr = 0;
    if (n == 0) {
	/* First file determines parameters */
		el->video_height = lav_video_height(el->lav_fd[n]);
		el->video_width = lav_video_width(el->lav_fd[n]);
		el->video_inter = lav_video_interlacing(el->lav_fd[n]);
		el->video_fps = lav_frame_rate(el->lav_fd[n]);
	
		lav_video_clipaspect(el->lav_fd[n],
				       &el->video_sar_width,
				       &el->video_sar_height);

		if (!el->video_norm)
		{
			el->video_norm = vj_el_get_default_norm( el->video_fps );
		}

		if (!el->video_norm)
		{
			if(override_norm == 'p' || override_norm == 'n' || override_norm == 's')
				el->video_norm = override_norm;
		}

		if( !el->video_norm ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to detect video norm, using PAL" );
			el->video_norm = 'p';
		}
	
		if(!el->is_empty)
		{
			el->audio_chans = lav_audio_channels(el->lav_fd[n]);
			if (el->audio_chans > 2) {
		  	  veejay_msg(VEEJAY_MSG_ERROR, "File has %d audio channels - cant play that!",
			              el->audio_chans);
			   nerr++;
			}
	
			el->has_audio = (el->audio_chans == 0 ? 0: 1);
			el->audio_bits = lav_audio_bits(el->lav_fd[n]);
			el->audio_rate = lav_audio_rate(el->lav_fd[n]);
			el->audio_bps = (el->audio_bits * el->audio_chans + 7) / 8;
		}
		else
		{
			if(lav_audio_channels(el->lav_fd[n]) != el->audio_chans ||
			   lav_audio_rate(el->lav_fd[n]) != el->audio_rate ||
			   lav_audio_bits(el->lav_fd[n]) != el->audio_bits ) {
				veejay_msg(VEEJAY_MSG_ERROR,"Different audio properties detected - cant play that!");
				veejay_msg(VEEJAY_MSG_DEBUG,"Audio rate %ld, source is %ld", el->audio_rate, lav_audio_rate(el->lav_fd[n]));
				veejay_msg(VEEJAY_MSG_DEBUG,"Audio bits %d, source is %d", el->audio_bits, lav_audio_bits(el->lav_fd[n]));
				veejay_msg(VEEJAY_MSG_DEBUG,"Audio channels %d, source is %d", el->audio_chans, lav_audio_channels(el->lav_fd[n]) );
				nerr++;
			} else
				el->has_audio = 1;
		}
   	 } else {
		/* All files after first have to match the paramters of the first */
	
		if (el->video_height != lav_video_height(el->lav_fd[n]) ||
		    el->video_width != lav_video_width(el->lav_fd[n])) {
		    veejay_msg( (require_same_resolution ? VEEJAY_MSG_ERROR: VEEJAY_MSG_WARNING),
				"Geometry %dx%d does not match %dx%d (performance penalty).",
				lav_video_width(el->lav_fd[n]),
				lav_video_height(el->lav_fd[n]), el->video_width,
				el->video_height);
		    if( require_same_resolution )
		    	nerr++;
		}

	/* give a warning on different fps instead of error , this is better 
	   for live performances */
	if (fabs(el->video_fps - lav_frame_rate(el->lav_fd[n])) >
	    0.0000001) {
	    veejay_msg(VEEJAY_MSG_WARNING, "FPS is %3.2f , but playing at %3.2f", filename,
		       lav_frame_rate(el->lav_fd[n]), el->video_fps);
	}
	/* If first file has no audio, we don't care about audio */

	if (el->has_audio) {
	    if( el->audio_rate < 44000 )
	    {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot play %d Hz audio. Use at least 44100 Hz or start with -a0", el->audio_rate);
		nerr++;
	    }
        else {
		    if (el->audio_chans != lav_audio_channels(el->lav_fd[n]) ||
			el->audio_bits != lav_audio_bits(el->lav_fd[n]) ||
			el->audio_rate != lav_audio_rate(el->lav_fd[n])) {

			int err_level = VEEJAY_MSG_ERROR;
			if( lav_audio_rate(el->lav_fd[n]) == 0 )
				err_level = VEEJAY_MSG_WARNING;

			veejay_msg(err_level,"Mismatched audio properties: %d channels , %d bit %ld Hz",
			   lav_audio_channels(el->lav_fd[n]),
			   lav_audio_bits(el->lav_fd[n]),
			   lav_audio_rate(el->lav_fd[n]) );
			   if( err_level == VEEJAY_MSG_ERROR )
				   nerr++;
			}
        }
	}


	if (nerr) {
	    veejay_msg(VEEJAY_MSG_ERROR, "Too many errors in %s, refusing to load", filename);
	    if(el->lav_fd[n]) 
		lav_close( el->lav_fd[n] );
	    el->lav_fd[n] = NULL;
	    if(realname) free(realname);
	    if(el->video_file_list[n]) 
		free(el->video_file_list[n]);
	    el->video_file_list[n] = NULL;
		if( el->ctx[n] ) avhelper_close_decoder( el->ctx[n] );
	    return -1;
        }
    }

	compr_type = (const char*) lav_video_compressor(el->lav_fd[n]);
	
	if(compr_type==NULL)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to read fourcc from %s", filename);
		if(el->lav_fd[n])
		 lav_close( el->lav_fd[n] );
		el->lav_fd[n] = NULL;
		if(realname) free(realname);
		if(el->video_file_list[n]) 
			free(el->video_file_list[n]);
		el->video_file_list[n] = NULL;
		if( el->ctx[n] ) avhelper_close_decoder( el->ctx[n] );

		return -1;
	}

	set_fourcc(el->lav_fd[n], compr_type);

	if( el->decoders[n] == NULL ) {
		
		decoder_id = el->lav_fd[n]->codec_id;

		long max_frame_size = (el->video_width * el->video_height * 4);
		if( decoder_id < 900 && decoder_id != AV_CODEC_ID_HUFFYUV ) 
			max_frame_size = get_max_frame_size( el->lav_fd[n] );

		el->decoders[n] = 
			_el_new_decoder( el->ctx[n], decoder_id, el->video_width, el->video_height, el->video_fps, el_pixel_format_, max_frame_size );
		if( el->decoders[n] == NULL ) {
			veejay_msg(VEEJAY_MSG_ERROR,"Unsupported video compression type: %s", compr_type );
			if( el->lav_fd[n] ) 
				lav_close( el->lav_fd[n] );
			el->lav_fd[n] = NULL;
			if( realname ) free(realname );
			if( el->video_file_list[n]) 
				free(el->video_file_list[n]);
			el->video_file_list[n] = NULL;
			if( el->ctx[n] ) avhelper_close_decoder( el->ctx[n] );

			return -1;
		}
	}

	if(realname)
		free(realname);

	if(el->is_empty)
	{
		el->video_frames = el->num_frames[0];
		el->video_frames -= DUMMY_FRAMES;
	}

	el->is_empty = 0;	
	el->has_video = 1;
	el->num_video_files ++;
    
	el->source_hash = editlist_source_hash(el);

	if( el_width_ == 0 && el_height_ == 0 ) {
		el_width_ = el->video_width;
		el_height_ = el->video_height;
		el_fps_ = el->video_fps;

		veejay_msg(VEEJAY_MSG_WARNING, "Initialized video project settings from first file (%s)" , filename );
	}

	return n;
}

static void configure_ffmpeg_audio(editlist *el,
                                   vj_ffmpeg_input *input,
                                   const vj_ffmpeg_input_info *info,
                                   int file_index,
                                   const char *filename)
{
    if(file_index == 0) {
        el->has_audio = 0;
        el->audio_rate = 0;
        el->audio_chans = 0;
        el->audio_bits = 0;
        el->audio_bps = 0;

        if(!info->has_audio)
            return;

        int rate = info->audio_rate >= 44000 ? info->audio_rate : 48000;
        int channels = info->audio_channels == 1 ? 1 : 2;
        if(vj_ffmpeg_input_configure_audio(input, rate, channels)) {
            el->has_audio = 1;
            el->audio_rate = rate;
            el->audio_chans = channels;
            el->audio_bits = 16;
            el->audio_bps = channels * 2;
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[FFMPEG-AUDIO] EDL PCM established source='%s' rate=%ld channels=%d bits=16",
                       filename,
                       el->audio_rate,
                       el->audio_chans);
        }
        else {
            veejay_msg(VEEJAY_MSG_WARNING,
                       "Unable to configure audio for generic FFmpeg input '%s'; video remains available",
                       filename);
        }
        return;
    }

    if(!el->has_audio)
        return;

    if(!info->has_audio) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-AUDIO] no audio stream source='%s'; this EDL segment will be silent",
                   filename);
        return;
    }

    if(el->audio_bits != 16 || el->audio_rate <= 0 ||
       el->audio_chans < 1 || el->audio_chans > 2) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "Generic FFmpeg audio in '%s' cannot match the current EDL PCM format; this segment will be silent",
                   filename);
        return;
    }

    if(!vj_ffmpeg_input_configure_audio(input,
                                        (int)el->audio_rate,
                                        el->audio_chans)) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "Unable to normalize generic FFmpeg audio in '%s'; this EDL segment will be silent",
                   filename);
        return;
    }

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-AUDIO] normalized source='%s' %dHz/%dch -> %ldHz/%dch s16le",
               filename,
               info->audio_rate,
               info->audio_channels,
               el->audio_rate,
               el->audio_chans);
}

int vj_el_retarget_audio(editlist *el,
                         long sample_rate,
                         int channels,
                         int bits)
{
    if(!el || !el->has_audio || sample_rate <= 0 ||
       channels < 1 || channels > 2)
        return 0;

    int has_ffmpeg = 0;
    for(int i = 0; i < el->num_video_files; i++)
        if(el->backend[i] == VJ_EL_BACKEND_FFMPEG)
            has_ffmpeg = 1;

    if(!has_ffmpeg)
        return 0;
    if(bits != 16)
        return -1;

    for(int i = 0; i < el->num_video_files; i++) {
        if(el->backend[i] == VJ_EL_BACKEND_FFMPEG)
            continue;
        if(!el->lav_fd[i] || lav_audio_channels(el->lav_fd[i]) <= 0)
            continue;
        if(lav_audio_rate(el->lav_fd[i]) != sample_rate ||
           lav_audio_channels(el->lav_fd[i]) != channels ||
           lav_audio_bits(el->lav_fd[i]) != bits)
            return -1;
    }

    long old_rate = el->audio_rate;
    int old_channels = el->audio_chans;
    int old_bits = el->audio_bits;

    for(int i = 0; i < el->num_video_files; i++) {
        if(el->backend[i] != VJ_EL_BACKEND_FFMPEG || !el->ffmpeg_input[i])
            continue;

        const vj_ffmpeg_input_info *info =
            vj_ffmpeg_input_get_info(el->ffmpeg_input[i]);
        if(!info || !info->has_audio)
            continue;

        if(!vj_ffmpeg_input_configure_audio(el->ffmpeg_input[i],
                                            (int)sample_rate,
                                            channels)) {
            if(old_rate > 0 && old_channels > 0 && old_channels <= 2 &&
               old_bits == 16) {
                for(int j = 0; j < i; j++) {
                    if(el->backend[j] != VJ_EL_BACKEND_FFMPEG ||
                       !el->ffmpeg_input[j])
                        continue;
                    const vj_ffmpeg_input_info *old_info =
                        vj_ffmpeg_input_get_info(el->ffmpeg_input[j]);
                    if(old_info && old_info->has_audio)
                        vj_ffmpeg_input_configure_audio(el->ffmpeg_input[j],
                                                        (int)old_rate,
                                                        old_channels);
                }
            }
            return -1;
        }
    }

    el->audio_rate = sample_rate;
    el->audio_chans = channels;
    el->audio_bits = 16;
    el->audio_bps = channels * 2;
    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-AUDIO] retargeted EDL PCM to %ldHz/%dch/s16le",
               el->audio_rate,
               el->audio_chans);
    return 1;
}

static int open_video_file_ffmpeg(char *filename, editlist *el, int preserve_pathname,
                                  int deinter, char override_norm, int out_format,
                                  int width, int height)
{
    char *realname = preserve_pathname ? vj_strdup(filename) : canonicalize_file_name(filename);
    if(!realname)
        return -1;

    for(int i = 0; i < el->num_video_files; i++) {
        if(el->video_file_list[i] && strcmp(realname, el->video_file_list[i]) == 0) {
            free(realname);
            return -1;
        }
    }

    if(el->num_video_files >= MAX_EDIT_LIST_FILES) {
        free(realname);
        return -1;
    }

    int n = el->num_video_files;
    el->lav_fd[n] = NULL;
    el->ctx[n] = NULL;
    el->decoders[n] = NULL;
    el->ffmpeg_input[n] = NULL;
    el->backend[n] = VJ_EL_BACKEND_LEGACY;

    vj_ffmpeg_input *input = vj_ffmpeg_input_open(filename, out_format, width, height);
    if(!input) {
        free(realname);
        return -1;
    }

    const vj_ffmpeg_input_info *info = vj_ffmpeg_input_get_info(input);
    if(!info || info->frame_count <= 0 || info->frame_count > LONG_MAX ||
       info->width <= 0 || info->height <= 0) {
        vj_ffmpeg_input_close(input);
        free(realname);
        return -1;
    }

    if(n == 0) {
        el->video_width = info->width;
        el->video_height = info->height;
        el->video_inter = info->interlaced ? LAV_INTER_TOP_FIRST : LAV_NOT_INTERLACED;
        el->video_fps = info->fps > 0.0 ? (float)info->fps : el_fps_;
        el->video_sar_width = info->sar_num;
        el->video_sar_height = info->sar_den;
        if(!el->video_norm)
            el->video_norm = vj_el_get_default_norm(el->video_fps);
        if(!el->video_norm && (override_norm == 'p' || override_norm == 'n' || override_norm == 's'))
            el->video_norm = override_norm;
        if(!el->video_norm)
            el->video_norm = 'p';
        if(deinter && info->interlaced)
            el->auto_deinter = 1;
    }
    else {
        if(el->video_width != info->width || el->video_height != info->height) {
            veejay_msg(require_same_resolution ? VEEJAY_MSG_ERROR : VEEJAY_MSG_WARNING,
                       "Geometry %dx%d does not match %dx%d (performance penalty).",
                       info->width, info->height, el->video_width, el->video_height);
            if(require_same_resolution) {
                vj_ffmpeg_input_close(input);
                free(realname);
                return -1;
            }
        }
        if(info->fps > 0.0 && fabs(el->video_fps - info->fps) > 0.0000001)
            veejay_msg(VEEJAY_MSG_WARNING,
                       "FPS is %3.2f, but playing at %3.2f",
                       info->fps, el->video_fps);
    }

    configure_ffmpeg_audio(el, input, info, n, realname);

    el->video_file_list[n] = vj_strdup(realname);
    if(!el->video_file_list[n]) {
        vj_ffmpeg_input_close(input);
        free(realname);
        return -1;
    }

    el->ffmpeg_input[n] = input;
    el->backend[n] = VJ_EL_BACKEND_FFMPEG;
    el->media_id[n] = media_source_hash(realname);
    el->num_frames[n] = (long)info->frame_count;
    el->pixfmt[n] = out_format;
    int target_width = width > 0 ? width : info->width;
    int target_height = height > 0 ? height : info->height;
    if(target_width > 0 && target_height > 0 &&
       (long)target_width <= LONG_MAX / (long)target_height / 4L)
        el->max_frame_sizes[n] = (long)target_width * (long)target_height * 4L;
    else
        el->max_frame_sizes[n] = LONG_MAX;

    el->is_empty = 0;
    el->has_video = 1;
    el->num_video_files++;
    el->source_hash = editlist_source_hash(el);

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-IN] generic input accepted file=%s slot=%d frames=%ld",
               realname, n, el->num_frames[n]);

    free(realname);
    return n;
}

int open_video_file(char *filename, editlist *el, int preserve_pathname, int deinter,
                    int force, char override_norm, int out_format, int width, int height)
{
    int generic_tried = 0;
    if(vj_ffmpeg_input_prefers_generic(filename)) {
        generic_tried = 1;
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-IN] non-intra media '%s'; trying generic FFmpeg input first",
                   filename);
        int ffmpeg_n = open_video_file_ffmpeg(filename, el, preserve_pathname, deinter,
                                              override_norm, out_format, width, height);
        if(ffmpeg_n >= 0)
            return ffmpeg_n;
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-IN] preferred generic input rejected '%s'; trying legacy input",
                   filename);
    }

    int n = open_video_file_legacy(filename, el, preserve_pathname, deinter, force,
                                   override_norm, out_format, width, height);
    if(n >= 0) {
        el->backend[n] = VJ_EL_BACKEND_LEGACY;
        const char *codec = lav_video_compressor(el->lav_fd[n]);
        const char *source = el->video_file_list[n]
            ? el->video_file_list[n]
            : filename;
        int codec_id = lav_video_compressor_type(el->lav_fd[n]);
        vj_decoder *decoder = (vj_decoder *)el->decoders[n];

        if(codec_id == CODEC_ID_MJPEG && decoder &&
           vj_nvjpeg_decoder_is_active(decoder->nvjpeg_decoder)) {
            veejay_msg(VEEJAY_MSG_INFO,
                       "[VIDEO-DECODE] source='%s' mode=probing backend=nvjpeg engine=%s codec=mjpeg containerfmt=%s validation=jpeg-header gpu444=%s upsampler=%s",
                       source,
                       vj_nvjpeg_decoder_engine(decoder->nvjpeg_decoder),
                       vj_el_pixfmt_name(el->pixfmt[n]),
                       vj_nvjpeg_decoder_supports_444(decoder->nvjpeg_decoder)
                           ? "available"
                           : "unavailable",
                       vj_nvjpeg_decoder_upsampler(decoder->nvjpeg_decoder));
        }
        else {
            const char *reason = codec_id == CODEC_ID_MJPEG && decoder &&
                                 decoder->nvjpeg_reason[0]
                ? decoder->nvjpeg_reason
                : "nvJPEG is not eligible for this stream";

            if(codec_id == CODEC_ID_MJPEG && decoder && decoder->nvjpeg_forced)
                veejay_msg(VEEJAY_MSG_WARNING,
                           "[VIDEO-DECODE] requested backend=nvjpeg unavailable source='%s' reason='%s'; using software",
                           source,
                           reason);

            if(codec_id == CODEC_ID_MJPEG)
                veejay_msg(VEEJAY_MSG_INFO,
                           "[VIDEO-DECODE] source='%s' mode=software backend=legacy codec=mjpeg reason='%s'",
                           source,
                           reason);
            else
                veejay_msg(VEEJAY_MSG_INFO,
                           "[VIDEO-DECODE] source='%s' mode=software backend=legacy codec=%s",
                           source,
                           codec ? codec : "unknown");
        }
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-IN] legacy input retained for '%s' slot=%d",
                   filename, n);
        return n;
    }

    if(generic_tried) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-IN] generic and legacy inputs rejected '%s'",
                   filename);
        return -1;
    }

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-IN] legacy input rejected '%s'; trying generic FFmpeg fallback",
               filename);

    n = open_video_file_ffmpeg(filename, el, preserve_pathname, deinter,
                               override_norm, out_format, width, height);
    if(n < 0)
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-IN] generic FFmpeg fallback also rejected '%s'",
                   filename);
    return n;
}

static int	vj_el_dummy_frame( uint8_t *dst[3], editlist *el ,int pix_fmt)
{
	const int uv_len = (el->video_width * el->video_height) / ( ( (pix_fmt==FMT_422||pix_fmt==FMT_422F) ? 2 : 4));
	const int len = el->video_width * el->video_height;
	const uint8_t black = (el_switch_jpeg_ || pix_fmt == FMT_422F ) ? 0 : 16;
	veejay_memset( dst[0], black, len );
	veejay_memset( dst[1],128, uv_len );
	veejay_memset( dst[2],128, uv_len );
	return 1;
}

int vj_el_get_file_fourcc(editlist *el, int num, char *fourcc)
{
    if (!fourcc)
        return 0;

    if (num < 0 || num >= el->num_video_files)
        return 0;

    if(el->backend[num] == VJ_EL_BACKEND_FFMPEG) {
        const vj_ffmpeg_input_info *info = vj_ffmpeg_input_get_info(el->ffmpeg_input[num]);
        if(!info)
            return 0;
        memcpy(fourcc, info->fourcc, 5);
        return 1;
    }

    const char *compr = lav_video_compressor(el->lav_fd[num]);
    if (!compr)
        return 0;

    fourcc[0] = '0';
    fourcc[1] = '0';
    fourcc[2] = '0';
    fourcc[3] = '0';
    fourcc[4] = '\0';

    for (int i = 0; i < 4 && compr[i]; i++) {
        unsigned char c = (unsigned char)compr[i];
        fourcc[i] = (c >= 32 && c <= 126) ? (char)c : '?';
    }

    return 1;
}

int	vj_el_bogus_length( editlist *el, long nframe )
{
	uint64_t n = 0;
	if(! el)
		return 0;

	if( !el->has_video || el->is_empty )
		return 0;

	if( nframe < 0 )
		nframe = 0;
	else if (nframe > el->total_frames )
		nframe = el->total_frames;

	n = el->frame_list[nframe];
	if(el->backend[N_EL_FILE(n)] == VJ_EL_BACKEND_FFMPEG)
		return 0;

	return lav_bogus_video_length( el->lav_fd[ N_EL_FILE(n) ] );
}

int	vj_el_set_bogus_length( editlist *el, long nframe, int len )
{
	uint64_t n = 0;
	
	if( len <= 0 )
		return 0;	

	if( !el->has_video || el->is_empty )
		return 0;
	if (nframe < 0)
		nframe = 0;

	if (nframe > el->total_frames)
		nframe = el->total_frames;

	n = el->frame_list[nframe];
	if(el->backend[N_EL_FILE(n)] == VJ_EL_BACKEND_FFMPEG)
		return 0;

	if( !lav_bogus_video_length( el->lav_fd[N_EL_FILE(n)] ) )
		return 0;

	lav_bogus_set_length( el->lav_fd[N_EL_FILE(n)], len );
	
	return 1;
}

int	vj_el_get_video_frame1(editlist *el, long nframe, uint8_t *dst[4])
{
	if( el->has_video == 0 || el->is_empty )
	{
		vj_el_dummy_frame( dst, el, el->pixel_format );
		return 2;
	}

	int res = 0;
   	uint64_t n;
	
	if (nframe < 0) {
	    veejay_msg(VEEJAY_MSG_DEBUG, "Oops, veejay requested frame %d < 0 ",nframe);
        nframe = 0;
    }
	if (nframe > el->total_frames) {
	    veejay_msg(VEEJAY_MSG_DEBUG, "Oops, veejay requested frame %d > %ld ", nframe, el->total_frames);
        nframe = el->total_frames;
    }

	n = el->frame_list[nframe];;

	if(el->backend[N_EL_FILE(n)] == VJ_EL_BACKEND_FFMPEG)
		return vj_ffmpeg_input_get_frame(el->ffmpeg_input[N_EL_FILE(n)],
		                                 (int64_t)N_EL_FRAME(n), dst,
		                                 0, NULL, NULL);

	int decoder_id = lav_video_compressor_type( el->lav_fd[N_EL_FILE(n)] );


	res = lav_set_video_position(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n));
	if (res < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Error setting video position: %s",
				lav_strerror());
		return -1;
	}


	if( decoder_id == 0xffff )
	{
		VJFrame *srci  = lav_get_frame_ptr( el->lav_fd[ N_EL_FILE(n) ] );
		if( srci == NULL )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Error decoding Image %ld",
				N_EL_FRAME(n));
			return -1;
		}
		int strides[4] = { el_out_->len, el_out_->uv_len, el_out_->uv_len,0 };
		vj_frame_copy( srci->data, dst, strides );
                return 1;     
	}

	vj_decoder *d = (vj_decoder*) el->decoders[ N_EL_FILE(n) ];
	if(lav_filetype( el->lav_fd[N_EL_FILE(n)] ) != 'x')
	{
		res = lav_read_frame(el->lav_fd[N_EL_FILE(n)], d->tmp_buffer);
	}

	uint8_t *data = d->tmp_buffer;
	uint8_t *in[3] = { NULL,NULL,NULL };
	int strides[4] = { el_out_->len, el_out_->uv_len, el_out_->uv_len ,0};
	uint8_t *dataplanes[4] = { data , data + el_out_->len, data + el_out_->len + el_out_->uv_len,0 };
	switch( decoder_id ) //FIXME: use swscaler ?
	{
		case CODEC_ID_YUV420:
			vj_frame_copy1( data,dst[0], el_out_->len );
			in[0] = data; 
			in[1] = data+el_out_->len; 
			in[2] = data+el_out_->len + (el_out_->len/4);
			if( el_pixel_format_ == PIX_FMT_YUVJ422P ) {
				yuv_scale_pixels_from_ycbcr( in[0],16.0f,235.0f, el_out_->len );
				yuv_scale_pixels_from_ycbcr( in[1],16.0f,240.0f, el_out_->len/4); 
			}
			yuv420to422planar( in , dst, el->video_width,el->video_height );
			return 1;
			break;	
		case CODEC_ID_YUV420F:
			vj_frame_copy1( data, dst[0], el_out_->len);
			in[0] = data;
			in[1] = data + el_out_->len;
			in[2] = data + el_out_->len+(el_out_->len/4);
			if( el_pixel_format_ == PIX_FMT_YUV422P ) {
				yuv_scale_pixels_from_y( dst[0], el_out_->len );
				yuv_scale_pixels_from_uv( dst[1], el_out_->len/4);
			}
			yuv420to422planar( in , dst, el->video_width,el->video_height );
			return 1;
			break;
		case CODEC_ID_YUV422:
			vj_frame_copy( dataplanes,dst,strides );
			if( el_pixel_format_ == PIX_FMT_YUVJ422P ) {
				yuv_scale_pixels_from_ycbcr( dst[0],16.0f,235.0f, el_out_->len );
				yuv_scale_pixels_from_ycbcr( dst[1],16.0f,240.0f, el_out_->len/2);
			}	
			return 1;
			break;
		case CODEC_ID_YUV422F:
			vj_frame_copy( dataplanes, dst, strides );
			if( el_pixel_format_ == PIX_FMT_YUV422P ) {
				yuv_scale_pixels_from_y( dst[0], el_out_->len );
				yuv_scale_pixels_from_uv( dst[1], el_out_->len/2);
			}
			return 1;
			break;
		case CODEC_ID_YUVLZO:
			return lzo_decompress_el( d->lzo_decoder, data,res, dst, el_width_, el_height_, el_pixel_format_);
			break;
		case CODEC_ID_QOIY:
			{
				qoi_desc qd;
				qd.channels = 1;
				qd.colorspace = QOI_LINEAR;
				qd.height = el_height_;
				qd.width = el_width_;
		    	qoi_decode( data,res, &qd, 1, dst, el_out_->len );
			
				return 1;
			}
			break;		
		default:
			{
			int ret;
			if(decoder_id == CODEC_ID_MJPEG)
				ret = vj_el_decode_mjpeg(
					d,
					el->ctx[N_EL_FILE(n)],
					data,
					res,
					dst,
					el->video_file_list[N_EL_FILE(n)],
					VJ_EL_CHROMA_422,
					NULL);
			else {
				ret = avhelper_decode_video_direct( el->ctx[ N_EL_FILE(n) ], data, res, dst, el_pixel_format_,el_width_,el_height_ );
				avhelper_decode_finish( el->ctx[ N_EL_FILE(n)] );
			}

			return ret;
			}
			break;
	}

	return 0;  
}


int vj_el_get_video_frame_ex(editlist *el,
                             long nframe,
                             uint8_t *dst[4],
                             vj_el_chroma requested_chroma,
                             vj_el_chroma *actual_chroma)
{
    vj_el_chroma decoded_chroma = VJ_EL_CHROMA_422;

    if(actual_chroma)
        *actual_chroma = VJ_EL_CHROMA_422;
    if(requested_chroma != VJ_EL_CHROMA_422 &&
       requested_chroma != VJ_EL_CHROMA_444)
        requested_chroma = VJ_EL_CHROMA_422;

    if (el->has_video == 0 || el->is_empty)
    {
        vj_el_dummy_frame(dst, el, el->pixel_format);
        return 2;
    }

    if (nframe < 0) {
        veejay_msg(VEEJAY_MSG_DEBUG, "Oops, veejay requested frame %d < 0 ", nframe);
        nframe = 0;
    }
    if (nframe > el->total_frames) {
        veejay_msg(VEEJAY_MSG_DEBUG, "Oops, veejay requested frame %d > %ld ", nframe, el->total_frames);
        nframe = el->total_frames;
    }

    int res = 0;
    uint64_t n = el->frame_list[nframe];
    int file_index = (int)N_EL_FILE(n);
    global_raw_frame_cache_t *cache = get_global_cache();

    if(el->backend[file_index] == VJ_EL_BACKEND_FFMPEG) {
        uint64_t source_frame = N_EL_FRAME(n);
        uint64_t media_id = el->media_id[file_index];
        el_cache_decision_t decision = el_cache_prepare_request(cache,
                                                                EL_CACHE_KEY_MEDIA,
                                                                media_id,
                                                                source_frame,
                                                                el->num_frames[file_index]);

        int cache_hit = decision.lookup &&
                        find_cached_frame(cache,
                                          EL_CACHE_KEY_MEDIA,
                                          media_id,
                                          source_frame,
                                          dst);
        if(cache_hit) {
            el_cache_note_result(cache, &decision, source_frame, 1);
            return 1;
        }

        vj_ffmpeg_input *input = el->ffmpeg_input[file_index];
        long borrowed = 0;
        if(decision.admit_preroll) {
            int64_t requested = vj_ffmpeg_input_preroll_requirement(input,
                                                                    (int64_t)source_frame);
            long request_frames = requested > LONG_MAX ? LONG_MAX : (long)requested;
            if(request_frames > decision.preroll_limit)
                request_frames = decision.preroll_limit;

            if( vj_ffmpeg_input_is_hardware(input)) {
                request_frames = 0; // decode GOP in VRAM and only fetch the target frame to system RAM
            }

            borrowed = el_cache_borrow(cache, decision.owner, request_frames);
        }

        ffmpeg_cache_sink_t sink = {
            cache,
            decision.owner,
            media_id,
            borrowed
        };
        int ret = vj_ffmpeg_input_get_frame(input,
                                            (int64_t)source_frame,
                                            dst,
                                            borrowed,
                                            borrowed > 0 ? el_cache_store_ffmpeg_preroll : NULL,
                                            &sink);

        if(borrowed > 0)
            el_cache_release_borrow(cache);

        if(ret == 1 && decision.admit) {
            el_cache_frame(cache,
                           decision.owner,
                           EL_CACHE_KEY_MEDIA,
                           media_id,
                           source_frame,
                           dst,
                           0);
        }

        if(ret == 1 && el->video_fps > 0.0f)
            vj_ffmpeg_input_check_seek_latency(input,
                                            vj_el_get_usec_per_frame(el->video_fps));

        el_cache_note_result(cache, &decision, source_frame, 0);
        return ret;
    }

    uint64_t source_frame = N_EL_FRAME(n);
    uint64_t media_id = el->media_id[file_index];
    el_cache_decision_t decision = el_cache_prepare_request(cache,
                                                            EL_CACHE_KEY_LEGACY,
                                                            media_id,
                                                            source_frame,
                                                            el->num_frames[file_index]);

    if (decision.lookup &&
        find_cached_frame(cache,
                          EL_CACHE_KEY_LEGACY,
                          media_id,
                          source_frame,
                          dst)) {
        el_cache_note_result(cache, &decision, source_frame, 1);
        return 1;
    }


    int decoder_id = lav_video_compressor_type(el->lav_fd[N_EL_FILE(n)]);

    res = lav_set_video_position(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n));
    if (res < 0)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Error setting video position: %s", lav_strerror());
        return -1;
    }

    int ret_code = 0;
    int strides[4] = { el_out_->len, el_out_->uv_len, el_out_->uv_len, 0 };

    if (decoder_id == 0xffff)
    {
        VJFrame *srci = lav_get_frame_ptr(el->lav_fd[N_EL_FILE(n)]);
        if (srci == NULL)
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Error decoding Image %ld", N_EL_FRAME(n));
            return -1;
        }
        vj_frame_copy(srci->data, dst, strides);
        ret_code = 1;
    }
    else
    {
        vj_decoder *d = (vj_decoder*) el->decoders[N_EL_FILE(n)];
        if (lav_filetype(el->lav_fd[N_EL_FILE(n)]) != 'x')
        {
            res = lav_read_frame(el->lav_fd[N_EL_FILE(n)], d->tmp_buffer);
        }

        uint8_t *data = d->tmp_buffer;
        uint8_t *in[3] = { NULL, NULL, NULL };
        uint8_t *dataplanes[4] = { data, data + el_out_->len, data + el_out_->len + el_out_->uv_len, 0 };

        switch (decoder_id)
        {
            case CODEC_ID_YUV420:
                vj_frame_copy1(data, dst[0], el_out_->len);
                in[0] = data;
                in[1] = data + el_out_->len;
                in[2] = data + el_out_->len + (el_out_->len / 4);
                if (el_pixel_format_ == PIX_FMT_YUVJ422P) {
                    yuv_scale_pixels_from_ycbcr(in[0], 16.0f, 235.0f, el_out_->len);
                    yuv_scale_pixels_from_ycbcr(in[1], 16.0f, 240.0f, el_out_->len / 4);
                }
                yuv420to422planar(in, dst, el->video_width, el->video_height);
                ret_code = 1;
                break;
            case CODEC_ID_YUV420F:
                vj_frame_copy1(data, dst[0], el_out_->len);
                in[0] = data;
                in[1] = data + el_out_->len;
                in[2] = data + el_out_->len + (el_out_->len / 4);
                if (el_pixel_format_ == PIX_FMT_YUV422P) {
                    yuv_scale_pixels_from_y(dst[0], el_out_->len);
                    yuv_scale_pixels_from_uv(dst[1], el_out_->len / 4);
                }
                yuv420to422planar(in, dst, el->video_width, el->video_height);
                ret_code = 1;
                break;
            case CODEC_ID_YUV422:
                vj_frame_copy(dataplanes, dst, strides);
                if (el_pixel_format_ == PIX_FMT_YUVJ422P) {
                    yuv_scale_pixels_from_ycbcr(dst[0], 16.0f, 235.0f, el_out_->len);
                    yuv_scale_pixels_from_ycbcr(dst[1], 16.0f, 240.0f, el_out_->len / 2);
                }
                ret_code = 1;
                break;
            case CODEC_ID_YUV422F:
                vj_frame_copy(dataplanes, dst, strides);
                if (el_pixel_format_ == PIX_FMT_YUV422P) {
                    yuv_scale_pixels_from_y(dst[0], el_out_->len);
                    yuv_scale_pixels_from_uv(dst[1], el_out_->len / 2);
                }
                ret_code = 1;
                break;
            case CODEC_ID_YUVLZO:
                ret_code = lzo_decompress_el(d->lzo_decoder, data, res, dst, el_width_, el_height_, el_pixel_format_);
                break;
            case CODEC_ID_QOIY:
                {
                    qoi_desc qd;
                    qd.channels = 1;
                    qd.colorspace = QOI_LINEAR;
                    qd.height = el_height_;
                    qd.width = el_width_;
                    qoi_decode(data, res, &qd, 1, dst, el_out_->len);
                    ret_code = 1;
                }
                break;
            default:
                {
                    int ret;
                    if(decoder_id == CODEC_ID_MJPEG)
                        ret = vj_el_decode_mjpeg(
                            d,
                            el->ctx[N_EL_FILE(n)],
                            data,
                            res,
                            dst,
                            el->video_file_list[file_index],
                            requested_chroma,
                            &decoded_chroma);
                    else {
                        ret = avhelper_decode_video_direct(el->ctx[N_EL_FILE(n)], data, res, dst, el_pixel_format_, el_width_, el_height_);
                        avhelper_decode_finish(el->ctx[N_EL_FILE(n)]);
                    }
                    ret_code = ret;
                }
                break;
        }
    }

    if (ret_code == 1 && decision.admit &&
        decoded_chroma == VJ_EL_CHROMA_422) {
        el_cache_frame(cache,
                       decision.owner,
                       EL_CACHE_KEY_LEGACY,
                       media_id,
                       source_frame,
                       dst,
                       0);
    }

    el_cache_note_result(cache, &decision, source_frame, 0);
    if(actual_chroma)
        *actual_chroma = decoded_chroma;
    return ret_code;
}

int vj_el_get_video_frame(editlist *el, long nframe, uint8_t *dst[4])
{
    return vj_el_get_video_frame_ex(el,
                                    nframe,
                                    dst,
                                    VJ_EL_CHROMA_422,
                                    NULL);
}


int	test_video_frame( editlist *el, int n, lav_file_t *lav,int out_pix_fmt)
{
	int in_pix_fmt  = 0;

	int res = lav_set_video_position( lav,  0);
	if( res < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error setting frame 0: %s", lav_strerror());
		return -1;
	}
	
   	int decoder_id = lav_video_compressor_type( lav );

	if( decoder_id < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot play that file, unsupported codec");
		return -1;
	}

	if(lav_filetype( lav ) == 'x' || lav_filetype(lav) == 'G')
	{
		return out_pix_fmt;
	}

	switch( lav->MJPG_chroma )
	{
		case CHROMA420F:
			in_pix_fmt = PIX_FMT_YUVJ420P;break;
		case CHROMA422F:
			in_pix_fmt = PIX_FMT_YUVJ422P;break;
		case CHROMA420:
			in_pix_fmt = PIX_FMT_YUV420P; break;
		case CHROMA422:
		case CHROMA411:
			in_pix_fmt = PIX_FMT_YUV422P; break;
		default:
			veejay_msg(0 ,"Unsupported pixel format %d (format=%d)", lav->MJPG_chroma, lav_filetype(lav));
			break;			
	}
	long max_frame_size = get_max_frame_size( lav );

	vj_decoder *d  = _el_new_decoder(
				NULL,
				decoder_id,
				lav_video_width( lav),
				lav_video_height( lav),
			   	(float) lav_frame_rate( lav ),
				out_pix_fmt,
				max_frame_size );

	if(!d)
	{
		veejay_msg(0, "Failed to initialize decoder");
		return -1;
	} 

	res = lav_read_frame( lav, d->tmp_buffer);

	if( res <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error reading frame: %s", lav_strerror());
		_el_free_decoder( d );
		return -1;
	}


	int ret = -1;
	switch( decoder_id )
	{
		case CODEC_ID_HUFFYUV:
			ret = PIX_FMT_YUV422P;
			break;
		case CODEC_ID_YUV420F:
			ret = PIX_FMT_YUVJ420P;
			break;
		case CODEC_ID_YUV422F:
			ret = PIX_FMT_YUVJ422P;
			break;
		case CODEC_ID_QOIY:
			ret = PIX_FMT_YUVJ422P;
			break;
		case CODEC_ID_YUV420:
			ret = PIX_FMT_YUV420P;
			break;
		case CODEC_ID_YUV422:
			ret = PIX_FMT_YUV422P;
			break;
		case CODEC_ID_DVVIDEO:
#ifdef SUPPORT_READ_DV2
			ret = vj_dv_scan_frame( d->dv_decoder, d->tmp_buffer );
			if( ret == PIX_FMT_YUV420P || ret == PIX_FMT_YUVJ420P )
				lav->MJPG_chroma = CHROMA420;
			else
				lav->MJPG_chroma = CHROMA422;
#endif
			break;
		case CODEC_ID_YUVLZO:
			ret = PIX_FMT_YUVJ422P;
			if ( in_pix_fmt != ret )	
			{
				//@ correct chroma 
				if( ret == PIX_FMT_YUV420P || ret == PIX_FMT_YUVJ420P )
					lav->MJPG_chroma = CHROMA420;
				else
					lav->MJPG_chroma = CHROMA422;
			}

			break;
		default:
			_el_free_decoder( d );
			return -1;
			break;	
	}

	el->decoders[n] = (void*) d;

	
	return ret;  
} 

int	vj_el_get_audio_frame(editlist *el, uint32_t nframe, uint8_t *dst)
{
    int ret = 0;
    uint64_t n;	
	int ns0, ns1;

	if(el->is_empty)
	{
		int ns = el->audio_rate / el->video_fps;
		veejay_memset( dst, 0, sizeof(uint8_t) * ns * el->audio_bps );
		return 1;
	}

    if (!el->has_audio)
		return 0;
    
	if (nframe < 0)
		nframe = 0;

	if (nframe > el->total_frames)
		nframe = el->total_frames;

    n = el->frame_list[nframe];

    ns1 = (double) (N_EL_FRAME(n) + 1) * el->audio_rate / el->video_fps;
    ns0 = (double) N_EL_FRAME(n) * el->audio_rate / el->video_fps;

    int file_index = (int)N_EL_FILE(n);
    if(el->backend[file_index] == VJ_EL_BACKEND_FFMPEG) {
        int samples = ns1 - ns0;
        if(samples <= 0)
            return 0;
        int got = 0;
        if(el->ffmpeg_input[file_index])
            got = vj_ffmpeg_input_get_audio_samples(el->ffmpeg_input[file_index],
                                                     ns0,
                                                     samples,
                                                     dst);
        if(got == samples)
            return samples;

        veejay_memset(dst, 0, (size_t)samples * (size_t)el->audio_bps);
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-AUDIO] unavailable EDL audio file=%d sample=%d count=%d; using silence",
                   file_index,
                   ns0,
                   samples);
        return samples;
    }

    ret = lav_set_audio_position(el->lav_fd[file_index], ns0);

    if (ret < 0)
    {
	    veejay_msg(0,"Unable to seek to frame position %ld", ns0);
		return -1;
	}

    ret = lav_read_audio(el->lav_fd[file_index], dst, (ns1 - ns0));
    if (ret < 0) {
	    veejay_msg(0, "Error reading audio data at frame position %ld", ns0);
		int ns = el->audio_rate / el->video_fps;
		veejay_memset( dst, 0, sizeof(uint8_t) * ns * el->audio_bps );
		return 1;
	}
    
	return (ns1 - ns0);
}

int	vj_el_init_420_frame(editlist *el, VJFrame *frame)
{
	frame->data[0] = NULL;
	frame->data[1] = NULL;
	frame->data[2] = NULL;
	frame->uv_len = (el->video_width>>1) * (el->video_height>>1);
	frame->uv_width = el->video_width >> 1;
	frame->uv_height = el->video_height >> 1;
	frame->len = el->video_width * el->video_height;
	frame->shift_v = 1;
	frame->shift_h = 1;
	frame->width = el->video_width;
	frame->height = el->video_height;
	frame->ssm = 0;
	frame->stride[0] = el->video_width;
	frame->stride[1] = frame->stride[2] = frame->stride[0]/2;
	frame->format = el_pixel_format_;
	return 1;
}


int	vj_el_init_422_frame(editlist *el, VJFrame *frame)
{
	frame->data[0] = NULL;
	frame->data[1] = NULL;
	frame->data[2] = NULL;
	frame->uv_len = (el->video_width>>1) * (el->video_height);
	frame->uv_width = el->video_width >> 1;
	frame->uv_height = el->video_height;
	frame->len = el->video_width * el->video_height;
	frame->shift_v = 0;
	frame->shift_h = 1;
	frame->width = el->video_width;
	frame->height = el->video_height;
	frame->ssm = 0;
	frame->stride[0] = el->video_width;
	frame->stride[1] = frame->stride[2] = frame->stride[0]/2;
	frame->format = el_pixel_format_;
	return 1;
}

int	vj_el_get_audio_frame_at(editlist *el, uint32_t nframe, uint8_t *dst, int num )
{
	// get audio from current frame + n frames
    int ret = 0;
    uint64_t n;	
    int ns0, ns1;

    if (!el->has_audio)
	return 0;

    if  (!el->has_video)
	{
		int size = el->audio_rate / el->video_fps * el->audio_bps;
		veejay_memset(dst,0,size);
		return size;
	}
	if (nframe < 0)
		nframe = 0;

	if (nframe > el->total_frames)
		nframe = el->total_frames;

    n = el->frame_list[nframe];

    ns1 = (double) (N_EL_FRAME(n) + num) * el->audio_rate / el->video_fps;
    ns0 = (double) N_EL_FRAME(n) * el->audio_rate / el->video_fps;

    int file_index = (int)N_EL_FILE(n);
    if(el->backend[file_index] == VJ_EL_BACKEND_FFMPEG) {
        int samples = ns1 - ns0;
        if(samples <= 0)
            return 0;
        int got = 0;
        if(el->ffmpeg_input[file_index])
            got = vj_ffmpeg_input_get_audio_samples(el->ffmpeg_input[file_index],
                                                     ns0,
                                                     samples,
                                                     dst);
        if(got == samples)
            return samples;
        veejay_memset(dst, 0, (size_t)samples * (size_t)el->audio_bps);
        return samples;
    }

    ret = lav_set_audio_position(el->lav_fd[file_index], ns0);

    if (ret < 0)
		return -1;

    ret = lav_read_audio(el->lav_fd[file_index], dst, (ns1 - ns0));
    if (ret < 0)
		return -1;

    return (ns1 - ns0);

}


editlist *vj_el_dummy(int flags, int deinterlace, int chroma, char norm, int width, int height, float fps, int fmt)
{
	editlist *el = vj_calloc(sizeof(editlist));
	if(!el) {
		return NULL;
	}
	el->MJPG_chroma = chroma;
	el->video_norm = norm;
	el->is_empty = 1;
	el->is_clone = 1;
	el->has_audio = 0;
	el->audio_rate = 0;
	el->audio_bits = 0;
	el->audio_bps = 0;
	el->audio_chans = 0;
	el->num_video_files = 0;
	el->video_width = width;
	el->video_height = height;
	el->video_frames = DUMMY_FRAMES; 
	el->total_frames = el->video_frames - 1;
	el->video_fps = fps;
	el->video_inter = LAV_NOT_INTERLACED;
	el->pixel_format = get_ffmpeg_pixfmt(fmt);
	/* output pixel format */
	if( fmt == -1 )
		el->pixel_format = el_pixel_format_;
	
	el->pixel_format = fmt;

	el->auto_deinter = deinterlace;
	el->max_frame_size = width * height * 3;
	el->last_afile = -1;
	el->last_apos = 0;
	el->frame_list = NULL;
	el->has_video = 0;
	el->source_hash = editlist_source_hash(el);

	return el;
}

void	vj_el_scan_video_file( char *filename,  int *dw, int *dh, float *dfps, long *arate )
{
	void *tmp = avhelper_get_decoder( filename, PIX_FMT_YUVJ422P, -1, -1 );
	float p_fps = 0.0f;
	int p_wid = 0.0f;
	int p_hei = 0.0f;
	long p_rate = 0;

	float p2_fps = 0.0f;
	int p2_wid = 0;
	int p2_hei = 0;
	long p2_rate = 0;

	if( tmp ) {
		AVCodecContext *c = avhelper_get_codec_ctx( tmp );
		p_wid = c->width;
		p_hei = c->height;
		if( c->time_base.num > 0 ) {
			p_fps = (float) c->time_base.den / c->time_base.num;
		} 
		p_rate = c->sample_rate;
		avhelper_close_decoder(tmp);
	} 
	
	
	char *files[1];
    files[0] = filename;

    editlist *el = vj_el_init_with_args(
        files,
        1,
        0,
        0,
        0,
        0,
        0,
        0,
        0
    );

    if (el) {
		
		if (el->num_video_files > 0) {
            p2_wid  = el->video_width;
            p2_hei  = el->video_height;
            p2_fps  = el->video_fps;
            p2_rate = el->audio_rate;
        }

        vj_el_free(el);
	}

	/*
	lav_file_t *fd = lav_open_input_file( filename, 0 );
	if( fd ) {
		p2_wid = lav_video_width( fd );
		p2_hei = lav_video_height( fd );
		p2_fps = lav_frame_rate( fd );
		p2_rate = lav_audio_rate( fd );
		lav_close(fd);
	}*/
	
	*dw = (p_wid > 0) ? p_wid : p2_wid;
    *dh = (p_hei > 0) ? p_hei : p2_hei;
    *dfps = (p_fps > 0.0f) ? p_fps : p2_fps;
    *arate = (p_rate > 0) ? p_rate : p2_rate;
	
	veejay_msg(VEEJAY_MSG_DEBUG, "Using video settings from first loaded video %s: %dx%d@%2.2f R=%ld", filename,*dw,*dh,*dfps, *arate);
}


int	vj_el_auto_detect_scenes( editlist *el, uint8_t *tmp[4], int w, int h, int dl_threshold )
{
	long n1 = 0;
	long n2 = el->total_frames;
	long n;
	int dl = 0;
	int last_lm = 0;
	int index = 0;
	long prev = 0;

	if( el == NULL || el->is_empty || el->total_frames < 2 )
		return 0;

	for( n = n1; n < n2; n ++ ) {
		vj_el_get_video_frame(el, n, tmp );
		int lm = luminance_mean( tmp, w, h );
		if( n == 0 ) {
			dl = 0;
		}
		else {
			dl = abs( lm - last_lm );
		}
		last_lm = lm;

		veejay_msg(VEEJAY_MSG_DEBUG,"frame %ld/%ld luminance mean %d, delta %d ", n, n2, lm, dl );

		if( dl > dl_threshold ) {

			if( prev == 0 ) {
				sample_new_simple(el,0,n);
				veejay_msg(VEEJAY_MSG_INFO,"sampled frames %ld - %ld", 0,n);
			} else {
				sample_new_simple(el,prev,n);
				veejay_msg(VEEJAY_MSG_INFO,"sampled frames %ld - %ld", prev, n );
			}

			prev = n;
			index ++;
		}	
	}
	return index;
}


static char *vj_el_read_disk_line(FILE *fd)
{
    if (!fd)
        return NULL;

    size_t cap = 256;
    size_t len = 0;
    char *line = (char *)malloc(cap);

    if (!line)
        return NULL;

    int ch = 0;

    while ((ch = fgetc(fd)) != EOF) {
        if (ch == '\n')
            break;

        if (len + 1 >= cap) {
            size_t new_cap = cap * 2;
            char *tmp = (char *)realloc(line, new_cap);

            if (!tmp) {
                free(line);
                return NULL;
            }

            line = tmp;
            cap = new_cap;
        }

        line[len++] = (char)ch;
    }

    if (ch == EOF && len == 0) {
        free(line);
        return NULL;
    }

    if (len > 0 && line[len - 1] == '\r')
        len--;

    line[len] = '\0';

    return line;
}

static int vj_el_filename_disk_safe(const char *s)
{
    if (!s)
        return 0;

    while (*s) {
        if (*s == '\n' || *s == '\r')
            return 0;
        s++;
    }

    return 1;
}

static int vj_el_append_frame_range(editlist *el, int file_idx, long n1, long n2)
{
    if (!el || file_idx < 0 || file_idx >= el->num_video_files)
        return 0;

    if (el->num_frames[file_idx] <= 0)
        return 1;

    if (n1 < 0)
        n1 = 0;

    if (n2 >= el->num_frames[file_idx])
        n2 = el->num_frames[file_idx] - 1;

    if (n2 < n1)
        return 1;

    uint64_t old_frames = el->video_frames;
    uint64_t add_frames = (uint64_t)(n2 - n1 + 1);
    uint64_t new_frames = old_frames + add_frames;

    if (new_frames < old_frames)
        return 0;

    uint64_t *tmp = (uint64_t *)realloc(
        el->frame_list,
        (size_t)new_frames * sizeof(uint64_t)
    );

    if (!tmp)
        return 0;

    el->frame_list = tmp;

    for (long f = n1; f <= n2; f++)
        el->frame_list[el->video_frames++] = EL_ENTRY(file_idx, f);

    return 1;
}

editlist *vj_el_init_with_args(char **filename,
                               int num_files,
                               int flags,
                               int deinterlace,
                               int force,
                               char norm,
                               int out_format,
                               int width,
                               int height)
{
    editlist *el = vj_calloc(sizeof(editlist));
    FILE *fd;
    uint64_t index_list[MAX_EDIT_LIST_FILES];
    long nf = 0;
    uint64_t n = 0;

    int av_pixfmt = get_ffmpeg_pixfmt(out_format);

    if (!el)
        return NULL;

    el->has_video = 1;
    el->MJPG_chroma = CHROMA420;
    el->is_empty = 0;

    if (!filename || !filename[0]) {
        veejay_msg(VEEJAY_MSG_ERROR, "\tInvalid filename given");
        free(el);
        return NULL;
    }

    if (strcmp(filename[0], "+p") == 0 ||
        strcmp(filename[0], "+n") == 0 ||
        strcmp(filename[0], "+s") == 0)
    {
        el->video_norm = filename[0][1];
        nf = 1;
    }

    if (force)
        veejay_msg(VEEJAY_MSG_WARNING, "Forcing load on interlacing and gop_size");

    for (; nf < num_files; nf++) {
        struct stat fileinfo;

        if (stat(filename[nf], &fileinfo) != 0) {
            veejay_msg(VEEJAY_MSG_ERROR, "Unable to access file '%s'", filename[nf]);
            vj_el_free(el);
            return NULL;
        }

        fd = fopen(filename[nf], "r");
        if (!fd) {
            veejay_msg(VEEJAY_MSG_DEBUG, "Error opening %s:", filename[nf]);
            vj_el_free(el);
            return NULL;
        }

        char *line = vj_el_read_disk_line(fd);
        if (!line) {
            fclose(fd);
            veejay_msg(VEEJAY_MSG_DEBUG, "Error opening %s:", filename[nf]);
            vj_el_free(el);
            return NULL;
        }

        if (strcmp(line, "LAV Edit List") == 0) {
            int num_list_files = 0;

            free(line);

            veejay_msg(VEEJAY_MSG_DEBUG, "Edit list %s opened", filename[nf]);

            line = vj_el_read_disk_line(fd);
            if (!line) {
                veejay_msg(VEEJAY_MSG_ERROR, "Failed to read %s", filename[nf]);
                fclose(fd);
                vj_el_free(el);
                return NULL;
            }

            if (line[0] != 'N' && line[0] != 'n' &&
                line[0] != 'P' && line[0] != 'p' &&
                line[0] != 'S' && line[0] != 's')
            {
                veejay_msg(VEEJAY_MSG_DEBUG, "Edit list second line is not NTSC/PAL/SECAM");
                free(line);
                fclose(fd);
                vj_el_free(el);
                return NULL;
            }

            if (el->video_norm != '\0')
                veejay_msg(VEEJAY_MSG_WARNING, "Norm already set to, ignoring new norm");
            else
                el->video_norm = tolower((unsigned char)line[0]);

            free(line);

            line = vj_el_read_disk_line(fd);
            if (!line) {
                veejay_msg(VEEJAY_MSG_DEBUG, "Third line: cannot read number of files");
                fclose(fd);
                vj_el_free(el);
                return NULL;
            }

            if (sscanf(line, "%d", &num_list_files) != 1 ||
                num_list_files < 0 ||
                num_list_files > MAX_EDIT_LIST_FILES)
            {
                veejay_msg(VEEJAY_MSG_DEBUG, "Parse error");
                free(line);
                fclose(fd);
                vj_el_free(el);
                return NULL;
            }

            free(line);

            veejay_msg(VEEJAY_MSG_DEBUG, "Edit list contains %d files", num_list_files);

            for (int li = 0; li < num_list_files; li++) {
                line = vj_el_read_disk_line(fd);
                if (!line) {
                    fclose(fd);
                    vj_el_free(el);
                    return NULL;
                }

                if (!vj_el_filename_disk_safe(line)) {
                    veejay_msg(VEEJAY_MSG_ERROR, "Invalid filename in edit list");
                    free(line);
                    fclose(fd);
                    vj_el_free(el);
                    return NULL;
                }

                long opened = open_video_file(line,
                                              el,
                                              flags,
                                              deinterlace,
                                              force,
                                              norm,
                                              av_pixfmt,
                                              width,
                                              height);

                if (opened < 0) {
                    veejay_msg(VEEJAY_MSG_ERROR, "File %s not added to EDL", line);
                    free(line);
                    fclose(fd);
                    vj_el_free(el);
                    return NULL;
                }

                index_list[li] = (uint64_t)opened;

                free(line);
            }

            while ((line = vj_el_read_disk_line(fd)) != NULL) {
                long nl = 0;
                long n1 = 0;
                long n2 = 0;

                if (line[0] == ':' || line[0] == '\0') {
                    free(line);
                    continue;
                }

                if (sscanf(line, "%ld %ld %ld", &nl, &n1, &n2) != 3) {
                    veejay_msg(VEEJAY_MSG_ERROR, "Parse error in edit list entry");
                    free(line);
                    fclose(fd);
                    vj_el_free(el);
                    return NULL;
                }

                free(line);

                if (nl < 0 || nl >= num_list_files) {
                    veejay_msg(VEEJAY_MSG_ERROR, "Wrong file number in edit list entry");
                    fclose(fd);
                    vj_el_free(el);
                    return NULL;
                }

                int real_file = (int)index_list[nl];

                if (real_file < 0 || real_file >= el->num_video_files) {
                    veejay_msg(VEEJAY_MSG_ERROR, "Invalid mapped file number in edit list entry");
                    fclose(fd);
                    vj_el_free(el);
                    return NULL;
                }

                if (!vj_el_append_frame_range(el, real_file, n1, n2)) {
                    veejay_msg(VEEJAY_MSG_ERROR, "Insufficient memory to allocate frame_list");
                    fclose(fd);
                    vj_el_free(el);
                    return NULL;
                }
            }

            if (ferror(fd)) {
                veejay_msg(VEEJAY_MSG_ERROR, "Error while reading edit list");
                fclose(fd);
                vj_el_free(el);
                return NULL;
            }

            fclose(fd);
        } else {
            free(line);
            fclose(fd);

            long opened = open_video_file(filename[nf],
                                          el,
                                          flags,
                                          deinterlace,
                                          force,
                                          norm,
                                          av_pixfmt,
                                          width,
                                          height);

            if (opened >= 0) {
                n = (uint64_t)opened;

                if (!vj_el_append_frame_range(el, (int)n, 0, el->num_frames[n] - 1)) {
                    veejay_msg(VEEJAY_MSG_ERROR, "Insufficient memory to allocate frame_list");
                    vj_el_free(el);
                    return NULL;
                }
            }
        }
    }

    if (el->num_video_files == 0 ||
        el->video_width == 0 ||
        el->video_height == 0 ||
        el->video_frames < 1)
    {
        if (el->video_frames < 1)
            veejay_msg(VEEJAY_MSG_ERROR, "\tFile has no video frames");

        if (el->num_video_files == 0)
            veejay_msg(VEEJAY_MSG_ERROR, "\tNo videofiles in EDL");

        if (el->video_height == 0 || el->video_width == 0)
            veejay_msg(VEEJAY_MSG_ERROR, "\tImage dimensions unknown");

        vj_el_free(el);
        return NULL;
    }

    long cur_max_frame_size = 0;

    for (long i = 0; i < el->num_video_files; i++) {
        if (el->max_frame_sizes[i] > cur_max_frame_size)
            cur_max_frame_size = el->max_frame_sizes[i];
    }

    if (cur_max_frame_size == 0) {
        for (long i = 0; i < el->num_video_files; i++) {
            if(el->backend[i] == VJ_EL_BACKEND_FFMPEG)
                continue;
            long tmp = get_max_frame_size(el->lav_fd[i]);

            if (tmp > cur_max_frame_size)
                cur_max_frame_size = tmp;
        }
    }

    el->max_frame_size = cur_max_frame_size;
    el->pixel_format = el_pixel_format_org;
    el->total_frames = el->video_frames - 1;
    el->last_afile = -1;
    el->auto_deinter = 0;
    el->source_hash = editlist_source_hash(el);

    return el;
}


void	vj_el_free(editlist *el)
{
	if(!el)
		return;

	int i;
	for ( i = 0; i < el->num_video_files; i ++ )
	{
		if( el->video_file_list[i]) {
			free(el->video_file_list[i]);
			el->video_file_list[i] = NULL;
		}

		if( el->is_clone )
			continue;

		if(el->backend[i] == VJ_EL_BACKEND_FFMPEG) {
			vj_ffmpeg_input_close(el->ffmpeg_input[i]);
			el->ffmpeg_input[i] = NULL;
			continue;
		}

		if( el->ctx[i] ) {
			avhelper_close_decoder( el->ctx[i] );
		}
		if( el->decoders[i] ) {
			_el_free_decoder( el->decoders[i] );	
		}
		if( el->lav_fd[i] ) 
		{
			lav_close( el->lav_fd[i] );
			el->lav_fd[i] = NULL;
		}
	}

	if( el->frame_list ) {
		free(el->frame_list );
		el->frame_list = NULL;
	}
	
	free(el);
	el = NULL;
}

void	vj_el_print(editlist *el)
{
	int i;
	char timecode[64];
	char interlacing[64];
	MPEG_timecode_t ttc;
	veejay_msg(VEEJAY_MSG_INFO,"EditList settings: Video:%dx%d@%2.2f %s\tAudio:%d Hz/%d channels/%d bits",
		el->video_width,el->video_height,el->video_fps,(el->video_norm=='p' ? "PAL" :"NTSC"),
		el->audio_rate, el->audio_chans, el->audio_bits);
	for(i=0; i < el->num_video_files ; i++)
	{
		if(el->backend[i] == VJ_EL_BACKEND_FFMPEG) {
			const vj_ffmpeg_input_info *info = vj_ffmpeg_input_get_info(el->ffmpeg_input[i]);
			if(info) {
				MPEG_timecode_t tc;
				mpeg_timecode(&tc, info->frame_count,
						mpeg_framerate_code(mpeg_conform_framerate(el->video_fps)),
						el->video_fps);
				snprintf(timecode, sizeof(timecode), "%2d:%2.2d:%2.2d:%2.2d", tc.h, tc.m, tc.s, tc.f);
				veejay_msg(VEEJAY_MSG_INFO,
					"\tFile %s (FFmpeg/%s) with %ld frames (total duration %s)",
					el->video_file_list[i], info->codec_name, (long)info->frame_count, timecode);
			}
			continue;
		}
		long num_frames = lav_video_frames(el->lav_fd[i]);
		MPEG_timecode_t tc;
		switch( lav_video_interlacing(el->lav_fd[i]))
		{
			case LAV_NOT_INTERLACED:
				snprintf(interlacing, sizeof(interlacing), "Not interlaced"); break;
			case LAV_INTER_TOP_FIRST:
				snprintf(interlacing, sizeof(interlacing),"Top field first"); break;
			case LAV_INTER_BOTTOM_FIRST:
				snprintf(interlacing, sizeof(interlacing), "Bottom field first"); break;
			default:
				snprintf(interlacing, sizeof(interlacing), "Unknown !"); break;
		} 

		mpeg_timecode(&tc, num_frames,
				mpeg_framerate_code( mpeg_conform_framerate( el->video_fps )),
				el->video_fps );

		snprintf(timecode, sizeof(timecode), "%2d:%2.2d:%2.2d:%2.2d", tc.h, tc.m, tc.s, tc.f );

		veejay_msg(VEEJAY_MSG_INFO, "\tFile %s (%s) with %ld frames (total duration %s)",
			el->video_file_list[i],
			interlacing,
			num_frames,
			timecode );
			
	}

	mpeg_timecode(&ttc, el->video_frames,
			mpeg_framerate_code( mpeg_conform_framerate( el->video_fps )),
			el->video_fps );

	snprintf(timecode, sizeof(timecode), "%2d:%2.2d:%2.2d:%2.2d", ttc.h, ttc.m, ttc.s, ttc.f );

	veejay_msg(VEEJAY_MSG_INFO, "\tDuration: %s (%2d hours, %2d minutes)(%ld frames)", timecode,ttc.h,ttc.m,el->video_frames);
}

int	vj_el_get_file_entry(editlist *el, long *start_pos, long *end_pos, long entry )
{
	if(entry >= el->num_video_files)
		return 0;

	int64_t	n = (int64_t) entry;
	int64_t i = 0;

	if( el->video_file_list[ n ] == NULL )
		return 0;

	*start_pos = 0;

	for( i = 0;i < n ; i ++ )
		*start_pos += el->num_frames[i];

	*end_pos = (*start_pos + el->num_frames[n] - 1);

	return 1;
}


static int edl_put_dec_field(char **pp, char *end, uint64_t v, int width)
{
    char tmp[32];

    if (width <= 0 || width >= (int)sizeof(tmp)) return 0;
    if (*pp + width > end) return 0;

    for (int i = width - 1; i >= 0; i--) {
        tmp[i] = (char)('0' + (v % 10));
        v /= 10;
    }

    if (v != 0) return 0;

    memcpy(*pp, tmp, width);
    *pp += width;

    return 1;
}

static int edl_put_bytes(char **pp, char *end, const char *src, size_t n)
{
    if (!src || *pp + n > end) return 0;

    memcpy(*pp, src, n);
    *pp += n;

    return 1;
}

char *vj_el_write_line_utf8(editlist *el, int *bytes_written)
{
    if (bytes_written) *bytes_written = 0;
    if (!el || el->is_empty || !bytes_written) return NULL;

    int64_t *index = (int64_t *)vj_malloc(sizeof(int64_t) * MAX_EDIT_LIST_FILES);
    if (!index) return NULL;

    for (int i = 0; i < MAX_EDIT_LIST_FILES; i++)
        index[i] = -1;

    for (uint64_t j = 0; j <= (uint64_t)el->total_frames; j++) {
        int f = N_EL_FILE(el->frame_list[j]);

        if (f >= 0 && f < MAX_EDIT_LIST_FILES && el->video_file_list[f])
            index[f] = 1;
    }

    int nnf = 0;
    size_t total_len_estimate = 128;

    for (int j = 0; j < MAX_EDIT_LIST_FILES; j++) {
        if (index[j] == 1 && el->video_file_list[j]) {
            index[j] = nnf++;
            total_len_estimate += strlen(el->video_file_list[j]) + 64;
        } else {
            index[j] = -1;
        }
    }

    if (nnf <= 0) {
        free(index);
        return NULL;
    }

    total_len_estimate += ((size_t)el->total_frames + 1) * 48;

    char *result = (char *)vj_calloc(total_len_estimate);
    if (!result) {
        free(index);
        return NULL;
    }

    char *p = result;
    char *end = result + total_len_estimate;

#define PUT_DEC(V, W) do { if (!edl_put_dec_field(&p, end, (uint64_t)(V), (W))) goto fail; } while (0)
#define PUT_MEM(S, N) do { if (!edl_put_bytes(&p, end, (S), (N))) goto fail; } while (0)

    PUT_DEC(nnf, 4);

    for (int j = 0; j < MAX_EDIT_LIST_FILES; j++) {
        if (index[j] >= 0 && el->video_file_list[j]) {
            const char *name = el->video_file_list[j];
            size_t name_len = strlen(name);

            if (name_len > 9999) goto fail;

            char fourcc[5] = "????";
            vj_el_get_file_fourcc(el, j, fourcc);

            PUT_DEC(name_len, 4);
            PUT_MEM(name, name_len);
            PUT_DEC(j, 4);
            PUT_DEC((uint64_t)el->num_frames[j], 10);
            PUT_MEM(fourcc, 4);
        }
    }

    uint64_t first_raw = el->frame_list[0];
    int first_file = N_EL_FILE(first_raw);

    if (first_file < 0 || first_file >= MAX_EDIT_LIST_FILES || index[first_file] < 0)
        goto fail;

    int64_t oldfile = index[first_file];
    uint64_t oldframe = N_EL_FRAME(first_raw);

    if (oldfile < 0) goto fail;

    PUT_DEC((uint64_t)oldfile, 16);
    PUT_DEC(oldframe, 16);

    for (uint64_t j = 1; j <= (uint64_t)el->total_frames; j++) {
        uint64_t nframe = el->frame_list[j];

        int file_no = N_EL_FILE(nframe);
        if (file_no < 0 || file_no >= MAX_EDIT_LIST_FILES) goto fail;

        int64_t cur_file_idx = index[file_no];
        if (cur_file_idx < 0) goto fail;

        uint64_t cur_frame_idx = N_EL_FRAME(nframe);

        if (cur_file_idx != oldfile || cur_frame_idx != oldframe + 1) {
            PUT_DEC(oldframe, 16);
            PUT_DEC((uint64_t)cur_file_idx, 16);
            PUT_DEC(cur_frame_idx, 16);
        }

        oldfile = cur_file_idx;
        oldframe = cur_frame_idx;
    }

    PUT_DEC(oldframe, 16);

    *bytes_written = (int)(p - result);

#undef PUT_DEC
#undef PUT_MEM

    free(index);
    return result;

fail:
#undef PUT_DEC
#undef PUT_MEM

    veejay_msg(VEEJAY_MSG_ERROR, "Failed to serialize editlist");

    free(index);
    free(result);
    return NULL;
}

int vj_el_write_editlist(char *name, long _n1, long _n2, editlist *el)
{
    FILE *fd;
    int num_files = 0;
    uint64_t oldfile, oldframe;
    uint64_t n;
    uint64_t n1, n2;
    uint64_t i;

    if (!name || !el || el->is_empty || el->video_frames == 0)
        return 0;

    if (_n1 < 0)
        _n1 = 0;

    if (_n2 < 0)
        _n2 = 0;

    n1 = (uint64_t)_n1;
    n2 = (uint64_t)_n2;

    if (n1 >= el->video_frames)
        n1 = el->video_frames - 1;

    if (n2 >= el->video_frames)
        n2 = el->video_frames - 1;

    if (n2 < n1) {
        uint64_t tmp = n1;
        n1 = n2;
        n2 = tmp;
    }

    int *index = (int *)vj_malloc(sizeof(int) * MAX_EDIT_LIST_FILES);
    if (!index)
        return 0;

    for (i = 0; i < MAX_EDIT_LIST_FILES; i++)
        index[i] = -1;

    for (i = n1; i <= n2; i++) {
        n = el->frame_list[i];

        int file = (int)N_EL_FILE(n);

        if (file < 0 ||
            file >= MAX_EDIT_LIST_FILES ||
            el->video_file_list[file] == NULL ||
            !vj_el_filename_disk_safe(el->video_file_list[file]))
        {
            free(index);
            return 0;
        }

        index[file] = 1;
    }

    for (i = 0; i < MAX_EDIT_LIST_FILES; i++) {
        if (index[i] == 1)
            index[i] = num_files++;
    }

    int out_fd = open(name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                      S_IRUSR | S_IWUSR | S_IRGRP);
    if (out_fd < 0) {
        free(index);
        return 0;
    }

    if (fchmod(out_fd, S_IRUSR | S_IWUSR | S_IRGRP) != 0) {
        close(out_fd);
        free(index);
        return 0;
    }

    fd = fdopen(out_fd, "w");
    if (!fd) {
        close(out_fd);
        free(index);
        return 0;
    }

    fprintf(fd, "LAV Edit List\n");
    fprintf(fd, "%s\n", el->video_norm == 'n' ? "NTSC" : "PAL");
    fprintf(fd, "%d\n", num_files);

    for (i = 0; i < MAX_EDIT_LIST_FILES; i++) {
        if (index[i] != -1 && el->video_file_list[i] != NULL)
            fprintf(fd, "%s\n", el->video_file_list[i]);
    }

    n = el->frame_list[n1];

    {
        int file = (int)N_EL_FILE(n);

        oldfile  = (uint64_t)index[file];
        oldframe = N_EL_FRAME(n);
    }

    fprintf(fd, "%" PRIu64 " %" PRIu64 " ", oldfile, oldframe);

    for (i = n1 + 1; i <= n2; i++) {
        uint64_t cur_file;
        uint64_t cur_frame;
        int file;

        n = el->frame_list[i];
        file = (int)N_EL_FILE(n);

        cur_file  = (uint64_t)index[file];
        cur_frame = N_EL_FRAME(n);

        if (cur_file != oldfile || cur_frame != oldframe + 1) {
            fprintf(fd, "%" PRIu64 "\n", oldframe);
            fprintf(fd, "%" PRIu64 " %" PRIu64 " ", cur_file, cur_frame);
        }

        oldfile = cur_file;
        oldframe = cur_frame;
    }

    fprintf(fd, "%" PRIu64 "\n", oldframe);

    if (ferror(fd)) {
        fclose(fd);
        free(index);
        return 0;
    }

    if (fclose(fd) != 0) {
        free(index);
        return 0;
    }

    free(index);
    return 1;
}


static editlist *vj_el_soft_clone_base(editlist *el)
{
    editlist *clone = (editlist*) vj_calloc(sizeof(editlist));
	if(!clone)
		return NULL;

	clone->is_empty = el->is_empty;
	clone->has_video = el->has_video;
	clone->video_width = el->video_width;
	clone->video_height = el->video_height;
	clone->video_inter = el->video_inter;
	clone->video_fps = el->video_fps;
	clone->video_sar_width = el->video_sar_width;
	clone->video_sar_height = el->video_sar_height;
	clone->video_norm = el->video_norm;
	clone->has_audio = el->has_audio;
	clone->audio_rate = el->audio_rate;
	clone->audio_chans = el->audio_chans;
	clone->audio_bits = el->audio_bits;
	clone->audio_bps = el->audio_bps;
	clone->video_frames = el->video_frames;
	clone->total_frames = el->video_frames - 1;
#ifdef STRICT_CHECKING
    assert( clone->total_frames == el->total_frames );
#endif
	clone->num_video_files = el->num_video_files;
	clone->max_frame_size = el->max_frame_size;
	clone->MJPG_chroma = el->MJPG_chroma;
	clone->last_afile = el->last_afile;
	clone->last_apos  = el->last_apos;
	clone->auto_deinter = el->auto_deinter;
	clone->pixel_format = el->pixel_format;
	clone->is_clone = 1;

    return clone;
}

editlist	*vj_el_soft_clone(editlist *el)
{
    editlist *clone = vj_el_soft_clone_base(el);
    int i;
	for( i = 0; i < MAX_EDIT_LIST_FILES; i ++ )
	{
		clone->video_file_list[i] = NULL;
		clone->lav_fd[i] = NULL;
		clone->ffmpeg_input[i] = NULL;
		clone->backend[i] = el->backend[i];
		clone->media_id[i] = el->media_id[i];
		clone->num_frames[i] = 0;
		clone->pixfmt[i] = 0;
		if( (el->lav_fd[i] || el->ffmpeg_input[i]) && el->video_file_list[i])
		{
			clone->video_file_list[i] = vj_strdup( el->video_file_list[i] );
			clone->lav_fd[i] = el->lav_fd[i];
			clone->ffmpeg_input[i] = el->ffmpeg_input[i];
			clone->num_frames[i] = el->num_frames[i];
			clone->pixfmt[i] =el->pixfmt[i];
		}
		clone->decoders[i] = el->decoders[i]; 
		clone->ctx[i] = el->ctx[i];
	}

	clone->source_hash = editlist_source_hash(clone);

	return clone;
}

editlist *vj_el_soft_clone_range(editlist *el, long n1, long n2)
{
    if(!el || el->is_empty || !el->frame_list ||
       n1 < 0 || n2 < n1 || (uint64_t)n2 >= (uint64_t)el->video_frames)
    {
        return NULL;
    }

    const uint64_t count = (uint64_t)(n2 - n1) + 1u;

    if(count > (uint64_t)SIZE_MAX / sizeof(uint64_t))
        return NULL;

    editlist *clone = vj_el_soft_clone_base(el);
    if(!clone)
        return NULL;

    clone->frame_list = (uint64_t *)vj_calloc((size_t)count * sizeof(uint64_t));
    if(!clone->frame_list) {
        free(clone);
        return NULL;
    }

    uint64_t k = 0;

    for(long nframe = n1; nframe <= n2; nframe++) {
        uint64_t entry = el->frame_list[nframe];
        int file_idx = (int)N_EL_FILE(entry);

        if(file_idx < 0 || file_idx >= MAX_EDIT_LIST_FILES ||
           !el->video_file_list[file_idx])
        {
            vj_el_free(clone);
            return NULL;
        }

        if(!clone->video_file_list[file_idx]) {
            clone->video_file_list[file_idx] = vj_strdup(el->video_file_list[file_idx]);
            if(!clone->video_file_list[file_idx]) {
                vj_el_free(clone);
                return NULL;
            }

            clone->lav_fd[file_idx] = el->lav_fd[file_idx];
            clone->ffmpeg_input[file_idx] = el->ffmpeg_input[file_idx];
            clone->backend[file_idx] = el->backend[file_idx];
            clone->media_id[file_idx] = el->media_id[file_idx];
            clone->num_frames[file_idx] = el->num_frames[file_idx];
            clone->max_frame_sizes[file_idx] = el->max_frame_sizes[file_idx];
            clone->pixfmt[file_idx] = el->pixfmt[file_idx];
            clone->decoders[file_idx] = el->decoders[file_idx];
            clone->ctx[file_idx] = el->ctx[file_idx];
        }

        clone->frame_list[k++] = entry;
    }

    clone->is_empty = 0;
    clone->video_frames = (long)k;
    clone->total_frames = k - 1u;
    clone->source_hash = editlist_source_hash(clone);

    return clone;
}



int		vj_el_framelist_clone( editlist *src, editlist *dst)
{
	if(!src || !dst) return 0;
	if(dst->frame_list)
		return 0;
	if(src->video_frames <= 0)
		return 1;
	if(!src->frame_list)
		return 0;

	dst->frame_list = (uint64_t*) vj_malloc(sizeof(uint64_t) * src->video_frames );
	if(!dst->frame_list)
		return 0;
	
	veejay_memcpy(
		dst->frame_list,
		src->frame_list,
		(sizeof(uint64_t) * src->video_frames )
	); 
	
	return 1;
}

static int vj_el_clone_build_file_map(editlist *el, char ***files_out, int **map_out, int *num_files_out)
{
    int *map = NULL;
    char **files = NULL;
    int num_files = 0;

    if(!el || el->num_video_files <= 0)
        return 0;

    map = (int*) vj_malloc(sizeof(int) * el->num_video_files);
    files = (char**) vj_calloc(sizeof(char*) * el->num_video_files);
    if(!map || !files) {
        if(map) free(map);
        if(files) free(files);
        return 0;
    }

    for(int i = 0; i < el->num_video_files; i++) {
        map[i] = -1;
        if(!el->video_file_list[i])
            continue;

        for(int j = 0; j < num_files; j++) {
            if(strcmp(files[j], el->video_file_list[i]) == 0) {
                map[i] = j;
                break;
            }
        }

        if(map[i] == -1) {
            files[num_files] = el->video_file_list[i];
            map[i] = num_files;
            num_files++;
        }
    }

    if(num_files <= 0) {
        free(map);
        free(files);
        return 0;
    }

    *files_out = files;
    *map_out = map;
    *num_files_out = num_files;
    return 1;
}

static int vj_el_clone_framelist_remap(editlist *src, editlist *dst, const int *map, int map_len)
{
    if(!src || !dst || !map || map_len <= 0 || dst->frame_list)
        return 0;

    if(src->video_frames <= 0)
        return 1;

    if(!src->frame_list)
        return 0;

    dst->frame_list = (uint64_t*) vj_malloc(sizeof(uint64_t) * src->video_frames);
    if(!dst->frame_list)
        return 0;

    for(long i = 0; i < src->video_frames; i++) {
        uint64_t entry = src->frame_list[i];
        int old_file = N_EL_FILE(entry);

        if(old_file < 0 || old_file >= map_len || map[old_file] < 0) {
            free(dst->frame_list);
            dst->frame_list = NULL;
            return 0;
        }

        dst->frame_list[i] = EL_ENTRY(map[old_file], N_EL_FRAME(entry));
    }

    return 1;
}

editlist	*vj_el_clone(editlist *el)
{
    editlist *clone = NULL;

    if(!el)
        return NULL;

    if(el->is_empty || !el->has_video || el->num_video_files <= 0) {
        clone = vj_el_soft_clone_base(el);
        if(!clone)
            return NULL;

        clone->is_clone = 0;
        clone->num_video_files = 0;

        if(el->frame_list && !vj_el_framelist_clone(el, clone)) {
            vj_el_free(clone);
            veejay_msg(VEEJAY_MSG_ERROR, "Not enough memory to clone EDL");
            return NULL;
        }

        clone->source_hash = editlist_source_hash(clone);
        return clone;
    }

    char **files = NULL;
    int *map = NULL;
    int num_files = 0;

    if(!vj_el_clone_build_file_map(el, &files, &map, &num_files)) {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to build file map while cloning EDL");
        return NULL;
    }

    clone = vj_el_init_with_args(files,
                                 num_files,
                                 1,
                                 el->auto_deinter,
                                 0,
                                 el->video_norm,
                                 el->pixel_format,
                                 el->video_width,
                                 el->video_height);

    free(files);

    if(!clone) {
        free(map);
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to reopen video files while cloning EDL");
        return NULL;
    }

    if(clone->frame_list) {
        free(clone->frame_list);
        clone->frame_list = NULL;
    }

    if(!vj_el_clone_framelist_remap(el, clone, map, el->num_video_files)) {
        free(map);
        vj_el_free(clone);
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to copy edited frame sequence while cloning EDL");
        return NULL;
    }

    if(el->last_afile >= 0 && el->last_afile < el->num_video_files)
        clone->last_afile = map[el->last_afile];
    else
        clone->last_afile = -1;

    free(map);

    clone->video_frames = el->video_frames;
    clone->total_frames = el->total_frames;
    clone->last_apos = el->last_apos;
    clone->auto_deinter = el->auto_deinter;
    clone->is_clone = 0;
    clone->source_hash = editlist_source_hash(clone);

    return clone;
}
