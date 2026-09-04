/* veejay - Linux VeeJay - libplugger utility
 *			 (C) 2002-2015 Niels Elburg <nwelburg@gmail.com> 
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

/** \defgroup livido Livido Host
 *
 * See livido specification at http://livido.dyne.org
 */
#include <config.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <veejaycore/hash.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <veejaycore/libvevo.h>
#include <libplugger/defs.h>
#include <libplugger/ldefs.h>
#include <libplugger/specs/livido.h>
#include <libplugger/portdef.h>
#include <libplugger/defaults.h>
#include <veejaycore/yuvconv.h>
#include <libavutil/pixfmt.h>
#include <libplugger/utility.h>
#include <libplugger/livido-loader.h>
#include <libsubsample/subsample.h>
#include <veejaycore/avcommon.h>
#include <libveejay/vj-shm.h>
#include <veejaycore/vims.h>
#define LIVIDO_COPY 1

#define IS_RGB_PALETTE( p ) ( p < 512 ? 1 : 0 )

static int livido_dummy_keyframe(livido_port_t *port, long pos, int dir)
{
	(void)port;
	(void)pos;
	(void)dir;
	return LIVIDO_ERROR_NOSUCH_PROPERTY;
}
   
static	char	make_valid_char_( const char c )
{
	const char *invalid = " #*,?[]{}";
	int k = 0;
	char o = '_';
	char r = c;
	for( k = 0; k < 8 ; k ++ )
	{
		if ( c == invalid[k] || isspace((unsigned char)c))
			return o;
		char l = tolower(c);
		if(l)
			r = l;
	}
	return r;
}

char	*veejay_valid_osc_name( const char *in )
{
	int n = strlen( in );
	int k;
	char *res = vj_strndup( in, n );
	for( k = 0; k < n ; k ++ )
	{
		res[k] = make_valid_char_( in[k] );
	}
	return res;
}

static	int	pref_palette_ = 0;
static	int	pref_palette_ffmpeg_ = 0;
static	int	livido_signature_ = VEVO_PLUG_LIVIDO;
static  int read_plugin_configuration = 0;

typedef	int	(*livido_set_parameter_f)( void *parameter, void *value );

#define LIVIDO_ASYNC_SLOT_COUNT 2
#define LIVIDO_FILTER_FLAGS_PROPERTY "HOST_filter_flags"

typedef enum
{
	LIVIDO_ASYNC_INPUT_EMPTY = 0,
	LIVIDO_ASYNC_INPUT_STAGING,
	LIVIDO_ASYNC_INPUT_PENDING,
	LIVIDO_ASYNC_INPUT_PROCESSING
} livido_async_input_state_t;

typedef enum
{
	LIVIDO_ASYNC_OUTPUT_EMPTY = 0,
	LIVIDO_ASYNC_OUTPUT_COMPLETE,
	LIVIDO_ASYNC_OUTPUT_DISPLAY,
	LIVIDO_ASYNC_OUTPUT_READING
} livido_async_output_state_t;

/*
 * A slot's input and output storage are independent: one slot may retain the
 * displayed output while its input half holds the next bounded submission.
 */
typedef struct
{
	VJFrame *inputs;
	uint8_t **input_storage;
	size_t *input_capacity;
	VJFrame output_template;
	int pending_output_sizes[4];
	VJFrame output;
	int output_sizes[4];
	uint8_t *output_storage;
	size_t output_capacity;
	int *parameters;
	int parameter_count;
	int has_output;
	double timecode;
	uint64_t input_sequence;
	uint64_t input_generation;
	uint64_t output_sequence;
	uint64_t output_generation;
	livido_async_input_state_t input_state;
	livido_async_output_state_t output_state;
} livido_async_slot_t;

typedef struct livido_async_state_s
{
	void *instance;
	struct livido_async_state_s *next;
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_mutex_t call_mutex;
	pthread_cond_t cond;
	livido_async_slot_t slots[LIVIDO_ASYNC_SLOT_COUNT];
	int flags;
	int num_inputs;
	int num_outputs;
	int parameter_capacity;
	int thread_started;
	int stop;
	int destroying;
	unsigned int lifetime_refs;
	int processing_slot;
	int display_slot;
	int have_frame_identity;
	int have_frame_step;
	long last_frame_num;
	long last_frame_step;
	uint64_t generation;
	uint64_t next_sequence;
	uint64_t completed_sequence;
	uint64_t coalesced_frames;
	uint64_t backpressure_waits;
} livido_async_state_t;

static int livido_plug_process_internal(void *instance, double time_code);
static pthread_mutex_t livido_async_registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t livido_async_registry_cond = PTHREAD_COND_INITIALIZER;
static livido_async_state_t *livido_async_states = NULL;

static	struct
{
	int it;
	int lp;
} vj_palettes_[] = 
{
	{	FMT_420,	LIVIDO_PALETTE_YUV420P },
	{	FMT_422,	LIVIDO_PALETTE_YUV422P },
	{	FMT_422F,	LIVIDO_PALETTE_YUV422P },
	{	FMT_444,	LIVIDO_PALETTE_YUV444P },
	{	-1,	-1 },
};

int		lvd_livido_palette(int v)
{
	int i;
	for( i = 0; vj_palettes_[i].it >= 0 ; i ++ ) {
		if( vj_palettes_[i].it == v )
			return vj_palettes_[i].lp;
	}
	return -1;
}

static int match_palette(livido_port_t *ptr, int palette);

static int livido_palette_from_vjframe(const VJFrame *frame)
{
	if(!frame)
		return pref_palette_;

	if(frame->width > 0 && frame->height > 0 && frame->uv_width > 0 && frame->uv_height > 0) {
		if(frame->uv_width == frame->width && frame->uv_height == frame->height)
			return LIVIDO_PALETTE_YUV444P;
		if(frame->uv_width == ((frame->width + 1) >> 1) && frame->uv_height == frame->height)
			return LIVIDO_PALETTE_YUV422P;
		if(frame->uv_width == ((frame->width + 1) >> 1) && frame->uv_height == ((frame->height + 1) >> 1))
			return LIVIDO_PALETTE_YUV420P;
	}

	switch(frame->format) {
		case PIX_FMT_YUV420P:
		case PIX_FMT_YUVJ420P:
		case PIX_FMT_YUVA420P:
			return LIVIDO_PALETTE_YUV420P;
		case PIX_FMT_YUV422P:
		case PIX_FMT_YUVJ422P:
		case PIX_FMT_YUVA422P:
			return LIVIDO_PALETTE_YUV422P;
		case PIX_FMT_YUV444P:
		case PIX_FMT_YUVJ444P:
		case PIX_FMT_YUVA444P:
			return LIVIDO_PALETTE_YUV444P;
		default:
			break;
	}

	return pref_palette_;
}

static	int	configure_channel( void *instance, const char *name, int channel_id, VJFrame *frame )
{
	void *channel = NULL;
	void *pd[4];

	if( vevo_property_get( instance, name, channel_id, &channel ) != VEVO_NO_ERROR )
		return 0;
	vevo_property_set( channel  , "fps"	, LIVIDO_ATOM_TYPE_DOUBLE,1, &(frame->fps));
	int	rowstrides[4] = { frame->width, frame->uv_width, frame->uv_width, frame->stride[3] };
	vevo_property_set( channel  , "rowstrides", LIVIDO_ATOM_TYPE_INT,4, &rowstrides );
	vevo_property_set( channel  , "timecode", LIVIDO_ATOM_TYPE_DOUBLE,1, &(frame->timecode));

	int actual_palette = livido_palette_from_vjframe(frame);
	void *parent_template = NULL;
	if( vevo_property_get( channel, "parent_template", 0, &parent_template ) == VEVO_NO_ERROR ) {
		if( match_palette((livido_port_t*) parent_template, actual_palette) )
			vevo_property_set( channel, "current_palette", LIVIDO_ATOM_TYPE_INT,1, &actual_palette );
	}
	else {
		vevo_property_set( channel, "current_palette", LIVIDO_ATOM_TYPE_INT,1, &actual_palette );
	}

	pd[0] = (void*) frame->data[0];
	pd[1] = (void*) frame->data[1];
	pd[2] = (void*) frame->data[2];
	pd[3] = (void*) frame->data[3];

	vevo_property_set( channel, "pixel_data",LIVIDO_ATOM_TYPE_VOIDPTR, 4, &pd);	

	if( name[0] ==  'i' ) {
		int current_palette = 0;
		vevo_property_get( channel, "current_palette", 0, &current_palette );
		if( current_palette != pref_palette_ ) {
			switch( current_palette ) {
				case LIVIDO_PALETTE_YUV444P:
				case LIVIDO_PALETTE_YUVA8888:
					break;
				
			}
		}
	}
	
	return 1;
}

static livido_async_state_t *livido_async_lookup(void *instance)
{
	for(livido_async_state_t *state = livido_async_states;
		state != NULL; state = state->next)
	{
		if(state->instance == instance)
			return state;
	}
	return NULL;
}

static livido_async_state_t *livido_async_acquire(void *instance, int *known_async)
{
	livido_async_state_t *state = NULL;
	int error;

	if(known_async)
		*known_async = 0;

	error = pthread_mutex_lock(&livido_async_registry_mutex);
	if(error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to lock the non-real-time Livido registry: %s",
				   strerror(error));
		return NULL;
	}

	state = livido_async_lookup(instance);
	if(state) {
		if(known_async)
			*known_async = 1;
		if(state->destroying)
			state = NULL;
		else
			state->lifetime_refs++;
	}

	pthread_mutex_unlock(&livido_async_registry_mutex);
	return state;
}

static void livido_async_release(livido_async_state_t *state)
{
	if(!state)
		return;

	int error = pthread_mutex_lock(&livido_async_registry_mutex);
	if(error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to release non-real-time Livido lifetime ownership: %s",
				   strerror(error));
		return;
	}

	if(state->lifetime_refs > 0)
		state->lifetime_refs--;
	else
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Non-real-time Livido lifetime ownership underflow");
	pthread_cond_broadcast(&livido_async_registry_cond);
	pthread_mutex_unlock(&livido_async_registry_mutex);
}

static livido_async_state_t *livido_async_begin_destroy(void *instance)
{
	livido_async_state_t *state = NULL;
	int error = pthread_mutex_lock(&livido_async_registry_mutex);
	if(error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to lock the non-real-time Livido registry for teardown: %s",
				   strerror(error));
		return NULL;
	}

	state = livido_async_lookup(instance);
	if(state)
		state->destroying = 1;

	pthread_mutex_unlock(&livido_async_registry_mutex);
	return state;
}

static int livido_async_unregister(livido_async_state_t *state)
{
	int error = pthread_mutex_lock(&livido_async_registry_mutex);
	if(error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to lock the non-real-time Livido registry for removal: %s",
				   strerror(error));
		return 0;
	}

	livido_async_state_t **link = &livido_async_states;
	while(*link && *link != state)
		link = &(*link)->next;
	if(*link == state)
		*link = state->next;
	else {
		pthread_mutex_unlock(&livido_async_registry_mutex);
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Non-real-time Livido instance state was not registered");
		return 0;
	}

	state->next = NULL;
	pthread_mutex_unlock(&livido_async_registry_mutex);
	return 1;
}

static int livido_async_wait_for_references(livido_async_state_t *state)
{
	int error = pthread_mutex_lock(&livido_async_registry_mutex);
	if(error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to lock non-real-time Livido lifetime state: %s",
				   strerror(error));
		return 0;
	}

	while(state->lifetime_refs > 0) {
		error = pthread_cond_wait(&livido_async_registry_cond,
								  &livido_async_registry_mutex);
		if(error != 0) {
			veejay_msg(VEEJAY_MSG_ERROR,
					   "Unable to wait for non-real-time Livido lifetime owners: %s",
					   strerror(error));
			pthread_mutex_unlock(&livido_async_registry_mutex);
			return 0;
		}
	}

	pthread_mutex_unlock(&livido_async_registry_mutex);
	return 1;
}

