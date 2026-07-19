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
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>
#include "gtkpreviewcache.h"

#define GVR_PREVIEW_CACHE_VERSION 3
#define GVR_PREVIEW_CACHE_COLUMNS 16
#define GVR_PREVIEW_CACHE_SAVE_DELAY_MS 1800

struct _GvrPreviewCache {
    char *directory;
    char *state_hash;
    char *png_path;
    char *manifest_path;
    int tile_width;
    int tile_height;
    int total_slots;
    int columns;
    int rows;
    int tile_count;
    int *slot_to_tile;
    int *tile_to_slot;
    GdkPixbuf *atlas;
    gboolean dirty;
    guint save_source;
};

static char *gvr_preview_cache_safe_host(const char *host)
{
    const char *source = (host && *host) ? host : "localhost";
    char *safe = g_strdup(source);

    for(char *p = safe; *p; p++) {
        if(!g_ascii_isalnum(*p) && *p != '.' && *p != '-' && *p != '_')
            *p = '_';
    }

    return safe;
}

static void gvr_preview_cache_reset_mapping(GvrPreviewCache *cache)
{
    for(int i = 0; i < cache->total_slots; i++) {
        cache->slot_to_tile[i] = -1;
        cache->tile_to_slot[i] = -1;
    }
    cache->rows = 0;
    cache->tile_count = 0;
}

static void gvr_preview_cache_clear_paths(GvrPreviewCache *cache)
{
    g_clear_pointer(&cache->state_hash, g_free);
    g_clear_pointer(&cache->png_path, g_free);
    g_clear_pointer(&cache->manifest_path, g_free);
}

static void gvr_preview_cache_clear_atlas(GvrPreviewCache *cache)
{
    if(cache->atlas) {
        g_object_unref(cache->atlas);
        cache->atlas = NULL;
    }
    gvr_preview_cache_reset_mapping(cache);
}

static gboolean gvr_preview_cache_ensure_atlas(GvrPreviewCache *cache,
                                                int tile_count)
{
    GdkPixbuf *next;
    int needed_rows;

    if(tile_count <= 0)
        return FALSE;

    needed_rows = (tile_count + cache->columns - 1) / cache->columns;
    if(cache->atlas && needed_rows <= cache->rows)
        return TRUE;

    next = gdk_pixbuf_new(GDK_COLORSPACE_RGB,
                          FALSE,
                          8,
                          cache->columns * cache->tile_width,
                          needed_rows * cache->tile_height);
    if(!next)
        return FALSE;

    gdk_pixbuf_fill(next, 0x101116ff);
    if(cache->atlas) {
        gdk_pixbuf_copy_area(cache->atlas,
                             0,
                             0,
                             gdk_pixbuf_get_width(cache->atlas),
                             gdk_pixbuf_get_height(cache->atlas),
                             next,
                             0,
                             0);
        g_object_unref(cache->atlas);
    }

    cache->atlas = next;
    cache->rows = needed_rows;
    return TRUE;
}

static char *gvr_preview_cache_combined_hash(GvrPreviewCache *cache,
                                             const char *editlist_hash,
                                             const char *samplelist_hash)
{
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    char geometry[128];
    char *result;

    g_snprintf(geometry,
               sizeof(geometry),
               "gvr-preview-cache:%d:%d:%d:%d",
               GVR_PREVIEW_CACHE_VERSION,
               cache->tile_width,
               cache->tile_height,
               cache->total_slots);

    g_checksum_update(sum, (const guchar *)geometry, strlen(geometry));
    g_checksum_update(sum, (const guchar *)"\n", 1);
    g_checksum_update(sum, (const guchar *)editlist_hash, strlen(editlist_hash));
    g_checksum_update(sum, (const guchar *)"\n", 1);
    g_checksum_update(sum, (const guchar *)samplelist_hash, strlen(samplelist_hash));
    result = g_strdup(g_checksum_get_string(sum));
    g_checksum_free(sum);
    return result;
}

