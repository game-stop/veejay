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
#include "vj-midi-map.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <veejaycore/vims.h>

#define VJ_MIDI_MAX_MAPPINGS 4096

struct _VjMidiMap {
    GPtrArray *mappings;
    guint next_id;
    VjMidiSendFunc send;
    void *send_data;
};

static int midi_clampi(int value, int lo, int hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

static void midi_set_error(char *dst, size_t dst_size, const char *fmt, ...)
{
    if(!dst || dst_size == 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    g_vsnprintf(dst, dst_size, fmt, ap);
    va_end(ap);
}

const char *vj_midi_mode_name(VjMidiMode mode)
{
    switch(mode) {
        case VJ_MIDI_MODE_ABSOLUTE:    return "Absolute";
        case VJ_MIDI_MODE_RELATIVE:    return "Relative";
        case VJ_MIDI_MODE_RELATIVE_2C: return "Relative 2C";
        case VJ_MIDI_MODE_TRIGGER:     return "Trigger";
        case VJ_MIDI_MODE_MOMENTARY:   return "Momentary";
        case VJ_MIDI_MODE_TOGGLE:      return "Toggle";
        default:                       return "Unknown";
    }
}

const char *vj_midi_action_type_name(VjMidiActionType type)
{
    switch(type) {
        case VJ_MIDI_ACTION_VIMS:   return "VIMS Event";
        case VJ_MIDI_ACTION_BUNDLE: return "VIMS Bundle";
        case VJ_MIDI_ACTION_RAW:    return "Raw VIMS";
        default:                    return "Unknown";
    }
}

static void midi_action_clear(VjMidiAction *action)
{
    g_free(action->args_template);
    g_free(action->raw_message);
    g_free(action->raw_off_message);
    g_free(action->raw_on_message);
    memset(action, 0, sizeof(*action));
}

static void midi_action_copy(VjMidiAction *dst, const VjMidiAction *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->type = src->type;
    dst->vims_id = src->vims_id;
    dst->bundle_id = src->bundle_id;
    dst->args_template = g_strdup(src->args_template);
    dst->raw_message = g_strdup(src->raw_message);
    dst->raw_off_message = g_strdup(src->raw_off_message);
    dst->raw_on_message = g_strdup(src->raw_on_message);
}

VjMidiMapping *vj_midi_mapping_new(void)
{
    VjMidiMapping *mapping = g_new0(VjMidiMapping, 1);
    mapping->device = g_strdup(VJ_MIDI_ANY_DEVICE);
    mapping->event_type = VJ_MIDI_EVENT_CC;
    mapping->channel = VJ_MIDI_ANY_CHANNEL;
    mapping->control = VJ_MIDI_ANY_CONTROL;
    mapping->mode = VJ_MIDI_MODE_ABSOLUTE;
    vj_midi_event_value_range(mapping->event_type, &mapping->input_min, &mapping->input_max);
    mapping->output_min = 0;
    mapping->output_max = 127;
    mapping->output_limit_enabled = 0;
    mapping->output_limit_min = 0;
    mapping->output_limit_max = 127;
    mapping->enabled = 1;
    mapping->last_output = mapping->output_min;
    mapping->action.type = VJ_MIDI_ACTION_VIMS;
    return mapping;
}

VjMidiMapping *vj_midi_mapping_copy(const VjMidiMapping *src)
{
    if(!src)
        return NULL;
    VjMidiMapping *dst = g_new0(VjMidiMapping, 1);
    *dst = *src;
    dst->name = g_strdup(src->name);
    dst->device = g_strdup(src->device);
    midi_action_copy(&dst->action, &src->action);
    return dst;
}

void vj_midi_mapping_free(VjMidiMapping *mapping)
{
    if(!mapping)
        return;
    g_free(mapping->name);
    g_free(mapping->device);
    midi_action_clear(&mapping->action);
    g_free(mapping);
}

VjMidiMap *vj_midi_map_new(void)
{
    VjMidiMap *map = g_new0(VjMidiMap, 1);
    map->mappings = g_ptr_array_new_with_free_func((GDestroyNotify) vj_midi_mapping_free);
    map->next_id = 1;
    return map;
}

void vj_midi_map_free(VjMidiMap *map)
{
    if(!map)
        return;
    g_ptr_array_free(map->mappings, TRUE);
    g_free(map);
}

void vj_midi_map_clear(VjMidiMap *map)
{
    if(!map)
        return;
    g_ptr_array_set_size(map->mappings, 0);
    map->next_id = 1;
}

void vj_midi_map_set_send_callback(VjMidiMap *map,
                                   VjMidiSendFunc callback,
                                   void *user_data)
{
    if(!map)
        return;
    map->send = callback;
    map->send_data = user_data;
}

static int midi_map_id_exists(const VjMidiMap *map, guint id)
{
    if(!map || id == 0)
        return 0;
    for(guint i = 0; i < map->mappings->len; i++) {
        const VjMidiMapping *mapping = g_ptr_array_index(map->mappings, i);
        if(mapping->id == id)
            return 1;
    }
    return 0;
}

guint vj_midi_map_add(VjMidiMap *map, VjMidiMapping *mapping)
{
    if(!map || !mapping)
        return 0;
    if(map->mappings->len >= VJ_MIDI_MAX_MAPPINGS)
        return 0;

    if(mapping->id == 0 || midi_map_id_exists(map, mapping->id))
        mapping->id = map->next_id++;
    else if(mapping->id >= map->next_id)
        map->next_id = mapping->id + 1;

    mapping->last_output = midi_clampi(mapping->last_output,
                                       MIN(mapping->output_min, mapping->output_max),
                                       MAX(mapping->output_min, mapping->output_max));
    g_ptr_array_add(map->mappings, mapping);
    return mapping->id;
}

int vj_midi_map_remove(VjMidiMap *map, guint id)
{
    if(!map)
        return 0;
    for(guint i = 0; i < map->mappings->len; i++) {
        VjMidiMapping *mapping = g_ptr_array_index(map->mappings, i);
        if(mapping->id == id) {
            g_ptr_array_remove_index(map->mappings, i);
            return 1;
        }
    }
    return 0;
}

VjMidiMapping *vj_midi_map_get(VjMidiMap *map, guint id)
{
    if(!map)
        return NULL;
    for(guint i = 0; i < map->mappings->len; i++) {
        VjMidiMapping *mapping = g_ptr_array_index(map->mappings, i);
        if(mapping->id == id)
            return mapping;
    }
    return NULL;
}

VjMidiMapping *vj_midi_map_get_nth(VjMidiMap *map, guint index)
{
    if(!map || index >= map->mappings->len)
        return NULL;
    return g_ptr_array_index(map->mappings, index);
}

guint vj_midi_map_count(const VjMidiMap *map)
{
    return map ? map->mappings->len : 0;
}

static int midi_template_dynamic_value_count(const char *text)
{
    int count = 0;
    const char *p = text;

    while(p && *p) {
        if(g_str_has_prefix(p, "$VALUE")) {
            count++;
            p += 6;
        } else if(g_str_has_prefix(p, "$NORM")) {
            count++;
            p += 5;
        } else if(g_str_has_prefix(p, "$RAW")) {
            count++;
            p += 4;
        } else {
            p++;
        }
    }
    return count;
}

static int midi_template_has_dynamic_value(const char *text)
{
    return midi_template_dynamic_value_count(text) > 0;
}

static int midi_vims_template_safe(const char *text)
{
    if(!text)
        return 1;
    for(const char *p = text; *p; p++) {
        if(*p == ';' || *p == '\n' || *p == '\r')
            return 0;
    }
    return 1;
}

static int midi_raw_message_safe(const char *text)
{
    size_t len;
    int selector;

    if(!text || !text[0])
        return 0;
    len = strlen(text);
    if(len < 5 || len > 4096)
        return 0;
    if(!g_ascii_isdigit(text[0]) || !g_ascii_isdigit(text[1]) || !g_ascii_isdigit(text[2]) || text[3] != ':')
        return 0;
    selector = (text[0] - '0') * 100 + (text[1] - '0') * 10 + (text[2] - '0');
    if(selector < VJ_MIDI_VIMS_MIN_ID || selector > VJ_MIDI_VIMS_MAX_ID)
        return 0;
    if(strchr(text, '\n') || strchr(text, '\r'))
        return 0;
    if(text[len - 1] != ';')
        return 0;
    if(strchr(text, ';') != text + len - 1)
        return 0;
    return 1;
}

int vj_midi_mapping_validate(const VjMidiMapping *mapping,
                             char *error_text,
                             size_t error_text_size)
{
    if(error_text && error_text_size)
        error_text[0] = '\0';
    if(!mapping) {
        midi_set_error(error_text, error_text_size, "No mapping selected");
        return 0;
    }
    if(mapping->event_type <= VJ_MIDI_EVENT_UNKNOWN ||
       mapping->event_type > VJ_MIDI_EVENT_PROGRAM_CHANGE) {
        midi_set_error(error_text, error_text_size, "Invalid MIDI event type");
        return 0;
    }
    int native_min = 0, native_max = 127;
    int control_min = 0, control_max = 127;
    vj_midi_event_value_range(mapping->event_type, &native_min, &native_max);
    vj_midi_event_control_range(mapping->event_type, &control_min, &control_max);
    if(mapping->control != VJ_MIDI_ANY_CONTROL &&
       (mapping->control < control_min || mapping->control > control_max)) {
        midi_set_error(error_text, error_text_size,
                       "MIDI control %d is outside the %s protocol range %d..%d",
                       mapping->control, vj_midi_event_type_name(mapping->event_type),
                       control_min, control_max);
        return 0;
    }
    if(mapping->input_min < native_min || mapping->input_min > native_max ||
       mapping->input_max < native_min || mapping->input_max > native_max) {
        midi_set_error(error_text, error_text_size,
                       "MIDI input range %d..%d is outside the native %s range %d..%d",
                       mapping->input_min, mapping->input_max,
                       vj_midi_event_type_name(mapping->event_type), native_min, native_max);
        return 0;
    }
    if(mapping->channel < VJ_MIDI_ANY_CHANNEL || mapping->channel > 15) {
        midi_set_error(error_text, error_text_size, "MIDI channel must be Any or 1-16");
        return 0;
    }
    if(mapping->mode == VJ_MIDI_MODE_ABSOLUTE && mapping->input_min >= mapping->input_max) {
        midi_set_error(error_text, error_text_size, "Absolute MIDI range requires minimum < maximum; use Invert to reverse direction");
        return 0;
    }
    if(mapping->output_limit_enabled) {
        if(mapping->output_limit_min > mapping->output_limit_max) {
            midi_set_error(error_text, error_text_size, "Invalid learned target widget range");
            return 0;
        }
        if(mapping->output_min < mapping->output_limit_min ||
           mapping->output_min > mapping->output_limit_max ||
           mapping->output_max < mapping->output_limit_min ||
           mapping->output_max > mapping->output_limit_max) {
            midi_set_error(error_text, error_text_size,
                           "VIMS output range %d..%d is outside the learned Reloaded control range %d..%d",
                           mapping->output_min, mapping->output_max,
                           mapping->output_limit_min, mapping->output_limit_max);
            return 0;
        }
    }
    if(mapping->output_center_enabled) {
        const int out_lo = MIN(mapping->output_min, mapping->output_max);
        const int out_hi = MAX(mapping->output_min, mapping->output_max);
        if(mapping->mode != VJ_MIDI_MODE_ABSOLUTE ||
           mapping->input_min >= 0 || mapping->input_max <= 0) {
            midi_set_error(error_text, error_text_size,
                           "Centered output requires an absolute bipolar MIDI input range");
            return 0;
        }
        if(mapping->output_center < out_lo || mapping->output_center > out_hi) {
            midi_set_error(error_text, error_text_size,
                           "Centered output value must lie inside the target output range");
            return 0;
        }
    }
    if(mapping->deadzone < 0 || mapping->deadzone > ABS(mapping->input_max - mapping->input_min)) {
        midi_set_error(error_text, error_text_size, "Deadzone is outside the MIDI input range");
        return 0;
    }
    if(mapping->mode < VJ_MIDI_MODE_ABSOLUTE || mapping->mode > VJ_MIDI_MODE_TOGGLE) {
        midi_set_error(error_text, error_text_size, "Invalid MIDI mapping mode");
        return 0;
    }
    if((mapping->mode == VJ_MIDI_MODE_RELATIVE ||
        mapping->mode == VJ_MIDI_MODE_RELATIVE_2C) &&
       mapping->event_type != VJ_MIDI_EVENT_CC) {
        midi_set_error(error_text, error_text_size, "Relative encoder modes require 7-bit MIDI CC input");
        return 0;
    }
    if(mapping->mode == VJ_MIDI_MODE_MOMENTARY &&
       mapping->event_type != VJ_MIDI_EVENT_NOTE_ON &&
       mapping->event_type != VJ_MIDI_EVENT_CC &&
       mapping->event_type != VJ_MIDI_EVENT_CC14 &&
       mapping->event_type != VJ_MIDI_EVENT_KEY_PRESSURE &&
       mapping->event_type != VJ_MIDI_EVENT_CHANNEL_PRESSURE) {
        midi_set_error(error_text, error_text_size, "Momentary mode requires Note On or a controller/pressure event with a release/zero state");
        return 0;
    }
    if(mapping->action.type < VJ_MIDI_ACTION_VIMS || mapping->action.type > VJ_MIDI_ACTION_RAW) {
        midi_set_error(error_text, error_text_size, "Invalid mapping action type");
        return 0;
    }

    if(mapping->action.type == VJ_MIDI_ACTION_VIMS) {
        if(mapping->action.vims_id < VJ_MIDI_VIMS_MIN_ID || mapping->action.vims_id > VJ_MIDI_VIMS_MAX_ID) {
            midi_set_error(error_text, error_text_size, "VIMS selector is outside the VIMS protocol range");
            return 0;
        }
        if(!midi_vims_template_safe(mapping->action.args_template)) {
            midi_set_error(error_text, error_text_size, "VIMS arguments cannot contain ';' or newlines");
            return 0;
        }
        const int dynamic_value_count =
            midi_template_dynamic_value_count(mapping->action.args_template);
        if(dynamic_value_count > 1) {
            midi_set_error(error_text, error_text_size,
                           "A VIMS mapping may have only one MIDI-controlled value argument ($VALUE, $NORM or $RAW)");
            return 0;
        }
        if((mapping->mode == VJ_MIDI_MODE_ABSOLUTE ||
            mapping->mode == VJ_MIDI_MODE_RELATIVE ||
            mapping->mode == VJ_MIDI_MODE_RELATIVE_2C ||
            mapping->mode == VJ_MIDI_MODE_MOMENTARY ||
            mapping->mode == VJ_MIDI_MODE_TOGGLE) &&
           dynamic_value_count != 1) {
            midi_set_error(error_text, error_text_size,
                           "Continuous/toggle VIMS mappings need exactly one MIDI-controlled argument using $VALUE, $NORM or $RAW");
            return 0;
        }
        if(mapping->action.vims_id == VIMS_CHAIN_ENTRY_SET_NARG_VAL &&
           (!mapping->action.args_template || !strstr(mapping->action.args_template, "$NORM"))) {
            midi_set_error(error_text, error_text_size,
                           "Normalized FX parameter VIMS requires $NORM (0.0-1.0)");
            return 0;
        }
        if(mapping->action.vims_id == VIMS_CHAIN_ENTRY_SET_ARG_VAL &&
           (!mapping->action.args_template || !strstr(mapping->action.args_template, "$VALUE"))) {
            midi_set_error(error_text, error_text_size,
                           "FX parameter VIMS requires $VALUE mapped to that parameter's min/max");
            return 0;
        }
    } else if(mapping->action.type == VJ_MIDI_ACTION_BUNDLE) {
        if(mapping->action.bundle_id < VJ_MIDI_BUNDLE_MIN_ID ||
           mapping->action.bundle_id > VJ_MIDI_BUNDLE_MAX_ID) {
            midi_set_error(error_text, error_text_size, "VIMS bundles must use selector IDs 500-599");
            return 0;
        }
        if(mapping->mode != VJ_MIDI_MODE_TRIGGER) {
            midi_set_error(error_text, error_text_size, "VIMS bundles are trigger actions; use a VIMS event/raw pair for momentary or toggle behaviour");
            return 0;
        }
    } else {
        if(mapping->mode == VJ_MIDI_MODE_TOGGLE &&
           mapping->action.raw_off_message && mapping->action.raw_on_message) {
            if(!midi_raw_message_safe(mapping->action.raw_off_message) ||
               !midi_raw_message_safe(mapping->action.raw_on_message)) {
                midi_set_error(error_text, error_text_size, "Raw toggle messages are not valid VIMS messages");
                return 0;
            }
        } else if(!midi_raw_message_safe(mapping->action.raw_message)) {
            midi_set_error(error_text, error_text_size, "Raw action must start with NNN: and contain no newlines");
            return 0;
        }
    }

    return 1;
}

static int midi_event_is_release(const VjMidiMapping *mapping, const VjMidiEvent *event)
{
    if(event->type == VJ_MIDI_EVENT_NOTE_OFF)
        return mapping->event_type != VJ_MIDI_EVENT_NOTE_OFF;

    return ((event->type == VJ_MIDI_EVENT_CC ||
             event->type == VJ_MIDI_EVENT_CC14 ||
             event->type == VJ_MIDI_EVENT_KEY_PRESSURE ||
             event->type == VJ_MIDI_EVENT_CHANNEL_PRESSURE) &&
            event->value <= event->value_min);
}

static int midi_mapping_matches(const VjMidiMapping *mapping,
                                const VjMidiEvent *event)
{
    if(!mapping->enabled)
        return 0;
    if(mapping->device && strcmp(mapping->device, VJ_MIDI_ANY_DEVICE) != 0 &&
       g_strcmp0(mapping->device, event->device_name) != 0)
        return 0;

    if(mapping->event_type != event->type) {
        if(!(mapping->event_type == VJ_MIDI_EVENT_NOTE_ON &&
             event->type == VJ_MIDI_EVENT_NOTE_OFF &&
             mapping->mode == VJ_MIDI_MODE_MOMENTARY))
            return 0;
    }
    if(mapping->channel != VJ_MIDI_ANY_CHANNEL && mapping->channel != event->channel)
        return 0;
    if(mapping->control != VJ_MIDI_ANY_CONTROL && mapping->control != event->control)
        return 0;
    return 1;
}

static double midi_absolute_normalized(const VjMidiMapping *mapping, int raw)
{
    int in_lo = mapping->input_min;
    int in_hi = mapping->input_max;
    if(in_hi == in_lo)
        return 0.0;

    raw = midi_clampi(raw, MIN(in_lo, in_hi), MAX(in_lo, in_hi));
    double n = (double) (raw - in_lo) / (double) (in_hi - in_lo);
    n = CLAMP(n, 0.0, 1.0);
    if(mapping->invert)
        n = 1.0 - n;

    if(mapping->deadzone > 0) {
        int range_lo = MIN(mapping->input_min, mapping->input_max);
        int range_hi = MAX(mapping->input_min, mapping->input_max);
        int center = range_lo + (range_hi - range_lo) / 2;

        if(mapping->input_min < 0 && mapping->input_max > 0) {
            center = 0;
            if(ABS(raw - center) <= mapping->deadzone)
                n = 0.5;
        } else if(ABS(raw - mapping->input_min) <= mapping->deadzone) {
            n = mapping->invert ? 1.0 : 0.0;
        }
    }

    return n;
}

static int midi_scale_absolute(const VjMidiMapping *mapping, int raw)
{
    if(mapping->output_center_enabled &&
       mapping->input_min < 0 && mapping->input_max > 0) {
        const int in_lo = MIN(mapping->input_min, mapping->input_max);
        const int in_hi = MAX(mapping->input_min, mapping->input_max);
        const int left_out = mapping->invert ? mapping->output_max : mapping->output_min;
        const int right_out = mapping->invert ? mapping->output_min : mapping->output_max;
        const int center = mapping->output_center;

        raw = midi_clampi(raw, in_lo, in_hi);
        if(mapping->deadzone > 0 && ABS(raw) <= mapping->deadzone)
            return center;

        if(raw <= 0) {
            const double t = in_lo < 0 ?
                (double)(raw - in_lo) / (double)(-in_lo) : 1.0;
            return (int) llround((double)left_out +
                                 CLAMP(t, 0.0, 1.0) * (double)(center - left_out));
        }

        const double t = in_hi > 0 ? (double)raw / (double)in_hi : 1.0;
        return (int) llround((double)center +
                             CLAMP(t, 0.0, 1.0) * (double)(right_out - center));
    }

    double n = midi_absolute_normalized(mapping, raw);
    return (int) llround((double) mapping->output_min +
                         n * (double) (mapping->output_max - mapping->output_min));
}

static int midi_relative_delta(VjMidiMode mode, int value)
{
    if(mode == VJ_MIDI_MODE_RELATIVE_2C) {
        if(value <= 63)
            return value;
        return value - 128;
    }

    if(value == 64 || value == 0)
        return 0;
    if(value < 64)
        return value;
    return -(128 - value);
}

static int midi_mapping_value(VjMidiMapping *mapping, const VjMidiEvent *event,
                              int *should_send)
{
    *should_send = 1;

    switch(mapping->mode) {
        case VJ_MIDI_MODE_ABSOLUTE:
            return midi_scale_absolute(mapping, event->value);

        case VJ_MIDI_MODE_RELATIVE:
        case VJ_MIDI_MODE_RELATIVE_2C: {
            int delta = midi_relative_delta(mapping->mode, event->value);
            if(mapping->invert)
                delta = -delta;
            if(delta == 0) {
                *should_send = 0;
                return mapping->last_output;
            }
            mapping->last_output = midi_clampi(mapping->last_output + delta,
                                               MIN(mapping->output_min, mapping->output_max),
                                               MAX(mapping->output_min, mapping->output_max));
            return mapping->last_output;
        }

        case VJ_MIDI_MODE_TRIGGER:
            if(midi_event_is_release(mapping, event)) {
                *should_send = 0;
                return mapping->last_output;
            }
            return mapping->output_max;

        case VJ_MIDI_MODE_MOMENTARY:
            return midi_event_is_release(mapping, event) ? mapping->output_min : mapping->output_max;

        case VJ_MIDI_MODE_TOGGLE:
            if(midi_event_is_release(mapping, event)) {
                *should_send = 0;
                return mapping->last_output;
            }
            mapping->toggle_state = !mapping->toggle_state;
            return mapping->toggle_state ? mapping->output_max : mapping->output_min;

        default:
            *should_send = 0;
            return 0;
    }
}

static double midi_output_normalized(const VjMidiMapping *mapping, int value)
{
    if(mapping->output_max == mapping->output_min)
        return value == mapping->output_max ? 1.0 : 0.0;

    double n = (double) (value - mapping->output_min) /
               (double) (mapping->output_max - mapping->output_min);
    return CLAMP(n, 0.0, 1.0);
}

static char *midi_expand_template(const char *text,
                                  const VjMidiMapping *mapping,
                                  const VjMidiEvent *event,
                                  int value)
{
    if(!text)
        return g_strdup("");

    const double norm = midi_output_normalized(mapping, value);
    GString *out = g_string_new(NULL);
    const char *p = text;
    while(*p) {
        if(g_str_has_prefix(p, "$CHANNEL0")) {
            g_string_append_printf(out, "%d", event ? event->channel : 0);
            p += 9;
        }
        else if(g_str_has_prefix(p, "$CHANNEL")) {
            g_string_append_printf(out, "%d", event ? event->channel + 1 : 1);
            p += 8;
        }
        else if(g_str_has_prefix(p, "$CONTROL")) {
            g_string_append_printf(out, "%d", event ? event->control : 0);
            p += 8;
        }
        else if(g_str_has_prefix(p, "$VALUE")) {
            g_string_append_printf(out, "%d", value);
            p += 6;
        }
        else if(g_str_has_prefix(p, "$NORM")) {
            g_string_append_printf(out, "%.6f", norm);
            p += 5;
        }
        else if(g_str_has_prefix(p, "$RAW")) {
            g_string_append_printf(out, "%d", event ? event->value : 0);
            p += 4;
        }
        else {
            g_string_append_c(out, *p++);
        }
    }
    return g_string_free(out, FALSE);
}

static char *midi_render_action(const VjMidiMapping *mapping,
                                const VjMidiEvent *event,
                                int value)
{
    const VjMidiAction *action = &mapping->action;

    if(action->type == VJ_MIDI_ACTION_VIMS) {
        char *args = midi_expand_template(action->args_template, mapping, event, value);
        char *message = args[0]
            ? g_strdup_printf("%03d:%s;", action->vims_id, args)
            : g_strdup_printf("%03d:;", action->vims_id);
        g_free(args);
        return message;
    }

    if(action->type == VJ_MIDI_ACTION_BUNDLE)
        return g_strdup_printf("%03d:;", action->bundle_id);

    if(mapping->mode == VJ_MIDI_MODE_TOGGLE &&
       action->raw_off_message && action->raw_on_message)
        return g_strdup(mapping->toggle_state ? action->raw_on_message : action->raw_off_message);

    return midi_expand_template(action->raw_message, mapping, event, value);
}

static int midi_execute(VjMidiMap *map, VjMidiMapping *mapping,
                        const VjMidiEvent *event)
{
    char validation[192];
    if(!vj_midi_mapping_validate(mapping, validation, sizeof(validation)))
        return 0;

    int should_send = 0;
    int value = midi_mapping_value(mapping, event, &should_send);
    if(!should_send)
        return 0;

    value = midi_clampi(value,
                        MIN(mapping->output_min, mapping->output_max),
                        MAX(mapping->output_min, mapping->output_max));
    mapping->last_output = value;

    char *message = midi_render_action(mapping, event, value);
    if(!message || !message[0]) {
        g_free(message);
        return 0;
    }
    if(map->send)
        map->send(message, map->send_data);
    g_free(message);
    return 1;
}

int vj_midi_map_process_event(VjMidiMap *map, const VjMidiEvent *event)
{
    int sent = 0;
    if(!map || !event)
        return 0;

    for(guint i = 0; i < map->mappings->len; i++) {
        VjMidiMapping *mapping = g_ptr_array_index(map->mappings, i);
        if(midi_mapping_matches(mapping, event))
            sent += midi_execute(map, mapping, event);
    }
    return sent;
}

int vj_midi_map_test(VjMidiMap *map, VjMidiMapping *mapping, int input_value)
{
    if(!map || !mapping)
        return 0;
    VjMidiEvent event;
    memset(&event, 0, sizeof(event));
    event.type = mapping->event_type;
    event.channel = mapping->channel == VJ_MIDI_ANY_CHANNEL ? 0 : mapping->channel;
    event.control = mapping->control == VJ_MIDI_ANY_CONTROL ? 0 : mapping->control;
    event.value = midi_clampi(input_value,
                              MIN(mapping->input_min, mapping->input_max),
                              MAX(mapping->input_min, mapping->input_max));
    event.value_min = mapping->input_min;
    event.value_max = mapping->input_max;
    g_strlcpy(event.device_name,
              mapping->device ? mapping->device : VJ_MIDI_ANY_DEVICE,
              sizeof(event.device_name));
    return midi_execute(map, mapping, &event);
}

static void keyfile_set_string_safe(GKeyFile *kf, const char *group,
                                    const char *key, const char *value)
{
    g_key_file_set_string(kf, group, key, value ? value : "");
}

int vj_midi_map_save(VjMidiMap *map, const char *filename, GError **error)
{
    if(!map || !filename)
        return 0;

    GKeyFile *kf = g_key_file_new();
    g_key_file_set_integer(kf, "MIDI", "version", 2);
    g_key_file_set_integer(kf, "MIDI", "mapping_count", (int) map->mappings->len);

    for(guint i = 0; i < map->mappings->len; i++) {
        VjMidiMapping *m = g_ptr_array_index(map->mappings, i);
        char validation[192];
        if(!vj_midi_mapping_validate(m, validation, sizeof(validation))) {
            if(error && !*error)
                g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "Mapping %u is invalid: %s", i + 1, validation);
            g_key_file_free(kf);
            return 0;
        }

        char group[64];
        g_snprintf(group, sizeof(group), "Mapping %u", i + 1);
        g_key_file_set_integer(kf, group, "id", (int) m->id);
        keyfile_set_string_safe(kf, group, "name", m->name);
        keyfile_set_string_safe(kf, group, "device", m->device);
        g_key_file_set_integer(kf, group, "event_type", m->event_type);
        g_key_file_set_integer(kf, group, "channel", m->channel);
        g_key_file_set_integer(kf, group, "control", m->control);
        g_key_file_set_integer(kf, group, "mode", m->mode);
        g_key_file_set_integer(kf, group, "input_min", m->input_min);
        g_key_file_set_integer(kf, group, "input_max", m->input_max);
        g_key_file_set_integer(kf, group, "output_min", m->output_min);
        g_key_file_set_integer(kf, group, "output_max", m->output_max);
        g_key_file_set_boolean(kf, group, "output_limit_enabled", m->output_limit_enabled != 0);
        g_key_file_set_integer(kf, group, "output_limit_min", m->output_limit_min);
        g_key_file_set_integer(kf, group, "output_limit_max", m->output_limit_max);
        g_key_file_set_boolean(kf, group, "output_center_enabled", m->output_center_enabled != 0);
        g_key_file_set_integer(kf, group, "output_center", m->output_center);
        g_key_file_set_integer(kf, group, "deadzone", m->deadzone);
        g_key_file_set_boolean(kf, group, "invert", m->invert != 0);
        g_key_file_set_boolean(kf, group, "enabled", m->enabled != 0);
        g_key_file_set_integer(kf, group, "action_type", m->action.type);
        g_key_file_set_integer(kf, group, "vims_id", m->action.vims_id);
        g_key_file_set_integer(kf, group, "bundle_id", m->action.bundle_id);
        keyfile_set_string_safe(kf, group, "args", m->action.args_template);
        keyfile_set_string_safe(kf, group, "raw", m->action.raw_message);
        keyfile_set_string_safe(kf, group, "raw_off", m->action.raw_off_message);
        keyfile_set_string_safe(kf, group, "raw_on", m->action.raw_on_message);
    }

    gsize length = 0;
    gchar *data = g_key_file_to_data(kf, &length, error);
    int ok = data && g_file_set_contents(filename, data, (gssize) length, error);
    g_free(data);
    g_key_file_free(kf);
    return ok;
}

static char *midi_upgrade_fx_args_template(int vims_id, char *args)
{
    const char *token = NULL;
    if(vims_id == VIMS_CHAIN_ENTRY_SET_ARG_VAL)
        token = "$VALUE";
    else if(vims_id == VIMS_CHAIN_ENTRY_SET_NARG_VAL)
        token = "$NORM";
    else
        return args;

    if(!args || strstr(args, token) || midi_template_has_dynamic_value(args))
        return args;

    char *copy = g_strdup(args);
    g_strstrip(copy);
    int tokens = 0;
    char *last = NULL;
    for(char *p = copy; *p;) {
        while(*p && g_ascii_isspace(*p))
            p++;
        if(!*p)
            break;
        last = p;
        tokens++;
        while(*p && !g_ascii_isspace(*p))
            p++;
        if(*p)
            *p++ = '\0';
    }

    if(tokens != 4 || !last) {
        g_free(copy);
        return args;
    }

    char *end = NULL;
    g_ascii_strtod(last, &end);
    if(!end || *end != '\0') {
        g_free(copy);
        return args;
    }
    g_free(copy);

    char *trimmed = g_strdup(args);
    g_strstrip(trimmed);
    char *tail = trimmed + strlen(trimmed);
    while(tail > trimmed && !g_ascii_isspace(tail[-1]))
        tail--;
    *tail = '\0';
    g_strchomp(trimmed);
    char *upgraded = g_strdup_printf("%s %s", trimmed, token);
    g_free(trimmed);
    g_free(args);
    return upgraded;
}

static char *keyfile_get_optional_string(GKeyFile *kf, const char *group,
                                         const char *key)
{
    GError *local = NULL;
    char *value = g_key_file_get_string(kf, group, key, &local);
    if(local) {
        g_error_free(local);
        return NULL;
    }
    if(value && value[0])
        return value;
    g_free(value);
    return NULL;
}

int vj_midi_map_load(VjMidiMap *map, const char *filename, GError **error)
{
    if(!map || !filename)
        return 0;

    GKeyFile *kf = g_key_file_new();
    if(!g_key_file_load_from_file(kf, filename, G_KEY_FILE_NONE, error)) {
        g_key_file_free(kf);
        return 0;
    }

    int version = g_key_file_get_integer(kf, "MIDI", "version", error);
    if(version != 2) {
        if(error && !*error)
            g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                        "Unsupported MIDI mapping format version %d", version);
        g_key_file_free(kf);
        return 0;
    }

    int count = g_key_file_get_integer(kf, "MIDI", "mapping_count", error);
    if(error && *error) {
        g_key_file_free(kf);
        return 0;
    }
    if(count < 0 || count > VJ_MIDI_MAX_MAPPINGS) {
        if(error && !*error)
            g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                        "Invalid MIDI mapping count %d", count);
        g_key_file_free(kf);
        return 0;
    }

    VjMidiMap *tmp = vj_midi_map_new();
    tmp->send = map->send;
    tmp->send_data = map->send_data;

    for(int i = 0; i < count; i++) {
        char group[64];
        g_snprintf(group, sizeof(group), "Mapping %d", i + 1);
        VjMidiMapping *m = vj_midi_mapping_new();
        m->id = (guint) g_key_file_get_integer(kf, group, "id", NULL);
        g_free(m->name); m->name = keyfile_get_optional_string(kf, group, "name");
        g_free(m->device); m->device = keyfile_get_optional_string(kf, group, "device");
        if(!m->device) m->device = g_strdup(VJ_MIDI_ANY_DEVICE);
        m->event_type = g_key_file_get_integer(kf, group, "event_type", NULL);
        m->channel = g_key_file_get_integer(kf, group, "channel", NULL);
        m->control = g_key_file_get_integer(kf, group, "control", NULL);
        m->mode = g_key_file_get_integer(kf, group, "mode", NULL);
        m->input_min = g_key_file_get_integer(kf, group, "input_min", NULL);
        m->input_max = g_key_file_get_integer(kf, group, "input_max", NULL);
        m->output_min = g_key_file_get_integer(kf, group, "output_min", NULL);
        m->output_max = g_key_file_get_integer(kf, group, "output_max", NULL);
        if(g_key_file_has_key(kf, group, "output_limit_enabled", NULL)) {
            m->output_limit_enabled = g_key_file_get_boolean(kf, group, "output_limit_enabled", NULL);
            m->output_limit_min = g_key_file_get_integer(kf, group, "output_limit_min", NULL);
            m->output_limit_max = g_key_file_get_integer(kf, group, "output_limit_max", NULL);
        } else {
            m->output_limit_enabled = 0;
            m->output_limit_min = MIN(m->output_min, m->output_max);
            m->output_limit_max = MAX(m->output_min, m->output_max);
        }
        m->output_center_enabled = g_key_file_get_boolean(kf, group, "output_center_enabled", NULL);
        m->output_center = g_key_file_get_integer(kf, group, "output_center", NULL);
        m->deadzone = g_key_file_get_integer(kf, group, "deadzone", NULL);
        m->invert = g_key_file_get_boolean(kf, group, "invert", NULL);
        m->enabled = g_key_file_get_boolean(kf, group, "enabled", NULL);
        m->action.type = g_key_file_get_integer(kf, group, "action_type", NULL);
        m->action.vims_id = g_key_file_get_integer(kf, group, "vims_id", NULL);
        m->action.bundle_id = g_key_file_get_integer(kf, group, "bundle_id", NULL);
        m->action.args_template = keyfile_get_optional_string(kf, group, "args");
        if(m->action.type == VJ_MIDI_ACTION_VIMS)
            m->action.args_template = midi_upgrade_fx_args_template(m->action.vims_id,
                                                                    m->action.args_template);
        m->action.raw_message = keyfile_get_optional_string(kf, group, "raw");
        m->action.raw_off_message = keyfile_get_optional_string(kf, group, "raw_off");
        m->action.raw_on_message = keyfile_get_optional_string(kf, group, "raw_on");
        m->last_output = m->output_min;

        char validation[192];
        if(!vj_midi_mapping_validate(m, validation, sizeof(validation))) {
            if(error && !*error)
                g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "Mapping %d is invalid: %s", i + 1, validation);
            vj_midi_mapping_free(m);
            vj_midi_map_free(tmp);
            g_key_file_free(kf);
            return 0;
        }
        if(!vj_midi_map_add(tmp, m)) {
            if(error && !*error)
                g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "Could not add Mapping %d", i + 1);
            vj_midi_mapping_free(m);
            vj_midi_map_free(tmp);
            g_key_file_free(kf);
            return 0;
        }
    }

    g_ptr_array_free(map->mappings, TRUE);
    map->mappings = tmp->mappings;
    map->next_id = tmp->next_id;
    tmp->mappings = g_ptr_array_new_with_free_func((GDestroyNotify) vj_midi_mapping_free);
    vj_midi_map_free(tmp);
    g_key_file_free(kf);
    return 1;
}

char *vj_midi_mapping_describe(const VjMidiMapping *mapping)
{
    if(!mapping)
        return g_strdup("");
    const char *device = mapping->device && strcmp(mapping->device, VJ_MIDI_ANY_DEVICE) != 0
        ? mapping->device : "Any device";
    char channel[16];
    char control[16];
    if(mapping->channel == VJ_MIDI_ANY_CHANNEL)
        g_strlcpy(channel, "Any", sizeof(channel));
    else
        g_snprintf(channel, sizeof(channel), "%d", mapping->channel + 1);
    if(mapping->control == VJ_MIDI_ANY_CONTROL)
        g_strlcpy(control, "Any", sizeof(control));
    else
        g_snprintf(control, sizeof(control), "%d", mapping->control);

    return g_strdup_printf("%s · %s Ch %s / %s · %s → %s",
                           device,
                           vj_midi_event_type_name(mapping->event_type),
                           channel,
                           control,
                           vj_midi_mode_name(mapping->mode),
                           vj_midi_action_type_name(mapping->action.type));
}