static int livido_async_frame_sizes(const VJFrame *frame, int sizes[4], size_t *total)
{
	if(!frame || !sizes || !total || !frame->data[0] ||
	   frame->len <= 0 || frame->uv_len < 0 || frame->height <= 0)
		return 0;

	sizes[0] = frame->len;
	sizes[1] = frame->data[1] ? frame->uv_len : 0;
	sizes[2] = frame->data[2] ? frame->uv_len : 0;
	sizes[3] = 0;

	if(frame->data[3] && frame->stride[3] > 0) {
		if(frame->height > INT_MAX / frame->stride[3])
			return 0;
		sizes[3] = frame->stride[3] * frame->height;
	}

	*total = 0;
	for(int i = 0; i < 4; i++) {
		if(sizes[i] < 0 || *total > SIZE_MAX - (size_t)sizes[i])
			return 0;
		*total += (size_t)sizes[i];
	}

	return *total > 0;
}

static int livido_async_ensure_storage(uint8_t **storage, size_t *capacity, size_t need)
{
	if(*storage && *capacity >= need)
		return 1;

	uint8_t *new_storage = (uint8_t*) vj_malloc(need);
	if(!new_storage)
		return 0;

	if(*storage)
		free(*storage);

	*storage = new_storage;
	*capacity = need;
	return 1;
}

static void livido_async_map_frame(VJFrame *dst, const VJFrame *src,
								   uint8_t *storage, const int sizes[4])
{
	size_t offset = 0;

	*dst = *src;
	dst->local = NULL;

	for(int i = 0; i < 4; i++) {
		if(sizes[i] > 0) {
			dst->data[i] = storage + offset;
			offset += (size_t)sizes[i];
		}
		else {
			dst->data[i] = NULL;
		}
	}
}

static int livido_async_snapshot_frame(VJFrame *dst, uint8_t **storage,
									   size_t *capacity, const VJFrame *src)
{
	int sizes[4];
	size_t total;

	if(!livido_async_frame_sizes(src, sizes, &total) ||
	   !livido_async_ensure_storage(storage, capacity, total))
		return 0;

	livido_async_map_frame(dst, src, *storage, sizes);
	for(int i = 0; i < 4; i++) {
		if(sizes[i] > 0)
			veejay_memcpy(dst->data[i], src->data[i], (size_t)sizes[i]);
	}

	return 1;
}

static int livido_async_set_output_template(livido_async_slot_t *slot,
										 const VJFrame *output)
{
	size_t total;

	if(!livido_async_frame_sizes(output, slot->pending_output_sizes, &total))
		return 0;

	slot->output_template = *output;
	slot->output_template.local = NULL;
	for(int i = 0; i < 4; i++)
		slot->output_template.data[i] = NULL;

	return 1;
}

static void livido_async_fill_frame(VJFrame *frame, const int sizes[4])
{
	const int y_value = frame->range ? 0 : 16;

	for(int i = 0; i < 4; i++) {
		if(sizes[i] <= 0 || !frame->data[i])
			continue;
		int value = (i == 0) ? y_value : ((i == 3) ? 255 : 128);
		veejay_memset(frame->data[i], value, (size_t)sizes[i]);
	}
}

static int livido_async_prepare_output(livido_async_state_t *state,
									   livido_async_slot_t *slot)
{
	size_t total = 0;

	if(!slot->has_output)
		return 1;

	for(int i = 0; i < 4; i++) {
		if(slot->pending_output_sizes[i] < 0 ||
		   total > SIZE_MAX - (size_t)slot->pending_output_sizes[i])
			return 0;
		total += (size_t)slot->pending_output_sizes[i];
	}

	if(total == 0 ||
	   !livido_async_ensure_storage(&slot->output_storage,
									&slot->output_capacity, total))
		return 0;

	livido_async_map_frame(&slot->output, &slot->output_template,
						   slot->output_storage, slot->pending_output_sizes);
	for(int i = 0; i < 4; i++)
		slot->output_sizes[i] = slot->pending_output_sizes[i];

	if(state->num_inputs > 0) {
		int input_sizes[4];
		size_t input_total;

		if(!livido_async_frame_sizes(&slot->inputs[0], input_sizes, &input_total))
			return 0;

		for(int i = 0; i < 4; i++) {
			if(slot->output_sizes[i] <= 0)
				continue;
			if(slot->inputs[0].data[i] &&
			   input_sizes[i] == slot->output_sizes[i])
			{
				veejay_memcpy(slot->output.data[i], slot->inputs[0].data[i],
							  (size_t)slot->output_sizes[i]);
			}
			else {
				int value = (i == 0) ? (slot->output.range ? 0 : 16) :
							((i == 3) ? 255 : 128);
				veejay_memset(slot->output.data[i], value,
							  (size_t)slot->output_sizes[i]);
			}
		}
	}
	else {
		livido_async_fill_frame(&slot->output, slot->output_sizes);
	}

	return 1;
}

static int livido_async_copy_output(const livido_async_slot_t *slot,
									VJFrame *output)
{
	int output_sizes[4];
	size_t total;

	if(!output ||
	   !livido_async_frame_sizes(output, output_sizes, &total))
		return 0;

	for(int i = 0; i < 4; i++) {
		if(output_sizes[i] != slot->output_sizes[i])
			return 0;
	}

	for(int i = 0; i < 4; i++) {
		if(output_sizes[i] > 0)
			veejay_memcpy(output->data[i], slot->output.data[i],
						  (size_t)output_sizes[i]);
	}

	return 1;
}

static int livido_async_apply_fallback(VJFrame **inputs, int num_inputs,
									   VJFrame *output)
{
	if(!output)
		return 1;

	if(num_inputs <= 0) {
		int sizes[4];
		size_t total;
		if(!livido_async_frame_sizes(output, sizes, &total))
			return 0;
		livido_async_fill_frame(output, sizes);
		return 1;
	}

	if(!inputs || !inputs[0])
		return 0;
	if(inputs[0] == output)
		return 1;

	int input_sizes[4];
	int output_sizes[4];
	size_t input_total;
	size_t output_total;

	if(!livido_async_frame_sizes(inputs[0], input_sizes, &input_total) ||
	   !livido_async_frame_sizes(output, output_sizes, &output_total))
		return 0;

	for(int i = 0; i < 4; i++) {
		if(input_sizes[i] != output_sizes[i])
			return 0;
	}

	for(int i = 0; i < 4; i++) {
		if(output_sizes[i] > 0)
			veejay_memcpy(output->data[i], inputs[0]->data[i],
						  (size_t)output_sizes[i]);
	}

	return 1;
}

static int livido_async_find_complete(const livido_async_state_t *state)
{
	for(int i = 0; i < LIVIDO_ASYNC_SLOT_COUNT; i++) {
		if(state->slots[i].output_state == LIVIDO_ASYNC_OUTPUT_COMPLETE)
			return i;
	}
	return -1;
}

static int livido_async_publish_complete_locked(livido_async_state_t *state)
{
	int complete = livido_async_find_complete(state);
	if(complete < 0)
		return 0;

	if(state->slots[complete].output_generation != state->generation) {
		state->slots[complete].output_state = LIVIDO_ASYNC_OUTPUT_EMPTY;
		pthread_cond_broadcast(&state->cond);
		return 0;
	}

	if(state->display_slot >= 0) {
		livido_async_slot_t *display = &state->slots[state->display_slot];
		if(display->output_state == LIVIDO_ASYNC_OUTPUT_READING)
			return 0;
		display->output_state = LIVIDO_ASYNC_OUTPUT_EMPTY;
	}

	state->slots[complete].output_state = LIVIDO_ASYNC_OUTPUT_DISPLAY;
	state->display_slot = complete;
	state->completed_sequence = state->slots[complete].output_sequence;
	pthread_cond_broadcast(&state->cond);
	return 1;
}

static int livido_async_find_input_slot(const livido_async_state_t *state)
{
	int display_candidate = -1;

	for(int i = 0; i < LIVIDO_ASYNC_SLOT_COUNT; i++) {
		if(state->slots[i].input_state != LIVIDO_ASYNC_INPUT_EMPTY)
			continue;
		if(state->slots[i].output_state == LIVIDO_ASYNC_OUTPUT_EMPTY)
			return i;
		if(state->slots[i].output_state == LIVIDO_ASYNC_OUTPUT_DISPLAY &&
		   display_candidate < 0)
			display_candidate = i;
	}

	return display_candidate;
}

static int livido_async_find_coalesce_slot(const livido_async_state_t *state)
{
	int selected = -1;

	for(int i = 0; i < LIVIDO_ASYNC_SLOT_COUNT; i++) {
		if(state->slots[i].input_state != LIVIDO_ASYNC_INPUT_PENDING)
			continue;
		if(selected < 0 ||
		   state->slots[i].input_sequence >
		   state->slots[selected].input_sequence)
			selected = i;
	}

	return selected;
}

static int livido_async_find_runnable_slot(const livido_async_state_t *state)
{
	int selected = -1;

	if(livido_async_find_complete(state) >= 0)
		return -1;

	for(int i = 0; i < LIVIDO_ASYNC_SLOT_COUNT; i++) {
		if(state->slots[i].input_state != LIVIDO_ASYNC_INPUT_PENDING ||
		   state->slots[i].output_state != LIVIDO_ASYNC_OUTPUT_EMPTY)
			continue;
		if(selected < 0 ||
		   state->slots[i].input_sequence <
		   state->slots[selected].input_sequence)
			selected = i;
	}

	if(selected >= 0) {
		for(int i = 0; i < LIVIDO_ASYNC_SLOT_COUNT; i++) {
			if(state->slots[i].input_state == LIVIDO_ASYNC_INPUT_STAGING &&
			   state->slots[i].input_sequence <
			   state->slots[selected].input_sequence)
				return -1;
		}
	}

	return selected;
}

static int livido_async_reset_locked(livido_async_state_t *state)
{
	int active = state->processing_slot >= 0 || state->display_slot >= 0;

	for(int i = 0; i < LIVIDO_ASYNC_SLOT_COUNT; i++) {
		livido_async_slot_t *slot = &state->slots[i];
		if(slot->input_state != LIVIDO_ASYNC_INPUT_EMPTY)
			active = 1;
		if(slot->output_state != LIVIDO_ASYNC_OUTPUT_EMPTY)
			active = 1;
	}

	if(!active)
		return 0;

	state->generation++;
	state->display_slot = -1;

	for(int i = 0; i < LIVIDO_ASYNC_SLOT_COUNT; i++) {
		livido_async_slot_t *slot = &state->slots[i];
		if(slot->input_state == LIVIDO_ASYNC_INPUT_PENDING)
			slot->input_state = LIVIDO_ASYNC_INPUT_EMPTY;
		if(slot->output_state == LIVIDO_ASYNC_OUTPUT_COMPLETE ||
		   slot->output_state == LIVIDO_ASYNC_OUTPUT_DISPLAY)
			slot->output_state = LIVIDO_ASYNC_OUTPUT_EMPTY;
	}

	pthread_cond_broadcast(&state->cond);
	return 1;
}

static void livido_async_record_frame_locked(livido_async_state_t *state,
									 const VJFrame *frame)
{
	if(!frame)
		return;

	if(!state->have_frame_identity) {
		state->last_frame_num = frame->frame_num;
		state->have_frame_identity = 1;
		return;
	}

	long step = frame->frame_num - state->last_frame_num;
	state->last_frame_num = frame->frame_num;
	if(step == 0)
		return;

	if(state->have_frame_step && step != state->last_frame_step) {
		if(livido_async_reset_locked(state)) {
			veejay_msg(VEEJAY_MSG_DEBUG,
					   "Resetting non-real-time Livido buffers after timeline discontinuity");
		}
		state->have_frame_step = 0;
	}
	else if(!state->have_frame_step) {
		state->last_frame_step = step;
		state->have_frame_step = 1;
	}

	state->have_frame_identity = 1;
}

