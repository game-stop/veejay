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
#include "director-presets.h"

#include <string.h>

static void preset_replace_string(gchar **target, const gchar *value)
{
    g_free(*target);
    *target = g_strdup(value ? value : "");
}

static void preset_set_canvas(DirectorInstance *instance, gint width, gint height)
{
    instance->input_width = width;
    instance->input_height = height;
    instance->output_width = width;
    instance->output_height = height;
    director_slice_set_identity(&instance->slices[0], width, height);
    for(gint i = 1; i < DIRECTOR_MAX_SLICES; i++) {
        director_slice_set_identity(&instance->slices[i], width, height);
        instance->slices[i].enabled = FALSE;
    }
}

static DirectorInstance *preset_engine(const gchar *id,
                                       DirectorRole role,
                                       gint port,
                                       gint width,
                                       gint height,
                                       gboolean headless)
{
    DirectorInstance *instance = director_instance_new(id, role);
    instance->port = port;
    preset_set_canvas(instance, width, height);
    instance->startup_mode = DIRECTOR_STARTUP_BLANK;
    instance->output_driver = headless ? 3 : 0;
    return instance;
}

static DirectorInstance *preset_program(const gchar *id, gint port,
                                        gint width, gint height,
                                        gboolean headless)
{
    return preset_engine(id, DIRECTOR_ROLE_PROGRAM, port,
                         width, height, headless);
}

static DirectorInstance *preset_standalone(const gchar *id, gint port,
                                           gint width, gint height,
                                           gboolean headless)
{
    return preset_engine(id, DIRECTOR_ROLE_STANDALONE, port,
                         width, height, headless);
}

static DirectorInstance *preset_output(const gchar *id, gint port,
                                       gint width, gint height,
                                       const DirectorInstance *source)
{
    DirectorInstance *instance = preset_engine(id, DIRECTOR_ROLE_OUTPUT, port,
                                               width, height, FALSE);
    instance->input_width = source ? source->output_width : width;
    instance->input_height = source ? source->output_height : height;
    if(source) {
        preset_replace_string(&instance->source_instance_id, source->id);
        preset_replace_string(&instance->source_host, source->host);
        instance->source_port = source->port;
    }
    return instance;
}

static DirectorInstance *preset_wall_screen(const gchar *id,
                                            gint port,
                                            gint width,
                                            gint height,
                                            const DirectorInstance *master,
                                            gint row,
                                            gint column)
{
    DirectorInstance *screen = preset_standalone(id, port, width, height, FALSE);
    screen->audio_enabled = FALSE;
    screen->audio_muted = TRUE;
    screen->audio_sync_thread = FALSE;
    screen->audio_beat_thread = FALSE;
    screen->legacy_viewport = FALSE;
    screen->no_keyboard = TRUE;
    screen->no_mouse = TRUE;
    preset_replace_string(&screen->split_master_instance_id, master->id);
    screen->split_row = row;
    screen->split_column = column;
    return screen;
}

static gboolean preset_add(DirectorShow *show, DirectorInstance *instance,
                           GError **error)
{
    if(director_show_add_instance(show, instance, error))
        return TRUE;
    director_instance_free(instance);
    return FALSE;
}

static void preset_wire_position(DirectorInstance *instance, gint x, gint y)
{
    instance->wiring_x = x;
    instance->wiring_y = y;
    instance->wiring_position_explicit = TRUE;
}

static void preset_stage_position(DirectorInstance *instance, gint x, gint y)
{
    instance->stage_x = x;
    instance->stage_y = y;
    instance->stage_position_explicit = TRUE;
}

static gchar *preset_wall_screen_id(gint columns, gint rows,
                                     gint column, gint row)
{
    if(columns == 2 && rows == 1)
        return g_strdup(column == 0 ? "screen-left" : "screen-right");
    if(columns == 1 && rows == 2)
        return g_strdup(row == 0 ? "screen-top" : "screen-bottom");
    if(columns == 2 && rows == 2) {
        static const gchar *ids[2][2] = {
            { "screen-top-left", "screen-top-right" },
            { "screen-bottom-left", "screen-bottom-right" }
        };
        return g_strdup(ids[row][column]);
    }
    return g_strdup_printf("screen-r%d-c%d", row + 1, column + 1);
}

static void preset_set_split_config_path(DirectorInstance *master)
{
    gchar *path = g_build_filename(master->working_directory,
                                   "director-video-wall.cfg", NULL);
    preset_replace_string(&master->split_screen_file, path);
    g_free(path);
}

