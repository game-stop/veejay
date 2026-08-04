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

/*
 * NDI(R) is a registered trademark of Vizrt NDI AB.
 */
#ifndef VEEJAY_DIRECTOR_NDI_H
#define VEEJAY_DIRECTOR_NDI_H

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct {
    gchar *name;
    gchar *url;
} DirectorNdiSource;

typedef struct _DirectorNdiDiscovery DirectorNdiDiscovery;

typedef enum {
    DIRECTOR_NDI_DISCOVERY_STARTING = 0,
    DIRECTOR_NDI_DISCOVERY_WATCHING,
    DIRECTOR_NDI_DISCOVERY_UNAVAILABLE,
    DIRECTOR_NDI_DISCOVERY_ERROR,
    DIRECTOR_NDI_DISCOVERY_STOPPED
} DirectorNdiDiscoveryState;

typedef struct {
    DirectorNdiDiscoveryState state;
    guint64 generation;
    gint64 observed_us;
    const gchar *runtime_version;
    const GPtrArray *sources;
    const gchar *error;
    gboolean forced;
} DirectorNdiDiscoveryUpdate;

typedef void (*DirectorNdiDiscoveryFunc)(DirectorNdiDiscovery *discovery,
                                         const DirectorNdiDiscoveryUpdate *update,
                                         gpointer user_data);

gboolean director_ndi_available(void);
const gchar *director_ndi_version(void);
GPtrArray *director_ndi_discover(guint timeout_ms, GError **error);
void director_ndi_source_free(DirectorNdiSource *source);

DirectorNdiDiscovery *director_ndi_discovery_new(DirectorNdiDiscoveryFunc callback,
                                                  gpointer user_data);
void director_ndi_discovery_start(DirectorNdiDiscovery *discovery);
void director_ndi_discovery_request_refresh(DirectorNdiDiscovery *discovery);
void director_ndi_discovery_stop(DirectorNdiDiscovery *discovery);
void director_ndi_discovery_free(DirectorNdiDiscovery *discovery);

void director_ndi_shutdown(void);

G_END_DECLS

#endif