static int livido_async_process_slot(livido_async_state_t *state,
									 livido_async_slot_t *slot)
{
	if(!livido_async_prepare_output(state, slot)) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to allocate output storage for non-real-time Livido plugin");
		return LIVIDO_ERROR_MEMORY_ALLOCATION;
	}

	int lock_error = pthread_mutex_lock(&state->call_mutex);
	if(lock_error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to lock non-real-time Livido plugin call mutex: %s",
				   strerror(lock_error));
		return LIVIDO_ERROR_INTERNAL;
	}

	for(int i = 0; i < slot->parameter_count; i++)
		livido_set_parameter(state->instance, i, &slot->parameters[i]);

	for(int i = 0; i < state->num_inputs; i++) {
		VJFrame *input = &slot->inputs[i];
		if(i == 0 && slot->has_output &&
		   (state->flags & LIVIDO_FILTER_CAN_DO_INPLACE))
			input = &slot->output;
		if(!configure_channel(state->instance, "in_channels", i, input)) {
			pthread_mutex_unlock(&state->call_mutex);
			veejay_msg(VEEJAY_MSG_ERROR,
					   "Unable to bind owned input %d for non-real-time Livido plugin",
					   i);
			return LIVIDO_ERROR_NO_INPUT_CHANNELS;
		}
	}

	if(slot->has_output &&
	   !configure_channel(state->instance, "out_channels", 0, &slot->output))
	{
		pthread_mutex_unlock(&state->call_mutex);
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to bind owned output for non-real-time Livido plugin");
		return LIVIDO_ERROR_NO_OUTPUT_CHANNELS;
	}

	int process_error = livido_plug_process_internal(state->instance,
												 slot->timecode);
	int unlock_error = pthread_mutex_unlock(&state->call_mutex);
	if(unlock_error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to unlock non-real-time Livido plugin call mutex: %s",
				   strerror(unlock_error));
		if(process_error == LIVIDO_NO_ERROR)
			process_error = LIVIDO_ERROR_INTERNAL;
	}

	return process_error;
}

static void *livido_async_worker(void *data)
{
	livido_async_state_t *state = (livido_async_state_t*) data;

	for(;;) {
		int slot_index = -1;
		int wait_error = 0;

		pthread_mutex_lock(&state->mutex);
		while(!state->stop)
        {
            slot_index = livido_async_find_runnable_slot(state);
            
            if(slot_index >= 0) {
                break;
            }

            wait_error = pthread_cond_wait(&state->cond, &state->mutex);
            if(wait_error != 0) {
                veejay_msg(VEEJAY_MSG_ERROR,
                           "Non-real-time Livido worker wait failed: %s",
                           strerror(wait_error));
                state->stop = 1;
                break;
            }
        }

		if(state->stop) {
			pthread_mutex_unlock(&state->mutex);
			break;
		}

		livido_async_slot_t *slot = &state->slots[slot_index];
		slot->input_state = LIVIDO_ASYNC_INPUT_PROCESSING;
		state->processing_slot = slot_index;
		pthread_mutex_unlock(&state->mutex);

		int process_error = livido_async_process_slot(state, slot);

		pthread_mutex_lock(&state->mutex);
		slot->input_state = LIVIDO_ASYNC_INPUT_EMPTY;
		state->processing_slot = -1;
		if(!state->stop &&
		   process_error == LIVIDO_NO_ERROR &&
		   slot->input_generation == state->generation &&
		   slot->input_sequence > state->completed_sequence &&
		   slot->has_output)
		{
			slot->output_sequence = slot->input_sequence;
			slot->output_generation = slot->input_generation;
			slot->output_state = LIVIDO_ASYNC_OUTPUT_COMPLETE;
		}
		else {
			slot->output_state = LIVIDO_ASYNC_OUTPUT_EMPTY;
			if(process_error != LIVIDO_NO_ERROR) {
				veejay_msg(VEEJAY_MSG_ERROR,
						   "Non-real-time Livido plugin processing failed with error %d",
						   process_error);
			}
		}
		pthread_cond_broadcast(&state->cond);
		pthread_mutex_unlock(&state->mutex);
	}

	return NULL;
}

static void livido_async_slot_free(livido_async_slot_t *slot, int num_inputs)
{
	if(slot->input_storage) {
		for(int i = 0; i < num_inputs; i++)
			free(slot->input_storage[i]);
	}
	free(slot->inputs);
	free(slot->input_storage);
	free(slot->input_capacity);
	free(slot->output_storage);
	free(slot->parameters);
}

static int livido_async_slot_init(livido_async_slot_t *slot, int num_inputs,
								  int parameter_capacity)
{
	if(num_inputs > 0) {
		slot->inputs = (VJFrame*) vj_calloc(sizeof(VJFrame) * (size_t)num_inputs);
		slot->input_storage = (uint8_t**) vj_calloc(sizeof(uint8_t*) * (size_t)num_inputs);
		slot->input_capacity = (size_t*) vj_calloc(sizeof(size_t) * (size_t)num_inputs);
		if(!slot->inputs || !slot->input_storage || !slot->input_capacity)
			return 0;
	}

	if(parameter_capacity > 0) {
		slot->parameters = (int*) vj_calloc(sizeof(int) * (size_t)parameter_capacity);
		if(!slot->parameters)
			return 0;
	}

	return 1;
}

static void livido_async_state_free(livido_async_state_t *state)
{
	if(!state)
		return;

	for(int i = 0; i < LIVIDO_ASYNC_SLOT_COUNT; i++)
		livido_async_slot_free(&state->slots[i], state->num_inputs);

	int error = pthread_cond_destroy(&state->cond);
	if(error != 0)
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to destroy non-real-time Livido condition variable: %s",
				   strerror(error));
	error = pthread_mutex_destroy(&state->call_mutex);
	if(error != 0)
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to destroy non-real-time Livido call mutex: %s",
				   strerror(error));
	error = pthread_mutex_destroy(&state->mutex);
	if(error != 0)
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to destroy non-real-time Livido state mutex: %s",
				   strerror(error));
	free(state);
}

static int livido_async_state_create(void *instance, int flags)
{
	int num_inputs = vevo_property_num_elements(instance, "in_channels");
	int num_outputs = vevo_property_num_elements(instance, "out_channels");
	int parameter_capacity = vevo_property_num_elements(instance, "in_parameters");

	if(num_inputs < 0)
		num_inputs = 0;
	if(num_outputs < 0)
		num_outputs = 0;
	if(parameter_capacity < 0)
		parameter_capacity = 0;

	livido_async_state_t *state =
		(livido_async_state_t*) vj_calloc(sizeof(livido_async_state_t));
	if(!state) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to allocate non-real-time Livido instance state");
		return 0;
	}

	state->instance = instance;
	state->flags = flags;
	state->num_inputs = num_inputs;
	state->num_outputs = num_outputs;
	state->parameter_capacity = parameter_capacity;
	state->processing_slot = -1;
	state->display_slot = -1;
	state->generation = 1;

	int mutex_ready = 0;
	int call_mutex_ready = 0;
	int cond_ready = 0;
	int error = pthread_mutex_init(&state->mutex, NULL);
	if(error == 0)
		mutex_ready = 1;
	else
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to initialize non-real-time Livido state mutex: %s",
				   strerror(error));

	if(mutex_ready) {
		error = pthread_mutex_init(&state->call_mutex, NULL);
		if(error == 0)
			call_mutex_ready = 1;
		else
			veejay_msg(VEEJAY_MSG_ERROR,
					   "Unable to initialize non-real-time Livido call mutex: %s",
					   strerror(error));
	}

	if(call_mutex_ready) {
		error = pthread_cond_init(&state->cond, NULL);
		if(error == 0)
			cond_ready = 1;
		else
			veejay_msg(VEEJAY_MSG_ERROR,
					   "Unable to initialize non-real-time Livido condition variable: %s",
					   strerror(error));
	}

	if(!mutex_ready || !call_mutex_ready || !cond_ready)
		goto fail;

	for(int i = 0; i < LIVIDO_ASYNC_SLOT_COUNT; i++) {
		if(!livido_async_slot_init(&state->slots[i], num_inputs,
								   parameter_capacity))
		{
			veejay_msg(VEEJAY_MSG_ERROR,
					   "Unable to allocate non-real-time Livido frame slots");
			goto fail;
		}
	}

	error = pthread_mutex_lock(&livido_async_registry_mutex);
	if(error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to lock the non-real-time Livido registry during activation: %s",
				   strerror(error));
		goto fail;
	}
	state->next = livido_async_states;
	livido_async_states = state;

	setenv("OMP_THREAD_LIMIT", "1", 1);
    
    // 2. Default all parallel regions to 1 thread
    setenv("OMP_NUM_THREADS", "1", 1);
    
    // 3. Disable nested parallelism entirely
    setenv("OMP_MAX_ACTIVE_LEVELS", "0", 1);
    
    // 4. Force threads to yield immediately instead of spinning (if any slip through)
    setenv("OMP_WAIT_POLICY", "PASSIVE", 1);

	error = pthread_create(&state->thread, NULL, livido_async_worker, state);
	if(error != 0) {
		livido_async_states = state->next;
		state->next = NULL;
		pthread_mutex_unlock(&livido_async_registry_mutex);
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to create non-real-time Livido worker: %s",
				   strerror(error));
		goto fail;
	}
	state->thread_started = 1;
	pthread_mutex_unlock(&livido_async_registry_mutex);

	return 1;

fail:
	for(int i = 0; i < LIVIDO_ASYNC_SLOT_COUNT; i++)
		livido_async_slot_free(&state->slots[i], num_inputs);
	if(cond_ready)
		pthread_cond_destroy(&state->cond);
	if(call_mutex_ready)
		pthread_mutex_destroy(&state->call_mutex);
	if(mutex_ready)
		pthread_mutex_destroy(&state->mutex);
	free(state);
	return 0;
}

static int livido_async_state_stop(livido_async_state_t *state)
{
	if(!state)
		return 1;

	int error = pthread_mutex_lock(&state->mutex);
	if(error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to lock non-real-time Livido state for shutdown: %s",
				   strerror(error));
		return 0;
	}

	state->stop = 1;
	pthread_cond_broadcast(&state->cond);
	pthread_mutex_unlock(&state->mutex);

	if(state->thread_started) {
		error = pthread_join(state->thread, NULL);
		if(error != 0) {
			veejay_msg(VEEJAY_MSG_ERROR,
					   "Unable to join non-real-time Livido worker: %s",
					   strerror(error));
			return 0;
		}
		state->thread_started = 0;
	}

	return livido_async_wait_for_references(state);
}

int livido_plug_get_filter_flags(void *instance)
{
	int flags = 0;
	if(!instance)
		return 0;

	int error = pthread_mutex_lock(&livido_async_registry_mutex);
	if(error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to lock the non-real-time Livido registry for capability lookup: %s",
				   strerror(error));
		return 0;
	}

	livido_async_state_t *state = livido_async_lookup(instance);
	if(state)
		flags = state->flags;
	pthread_mutex_unlock(&livido_async_registry_mutex);
	if(state)
		return flags;

	vevo_property_get(instance, LIVIDO_FILTER_FLAGS_PROPERTY, 0, &flags);
	return flags;
}

int livido_plug_get_async_parameter_count(void *instance, int *count)
{
	if(!count)
		return 0;

	int known_async = 0;
	livido_async_state_t *state =
		livido_async_acquire(instance, &known_async);
	if(state) {
		*count = state->parameter_capacity;
		livido_async_release(state);
		return 1;
	}

	if(known_async) {
		*count = 0;
		return 1;
	}
	return 0;
}

int livido_plug_is_async(void *instance)
{
	int known_async = 0;
	livido_async_state_t *state =
		livido_async_acquire(instance, &known_async);
	livido_async_release(state);
	return known_async;
}

