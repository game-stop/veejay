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
#include <config.h>
#include "vj-midi-engine.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#include <poll.h>

struct _VjMidiEngine {
    snd_seq_t *seq;
    int port_id;
    struct pollfd *pfd;
    int npfd;
    VjMidiEventFunc callback;
    void *callback_data;
    int cc14_msb[16][32];
    gint64 cc14_msb_time[16][32];
    int cc14_msb_client[16][32];
    int cc14_msb_port[16][32];
};

static VjMidiEventType midi_event_type(int type)
{
    switch(type) {
        case SND_SEQ_EVENT_CONTROLLER:  return VJ_MIDI_EVENT_CC;
        case SND_SEQ_EVENT_CONTROL14:   return VJ_MIDI_EVENT_CC14;
        case SND_SEQ_EVENT_NONREGPARAM: return VJ_MIDI_EVENT_NRPN;
        case SND_SEQ_EVENT_REGPARAM:    return VJ_MIDI_EVENT_RPN;
        case SND_SEQ_EVENT_PITCHBEND:   return VJ_MIDI_EVENT_PITCH_BEND;
        case SND_SEQ_EVENT_NOTEON:      return VJ_MIDI_EVENT_NOTE_ON;
        case SND_SEQ_EVENT_NOTEOFF:     return VJ_MIDI_EVENT_NOTE_OFF;
        case SND_SEQ_EVENT_KEYPRESS:    return VJ_MIDI_EVENT_KEY_PRESSURE;
        case SND_SEQ_EVENT_CHANPRESS:   return VJ_MIDI_EVENT_CHANNEL_PRESSURE;
        case SND_SEQ_EVENT_PGMCHANGE:   return VJ_MIDI_EVENT_PROGRAM_CHANGE;
        default:                        return VJ_MIDI_EVENT_UNKNOWN;
    }
}

const char *vj_midi_event_type_name(VjMidiEventType type)
{
    switch(type) {
        case VJ_MIDI_EVENT_CC:               return "CC";
        case VJ_MIDI_EVENT_CC14:             return "CC14";
        case VJ_MIDI_EVENT_NRPN:             return "NRPN";
        case VJ_MIDI_EVENT_RPN:              return "RPN";
        case VJ_MIDI_EVENT_PITCH_BEND:       return "Pitch Bend";
        case VJ_MIDI_EVENT_NOTE_ON:          return "Note On";
        case VJ_MIDI_EVENT_NOTE_OFF:         return "Note Off";
        case VJ_MIDI_EVENT_KEY_PRESSURE:     return "Key Pressure";
        case VJ_MIDI_EVENT_CHANNEL_PRESSURE: return "Channel Pressure";
        case VJ_MIDI_EVENT_PROGRAM_CHANGE:   return "Program Change";
        default:                             return "Unknown";
    }
}

void vj_midi_event_value_range(VjMidiEventType type, int *min_value, int *max_value)
{
    int lo = 0;
    int hi = 127;

    switch(type) {
        case VJ_MIDI_EVENT_CC14:
        case VJ_MIDI_EVENT_NRPN:
        case VJ_MIDI_EVENT_RPN:
            hi = 16383;
            break;
        case VJ_MIDI_EVENT_PITCH_BEND:
            lo = -8192;
            hi = 8191;
            break;
        default:
            break;
    }

    if(min_value)
        *min_value = lo;
    if(max_value)
        *max_value = hi;
}

void vj_midi_event_control_range(VjMidiEventType type, int *min_control, int *max_control)
{
    int lo = 0;
    int hi = 127;

    switch(type) {
        case VJ_MIDI_EVENT_CC14:
            hi = 31;
            break;
        case VJ_MIDI_EVENT_NRPN:
        case VJ_MIDI_EVENT_RPN:
            hi = 16383;
            break;
        case VJ_MIDI_EVENT_PITCH_BEND:
        case VJ_MIDI_EVENT_CHANNEL_PRESSURE:
            hi = 0;
            break;
        default:
            break;
    }

    if(min_control)
        *min_control = lo;
    if(max_control)
        *max_control = hi;
}