static DirectorShow *preset_wall(gint columns, gint rows,
                                 gint screen_width, gint screen_height,
                                 GError **error)
{
    const gint canvas_width = screen_width * columns;
    const gint canvas_height = screen_height * rows;
    gchar *show_name = g_strdup_printf("%d×%d video wall", columns, rows);
    DirectorShow *show = director_show_new(show_name);
    g_free(show_name);

    DirectorInstance *master = preset_program("wall-master", 3490,
                                              canvas_width, canvas_height, TRUE);
    preset_set_split_config_path(master);
    preset_wire_position(master, 50, 130 + (rows - 1) * 70);
    if(!preset_add(show, master, error)) {
        director_show_free(show);
        return NULL;
    }

    gint index = 0;
    for(gint row = 0; row < rows; row++) {
        for(gint column = 0; column < columns; column++, index++) {
            gchar *screen_id = preset_wall_screen_id(columns, rows, column, row);
            DirectorInstance *screen = preset_wall_screen(
                screen_id,
                4490 + index * 10,
                screen_width, screen_height,
                master, row, column);
            g_free(screen_id);
            preset_stage_position(screen,
                                  column * screen_width,
                                  row * screen_height);
            preset_wire_position(screen, 470, 55 + index * 130);
            if(!preset_add(show, screen, error)) {
                director_show_free(show);
                return NULL;
            }
        }
    }

    show->dirty = FALSE;
    return show;
}

static void preset_configure_dual_projector_slice(DirectorInstance *output,
                                                  gint source_x,
                                                  gint source_width,
                                                  gint blend_left,
                                                  gint blend_right)
{
    DirectorSlice *slice = &output->slices[0];
    director_slice_set_identity(slice, output->output_width, output->output_height);
    slice->source_x = source_x;
    slice->source_width = source_width;
    slice->blend_left = blend_left;
    slice->blend_right = blend_right;
    for(gint i = 1; i < DIRECTOR_MAX_SLICES; i++)
        output->slices[i].enabled = FALSE;
}

DirectorShow *director_preset_create_dual_projector(gint screen_width,
                                                    gint screen_height,
                                                    gint overlap,
                                                    GError **error)
{
    if(screen_width < 16 || screen_height < 16 ||
       screen_width > 16384 || screen_height > 16384) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Projector dimensions must be between 16 and 16384 pixels");
        return NULL;
    }
    if(overlap < 0 || overlap >= screen_width) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Edge overlap must be between 0 and projector width - 1");
        return NULL;
    }

    const gint canvas_width = screen_width * 2 - overlap;
    if(canvas_width > 32768) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Combined projector canvas exceeds VeeJay's 32768-pixel project width");
        return NULL;
    }

    DirectorShow *show = director_show_new("Dual mapped projectors");
    DirectorInstance *program = preset_program("program", 3490,
                                               canvas_width, screen_height, TRUE);
    preset_wire_position(program, 50, 165);
    if(!preset_add(show, program, error)) {
        director_show_free(show);
        return NULL;
    }

    DirectorInstance *left = preset_output("projector-left", 4490,
                                           screen_width, screen_height, program);
    DirectorInstance *right = preset_output("projector-right", 4500,
                                            screen_width, screen_height, program);
    const gint source_width = CLAMP((gint)(((gint64)screen_width * 10000 + canvas_width / 2) /
                                           canvas_width), 1, 10000);
    const gint right_x = MAX(0, 10000 - source_width);
    preset_configure_dual_projector_slice(left, 0, source_width, 0, overlap);
    preset_configure_dual_projector_slice(right, right_x, 10000 - right_x, overlap, 0);
    preset_stage_position(left, 0, 0);
    preset_stage_position(right, screen_width - overlap, 0);
    preset_wire_position(left, 500, 75);
    preset_wire_position(right, 500, 255);

    if(!preset_add(show, left, error)) {
        director_instance_free(right);
        director_show_free(show);
        return NULL;
    }
    if(!preset_add(show, right, error)) {
        director_show_free(show);
        return NULL;
    }

    show->dirty = FALSE;
    return show;
}

DirectorShow *director_preset_create_wall(gint columns,
                                          gint rows,
                                          gint screen_width,
                                          gint screen_height,
                                          GError **error)
{
    if(columns < 1 || rows < 1 || columns > 8 || rows > 8) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Video-wall dimensions must be between 1×1 and 8×8 screens");
        return NULL;
    }
    if(columns * rows < 2) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "A video wall needs at least two screen engines");
        return NULL;
    }
    if(screen_width < 16 || screen_height < 16 ||
       screen_width > 16384 || screen_height > 16384) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Screen dimensions must be between 16 and 16384 pixels");
        return NULL;
    }
    if((gint64)screen_width * columns > 32768 ||
       (gint64)screen_height * rows > 32768) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Video-wall master canvas exceeds VeeJay's 32768-pixel project limit");
        return NULL;
    }
    return preset_wall(columns, rows, screen_width, screen_height, error);
}