int livido_plug_call_lock(void *instance)
{
	int known_async = 0;
	livido_async_state_t *state =
		livido_async_acquire(instance, &known_async);
	if(!state)
		return known_async ? -1 : 0;

	int error = pthread_mutex_lock(&state->call_mutex);
	if(error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to serialize non-real-time Livido plugin call: %s",
				   strerror(error));
		livido_async_release(state);
		return -1;
	}

	error = pthread_mutex_lock(&state->mutex);
	if(error != 0) {
		pthread_mutex_unlock(&state->call_mutex);
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to inspect non-real-time Livido plugin state: %s",
				   strerror(error));
		livido_async_release(state);
		return -1;
	}
	int stopped = state->stop;
	pthread_mutex_unlock(&state->mutex);

	if(stopped) {
		pthread_mutex_unlock(&state->call_mutex);
		livido_async_release(state);
		return -1;
	}

	return 1;
}

void livido_plug_call_unlock(void *instance)
{
	int error = pthread_mutex_lock(&livido_async_registry_mutex);
	if(error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to lock the non-real-time Livido registry while releasing a plugin call: %s",
				   strerror(error));
		return;
	}
	livido_async_state_t *state = livido_async_lookup(instance);
	pthread_mutex_unlock(&livido_async_registry_mutex);
	if(!state) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Non-real-time Livido plugin call lost its instance state");
		return;
	}

	error = pthread_mutex_unlock(&state->call_mutex);
	if(error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to release non-real-time Livido plugin call mutex: %s",
				   strerror(error));
	}
	livido_async_release(state);
}

void livido_plug_reset(void *instance)
{
	livido_async_state_t *state = livido_async_acquire(instance, NULL);
	if(!state)
		return;

	int error = pthread_mutex_lock(&state->mutex);
	if(error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to lock non-real-time Livido state for reset: %s",
				   strerror(error));
		livido_async_release(state);
		return;
	}

	livido_async_reset_locked(state);
	state->have_frame_identity = 0;
	state->have_frame_step = 0;
	pthread_mutex_unlock(&state->mutex);
	livido_async_release(state);
}

int livido_plug_process_frame(void *instance, VJFrame **inputs, int num_inputs,
							  VJFrame *output, const int *args,
							  int num_params, double timecode)
{
	livido_async_state_t *state = livido_async_acquire(instance, NULL);
	if(!state || num_inputs != state->num_inputs ||
	   num_params < 0 || num_params > state->parameter_capacity ||
	   (num_inputs > 0 && !inputs) ||
	   (num_params > 0 && !args) ||
	   (state->num_outputs > 0 && !output))
	{
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Invalid non-real-time Livido frame submission");
		livido_async_release(state);
		return 0;
	}

	for(int i = 0; i < num_inputs; i++) {
		if(!inputs[i]) {
			veejay_msg(VEEJAY_MSG_ERROR,
					   "Missing input %d for non-real-time Livido frame submission",
					   i);
			livido_async_release(state);
			return 0;
		}
	}

	const VJFrame *identity_frame =
		(num_inputs > 0) ? inputs[0] : output;
	int slot_index = -1;
	int submission_ok = 1;
	int read_slot = -1;
	uint64_t read_output_generation = 0;
	int coalesced = 0;
	int wait_reported = 0;

	int error = pthread_mutex_lock(&state->mutex);
	if(error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to lock non-real-time Livido submission state: %s",
				   strerror(error));
		livido_async_release(state);
		return 0;
	}

	if(state->stop) {
		pthread_mutex_unlock(&state->mutex);
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Cannot submit a frame to a stopping non-real-time Livido plugin");
		livido_async_release(state);
		return 0;
	}

	livido_async_record_frame_locked(state, identity_frame);

	/* Publishing first is required before stateful backpressure can wait. */
	for(;;) {
		livido_async_publish_complete_locked(state);
		slot_index = livido_async_find_input_slot(state);
		if(slot_index >= 0)
			break;

		if(!(state->flags & LIVIDO_FILTER_NON_STATELESS)) {
			slot_index = livido_async_find_coalesce_slot(state);
			if(slot_index >= 0) {
				coalesced = 1;
				break;
			}

			veejay_msg(VEEJAY_MSG_ERROR,
					   "No reusable slot for stateless non-real-time Livido frame");
			submission_ok = 0;
			break;
		}

		if(!wait_reported) {
			state->backpressure_waits++;
			veejay_msg(VEEJAY_MSG_DEBUG,
					   "Applying bounded backpressure to stateful non-real-time Livido plugin");
			wait_reported = 1;
		}

		error = pthread_cond_wait(&state->cond, &state->mutex);
		if(error != 0) {
			veejay_msg(VEEJAY_MSG_ERROR,
					   "Stateful non-real-time Livido backpressure wait failed: %s",
					   strerror(error));
			submission_ok = 0;
			break;
		}
		if(state->stop) {
			submission_ok = 0;
			break;
		}
	}

	uint64_t submission_generation = state->generation;
	if(submission_ok) {
		livido_async_slot_t *slot = &state->slots[slot_index];
		slot->input_state = LIVIDO_ASYNC_INPUT_STAGING;
		slot->input_sequence = ++state->next_sequence;
		slot->input_generation = submission_generation;
		if(coalesced) {
			state->coalesced_frames++;
			if(state->coalesced_frames == 1 ||
			   (state->coalesced_frames % 120) == 0)
			{
				veejay_msg(VEEJAY_MSG_DEBUG,
						   "Coalesced %llu busy-frame submissions for stateless non-real-time Livido plugin",
						   (unsigned long long)state->coalesced_frames);
			}
		}
	}
	pthread_mutex_unlock(&state->mutex);

	if(submission_ok) {
		livido_async_slot_t *slot = &state->slots[slot_index];

		for(int i = 0; i < num_inputs; i++) {
			if(!livido_async_snapshot_frame(&slot->inputs[i],
										&slot->input_storage[i],
										&slot->input_capacity[i],
										inputs[i]))
			{
				veejay_msg(VEEJAY_MSG_ERROR,
						   "Unable to snapshot input %d for non-real-time Livido plugin",
						   i);
				submission_ok = 0;
				break;
			}
		}

		if(submission_ok && state->num_outputs > 0 &&
		   !livido_async_set_output_template(slot, output))
		{
			veejay_msg(VEEJAY_MSG_ERROR,
					   "Unable to snapshot output metadata for non-real-time Livido plugin");
			submission_ok = 0;
		}

		if(submission_ok && num_params > 0)
			veejay_memcpy(slot->parameters, args,
						  sizeof(int) * (size_t)num_params);

		slot->parameter_count = num_params;
		slot->has_output = state->num_outputs > 0;
		slot->timecode = timecode;
	}

	error = pthread_mutex_lock(&state->mutex);
	if(error != 0) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to publish non-real-time Livido frame submission: %s",
				   strerror(error));
		livido_async_release(state);
		return 0;
	}

	if(slot_index >= 0) {
		livido_async_slot_t *slot = &state->slots[slot_index];
		if(submission_ok && !state->stop &&
		   slot->input_generation == state->generation)
		{
			slot->input_state = LIVIDO_ASYNC_INPUT_PENDING;
		}
		else {
			slot->input_state = LIVIDO_ASYNC_INPUT_EMPTY;
			submission_ok = 0;
		}
		pthread_cond_broadcast(&state->cond);
	}

	livido_async_publish_complete_locked(state);
	if(state->display_slot >= 0) {
		livido_async_slot_t *display = &state->slots[state->display_slot];
		if(display->output_state == LIVIDO_ASYNC_OUTPUT_DISPLAY) {
			read_slot = state->display_slot;
			read_output_generation = display->output_generation;
			display->output_state = LIVIDO_ASYNC_OUTPUT_READING;
		}
	}
	pthread_mutex_unlock(&state->mutex);

	int output_ok = 1;
	if(read_slot >= 0) {
		output_ok = livido_async_copy_output(&state->slots[read_slot], output);
		if(!output_ok) {
			veejay_msg(VEEJAY_MSG_ERROR,
					   "Completed non-real-time Livido output does not match the current frame");
		}

		pthread_mutex_lock(&state->mutex);
		livido_async_slot_t *display = &state->slots[read_slot];
		if(display->output_state == LIVIDO_ASYNC_OUTPUT_READING) {
			if(output_ok && !state->stop &&
			   read_output_generation == state->generation &&
			   display->output_generation == read_output_generation)
			{
				display->output_state = LIVIDO_ASYNC_OUTPUT_DISPLAY;
				state->display_slot = read_slot;
			}
			else {
				display->output_state = LIVIDO_ASYNC_OUTPUT_EMPTY;
				if(state->display_slot == read_slot)
					state->display_slot = -1;
			}
		}
		pthread_cond_broadcast(&state->cond);
		pthread_mutex_unlock(&state->mutex);

		if(!output_ok)
			output_ok = livido_async_apply_fallback(inputs, num_inputs, output);
	}
	else {
		output_ok = livido_async_apply_fallback(inputs, num_inputs, output);
		if(!output_ok) {
			veejay_msg(VEEJAY_MSG_ERROR,
					   "Unable to apply first-frame fallback for non-real-time Livido plugin");
		}
	}

	livido_async_release(state);
	return submission_ok && output_ok;
}

int	livido_plug_parameter_set_text( void *parameter, void *value )
{
	char *new_val = ( (char*) value );
	int   len     = (new_val == NULL ? 0 : strlen( new_val ));
	if( len > 0 )
	{
		if( vevo_property_set( parameter, "value", LIVIDO_ATOM_TYPE_STRING, 1, value ) == VEVO_NO_ERROR )
			return 1;
	}
	return 0;
}

int	livido_plug_get_index_parameter_as_dbl( void *fx_instance, const char *key,int k, double *res )
{
	void *parameter = NULL;
	int error = vevo_property_get( fx_instance, key, k, &parameter );
	if(error != VEVO_NO_ERROR )
		return 0;
	int value = 0;
	error = vevo_property_get( parameter, "value", 0, &value );
	if( error == VEVO_NO_ERROR )
	{
		*res = value;
		return 0;
	}	
	return 1;
}
int	livido_plug_get_number_parameter_as_dbl( void *fx_instance,const char *key, int k, double *res )
{
	void *parameter = NULL;
	int error = vevo_property_get( fx_instance, key, k, &parameter );
	double value = 0.0;
	error = vevo_property_get( parameter, "value", 0, &value );
	if( error == VEVO_NO_ERROR )
	{
		*res = value;
		return 0;
	}
	return 1;
}
int	livido_plug_get_coord_parameter_as_dbl( void *fx_instance,const char *key, int k, double *res_x, double *res_y )
{
	void *parameter = NULL;
	int error = vevo_property_get( fx_instance, key, k, &parameter );
	double value[2];
	error = vevo_property_get( parameter, "value", 0, &value );
	if( error == VEVO_NO_ERROR )
	{
		*res_x = value[0];
		*res_y = value[1];
		return 0;
	}
	return 1;
}
int	livido_plug_parameter_get_range_dbl( void *fx_instance,const char *key, int k, double *min, double *max, int *dkind )
{
	void *parameter = NULL;
	int error = vevo_property_get( fx_instance, key, k, &parameter );
	if(error != VEVO_NO_ERROR )
		return 0;
	void *parameter_templ = NULL;
	error = vevo_property_get( parameter, "parent_template", 0, &parameter_templ );
/*	if(error  != VEVO_NO_ERROR )
	{
		*min = 0.0;
		*max = 1.0;
		*dkind =HOST_PARAM_NUMBER;
		veejay_msg(0, "No parent template in output parameter, working arround");
		return VEVO_NO_ERROR;
	}*/
	int kind = 0;
	error = vevo_property_get( parameter_templ, "HOST_kind", 0, &kind );
	int irange[2];

	if (kind == HOST_PARAM_NUMBER )
	{
		error = vevo_property_get( parameter_templ, "min",0, min );
		error = vevo_property_get( parameter_templ, "max",0, max );
		*dkind = HOST_PARAM_NUMBER;
		return VEVO_NO_ERROR;
	}	
	else if(kind == HOST_PARAM_INDEX )
	{
		error = vevo_property_get( parameter_templ, "min", 0 , &(irange[0]) );
		error = vevo_property_get( parameter_templ, "max", 0, &(irange[1]) );
		*min = (double) irange[0];
		*max = (double) irange[1];
		*dkind = HOST_PARAM_INDEX;
		return VEVO_NO_ERROR;
	}
	else if(kind == HOST_PARAM_WIDTH )
	{
		error = vevo_property_get( parameter_templ, "max", 0, &(irange[1]) );
		*min = 0.0;
		*max = (double) irange[1];
		*dkind = HOST_PARAM_WIDTH;   
		return VEVO_NO_ERROR;	
	}
	else if(kind == HOST_PARAM_HEIGHT )
	{
		error = vevo_property_get( parameter_templ, "max", 0, &(irange[1]) );
		*min = 0.0;
		*max = (double) irange[1];
		*dkind = HOST_PARAM_HEIGHT;
		return VEVO_NO_ERROR;
	}
		
	return 1;
}

