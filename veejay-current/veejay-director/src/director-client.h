/* director - Linux VeeJay - GVeejay GTK+-3/Glade User Interface 
 * (C) 2026 Niels Elburg <nwelburg@gmail.com>
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
#ifndef VEEJAY_DIRECTOR_CLIENT_H
#define VEEJAY_DIRECTOR_CLIENT_H

#include <glib.h>
#include "director-model.h"

G_BEGIN_DECLS

typedef enum {
    DIRECTOR_CLIENT_CONNECTED = 0,
    DIRECTOR_CLIENT_DISCONNECTED,
    DIRECTOR_CLIENT_INSTANCE_STATUS,
    DIRECTOR_CLIENT_OUTPUT_STATUS,
    DIRECTOR_CLIENT_PROJECTION_STATUS,
    DIRECTOR_CLIENT_PERF_STATUS,
    DIRECTOR_CLIENT_DEVICE_LIST,
    DIRECTOR_CLIENT_V4L_STATUS,
    DIRECTOR_CLIENT_ERROR
} DirectorClientEvent;

typedef void (*DirectorClientEventFunc)(DirectorClient *client,
                                        DirectorClientEvent event,
                                        const gchar *payload,
                                        gpointer user_data);

DirectorClient *director_client_new(DirectorInstance *instance,
                                    DirectorClientEventFunc callback,
                                    gpointer user_data);
void director_client_start(DirectorClient *client);
void director_client_stop(DirectorClient *client);
void director_client_free(DirectorClient *client);
DirectorInstance *director_client_get_instance(DirectorClient *client);

void director_client_send(DirectorClient *client, const gchar *command);
void director_client_refresh(DirectorClient *client);
void director_client_query_devices(DirectorClient *client);
void director_client_query_v4l(DirectorClient *client, gint stream_id);
void director_client_apply_output_graph(DirectorClient *client,
                                        const DirectorInstance *instance);

G_END_DECLS

#endif