static void midi_source_name(VjMidiEngine *engine, int client, int port,
                             char *dst, size_t dst_size)
{
    snd_seq_client_info_t *ci;
    snd_seq_port_info_t *pi;
    const char *client_name = NULL;
    const char *port_name = NULL;

    snd_seq_client_info_alloca(&ci);
    snd_seq_port_info_alloca(&pi);

    if(snd_seq_get_any_client_info(engine->seq, client, ci) >= 0)
        client_name = snd_seq_client_info_get_name(ci);
    if(snd_seq_get_any_port_info(engine->seq, client, port, pi) >= 0)
        port_name = snd_seq_port_info_get_name(pi);

    if(client_name && port_name && g_strcmp0(client_name, port_name) != 0)
        g_snprintf(dst, dst_size, "%s / %s", client_name, port_name);
    else if(client_name)
        g_strlcpy(dst, client_name, dst_size);
    else if(port_name)
        g_strlcpy(dst, port_name, dst_size);
    else
        g_snprintf(dst, dst_size, "%d:%d", client, port);
}

static int midi_normalize_event(VjMidiEngine *engine,
                                const snd_seq_event_t *ev,
                                VjMidiEvent *out)
{
    memset(out, 0, sizeof(*out));
    out->raw_type = ev->type;
    out->type = midi_event_type(ev->type);
    if(out->type == VJ_MIDI_EVENT_UNKNOWN)
        return 0;

    out->source_client = ev->source.client;
    out->source_port = ev->source.port;
    midi_source_name(engine, out->source_client, out->source_port,
                     out->device_name, sizeof(out->device_name));

    switch(ev->type) {
        case SND_SEQ_EVENT_CONTROLLER:
            out->channel = ev->data.control.channel;
            out->control = ev->data.control.param;
            out->value = ev->data.control.value;
            out->value_min = 0;
            out->value_max = 127;
            break;
        case SND_SEQ_EVENT_CONTROL14:
        case SND_SEQ_EVENT_NONREGPARAM:
        case SND_SEQ_EVENT_REGPARAM:
            out->channel = ev->data.control.channel;
            out->control = ev->data.control.param;
            out->value = ev->data.control.value;
            out->value_min = 0;
            out->value_max = 16383;
            break;
        case SND_SEQ_EVENT_PITCHBEND:
            out->channel = ev->data.control.channel;
            out->control = 0;
            out->value = ev->data.control.value;
            out->value_min = -8192;
            out->value_max = 8191;
            break;
        case SND_SEQ_EVENT_NOTEON:
            out->channel = ev->data.note.channel;
            out->control = ev->data.note.note;
            out->value = ev->data.note.velocity;
            out->value_min = 0;
            out->value_max = 127;
            if(out->value == 0)
                out->type = VJ_MIDI_EVENT_NOTE_OFF;
            break;
        case SND_SEQ_EVENT_NOTEOFF:
            out->channel = ev->data.note.channel;
            out->control = ev->data.note.note;
            out->value = ev->data.note.velocity;
            out->value_min = 0;
            out->value_max = 127;
            break;
        case SND_SEQ_EVENT_KEYPRESS:
            out->channel = ev->data.note.channel;
            out->control = ev->data.note.note;
            out->value = ev->data.note.velocity;
            out->value_min = 0;
            out->value_max = 127;
            break;
        case SND_SEQ_EVENT_CHANPRESS:
            out->channel = ev->data.control.channel;
            out->control = 0;
            out->value = ev->data.control.value;
            out->value_min = 0;
            out->value_max = 127;
            break;
        case SND_SEQ_EVENT_PGMCHANGE:
            out->channel = ev->data.control.channel;
            out->control = ev->data.control.value;
            out->value = ev->data.control.value;
            out->value_min = 0;
            out->value_max = 127;
            break;
        default:
            return 0;
    }

    return 1;
}