int	livido_plug_parameter_set_number( void *parameter, void *value )
{
	double range[2];
	void *templ = NULL;
	if( vevo_property_get( parameter, "parent_template",0, &templ ) != VEVO_NO_ERROR )
		return 0;
	if( vevo_property_get( templ, "min", 0 , &(range[0]) ) != VEVO_NO_ERROR )
		return 0;
	if( vevo_property_get( templ, "max", 0, &(range[1]) ) != VEVO_NO_ERROR )
		return 0;
	double new_val = *((double*) value);
	if( new_val >= range[0] && new_val <= range[1] )
	{
		if( vevo_property_set( parameter, "value", VEVO_ATOM_TYPE_DOUBLE, 1, value ) == VEVO_NO_ERROR )
			return 1;
	}
	else
	{
		char *name = get_str_vevo(templ, "name");
		veejay_msg(0, "Parameter '%s' value %g out of range %g - %g", name,new_val, range[0],range[1]);
		free(name);
	}
	return 0;
}

int	livido_plug_parameter_set_index( void *parameter, void *value)
{
	int range[2];
	void *templ = NULL;
	if( vevo_property_get( parameter, "parent_template",0, &templ ) != VEVO_NO_ERROR )
		return 0;

	if( vevo_property_get( templ, "min", 0 , &(range[0]) ) != VEVO_NO_ERROR )
		return 0;

	if( vevo_property_get( templ, "max", 0, &(range[1]) ) != VEVO_NO_ERROR )
		return 0;

	int new_val = *((int*) value);
	if( new_val >= range[0] && new_val <= range[1] )
	{
		if( vevo_property_set( parameter, "value", VEVO_ATOM_TYPE_INT, 1, value ) == VEVO_NO_ERROR )
			return 1; 
	}
	else
	{
		char *name = get_str_vevo(templ, "name");
		veejay_msg(0, "Parameter '%s' value %d out of range %d - %d", name,new_val, range[0],range[1]);
		free(name);
	}
	return 0;
}

int	livido_plug_parameter_set_bool( void *parameter, void *value )
{	
	void *templ = NULL;
	int new_val = *((int*) value);

	if( new_val >= 0 && new_val <= 1 )
	{
		if( vevo_property_set( parameter, "value", VEVO_ATOM_TYPE_BOOL, 1, value ) == VEVO_NO_ERROR )
			return 1;
	}
	else
	{	
		char *name = get_str_vevo(templ, "name");
		veejay_msg(0, "Parameter '%s' value %d out of range 0 - 1 (TRUE)", name,new_val);
		free(name);
	}
	return 0;
}

int	livido_plug_parameter_set_color( void *parameter,void *value )
{
	veejay_msg(0,"%s: FIXME not implemented", __FUNCTION__);
//	vevo_property_set( parameter, "value", VEVO_ATOM_TYPE_DOUBLE, 4, value );
	return 0;
}

int	livido_plug_parameter_set_coord( void *parameter, void *value )
{
	veejay_msg(0,"%s: FIXME not implemented", __FUNCTION__);
//	vevo_property_set( parameter, "value", LIVIDO_ATOM_TYPE_DOUBLE, 2, value );
	return 0;
}

static	int	livido_pname_to_host_kind( const char *str )
{
	if (strcasecmp( str, "NUMBER" ) == 0 ) {
		return HOST_PARAM_NUMBER;
	}
	else if(strcasecmp(str, "INDEX" ) == 0 ) {
		return HOST_PARAM_INDEX;
	}
	else if(strcasecmp(str, "SWITCH") == 0 ) {
		return HOST_PARAM_SWITCH;
	}
	else if(strcasecmp(str, "COORD") == 0 ) {
		return HOST_PARAM_COORD;
	}
	else if(strcasecmp(str, "COLOR") == 0 ) {
		return HOST_PARAM_COLOR;
	}
	else if(strcasecmp(str, "TEXT") == 0 ) {
		return HOST_PARAM_TEXT;
	}
	else if(strcasecmp(str, "WIDTH") == 0 ) {
		return HOST_PARAM_WIDTH;
	}
	else if(strcasecmp(str,"HEIGHT") == 0 ) {
		return HOST_PARAM_HEIGHT;
	}
	return 0;
}

static	int	livido_scan_out_parameters( void *plugin , void *plugger_port)
{
	int n = 0;
	int NP = vevo_property_num_elements( plugin , "out_parameter_templates");

	if( NP <= 0 )
		return 0;

    char key[16];
	for( n = 0; n < NP; n ++ )
	{
		void *param = NULL;

		if( vevo_property_get( plugin, "out_parameter_templates", n, &param ) != VEVO_NO_ERROR )
			continue;

		snprintf(key,sizeof(key), "p%02d", n );

		int ikind = 0;
		char *kind = vevo_property_get_string( param, "kind" );

		ikind = livido_pname_to_host_kind(kind);

		vevo_property_set( param, "HOST_kind", VEVO_ATOM_TYPE_INT,1,&ikind );	
		void *vje_port = vpn( VEVO_VJE_PORT );
		vevo_property_set( plugger_port, key, LIVIDO_ATOM_TYPE_PORTPTR,1, &vje_port );

		free(kind);
	}
	return NP;
}

static	int	livido_scan_parameters( void *plugin, void *plugger_port, int w, int h )
{
	int n = 0;
	int vj_np = 0;
	int NP = vevo_property_num_elements( plugin , "in_parameter_templates");

	if( NP <= 0 )
		return 0;

    char key[20];
	for( n = 0; n < NP; n ++ )
	{
		void *param = NULL;

		if( vevo_property_get( plugin, "in_parameter_templates", n, &param ) != VEVO_NO_ERROR )
			continue;
		snprintf(key,sizeof(key),"p%02d", n );

		int ikind = 0;
		char *kind = get_str_vevo( param, "kind" );

		void *vje_port = vpn( VEVO_VJE_PORT );
		int tmp[4];  
		double dtmp[4];

		vevo_property_set( plugger_port, key, LIVIDO_ATOM_TYPE_PORTPTR,1, &vje_port );

		if(strcasecmp(kind, "NUMBER") == 0 ) {
			ikind = HOST_PARAM_NUMBER; vj_np ++;
			clone_prop_vevo( param, vje_port, "default", "value" );
			clone_prop_vevo( param, vje_port, "default", "default" );
			clone_prop_vevo( param, vje_port, "min", "min" );
			clone_prop_vevo( param, vje_port, "max", "max" );
		} else if (strcasecmp(kind, "INDEX") == 0 ) {
			ikind = HOST_PARAM_INDEX; vj_np ++;
			clone_prop_vevo( param, vje_port, "default", "value" );
			clone_prop_vevo( param, vje_port, "default", "default" );
			clone_prop_vevo( param, vje_port, "min", "min" );
			clone_prop_vevo( param, vje_port, "max", "max" );
		} else if( strcasecmp(kind, "WIDTH") == 0 ) {
			ikind = HOST_PARAM_WIDTH; vj_np++;
			tmp[0] = 0;
			tmp[1] = w;
			vevo_property_set( vje_port, "min", VEVO_ATOM_TYPE_INT,1,&tmp[0] );
			vevo_property_set( vje_port, "max",VEVO_ATOM_TYPE_INT,1,&tmp[1] );
			int err = vevo_property_get( param,"default", 0, &tmp[2]);
			if( err ) {
				vevo_property_set( vje_port, "default", VEVO_ATOM_TYPE_INT,1,&tmp[1]);
			} else {
				vevo_property_set( vje_port, "default", VEVO_ATOM_TYPE_INT,1,&tmp[2]);
			}
			clone_prop_vevo( vje_port, vje_port, "default", "value" );
			clone_prop_vevo( vje_port, param, "max", "max" );
			clone_prop_vevo( vje_port, param, "min", "min" );
			clone_prop_vevo( vje_port, param, "default" ,"default" );
		} else if( strcasecmp(kind, "HEIGHT") == 0 ) {
			ikind = HOST_PARAM_HEIGHT; vj_np++;
			tmp[0] = 0;
			tmp[1] = h;
			vevo_property_set( vje_port, "min", VEVO_ATOM_TYPE_INT,1,&tmp[0] );
			vevo_property_set( vje_port, "max",VEVO_ATOM_TYPE_INT,1,&tmp[1] );
			int err = vevo_property_get( param, "default", 0, &tmp[2] );
			if( err )  {
				vevo_property_set( vje_port, "default", VEVO_ATOM_TYPE_INT,1,&tmp[1]);
			} else {
				vevo_property_set( vje_port, "default", VEVO_ATOM_TYPE_INT,1,&tmp[2]);
			}
			clone_prop_vevo( vje_port, vje_port, "default", "value" );
			clone_prop_vevo( vje_port, param, "max", "max" );
			clone_prop_vevo( vje_port, param, "min", "min" );
			clone_prop_vevo( vje_port, param, "default" ,"default" );
		} else if (strcasecmp(kind, "SWITCH") == 0 ) {
			ikind = HOST_PARAM_SWITCH; vj_np ++;
			clone_prop_vevo( param, vje_port, "default", "value" );
			clone_prop_vevo( param, vje_port, "default", "default" );
			tmp[0] = 0; tmp[1] = 1;
			vevo_property_set(vje_port, "min", VEVO_ATOM_TYPE_BOOL,1, &tmp[0] );
			vevo_property_set(vje_port, "max", VEVO_ATOM_TYPE_BOOL,1, &tmp[1] );
		} else if (strcasecmp(kind, "COORD" ) == 0 ) {
			ikind = HOST_PARAM_COORD; vj_np += 2;
			dtmp[0] = 0.0; dtmp[1] = 0.0;
			vevo_property_set(vje_port, "min", VEVO_ATOM_TYPE_DOUBLE,1, &dtmp[0] );
			dtmp[1] = 1.0; dtmp[0] = 1.0;
			vevo_property_set(vje_port, "max", VEVO_ATOM_TYPE_DOUBLE,1, &dtmp[1] );
			double *dv = get_dbl_arr_vevo( vje_port, "default" );
			vevo_property_set(vje_port, "default", VEVO_ATOM_TYPE_DOUBLE,2,&dv );
			vevo_property_set(vje_port, "value", VEVO_ATOM_TYPE_DOUBLE,2,&dv );
			free(dv);
		} else if (strcasecmp(kind, "COLOR" ) == 0 ) {
			ikind = HOST_PARAM_COLOR; vj_np += 3; // fixme, should be 4
			dtmp[0] = 0.0; dtmp[1] = 0.0; dtmp[2] = 0.0; dtmp[3] = 0.0;
			vevo_property_set(vje_port, "min", VEVO_ATOM_TYPE_DOUBLE,4, &dtmp );
			dtmp[0] = 1.0; dtmp[1] = 1.0; dtmp[2] = 1.0; dtmp[3] = 1.0;
			vevo_property_set(vje_port, "max", VEVO_ATOM_TYPE_DOUBLE,4, &dtmp );
			double *dv = get_dbl_arr_vevo( vje_port, "default" );
			vevo_property_set(vje_port, "default", VEVO_ATOM_TYPE_DOUBLE,2,&dv );
			free(dv);
		} else if (strcasecmp(kind, "TEXT" ) == 0 ) {
			ikind = HOST_PARAM_TEXT; 
			vj_np ++;  
		}
		clone_prop_vevo( param, vje_port, "name", "name" );

		vevo_property_set( param, "HOST_kind", VEVO_ATOM_TYPE_INT,1,&ikind );	
		vevo_property_set( vje_port, "HOST_kind", VEVO_ATOM_TYPE_INT,1,&ikind );

		free(kind);	
	}

	return vj_np;
}