static gboolean gvr_preview_cache_manifest_valid(GvrPreviewCache *cache,
                                                 GKeyFile *key_file)
{
    GError *error = NULL;
    gint version = g_key_file_get_integer(key_file, "cache", "version", &error);
    gint tile_width;
    gint tile_height;
    gint total_slots;
    gchar *state_hash;
    gboolean valid;

    if(error) {
        g_error_free(error);
        return FALSE;
    }

    tile_width = g_key_file_get_integer(key_file, "cache", "tile-width", NULL);
    tile_height = g_key_file_get_integer(key_file, "cache", "tile-height", NULL);
    total_slots = g_key_file_get_integer(key_file, "cache", "total-slots", NULL);
    state_hash = g_key_file_get_string(key_file, "cache", "state-hash", NULL);
    valid = version == GVR_PREVIEW_CACHE_VERSION &&
            tile_width == cache->tile_width &&
            tile_height == cache->tile_height &&
            total_slots == cache->total_slots &&
            state_hash && cache->state_hash &&
            strcmp(state_hash, cache->state_hash) == 0;

    g_free(state_hash);
    return valid;
}

static void gvr_preview_cache_load(GvrPreviewCache *cache)
{
    GKeyFile *key_file;
    GError *error = NULL;
    GdkPixbuf *atlas;
    gsize slot_count = 0;
    gint *slots;
    int rows;

    if(!cache->manifest_path || !cache->png_path)
        return;

    key_file = g_key_file_new();
    if(!g_key_file_load_from_file(key_file,
                                  cache->manifest_path,
                                  G_KEY_FILE_NONE,
                                  &error)) {
        g_clear_error(&error);
        g_key_file_free(key_file);
        return;
    }

    if(!gvr_preview_cache_manifest_valid(cache, key_file)) {
        g_key_file_free(key_file);
        return;
    }

    slots = g_key_file_get_integer_list(key_file,
                                        "cache",
                                        "slots",
                                        &slot_count,
                                        NULL);
    if(!slots || slot_count == 0 || slot_count > (gsize)cache->total_slots) {
        g_free(slots);
        g_key_file_free(key_file);
        return;
    }

    for(gsize i = 0; i < slot_count; i++) {
        int slot = slots[i];
        if(slot < 0 || slot >= cache->total_slots ||
           cache->slot_to_tile[slot] >= 0) {
            g_free(slots);
            g_key_file_free(key_file);
            gvr_preview_cache_reset_mapping(cache);
            return;
        }
        cache->slot_to_tile[slot] = (int)i;
        cache->tile_to_slot[i] = slot;
    }

    rows = ((int)slot_count + cache->columns - 1) / cache->columns;
    atlas = gdk_pixbuf_new_from_file(cache->png_path, &error);
    if(!atlas) {
        g_clear_error(&error);
        g_free(slots);
        g_key_file_free(key_file);
        gvr_preview_cache_reset_mapping(cache);
        return;
    }

    if(gdk_pixbuf_get_width(atlas) != cache->columns * cache->tile_width ||
       gdk_pixbuf_get_height(atlas) != rows * cache->tile_height) {
        g_object_unref(atlas);
        g_free(slots);
        g_key_file_free(key_file);
        gvr_preview_cache_reset_mapping(cache);
        return;
    }

    cache->atlas = atlas;
    cache->rows = rows;
    cache->tile_count = (int)slot_count;
    g_free(slots);
    g_key_file_free(key_file);
}

static gboolean gvr_preview_cache_save_cb(gpointer data)
{
    GvrPreviewCache *cache = data;
    cache->save_source = 0;
    gvr_preview_cache_flush(cache);
    return G_SOURCE_REMOVE;
}

static void gvr_preview_cache_schedule_save(GvrPreviewCache *cache)
{
    if(cache->save_source)
        g_source_remove(cache->save_source);

    cache->save_source = g_timeout_add(GVR_PREVIEW_CACHE_SAVE_DELAY_MS,
                                       gvr_preview_cache_save_cb,
                                       cache);
}