static void preset_configure_preview(DirectorInstance *preview,
                                     const DirectorInstance *master,
                                     gint width,
                                     gint height)
{
    preview->control_mode = DIRECTOR_CONTROL_PREVIEW;
    preset_replace_string(&preview->master_instance_id, master->id);
    preset_replace_string(&preview->master_host, master->host);
    preview->master_port = master->port;
    preview->preview_forward_vims = FALSE;
    preview->preview_sync_samplelist = FALSE;
    preview->preview_headless = FALSE;
    preview->window_width = MIN(640, width);
    preview->window_height = MAX(16, (preview->window_width * height) / width);
}

const gchar *director_preset_name(DirectorPreset preset)
{
    switch(preset) {
        case DIRECTOR_PRESET_SINGLE_VEEJAY: return "Single VeeJay";
        case DIRECTOR_PRESET_MASTER_PREVIEW: return "Master + Preview";
        case DIRECTOR_PRESET_PROJECTOR_CALIBRATION: return "Projector Calibration";
        case DIRECTOR_PRESET_MAPPED_PROJECTOR: return "Mapped Projector";
        case DIRECTOR_PRESET_DUAL_MAPPED_PROJECTOR: return "Dual Projectors + Edge Blend";
        case DIRECTOR_PRESET_MASTER_PREVIEW_PROJECTOR:
            return "Master + Preview + Projector";
        case DIRECTOR_PRESET_WALL_2X1: return "2 × 1 Video Wall";
        case DIRECTOR_PRESET_WALL_1X2: return "1 × 2 Video Wall";
        case DIRECTOR_PRESET_WALL_2X2: return "2 × 2 Video Wall";
        case DIRECTOR_PRESET_CUSTOM_WALL: return "Custom Video Wall";
        default: return "Unknown setup";
    }
}

const gchar *director_preset_description(DirectorPreset preset)
{
    switch(preset) {
        case DIRECTOR_PRESET_SINGLE_VEEJAY:
            return "One self-contained VeeJay engine. Use this for normal playback on one local screen.";
        case DIRECTOR_PRESET_MASTER_PREVIEW:
            return "Two Program engines: Master stays live while Preview is the work-ahead engine.";
        case DIRECTOR_PRESET_PROJECTOR_CALIBRATION:
            return "One blank Output engine for test patterns, screen mapping and projection-mesh calibration before a video feed is connected.";
        case DIRECTOR_PRESET_MAPPED_PROJECTOR:
            return "A headless Program feeds one Output. The Output owns the physical projector, screen mapping and projection mesh.";
        case DIRECTOR_PRESET_DUAL_MAPPED_PROJECTOR:
            return "One headless Program feeds left and right Outputs with an overlapping crop and matching edge blend.";
        case DIRECTOR_PRESET_MASTER_PREVIEW_PROJECTOR:
            return "Master and Preview Programs for work-ahead control, plus one Output fed by Master for the physical projector.";
        case DIRECTOR_PRESET_WALL_2X1:
            return "One wide Program canvas is split by VeeJay's native video-wall engine and sent to two screen engines.";
        case DIRECTOR_PRESET_WALL_1X2:
            return "One tall Program canvas is split by VeeJay's native video-wall engine and sent to two screen engines.";
        case DIRECTOR_PRESET_WALL_2X2:
            return "One 2 × 2 Program canvas is split by VeeJay's native video-wall engine and sent to four screen engines.";
        case DIRECTOR_PRESET_CUSTOM_WALL:
            return "Choose 1–8 columns and rows. Director builds the native wall-master canvas and all screen engines automatically.";
        default:
            return "";
    }
}

