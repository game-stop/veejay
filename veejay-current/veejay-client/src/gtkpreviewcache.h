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
#ifndef GTK_PREVIEW_CACHE_H
#define GTK_PREVIEW_CACHE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _GvrPreviewCache GvrPreviewCache;

GvrPreviewCache *gvr_preview_cache_new(const char *host,
                                       int port,
                                       int tile_width,
                                       int tile_height,
                                       int total_slots);
void gvr_preview_cache_free(GvrPreviewCache *cache);

gboolean gvr_preview_cache_set_state(GvrPreviewCache *cache,
                                     const char *editlist_hash,
                                     const char *samplelist_hash);
GdkPixbuf *gvr_preview_cache_get(GvrPreviewCache *cache, int slot_index);
void gvr_preview_cache_put(GvrPreviewCache *cache,
                           int slot_index,
                           GdkPixbuf *pixbuf);
void gvr_preview_cache_flush(GvrPreviewCache *cache);
const char *gvr_preview_cache_get_directory(GvrPreviewCache *cache);
const char *gvr_preview_cache_get_state_hash(GvrPreviewCache *cache);

G_END_DECLS

#endif
