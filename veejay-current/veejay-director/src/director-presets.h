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
#ifndef VEEJAY_DIRECTOR_PRESETS_H
#define VEEJAY_DIRECTOR_PRESETS_H

#include "director-model.h"

G_BEGIN_DECLS

typedef enum {
    DIRECTOR_PRESET_SINGLE_VEEJAY = 0,
    DIRECTOR_PRESET_MASTER_PREVIEW,
    DIRECTOR_PRESET_PROJECTOR_CALIBRATION,
    DIRECTOR_PRESET_MAPPED_PROJECTOR,
    DIRECTOR_PRESET_DUAL_MAPPED_PROJECTOR,
    DIRECTOR_PRESET_MASTER_PREVIEW_PROJECTOR,
    DIRECTOR_PRESET_WALL_2X1,
    DIRECTOR_PRESET_WALL_1X2,
    DIRECTOR_PRESET_WALL_2X2,
    DIRECTOR_PRESET_CUSTOM_WALL,
    DIRECTOR_PRESET_COUNT
} DirectorPreset;

const gchar *director_preset_name(DirectorPreset preset);
const gchar *director_preset_description(DirectorPreset preset);
DirectorShow *director_preset_create(DirectorPreset preset,
                                     gint screen_width,
                                     gint screen_height,
                                     GError **error);
DirectorShow *director_preset_create_wall(gint columns,
                                          gint rows,
                                          gint screen_width,
                                          gint screen_height,
                                          GError **error);
DirectorShow *director_preset_create_dual_projector(gint screen_width,
                                                    gint screen_height,
                                                    gint overlap,
                                                    GError **error);

G_END_DECLS

#endif