GvrPreviewCache *gvr_preview_cache_new(const char *host,
                                       int port,
                                       int tile_width,
                                       int tile_height,
                                       int total_slots)
{
    GvrPreviewCache *cache;
    char *safe_host;
    char *leaf;
    const char *home;

    if(tile_width <= 0 || tile_height <= 0 || total_slots <= 0)
        return NULL;

    cache = g_new0(GvrPreviewCache, 1);
    cache->tile_width = tile_width;
    cache->tile_height = tile_height;
    cache->total_slots = total_slots;
    cache->columns = MIN(GVR_PREVIEW_CACHE_COLUMNS, total_slots);
    cache->slot_to_tile = g_new(gint, total_slots);
    cache->tile_to_slot = g_new(gint, total_slots);
    gvr_preview_cache_reset_mapping(cache);

    safe_host = gvr_preview_cache_safe_host(host);
    leaf = g_strdup_printf("%s-%d", safe_host, port);
    home = g_get_home_dir();
    cache->directory = g_build_filename(home,
                                        ".veejay",
                                        "reloaded",
                                        "previews",
                                        leaf,
                                        NULL);
    g_mkdir_with_parents(cache->directory, 0700);

    g_free(leaf);
    g_free(safe_host);
    return cache;
}

void gvr_preview_cache_free(GvrPreviewCache *cache)
{
    if(!cache)
        return;

    if(cache->save_source) {
        g_source_remove(cache->save_source);
        cache->save_source = 0;
    }

    gvr_preview_cache_flush(cache);
    gvr_preview_cache_clear_atlas(cache);
    gvr_preview_cache_clear_paths(cache);
    g_free(cache->slot_to_tile);
    g_free(cache->tile_to_slot);
    g_free(cache->directory);
    g_free(cache);
}

gboolean gvr_preview_cache_set_state(GvrPreviewCache *cache,
                                     const char *editlist_hash,
                                     const char *samplelist_hash)
{
    char *state_hash;

    if(!cache || !editlist_hash || !samplelist_hash ||
       !*editlist_hash || !*samplelist_hash)
        return FALSE;

    state_hash = gvr_preview_cache_combined_hash(cache,
                                                editlist_hash,
                                                samplelist_hash);
    if(cache->state_hash && strcmp(cache->state_hash, state_hash) == 0) {
        g_free(state_hash);
        return FALSE;
    }

    if(cache->save_source) {
        g_source_remove(cache->save_source);
        cache->save_source = 0;
    }
    gvr_preview_cache_flush(cache);
    gvr_preview_cache_clear_atlas(cache);
    gvr_preview_cache_clear_paths(cache);

    cache->state_hash = state_hash;
    cache->png_path = g_strdup_printf("%s/%s.png",
                                      cache->directory,
                                      cache->state_hash);
    cache->manifest_path = g_strdup_printf("%s/%s.ini",
                                           cache->directory,
                                           cache->state_hash);
    cache->dirty = FALSE;
    gvr_preview_cache_load(cache);
    return TRUE;
}

GdkPixbuf *gvr_preview_cache_get(GvrPreviewCache *cache, int slot_index)
{
    int tile;
    int column;
    int row;

    if(!cache || !cache->atlas || slot_index < 0 ||
       slot_index >= cache->total_slots)
        return NULL;

    tile = cache->slot_to_tile[slot_index];
    if(tile < 0 || tile >= cache->tile_count)
        return NULL;

    column = tile % cache->columns;
    row = tile / cache->columns;
    return gdk_pixbuf_new_subpixbuf(cache->atlas,
                                    column * cache->tile_width,
                                    row * cache->tile_height,
                                    cache->tile_width,
                                    cache->tile_height);
}