static int midi_is_connected(VjMidiEngine *engine, int client, int port)
{
    snd_seq_port_subscribe_t *sub;
    snd_seq_addr_t sender;
    snd_seq_addr_t receiver;

    snd_seq_port_subscribe_alloca(&sub);
    sender.client = client;
    sender.port = port;
    receiver.client = snd_seq_client_id(engine->seq);
    receiver.port = engine->port_id;
    snd_seq_port_subscribe_set_sender(sub, &sender);
    snd_seq_port_subscribe_set_dest(sub, &receiver);
    return snd_seq_get_port_subscription(engine->seq, sub) >= 0;
}

VjMidiEngine *vj_midi_engine_new(void)
{
    VjMidiEngine *engine = vj_calloc(sizeof(*engine));

    int err = snd_seq_open(&engine->seq, "default",
                           SND_SEQ_OPEN_DUPLEX | SND_SEQ_NONBLOCK, 0);
    if(err < 0) {
        veejay_msg(VEEJAY_MSG_WARNING, "MIDI: unable to open ALSA sequencer: %s", snd_strerror(err));
        return engine;
    }

    snd_seq_set_client_name(engine->seq, "Veejay Reloaded");
    engine->port_id = snd_seq_create_simple_port(
        engine->seq,
        "MIDI Control",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION);

    if(engine->port_id < 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "MIDI: unable to create ALSA input port: %s",
                   snd_strerror(engine->port_id));
        snd_seq_close(engine->seq);
        engine->seq = NULL;
        return engine;
    }

    engine->npfd = snd_seq_poll_descriptors_count(engine->seq, POLLIN);
    if(engine->npfd < 0) {
        veejay_msg(VEEJAY_MSG_WARNING, "MIDI: unable to obtain ALSA poll descriptors: %s",
                   snd_strerror(engine->npfd));
        engine->npfd = 0;
    } else if(engine->npfd > 0) {
        engine->pfd = vj_calloc((size_t) engine->npfd * sizeof(*engine->pfd));
        err = snd_seq_poll_descriptors(engine->seq, engine->pfd, engine->npfd, POLLIN);
        if(err < 0) {
            veejay_msg(VEEJAY_MSG_WARNING, "MIDI: unable to initialize ALSA poll descriptors: %s",
                       snd_strerror(err));
            free(engine->pfd);
            engine->pfd = NULL;
            engine->npfd = 0;
        }
    }

    veejay_msg(VEEJAY_MSG_INFO,
               "MIDI: Reloaded input ready as ALSA client %d port %d",
               snd_seq_client_id(engine->seq), engine->port_id);
    return engine;
}

void vj_midi_engine_free(VjMidiEngine *engine)
{
    if(!engine)
        return;
    if(engine->seq)
        snd_seq_close(engine->seq);
    free(engine->pfd);
    free(engine);
}

int vj_midi_engine_available(const VjMidiEngine *engine)
{
    return engine && engine->seq && engine->port_id >= 0;
}

void vj_midi_engine_set_event_callback(VjMidiEngine *engine,
                                       VjMidiEventFunc callback,
                                       void *user_data)
{
    if(!engine)
        return;
    engine->callback = callback;
    engine->callback_data = user_data;
}