static	int	init_parameter_port(livido_port_t *ptr, livido_port_t *in_param )
{
	int kind = 0;
	int error = vevo_property_get( ptr, "HOST_kind", 0, &kind );

	if( error != VEVO_NO_ERROR )
	{
		veejay_msg(0, "\tProperty 'HOST_kind' not set in parameter");
		return 0;
	}
	livido_set_parameter_f pctrl;
				
	switch(kind)
	{
		case HOST_PARAM_INDEX:
		case HOST_PARAM_NUMBER:
		case HOST_PARAM_WIDTH:
		case HOST_PARAM_HEIGHT:
		case HOST_PARAM_SWITCH:
			pctrl = livido_plug_parameter_set_index; 
			break;
//		case HOST_PARAM_NUMBER:
//			pctrl = livido_plug_parameter_set_number; break;
//		case HOST_PARAM_SWITCH:
//			pctrl = livido_plug_parameter_set_bool; break;
//@ TODO: these are not yet supported
//		case HOST_PARAM_COORD:
//			pctrl = livido_plug_parameter_set_coord; break;
//		case HOST_PARAM_COLOR:
//			pctrl = livido_plug_parameter_set_color; break;
//		case HOST_PARAM_TEXT:
//			pctrl = livido_plug_parameter_set_text; break;
		default:
			return 0;
			break;
	}
	
	vevo_property_set( in_param, "HOST_parameter_func", LIVIDO_ATOM_TYPE_VOIDPTR,1,&pctrl );

    void *priv = NULL;
    error = vevo_property_get( ptr, "PLUGIN_param_private", 0, &priv );
    if( error == VEVO_NO_ERROR ) {
        vevo_property_set( in_param, "PLUGIN_param_private", LIVIDO_ATOM_TYPE_VOIDPTR,1,&priv );
    }
	return 1;
}

static	int	match_palette(livido_port_t *ptr, int palette )
{
	int p;
	int np = vevo_property_num_elements( ptr, "palette_list" );
	for( p = 0; p < np; p ++ )
	{
		int ppalette = 0;
		vevo_property_get( ptr, "palette_list", p, &ppalette );
		if( palette == ppalette )
			return 1;
	}
	return 0;
}

static	int	find_cheap_palette(livido_port_t *c, livido_port_t *ptr)
{
	int palette = LIVIDO_PALETTE_YUV444P;
	if( match_palette(ptr,palette ))
	{
		vevo_property_set( c, "current_palette", LIVIDO_ATOM_TYPE_INT,1, &palette );
		return 1;
	}
	else {
		palette = LIVIDO_PALETTE_YUV422P;
		if( match_palette(ptr,palette ))
		{
			vevo_property_set( c, "current_palette", LIVIDO_ATOM_TYPE_INT,1, &palette );
			return 1;
		}
		else 
		{
			palette = LIVIDO_PALETTE_YUVA8888;
			if( match_palette(ptr,palette ))
			{
				vevo_property_set( c, "current_palette", LIVIDO_ATOM_TYPE_INT,1, &palette );
				return 1;
			}
		}
	}
	return 0;
}

static	int	init_channel_port(livido_port_t *ptr, livido_port_t *in_channel, int w, int h)
{
	int np = vevo_property_num_elements( ptr, "palette_list" );
	int plug_pp = 0;
	int flags = 0;
	int error = vevo_property_get( ptr, "palette_list", 0, &plug_pp );
	
	if( np < 0 )
		return 0;
	
	if( match_palette( ptr, pref_palette_ ))
		vevo_property_set( in_channel, "current_palette", LIVIDO_ATOM_TYPE_INT,1,&pref_palette_ );
	else
	{
		if(!find_cheap_palette(in_channel ,ptr))
		{
			veejay_msg(VEEJAY_MSG_WARNING, "No support for any palette in plugin");
			return 0;
		}
	}	

	error = vevo_property_get( ptr, "flags", 0, &flags );
	livido_property_set( in_channel, "width",  LIVIDO_ATOM_TYPE_INT,1,&w );
	livido_property_set( in_channel, "height", LIVIDO_ATOM_TYPE_INT,1,&h );
	livido_property_set( in_channel, "flags",  LIVIDO_ATOM_TYPE_INT,1,&flags );


	error = vevo_property_get( in_channel, "current_palette",0,NULL );
	if( error != LIVIDO_NO_ERROR )
	{
		veejay_msg(0, "No suitable palette found");
		return 0;
	}
	
	return 1;
}

static	int	init_ports_from_template( livido_port_t *filter_instance, livido_port_t *template, int id, const char *name, const char *iname, int w, int h, int host_palette )
{
	int num = 0;
        int i;
	int error = 0;
	error = livido_property_get( template, name, 0, NULL);

	if( error != VEVO_NO_ERROR )
		return 0;
	
    num = livido_property_num_elements( template, name );

    if(num <= 0)
		return 0;

    livido_port_t *in_channels[num];

	for( i = 0; i < num;  i ++ )
    {
		livido_port_t *ptr = NULL;
		error = livido_property_get( template, name, i, &ptr );
	
		in_channels[i] = vpn( id ); 
		livido_property_set( in_channels[i], "parent_template",LIVIDO_ATOM_TYPE_VOIDPTR,1, &ptr);
//		livido_property_soft_reference( in_channels[i], "parent_template" );
		if( id == LIVIDO_PORT_TYPE_CHANNEL )
		{
			if(!init_channel_port( ptr,in_channels[i],w,h))
			{
				   veejay_msg(0,"Unable to initialize output channel %d ",i );
			       return -1;	
			}
		}
		else if( id == LIVIDO_PORT_TYPE_PARAMETER )
		{
			if(!init_parameter_port( ptr, in_channels[i] ))
			{
				veejay_msg(0, "Unable to initialize output parameter %d", i);
				return -1;
			}
		}
	}
        
	livido_property_set( filter_instance, iname, LIVIDO_ATOM_TYPE_PORTPTR,num, in_channels );

	return num;
}

char	*livido_describe_parameter_format_osc( void *instance, int p )
{
	void *param = NULL;
	void *param_templ = NULL;
	int error = vevo_property_get( instance, "in_parameters", p, &param );
	if(error != VEVO_NO_ERROR )
	{
		veejay_msg(0, "Input parameter %d does not exist ", p );
		return NULL;
	}
	error = vevo_property_get( param, "parent_template",0,&param_templ);
	int kind = 0;
	error = vevo_property_get( param_templ, "HOST_kind",0,&kind );
	char fmt[5];
	fmt[1] = '\0';

	switch(kind)
	{
		case HOST_PARAM_WIDTH:
		case HOST_PARAM_HEIGHT:
		case HOST_PARAM_INDEX:
			fmt[0] = 'i';
			break;
		case HOST_PARAM_NUMBER:
			fmt[0] = 'd';
			break;
		case HOST_PARAM_SWITCH:
			fmt[0] = 'i';
			break;
		case HOST_PARAM_COORD:
			fmt[0] = 'd';
			fmt[1] = 'd';
			break;
		case HOST_PARAM_COLOR:
			fmt[0] = 'd';
			fmt[1] = 'd';
			fmt[2] = 'd';
			break;
		case HOST_PARAM_TEXT:
			fmt[0] = 's';
			break;
		default:
			break;
	}

	char *res = vj_strdup( fmt );
	return res;
}

void	livido_plug_free_namespace( void *fx_instance , void *data )
{
	void *osc_namespace = NULL;
	vevo_property_get( fx_instance, "HOST_osc",0,&osc_namespace);
//@TODO OMC
//	if( error == VEVO_NO_ERROR)
//		veejay_osc_del_methods( data,osc_namespace,fx_instance, fx_instance );
}

int	livido_plug_build_namespace( void *plugin_template , int entry_id, void *fx_instance , void *data, int sample_id,
		generic_osc_cb_f osc_cb_f, void *osc_data)
{
	void *plug_info = NULL;
	void *filter_templ = NULL;
	if( vevo_property_get( plugin_template, "instance", 0, &plug_info) != VEVO_NO_ERROR )
		return 0;

	if( vevo_property_get( plug_info, "filters",0,&filter_templ) != VEVO_NO_ERROR )
		return 0;
	if( vevo_property_set( fx_instance, "HOST_osc_cb", VEVO_ATOM_TYPE_VOIDPTR,1,&osc_cb_f ) != VEVO_NO_ERROR )
		return 0;
	if( vevo_property_set( fx_instance, "HOST_data", VEVO_ATOM_TYPE_VOIDPTR,1,&osc_data ) != VEVO_NO_ERROR )
		return 0;

	int n_in = vevo_property_num_elements( filter_templ, "in_parameter_templates" );
	int i;
	if( n_in <= 0)
	{
		return 0; // no namespace needed yet
	}
	
	char *plug_name = get_str_vevo( filter_templ, "name" );

	char base[256];
	char mpath[256];
	void *osc_namespace = vpn(VEVO_ANONYMOUS_PORT);
	
	for( i = 0; i < n_in ; i ++ )
	{
		void *parameter = NULL;
		vevo_property_get( fx_instance, "in_parameters", i, &parameter );	
	
		void *param_templ = NULL;
		if( vevo_property_get( filter_templ, "in_parameter_templates", i, &param_templ ) != VEVO_NO_ERROR )
			continue;
		char *param_name = get_str_vevo( param_templ, "name" );
		char *descrip    = get_str_vevo( param_templ, "description" );
		
		snprintf(base, sizeof(base), "/sample_%d/fx_%d/%s",
				sample_id,
				entry_id,
				plug_name );
		snprintf(mpath, sizeof(mpath), "/sample_%d/fx_%d/%s/%s",
				sample_id,
				entry_id,
				plug_name,
		      		param_name );


		char *format = livido_describe_parameter_format_osc( fx_instance ,i);
		
		char *ppo = veejay_valid_osc_name( mpath );
		vevo_property_set( parameter, "HOST_osc_path",VEVO_ATOM_TYPE_STRING, 1, &ppo );	
		vevo_property_set( parameter, "HOST_osc_types", VEVO_ATOM_TYPE_STRING,1,&format );
		free(ppo);
/*		plugin_new_event(
				data,
				osc_namespace,
				fx_instance,
				base,
				param_name,
			        format,
				NULL,
				descrip,
			       	NULL,
				i,
			       	param_templ	);
		*/
		//@ TODO: OMC
		free(param_name);
		free(format);
		free(descrip);
	}	

	vevo_property_set( fx_instance, "HOST_osc", LIVIDO_ATOM_TYPE_PORTPTR,1,&osc_namespace);
	free(plug_name);
	
	veejay_msg(0, "End of OSC namespace");	
	return n_in;
}

void	*livido_get_name_space( void *instance )
{
	void *space = NULL;
	int error = vevo_property_get( instance, "HOST_osc", 0, &space );
	if( error != VEVO_NO_ERROR )
		return NULL;
	return space;
}