void gvr_preview_cache_put(GvrPreviewCache *cache,
                           int slot_index,
                           GdkPixbuf *pixbuf)
{
    GdkPixbuf *source = pixbuf;
    GdkPixbuf *scaled = NULL;
    int tile;
    int column;
    int row;

    if(!cache || !pixbuf || slot_index < 0 || slot_index >= cache->total_slots ||
       !cache->state_hash)
        return;

    tile = cache->slot_to_tile[slot_index];
    if(tile < 0) {
        tile = cache->tile_count;
        if(tile >= cache->total_slots ||
           !gvr_preview_cache_ensure_atlas(cache, tile + 1))
            return;
        cache->slot_to_tile[slot_index] = tile;
        cache->tile_to_slot[tile] = slot_index;
        cache->tile_count++;
    }
    else if(!gvr_preview_cache_ensure_atlas(cache, cache->tile_count)) {
        return;
    }

    if(gdk_pixbuf_get_width(pixbuf) != cache->tile_width ||
       gdk_pixbuf_get_height(pixbuf) != cache->tile_height) {
        scaled = gdk_pixbuf_scale_simple(pixbuf,
                                         cache->tile_width,
                                         cache->tile_height,
                                         GDK_INTERP_BILINEAR);
        if(!scaled)
            return;
        source = scaled;
    }

    column = tile % cache->columns;
    row = tile / cache->columns;
    gdk_pixbuf_copy_area(source,
                         0,
                         0,
                         cache->tile_width,
                         cache->tile_height,
                         cache->atlas,
                         column * cache->tile_width,
                         row * cache->tile_height);
    cache->dirty = TRUE;
    gvr_preview_cache_schedule_save(cache);

    if(scaled)
        g_object_unref(scaled);
}

void gvr_preview_cache_flush(GvrPreviewCache *cache)
{
    GKeyFile *key_file;
    GError *error = NULL;
    gchar *manifest_data;
    gsize manifest_len;
    gchar *png_tmp;
    gchar *manifest_tmp;

    if(!cache || !cache->dirty || !cache->atlas || cache->tile_count <= 0 ||
       !cache->state_hash || !cache->png_path || !cache->manifest_path)
        return;

    key_file = g_key_file_new();
    g_key_file_set_integer(key_file, "cache", "version", GVR_PREVIEW_CACHE_VERSION);
    g_key_file_set_integer(key_file, "cache", "tile-width", cache->tile_width);
    g_key_file_set_integer(key_file, "cache", "tile-height", cache->tile_height);
    g_key_file_set_integer(key_file, "cache", "total-slots", cache->total_slots);
    g_key_file_set_string(key_file, "cache", "state-hash", cache->state_hash);
    g_key_file_set_integer_list(key_file,
                                "cache",
                                "slots",
                                cache->tile_to_slot,
                                cache->tile_count);
    manifest_data = g_key_file_to_data(key_file, &manifest_len, NULL);

    png_tmp = g_strdup_printf("%s.tmp-%08x", cache->png_path, g_random_int());
    manifest_tmp = g_strdup_printf("%s.tmp-%08x", cache->manifest_path, g_random_int());

    if(!gdk_pixbuf_save(cache->atlas,
                        png_tmp,
                        "png",
                        &error,
                        "compression",
                        "6",
                        NULL)) {
        g_clear_error(&error);
        goto cleanup;
    }

    if(!g_file_set_contents(manifest_tmp, manifest_data, manifest_len, &error)) {
        g_clear_error(&error);
        g_unlink(png_tmp);
        goto cleanup;
    }

    if(g_rename(png_tmp, cache->png_path) != 0) {
        g_unlink(png_tmp);
        g_unlink(manifest_tmp);
        goto cleanup;
    }

    if(g_rename(manifest_tmp, cache->manifest_path) != 0) {
        g_unlink(manifest_tmp);
        goto cleanup;
    }

    cache->dirty = FALSE;

cleanup:
    g_free(png_tmp);
    g_free(manifest_tmp);
    g_free(manifest_data);
    g_key_file_free(key_file);
}

const char *gvr_preview_cache_get_directory(GvrPreviewCache *cache)
{
    return cache ? cache->directory : NULL;
}

const char *gvr_preview_cache_get_state_hash(GvrPreviewCache *cache)
{
    return cache ? cache->state_hash : NULL;
}