DirectorShow *director_preset_create(DirectorPreset preset,
                                     gint screen_width,
                                     gint screen_height,
                                     GError **error)
{
    if(screen_width < 16 || screen_height < 16 ||
       screen_width > 16384 || screen_height > 16384) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Video dimensions must be between 16 and 16384 pixels");
        return NULL;
    }

    if(preset == DIRECTOR_PRESET_WALL_2X1)
        return director_preset_create_wall(2, 1, screen_width, screen_height, error);
    if(preset == DIRECTOR_PRESET_WALL_1X2)
        return director_preset_create_wall(1, 2, screen_width, screen_height, error);
    if(preset == DIRECTOR_PRESET_WALL_2X2)
        return director_preset_create_wall(2, 2, screen_width, screen_height, error);
    if(preset == DIRECTOR_PRESET_CUSTOM_WALL)
        return director_preset_create_wall(3, 1, screen_width, screen_height, error);
    if(preset == DIRECTOR_PRESET_DUAL_MAPPED_PROJECTOR)
        return director_preset_create_dual_projector(screen_width, screen_height,
                                                     MAX(0, screen_width / 10), error);

    const gchar *show_name =
        preset == DIRECTOR_PRESET_MASTER_PREVIEW ? "Master + Preview" :
        preset == DIRECTOR_PRESET_PROJECTOR_CALIBRATION ? "Projector calibration" :
        preset == DIRECTOR_PRESET_MAPPED_PROJECTOR ? "Mapped projector" :
        preset == DIRECTOR_PRESET_MASTER_PREVIEW_PROJECTOR ?
            "Master + Preview + Projector" : "Single VeeJay";
    DirectorShow *show = director_show_new(show_name);

    if(preset == DIRECTOR_PRESET_SINGLE_VEEJAY) {
        DirectorInstance *engine = preset_standalone("veejay", 3490,
                                                     screen_width, screen_height, FALSE);
        preset_stage_position(engine, 0, 0);
        preset_wire_position(engine, 160, 100);
        if(!preset_add(show, engine, error)) {
            director_show_free(show);
            return NULL;
        }
    }
    else if(preset == DIRECTOR_PRESET_MASTER_PREVIEW) {
        DirectorInstance *master = preset_program("master", 3490,
                                                  screen_width, screen_height, FALSE);
        master->control_mode = DIRECTOR_CONTROL_MASTER;
        preset_stage_position(master, 0, 0);
        preset_wire_position(master, 430, 110);
        if(!preset_add(show, master, error)) {
            director_show_free(show);
            return NULL;
        }

        DirectorInstance *preview = preset_program("preview", 3500,
                                                   screen_width, screen_height, FALSE);
        preset_configure_preview(preview, master, screen_width, screen_height);
        preset_stage_position(preview, screen_width + 80, 0);
        preset_wire_position(preview, 50, 110);
        if(!preset_add(show, preview, error)) {
            director_show_free(show);
            return NULL;
        }
    }
    else if(preset == DIRECTOR_PRESET_PROJECTOR_CALIBRATION) {
        DirectorInstance *output = preset_output("projector", 4490,
                                                 screen_width, screen_height, NULL);
        preset_stage_position(output, 0, 0);
        preset_wire_position(output, 420, 110);
        if(!preset_add(show, output, error)) {
            director_show_free(show);
            return NULL;
        }
    }
    else if(preset == DIRECTOR_PRESET_MAPPED_PROJECTOR) {
        DirectorInstance *program = preset_program("program", 3490,
                                                   screen_width, screen_height, TRUE);
        preset_wire_position(program, 50, 110);
        if(!preset_add(show, program, error)) {
            director_show_free(show);
            return NULL;
        }

        DirectorInstance *output = preset_output("projector", 4490,
                                                 screen_width, screen_height, program);
        preset_stage_position(output, 0, 0);
        preset_wire_position(output, 430, 110);
        if(!preset_add(show, output, error)) {
            director_show_free(show);
            return NULL;
        }
    }
    else if(preset == DIRECTOR_PRESET_MASTER_PREVIEW_PROJECTOR) {
        DirectorInstance *master = preset_program("master", 3490,
                                                  screen_width, screen_height, TRUE);
        master->control_mode = DIRECTOR_CONTROL_MASTER;
        preset_wire_position(master, 360, 110);
        if(!preset_add(show, master, error)) {
            director_show_free(show);
            return NULL;
        }

        DirectorInstance *preview = preset_program("preview", 3500,
                                                   screen_width, screen_height, FALSE);
        preset_configure_preview(preview, master, screen_width, screen_height);
        preset_stage_position(preview, screen_width + 80, 0);
        preset_wire_position(preview, 40, 110);
        if(!preset_add(show, preview, error)) {
            director_show_free(show);
            return NULL;
        }

        DirectorInstance *output = preset_output("projector", 4490,
                                                 screen_width, screen_height, master);
        preset_stage_position(output, 0, 0);
        preset_wire_position(output, 690, 110);
        if(!preset_add(show, output, error)) {
            director_show_free(show);
            return NULL;
        }
    }
    else {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Unknown Director setup");
        director_show_free(show);
        return NULL;
    }

    show->dirty = FALSE;
    return show;
}
