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
#ifndef VJ_MIDI_ENGINE_H
#define VJ_MIDI_ENGINE_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    VJ_MIDI_EVENT_UNKNOWN = 0,
    VJ_MIDI_EVENT_CC,
    VJ_MIDI_EVENT_CC14,
    VJ_MIDI_EVENT_NRPN,
    VJ_MIDI_EVENT_RPN,
    VJ_MIDI_EVENT_PITCH_BEND,
    VJ_MIDI_EVENT_NOTE_ON,
    VJ_MIDI_EVENT_NOTE_OFF,
    VJ_MIDI_EVENT_KEY_PRESSURE,
    VJ_MIDI_EVENT_CHANNEL_PRESSURE,
    VJ_MIDI_EVENT_PROGRAM_CHANGE
} VjMidiEventType;

typedef struct {
    VjMidiEventType type;
    int raw_type;
    int channel;
    int control;
    int value;
    int value_min;
    int value_max;
    int source_client;
    int source_port;
    char device_name[128];
} VjMidiEvent;

typedef struct {
    int client;
    int port;
    int connected;
    char name[128];
} VjMidiDeviceInfo;

typedef struct _VjMidiEngine VjMidiEngine;
typedef void (*VjMidiEventFunc)(const VjMidiEvent *event, void *user_data);

VjMidiEngine *vj_midi_engine_new(void);
void          vj_midi_engine_free(VjMidiEngine *engine);
int           vj_midi_engine_available(const VjMidiEngine *engine);
int           vj_midi_engine_handle_events(VjMidiEngine *engine);
void          vj_midi_engine_set_event_callback(VjMidiEngine *engine,
                                                 VjMidiEventFunc callback,
                                                 void *user_data);
int           vj_midi_engine_scan_devices(VjMidiEngine *engine,
                                           VjMidiDeviceInfo **devices);
void          vj_midi_engine_free_devices(VjMidiDeviceInfo *devices);
int           vj_midi_engine_connect(VjMidiEngine *engine, int client, int port);
int           vj_midi_engine_disconnect(VjMidiEngine *engine, int client, int port);

const char   *vj_midi_event_type_name(VjMidiEventType type);
void          vj_midi_event_value_range(VjMidiEventType type, int *min_value, int *max_value);
void          vj_midi_event_control_range(VjMidiEventType type, int *min_control, int *max_control);

G_END_DECLS

#endif