/* initialize a plugin */
void	*livido_plug_init(void *plugin,int w, int h, int base_fmt_ , int org_fmt_, int read_plug_cfg)
{
	void *plug_info = NULL;
	void *filter_templ = NULL;
	int filter_flags = 0;
	
    read_plugin_configuration = read_plug_cfg;
	(void) base_fmt_;

	if( vevo_property_get( plugin, "instance", 0, &plug_info) != VEVO_NO_ERROR ) {
		veejay_msg(0, "Not a Livido plugin");
		return NULL;
	}
	if( vevo_property_get( plug_info, "filters",0,&filter_templ) != VEVO_NO_ERROR ) {
		veejay_msg(0, "Not a Livido filter");
		return NULL;
	}
	vevo_property_get(filter_templ, "flags", 0, &filter_flags);
	void *filter_instance = vpn( LIVIDO_PORT_TYPE_FILTER_INSTANCE );
	vevo_property_set(filter_instance, LIVIDO_FILTER_FLAGS_PROPERTY,
					  LIVIDO_ATOM_TYPE_INT, 1, &filter_flags);
	init_ports_from_template(
			filter_instance, filter_templ,
			LIVIDO_PORT_TYPE_CHANNEL,
			"in_channel_templates", "in_channels",
			w,h, 0);

	init_ports_from_template( 
			filter_instance, filter_templ,
			LIVIDO_PORT_TYPE_CHANNEL,
			"out_channel_templates", "out_channels",
			w,h, 0 );
	
	init_ports_from_template( 
			filter_instance, filter_templ,
			LIVIDO_PORT_TYPE_PARAMETER,
			"in_parameter_templates", "in_parameters",
			w,h, 0 );

	init_ports_from_template(
			filter_instance, filter_templ,
			LIVIDO_PORT_TYPE_PARAMETER,
			"out_parameter_templates", "out_parameters",
			w,h,0 );

	//@ call livido init
	livido_init_f init_f;
	vevo_property_get( filter_templ, "init_func", 0, &init_f );
	int fullrange = ( (org_fmt_ == PIX_FMT_YUVJ420P || org_fmt_ == PIX_FMT_YUVJ422P || org_fmt_ == PIX_FMT_YUVJ444P) ? 1: 0 );
	vevo_property_set( filter_instance, 
					"HOST_fullrange",
					VEVO_ATOM_TYPE_INT,
					1,
					&fullrange );

	vevo_property_set( filter_instance,
					"HOST_format",
					VEVO_ATOM_TYPE_INT,
					1,
					&org_fmt_);

	int shmid = 0;
	if( vevo_property_get( filter_templ, "HOST_shmid", 0,&shmid ) == VEVO_NO_ERROR )
	{
		shmid = vj_shm_get_id(); //@ put in HOST value
		vevo_property_set( filter_instance,"HOST_shmid", VEVO_ATOM_TYPE_INT,1,&shmid );
		veejay_msg( VEEJAY_MSG_INFO, "Hooking up plugin to shared resource %d", shmid);
	} 

	if( (*init_f)( (livido_port_t*) filter_instance ) != LIVIDO_NO_ERROR ) {
		livido_port_recursive_free( filter_instance );
		
		char *plugin_name =  get_str_vevo( filter_templ, "name" );
		veejay_msg(VEEJAY_MSG_WARNING, "Unable to initialize LiViDO plugin '%s'", plugin_name);
	    if(plugin_name)
			free(plugin_name);	   
		return NULL;
	}

	//@ ok, finish
	vevo_property_set( filter_instance, "filter_templ", VEVO_ATOM_TYPE_VOIDPTR,1, &filter_templ );

	//@ prepare function pointers for plugloader to call
	generic_process_f	gpf = livido_plug_process;
	vevo_property_set( filter_instance, "HOST_plugin_process_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gpf ); 	
	generic_push_channel_f		gpu = livido_push_channel;
	vevo_property_set( filter_instance, "HOST_plugin_push_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gpu );
	generic_default_values_f	gdv = livido_plug_retrieve_values;
	vevo_property_set( filter_instance, "HOST_plugin_defaults_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gdv );
	generic_push_parameter_f	gpp = livido_set_parameter;
	vevo_property_set( filter_instance, "HOST_plugin_param_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gpp );
	generic_clone_parameter_f	gcc = livido_clone_parameter;
	vevo_property_set( filter_instance, "HOST_plugin_param_clone_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gcc );	
	generic_reverse_clone_parameter_f grc = livido_reverse_clone_parameter;
	vevo_property_set( filter_instance, "HOST_plugin_param_reverse_f", VEVO_ATOM_TYPE_VOIDPTR,1,&grc );
	generic_reverse_clone_out_parameter_f gro = livido_plug_read_output_parameters;
	vevo_property_set( filter_instance, "HOST_plugin_out_param_reverse_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gro );

	
	generic_deinit_f		gin = livido_plug_deinit;
	vevo_property_set( filter_instance, "HOST_plugin_deinit_f", VEVO_ATOM_TYPE_VOIDPTR,1,&gin);

	if((filter_flags & LIVIDO_FILTER_NON_REALTIME) &&
	   !livido_async_state_create(filter_instance, filter_flags))
	{
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Unable to initialize asynchronous host processing for Livido plugin");
		livido_plug_deinit(filter_instance);
		return NULL;
	}

	return filter_instance;
}


static void	livido_push_channel_local( void *instance,const char *key, int n, VJFrame *frame ) // in_channels / out_channels
{
	configure_channel( instance, key, n, frame );
}

void	livido_push_channel( void *instance,int n,int dir, VJFrame *frame ) // in_channels / out_channels
{

	const char *key = (dir == 0 ? "in_channels" : "out_channels" );
	livido_push_channel_local(instance, key, n, frame );
}

static int livido_plug_process_internal(void *instance, double time_code)
{
	void *filter_templ = NULL;
	if(vevo_property_get(instance, "filter_templ", 0, &filter_templ) != VEVO_NO_ERROR)
		return LIVIDO_ERROR_INTERNAL;

	livido_process_f process = NULL;
	if(vevo_property_get(filter_templ, "process_func", 0, &process) != VEVO_NO_ERROR ||
	   !process)
		return LIVIDO_ERROR_INTERNAL;

	int process_error = (*process)(instance, time_code);
	if(process_error != LIVIDO_NO_ERROR)
		return process_error;

	int num_outputs = vevo_property_num_elements(instance, "out_channels");
	if(num_outputs <= 0)
		return LIVIDO_NO_ERROR;

	void *channel = NULL;
	//see if output channel needs downsampling
	int error = vevo_property_get( instance, "out_channels", 0, &channel );

	if( error != LIVIDO_NO_ERROR )
		return LIVIDO_ERROR_NO_OUTPUT_CHANNELS;

	int current_palette = 0;
	vevo_property_get( channel, "current_palette", 0, &current_palette );
	if( current_palette != pref_palette_ ) {
		switch( current_palette ) {
            default: {
				VJFrame frame; VJFrame *f = &frame;
				vevo_property_get( channel, "width", 0, &(f->width));
				vevo_property_get( channel, "height", 0, &(f->height));
				int i;
				for( i = 0; i < 4; i ++ ) {
					vevo_property_get( channel, "pixel_data", i, &(f->data[i]));
				}
                     }
			break;
		}
	}

	return LIVIDO_NO_ERROR;
}

void	livido_plug_process( void *instance, double time_code )
{
	int process_error = livido_plug_process_internal(instance, time_code);
	if(process_error != LIVIDO_NO_ERROR) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Livido plugin processing failed with error %d",
				   process_error);
	}
}

void	livido_plug_deinit( void *instance )
{
	livido_async_state_t *state = livido_async_begin_destroy(instance);
	if(state && !livido_async_state_stop(state)) {
		veejay_msg(VEEJAY_MSG_ERROR,
				   "Leaving Livido plugin allocated because its worker did not stop safely");
		return;
	}

	if(state) {
		int lock_error = pthread_mutex_lock(&state->call_mutex);
		if(lock_error != 0) {
			veejay_msg(VEEJAY_MSG_ERROR,
					   "Leaving Livido plugin allocated because teardown serialization failed: %s",
					   strerror(lock_error));
			return;
		}
	}

	livido_deinit_f deinit;
	void *filter_templ = NULL;
	if( vevo_property_get( instance, "filter_templ",0,&filter_templ) == VEVO_NO_ERROR ) {
		if( vevo_property_get( filter_templ, "deinit_func", 0, &deinit ) == VEVO_NO_ERROR ) {
			int deinit_error = (*deinit)( instance );
			if(deinit_error != LIVIDO_NO_ERROR) {
				veejay_msg(VEEJAY_MSG_ERROR,
						   "Livido plugin deinitialization failed with error %d",
						   deinit_error);
			}
		}
	}

	if(state) {
		pthread_mutex_unlock(&state->call_mutex);
		livido_port_recursive_free(instance);
		if(!livido_async_unregister(state)) {
			veejay_msg(VEEJAY_MSG_ERROR,
					   "Leaving non-real-time Livido host state allocated because it could not be detached");
			return;
		}
		livido_async_state_free(state);
		return;
	}

	livido_port_recursive_free( instance );
	instance = NULL;
}

//get plugin defaults
void	livido_plug_retrieve_values( void *instance, void *fx_values )
{
	int vj_np = vevo_property_num_elements( instance, "in_parameters" );
	int i;
	char vkey[16];
	for( i = 0; i < vj_np; i ++ )
	{
		void *param = NULL;
		void *param_templ = NULL;
		if(vevo_property_get( instance, "in_parameters", i, &param)==VEVO_NO_ERROR) {
			if( vevo_property_get( param, "parent_template", 0, &param_templ ) == VEVO_NO_ERROR ) {
				snprintf(vkey,sizeof(vkey), "p%02d", i );
				clone_prop_vevo( param_templ, fx_values, "default", vkey );
			}
		}
	}
}

int	livido_plug_read_output_parameters( void *instance, void *fx_values )
{
	int np = vevo_property_num_elements( instance, "out_parameters" );
	int i;

	if(np <= 0)
		return 0;

    char vkey[16];
	for( i = 0; i < np ; i ++ )
	{
		void *param = NULL;

		if( vevo_property_get( instance, "out_parameters", i, &param ) != VEVO_NO_ERROR )
			continue;

		snprintf(vkey,sizeof(vkey), "p%02d", i );
		clone_prop_vevo( param, fx_values, "value", vkey);

	}
	return 1;
}
char	*livido_describe_parameter_format( void *instance, int p )
{
	void *param = NULL;
	void *param_templ = NULL;
	if( vevo_property_get( instance, "in_parameters", p, &param ) != VEVO_NO_ERROR )
		return NULL;
	if( vevo_property_get( param, "parent_template",0,&param_templ) != VEVO_NO_ERROR )
		return NULL;

	int kind = 0;
	vevo_property_get( param_templ, "HOST_kind",0,&kind );

//	int n_elems = vevo_property_num_elements( param, "value" );

	char fmt[4];
	fmt[1] = '\0';

	switch(kind)
	{
		case HOST_PARAM_WIDTH:
		case HOST_PARAM_HEIGHT:
		case HOST_PARAM_INDEX:
			fmt[0] = 'd';
			break;
		case HOST_PARAM_NUMBER:
			fmt[0] = 'g';
			break;
		case HOST_PARAM_SWITCH:
			fmt[0] = 'd';
			break;
		case HOST_PARAM_COORD:
			fmt[0] = 'g';
			fmt[1] = 'g';
			fmt[2] = '\0';
			break;
		case HOST_PARAM_COLOR:
			fmt[0] = 'g';
			fmt[1] = 'g';
			fmt[2] = 'g';
			fmt[3] = '\0'; 
			break;
		case HOST_PARAM_TEXT:
			fmt[0] = 's';
			break;
	}

	return vj_strdup( fmt );
}


int	livido_set_parameter_from_string( void *instance, int p, const char *str, void *fx_values )
{
	void *param = NULL;
	void *param_templ = NULL;
	int error = vevo_property_get( instance, "in_parameters", p, &param );
	if(error != VEVO_NO_ERROR )
		return 0;
	error = vevo_property_get( param, "parent_template",0,&param_templ);
	int kind = 0;
	error = vevo_property_get( param_templ, "HOST_kind",0,&kind );
	int res = 0;
	char vkey[64];
	snprintf(vkey,sizeof(vkey), "p%02d", p );

	switch(kind)
	{
		case HOST_PARAM_HEIGHT:
		case HOST_PARAM_WIDTH:
		case HOST_PARAM_INDEX:
			res = vevo_property_from_string( fx_values,str, vkey,1, VEVO_ATOM_TYPE_INT );
			break;
		case HOST_PARAM_NUMBER:
			res = vevo_property_from_string( fx_values,str, vkey,1, VEVO_ATOM_TYPE_DOUBLE );
			break;
		case HOST_PARAM_SWITCH:
			res = vevo_property_from_string( fx_values,str, vkey,1, VEVO_ATOM_TYPE_BOOL );
			break;
		case HOST_PARAM_COORD:
			res = vevo_property_from_string( fx_values ,str, vkey,2, VEVO_ATOM_TYPE_DOUBLE );
			break;
		case HOST_PARAM_COLOR:
			res = vevo_property_from_string( fx_values,str, vkey,3, VEVO_ATOM_TYPE_DOUBLE );
			break;
		case HOST_PARAM_TEXT:
			res = vevo_property_from_string( fx_values,str, vkey,1, VEVO_ATOM_TYPE_STRING );
			break;
	}
	return res;
}

// set plugin defaults
void	livido_reverse_clone_parameter( void *instance, int seq, void *fx_value_port )
{
	int vj_np = vevo_property_num_elements( instance, "in_parameters" );
	int i;

    char vkey[16];
	for( i = 0; i < vj_np; i ++ )
	{
		void *param = NULL;
		if( vevo_property_get( instance, "in_parameters", i, &param) != VEVO_NO_ERROR )
			continue;
		snprintf(vkey,sizeof(vkey), "p%02d", i );
		clone_prop_vevo( param, fx_value_port, vkey, "value"  );
	}
}

void	livido_clone_parameter( void *instance, int seq, void *fx_value_port )
{
	int vj_np = vevo_property_num_elements( instance, "in_parameters" );
	int i;
    char vkey[16];
	for( i = 0; i < vj_np; i ++ )
	{
		void *param = NULL;
		if( vevo_property_get( instance, "in_parameters", i, &param) != VEVO_NO_ERROR )
			continue;
		snprintf(vkey,sizeof(vkey), "p%02d", i );
		clone_prop_vevo( fx_value_port, param,vkey, "value"  );
	}
}

static void	livido_notify_parameter_update( void *instance ,void *param, void *value )
{
	generic_osc_cb_f cbf;
	int error = vevo_property_get( instance, "HOST_osc_cb", 0, &cbf );
	if( error == VEVO_NO_ERROR )
	{
		void *userdata = NULL;
		error = vevo_property_get( instance, "HOST_data",0, &userdata);
		(*cbf)( userdata, param, value );
	}
}

void	livido_set_parameter( void *instance, int seq, void *value )
{
	void *param = NULL;
	if( vevo_property_get( instance, "in_parameters", seq, &param) == VEVO_NO_ERROR ) {
		livido_set_parameter_f pctrl;
		if( vevo_property_get( param, "HOST_parameter_func", 0, &pctrl ) == VEVO_NO_ERROR ) {
			if( (*pctrl)( param, value ) )
				livido_notify_parameter_update( instance,param, value );
		}
	}
}
static	void	*livido_get_parameter_template(void *plugin, unsigned int pos )
{
	void *param_templ = NULL; 
	int error = vevo_property_get( plugin, "in_parameter_templates", pos, &param_templ);
	if( error != VEVO_NO_ERROR )
		return NULL;
	
	return param_templ;	
}

int	livido_get_num_input_parameters( void *instance )
{
	return vevo_property_num_elements( instance, "in_parameters" );
}

void	livido_get_default_parameters( void *instance, int *values )
{
	void *in_params;
	int i;

	if( vevo_property_get( instance, "in_parameters" ,0, &in_params ) != VEVO_NO_ERROR )
		return;

	int n_params = vevo_property_num_elements( instance, "in_parameters" );
	if( n_params <= 0 ) {
		return;
	}
	
	for( i = 0; i < n_params; i ++ ) {
		void *templ;
		if( vevo_property_get( in_params, "parent_template", 0, &templ ) != VEVO_NO_ERROR )
			continue;

		vevo_property_get( templ, "default", i, &(values[i]));
	}
}

static int	livido_read_plug_configuration(void *filter_template, const char *name)
{
	FILE *f = plug_open_config( "livido", name, "r",0 );
	unsigned int i;	

	if(!f) { // lets write the plugin's default parameter values to disk
		int n_params = vevo_property_num_elements( filter_template, "in_parameter_templates" );
		if( n_params <= 0 ) {
			return 0; // no defaults to write (for now). vevo serialization not yet complete for full dump TODO
		}

		veejay_msg(VEEJAY_MSG_DEBUG, "No configuration file for livido plugin %s", name);
		f = plug_open_config( "livido", name, "w",1 );
		if( f ) {
			for( i = 0; i < n_params; i ++ ) {
				void *templ = livido_get_parameter_template( filter_template, i );
				if( templ == NULL )
					continue;
				
				char *str = vevo_sprintf_property( templ, "default" );
				if( str ) {
					fprintf( f, "%s\n", str );
					free(str);
				}
				else {
					fprintf( f, "default=DYNAMIC:\n" );
				}
			}
			fclose(f);
			return 1;
		}
	}

	/* if the file exists, write back the properties defined in it */
 	//only in_paramter_templates for now...
	if( f ) {
		i = 0;
		char buf[256];
		while( (fgets( buf, sizeof(buf), f )) != NULL ) {
			void *templ = livido_get_parameter_template( filter_template, i );
			if(templ==NULL)
				break;
			vevo_sscanf_port( templ, buf ); 
			i ++;
		}
	
		fclose(f);
	}
	return 0;
}



void*	deal_with_livido( void *handle, const char *name, int w, int h )
{
	void *port = vpn( VEVO_LIVIDO_PORT );
	char *plugin_name = NULL;

	livido_setup_f livido_setup = dlsym( handle, "livido_setup" );

	livido_setup_t setup[] = {
		{ .f.malloc = vj_malloc_ },
		{ .f.free = free },
		{ .f.memset = veejay_memset },
		{ .f.memcpy = veejay_memcpy },
		{ .f.port_new = vevo_port_new },
		{ .f.port_free = vevo_port_free },
		{ .f.property_set = vevo_property_set },
		{ .f.property_get = vevo_property_get },
		{ .f.property_num_elements = vevo_property_num_elements },
		{ .f.property_atom_type = vevo_property_atom_type },
		{ .f.property_element_size = vevo_property_element_size },
		{ .f.list_properties = vevo_list_properties },
		{ .f.keyframe_get = livido_dummy_keyframe },
		{ .f.keyframe_put = livido_dummy_keyframe },
	};

	void *livido_plugin = livido_setup( setup, LIVIDO_API_VERSION );
	
	if(!livido_plugin)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to load livido plugin '%s'",name);
		return NULL;
	}

	vevo_property_set( port, "instance", LIVIDO_ATOM_TYPE_PORTPTR, 1,&livido_plugin );
	vevo_property_set( port, "handle", LIVIDO_ATOM_TYPE_VOIDPTR,1,&handle );


	void *filter_templ = NULL;
	if(vevo_property_get( livido_plugin, "filters",0,&filter_templ) != VEVO_NO_ERROR ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Plugin %s does not have a filter template",name );
		return NULL;
	}
	
	plugin_name =  get_str_vevo( filter_templ, "name" );

	int compiled_as = 0;
	if( vevo_property_get( filter_templ, "api_version", 0,&compiled_as ) != LIVIDO_NO_ERROR )
	{
		veejay_msg(VEEJAY_MSG_WARNING,"Plugin '%s' does not have the property 'api_version'. ", plugin_name );
		free(plugin_name);
		return NULL;	
	}

	if( compiled_as < LIVIDO_API_VERSION ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "Plugin '%s' was compiled for LIVIDO API %d", plugin_name, compiled_as );
	}

	if( compiled_as > LIVIDO_API_VERSION ) {
		veejay_msg(VEEJAY_MSG_WARNING, "Plugin '%s' uses newer LiViDO API (version %d)", plugin_name, compiled_as);
		free(plugin_name);
		return NULL;
	}

    if( read_plugin_configuration ) {
	    livido_read_plug_configuration( filter_templ, name );
    }

	int n_params = livido_scan_parameters( filter_templ, port, w, h );
	int n_oparams = livido_scan_out_parameters( filter_templ, port );
	
	int n_inputs = livido_property_num_elements( filter_templ, "in_channel_templates" );
	int n_outputs = livido_property_num_elements( filter_templ, "out_channel_templates" );
	int filter_flags = 0;
	vevo_property_get(filter_templ, "flags", 0, &filter_flags);

	veejay_msg(VEEJAY_MSG_DEBUG, "Loading LiVIDO-%d plugin '%s'" , compiled_as, plugin_name);
	
	size_t clone_name_size = strlen(plugin_name) + 5;
	char *clone_name = (char*) vj_malloc(clone_name_size);
	snprintf(clone_name, clone_name_size, "LVD %s", plugin_name );

	int mixer = (n_inputs == 2 && n_outputs == 1 ) ? 1: 0;

	vevo_property_set( port, "num_params", VEVO_ATOM_TYPE_INT, 1, &n_params );
	vevo_property_set( port, "num_out_params", VEVO_ATOM_TYPE_INT,1,&n_oparams );
	vevo_property_set( port, "name", VEVO_ATOM_TYPE_STRING,1, &clone_name );
	vevo_property_set( port, "num_inputs", VEVO_ATOM_TYPE_INT,1, &n_inputs);
	vevo_property_set( port, "num_outputs",VEVO_ATOM_TYPE_INT,1, &n_outputs);
	vevo_property_set( port, "info", LIVIDO_ATOM_TYPE_PORTPTR,1,&filter_templ );
	vevo_property_set( port, "mixer", VEVO_ATOM_TYPE_INT,1, &mixer );
	vevo_property_set( port, "livido_filter_flags", VEVO_ATOM_TYPE_INT, 1,
					  &filter_flags );
	vevo_property_softref( port, "info" );
	vevo_property_set( port, "HOST_plugin_type", VEVO_ATOM_TYPE_INT,1,&livido_signature_);

	free(clone_name);
	free(plugin_name);	

	return port;
}


static int		host_to_palette( int pref_palette )
{	
	switch(pref_palette) {
		case PIX_FMT_YUV420P:
		case PIX_FMT_YUVA420P:
		case PIX_FMT_YUVJ420P:
			return LIVIDO_PALETTE_YUV420P;
		case PIX_FMT_YUV422P:
		case PIX_FMT_YUVJ422P:
		case PIX_FMT_YUVA422P:
			return LIVIDO_PALETTE_YUV422P;
        case PIX_FMT_YUV444P:
        case PIX_FMT_YUVJ444P:
        case PIX_FMT_YUVA444P:
            return LIVIDO_PALETTE_YUV444P;
		default:
			return LIVIDO_PALETTE_YUVA8888;
	}
	return LIVIDO_PALETTE_YUVA8888;
}


void	livido_set_pref_palette( int pref_palette )
{
	pref_palette_ffmpeg_= pref_palette;
	pref_palette_		= host_to_palette(pref_palette);
}