int vj_midi_engine_handle_events(VjMidiEngine *engine)
{
    int count = 0;

    if(!vj_midi_engine_available(engine) || engine->npfd <= 0)
        return 0;
    int ready = poll(engine->pfd, engine->npfd, 0);
    if(ready < 0) {
        if(errno != EINTR)
            veejay_msg(VEEJAY_MSG_WARNING, "MIDI: ALSA poll failed: %s", strerror(errno));
        return 0;
    }
    if(ready == 0)
        return 0;

    int pending = 0;
    while((pending = snd_seq_event_input_pending(engine->seq, 1)) > 0) {
        snd_seq_event_t *ev = NULL;
        int err = snd_seq_event_input(engine->seq, &ev);
        if(err == -EAGAIN || err == -ENOSPC)
            break;
        if(err < 0) {
            veejay_msg(VEEJAY_MSG_WARNING, "MIDI: ALSA event input failed: %s", snd_strerror(err));
            break;
        }
        if(!ev)
            break;

        VjMidiEvent event;
        if(midi_normalize_event(engine, ev, &event)) {
            if(engine->callback)
                engine->callback(&event, engine->callback_data);
            count++;

            if(ev->type == SND_SEQ_EVENT_CONTROLLER &&
               event.channel >= 0 && event.channel < 16) {
                int cc = event.control;
                gint64 now = g_get_monotonic_time();
                if(cc >= 0 && cc < 32) {
                    engine->cc14_msb[event.channel][cc] = event.value;
                    engine->cc14_msb_time[event.channel][cc] = now;
                    engine->cc14_msb_client[event.channel][cc] = event.source_client;
                    engine->cc14_msb_port[event.channel][cc] = event.source_port;
                } else if(cc >= 32 && cc < 64) {
                    int msb_cc = cc - 32;
                    gint64 msb_time = engine->cc14_msb_time[event.channel][msb_cc];
                    if(msb_time > 0 && now - msb_time <= 100000 &&
                       engine->cc14_msb_client[event.channel][msb_cc] == event.source_client &&
                       engine->cc14_msb_port[event.channel][msb_cc] == event.source_port) {
                        VjMidiEvent highres = event;
                        highres.type = VJ_MIDI_EVENT_CC14;
                        highres.control = msb_cc;
                        highres.value = (engine->cc14_msb[event.channel][msb_cc] << 7) |
                                        (event.value & 0x7f);
                        highres.value_min = 0;
                        highres.value_max = 16383;
                        if(engine->callback)
                            engine->callback(&highres, engine->callback_data);
                        count++;
                    }
                }
            }
        }
        snd_seq_free_event(ev);
    }
    if(pending < 0)
        veejay_msg(VEEJAY_MSG_WARNING, "MIDI: ALSA pending-event query failed: %s", snd_strerror(pending));

    return count;
}

int vj_midi_engine_scan_devices(VjMidiEngine *engine, VjMidiDeviceInfo **devices)
{
    GArray *array;
    snd_seq_client_info_t *ci;
    snd_seq_port_info_t *pi;
    int own_client;

    if(devices)
        *devices = NULL;
    if(!devices || !vj_midi_engine_available(engine))
        return 0;

    array = g_array_new(FALSE, FALSE, sizeof(VjMidiDeviceInfo));
    own_client = snd_seq_client_id(engine->seq);
    snd_seq_client_info_alloca(&ci);
    snd_seq_port_info_alloca(&pi);
    snd_seq_client_info_set_client(ci, -1);

    while(snd_seq_query_next_client(engine->seq, ci) >= 0) {
        int client = snd_seq_client_info_get_client(ci);
        if(client == own_client || client == SND_SEQ_CLIENT_SYSTEM)
            continue;

        snd_seq_port_info_set_client(pi, client);
        snd_seq_port_info_set_port(pi, -1);
        while(snd_seq_query_next_port(engine->seq, pi) >= 0) {
            unsigned int caps = snd_seq_port_info_get_capability(pi);
            if((caps & SND_SEQ_PORT_CAP_READ) == 0 ||
               (caps & SND_SEQ_PORT_CAP_SUBS_READ) == 0)
                continue;

            VjMidiDeviceInfo info;
            memset(&info, 0, sizeof(info));
            info.client = client;
            info.port = snd_seq_port_info_get_port(pi);
            info.connected = midi_is_connected(engine, info.client, info.port);
            midi_source_name(engine, info.client, info.port,
                             info.name, sizeof(info.name));
            g_array_append_val(array, info);
        }
    }

    int count = (int) array->len;
    if(count > 0)
        *devices = (VjMidiDeviceInfo *) g_array_free(array, FALSE);
    else
        g_array_free(array, TRUE);
    return count;
}

