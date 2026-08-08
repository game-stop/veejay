/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2026 Niels Elburg <nwelburg@gmail.com> 
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

#ifndef VJ_MIDI_MAP_H
#define VJ_MIDI_MAP_H

#include <glib.h>
#include <stddef.h>
#include <veejaycore/vims.h>
#include "vj-midi-engine.h"

G_BEGIN_DECLS

#define VJ_MIDI_ANY_CHANNEL (-1)
#define VJ_MIDI_ANY_CONTROL (-1)
#define VJ_MIDI_ANY_DEVICE  "*"
#define VJ_MIDI_VIMS_MIN_ID 1
#define VJ_MIDI_VIMS_MAX_ID (VIMS_MAX - 1)
#define VJ_MIDI_BUNDLE_MIN_ID VIMS_BUNDLE_START
#define VJ_MIDI_BUNDLE_MAX_ID VIMS_BUNDLE_END

typedef enum {
    VJ_MIDI_MODE_ABSOLUTE = 0,
    VJ_MIDI_MODE_RELATIVE,
    VJ_MIDI_MODE_RELATIVE_2C,
    VJ_MIDI_MODE_TRIGGER,
    VJ_MIDI_MODE_MOMENTARY,
    VJ_MIDI_MODE_TOGGLE
} VjMidiMode;

typedef enum {
    VJ_MIDI_ACTION_VIMS = 0,
    VJ_MIDI_ACTION_BUNDLE,
    VJ_MIDI_ACTION_RAW
} VjMidiActionType;

typedef struct {
    VjMidiActionType type;
    int vims_id;
    int bundle_id;
    char *args_template;
    char *raw_message;
    char *raw_off_message;
    char *raw_on_message;
} VjMidiAction;

typedef struct _VjMidiMapping {
    guint id;
    char *name;
    char *device;
    VjMidiEventType event_type;
    int channel;
    int control;
    VjMidiMode mode;
    int input_min;
    int input_max;
    int output_min;
    int output_max;
    int output_limit_enabled;
    int output_limit_min;
    int output_limit_max;
    int output_center_enabled;
    int output_center;
    int deadzone;
    int invert;
    int enabled;
    int last_output;
    int toggle_state;
    VjMidiAction action;
} VjMidiMapping;

typedef struct _VjMidiMap VjMidiMap;
typedef void (*VjMidiSendFunc)(const char *message, void *user_data);

VjMidiMap     *vj_midi_map_new(void);
void           vj_midi_map_free(VjMidiMap *map);
void           vj_midi_map_clear(VjMidiMap *map);
void           vj_midi_map_set_send_callback(VjMidiMap *map,
                                              VjMidiSendFunc callback,
                                              void *user_data);
VjMidiMapping *vj_midi_mapping_new(void);
VjMidiMapping *vj_midi_mapping_copy(const VjMidiMapping *mapping);
void           vj_midi_mapping_free(VjMidiMapping *mapping);
guint          vj_midi_map_add(VjMidiMap *map, VjMidiMapping *mapping);
int            vj_midi_map_remove(VjMidiMap *map, guint id);
VjMidiMapping *vj_midi_map_get(VjMidiMap *map, guint id);
VjMidiMapping *vj_midi_map_get_nth(VjMidiMap *map, guint index);
guint          vj_midi_map_count(const VjMidiMap *map);
int            vj_midi_map_process_event(VjMidiMap *map,
                                          const VjMidiEvent *event);
int            vj_midi_map_test(VjMidiMap *map,
                                 VjMidiMapping *mapping,
                                 int input_value);
int            vj_midi_mapping_validate(const VjMidiMapping *mapping,
                                         char *error_text,
                                         size_t error_text_size);
int            vj_midi_map_save(VjMidiMap *map,
                                 const char *filename,
                                 GError **error);
int            vj_midi_map_load(VjMidiMap *map,
                                 const char *filename,
                                 GError **error);
char          *vj_midi_mapping_describe(const VjMidiMapping *mapping);
const char    *vj_midi_mode_name(VjMidiMode mode);
const char    *vj_midi_action_type_name(VjMidiActionType type);

typedef void (*vj_midi_mapping_func)(const char *mapping_key,
                                     int event_type,
                                     int parameter,
                                     int extra,
                                     const char *event_name,
                                     const char *parameter_text,
                                     const char *source_widget,
                                     const char *message,
                                     void *user_data);

void *vj_midi_new(void *mw, void *timeline);
int   vj_midi_handle_events(void *context);
void  vj_midi_play(void *context, int play);
void  vj_midi_learn(void *context, int start);
void  vj_midi_load(void *context, const char *filename);
void  vj_midi_save(void *context, const char *filename);
void  vj_midi_reset(void *context);
int   vj_midi_foreach_mapping(void *context, vj_midi_mapping_func callback, void *user_data);
int   vj_midi_unbind(void *context, const char *mapping_key);
void  vj_midi_learning_vims(void *context, char *widget, char *msg, int extra);
void  vj_midi_learning_vims_simple(void *context, char *widget, int id);
void  vj_midi_learning_vims_toggle(void *context, char *widget, int id);
void  vj_midi_learning_vims_toggle2(void *context, char *widget, int id, int arg);
void  vj_midi_learning_vims_toggle3(void *context, char *widget, int id, int arg0, int arg1);
void  vj_midi_learning_vims_dual_toggle(void *context, char *widget, int off_id, int on_id, int arg);
void  vj_midi_learning_vims_complex(void *context, char *widget, int id, int first, int extra);
void  vj_midi_learning_vims_complex_msg(void *context, char *widget, int id, int first, int extra, char *name);
void  vj_midi_learning_vims_msg(void *context, char *widget, int id, int arg);
void  vj_midi_learning_vims_msg2(void *context, char *widget, int vims_id, int id, int arg);
void  vj_midi_learning_vims_msg2_extra(void *context, int id, int a, int extra);
void  vj_midi_learning_vims_fx(void *context, int widget, int id, int a, int b, int c, int extra);
void  vj_midi_learning_vims_spin(void *context, char *widget, int id);

G_END_DECLS

#endif
