/* Gveejay Reloaded - graphical interface for VeeJay
 *       (C) 2026 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef VJ_MIDI_ENGINE_H
#define VJ_MIDI_ENGINE_H

#include <glib.h>
#include <stddef.h>

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
typedef struct _VjMidiContext VjMidiContext;
typedef struct _VjMidiMap VjMidiMap;

typedef void (*VjMidiEventFunc)(const VjMidiEvent *event, void *user_data);

VjMidiEngine *vj_midi_engine_new(void);
void          vj_midi_engine_free(VjMidiEngine *engine);
int           vj_midi_engine_handle_events(VjMidiEngine *engine);
void          vj_midi_engine_set_event_callback(VjMidiEngine *engine,
                                                 VjMidiEventFunc callback,
                                                 void *user_data);
int           vj_midi_engine_scan_devices(VjMidiEngine *engine,
                                           VjMidiDeviceInfo **devices);
void          vj_midi_engine_free_devices(VjMidiDeviceInfo *devices);
int           vj_midi_engine_connect(VjMidiEngine *engine, int client, int port);
int           vj_midi_engine_disconnect(VjMidiEngine *engine, int client, int port);
int           vj_midi_engine_available(const VjMidiEngine *engine);

VjMidiEngine *vj_midi_context_engine(void *context);
VjMidiMap    *vj_midi_context_map(void *context);
void         *vj_midi_context_builder(void *context);
void         *vj_midi_context_timeline(void *context);
int           vj_midi_context_learning(void *context);
int           vj_midi_context_dispatch_enabled(void *context);
int           vj_midi_context_last_event(void *context, VjMidiEvent *event);
void          vj_midi_context_set_learning(void *context, int enabled);
void          vj_midi_context_set_dispatch_enabled(void *context, int enabled);
void          vj_midi_context_mapping_changed(void *context);

/* Reloaded-facing lifecycle entry points. */
void *vj_midi_new(void *mw, void *timeline);
int   vj_midi_handle_events(void *context);
void  vj_midi_play(void *context, int play);
void  vj_midi_learn(void *context, int start);
void  vj_midi_reset(void *context);
void  vj_midi_load(void *context, const char *filename);
void  vj_midi_save(void *context, const char *filename);

G_END_DECLS

#endif