void vj_midi_engine_free_devices(VjMidiDeviceInfo *devices)
{
    g_free(devices);
}

int vj_midi_engine_connect(VjMidiEngine *engine, int client, int port)
{
    if(!vj_midi_engine_available(engine))
        return 0;
    if(midi_is_connected(engine, client, port))
        return 1;
    int err = snd_seq_connect_from(engine->seq, engine->port_id, client, port);
    if(err < 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "MIDI: unable to connect ALSA source %d:%d: %s",
                   client, port, snd_strerror(err));
        return 0;
    }
    return 1;
}

int vj_midi_engine_disconnect(VjMidiEngine *engine, int client, int port)
{
    if(!vj_midi_engine_available(engine))
        return 0;
    if(!midi_is_connected(engine, client, port))
        return 1;
    int err = snd_seq_disconnect_from(engine->seq, engine->port_id, client, port);
    if(err < 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "MIDI: unable to disconnect ALSA source %d:%d: %s",
                   client, port, snd_strerror(err));
        return 0;
    }
    return 1;
}

#else

struct _VjMidiEngine {
    VjMidiEventFunc callback;
    void *callback_data;
};

const char *vj_midi_event_type_name(VjMidiEventType type)
{
    (void) type;
    return "MIDI unavailable";
}

void vj_midi_event_value_range(VjMidiEventType type, int *min_value, int *max_value)
{
    int lo = 0;
    int hi = 127;

    switch(type) {
        case VJ_MIDI_EVENT_CC14:
        case VJ_MIDI_EVENT_NRPN:
        case VJ_MIDI_EVENT_RPN:
            hi = 16383;
            break;
        case VJ_MIDI_EVENT_PITCH_BEND:
            lo = -8192;
            hi = 8191;
            break;
        default:
            break;
    }

    if(min_value)
        *min_value = lo;
    if(max_value)
        *max_value = hi;
}

void vj_midi_event_control_range(VjMidiEventType type, int *min_control, int *max_control)
{
    int lo = 0;
    int hi = 127;

    switch(type) {
        case VJ_MIDI_EVENT_CC14:
            hi = 31;
            break;
        case VJ_MIDI_EVENT_NRPN:
        case VJ_MIDI_EVENT_RPN:
            hi = 16383;
            break;
        case VJ_MIDI_EVENT_PITCH_BEND:
        case VJ_MIDI_EVENT_CHANNEL_PRESSURE:
            hi = 0;
            break;
        default:
            break;
    }

    if(min_control)
        *min_control = lo;
    if(max_control)
        *max_control = hi;
}

VjMidiEngine *vj_midi_engine_new(void)
{
    return vj_calloc(sizeof(VjMidiEngine));
}

void vj_midi_engine_free(VjMidiEngine *engine)
{
    free(engine);
}

int vj_midi_engine_available(const VjMidiEngine *engine)
{
    (void) engine;
    return 0;
}

int vj_midi_engine_handle_events(VjMidiEngine *engine)
{
    (void) engine;
    return 0;
}

void vj_midi_engine_set_event_callback(VjMidiEngine *engine,
                                       VjMidiEventFunc callback,
                                       void *user_data)
{
    if(!engine)
        return;
    engine->callback = callback;
    engine->callback_data = user_data;
}

int vj_midi_engine_scan_devices(VjMidiEngine *engine, VjMidiDeviceInfo **devices)
{
    (void) engine;
    if(devices)
        *devices = NULL;
    return 0;
}

void vj_midi_engine_free_devices(VjMidiDeviceInfo *devices)
{
    g_free(devices);
}

int vj_midi_engine_connect(VjMidiEngine *engine, int client, int port)
{
    (void) engine; (void) client; (void) port;
    return 0;
}

int vj_midi_engine_disconnect(VjMidiEngine *engine, int client, int port)
{
    (void) engine; (void) client; (void) port;
    return 0;
}

#endif
