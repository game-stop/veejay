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
#include <pango/pangocairo.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <veejaycore/vims.h>
#include "gtkvimspatternview.h"

#define GVR_PATTERN_ROW_HEIGHT_MIN 21
#define GVR_PATTERN_HEADER_HEIGHT 0
#define GVR_PATTERN_ALL_COLUMNS_MASK ((1u << GVR_VIMS_PATTERN_COLUMNS) - 1u)
#define GVR_PATTERN_FRAME_WIDTH 96
#define GVR_PATTERN_DEFAULT_FRAMES 256
#define GVR_PATTERN_MAX_FRAMES 10000000
#define GVR_PATTERN_LABEL_TEXT_MAX 14

typedef enum {
    GVR_PATTERN_LEARN_COLUMN_LOCK = 0,
    GVR_PATTERN_LEARN_NEXT_FREE,
    GVR_PATTERN_LEARN_REPLACE_MATCH,
    GVR_PATTERN_LEARN_OVERWRITE,
    GVR_PATTERN_LEARN_LAYER,
    GVR_PATTERN_LEARN_POLICY_COUNT
} GvrPatternLearnPolicy;

typedef enum {
    GVR_PATTERN_PASTE_REPLACE = 0,
    GVR_PATTERN_PASTE_MERGE,
    GVR_PATTERN_PASTE_INSERT,
    GVR_PATTERN_PASTE_MODE_COUNT
} GvrPatternPasteMode;
#define GVR_PATTERN_UNDO_LIMIT 64
#define GVR_PATTERN_BLOCK_MAX_CELLS 65536

#define GVR_VIMS_MESSAGE_DND_TARGET \
    "application/x-gveejay-vims-message"

static GtkTargetEntry gvr_vims_message_drag_targets[] = {
    { (gchar *)GVR_VIMS_MESSAGE_DND_TARGET,
      GTK_TARGET_SAME_APP,
      0 },
    { (gchar *)"text/plain", 0, 1 }
};

typedef struct {
    int vims_id;
    char *message;
    char label[32];
} GvrVimsPatternEvent;

typedef struct {
    int frame;
    GvrVimsPatternEvent events[GVR_VIMS_PATTERN_COLUMNS];
} GvrVimsPatternRow;

typedef struct {
    int sample_id;
    int sample_type;
    GTree *rows;
    guint flags;
    gboolean loop_enabled;
    int loop_start;
    int loop_end;
} GvrVimsPatternCell;

typedef struct {
    int frame;
    int source_frame;
    gboolean paused;
    gboolean seeked_while_paused;
    gboolean transport_valid;
    guint transport_epoch;
    int direction;
    int loop_type;
    int loops_remaining;
} GvrVimsPatternPlaybackState;

typedef struct {
    int rows;
    int columns;
    int frame_step;
    GvrVimsPatternEvent *events;
} GvrPatternBlockClipboard;

typedef struct {
    int selected_row;
    int selected_column;
    gboolean selection_active;
    int selection_anchor_row;
    int selection_anchor_column;
    GvrVimsPatternCell cell;
} GvrPatternSnapshot;

struct _GvrVimsPatternView {
    GtkBox parent_instance;

    GtkWidget *target_badge;
    GtkWidget *target_label;
    GtkWidget *step_spin;
    GtkWidget *learn_toggle;
    GtkWidget *learn_policy_combo;
    GtkWidget *follow_toggle;
    GtkWidget *loop_toggle;
    GtkWidget *loop_set_button;
    GtkWidget *loop_clear_button;
    GtkWidget *command_entry;
    GtkWidget *undo_button;
    GtkWidget *redo_button;
    GtkWidget *paste_mode_combo;
    GtkWidget *track_toggle[GVR_VIMS_PATTERN_COLUMNS];
    GtkWidget *area;
    GtkWidget *scrollbar;
    GtkAdjustment *vadjustment;
    guint enabled_columns_mask;
    int row_step;
    int learn_policy;
    int paste_mode;
    gboolean inline_editing;
    gboolean syncing_track_toggles;
    gboolean syncing_loop_toggle;

    GvrVimsPatternCell cells[GVR_VIMS_PATTERN_BANKS][GVR_VIMS_PATTERN_SLOTS];
    GvrVimsPatternCell bank_cells[GVR_VIMS_PATTERN_BANKS];
    GHashTable *sample_cells;
    GHashTable *stream_cells;
    GHashTable *clipboard;
    GHashTable *playback_states;
    GQueue *undo_stack;
    GQueue *redo_stack;
    GvrPatternBlockClipboard block_clipboard;

    int selected_bank;
    int selected_slot;
    int selected_sample_id;
    int selected_sample_type;
    int frame_count;
    gboolean frame_count_known;
    int selected_row;
    int selected_column;
    gboolean selection_active;
    int selection_anchor_row;
    int selection_anchor_column;
    gboolean drag_selecting;
    int last_fired_bank;
    int last_fired_slot;
    int last_fired_frame;
    guint last_fired_mask;
    int live_bank;
    int live_slot;
    int live_frame;
    gboolean live_active;
    gboolean transport_valid;
    guint transport_epoch;
    int transport_direction;
    int transport_loop_type;
    int transport_loops_remaining;

    GvrVimsPatternDescriptionLookup description_lookup;
    gpointer description_lookup_data;
    GDestroyNotify description_lookup_destroy;
};

struct _GvrVimsPatternViewClass {
    GtkBoxClass parent_class;
};

enum {
    SIGNAL_VIMS_FIRE,
    SIGNAL_PATTERN_CHANGED,
    SIGNAL_TRANSPORT_REQUEST,
    SIGNAL_COMMAND_REQUESTED,
    SIGNAL_LAST
};

static guint gvr_vims_pattern_view_signals[SIGNAL_LAST];

G_DEFINE_TYPE(GvrVimsPatternView, gvr_vims_pattern_view, GTK_TYPE_BOX)


static double gvr_pattern_font_points(GtkWidget *widget)
{
    PangoContext *context;
    const PangoFontDescription *font;
    int size;
    double points;

    if(!widget)
        return 10.0;

    context = gtk_widget_get_pango_context(widget);
    font = context ? pango_context_get_font_description(context) : NULL;
    size = font ? pango_font_description_get_size(font) : 0;
    if(size <= 0)
        return 10.0;

    points = (double)size / PANGO_SCALE;
    if(pango_font_description_get_size_is_absolute(font))
        points *= 72.0 / 96.0;
    return CLAMP(points, 6.0, 32.0);
}

static double gvr_pattern_font_px(GvrVimsPatternView *view,
                                  double scale)
{
    return MAX(7.0,
               gvr_pattern_font_points(view ? view->area : NULL) *
               (96.0 / 72.0) * scale);
}

static int gvr_pattern_row_height(GvrVimsPatternView *view)
{
    return MAX(GVR_PATTERN_ROW_HEIGHT_MIN,
               (int)ceil(gvr_pattern_font_px(view, 1.0) + 7.0));
}

static PangoFontDescription *gvr_pattern_font_description(GtkWidget *widget,
                                                           double scale,
                                                           PangoWeight weight)
{
    PangoContext *context = widget ? gtk_widget_get_pango_context(widget) : NULL;
    const PangoFontDescription *base = context ? pango_context_get_font_description(context) : NULL;
    PangoFontDescription *font = base ?
        pango_font_description_copy(base) :
        pango_font_description_from_string("Monospace 10");

    pango_font_description_set_family(font, "Monospace");
    pango_font_description_set_size(
        font,
        (int)lrint(gvr_pattern_font_points(widget) * scale * PANGO_SCALE));
    pango_font_description_set_weight(font, weight);
    return font;
}

static void gvr_pattern_update_target_label(GvrVimsPatternView *view);
static void gvr_pattern_update_history_buttons(GvrVimsPatternView *view);
static void gvr_pattern_sync_loop_controls(GvrVimsPatternView *view);
static void gvr_pattern_sync_track_toggles(GvrVimsPatternView *view);
static void gvr_pattern_loop_set(GtkButton *button, gpointer user_data);
static void gvr_pattern_loop_clear(GtkButton *button, gpointer user_data);
static int gvr_pattern_display_row_count(GvrVimsPatternView *view);
static void gvr_pattern_update_adjustment(GvrVimsPatternView *view);
static void gvr_pattern_scroll_to_cursor(GvrVimsPatternView *view);
static void gvr_pattern_move_row(GvrVimsPatternView *view,
                                 int row,
                                 int column,
                                 gboolean seek);
static void gvr_pattern_selection_clear(GvrVimsPatternView *view);
static gboolean gvr_pattern_insert_gap_no_undo(
        GvrVimsPatternView *view,
        int start_frame,
        int amount);
static void gvr_pattern_drag_data_received(
        GtkWidget *widget,
        GdkDragContext *context,
        gint x,
        gint y,
        GtkSelectionData *selection,
        guint info,
        guint time,
        gpointer user_data);

static int gvr_pattern_clampi(int value, int lo, int hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

static gint gvr_pattern_int_compare(gconstpointer a, gconstpointer b, gpointer data)
{
    (void)data;
    const int ia = GPOINTER_TO_INT(a);
    const int ib = GPOINTER_TO_INT(b);
    return (ia > ib) - (ia < ib);
}

static void gvr_pattern_event_clear(GvrVimsPatternEvent *event)
{
    if(!event)
        return;

    g_clear_pointer(&event->message, g_free);
    event->vims_id = 0;
    event->label[0] = '\0';
}

static void gvr_pattern_row_free(gpointer data)
{
    GvrVimsPatternRow *row = data;

    if(!row)
        return;

    for(int column = 0; column < GVR_VIMS_PATTERN_COLUMNS; column++)
        gvr_pattern_event_clear(&row->events[column]);

    g_free(row);
}

static void gvr_pattern_cell_ensure(GvrVimsPatternCell *cell)
{
    if(!cell->rows)
        cell->rows = g_tree_new_full((GCompareDataFunc)gvr_pattern_int_compare,
                                     NULL,
                                     NULL,
                                     gvr_pattern_row_free);
}

static void gvr_pattern_cell_clear_data(GvrVimsPatternCell *cell)
{
    if(!cell)
        return;

    if(cell->rows) {
        g_tree_destroy(cell->rows);
        cell->rows = NULL;
    }

    gvr_pattern_cell_ensure(cell);
    cell->flags = 0;
    cell->loop_enabled = FALSE;
    cell->loop_start = -1;
    cell->loop_end = -1;
}

static void gvr_pattern_dynamic_cell_free(gpointer data)
{
    GvrVimsPatternCell *cell = data;

    if(!cell)
        return;

    if(cell->rows)
        g_tree_destroy(cell->rows);

    g_free(cell);
}

static guint64 gvr_pattern_playback_key(int bank, int slot)
{
    return (((guint64)(guint32)bank) << 32) | (guint32)slot;
}

static GvrVimsPatternPlaybackState *gvr_pattern_playback_state(
        GvrVimsPatternView *view,
        int bank,
        int slot,
        gboolean create)
{
    guint64 key;
    gpointer value = NULL;

    if(!view || !view->playback_states)
        return NULL;

    key = gvr_pattern_playback_key(bank, slot);
    if(g_hash_table_lookup_extended(view->playback_states, &key, NULL, &value))
        return value;

    if(!create)
        return NULL;

    guint64 *stored_key = g_new(guint64, 1);
    GvrVimsPatternPlaybackState *state = g_new0(GvrVimsPatternPlaybackState, 1);
    *stored_key = key;
    state->frame = -1;
    state->source_frame = -1;
    g_hash_table_insert(view->playback_states, stored_key, state);
    return state;
}

static void gvr_pattern_playback_remove(GvrVimsPatternView *view,
                                        int bank,
                                        int slot)
{
    guint64 key;

    if(!view || !view->playback_states)
        return;

    key = gvr_pattern_playback_key(bank, slot);
    g_hash_table_remove(view->playback_states, &key);
}

static gboolean gvr_pattern_target_valid(int bank, int slot)
{
    if(bank == GVR_VIMS_PATTERN_SAMPLE_BANK ||
       bank == GVR_VIMS_PATTERN_STREAM_BANK)
        return slot > 0;

    if(bank == GVR_VIMS_PATTERN_SEQUENCE_BANK)
        return slot >= 0 && slot < GVR_VIMS_PATTERN_BANKS;

    return bank >= 0 && bank < GVR_VIMS_PATTERN_BANKS &&
           slot >= 0 && slot < GVR_VIMS_PATTERN_SLOTS;
}

static gboolean gvr_pattern_target_editable(GvrVimsPatternView *view)
{
    return view &&
           gvr_pattern_target_valid(view->selected_bank, view->selected_slot) &&
           (view->selected_bank == GVR_VIMS_PATTERN_SEQUENCE_BANK ||
            view->selected_sample_id > 0);
}

static GvrVimsPatternCell *gvr_pattern_cell(GvrVimsPatternView *view, int bank, int slot)
{
    GvrVimsPatternCell *cell;
    GHashTable *source_cells;

    if(!view || !gvr_pattern_target_valid(bank, slot))
        return NULL;

    if(bank >= 0)
        return &view->cells[bank][slot];

    if(bank == GVR_VIMS_PATTERN_SEQUENCE_BANK)
        return &view->bank_cells[slot];

    source_cells = bank == GVR_VIMS_PATTERN_SAMPLE_BANK ?
                   view->sample_cells : view->stream_cells;
    cell = g_hash_table_lookup(source_cells, GINT_TO_POINTER(slot));
    if(!cell) {
        cell = g_new0(GvrVimsPatternCell, 1);
        cell->sample_id = slot;
        cell->sample_type = bank == GVR_VIMS_PATTERN_SAMPLE_BANK ? 0 : -1;
        cell->loop_start = -1;
        cell->loop_end = -1;
        gvr_pattern_cell_ensure(cell);
        g_hash_table_insert(source_cells, GINT_TO_POINTER(slot), cell);
    }

    return cell;
}

static GvrVimsPatternCell *gvr_pattern_cell_lookup(GvrVimsPatternView *view,
                                                    int bank,
                                                    int slot)
{
    if(!view || !gvr_pattern_target_valid(bank, slot))
        return NULL;

    if(bank >= 0)
        return &view->cells[bank][slot];

    if(bank == GVR_VIMS_PATTERN_SEQUENCE_BANK)
        return &view->bank_cells[slot];

    return g_hash_table_lookup(bank == GVR_VIMS_PATTERN_SAMPLE_BANK ?
                               view->sample_cells : view->stream_cells,
                               GINT_TO_POINTER(slot));
}

static gboolean gvr_pattern_loop_valid(const GvrVimsPatternCell *cell)
{
    return cell &&
           cell->loop_enabled &&
           cell->loop_start >= 0 &&
           cell->loop_end >= cell->loop_start;
}

static void gvr_pattern_loop_clamp(GvrVimsPatternCell *cell,
                                   int frame_limit)
{
    if(!cell || !cell->loop_enabled)
        return;

    if(frame_limit <= 0 ||
       cell->loop_start < 0 ||
       cell->loop_end < cell->loop_start)
    {
        cell->loop_enabled = FALSE;
        cell->loop_start = -1;
        cell->loop_end = -1;
        return;
    }

    cell->loop_start = MIN(cell->loop_start, frame_limit - 1);
    cell->loop_end = MIN(cell->loop_end, frame_limit - 1);

    if(cell->loop_end < cell->loop_start) {
        cell->loop_enabled = FALSE;
        cell->loop_start = -1;
        cell->loop_end = -1;
    }
}

static const char *gvr_pattern_learn_policy_name(int policy)
{
    switch(policy) {
        case GVR_PATTERN_LEARN_COLUMN_LOCK:
            return "Column lock";
        case GVR_PATTERN_LEARN_NEXT_FREE:
            return "Next free track";
        case GVR_PATTERN_LEARN_REPLACE_MATCH:
            return "Replace matching VIMS";
        case GVR_PATTERN_LEARN_OVERWRITE:
            return "Overwrite selected";
        case GVR_PATTERN_LEARN_LAYER:
            return "Layer across tracks";
        default:
            return "Replace matching VIMS";
    }
}

static GvrVimsPatternRow *gvr_pattern_row_get(GvrVimsPatternCell *cell,
                                               int frame,
                                               gboolean create)
{
    GvrVimsPatternRow *row;
    gpointer key;

    if(!cell || frame < 0)
        return NULL;

    gvr_pattern_cell_ensure(cell);
    key = GINT_TO_POINTER(frame + 1);
    row = g_tree_lookup(cell->rows, key);

    if(!row && create) {
        row = g_new0(GvrVimsPatternRow, 1);
        row->frame = frame;
        g_tree_insert(cell->rows, key, row);
    }

    return row;
}

static gboolean gvr_pattern_row_empty(const GvrVimsPatternRow *row)
{
    if(!row)
        return TRUE;

    for(int column = 0; column < GVR_VIMS_PATTERN_COLUMNS; column++)
        if(row->events[column].message)
            return FALSE;

    return TRUE;
}

typedef struct {
    guint flags;
    guint event_count;
    guint row_count;
} GvrPatternSummaryScan;

static gboolean gvr_pattern_scan_summary(gpointer key, gpointer value, gpointer data)
{
    (void)key;
    GvrVimsPatternRow *row = value;
    GvrPatternSummaryScan *scan = data;
    gboolean occupied = FALSE;

    for(int column = 0; column < GVR_VIMS_PATTERN_COLUMNS; column++) {
        GvrVimsPatternEvent *event = &row->events[column];

        if(!event->message)
            continue;

        occupied = TRUE;
        scan->event_count++;
        if(event->vims_id == VIMS_SAMPLE_HOLD_FRAME)
            scan->flags |= GVR_VIMS_PATTERN_HAS_HOLD;
        else
            scan->flags |= GVR_VIMS_PATTERN_HAS_VIMS;
    }

    if(occupied)
        scan->row_count++;

    return FALSE;
}

static gboolean gvr_pattern_cell_summary(GvrVimsPatternCell *cell,
                                          GvrVimsPatternSummary *summary)
{
    GvrPatternSummaryScan scan = { 0 };

    if(summary)
        memset(summary, 0, sizeof(*summary));

    if(cell && cell->rows)
        g_tree_foreach(cell->rows, gvr_pattern_scan_summary, &scan);

    if(cell)
        cell->flags = scan.flags;

    if(summary) {
        summary->flags = scan.flags;
        summary->event_count = scan.event_count;
        summary->row_count = scan.row_count;
    }

    return scan.event_count > 0;
}

static guint gvr_pattern_cell_recount_flags(GvrVimsPatternCell *cell)
{
    GvrVimsPatternSummary summary;
    gvr_pattern_cell_summary(cell, &summary);
    return summary.flags;
}

static void gvr_pattern_clear_fired_highlight(GvrVimsPatternView *view)
{
    if(!view)
        return;

    view->last_fired_bank = -1;
    view->last_fired_slot = -1;
    view->last_fired_frame = -1;
    view->last_fired_mask = 0;
}

static void gvr_pattern_clear_fired_highlight_for(
        GvrVimsPatternView *view,
        int bank,
        int slot)
{
    if(view &&
       view->last_fired_bank == bank &&
       view->last_fired_slot == slot)
        gvr_pattern_clear_fired_highlight(view);
}

static void gvr_pattern_emit_changed(GvrVimsPatternView *view, int bank, int slot)
{
    GvrVimsPatternCell *cell = gvr_pattern_cell(view, bank, slot);
    guint flags = gvr_pattern_cell_recount_flags(cell);

    gvr_pattern_clear_fired_highlight_for(view, bank, slot);

    g_signal_emit(view,
                  gvr_vims_pattern_view_signals[SIGNAL_PATTERN_CHANGED],
                  0,
                  bank,
                  slot,
                  flags);

    if(bank == view->selected_bank && slot == view->selected_slot) {
        gvr_pattern_update_target_label(view);
        gtk_widget_queue_draw(view->area);
    }
}

static void gvr_pattern_event_copy(GvrVimsPatternEvent *dst,
                                   const GvrVimsPatternEvent *src)
{
    gvr_pattern_event_clear(dst);

    if(!src || !src->message)
        return;

    dst->vims_id = src->vims_id;
    dst->message = g_strdup(src->message);
    g_strlcpy(dst->label, src->label, sizeof(dst->label));
}

static gboolean gvr_pattern_clone_row(gpointer key, gpointer value, gpointer data)
{
    GvrVimsPatternCell *dst = data;
    GvrVimsPatternRow *src_row = value;
    GvrVimsPatternRow *dst_row = gvr_pattern_row_get(dst, GPOINTER_TO_INT(key) - 1, TRUE);

    for(int column = 0; column < GVR_VIMS_PATTERN_COLUMNS; column++)
        gvr_pattern_event_copy(&dst_row->events[column], &src_row->events[column]);

    return FALSE;
}

static void gvr_pattern_cell_clone(GvrVimsPatternCell *dst,
                                   const GvrVimsPatternCell *src)
{
    if(!dst)
        return;

    gvr_pattern_cell_clear_data(dst);
    dst->sample_id = src ? src->sample_id : -1;
    dst->sample_type = src ? src->sample_type : -1;
    dst->loop_enabled = src ? src->loop_enabled : FALSE;
    dst->loop_start = src ? src->loop_start : -1;
    dst->loop_end = src ? src->loop_end : -1;

    if(src && src->rows)
        g_tree_foreach(src->rows, gvr_pattern_clone_row, dst);

    gvr_pattern_cell_recount_flags(dst);
}


static void gvr_pattern_snapshot_free(gpointer data)
{
    GvrPatternSnapshot *snapshot = data;

    if(!snapshot)
        return;

    if(snapshot->cell.rows)
        g_tree_destroy(snapshot->cell.rows);

    g_free(snapshot);
}

static GvrPatternSnapshot *gvr_pattern_snapshot_new(GvrVimsPatternView *view)
{
    GvrPatternSnapshot *snapshot;
    GvrVimsPatternCell *cell;

    if(!view)
        return NULL;

    cell = gvr_pattern_cell(view, view->selected_bank, view->selected_slot);
    if(!cell)
        return NULL;

    snapshot = g_new0(GvrPatternSnapshot, 1);
    snapshot->selected_row = view->selected_row;
    snapshot->selected_column = view->selected_column;
    snapshot->selection_active = view->selection_active;
    snapshot->selection_anchor_row = view->selection_anchor_row;
    snapshot->selection_anchor_column = view->selection_anchor_column;
    gvr_pattern_cell_ensure(&snapshot->cell);
    gvr_pattern_cell_clone(&snapshot->cell, cell);
    return snapshot;
}

static void gvr_pattern_history_queue_clear(GQueue *queue)
{
    while(queue && !g_queue_is_empty(queue))
        gvr_pattern_snapshot_free(g_queue_pop_head(queue));
}

static void gvr_pattern_update_history_buttons(GvrVimsPatternView *view)
{
    if(!view)
        return;

    if(view->undo_button)
        gtk_widget_set_sensitive(view->undo_button,
                                 view->undo_stack &&
                                 !g_queue_is_empty(view->undo_stack));
    if(view->redo_button)
        gtk_widget_set_sensitive(view->redo_button,
                                 view->redo_stack &&
                                 !g_queue_is_empty(view->redo_stack));
}

static void gvr_pattern_history_reset(GvrVimsPatternView *view)
{
    if(!view)
        return;

    gvr_pattern_history_queue_clear(view->undo_stack);
    gvr_pattern_history_queue_clear(view->redo_stack);
    gvr_pattern_update_history_buttons(view);
}


static void gvr_pattern_history_invalidate_target(GvrVimsPatternView *view,
                                                  int bank,
                                                  int slot)
{
    if(view &&
       view->selected_bank == bank &&
       view->selected_slot == slot)
    {
        gvr_pattern_history_reset(view);
        gvr_pattern_selection_clear(view);
    }
}

static void gvr_pattern_history_push(GQueue *queue, GvrPatternSnapshot *snapshot)
{
    if(!queue || !snapshot)
        return;

    g_queue_push_head(queue, snapshot);
    while(g_queue_get_length(queue) > GVR_PATTERN_UNDO_LIMIT)
        gvr_pattern_snapshot_free(g_queue_pop_tail(queue));
}

static gboolean gvr_pattern_push_undo(GvrVimsPatternView *view)
{
    GvrPatternSnapshot *snapshot = gvr_pattern_snapshot_new(view);

    if(!snapshot)
        return FALSE;

    gvr_pattern_history_push(view->undo_stack, snapshot);
    gvr_pattern_history_queue_clear(view->redo_stack);
    gvr_pattern_update_history_buttons(view);
    return TRUE;
}

static gboolean gvr_pattern_restore_snapshot(GvrVimsPatternView *view,
                                             GvrPatternSnapshot *snapshot)
{
    GvrVimsPatternCell *cell;
    int last_row;

    if(!view || !snapshot)
        return FALSE;

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);
    if(!cell)
        return FALSE;

    gvr_pattern_cell_clone(cell, &snapshot->cell);
    last_row =
        MAX(0, gvr_pattern_display_row_count(view) - 1);
    view->selected_row =
        gvr_pattern_clampi(snapshot->selected_row,
                           0,
                           last_row);
    view->selected_column =
        gvr_pattern_clampi(snapshot->selected_column,
                           0,
                           GVR_VIMS_PATTERN_COLUMNS - 1);
    view->selection_active =
        snapshot->selection_active;
    view->selection_anchor_row =
        gvr_pattern_clampi(snapshot->selection_anchor_row,
                           0,
                           last_row);
    view->selection_anchor_column =
        gvr_pattern_clampi(
            snapshot->selection_anchor_column,
            0,
            GVR_VIMS_PATTERN_COLUMNS - 1);
    gvr_pattern_emit_changed(view,
                             view->selected_bank,
                             view->selected_slot);
    gvr_pattern_playback_remove(view,
                                view->selected_bank,
                                view->selected_slot);
    gvr_pattern_sync_loop_controls(view);
    return TRUE;
}

static void gvr_pattern_undo(GvrVimsPatternView *view)
{
    GvrPatternSnapshot *snapshot;
    GvrPatternSnapshot *current;

    if(!view || !view->undo_stack || g_queue_is_empty(view->undo_stack))
        return;

    current = gvr_pattern_snapshot_new(view);
    snapshot = g_queue_pop_head(view->undo_stack);
    if(current)
        gvr_pattern_history_push(view->redo_stack, current);

    if(gvr_pattern_restore_snapshot(view, snapshot)) {
        gvr_pattern_update_adjustment(view);
        gvr_pattern_scroll_to_cursor(view);
    }

    gvr_pattern_snapshot_free(snapshot);
    gvr_pattern_update_history_buttons(view);
}

static void gvr_pattern_redo(GvrVimsPatternView *view)
{
    GvrPatternSnapshot *snapshot;
    GvrPatternSnapshot *current;

    if(!view || !view->redo_stack || g_queue_is_empty(view->redo_stack))
        return;

    current = gvr_pattern_snapshot_new(view);
    snapshot = g_queue_pop_head(view->redo_stack);
    if(current)
        gvr_pattern_history_push(view->undo_stack, current);

    if(gvr_pattern_restore_snapshot(view, snapshot)) {
        gvr_pattern_update_adjustment(view);
        gvr_pattern_scroll_to_cursor(view);
    }

    gvr_pattern_snapshot_free(snapshot);
    gvr_pattern_update_history_buttons(view);
}

static void gvr_pattern_clipboard_cell_free(gpointer data)
{
    GvrVimsPatternCell *cell = data;

    if(!cell)
        return;

    if(cell->rows)
        g_tree_destroy(cell->rows);

    g_free(cell);
}

static int gvr_pattern_parse_id(const char *message)
{
    char *end = NULL;
    long id;

    if(!message)
        return -1;

    while(g_ascii_isspace(*message))
        message++;

    id = strtol(message, &end, 10);
    if(end == message || *end != ':' || id < 0 || id > 9999)
        return -1;

    return (int)id;
}

static int gvr_pattern_integer_arguments(const char *message,
                                         int *values,
                                         int max_values)
{
    const char *p;
    int count = 0;

    if(!message || !values || max_values <= 0)
        return 0;

    p = strchr(message, ':');
    if(!p)
        return 0;

    p++;
    while(*p && *p != ';' && count < max_values) {
        const char *start;
        const char *end;
        char token[32];
        char *parse_end = NULL;
        long value;
        gsize length;

        while(g_ascii_isspace(*p))
            p++;
        if(!*p || *p == ';')
            break;

        start = p;
        while(*p && *p != ';' && !g_ascii_isspace(*p))
            p++;
        end = p;
        length = (gsize)(end - start);

        if(length == 0 || length >= sizeof(token))
            continue;

        memcpy(token, start, length);
        token[length] = '\0';
        value = strtol(token, &parse_end, 10);
        if(parse_end != token && *parse_end == '\0')
            values[count++] = (int)value;
    }

    return count;
}

static int gvr_pattern_integer_argument(const int *values,
                                        int count,
                                        int index,
                                        int fallback)
{
    return values && index >= 0 && index < count ?
           values[index] : fallback;
}

static gboolean gvr_pattern_last_argument_text(const char *message,
                                               char *text,
                                               gsize text_size)
{
    const char *colon;
    const char *start;
    const char *end;
    gsize length;

    if(!message || !text || text_size == 0)
        return FALSE;

    text[0] = '\0';
    colon = strchr(message, ':');
    if(!colon)
        return FALSE;

    end = strchr(colon + 1, ';');
    if(!end)
        end = message + strlen(message);

    while(end > colon + 1 && g_ascii_isspace(end[-1]))
        end--;

    start = end;
    while(start > colon + 1 && !g_ascii_isspace(start[-1]))
        start--;

    length = (gsize)(end - start);
    if(length == 0)
        return FALSE;

    length = MIN(length, text_size - 1);
    memcpy(text, start, length);
    text[length] = '\0';
    return TRUE;
}

static void gvr_pattern_upper_token(const char *source,
                                    char *target,
                                    gsize target_size)
{
    gsize index = 0;

    if(!target || target_size == 0)
        return;

    if(source) {
        while(source[index] && index + 1 < target_size) {
            target[index] = g_ascii_toupper(source[index]);
            index++;
        }
    }

    target[index] = '\0';
}

static const char *gvr_pattern_state_name(int value)
{
    if(value < 0)
        return "TOGGLE";

    return value ? "ON" : "OFF";
}

static guint32 gvr_pattern_description_word_hash(const char *word)
{
    guint32 hash = 2166136261u;

    while(word && *word) {
        hash ^= (guint8)g_ascii_tolower(*word++);
        hash *= 16777619u;
    }

    return hash;
}

static gboolean gvr_pattern_description_skip_hash(guint32 hash)
{
    switch(hash) {
        case 0xe40c292cu:
        case 0x4124f2e6u:
        case 0xb40eb21cu:
        case 0x69343c68u:
        case 0x42454824u:
        case 0x95cd8075u:
        case 0xacf38390u:
        case 0x0c4afe69u:
        case 0x69ce1407u:
        case 0x5791c4f4u:
        case 0x5e25208du:
        case 0x61342fd0u:
        case 0x41387a9eu:
        case 0x2f91a723u:
        case 0x542bcc94u:
        case 0x7f778519u:
        case 0x0dc628ceu:
        case 0x0dd08785u:
        case 0xd965bbdau:
        case 0xda2bd281u:
        case 0x147aa128u:
        case 0x28999611u:
        case 0x159ac2b7u:
        case 0x13254bc4u:
        case 0x5d342984u:
        case 0x0f29c2a6u:
        case 0x37351c20u:
        case 0x540ca757u:
        case 0xc6270703u:
        case 0x729d01bdu:
        case 0xf1b0d04bu:
        case 0xa4ee076fu:
        case 0xfd8fc87au:
        case 0xcbfd3c67u:
        case 0xfd0c5087u:
        case 0xfc0c4ef4u:
            return TRUE;
        default:
            return FALSE;
    }
}

static const char *gvr_pattern_description_abbreviation(guint32 hash)
{
    switch(hash) {
        case 0x6e6e8d54u:
        case 0xff083465u:
        case 0xcac3d793u:
        case 0x6f5882f3u:
            return "FX";
        case 0xf510291eu:
        case 0x35713697u:
            return "PRESET";
        case 0x60b4a3d6u:
        case 0xe15d9cbfu:
            return "CHAIN";
        case 0x55b10110u:
        case 0x48a52ed9u:
            return "PARAM";
        case 0x933b5bdeu:
        case 0x73715157u:
            return "DEFAULT";
        case 0x96e382a7u:
        case 0x5c26f3bcu:
            return "SMP";
        case 0x5f6f6d65u:
        case 0x5268b9a2u:
            return "STRM";
        case 0xcf8a43ecu:
            return "PLAY";
        case 0x9b111ae4u:
            return "FPS";
        case 0xd20a71a6u:
        case 0x7b71324fu:
            return "FRAME";
        case 0xabf8d4ddu:
        case 0x66b6cdeau:
            return "SEC";
        case 0x934f4e0au:
            return "POS";
        case 0x97537db5u:
            return "PCT";
        case 0xa62782e2u:
        case 0xb19001feu:
            return "TRANS";
        case 0x21c252a4u:
        case 0xfbe86875u:
            return "CH";
        case 0x1bcf29d8u:
        case 0xb7c0bbbcu:
            return "SRC";
        case 0x2fa0fd0du:
            return "DUR";
        case 0x83d03615u:
            return "LEN";
        case 0x4939f3f8u:
            return "THRESH";
        case 0x85d8a7e8u:
            return "COOL";
        case 0x7c2d100cu:
            return "SENS";
        case 0x03043d51u:
            return "CORR";
        case 0x96e70aa5u:
            return "XFADE";
        case 0x34d0622au:
            return "BRIGHT";
        case 0xf5a2e289u:
            return "SAT";
        case 0xe4497980u:
            return "PROJ";
        case 0xe4abbac3u:
            return "VIEW";
        case 0x115bfcb9u:
        case 0x1dcef1feu:
            return "SUB";
        case 0x2ee0698fu:
            return "VOL";
        case 0xc6c2dd66u:
            return "OPACITY";
        case 0x3812e73eu:
        case 0xff37c82du:
            return "INC";
        case 0x19cb36b2u:
        case 0xd6819a41u:
            return "DEC";
        case 0xaf8bb8ceu:
        case 0x02f3b39eu:
            return "ON";
        case 0xcded8c63u:
        case 0x33f36f05u:
            return "OFF";
        case 0xa92b8b72u:
            return "ORIG";
        case 0x331d748au:
            return "EXT";
        case 0x10bb9798u:
            return "NORM";
        case 0x425ed3cau:
            return "VAL";
        case 0xf4bf083fu:
        case 0xfb34269au:
            return "MIX";
        case 0x70aced6fu:
            return "DET";
        case 0xff7c4253u:
            return "RENDER";
        case 0x79a94f04u:
            return "OUT";
        case 0x593058ccu:
        case 0x96908b6cu:
        case 0xd467947bu:
            return "REC";
        case 0x3d32bd4au:
            return "RAND";
        case 0x685d8527u:
        case 0x37b8f592u:
            return "CAL";
        case 0x6eec35c2u:
        case 0x4ed885a3u:
            return "GEN";
        case 0x13ba397au:
        case 0x1727c92bu:
            return "PLUG";
        case 0x815f1c7bu:
        case 0xb0b92098u:
        case 0x33a50cfau:
            return "BUF";
        case 0x24f208e4u:
        case 0xc00385b5u:
            return "MSG";
        case 0x6cab33b5u:
            return "INFO";
        case 0xd6b302c8u:
        case 0x11de6cdcu:
            return "PROP";
        case 0x763e0219u:
            return "MON";
        case 0xf2fb8359u:
            return "LAT";
        case 0x4674caeeu:
        case 0x86daf527u:
            return "PROF";
        case 0x3c43ef68u:
        case 0xf8f165eeu:
            return "SEQ";
        case 0x3b952e97u:
        case 0xafd8d0ecu:
            return "BANK";
        case 0x70954771u:
        case 0x3cfec826u:
            return "SLOT";
        case 0xfb8dc969u:
        case 0x1a3393eeu:
            return "KF";
        case 0x99380614u:
        case 0x9a321425u:
            return "CURVE";
        case 0x1010cf81u:
            return "MCAST";
        case 0x1dc014eeu:
            return "V4L";
        case 0x6b7b06c4u:
            return "Y4M";
        case 0x29df7ff5u:
        case 0x70d5bff2u:
            return "RES";
        case 0xb1ba38aeu:
            return "TRICK";
        case 0xb2c3d7b9u:
        case 0x23ff66c7u:
            return "CFG";
        case 0x4364e615u:
        case 0x4e0a1774u:
        case 0x11c2662du:
            return "SEL";
        case 0xd07076f3u:
        case 0xa10a8b80u:
            return "DEV";
        case 0xb35135fau:
        case 0xd1d746abu:
            return "IMG";
        case 0x090aa9abu:
            return "IDX";
        case 0xba4b77efu:
            return "STATE";
        case 0x5c6e1222u:
            return "CLEAR";
        case 0x650d33c0u:
            return "RESET";
        case 0xe562ea44u:
            return "COPY";
        case 0x1dff06aeu:
            return "GLOBAL";
        case 0xb45fa81au:
            return "FWD";
        case 0x675d2bc4u:
            return "REV";
        case 0x652b04dfu:
        case 0x2370e859u:
            return "START";
        case 0x6a8e75aau:
        case 0x6b532a96u:
            return "END";
        case 0xcb532ae5u:
        case 0x160a4da9u:
            return "STOP";
        case 0xbe269f5cu:
        case 0xdf66914fu:
            return "WRITE";
        case 0x3f110988u:
            return "FILE";
        case 0xab45f730u:
        case 0xec6ee012u:
            return "MODE";
        case 0xe0613999u:
            return "AUDIO";
        case 0xcef90b6cu:
            return "VIDEO";
        case 0x5d8b6dabu:
            return "ALPHA";
        case 0xf16d8413u:
            return "FADE";
        case 0x7084d38du:
        case 0x0a1997cbu:
            return "PAUSE";
        case 0x67c2444au:
        case 0xdb9215fdu:
            return "DEL";
        case 0xe60759e9u:
            return "LOAD";
        case 0xccff7e48u:
        case 0xc7e7bc2eu:
            return "SAVE";
        case 0xb4652c81u:
            return "CTRL";
        case 0x8cdfbd85u:
            return "CONV";
        case 0x1e99e7d9u:
        case 0x9e010407u:
            return "ENC";
        case 0x40296205u:
        case 0x93e05f71u:
            return "TOGGLE";
        default:
            return NULL;
    }
}

static gboolean gvr_pattern_format_description_label(
        GvrVimsPatternView *view,
        int id,
        const int *arguments,
        int argument_count,
        char *label,
        gsize label_size)
{
    const char *description;
    char **words;
    char base[GVR_PATTERN_LABEL_TEXT_MAX + 1] = { 0 };
    char suffix[16] = { 0 };
    gsize text_limit;
    gsize base_limit;
    int added = 0;

    if(!view || !view->description_lookup || !label || label_size == 0)
        return FALSE;

    description = view->description_lookup(id,
                                           view->description_lookup_data);
    if(!description || !description[0])
        return FALSE;

    text_limit = MIN((gsize)GVR_PATTERN_LABEL_TEXT_MAX,
                     label_size - 1);
    base_limit = text_limit;

    if(argument_count > 0) {
        g_snprintf(suffix,
                   sizeof(suffix),
                   "%d",
                   arguments[argument_count - 1]);

        const gsize suffix_length = strlen(suffix);
        if(suffix_length + 1 < text_limit)
            base_limit = text_limit - suffix_length - 1;
        else
            suffix[0] = '\0';
    }

    words = g_strsplit_set(description,
                          " \t\r\n/()[],:;<>-=+\'\".!?",
                          -1);

    for(int i = 0; words && words[i] && added < 3; i++) {
        const char *abbreviation;
        char upper[9];
        const char *piece;
        const gsize used = strlen(base);
        const guint32 hash =
            gvr_pattern_description_word_hash(words[i]);
        gsize piece_length;
        gsize required;

        if(!words[i][0] ||
           g_ascii_isdigit(words[i][0]) ||
           gvr_pattern_description_skip_hash(hash))
            continue;

        abbreviation =
            gvr_pattern_description_abbreviation(hash);
        if(abbreviation) {
            piece = abbreviation;
        }
        else {
            gvr_pattern_upper_token(words[i],
                                    upper,
                                    sizeof(upper));
            piece = upper;
        }

        if(!piece[0])
            continue;

        piece_length = strlen(piece);
        required = used + (used > 0 ? 1 : 0) + piece_length;

        if(required > base_limit) {
            if(used == 0 && base_limit > 0) {
                const gsize copy_length =
                    MIN(piece_length, base_limit);
                memcpy(base, piece, copy_length);
                base[copy_length] = '\0';
            }
            break;
        }

        if(used > 0)
            g_strlcat(base, " ", sizeof(base));
        g_strlcat(base, piece, sizeof(base));
        added++;
    }

    g_strfreev(words);

    if(!base[0])
        return FALSE;

    if(suffix[0])
        g_snprintf(label,
                   label_size,
                   "%s %s",
                   base,
                   suffix);
    else
        g_strlcpy(label, base, label_size);

    return TRUE;
}

static void gvr_pattern_format_label(GvrVimsPatternView *view,
                                     int id,
                                     const char *message,
                                     char *label,
                                     gsize label_size)
{
    int arguments[16];
    int argument_count =
        gvr_pattern_integer_arguments(message,
                                      arguments,
                                      G_N_ELEMENTS(arguments));
    int value =
        gvr_pattern_integer_argument(arguments,
                                     argument_count,
                                     argument_count - 1,
                                     0);
    int first =
        gvr_pattern_integer_argument(arguments,
                                     argument_count,
                                     0,
                                     0);
    int second =
        gvr_pattern_integer_argument(arguments,
                                     argument_count,
                                     1,
                                     0);
    int third =
        gvr_pattern_integer_argument(arguments,
                                     argument_count,
                                     2,
                                     0);
    int fourth =
        gvr_pattern_integer_argument(arguments,
                                     argument_count,
                                     3,
                                     0);
    char token[24];
    char upper[24];

    if(id == VIMS_SAMPLE_HOLD_FRAME) {
        g_snprintf(label, label_size, "HOLD %d", value);
        return;
    }

    if(id == VIMS_VIDEO_SET_SPEED ||
       id == VIMS_VIDEO_SET_SPEEDK ||
       id == VIMS_SAMPLE_SET_SPEED ||
       id == VIMS_SAMPLE_MIX_SET_SPEED ||
       id == VIMS_STREAM_BUFFER_SET_SPEED)
    {
        g_snprintf(label, label_size, "SPEED %d", value);
        return;
    }

    if(id == VIMS_VIDEO_SET_SLOW ||
       id == VIMS_SAMPLE_SET_DUP ||
       id == VIMS_SAMPLE_MIX_SET_DUP ||
       id == VIMS_STREAM_BUFFER_SET_SLOW)
    {
        g_snprintf(label, label_size, "SLOW %d", value);
        return;
    }

    if(id == VIMS_VIDEO_PLAY_FORWARD ||
       id == VIMS_STREAM_BUFFER_FORWARD)
    {
        g_strlcpy(label, "FWD", label_size);
        return;
    }

    if(id == VIMS_VIDEO_PLAY_BACKWARD ||
       id == VIMS_STREAM_BUFFER_BACKWARD)
    {
        g_strlcpy(label, "REV", label_size);
        return;
    }

    if(id == VIMS_VIDEO_PLAY_STOP ||
       id == VIMS_STREAM_BUFFER_STOP)
    {
        g_strlcpy(label, "STOP", label_size);
        return;
    }

    if(id == VIMS_VIDEO_PLAY_STOP_ALL) {
        g_strlcpy(label, "STOP ALL", label_size);
        return;
    }

    if(id == VIMS_VIDEO_SET_FREEZE) {
        g_strlcpy(label, "FREEZE", label_size);
        return;
    }

    if(id == VIMS_VIDEO_SKIP_FRAME) {
        g_snprintf(label,
                   label_size,
                   "JUMP +%d",
                   ABS(gvr_pattern_integer_argument(arguments,
                                                    argument_count,
                                                    0,
                                                    1)));
        return;
    }

    if(id == VIMS_VIDEO_PREV_FRAME) {
        g_snprintf(label,
                   label_size,
                   "JUMP -%d",
                   ABS(gvr_pattern_integer_argument(arguments,
                                                    argument_count,
                                                    0,
                                                    1)));
        return;
    }

    if(id == VIMS_SAMPLE_SKIP_FRAME) {
        g_snprintf(label, label_size, "JUMP %+d", second);
        return;
    }

    if(id == VIMS_VIDEO_SKIP_SECOND) {
        g_snprintf(label,
                   label_size,
                   "JUMP +%ds",
                   ABS(gvr_pattern_integer_argument(arguments,
                                                    argument_count,
                                                    0,
                                                    1)));
        return;
    }

    if(id == VIMS_VIDEO_PREV_SECOND) {
        g_snprintf(label,
                   label_size,
                   "JUMP -%ds",
                   ABS(gvr_pattern_integer_argument(arguments,
                                                    argument_count,
                                                    0,
                                                    1)));
        return;
    }

    if(id == VIMS_VIDEO_GOTO_START) {
        g_strlcpy(label, "JUMP START", label_size);
        return;
    }

    if(id == VIMS_VIDEO_GOTO_END) {
        g_strlcpy(label, "JUMP END", label_size);
        return;
    }

    if(id == VIMS_VIDEO_SET_FRAME) {
        g_snprintf(label, label_size, "FRAME %d", first);
        return;
    }

    if(id == VIMS_VIDEO_SET_FRAME_PERCENTAGE) {
        g_snprintf(label, label_size, "POS %d%%", first);
        return;
    }

    if(id == VIMS_STREAM_BUFFER_SKIP_FRAME) {
        g_snprintf(label, label_size, "JUMP %+d", second);
        return;
    }

    if(id == VIMS_STREAM_BUFFER_SKIP_SECOND) {
        g_snprintf(label, label_size, "JUMP +%ds", ABS(second));
        return;
    }

    if(id == VIMS_STREAM_BUFFER_PREV_SECOND) {
        g_snprintf(label, label_size, "JUMP -%ds", ABS(second));
        return;
    }

    if(id == VIMS_STREAM_BUFFER_SET_FRAME) {
        g_snprintf(label, label_size, "FRAME %d", second);
        return;
    }

    if(id == VIMS_SAMPLE_NEXT) {
        g_strlcpy(label, "NEXT", label_size);
        return;
    }

    if(id == VIMS_SELECT_BANK) {
        g_snprintf(label, label_size, "BANK %d", first);
        return;
    }

    if(id == VIMS_SELECT_ID) {
        g_snprintf(label, label_size, "SLOT %d", first);
        return;
    }

    if(id == VIMS_RESUME_ID) {
        g_snprintf(label, label_size, "RESUME %d", first);
        return;
    }

    if(id == VIMS_SAMPLE_TOGGLE_LOOP ||
       id == VIMS_SAMPLE_SET_LOOPTYPE)
    {
        g_snprintf(label, label_size, "LOOP %d", value);
        return;
    }

    if(id == VIMS_SAMPLE_TOGGLE_RAND_LOOP) {
        g_snprintf(label, label_size, "RAND LOOP %d", value);
        return;
    }

    if(id == VIMS_SAMPLE_SET_LOOPS) {
        g_snprintf(label, label_size, "LOOPS %d", value);
        return;
    }

    if(id == VIMS_SAMPLE_SET_POSITION) {
        g_snprintf(label, label_size, "MIX POS %d", third);
        return;
    }

    if(id == VIMS_SAMPLE_MOVE_MARKER) {
        g_snprintf(label, label_size, "MARK %+d", second);
        return;
    }

    if(id == VIMS_SAMPLE_SET_VOLUME) {
        g_snprintf(label, label_size, "SMP VOL %d", value);
        return;
    }

    if(id == VIMS_SET_VOLUME) {
        g_snprintf(label, label_size, "VOL %d", first);
        return;
    }

    if(id == VIMS_FRAMERATE) {
        g_snprintf(label,
                   label_size,
                   "FPS %d.%02d",
                   first / 100,
                   ABS(first % 100));
        return;
    }

    if(id == VIMS_STREAM_SET_BUFFER_LENGTH) {
        g_snprintf(label,
                   label_size,
                   "TRICK BUF %d",
                   second);
        return;
    }

    if(id == VIMS_STREAM_SET_LENGTH) {
        g_snprintf(label, label_size, "STRM LEN %d", first);
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_CLEAR) {
        g_snprintf(label, label_size, "CLEAR FX %d", second);
        return;
    }

    if(id == VIMS_CHAIN_CLEAR) {
        g_strlcpy(label, "CLEAR FX CHAIN", label_size);
        return;
    }

    if(id == VIMS_CHAIN_SET_ENTRY) {
        g_snprintf(label, label_size, "FX SLOT %d", first);
        return;
    }

    if(id == VIMS_GLOBAL_CHAIN_COPY) {
        g_strlcpy(label, "COPY GLOBAL FX", label_size);
        return;
    }

    if(id == VIMS_SAMPLE_KF_STATUS_PARAM) {
        g_snprintf(label,
                   label_size,
                   "KF%d P%d %s",
                   second,
                   third,
                   gvr_pattern_state_name(fourth));
        return;
    }

    if(id == VIMS_SAMPLE_KF_STATUS) {
        g_snprintf(label,
                   label_size,
                   "KF%d %s C%d",
                   first,
                   gvr_pattern_state_name(second),
                   third);
        return;
    }

    if(id == VIMS_SAMPLE_KF_RESET) {
        g_snprintf(label, label_size, "CLEAR KF %d", first);
        return;
    }

    if(id == VIMS_SAMPLE_KF_CLEAR) {
        g_snprintf(label,
                   label_size,
                   "CLEAR KF%d P%d",
                   first,
                   second);
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_SET_PRESET ||
       id == VIMS_CHAIN_ENTRY_SET_EFFECT)
    {
        g_strlcpy(label, "FX PRESET", label_size);
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_SET_ARG_VAL) {
        g_snprintf(label,
                   label_size,
                   "FX%d P%d=%d",
                   second,
                   third,
                   fourth);
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_SET_NARG_VAL) {
        if(gvr_pattern_last_argument_text(message,
                                          token,
                                          sizeof(token)))
            g_snprintf(label,
                       label_size,
                       "FX%d P%d=%s",
                       second,
                       third,
                       token);
        else
            g_snprintf(label,
                       label_size,
                       "FX%d P%d",
                       second,
                       third);
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_INC_ARG) {
        g_snprintf(label,
                   label_size,
                   "P%d +%d",
                   first,
                   ABS(second));
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_DEC_ARG) {
        g_snprintf(label,
                   label_size,
                   "P%d -%d",
                   first,
                   ABS(second));
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_UP) {
        g_snprintf(label,
                   label_size,
                   "FX +%d",
                   ABS(gvr_pattern_integer_argument(arguments,
                                                    argument_count,
                                                    0,
                                                    1)));
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_DOWN) {
        g_snprintf(label,
                   label_size,
                   "FX -%d",
                   ABS(gvr_pattern_integer_argument(arguments,
                                                    argument_count,
                                                    0,
                                                    1)));
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_CHANNEL_INC) {
        g_snprintf(label,
                   label_size,
                   "SRC +%d",
                   ABS(gvr_pattern_integer_argument(arguments,
                                                    argument_count,
                                                    0,
                                                    1)));
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_CHANNEL_DEC) {
        g_snprintf(label,
                   label_size,
                   "SRC -%d",
                   ABS(gvr_pattern_integer_argument(arguments,
                                                    argument_count,
                                                    0,
                                                    1)));
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_SOURCE_TOGGLE) {
        g_snprintf(label,
                   label_size,
                   "FX%d %s",
                   first,
                   second ? "STRM" : "SMP");
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_SET_STATE) {
        g_snprintf(label, label_size, "FX%d TOGGLE", second);
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_SET_VIDEO_ON) {
        g_snprintf(label, label_size, "FX%d ON", second);
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_SET_VIDEO_OFF) {
        g_snprintf(label, label_size, "FX%d OFF", second);
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_SET_DEFAULTS) {
        g_snprintf(label, label_size, "FX%d RESET", second);
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_SET_CHANNEL) {
        g_snprintf(label,
                   label_size,
                   "FX%d SRC %d",
                   second,
                   third);
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_SET_SOURCE) {
        g_snprintf(label,
                   label_size,
                   "FX%d %s",
                   second,
                   third ? "STRM" : "SMP");
        return;
    }

    if(id == VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL) {
        g_snprintf(label,
                   label_size,
                   "FX%d %s%d",
                   second,
                   third ? "STRM" : "SMP",
                   fourth);
        return;
    }

    if(id == VIMS_CHAIN_TOGGLE) {
        g_strlcpy(label, "CHAIN TOGGLE", label_size);
        return;
    }

    if(id == VIMS_CHAIN_ENABLE) {
        g_strlcpy(label, "CHAIN ON", label_size);
        return;
    }

    if(id == VIMS_CHAIN_DISABLE) {
        g_strlcpy(label, "CHAIN OFF", label_size);
        return;
    }

    if(id == VIMS_SAMPLE_CHAIN_ENABLE) {
        g_snprintf(label, label_size, "SMP%d FX ON", first);
        return;
    }

    if(id == VIMS_SAMPLE_CHAIN_DISABLE) {
        g_snprintf(label, label_size, "SMP%d FX OFF", first);
        return;
    }

    if(id == VIMS_STREAM_CHAIN_ENABLE) {
        g_snprintf(label, label_size, "STRM%d FX ON", first);
        return;
    }

    if(id == VIMS_STREAM_CHAIN_DISABLE) {
        g_snprintf(label, label_size, "STRM%d FX OFF", first);
        return;
    }

    if(id == VIMS_GLOBAL_CHAIN) {
        if(second <= 0)
            g_strlcpy(label, "GLOBAL FX OFF", label_size);
        else if(second == 1)
            g_strlcpy(label, "GLOBAL FX PRE", label_size);
        else
            g_strlcpy(label, "GLOBAL FX POST", label_size);
        return;
    }

    if(id == VIMS_CHAIN_MANUAL_FADE) {
        g_snprintf(label, label_size, "CHAIN OP %d", second);
        return;
    }

    if(id == VIMS_CHAIN_FADE_ENTRY) {
        g_snprintf(label, label_size, "MIX SRC %d", second);
        return;
    }

    if(id == VIMS_CHAIN_FADE_METHOD) {
        g_snprintf(label, label_size, "MIX MODE %d", second);
        return;
    }

    if(id == VIMS_CHAIN_FADE_ALPHA) {
        g_snprintf(label,
                   label_size,
                   "MIX ALPHA %s",
                   gvr_pattern_state_name(second));
        return;
    }

    if(id == VIMS_SUB_RENDER) {
        g_snprintf(label,
                   label_size,
                   "SUB FX %s",
                   gvr_pattern_state_name(second));
        return;
    }

    if(id == VIMS_SUB_RENDER_ENTRY) {
        g_snprintf(label,
                   label_size,
                   "SUB FX%d %s",
                   second,
                   gvr_pattern_state_name(third));
        return;
    }

    if(id == VIMS_FEEDBACK) {
        g_snprintf(label,
                   label_size,
                   "FEEDBACK %s",
                   gvr_pattern_state_name(first));
        return;
    }

    if(id == VIMS_ALPHA_COMPOSITE) {
        if(first)
            g_snprintf(label, label_size, "ALPHA %d", second);
        else
            g_strlcpy(label, "ALPHA OFF", label_size);
        return;
    }

    if(id == VIMS_TOGGLE_TRANSITIONS) {
        g_snprintf(label,
                   label_size,
                   "TRANS %s",
                   gvr_pattern_state_name(first));
        return;
    }

    if(id == VIMS_SET_TRANSITION) {
        if(third)
            g_snprintf(label,
                       label_size,
                       "TRANS S%d L%d",
                       fourth,
                       value);
        else
            g_strlcpy(label, "TRANS OFF", label_size);
        return;
    }

    if(id == VIMS_AUDIO_ENABLE) {
        g_strlcpy(label, "AUDIO ON", label_size);
        return;
    }

    if(id == VIMS_AUDIO_DISABLE) {
        g_strlcpy(label, "AUDIO OFF", label_size);
        return;
    }

    if(id == VIMS_AUDIO_TOGGLE_MUTE) {
        g_snprintf(label,
                   label_size,
                   "MUTE %s",
                   gvr_pattern_state_name(first));
        return;
    }

    if(id == VIMS_AUDIO_BEAT_TOGGLE) {
        g_strlcpy(label, "BEAT TOGGLE", label_size);
        return;
    }

    if(id == VIMS_AUDIO_BEAT_STATUS) {
        g_snprintf(label,
                   label_size,
                   "BEAT %s",
                   gvr_pattern_state_name(first));
        return;
    }

    if(id == VIMS_AUDIO_BEAT_ACTION) {
        g_snprintf(label, label_size, "BEAT ACT %d", first);
        return;
    }

    if(id == VIMS_AUDIO_BEAT_FREEZE) {
        g_snprintf(label, label_size, "BEAT HOLD %d", first);
        return;
    }

    if(id == VIMS_AUDIO_BEAT_COOLDOWN) {
        g_snprintf(label, label_size, "BEAT COOL %d", first);
        return;
    }

    if(id == VIMS_AUDIO_BEAT_THRESHOLD) {
        g_snprintf(label, label_size, "BEAT THR %d", first);
        return;
    }

    if(id == VIMS_AUDIO_BEAT_CHANNELS) {
        g_snprintf(label, label_size, "BEAT CH %d", first);
        return;
    }

    if(id == VIMS_AUDIO_BEAT_PULSE) {
        g_snprintf(label, label_size, "BEAT PULSE %d", first);
        return;
    }

    if(id == VIMS_AUDIO_BEAT_GATE) {
        g_snprintf(label, label_size, "BEAT GATE %d", first);
        return;
    }

    if(id == VIMS_AUDIO_BEAT_AUTO_MODE) {
        g_snprintf(label, label_size, "AUTO FX %d", first);
        return;
    }

    if(id == VIMS_AUDIO_BEAT_AUTO_AMOUNT) {
        g_snprintf(label, label_size, "AUTO AMT %d", first);
        return;
    }

    if(id == VIMS_AUDIO_BEAT_AUTO_RESET) {
        g_strlcpy(label, "AUTO RESET", label_size);
        return;
    }

    if(id == VIMS_AUDIO_BEAT_SCRATCH_SENSITIVITY) {
        g_snprintf(label, label_size, "SCRATCH %d", first);
        return;
    }

    if(id == VIMS_AUDIO_BEAT_SOURCE_LOSS_PAUSE) {
        g_snprintf(label,
                   label_size,
                   "LOSS PAUSE %s",
                   gvr_pattern_state_name(first));
        return;
    }

    if(id == VIMS_AUDIO_BEAT_MONITOR_LATENCY) {
        if(first < 0)
            g_strlcpy(label, "MON LAT AUTO", label_size);
        else
            g_snprintf(label, label_size, "MON LAT %d", first);
        return;
    }

    if(id == VIMS_AUDIO_BEAT_CONFIG ||
       id == VIMS_AUDIO_BEAT_UI_CONFIG)
    {
        g_snprintf(label, label_size, "BEAT CFG %d", value);
        return;
    }

    if(id == VIMS_AUDIO_SYNC_STATUS) {
        g_snprintf(label,
                   label_size,
                   "SYNC %s",
                   gvr_pattern_state_name(first));
        return;
    }

    if(id == VIMS_AUDIO_SYNC_MODE) {
        g_snprintf(label, label_size, "SYNC MODE %d", first);
        return;
    }

    if(id == VIMS_AUDIO_SYNC_JACK) {
        g_snprintf(label,
                   label_size,
                   "SYNC JACK M%d C%d",
                   first,
                   second);
        return;
    }

    if(id == VIMS_AUDIO_SYNC_WAV) {
        g_snprintf(label, label_size, "SYNC WAV M%d", first);
        return;
    }

    if(id == VIMS_AUDIO_SYNC_WAV_PROFILE_SET) {
        g_snprintf(label, label_size, "WAV PROF %d", first);
        return;
    }

    if(id == VIMS_AUDIO_SYNC_WAV_PROFILE_CLEAR) {
        g_snprintf(label, label_size, "WAV CLR %d", first);
        return;
    }

    if(id == VIMS_SAMPLE_AUDIO_SYNC_SET) {
        g_snprintf(label,
                   label_size,
                   "SMP%d SYNC M%d",
                   first,
                   fourth);
        return;
    }

    if(id == VIMS_SAMPLE_AUDIO_SYNC_CLEAR) {
        g_snprintf(label, label_size, "SMP%d SYNC OFF", first);
        return;
    }

    if(id == VIMS_SAMPLE_AUDIO_SYNC_REARM) {
        g_snprintf(label, label_size, "SMP%d REARM", first);
        return;
    }

    if(id == VIMS_AUDIO_SYNC_TARGET) {
        g_snprintf(label,
                   label_size,
                   "BPM %d.%d",
                   first / 10,
                   ABS(first % 10));
        return;
    }

    if(id == VIMS_AUDIO_SYNC_CORRECTION) {
        g_snprintf(label, label_size, "SYNC CORR %d", first);
        return;
    }

    if(id == VIMS_AUDIO_MIX_MODE) {
        g_snprintf(label, label_size, "MIX MODE %d", first);
        return;
    }

    if(id == VIMS_AUDIO_MIX_CROSSFADE) {
        g_snprintf(label, label_size, "MIX XFADE %d", first);
        return;
    }

    if(id == VIMS_STREAM_COLOR) {
        g_snprintf(label,
                   label_size,
                   "RGB %d,%d,%d",
                   second,
                   third,
                   fourth);
        return;
    }

    if(id == VIMS_STREAM_SET_BRIGHTNESS) {
        g_snprintf(label, label_size, "BRIGHT %d", second);
        return;
    }

    if(id == VIMS_STREAM_SET_CONTRAST) {
        g_snprintf(label, label_size, "CONTRAST %d", second);
        return;
    }

    if(id == VIMS_STREAM_SET_HUE) {
        g_snprintf(label, label_size, "HUE %d", second);
        return;
    }

    if(id == VIMS_STREAM_SET_COLOR) {
        g_snprintf(label, label_size, "COLOR %d", second);
        return;
    }

    if(id == VIMS_STREAM_SET_SATURATION) {
        g_snprintf(label, label_size, "SAT %d", second);
        return;
    }

    if(id == VIMS_STREAM_SET_WHITE) {
        g_snprintf(label, label_size, "WHITE %d", second);
        return;
    }

    if(id == VIMS_STREAM_SET_V4LCTRL) {
        if(gvr_pattern_last_argument_text(message,
                                          token,
                                          sizeof(token))) {
            gvr_pattern_upper_token(token,
                                    upper,
                                    sizeof(upper));
            g_snprintf(label,
                       label_size,
                       "V4L %s %d",
                       upper,
                       second);
        }
        else {
            g_snprintf(label, label_size, "V4L %d", second);
        }
        return;
    }

    if(id == VIMS_RGB_PARAMETER_TYPE) {
        g_snprintf(label, label_size, "YUV/RGB %d", first);
        return;
    }

    if(id == VIMS_COLOR_VIBRANCE) {
        g_snprintf(label, label_size, "VIBRANCE %d", first);
        return;
    }

    if(gvr_pattern_format_description_label(view,
                                            id,
                                            arguments,
                                            argument_count,
                                            label,
                                            label_size))
        return;

    g_snprintf(label, label_size, "%03d", id);
}

static void gvr_pattern_refresh_event_label(GvrVimsPatternView *view,
                                            GvrVimsPatternEvent *event)
{
    char generated[sizeof(event->label)];
    char numeric[16];

    if(!view || !event || !event->message)
        return;

    gvr_pattern_format_label(view,
                             event->vims_id,
                             event->message,
                             generated,
                             sizeof(generated));
    g_snprintf(numeric, sizeof(numeric), "%03d", event->vims_id);

    if(strcmp(generated, numeric) == 0 && event->label[0])
        return;

    g_strlcpy(event->label, generated, sizeof(event->label));
}

static gboolean gvr_pattern_relabel_row(gpointer key,
                                        gpointer value,
                                        gpointer data)
{
    GvrVimsPatternView *view = data;
    GvrVimsPatternRow *row = value;

    (void)key;

    for(int column = 0; column < GVR_VIMS_PATTERN_COLUMNS; column++)
        gvr_pattern_refresh_event_label(view,
                                        &row->events[column]);

    return FALSE;
}

static void gvr_pattern_relabel_cell(GvrVimsPatternView *view,
                                     GvrVimsPatternCell *cell)
{
    if(view && cell && cell->rows)
        g_tree_foreach(cell->rows,
                       gvr_pattern_relabel_row,
                       view);
}

static void gvr_pattern_relabel_dynamic_cell(gpointer key,
                                             gpointer value,
                                             gpointer data)
{
    (void)key;
    gvr_pattern_relabel_cell(data, value);
}

static void gvr_pattern_relabel_all(GvrVimsPatternView *view)
{
    if(!view)
        return;

    for(int bank = 0; bank < GVR_VIMS_PATTERN_BANKS; bank++) {
        for(int slot = 0; slot < GVR_VIMS_PATTERN_SLOTS; slot++)
            gvr_pattern_relabel_cell(view,
                                     &view->cells[bank][slot]);

        gvr_pattern_relabel_cell(view,
                                 &view->bank_cells[bank]);
    }

    if(view->sample_cells)
        g_hash_table_foreach(view->sample_cells,
                             gvr_pattern_relabel_dynamic_cell,
                             view);
    if(view->stream_cells)
        g_hash_table_foreach(view->stream_cells,
                             gvr_pattern_relabel_dynamic_cell,
                             view);

    if(view->area)
        gtk_widget_queue_draw(view->area);
}

static char *gvr_pattern_normalize_message(const char *message)
{
    char *copy;
    char *trimmed;
    gsize length;
    const char *first_semicolon;

    if(!message)
        return NULL;

    copy = g_strdup(message);
    trimmed = g_strstrip(copy);
    length = strlen(trimmed);

    if(length == 0 || length >= GVR_VIMS_PATTERN_MESSAGE_MAX) {
        g_free(copy);
        return NULL;
    }

    first_semicolon = strchr(trimmed, ';');
    if(first_semicolon && first_semicolon[1] != '\0') {
        g_free(copy);
        return NULL;
    }

    if(trimmed[length - 1] != ';') {
        if(length + 1 >= GVR_VIMS_PATTERN_MESSAGE_MAX) {
            g_free(copy);
            return NULL;
        }

        char *with_term = g_strdup_printf("%s;", trimmed);
        g_free(copy);
        copy = with_term;
        trimmed = copy;
        length++;
    }

    if(trimmed != copy)
        memmove(copy, trimmed, strlen(trimmed) + 1);

    if(length >= GVR_VIMS_PATTERN_MESSAGE_MAX) {
        g_free(copy);
        return NULL;
    }

    return copy;
}

static int gvr_pattern_visible_rows(GvrVimsPatternView *view)
{
    GtkAllocation allocation;
    int rows;

    gtk_widget_get_allocation(view->area, &allocation);
    rows = (allocation.height - GVR_PATTERN_HEADER_HEIGHT) / gvr_pattern_row_height(view);
    return rows < 1 ? 1 : rows;
}

static int gvr_pattern_effective_frame_count(GvrVimsPatternView *view)
{
    int count;
    gint64 current_frame;

    if(!gvr_pattern_target_valid(view->selected_bank,
                                 view->selected_slot))
        return 0;

    count = view->frame_count;
    current_frame =
        (gint64)view->selected_row *
        MAX(1, view->row_step);

    if(!view->frame_count_known &&
       count < GVR_PATTERN_DEFAULT_FRAMES)
        count = GVR_PATTERN_DEFAULT_FRAMES;

    if(!view->frame_count_known &&
       current_frame + 1 > count)
    {
        count = (int)MIN((gint64)GVR_PATTERN_MAX_FRAMES,
                         current_frame + 1);
    }

    return gvr_pattern_clampi(count,
                              1,
                              GVR_PATTERN_MAX_FRAMES);
}

static int gvr_pattern_edit_step(GvrVimsPatternView *view)
{
    return view ? MAX(1, view->row_step) : 1;
}

static int gvr_pattern_display_row_count(GvrVimsPatternView *view)
{
    const int count = gvr_pattern_effective_frame_count(view);
    const int step = gvr_pattern_edit_step(view);

    if(count <= 0)
        return 0;

    return 1 + (count - 1) / step;
}

static int gvr_pattern_frame_to_display_row(GvrVimsPatternView *view, int frame)
{
    return MAX(0, frame) / gvr_pattern_edit_step(view);
}

static int gvr_pattern_display_row_to_frame(GvrVimsPatternView *view, int row)
{
    const int count = gvr_pattern_effective_frame_count(view);
    const int step = gvr_pattern_edit_step(view);
    const gint64 frame = (gint64)MAX(0, row) * step;

    if(count <= 0)
        return 0;

    return (int)MIN(frame, (gint64)count - 1);
}

typedef struct {
    int frame_count;
    int events;
} GvrPatternOutsideRange;

static gboolean gvr_pattern_count_outside_range(gpointer key, gpointer value, gpointer data)
{
    (void)key;
    GvrVimsPatternRow *row = value;
    GvrPatternOutsideRange *outside = data;

    if(!row || row->frame < outside->frame_count)
        return FALSE;

    for(int column = 0; column < GVR_VIMS_PATTERN_COLUMNS; column++)
        if(row->events[column].message)
            outside->events++;

    return FALSE;
}

static void gvr_pattern_update_adjustment(GvrVimsPatternView *view)
{
    const int visible_rows = gvr_pattern_visible_rows(view);
    const int display_rows = gvr_pattern_display_row_count(view);
    double value = gtk_adjustment_get_value(view->vadjustment);

    if(display_rows <= 0) {
        gtk_adjustment_configure(view->vadjustment, 0, 0, 1, 1, 1, 1);
        return;
    }

    const int page = MIN(visible_rows, display_rows);
    gtk_adjustment_configure(view->vadjustment,
                             MIN(value, MAX(0, display_rows - page)),
                             0,
                             display_rows,
                             1,
                             MAX(1, page - 1),
                             page);
}

static void gvr_pattern_scroll_to_cursor(GvrVimsPatternView *view)
{
    int visible_rows;
    int top_row;

    if(gvr_pattern_display_row_count(view) <= 0) {
        gtk_adjustment_set_value(view->vadjustment, 0);
        return;
    }

    visible_rows = gvr_pattern_visible_rows(view);
    top_row = (int)gtk_adjustment_get_value(view->vadjustment);

    if(view->selected_row < top_row)
        gtk_adjustment_set_value(view->vadjustment, view->selected_row);
    else if(view->selected_row >= top_row + visible_rows)
        gtk_adjustment_set_value(view->vadjustment,
                                 MAX(0, view->selected_row - visible_rows + 1));
}

static void gvr_pattern_set_cursor_row(GvrVimsPatternView *view,
                                       int row,
                                       int column)
{
    const int display_rows = gvr_pattern_display_row_count(view);

    view->selected_column = gvr_pattern_clampi(column,
                                                0,
                                                GVR_VIMS_PATTERN_COLUMNS - 1);

    if(display_rows <= 0) {
        view->selected_row = 0;
        gvr_pattern_update_adjustment(view);
        gtk_widget_queue_draw(view->area);
        return;
    }

    view->selected_row = gvr_pattern_clampi(row, 0, display_rows - 1);
    gvr_pattern_update_adjustment(view);
    gvr_pattern_scroll_to_cursor(view);
    gtk_widget_queue_draw(view->area);
}

static void gvr_pattern_set_cursor(GvrVimsPatternView *view,
                                   int frame,
                                   int column)
{
    gvr_pattern_set_cursor_row(view,
                               gvr_pattern_frame_to_display_row(view, frame),
                               column);
}

static int gvr_pattern_current_frame(GvrVimsPatternView *view)
{
    return gvr_pattern_display_row_to_frame(view, view->selected_row);
}

static int gvr_pattern_follow_frame(GvrVimsPatternView *view,
                                    int live_frame)
{
    int frame;
    int step;

    frame = MAX(0, live_frame);
    frame = gvr_pattern_clampi(frame, 0, GVR_PATTERN_MAX_FRAMES - 1);
    step = gvr_pattern_edit_step(view);

    if(view->frame_count_known)
        frame = gvr_pattern_clampi(frame,
                                   0,
                                   MAX(0, view->frame_count - 1));

    frame = (frame / step) * step;

    if(frame + 1 > view->frame_count) {
        view->frame_count = frame + 1;
        gvr_pattern_update_adjustment(view);
    }

    return frame;
}


static void gvr_pattern_selection_clear(GvrVimsPatternView *view)
{
    if(!view)
        return;

    view->selection_active = FALSE;
    view->selection_anchor_row = view->selected_row;
    view->selection_anchor_column = view->selected_column;
}

static void gvr_pattern_selection_begin(GvrVimsPatternView *view)
{
    if(!view || view->selection_active)
        return;

    view->selection_active = TRUE;
    view->selection_anchor_row = view->selected_row;
    view->selection_anchor_column = view->selected_column;
}

static void gvr_pattern_selection_bounds(GvrVimsPatternView *view,
                                         int *first_row,
                                         int *last_row,
                                         int *first_column,
                                         int *last_column)
{
    int anchor_row;
    int anchor_column;

    anchor_row = view->selection_active ?
                 view->selection_anchor_row : view->selected_row;
    anchor_column = view->selection_active ?
                    view->selection_anchor_column : view->selected_column;

    *first_row = MIN(anchor_row, view->selected_row);
    *last_row = MAX(anchor_row, view->selected_row);
    *first_column = MIN(anchor_column, view->selected_column);
    *last_column = MAX(anchor_column, view->selected_column);
}

static gboolean gvr_pattern_selection_contains(GvrVimsPatternView *view,
                                               int row,
                                               int column)
{
    int first_row;
    int last_row;
    int first_column;
    int last_column;

    if(!view || !view->selection_active)
        return FALSE;

    gvr_pattern_selection_bounds(view,
                                 &first_row,
                                 &last_row,
                                 &first_column,
                                 &last_column);

    return row >= first_row && row <= last_row &&
           column >= first_column && column <= last_column;
}

static void gvr_pattern_move_cursor(GvrVimsPatternView *view,
                                    int row_delta,
                                    int column_delta,
                                    gboolean extend,
                                    gboolean seek)
{
    int row = view->selected_row + row_delta;
    int column = view->selected_column + column_delta;

    if(extend)
        gvr_pattern_selection_begin(view);
    else
        gvr_pattern_selection_clear(view);

    gvr_pattern_move_row(view, row, column, seek);

    if(extend)
        view->selection_active = TRUE;
}

static GvrVimsPatternEvent *gvr_pattern_block_event(
        GvrPatternBlockClipboard *clipboard,
        int row,
        int column)
{
    if(!clipboard || !clipboard->events ||
       row < 0 || row >= clipboard->rows ||
       column < 0 || column >= clipboard->columns)
        return NULL;

    return &clipboard->events[row * clipboard->columns + column];
}

static void gvr_pattern_block_clipboard_clear(GvrPatternBlockClipboard *clipboard)
{
    if(!clipboard)
        return;

    if(clipboard->events) {
        const int count = clipboard->rows * clipboard->columns;

        for(int index = 0; index < count; index++)
            gvr_pattern_event_clear(&clipboard->events[index]);

        g_free(clipboard->events);
    }

    memset(clipboard, 0, sizeof(*clipboard));
}

static gboolean gvr_pattern_copy_block(GvrVimsPatternView *view)
{
    GvrVimsPatternCell *cell;
    int first_row;
    int last_row;
    int first_column;
    int last_column;

    if(!gvr_pattern_target_editable(view))
        return FALSE;

    cell = gvr_pattern_cell(view, view->selected_bank, view->selected_slot);
    if(!cell)
        return FALSE;

    gvr_pattern_selection_bounds(view,
                                 &first_row,
                                 &last_row,
                                 &first_column,
                                 &last_column);

    if((gint64)(last_row - first_row + 1) *
       (last_column - first_column + 1) >
       GVR_PATTERN_BLOCK_MAX_CELLS)
    {
        g_warning("VIMS pattern block selection is too large to copy");
        return FALSE;
    }

    gvr_pattern_block_clipboard_clear(&view->block_clipboard);
    view->block_clipboard.rows = last_row - first_row + 1;
    view->block_clipboard.columns = last_column - first_column + 1;
    view->block_clipboard.frame_step = gvr_pattern_edit_step(view);
    view->block_clipboard.events =
        g_new0(GvrVimsPatternEvent,
               view->block_clipboard.rows *
               view->block_clipboard.columns);

    for(int row_index = 0;
        row_index < view->block_clipboard.rows;
        row_index++)
    {
        const int frame =
            gvr_pattern_display_row_to_frame(view, first_row + row_index);
        GvrVimsPatternRow *row =
            gvr_pattern_row_get(cell, frame, FALSE);

        for(int column_index = 0;
            column_index < view->block_clipboard.columns;
            column_index++)
        {
            GvrVimsPatternEvent *dst =
                gvr_pattern_block_event(&view->block_clipboard,
                                        row_index,
                                        column_index);

            if(row)
                gvr_pattern_event_copy(
                    dst,
                    &row->events[first_column + column_index]);
        }
    }

    /* Pattern block copy becomes the active paste source. */
    gtk_clipboard_clear(
        gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));

    return TRUE;
}


typedef struct {
    GvrVimsPatternView *view;
    int first_row;
    int last_row;
    int first_column;
    int last_column;
    gboolean found;
    gboolean changed;
    GPtrArray *empty_rows;
} GvrPatternSelectionScan;

static gboolean gvr_pattern_selection_scan_row(gpointer key,
                                               gpointer value,
                                               gpointer data)
{
    GvrPatternSelectionScan *scan = data;
    GvrVimsPatternRow *row = value;
    const int frame = GPOINTER_TO_INT(key) - 1;
    const int display_row =
        gvr_pattern_frame_to_display_row(scan->view, frame);

    if(gvr_pattern_display_row_to_frame(scan->view,
                                        display_row) != frame ||
       display_row < scan->first_row ||
       display_row > scan->last_row)
        return FALSE;

    for(int column = scan->first_column;
        column <= scan->last_column;
        column++)
    {
        if(row->events[column].message) {
            scan->found = TRUE;
            if(!scan->empty_rows)
                return TRUE;
        }
    }

    return FALSE;
}

static gboolean gvr_pattern_selection_clear_row(gpointer key,
                                                gpointer value,
                                                gpointer data)
{
    GvrPatternSelectionScan *scan = data;
    GvrVimsPatternRow *row = value;
    const int frame = GPOINTER_TO_INT(key) - 1;
    const int display_row =
        gvr_pattern_frame_to_display_row(scan->view, frame);

    if(gvr_pattern_display_row_to_frame(scan->view,
                                        display_row) != frame ||
       display_row < scan->first_row ||
       display_row > scan->last_row)
        return FALSE;

    for(int column = scan->first_column;
        column <= scan->last_column;
        column++)
    {
        if(row->events[column].message) {
            gvr_pattern_event_clear(&row->events[column]);
            scan->changed = TRUE;
        }
    }

    if(gvr_pattern_row_empty(row))
        g_ptr_array_add(scan->empty_rows, key);

    return FALSE;
}

static gboolean gvr_pattern_selection_has_events(GvrVimsPatternView *view)
{
    GvrVimsPatternCell *cell;
    GvrPatternSelectionScan scan = { 0 };

    if(!gvr_pattern_target_editable(view))
        return FALSE;

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);
    if(!cell || !cell->rows)
        return FALSE;

    scan.view = view;
    gvr_pattern_selection_bounds(
        view,
        &scan.first_row,
        &scan.last_row,
        &scan.first_column,
        &scan.last_column);
    g_tree_foreach(cell->rows,
                   gvr_pattern_selection_scan_row,
                   &scan);
    return scan.found;
}

static gboolean gvr_pattern_clear_block(GvrVimsPatternView *view)
{
    GvrVimsPatternCell *cell;
    GvrPatternSelectionScan scan = { 0 };

    if(!gvr_pattern_selection_has_events(view))
        return FALSE;

    if(!gvr_pattern_push_undo(view))
        return FALSE;

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);
    scan.view = view;
    scan.empty_rows = g_ptr_array_new();
    gvr_pattern_selection_bounds(
        view,
        &scan.first_row,
        &scan.last_row,
        &scan.first_column,
        &scan.last_column);

    g_tree_foreach(cell->rows,
                   gvr_pattern_selection_clear_row,
                   &scan);

    for(guint index = 0;
        index < scan.empty_rows->len;
        index++)
    {
        g_tree_remove(
            cell->rows,
            g_ptr_array_index(scan.empty_rows, index));
    }

    g_ptr_array_free(scan.empty_rows, TRUE);

    if(scan.changed)
        gvr_pattern_emit_changed(view,
                                 view->selected_bank,
                                 view->selected_slot);

    return scan.changed;
}

static gboolean gvr_pattern_cut_block(GvrVimsPatternView *view)
{
    if(!gvr_pattern_copy_block(view))
        return FALSE;

    return gvr_pattern_clear_block(view);
}

static gboolean gvr_pattern_paste_block_mode(
        GvrVimsPatternView *view,
        int paste_mode)
{
    GvrVimsPatternCell *cell;
    const int start_frame = gvr_pattern_current_frame(view);
    gboolean changed = FALSE;

    if(!gvr_pattern_target_editable(view) ||
       !view->block_clipboard.events)
        return FALSE;

    paste_mode = gvr_pattern_clampi(
        paste_mode,
        GVR_PATTERN_PASTE_REPLACE,
        GVR_PATTERN_PASTE_INSERT);

    if(!gvr_pattern_push_undo(view))
        return FALSE;

    if(paste_mode == GVR_PATTERN_PASTE_INSERT) {
        const gint64 amount64 =
            (gint64)view->block_clipboard.rows *
            view->block_clipboard.frame_step;
        const int amount =
            (int)MIN(amount64,
                     (gint64)GVR_PATTERN_MAX_FRAMES);

        if(amount > 0 &&
           gvr_pattern_insert_gap_no_undo(view,
                                          start_frame,
                                          amount))
            changed = TRUE;
    }

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);

    for(int row_index = 0;
        row_index < view->block_clipboard.rows;
        row_index++)
    {
        const gint64 frame64 =
            (gint64)start_frame +
            (gint64)row_index *
            view->block_clipboard.frame_step;
        GvrVimsPatternRow *row;

        if(frame64 < 0 || frame64 >= GVR_PATTERN_MAX_FRAMES)
            continue;
        if(view->frame_count_known &&
           frame64 >= view->frame_count)
            continue;

        const int frame = (int)frame64;
        row = gvr_pattern_row_get(cell, frame, FALSE);

        for(int column_index = 0;
            column_index < view->block_clipboard.columns;
            column_index++)
        {
            const int column =
                view->selected_column + column_index;
            GvrVimsPatternEvent *src;
            GvrVimsPatternEvent *dst;

            if(column >= GVR_VIMS_PATTERN_COLUMNS)
                break;

            src = gvr_pattern_block_event(
                &view->block_clipboard,
                row_index,
                column_index);

            if(src && src->message) {
                if(!row)
                    row = gvr_pattern_row_get(
                        cell,
                        frame,
                        TRUE);
                dst = &row->events[column];
                gvr_pattern_event_copy(dst, src);
                changed = TRUE;
            }
            else if(paste_mode == GVR_PATTERN_PASTE_REPLACE &&
                    row &&
                    row->events[column].message)
            {
                gvr_pattern_event_clear(
                    &row->events[column]);
                changed = TRUE;
            }
        }

        if(row && gvr_pattern_row_empty(row))
            g_tree_remove(cell->rows,
                          GINT_TO_POINTER(frame + 1));

        if(!view->frame_count_known &&
           frame + 1 > view->frame_count)
            view->frame_count = frame + 1;
    }

    if(changed) {
        gvr_pattern_emit_changed(
            view,
            view->selected_bank,
            view->selected_slot);
        gvr_pattern_selection_clear(view);
        gvr_pattern_set_cursor(
            view,
            start_frame +
            (view->block_clipboard.rows - 1) *
            view->block_clipboard.frame_step,
            MIN(GVR_VIMS_PATTERN_COLUMNS - 1,
                view->selected_column +
                view->block_clipboard.columns - 1));
        gvr_pattern_sync_loop_controls(view);
    }

    return changed;
}

static gboolean gvr_pattern_paste_block(
        GvrVimsPatternView *view)
{
    return gvr_pattern_paste_block_mode(
        view,
        view ? view->paste_mode :
               GVR_PATTERN_PASTE_REPLACE);
}

static gboolean gvr_pattern_paste_clipboard_command(
        GvrVimsPatternView *view)
{
    GtkClipboard *clipboard;
    gchar *text;
    gchar *normalized;
    gboolean pasted = FALSE;

    if(!view || !gvr_pattern_target_editable(view))
        return FALSE;

    clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    text = gtk_clipboard_wait_for_text(clipboard);
    if(!text)
        return FALSE;

    normalized = gvr_pattern_normalize_message(text);
    if(normalized && gvr_pattern_parse_id(normalized) >= 0)
        pasted = gvr_vims_pattern_view_insert_message(
            GTK_WIDGET(view),
            normalized,
            -1,
            -1,
            FALSE);

    g_free(normalized);
    g_free(text);
    return pasted;
}

static gboolean gvr_pattern_paste(
        GvrVimsPatternView *view)
{
    if(gvr_pattern_paste_clipboard_command(view))
        return TRUE;

    return gvr_pattern_paste_block(view);
}


typedef struct {
    GTree *rows;
    int start_frame;
    int step;
    int frame_limit;
    int maximum_frame;
    gboolean delete_row;
} GvrPatternShiftContext;

static void gvr_pattern_clone_row_to_tree(GTree *rows,
                                          GvrVimsPatternRow *source,
                                          int frame)
{
    GvrVimsPatternRow *copy;

    if(!rows || !source || frame < 0 || frame >= GVR_PATTERN_MAX_FRAMES)
        return;

    copy = g_new0(GvrVimsPatternRow, 1);
    copy->frame = frame;

    for(int column = 0; column < GVR_VIMS_PATTERN_COLUMNS; column++)
        gvr_pattern_event_copy(&copy->events[column],
                               &source->events[column]);

    g_tree_replace(rows, GINT_TO_POINTER(frame + 1), copy);
}

static gboolean gvr_pattern_shift_row(gpointer key,
                                      gpointer value,
                                      gpointer data)
{
    GvrPatternShiftContext *context = data;
    GvrVimsPatternRow *row = value;
    int frame = GPOINTER_TO_INT(key) - 1;
    int destination = frame;

    if(context->delete_row) {
        if(frame >= context->start_frame &&
           frame < context->start_frame + context->step)
            return FALSE;

        if(frame >= context->start_frame + context->step)
            destination = frame - context->step;
    }
    else if(frame >= context->start_frame) {
        destination = frame + context->step;
    }

    if(destination >= 0 && destination < context->frame_limit) {
        gvr_pattern_clone_row_to_tree(context->rows, row, destination);
        context->maximum_frame =
            MAX(context->maximum_frame, destination);
    }

    return FALSE;
}

static gboolean gvr_pattern_insert_gap_no_undo(
        GvrVimsPatternView *view,
        int start_frame,
        int amount)
{
    GvrVimsPatternCell *cell;
    GvrPatternShiftContext context;

    if(!view || amount <= 0)
        return FALSE;

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);
    if(!cell)
        return FALSE;

    context.rows = g_tree_new_full(
        (GCompareDataFunc)gvr_pattern_int_compare,
        NULL,
        NULL,
        gvr_pattern_row_free);
    context.start_frame = start_frame;
    context.step = amount;
    context.frame_limit = view->frame_count_known ?
                          view->frame_count :
                          GVR_PATTERN_MAX_FRAMES;
    context.maximum_frame = -1;
    context.delete_row = FALSE;

    if(cell->rows)
        g_tree_foreach(cell->rows,
                       gvr_pattern_shift_row,
                       &context);

    if(cell->rows)
        g_tree_destroy(cell->rows);
    cell->rows = context.rows;

    if(gvr_pattern_loop_valid(cell)) {
        if(cell->loop_start >= start_frame)
            cell->loop_start += amount;
        if(cell->loop_end >= start_frame)
            cell->loop_end += amount;
    }

    gvr_pattern_loop_clamp(cell, context.frame_limit);

    if(!view->frame_count_known)
        view->frame_count = MAX(
            GVR_PATTERN_DEFAULT_FRAMES,
            context.maximum_frame + 1);

    return TRUE;
}


static gboolean gvr_pattern_shift_current_row(GvrVimsPatternView *view,
                                              gboolean delete_row)
{
    GvrVimsPatternCell *cell;
    GvrPatternShiftContext context;

    if(!gvr_pattern_target_editable(view) ||
       !gvr_pattern_push_undo(view))
        return FALSE;

    cell = gvr_pattern_cell(view, view->selected_bank, view->selected_slot);
    if(!cell)
        return FALSE;

    context.rows = g_tree_new_full(
        (GCompareDataFunc)gvr_pattern_int_compare,
        NULL,
        NULL,
        gvr_pattern_row_free);
    context.start_frame = gvr_pattern_current_frame(view);
    context.step = gvr_pattern_edit_step(view);
    context.frame_limit = view->frame_count_known ?
                          view->frame_count :
                          GVR_PATTERN_MAX_FRAMES;
    context.maximum_frame = -1;
    context.delete_row = delete_row;

    if(cell->rows)
        g_tree_foreach(cell->rows, gvr_pattern_shift_row, &context);

    if(cell->rows)
        g_tree_destroy(cell->rows);
    cell->rows = context.rows;

    if(gvr_pattern_loop_valid(cell)) {
        if(delete_row) {
            if(cell->loop_start >= context.start_frame + context.step)
                cell->loop_start -= context.step;
            else if(cell->loop_start >= context.start_frame)
                cell->loop_start = context.start_frame;

            if(cell->loop_end >= context.start_frame + context.step)
                cell->loop_end -= context.step;
            else if(cell->loop_end >= context.start_frame)
                cell->loop_end = context.start_frame - 1;

            if(cell->loop_end < cell->loop_start) {
                cell->loop_enabled = FALSE;
                cell->loop_start = -1;
                cell->loop_end = -1;
            }
        }
        else {
            if(cell->loop_start >= context.start_frame)
                cell->loop_start += context.step;
            if(cell->loop_end >= context.start_frame)
                cell->loop_end += context.step;
        }
    }

    gvr_pattern_loop_clamp(cell, context.frame_limit);

    if(!view->frame_count_known)
        view->frame_count = MAX(GVR_PATTERN_DEFAULT_FRAMES,
                                context.maximum_frame + 1);

    gvr_pattern_emit_changed(view,
                             view->selected_bank,
                             view->selected_slot);
    gvr_pattern_selection_clear(view);
    gvr_pattern_sync_loop_controls(view);
    gtk_widget_queue_draw(view->area);
    return TRUE;
}

static gboolean gvr_pattern_insert_row(GvrVimsPatternView *view)
{
    return gvr_pattern_shift_current_row(view, FALSE);
}

static gboolean gvr_pattern_delete_row_shift(GvrVimsPatternView *view)
{
    return gvr_pattern_shift_current_row(view, TRUE);
}

typedef struct {
    int rows;
    int events;
    int first_frame;
} GvrPatternOffgridInfo;

static GvrPatternOffgridInfo gvr_pattern_offgrid_info(
        GvrVimsPatternCell *cell,
        int frame,
        int next_frame)
{
    GvrPatternOffgridInfo info = { 0, 0, -1 };

    if(!cell || !cell->rows || next_frame <= frame + 1)
        return info;

    for(int hidden_frame = frame + 1;
        hidden_frame < next_frame;
        hidden_frame++)
    {
        GvrVimsPatternRow *row =
            gvr_pattern_row_get(cell, hidden_frame, FALSE);
        gboolean occupied = FALSE;

        if(!row)
            continue;

        for(int column = 0;
            column < GVR_VIMS_PATTERN_COLUMNS;
            column++)
        {
            if(row->events[column].message) {
                occupied = TRUE;
                info.events++;
            }
        }

        if(occupied) {
            if(info.first_frame < 0)
                info.first_frame = hidden_frame;
            info.rows++;
        }
    }

    return info;
}

static void gvr_pattern_offgrid_range(
        GvrVimsPatternView *view,
        int *first_row,
        int *last_row,
        int *start_frame,
        int *end_frame)
{
    int first;
    int last;
    const int step = gvr_pattern_edit_step(view);
    const int count = gvr_pattern_effective_frame_count(view);

    if(view->selection_active) {
        int first_column;
        int last_column;

        gvr_pattern_selection_bounds(
            view,
            &first,
            &last,
            &first_column,
            &last_column);
    }
    else {
        first = view->selected_row;
        last = view->selected_row;
    }

    first = MAX(0, first);
    last = MAX(first, last);

    if(first_row)
        *first_row = first;
    if(last_row)
        *last_row = last;
    if(start_frame)
        *start_frame = MIN(count, first * step);
    if(end_frame)
        *end_frame = MIN(count, (last + 1) * step);
}

static GvrPatternOffgridInfo
gvr_pattern_selected_offgrid_info(
        GvrVimsPatternView *view)
{
    GvrPatternOffgridInfo info = { 0, 0, -1 };
    GvrVimsPatternCell *cell;
    int start_frame;
    int end_frame;
    const int step = gvr_pattern_edit_step(view);

    if(!view || step <= 1)
        return info;

    cell = gvr_pattern_cell(
        view,
        view->selected_bank,
        view->selected_slot);
    if(!cell || !cell->rows)
        return info;

    gvr_pattern_offgrid_range(
        view,
        NULL,
        NULL,
        &start_frame,
        &end_frame);

    for(int frame = start_frame;
        frame < end_frame;
        frame++)
    {
        GvrVimsPatternRow *row;
        gboolean occupied = FALSE;

        if((frame % step) == 0)
            continue;

        row = gvr_pattern_row_get(
            cell,
            frame,
            FALSE);
        if(!row)
            continue;

        for(int column = 0;
            column < GVR_VIMS_PATTERN_COLUMNS;
            column++)
        {
            if(row->events[column].message) {
                occupied = TRUE;
                info.events++;
            }
        }

        if(occupied) {
            if(info.first_frame < 0)
                info.first_frame = frame;
            info.rows++;
        }
    }

    return info;
}

static gboolean gvr_pattern_reveal_offgrid(
        GvrVimsPatternView *view)
{
    GvrPatternOffgridInfo info =
        gvr_pattern_selected_offgrid_info(view);

    if(info.first_frame < 0)
        return FALSE;

    gvr_vims_pattern_view_set_edit_step(
        GTK_WIDGET(view),
        1);
    gvr_pattern_set_cursor(
        view,
        info.first_frame,
        view->selected_column);
    return TRUE;
}

typedef struct {
    GvrVimsPatternView *view;
    GTree *rows;
    int start_frame;
    int end_frame;
    int first_grid_frame;
    int last_grid_frame;
    int step;
    int moved;
    int conflicts;
} GvrPatternQuantizeContext;

static gboolean gvr_pattern_quantize_copy_fixed(
        gpointer key,
        gpointer value,
        gpointer data)
{
    GvrPatternQuantizeContext *context = data;
    GvrVimsPatternRow *row = value;
    const int frame = GPOINTER_TO_INT(key) - 1;

    if(frame < context->start_frame ||
       frame >= context->end_frame ||
       (frame % context->step) == 0)
    {
        gvr_pattern_clone_row_to_tree(
            context->rows,
            row,
            frame);
    }

    return FALSE;
}

static int gvr_pattern_quantize_free_column(
        GvrVimsPatternRow *row,
        int preferred)
{
    if(!row)
        return preferred;

    for(int offset = 1;
        offset < GVR_VIMS_PATTERN_COLUMNS;
        offset++)
    {
        const int column =
            (preferred + offset) %
            GVR_VIMS_PATTERN_COLUMNS;

        if(!row->events[column].message)
            return column;
    }

    return -1;
}

static gboolean gvr_pattern_quantize_move_hidden(
        gpointer key,
        gpointer value,
        gpointer data)
{
    GvrPatternQuantizeContext *context = data;
    GvrVimsPatternRow *source = value;
    const int frame = GPOINTER_TO_INT(key) - 1;
    int destination;

    if(frame < context->start_frame ||
       frame >= context->end_frame ||
       (frame % context->step) == 0)
        return FALSE;

    destination =
        ((frame + context->step / 2) /
         context->step) *
        context->step;
    destination =
        gvr_pattern_clampi(
            destination,
            context->first_grid_frame,
            context->last_grid_frame);

    for(int column = 0;
        column < GVR_VIMS_PATTERN_COLUMNS;
        column++)
    {
        GvrVimsPatternEvent *event =
            &source->events[column];
        GvrVimsPatternRow *target;
        int target_column = column;

        if(!event->message)
            continue;

        target = gvr_pattern_row_get(
            &(GvrVimsPatternCell){ .rows = context->rows },
            destination,
            FALSE);

        if(!target) {
            target = g_new0(GvrVimsPatternRow, 1);
            target->frame = destination;
            g_tree_insert(
                context->rows,
                GINT_TO_POINTER(destination + 1),
                target);
        }

        if(target->events[target_column].message) {
            if(target->events[target_column].vims_id ==
               event->vims_id)
            {
                gvr_pattern_event_copy(
                    &target->events[target_column],
                    event);
                context->moved++;
                continue;
            }

            target_column =
                gvr_pattern_quantize_free_column(
                    target,
                    column);
        }

        if(target_column >= 0) {
            gvr_pattern_event_copy(
                &target->events[target_column],
                event);
            context->moved++;
        }
        else {
            GvrVimsPatternRow *original =
                gvr_pattern_row_get(
                    &(GvrVimsPatternCell){
                        .rows = context->rows
                    },
                    frame,
                    FALSE);

            if(!original) {
                original =
                    g_new0(GvrVimsPatternRow, 1);
                original->frame = frame;
                g_tree_insert(
                    context->rows,
                    GINT_TO_POINTER(frame + 1),
                    original);
            }

            gvr_pattern_event_copy(
                &original->events[column],
                event);
            context->conflicts++;
        }
    }

    return FALSE;
}

static gboolean gvr_pattern_quantize_offgrid(
        GvrVimsPatternView *view)
{
    GvrPatternOffgridInfo info;
    GvrPatternQuantizeContext context;
    GvrVimsPatternCell *cell;
    int first_row;
    int last_row;

    info = gvr_pattern_selected_offgrid_info(view);
    if(info.events <= 0)
        return FALSE;

    if(!gvr_pattern_push_undo(view))
        return FALSE;

    cell = gvr_pattern_cell(
        view,
        view->selected_bank,
        view->selected_slot);
    if(!cell)
        return FALSE;

    memset(&context, 0, sizeof(context));
    context.view = view;
    context.rows = g_tree_new_full(
        (GCompareDataFunc)gvr_pattern_int_compare,
        NULL,
        NULL,
        gvr_pattern_row_free);
    context.step = gvr_pattern_edit_step(view);

    gvr_pattern_offgrid_range(
        view,
        &first_row,
        &last_row,
        &context.start_frame,
        &context.end_frame);

    context.first_grid_frame =
        first_row * context.step;
    context.last_grid_frame =
        last_row * context.step;

    if(cell->rows) {
        g_tree_foreach(
            cell->rows,
            gvr_pattern_quantize_copy_fixed,
            &context);
        g_tree_foreach(
            cell->rows,
            gvr_pattern_quantize_move_hidden,
            &context);
        g_tree_destroy(cell->rows);
    }

    cell->rows = context.rows;

    if(context.moved > 0) {
        gvr_pattern_emit_changed(
            view,
            view->selected_bank,
            view->selected_slot);
        gtk_widget_queue_draw(view->area);
        return TRUE;
    }

    return FALSE;
}


static gboolean gvr_pattern_transport_enabled(GvrVimsPatternView *view)
{
    return gvr_pattern_target_valid(view->selected_bank, view->selected_slot) &&
           view->selected_sample_id > 0 &&
           !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->learn_toggle)) &&
           !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->follow_toggle));
}

static void gvr_pattern_request_transport(GvrVimsPatternView *view,
                                          int frame,
                                          guint flags)
{
    const int count = gvr_pattern_effective_frame_count(view);

    if(count <= 0 || !gvr_pattern_transport_enabled(view))
        return;

    g_signal_emit(view,
                  gvr_vims_pattern_view_signals[SIGNAL_TRANSPORT_REQUEST],
                  0,
                  view->selected_bank,
                  view->selected_slot,
                  view->selected_sample_id,
                  gvr_pattern_clampi(frame, 0, count - 1),
                  flags);
}

static void gvr_pattern_move_row(GvrVimsPatternView *view,
                                 int row,
                                 int column,
                                 gboolean seek)
{
    gvr_pattern_set_cursor_row(view, row, column);

    if(seek)
        gvr_pattern_request_transport(view,
                                      gvr_pattern_current_frame(view),
                                      0);
}

static void gvr_pattern_update_target_label(GvrVimsPatternView *view)
{
    const char *badge = NULL;
    char text[160];
    int outside_events = 0;

    if(view->frame_count_known) {
        GvrVimsPatternCell *cell = gvr_pattern_cell(view,
                                                    view->selected_bank,
                                                    view->selected_slot);
        if(cell && cell->rows) {
            GvrPatternOutsideRange outside = { view->frame_count, 0 };
            g_tree_foreach(cell->rows, gvr_pattern_count_outside_range, &outside);
            outside_events = outside.events;
        }
    }

    if(!gvr_pattern_target_valid(view->selected_bank, view->selected_slot)) {
        char empty[96];

        gtk_widget_hide(view->target_badge);
        g_snprintf(empty,
                   sizeof(empty),
                   "No VIMS pattern target   0 frames   rows ×%d",
                   gvr_pattern_edit_step(view));
        gtk_label_set_text(GTK_LABEL(view->target_label), empty);
        return;
    }

    if(view->selected_bank == GVR_VIMS_PATTERN_SEQUENCE_BANK) {
        badge = "BANK PATTERN";

        if(!view->frame_count_known)
            g_snprintf(text,
                       sizeof(text),
                       "Sequence Bank %d   length unavailable   %d-row workspace",
                       view->selected_slot + 1,
                       gvr_pattern_effective_frame_count(view));
        else if(outside_events > 0)
            g_snprintf(text,
                       sizeof(text),
                       "Sequence Bank %d   %d frames   %d events outside range",
                       view->selected_slot + 1,
                       gvr_pattern_effective_frame_count(view),
                       outside_events);
        else
            g_snprintf(text,
                       sizeof(text),
                       "Sequence Bank %d   %d frames",
                       view->selected_slot + 1,
                       gvr_pattern_effective_frame_count(view));
    }
    else if(view->selected_bank == GVR_VIMS_PATTERN_SAMPLE_BANK ||
            view->selected_bank == GVR_VIMS_PATTERN_STREAM_BANK) {
        const char *kind = view->selected_bank == GVR_VIMS_PATTERN_SAMPLE_BANK ?
                           "Sample" : "Stream";

        badge = "PATTERN";

        if(!view->frame_count_known)
            g_snprintf(text,
                       sizeof(text),
                       "%s %d   length unknown   %d-row workspace",
                       kind,
                       view->selected_sample_id,
                       gvr_pattern_effective_frame_count(view));
        else if(outside_events > 0)
            g_snprintf(text,
                       sizeof(text),
                       "%s %d   %d frames   %d events outside range",
                       kind,
                       view->selected_sample_id,
                       gvr_pattern_effective_frame_count(view),
                       outside_events);
        else
            g_snprintf(text,
                       sizeof(text),
                       "%s %d   %d frames",
                       kind,
                       view->selected_sample_id,
                       gvr_pattern_effective_frame_count(view));
    }
    else {
        badge = "SLOT PATTERN";

        if(view->selected_sample_id > 0) {
            if(!view->frame_count_known)
                g_snprintf(text,
                           sizeof(text),
                           "B%d / %02d   %s %d   length unknown   %d-row workspace",
                           view->selected_bank + 1,
                           view->selected_slot + 1,
                           view->selected_sample_type == 0 ? "Sample" : "Stream",
                           view->selected_sample_id,
                           gvr_pattern_effective_frame_count(view));
            else if(outside_events > 0)
                g_snprintf(text,
                           sizeof(text),
                           "B%d / %02d   %s %d   %d frames   %d events outside range",
                           view->selected_bank + 1,
                           view->selected_slot + 1,
                           view->selected_sample_type == 0 ? "Sample" : "Stream",
                           view->selected_sample_id,
                           gvr_pattern_effective_frame_count(view),
                           outside_events);
            else
                g_snprintf(text,
                           sizeof(text),
                           "B%d / %02d   %s %d   %d frames",
                           view->selected_bank + 1,
                           view->selected_slot + 1,
                           view->selected_sample_type == 0 ? "Sample" : "Stream",
                           view->selected_sample_id,
                           gvr_pattern_effective_frame_count(view));
        }
        else {
            g_snprintf(text,
                       sizeof(text),
                       "B%d / %02d   empty",
                       view->selected_bank + 1,
                       view->selected_slot + 1);
        }
    }

    gtk_label_set_text(GTK_LABEL(view->target_badge), badge);
    gtk_widget_show(view->target_badge);

    {
        GvrVimsPatternCell *cell =
            gvr_pattern_cell_lookup(view,
                                    view->selected_bank,
                                    view->selected_slot);
        char stepped[256];

        if(gvr_pattern_loop_valid(cell))
            g_snprintf(stepped,
                       sizeof(stepped),
                       "%s   rows ×%d   loop %d–%d   Learn: %s",
                       text,
                       gvr_pattern_edit_step(view),
                       cell->loop_start,
                       cell->loop_end,
                       gvr_pattern_learn_policy_name(view->learn_policy));
        else
            g_snprintf(stepped,
                       sizeof(stepped),
                       "%s   rows ×%d   Learn: %s",
                       text,
                       gvr_pattern_edit_step(view),
                       gvr_pattern_learn_policy_name(view->learn_policy));

        gtk_label_set_text(GTK_LABEL(view->target_label), stepped);
    }
}

static void gvr_pattern_draw_text(GtkWidget *widget,
                                  cairo_t *cr,
                                  const char *text,
                                  double x,
                                  double y,
                                  double scale,
                                  cairo_font_weight_t weight)
{
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font;
    int baseline;

    font = gvr_pattern_font_description(
        widget,
        scale,
        weight == CAIRO_FONT_WEIGHT_BOLD ?
        PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text ? text : "", -1);
    pango_layout_set_single_paragraph_mode(layout, TRUE);

    baseline = pango_layout_get_baseline(layout);
    cairo_move_to(cr, x, y - ((double)baseline / PANGO_SCALE));
    pango_cairo_show_layout(cr, layout);

    pango_font_description_free(font);
    g_object_unref(layout);
}

static void gvr_pattern_draw_cell_text(GtkWidget *widget,
                                       cairo_t *cr,
                                       const char *text,
                                       double x,
                                       double baseline_y,
                                       double width,
                                       double scale)
{
    char buffer[40];
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font;
    PangoRectangle extents;
    gsize length;
    int baseline;

    g_strlcpy(buffer, text ? text : "", sizeof(buffer));
    length = strlen(buffer);

    font = gvr_pattern_font_description(widget,
                                        scale,
                                        PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    pango_layout_set_text(layout, buffer, -1);
    pango_layout_get_pixel_extents(layout, NULL, &extents);

    while(length > 1 && extents.width > width - 8.0) {
        buffer[--length] = '\0';
        pango_layout_set_text(layout, buffer, -1);
        pango_layout_get_pixel_extents(layout, NULL, &extents);
    }

    baseline = pango_layout_get_baseline(layout);
    cairo_move_to(cr,
                  x + 4.0,
                  baseline_y - ((double)baseline / PANGO_SCALE));
    pango_cairo_show_layout(cr, layout);

    pango_font_description_free(font);
    g_object_unref(layout);
}

static gboolean gvr_vims_pattern_view_draw(GtkWidget *widget,
                                                cairo_t *cr,
                                                gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);
    GtkAllocation allocation;
    GvrVimsPatternCell *cell;
    int top_row;
    int rows;
    int display_rows;
    int frame_count;
    int step;
    double event_width;
    int row_height;
    double font_px;
    double baseline_offset;

    if(!view)
        return FALSE;

    gtk_widget_get_allocation(widget, &allocation);
    rows = gvr_pattern_visible_rows(view);
    row_height = gvr_pattern_row_height(view);
    font_px = gvr_pattern_font_px(view, 1.0);
    baseline_offset = (row_height + font_px * 0.65) * 0.5;
    display_rows = gvr_pattern_display_row_count(view);
    frame_count = gvr_pattern_effective_frame_count(view);
    step = gvr_pattern_edit_step(view);
    top_row = (int)gtk_adjustment_get_value(view->vadjustment);
    event_width = MAX(34.0,
                      ((double)allocation.width - GVR_PATTERN_FRAME_WIDTH) /
                      GVR_VIMS_PATTERN_COLUMNS);
    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);

    cairo_set_source_rgb(cr, 0.050, 0.054, 0.064);
    cairo_paint(cr);

    for(int row_index = 0; row_index < rows + 1; row_index++) {
        const int display_row = top_row + row_index;
        const int frame =
            gvr_pattern_display_row_to_frame(view, display_row);
        const int next_frame =
            MIN(frame_count, frame + step);
        double y = GVR_PATTERN_HEADER_HEIGHT +
                   row_index * row_height;
        gboolean selected_row =
            display_row == view->selected_row;
        gboolean live_row =
            view->live_active &&
            view->live_bank == view->selected_bank &&
            view->live_slot == view->selected_slot &&
            gvr_pattern_frame_to_display_row(
                view,
                view->live_frame) ==
            display_row;
        GvrVimsPatternRow *pattern_row =
            cell ? gvr_pattern_row_get(cell, frame, FALSE) : NULL;
        char frame_text[16];

        if(y >= allocation.height || display_row >= display_rows)
            break;

        if(gvr_pattern_loop_valid(cell) &&
           next_frame > cell->loop_start &&
           frame <= cell->loop_end)
        {
            cairo_set_source_rgba(cr, 0.20, 0.62, 0.42, 0.10);
            cairo_rectangle(cr,
                            0,
                            y,
                            allocation.width,
                            row_height);
            cairo_fill(cr);
        }

        if(live_row) {
            cairo_set_source_rgba(cr, 1.0, 0.52, 0.0, 0.20);
            cairo_rectangle(cr,
                            0,
                            y,
                            allocation.width,
                            row_height);
            cairo_fill(cr);
        }
        else if(selected_row) {
            cairo_set_source_rgba(cr, 0.36, 0.43, 0.55, 0.28);
            cairo_rectangle(cr,
                            0,
                            y,
                            allocation.width,
                            row_height);
            cairo_fill(cr);
        }
        else if((display_row & 3) == 0) {
            cairo_set_source_rgba(cr, 0.12, 0.13, 0.15, 0.35);
            cairo_rectangle(cr,
                            0,
                            y,
                            allocation.width,
                            row_height);
            cairo_fill(cr);
        }

        g_snprintf(frame_text, sizeof(frame_text), "%06d", frame);
        cairo_set_source_rgb(cr,
                             live_row ? 1.0 : 0.62,
                             live_row ? 0.72 : 0.66,
                             live_row ? 0.28 : 0.72);
        gvr_pattern_draw_text(
            widget,
            cr,
            frame_text,
            8,
            y + baseline_offset,
            1.0,
            selected_row ?
            CAIRO_FONT_WEIGHT_BOLD :
            CAIRO_FONT_WEIGHT_NORMAL);

        if(step > 1) {
            GvrPatternOffgridInfo hidden =
                gvr_pattern_offgrid_info(cell, frame, next_frame);

            if(hidden.events > 0) {
                char hidden_text[8];

                if(hidden.events > 99)
                    g_strlcpy(hidden_text, "·99+", sizeof(hidden_text));
                else
                    g_snprintf(hidden_text,
                               sizeof(hidden_text),
                               "·%d",
                               hidden.events);

                cairo_set_source_rgb(cr, 1.0, 0.68, 0.20);
                gvr_pattern_draw_text(widget,
                                      cr,
                                      hidden_text,
                                      GVR_PATTERN_FRAME_WIDTH - 30.0,
                                      y + baseline_offset,
                                      0.8,
                                      CAIRO_FONT_WEIGHT_BOLD);
            }
        }

        if(gvr_pattern_loop_valid(cell)) {
            if(frame == cell->loop_start) {
                cairo_set_source_rgba(cr, 0.36, 0.93, 0.74, 0.95);
                cairo_set_line_width(cr, 1.5);
                cairo_move_to(cr, 0, y + 0.5);
                cairo_line_to(cr, allocation.width, y + 0.5);
                cairo_stroke(cr);
            }

            if(cell->loop_end >= frame &&
               cell->loop_end < next_frame)
            {
                cairo_set_source_rgba(cr, 0.36, 0.93, 0.74, 0.95);
                cairo_set_line_width(cr, 1.5);
                cairo_move_to(cr,
                              0,
                              y + row_height - 0.5);
                cairo_line_to(cr,
                              allocation.width,
                              y + row_height - 0.5);
                cairo_stroke(cr);
            }
        }

        for(int column = 0;
            column < GVR_VIMS_PATTERN_COLUMNS;
            column++)
        {
            double x =
                GVR_PATTERN_FRAME_WIDTH + column * event_width;
            GvrVimsPatternEvent *event =
                pattern_row ? &pattern_row->events[column] : NULL;
            gboolean enabled =
                (view->enabled_columns_mask &
                 (1u << column)) != 0;
            gboolean block_selected =
                gvr_pattern_selection_contains(view,
                                               display_row,
                                               column);

            if(!enabled) {
                cairo_set_source_rgba(cr,
                                      0.015,
                                      0.017,
                                      0.022,
                                      0.52);
                cairo_rectangle(cr,
                                x,
                                y,
                                event_width,
                                row_height);
                cairo_fill(cr);
            }

            if(block_selected) {
                cairo_set_source_rgba(cr,
                                      0.28,
                                      0.48,
                                      0.78,
                                      0.30);
                cairo_rectangle(cr,
                                x + 1,
                                y + 1,
                                event_width - 2,
                                row_height - 2);
                cairo_fill(cr);
            }

            if(selected_row &&
               column == view->selected_column)
            {
                cairo_set_source_rgba(cr,
                                      0.72,
                                      0.78,
                                      0.90,
                                      0.30);
                cairo_rectangle(cr,
                                x + 1,
                                y + 1,
                                event_width - 2,
                                row_height - 2);
                cairo_fill(cr);
                cairo_set_source_rgba(cr,
                                      0.90,
                                      0.94,
                                      1.0,
                                      0.95);
                cairo_set_line_width(cr, 1.0);
                cairo_rectangle(cr,
                                x + 1.5,
                                y + 1.5,
                                event_width - 3,
                                row_height - 3);
                cairo_stroke(cr);
            }
            else if(block_selected) {
                cairo_set_source_rgba(cr,
                                      0.55,
                                      0.72,
                                      0.98,
                                      0.85);
                cairo_set_line_width(cr, 1.0);
                cairo_rectangle(cr,
                                x + 1.5,
                                y + 1.5,
                                event_width - 3,
                                row_height - 3);
                cairo_stroke(cr);
            }

            if(event && event->message) {
                if(!enabled)
                    cairo_set_source_rgb(cr, 0.34, 0.37, 0.43);
                else if(event->vims_id == VIMS_SAMPLE_HOLD_FRAME)
                    cairo_set_source_rgb(cr, 1.0, 0.68, 0.20);
                else
                    cairo_set_source_rgb(cr, 0.62, 0.82, 1.0);

                gvr_pattern_draw_cell_text(widget,
                                           cr,
                                           event->label,
                                           x,
                                           y + baseline_offset,
                                           event_width,
                                           1.0);
            }

            if(event && event->message &&
               view->selected_bank == view->last_fired_bank &&
               view->selected_slot == view->last_fired_slot &&
               frame == view->last_fired_frame &&
               (view->last_fired_mask & (1u << column)))
            {
                cairo_set_source_rgba(cr, 0.36, 0.93, 0.74, 0.96);
                cairo_set_line_width(cr, 2.0);
                cairo_rectangle(cr,
                                x + 2.0,
                                y + 2.0,
                                event_width - 4.0,
                                row_height - 4.0);
                cairo_stroke(cr);
            }
        }
    }

    cairo_set_source_rgba(cr, 0.18, 0.20, 0.24, 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, GVR_PATTERN_FRAME_WIDTH + 0.5, 0);
    cairo_line_to(cr,
                  GVR_PATTERN_FRAME_WIDTH + 0.5,
                  allocation.height);
    cairo_stroke(cr);

    for(int column = 1;
        column < GVR_VIMS_PATTERN_COLUMNS;
        column++)
    {
        double x =
            GVR_PATTERN_FRAME_WIDTH +
            column * event_width + 0.5;
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x, allocation.height);
    }
    cairo_stroke(cr);

    const int grid_rows =
        MIN(rows + 1, MAX(0, display_rows - top_row));

    for(int row_index = 0;
        row_index <= grid_rows;
        row_index++)
    {
        double y =
            GVR_PATTERN_HEADER_HEIGHT +
            row_index * row_height + 0.5;
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, allocation.width, y);
    }
    cairo_stroke(cr);

    return FALSE;
}

static void gvr_pattern_area_size_allocate(GtkWidget *widget,
                                           GtkAllocation *allocation,
                                           gpointer user_data)
{
    (void)widget;
    (void)allocation;
    gvr_pattern_update_adjustment(GVR_VIMS_PATTERN_VIEW(user_data));
}

static void gvr_pattern_adjustment_changed(GtkAdjustment *adjustment, gpointer user_data)
{
    (void)adjustment;
    gtk_widget_queue_draw(GVR_VIMS_PATTERN_VIEW(user_data)->area);
}

static gboolean gvr_pattern_area_scroll(GtkWidget *widget,
                                        GdkEventScroll *event,
                                        gpointer user_data)
{
    (void)widget;
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);
    double value = gtk_adjustment_get_value(view->vadjustment);
    double delta = 0.0;

    if(event->direction == GDK_SCROLL_UP)
        delta = -3.0;
    else if(event->direction == GDK_SCROLL_DOWN)
        delta = 3.0;
#if GTK_CHECK_VERSION(3,4,0)
    else if(event->direction == GDK_SCROLL_SMOOTH)
        delta = event->delta_y * 3.0;
#endif

    if(delta == 0.0)
        return FALSE;

    gtk_adjustment_set_value(view->vadjustment, value + delta);
    return TRUE;
}

static gboolean gvr_pattern_position_from_xy(GvrVimsPatternView *view,
                                             double x,
                                             double y,
                                             int *frame,
                                             int *column)
{
    GtkAllocation allocation;
    double event_width;
    int top_row;
    int display_row;

    gtk_widget_get_allocation(view->area, &allocation);
    if(y < GVR_PATTERN_HEADER_HEIGHT || x < 0.0 || x >= allocation.width)
        return FALSE;

    top_row = (int)gtk_adjustment_get_value(view->vadjustment);
    display_row = top_row +
                  (int)((y - GVR_PATTERN_HEADER_HEIGHT) /
                        gvr_pattern_row_height(view));
    *frame = gvr_pattern_display_row_to_frame(view, display_row);

    if(x < GVR_PATTERN_FRAME_WIDTH) {
        *column = view->selected_column;
    }
    else {
        event_width = MAX(34.0,
                          ((double)allocation.width - GVR_PATTERN_FRAME_WIDTH) /
                          GVR_VIMS_PATTERN_COLUMNS);
        if(x >= GVR_PATTERN_FRAME_WIDTH + event_width * GVR_VIMS_PATTERN_COLUMNS)
            return FALSE;
        *column = (int)((x - GVR_PATTERN_FRAME_WIDTH) / event_width);
    }

    return display_row >= 0 &&
           display_row < gvr_pattern_display_row_count(view) &&
           *column >= 0 &&
           *column < GVR_VIMS_PATTERN_COLUMNS;
}

static void gvr_pattern_clear_event_at_cursor(GvrVimsPatternView *view)
{
    GvrVimsPatternCell *cell;
    GvrVimsPatternRow *row;
    const int frame = gvr_pattern_current_frame(view);

    if(view->selection_active) {
        gvr_pattern_clear_block(view);
        return;
    }

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);
    if(!cell)
        return;

    row = gvr_pattern_row_get(cell, frame, FALSE);
    if(!row || !row->events[view->selected_column].message)
        return;

    if(!gvr_pattern_push_undo(view))
        return;

    gvr_pattern_event_clear(&row->events[view->selected_column]);
    if(gvr_pattern_row_empty(row))
        g_tree_remove(cell->rows, GINT_TO_POINTER(frame + 1));

    gvr_pattern_emit_changed(view,
                             view->selected_bank,
                             view->selected_slot);
}

static void gvr_pattern_clear_row_at_cursor(GvrVimsPatternView *view)
{
    GvrVimsPatternCell *cell;
    const int frame = gvr_pattern_current_frame(view);

    if(view->selection_active) {
        gvr_pattern_clear_block(view);
        return;
    }

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);

    if(!cell || !cell->rows ||
       !g_tree_lookup(cell->rows, GINT_TO_POINTER(frame + 1)))
        return;

    if(!gvr_pattern_push_undo(view))
        return;

    if(g_tree_remove(cell->rows, GINT_TO_POINTER(frame + 1)))
        gvr_pattern_emit_changed(view,
                                 view->selected_bank,
                                 view->selected_slot);
}


static void gvr_pattern_clear_pattern_user(GvrVimsPatternView *view)
{
    GvrVimsPatternCell *cell;

    if(!gvr_pattern_target_editable(view))
        return;

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);
    if(!cell || !cell->rows || g_tree_nnodes(cell->rows) == 0)
        return;

    if(!gvr_pattern_push_undo(view))
        return;

    gvr_pattern_cell_clear_data(cell);
    gvr_pattern_emit_changed(view,
                             view->selected_bank,
                             view->selected_slot);
    gvr_pattern_selection_clear(view);
}

static void gvr_pattern_inline_finish(GvrVimsPatternView *view)
{
    GtkStyleContext *context;

    if(!view || !view->command_entry)
        return;

    context = gtk_widget_get_style_context(view->command_entry);
    gtk_style_context_remove_class(context, "error");
    view->inline_editing = FALSE;
    gtk_widget_hide(view->command_entry);
    gtk_widget_grab_focus(view->area);
}

static void gvr_pattern_inline_cancel(GvrVimsPatternView *view)
{
    gvr_pattern_inline_finish(view);
}

static gboolean gvr_pattern_inline_commit(GvrVimsPatternView *view)
{
    GvrVimsPatternCell *cell;
    GvrVimsPatternRow *row;
    GvrVimsPatternEvent *event;
    GtkStyleContext *context;
    const char *text;
    char *message;
    int id;
    int frame;

    if(!view || !view->inline_editing ||
       !gvr_pattern_target_editable(view))
        return FALSE;

    text = gtk_entry_get_text(GTK_ENTRY(view->command_entry));
    frame = gvr_pattern_current_frame(view);
    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);
    row = gvr_pattern_row_get(cell, frame, FALSE);
    event = row ? &row->events[view->selected_column] : NULL;

    if(!text || !text[0]) {
        if(event && event->message) {
            if(!gvr_pattern_push_undo(view))
                return FALSE;

            gvr_pattern_event_clear(event);
            if(gvr_pattern_row_empty(row))
                g_tree_remove(cell->rows,
                              GINT_TO_POINTER(frame + 1));
            gvr_pattern_emit_changed(view,
                                     view->selected_bank,
                                     view->selected_slot);
        }

        gvr_pattern_inline_finish(view);
        gvr_pattern_selection_clear(view);
        gvr_pattern_set_cursor_row(view,
                                   view->selected_row + 1,
                                   view->selected_column);
        return TRUE;
    }

    message = gvr_pattern_normalize_message(text);
    id = gvr_pattern_parse_id(message);

    if(!message || id < 0) {
        g_free(message);
        context = gtk_widget_get_style_context(view->command_entry);
        gtk_style_context_add_class(context, "error");
        gtk_widget_set_tooltip_text(
            view->command_entry,
            "Enter one VIMS message, for example 123:arguments;");
        return FALSE;
    }

    if(event && event->message &&
       strcmp(event->message, message) == 0)
    {
        g_free(message);
        gvr_pattern_inline_finish(view);
        gvr_pattern_selection_clear(view);
        gvr_pattern_set_cursor_row(view,
                                   view->selected_row + 1,
                                   view->selected_column);
        return TRUE;
    }

    if(!gvr_pattern_push_undo(view)) {
        g_free(message);
        return FALSE;
    }

    row = gvr_pattern_row_get(cell, frame, TRUE);
    event = &row->events[view->selected_column];
    gvr_pattern_event_clear(event);
    event->vims_id = id;
    event->message = message;
    gvr_pattern_format_label(view,
                             id,
                             message,
                             event->label,
                             sizeof(event->label));

    gvr_pattern_emit_changed(view,
                             view->selected_bank,
                             view->selected_slot);
    gvr_pattern_inline_finish(view);
    gvr_pattern_selection_clear(view);
    gvr_pattern_set_cursor_row(view,
                               view->selected_row + 1,
                               view->selected_column);
    return TRUE;
}

static void gvr_pattern_command_activate(GtkEntry *entry,
                                         gpointer user_data)
{
    (void)entry;
    gvr_pattern_inline_commit(GVR_VIMS_PATTERN_VIEW(user_data));
}

static gboolean gvr_pattern_command_key_press(GtkWidget *widget,
                                              GdkEventKey *event,
                                              gpointer user_data)
{
    (void)widget;

    if(event->keyval == GDK_KEY_Escape) {
        gvr_pattern_inline_cancel(GVR_VIMS_PATTERN_VIEW(user_data));
        return TRUE;
    }

    return FALSE;
}

static gboolean gvr_pattern_command_focus_out(GtkWidget *widget,
                                              GdkEventFocus *event,
                                              gpointer user_data)
{
    GvrVimsPatternView *view =
        GVR_VIMS_PATTERN_VIEW(user_data);

    (void)widget;
    (void)event;

    if(view->inline_editing) {
        GtkStyleContext *context =
            gtk_widget_get_style_context(
                view->command_entry);

        gtk_style_context_remove_class(context, "error");
        view->inline_editing = FALSE;
        gtk_widget_hide(view->command_entry);
    }

    return FALSE;
}

static gboolean gvr_pattern_begin_inline_edit(GvrVimsPatternView *view,
                                              const char *initial_text)
{
    GvrVimsPatternCell *cell;
    GvrVimsPatternRow *row;
    GvrVimsPatternEvent *event;
    const int frame = gvr_pattern_current_frame(view);

    if(!gvr_pattern_target_editable(view))
        return FALSE;

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);
    row = gvr_pattern_row_get(cell, frame, FALSE);
    event = row ? &row->events[view->selected_column] : NULL;

    if(initial_text)
        gtk_entry_set_text(GTK_ENTRY(view->command_entry),
                           initial_text);
    else if(event && event->message)
        gtk_entry_set_text(GTK_ENTRY(view->command_entry),
                           event->message);
    else
        gtk_entry_set_text(GTK_ENTRY(view->command_entry), "");

    view->inline_editing = TRUE;
    gtk_widget_show(view->command_entry);
    gtk_widget_grab_focus(view->command_entry);
    gtk_editable_select_region(GTK_EDITABLE(view->command_entry),
                               0,
                               -1);
    return TRUE;
}

enum {
    GVR_PATTERN_MENU_EDIT = 1,
    GVR_PATTERN_MENU_DELETE,
    GVR_PATTERN_MENU_CLEAR_ROW,
    GVR_PATTERN_MENU_INSERT_ROW,
    GVR_PATTERN_MENU_DELETE_ROW_SHIFT,
    GVR_PATTERN_MENU_COPY,
    GVR_PATTERN_MENU_CUT,
    GVR_PATTERN_MENU_PASTE_REPLACE,
    GVR_PATTERN_MENU_PASTE_MERGE,
    GVR_PATTERN_MENU_PASTE_INSERT,
    GVR_PATTERN_MENU_REVEAL_OFFGRID,
    GVR_PATTERN_MENU_QUANTIZE_OFFGRID,
    GVR_PATTERN_MENU_UNDO,
    GVR_PATTERN_MENU_REDO,
    GVR_PATTERN_MENU_CLEAR_PATTERN,
    GVR_PATTERN_MENU_ADD_HOLD,
    GVR_PATTERN_MENU_ADD_STOP,
    GVR_PATTERN_MENU_ADD_FORWARD,
    GVR_PATTERN_MENU_ADD_REVERSE,
    GVR_PATTERN_MENU_ADD_SPEED,
    GVR_PATTERN_MENU_ADD_SLOW
};

enum {
    GVR_PATTERN_COMMAND_HOLD = 1,
    GVR_PATTERN_COMMAND_STOP,
    GVR_PATTERN_COMMAND_FORWARD,
    GVR_PATTERN_COMMAND_REVERSE,
    GVR_PATTERN_COMMAND_SPEED,
    GVR_PATTERN_COMMAND_SLOW
};

typedef struct {
    GvrVimsPatternView *view;
    int action;
} GvrPatternMenuData;

static void gvr_pattern_menu_data_free(gpointer user_data, GClosure *closure)
{
    (void)closure;
    g_free(user_data);
}

static void gvr_pattern_menu_deactivate(GtkWidget *menu, gpointer user_data)
{
    (void)user_data;
    gtk_widget_destroy(menu);
}

static void gvr_pattern_menu_activate(GtkMenuItem *item,
                                      gpointer user_data)
{
    GvrPatternMenuData *data = user_data;

    (void)item;

    switch(data->action) {
        case GVR_PATTERN_MENU_EDIT:
            gvr_pattern_begin_inline_edit(data->view, NULL);
            break;
        case GVR_PATTERN_MENU_DELETE:
            gvr_pattern_clear_event_at_cursor(data->view);
            break;
        case GVR_PATTERN_MENU_CLEAR_ROW:
            gvr_pattern_clear_row_at_cursor(data->view);
            break;
        case GVR_PATTERN_MENU_INSERT_ROW:
            gvr_pattern_insert_row(data->view);
            break;
        case GVR_PATTERN_MENU_DELETE_ROW_SHIFT:
            gvr_pattern_delete_row_shift(data->view);
            break;
        case GVR_PATTERN_MENU_COPY:
            gvr_pattern_copy_block(data->view);
            break;
        case GVR_PATTERN_MENU_CUT:
            gvr_pattern_cut_block(data->view);
            break;
        case GVR_PATTERN_MENU_PASTE_REPLACE:
            gtk_combo_box_set_active(
                GTK_COMBO_BOX(
                    data->view->paste_mode_combo),
                GVR_PATTERN_PASTE_REPLACE);
            gvr_pattern_paste_block_mode(
                data->view,
                GVR_PATTERN_PASTE_REPLACE);
            break;
        case GVR_PATTERN_MENU_PASTE_MERGE:
            gtk_combo_box_set_active(
                GTK_COMBO_BOX(
                    data->view->paste_mode_combo),
                GVR_PATTERN_PASTE_MERGE);
            gvr_pattern_paste_block_mode(
                data->view,
                GVR_PATTERN_PASTE_MERGE);
            break;
        case GVR_PATTERN_MENU_PASTE_INSERT:
            gtk_combo_box_set_active(
                GTK_COMBO_BOX(
                    data->view->paste_mode_combo),
                GVR_PATTERN_PASTE_INSERT);
            gvr_pattern_paste_block_mode(
                data->view,
                GVR_PATTERN_PASTE_INSERT);
            break;
        case GVR_PATTERN_MENU_REVEAL_OFFGRID:
            gvr_pattern_reveal_offgrid(data->view);
            break;
        case GVR_PATTERN_MENU_QUANTIZE_OFFGRID:
            gvr_pattern_quantize_offgrid(data->view);
            break;
        case GVR_PATTERN_MENU_UNDO:
            gvr_pattern_undo(data->view);
            break;
        case GVR_PATTERN_MENU_REDO:
            gvr_pattern_redo(data->view);
            break;
        case GVR_PATTERN_MENU_CLEAR_PATTERN:
            gvr_pattern_clear_pattern_user(data->view);
            break;
        case GVR_PATTERN_MENU_ADD_HOLD:
        case GVR_PATTERN_MENU_ADD_STOP:
        case GVR_PATTERN_MENU_ADD_FORWARD:
        case GVR_PATTERN_MENU_ADD_REVERSE:
        case GVR_PATTERN_MENU_ADD_SPEED:
        case GVR_PATTERN_MENU_ADD_SLOW: {
            int command = GVR_PATTERN_COMMAND_HOLD +
                          (data->action - GVR_PATTERN_MENU_ADD_HOLD);

            g_signal_emit(data->view,
                          gvr_vims_pattern_view_signals[SIGNAL_COMMAND_REQUESTED],
                          0,
                          command,
                          gvr_pattern_current_frame(data->view),
                          data->view->selected_column);
            break;
        }
        default:
            break;
    }
}

static void gvr_pattern_menu_add(GtkWidget *menu,
                                 GvrVimsPatternView *view,
                                 const char *label,
                                 int action,
                                 gboolean sensitive)
{
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    GvrPatternMenuData *data = g_new0(GvrPatternMenuData, 1);

    data->view = view;
    data->action = action;
    gtk_widget_set_sensitive(item, sensitive);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect_data(item,
                          "activate",
                          G_CALLBACK(gvr_pattern_menu_activate),
                          data,
                          gvr_pattern_menu_data_free,
                          0);
    gtk_widget_show(item);
}

static gboolean gvr_pattern_area_button_press(GtkWidget *widget,
                                              GdkEventButton *event,
                                              gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);
    int frame;
    int column;
    int row;

    gtk_widget_grab_focus(widget);

    if(view->inline_editing)
        gvr_pattern_inline_cancel(view);

    if(!gvr_pattern_position_from_xy(view,
                                     event->x,
                                     event->y,
                                     &frame,
                                     &column))
        return FALSE;

    row = gvr_pattern_frame_to_display_row(view, frame);

    if(event->button == 1 &&
       event->x < GVR_PATTERN_FRAME_WIDTH &&
       (event->state & GDK_CONTROL_MASK) &&
       gvr_pattern_edit_step(view) > 1)
    {
        GvrVimsPatternCell *cell =
            gvr_pattern_cell(view,
                             view->selected_bank,
                             view->selected_slot);
        GvrPatternOffgridInfo hidden =
            gvr_pattern_offgrid_info(
                cell,
                frame,
                MIN(gvr_pattern_effective_frame_count(view),
                    frame + gvr_pattern_edit_step(view)));

        if(hidden.first_frame >= 0) {
            gvr_vims_pattern_view_set_edit_step(GTK_WIDGET(view), 1);
            gvr_pattern_set_cursor(view,
                                   hidden.first_frame,
                                   view->selected_column);
            return TRUE;
        }
    }

    if(event->button == 1) {
        const gboolean extend =
            (event->state & GDK_SHIFT_MASK) != 0;
        guint flags =
            (event->state & GDK_CONTROL_MASK) ?
            GVR_VIMS_PATTERN_TRANSPORT_FORCE : 0;

        if(extend) {
            gvr_pattern_selection_begin(view);
            gvr_pattern_set_cursor_row(view, row, column);
            view->selection_active = TRUE;
        }
        else {
            gvr_pattern_set_cursor_row(view, row, column);
            gvr_pattern_selection_clear(view);
        }

        view->drag_selecting = TRUE;
        gvr_pattern_request_transport(view, frame, flags);

        if(event->type == GDK_2BUTTON_PRESS) {
            view->drag_selecting = FALSE;
            gvr_pattern_begin_inline_edit(view, NULL);
        }

        return TRUE;
    }

    if(event->button == 3) {
        GvrVimsPatternCell *cell;
        GvrVimsPatternRow *pattern_row;
        gboolean have_event;
        gboolean have_row;
        GvrPatternOffgridInfo hidden;
        GtkWidget *menu;

        if(!gvr_pattern_selection_contains(view, row, column)) {
            gvr_pattern_set_cursor_row(view, row, column);
            gvr_pattern_selection_clear(view);
        }

        cell = gvr_pattern_cell(view,
                                view->selected_bank,
                                view->selected_slot);
        pattern_row =
            cell ? gvr_pattern_row_get(cell, frame, FALSE) : NULL;
        have_event =
            pattern_row &&
            pattern_row->events[column].message;
        have_row =
            pattern_row &&
            !gvr_pattern_row_empty(pattern_row);
        hidden =
            gvr_pattern_selected_offgrid_info(view);
        menu = gtk_menu_new();

        if(view->selected_bank == GVR_VIMS_PATTERN_SEQUENCE_BANK) {
            GtkWidget *command_menu = gtk_menu_new();
            GtkWidget *command_root = gtk_menu_item_new_with_label("Add Command");

            gtk_widget_set_sensitive(command_root,
                                     gvr_pattern_target_editable(view));
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(command_root),
                                      command_menu);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), command_root);
            gtk_widget_show(command_root);

            gboolean command_sensitive = gvr_pattern_target_editable(view);

            gvr_pattern_menu_add(command_menu, view, "HOLD", GVR_PATTERN_MENU_ADD_HOLD, command_sensitive);
            gvr_pattern_menu_add(command_menu, view, "STOP", GVR_PATTERN_MENU_ADD_STOP, command_sensitive);

            GtkWidget *transport_sep = gtk_separator_menu_item_new();
            gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), transport_sep);
            gtk_widget_show(transport_sep);

            gvr_pattern_menu_add(command_menu, view, "FORWARD", GVR_PATTERN_MENU_ADD_FORWARD, command_sensitive);
            gvr_pattern_menu_add(command_menu, view, "REVERSE", GVR_PATTERN_MENU_ADD_REVERSE, command_sensitive);
            gvr_pattern_menu_add(command_menu, view, "SPEED (current value)", GVR_PATTERN_MENU_ADD_SPEED, command_sensitive);
            gvr_pattern_menu_add(command_menu, view, "SLOW (current value)", GVR_PATTERN_MENU_ADD_SLOW, command_sensitive);


            GtkWidget *command_sep = gtk_separator_menu_item_new();
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), command_sep);
            gtk_widget_show(command_sep);
        }

        gvr_pattern_menu_add(menu,
                             view,
                             "Edit event",
                             GVR_PATTERN_MENU_EDIT,
                             gvr_pattern_target_editable(view));
        gvr_pattern_menu_add(menu,
                             view,
                             "Remove selection/event",
                             GVR_PATTERN_MENU_DELETE,
                             have_event ||
                             gvr_pattern_selection_has_events(view));
        gvr_pattern_menu_add(menu,
                             view,
                             "Clear row",
                             GVR_PATTERN_MENU_CLEAR_ROW,
                             have_row ||
                             gvr_pattern_selection_has_events(view));
        gvr_pattern_menu_add(menu,
                             view,
                             "Insert row",
                             GVR_PATTERN_MENU_INSERT_ROW,
                             gvr_pattern_target_editable(view));
        gvr_pattern_menu_add(menu,
                             view,
                             "Delete row and pull",
                             GVR_PATTERN_MENU_DELETE_ROW_SHIFT,
                             gvr_pattern_target_editable(view));

        GtkWidget *separator;

        separator = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
        gtk_widget_show(separator);

        gvr_pattern_menu_add(menu,
                             view,
                             "Copy block",
                             GVR_PATTERN_MENU_COPY,
                             gvr_pattern_target_editable(view));
        gvr_pattern_menu_add(menu,
                             view,
                             "Cut block",
                             GVR_PATTERN_MENU_CUT,
                             gvr_pattern_selection_has_events(view));
        gvr_pattern_menu_add(menu,
                             view,
                             "Paste — Replace",
                             GVR_PATTERN_MENU_PASTE_REPLACE,
                             view->block_clipboard.events != NULL);
        gvr_pattern_menu_add(menu,
                             view,
                             "Paste — Merge",
                             GVR_PATTERN_MENU_PASTE_MERGE,
                             view->block_clipboard.events != NULL);
        gvr_pattern_menu_add(menu,
                             view,
                             "Paste — Insert rows",
                             GVR_PATTERN_MENU_PASTE_INSERT,
                             view->block_clipboard.events != NULL);

        separator = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
        gtk_widget_show(separator);

        gvr_pattern_menu_add(menu,
                             view,
                             "Reveal hidden off-grid events",
                             GVR_PATTERN_MENU_REVEAL_OFFGRID,
                             hidden.events > 0);
        gvr_pattern_menu_add(menu,
                             view,
                             view->selection_active ?
                             "Quantize selected hidden events to Step" :
                             "Quantize hidden events to this Step row",
                             GVR_PATTERN_MENU_QUANTIZE_OFFGRID,
                             hidden.events > 0);

        separator = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
        gtk_widget_show(separator);

        gvr_pattern_menu_add(menu,
                             view,
                             "Undo",
                             GVR_PATTERN_MENU_UNDO,
                             !g_queue_is_empty(view->undo_stack));
        gvr_pattern_menu_add(menu,
                             view,
                             "Redo",
                             GVR_PATTERN_MENU_REDO,
                             !g_queue_is_empty(view->redo_stack));
        gvr_pattern_menu_add(menu,
                             view,
                             "Clear VIMS pattern",
                             GVR_PATTERN_MENU_CLEAR_PATTERN,
                             cell && cell->rows &&
                             g_tree_nnodes(cell->rows) > 0);

        g_signal_connect(menu,
                         "deactivate",
                         G_CALLBACK(gvr_pattern_menu_deactivate),
                         NULL);
        gtk_menu_popup_at_pointer(GTK_MENU(menu),
                                  (GdkEvent *)event);
        return TRUE;
    }

    return FALSE;
}


static gboolean gvr_pattern_area_motion(GtkWidget *widget,
                                        GdkEventMotion *event,
                                        gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);
    int frame;
    int column;
    int row;

    (void)widget;

    if(!view->drag_selecting)
        return FALSE;

    if(!gvr_pattern_position_from_xy(view,
                                     event->x,
                                     event->y,
                                     &frame,
                                     &column))
        return TRUE;

    row = gvr_pattern_frame_to_display_row(view, frame);

    if(row != view->selected_row ||
       column != view->selected_column)
    {
        gvr_pattern_selection_begin(view);
        gvr_pattern_set_cursor_row(view, row, column);
        view->selection_active = TRUE;
    }

    return TRUE;
}

static gboolean gvr_pattern_area_button_release(GtkWidget *widget,
                                                GdkEventButton *event,
                                                gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);

    (void)widget;

    if(event->button != 1 || !view->drag_selecting)
        return FALSE;

    view->drag_selecting = FALSE;
    return TRUE;
}

static gboolean gvr_pattern_area_key_press(GtkWidget *widget,
                                           GdkEventKey *event,
                                           gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);
    int rows = gvr_pattern_visible_rows(view);
    gboolean shift =
        (event->state & GDK_SHIFT_MASK) != 0;
    gboolean ctrl =
        (event->state & GDK_CONTROL_MASK) != 0;

    (void)widget;

    if(ctrl &&
       (event->keyval == GDK_KEY_z ||
        event->keyval == GDK_KEY_Z))
    {
        if(shift)
            gvr_pattern_redo(view);
        else
            gvr_pattern_undo(view);
        return TRUE;
    }

    if(ctrl &&
       (event->keyval == GDK_KEY_y ||
        event->keyval == GDK_KEY_Y))
    {
        gvr_pattern_redo(view);
        return TRUE;
    }

    if(ctrl &&
       (event->keyval == GDK_KEY_c ||
        event->keyval == GDK_KEY_C))
    {
        gvr_pattern_copy_block(view);
        return TRUE;
    }

    if(ctrl &&
       (event->keyval == GDK_KEY_x ||
        event->keyval == GDK_KEY_X))
    {
        gvr_pattern_cut_block(view);
        return TRUE;
    }

    if(ctrl &&
       (event->keyval == GDK_KEY_v ||
        event->keyval == GDK_KEY_V))
    {
        gvr_pattern_paste(view);
        return TRUE;
    }

    if(ctrl &&
       (event->keyval == GDK_KEY_l ||
        event->keyval == GDK_KEY_L))
    {
        if(shift)
            gvr_pattern_loop_clear(NULL, view);
        else
            gvr_pattern_loop_set(NULL, view);
        return TRUE;
    }

    switch(event->keyval) {
        case GDK_KEY_Escape:
            if(view->inline_editing)
                gvr_pattern_inline_cancel(view);
            else {
                gvr_pattern_selection_clear(view);
                gtk_widget_queue_draw(view->area);
            }
            return TRUE;

        case GDK_KEY_Tab:
        case GDK_KEY_ISO_Left_Tab: {
            int column =
                view->selected_column + (shift ? -1 : 1);

            if(column < 0)
                column = GVR_VIMS_PATTERN_COLUMNS - 1;
            else if(column >= GVR_VIMS_PATTERN_COLUMNS)
                column = 0;

            gvr_pattern_selection_clear(view);
            gvr_pattern_set_cursor_row(view,
                                       view->selected_row,
                                       column);
            return TRUE;
        }

        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
            gvr_pattern_move_cursor(view,
                                    0,
                                    -1,
                                    shift,
                                    FALSE);
            return TRUE;

        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
            gvr_pattern_move_cursor(view,
                                    0,
                                    1,
                                    shift,
                                    FALSE);
            return TRUE;

        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            gvr_pattern_move_cursor(view,
                                    -1,
                                    0,
                                    shift,
                                    !shift);
            return TRUE;

        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            gvr_pattern_move_cursor(view,
                                    1,
                                    0,
                                    shift,
                                    !shift);
            return TRUE;

        case GDK_KEY_Page_Up:
        case GDK_KEY_KP_Page_Up:
            gvr_pattern_move_cursor(view,
                                    -rows,
                                    0,
                                    shift,
                                    !shift);
            return TRUE;

        case GDK_KEY_Page_Down:
        case GDK_KEY_KP_Page_Down:
            gvr_pattern_move_cursor(view,
                                    rows,
                                    0,
                                    shift,
                                    !shift);
            return TRUE;

        case GDK_KEY_Home:
        case GDK_KEY_KP_Home:
            if(shift)
                gvr_pattern_selection_begin(view);
            else
                gvr_pattern_selection_clear(view);
            gvr_pattern_move_row(view,
                                 0,
                                 view->selected_column,
                                 !shift);
            if(shift)
                view->selection_active = TRUE;
            return TRUE;

        case GDK_KEY_End:
        case GDK_KEY_KP_End:
            if(shift)
                gvr_pattern_selection_begin(view);
            else
                gvr_pattern_selection_clear(view);
            gvr_pattern_move_row(
                view,
                MAX(0, gvr_pattern_display_row_count(view) - 1),
                view->selected_column,
                !shift);
            if(shift)
                view->selection_active = TRUE;
            return TRUE;

        case GDK_KEY_Insert:
        case GDK_KEY_KP_Insert:
            gvr_pattern_insert_row(view);
            return TRUE;

        case GDK_KEY_Delete:
        case GDK_KEY_KP_Delete:
        case GDK_KEY_BackSpace:
            if(shift)
                gvr_pattern_delete_row_shift(view);
            else
                gvr_pattern_clear_event_at_cursor(view);
            return TRUE;

        case GDK_KEY_F2:
            gvr_pattern_begin_inline_edit(view, NULL);
            return TRUE;

        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if(shift) {
                gvr_pattern_request_transport(
                    view,
                    gvr_pattern_current_frame(view),
                    GVR_VIMS_PATTERN_TRANSPORT_FORCE |
                    GVR_VIMS_PATTERN_TRANSPORT_PLAY);
            }
            else if(ctrl) {
                gvr_pattern_request_transport(
                    view,
                    gvr_pattern_current_frame(view),
                    GVR_VIMS_PATTERN_TRANSPORT_FORCE);
            }
            else {
                gvr_pattern_begin_inline_edit(view, NULL);
            }
            return TRUE;

        default:
            break;
    }

    if(!ctrl &&
       !(event->state & GDK_MOD1_MASK) &&
       event->string &&
       event->string[0] &&
       g_ascii_isdigit(event->string[0]))
    {
        char initial[2] = { event->string[0], '\0' };
        gvr_pattern_begin_inline_edit(view, initial);
        gtk_editable_set_position(
            GTK_EDITABLE(view->command_entry),
            -1);
        return TRUE;
    }

    return FALSE;
}

static gboolean gvr_pattern_area_query_tooltip(GtkWidget *widget,
                                               gint x,
                                               gint y,
                                               gboolean keyboard_mode,
                                               GtkTooltip *tooltip,
                                               gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);
    int frame = gvr_pattern_current_frame(view);
    int column = view->selected_column;
    GvrVimsPatternCell *cell;
    GvrVimsPatternRow *row;
    GvrVimsPatternEvent *event;
    const char *description = NULL;
    char text[960];

    (void)widget;

    if(!keyboard_mode &&
       !gvr_pattern_position_from_xy(view,
                                     x,
                                     y,
                                     &frame,
                                     &column))
        return FALSE;

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);
    row = cell ? gvr_pattern_row_get(cell, frame, FALSE) : NULL;
    event = row ? &row->events[column] : NULL;

    if(!keyboard_mode && x < GVR_PATTERN_FRAME_WIDTH) {
        const int step = gvr_pattern_edit_step(view);
        const int next_frame =
            MIN(gvr_pattern_effective_frame_count(view),
                frame + step);
        GvrPatternOffgridInfo hidden =
            gvr_pattern_offgrid_info(cell, frame, next_frame);

        if(gvr_pattern_loop_valid(cell))
            g_snprintf(text,
                       sizeof(text),
                       "Frame %d · stepped row ×%d\n"
                       "%d hidden event%s across %d hidden frame%s before frame %d.\n"
                       "Pattern loop: %d–%d. Events execute left-to-right, V1 through V8.",
                       frame,
                       step,
                       hidden.events,
                       hidden.events == 1 ? "" : "s",
                       hidden.rows,
                       hidden.rows == 1 ? "" : "s",
                       next_frame,
                       cell->loop_start,
                       cell->loop_end);
        else
            g_snprintf(text,
                       sizeof(text),
                       "Frame %d · stepped row ×%d\n"
                       "%d hidden event%s across %d hidden frame%s before frame %d.\n"
                       "Events execute left-to-right, V1 through V8.",
                       frame,
                       step,
                       hidden.events,
                       hidden.events == 1 ? "" : "s",
                       hidden.rows,
                       hidden.rows == 1 ? "" : "s",
                       next_frame);

        gtk_tooltip_set_text(tooltip, text);
        return TRUE;
    }

    if(event && event->message && view->description_lookup)
        description = view->description_lookup(
            event->vims_id,
            view->description_lookup_data);

    if(event && event->message &&
       description && description[0])
    {
        g_snprintf(
            text,
            sizeof(text),
            "Frame %d · V%d\n%s\nVIMS %03d — %s\nCommand: %s\n\n"
            "Enter/F2/double-click: edit · Delete: clear\n"
            "Shift+arrows or drag: select block · Ctrl+C/X/V uses the selected paste mode\n"
            "Insert: insert row · Shift+Delete: delete row and pull · Ctrl+Z/Y: undo/redo\n"
            "Ctrl+L: loop selection · Ctrl+Shift+L: clear loop",
            frame,
            column + 1,
            event->label,
            event->vims_id,
            description,
            event->message);
    }
    else if(event && event->message) {
        g_snprintf(
            text,
            sizeof(text),
            "Frame %d · V%d\n%s\nVIMS %03d\nCommand: %s\n\n"
            "Enter/F2/double-click: edit · Delete: clear\n"
            "Shift+arrows or drag: select block · Ctrl+C/X/V uses the selected paste mode\n"
            "Insert: insert row · Shift+Delete: delete row and pull · Ctrl+Z/Y: undo/redo\n"
            "Ctrl+L: loop selection · Ctrl+Shift+L: clear loop",
            frame,
            column + 1,
            event->label,
            event->vims_id,
            event->message);
    }
    else {
        g_snprintf(
            text,
            sizeof(text),
            "Frame %d · V%d\nEmpty event column\n\n"
            "Type a digit, press Enter/F2, or enable Learn to enter a VIMS command.\n"
            "Shift+arrows or drag selects a block. Insert adds one stepped row; "
            "Shift+Delete removes one and pulls later events backward. Ctrl+L loops the selected rows.",
            frame,
            column + 1);
    }

    gtk_tooltip_set_text(tooltip, text);
    return TRUE;
}

static void gvr_pattern_toolbar_clear_event(GtkButton *button, gpointer user_data)
{
    (void)button;
    gvr_pattern_clear_event_at_cursor(GVR_VIMS_PATTERN_VIEW(user_data));
}

static void gvr_pattern_toolbar_clear_row(GtkButton *button, gpointer user_data)
{
    (void)button;
    gvr_pattern_clear_row_at_cursor(GVR_VIMS_PATTERN_VIEW(user_data));
}

static void gvr_pattern_toolbar_clear_pattern(GtkButton *button,
                                              gpointer user_data)
{
    (void)button;
    gvr_pattern_clear_pattern_user(
        GVR_VIMS_PATTERN_VIEW(user_data));
}


static void gvr_pattern_toolbar_undo(GtkButton *button,
                                     gpointer user_data)
{
    (void)button;
    gvr_pattern_undo(GVR_VIMS_PATTERN_VIEW(user_data));
}

static void gvr_pattern_toolbar_redo(GtkButton *button,
                                     gpointer user_data)
{
    (void)button;
    gvr_pattern_redo(GVR_VIMS_PATTERN_VIEW(user_data));
}

static void gvr_pattern_toolbar_copy(GtkButton *button,
                                     gpointer user_data)
{
    (void)button;
    gvr_pattern_copy_block(GVR_VIMS_PATTERN_VIEW(user_data));
}

static void gvr_pattern_toolbar_cut(GtkButton *button,
                                    gpointer user_data)
{
    (void)button;
    gvr_pattern_cut_block(GVR_VIMS_PATTERN_VIEW(user_data));
}

static void gvr_pattern_toolbar_paste(GtkButton *button,
                                      gpointer user_data)
{
    (void)button;
    gvr_pattern_paste(GVR_VIMS_PATTERN_VIEW(user_data));
}

static void gvr_pattern_paste_mode_changed(
        GtkComboBox *combo,
        gpointer user_data)
{
    GvrVimsPatternView *view =
        GVR_VIMS_PATTERN_VIEW(user_data);
    int mode = gtk_combo_box_get_active(combo);

    if(mode >= 0 &&
       mode < GVR_PATTERN_PASTE_MODE_COUNT)
        view->paste_mode = mode;
}

static void gvr_pattern_toolbar_insert_row(GtkButton *button,
                                           gpointer user_data)
{
    (void)button;
    gvr_pattern_insert_row(GVR_VIMS_PATTERN_VIEW(user_data));
}

static void gvr_pattern_toolbar_delete_row(GtkButton *button,
                                           gpointer user_data)
{
    (void)button;
    gvr_pattern_delete_row_shift(
        GVR_VIMS_PATTERN_VIEW(user_data));
}

static void gvr_pattern_apply_step(GvrVimsPatternView *view,
                                   int step)
{
    int frame;

    step = gvr_pattern_clampi(step, 1, 999);

    if(view->row_step == step)
        return;

    frame = gvr_pattern_current_frame(view);
    view->row_step = step;
    view->selected_row =
        gvr_pattern_frame_to_display_row(view, frame);
    gvr_pattern_selection_clear(view);
    gvr_pattern_update_target_label(view);
    gvr_pattern_update_adjustment(view);
    gvr_pattern_scroll_to_cursor(view);
    gtk_widget_queue_draw(view->area);
}

static void gvr_pattern_step_adjustment_changed(GtkAdjustment *adjustment,
                                                 gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);

    gvr_pattern_apply_step(
        view,
        (int)(gtk_adjustment_get_value(adjustment) + 0.5));
}

static void gvr_pattern_step_text_changed(GtkEditable *editable,
                                          gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);
    const char *text = gtk_entry_get_text(GTK_ENTRY(editable));
    char *end = NULL;
    long step;

    if(!text || !text[0])
        return;

    step = strtol(text, &end, 10);
    if(end == text)
        return;

    while(*end && g_ascii_isspace(*end))
        end++;
    if(*end != '\0')
        return;

    gvr_pattern_apply_step(view, (int)step);
}


static void gvr_pattern_learn_policy_changed(GtkComboBox *combo,
                                                gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);
    int policy = gtk_combo_box_get_active(combo);

    if(policy < 0 || policy >= GVR_PATTERN_LEARN_POLICY_COUNT)
        policy = GVR_PATTERN_LEARN_REPLACE_MATCH;

    if(view->learn_policy == policy)
        return;

    view->learn_policy = policy;
    gvr_pattern_update_target_label(view);

    if(gvr_pattern_target_valid(view->selected_bank,
                                view->selected_slot))
        gvr_pattern_emit_changed(view,
                                 view->selected_bank,
                                 view->selected_slot);
}

static void gvr_pattern_sync_loop_controls(GvrVimsPatternView *view)
{
    GvrVimsPatternCell *cell;
    gboolean valid_target;
    gboolean have_range;

    if(!view)
        return;

    valid_target = gvr_pattern_target_valid(view->selected_bank,
                                            view->selected_slot);
    cell = valid_target ?
           gvr_pattern_cell_lookup(view,
                                   view->selected_bank,
                                   view->selected_slot) :
           NULL;
    have_range = cell &&
                 cell->loop_start >= 0 &&
                 cell->loop_end >= cell->loop_start;

    view->syncing_loop_toggle = TRUE;
    if(view->loop_toggle)
        gtk_toggle_button_set_active(
            GTK_TOGGLE_BUTTON(view->loop_toggle),
            have_range && cell->loop_enabled);
    view->syncing_loop_toggle = FALSE;

    if(view->loop_toggle)
        gtk_widget_set_sensitive(view->loop_toggle,
                                 valid_target && have_range);
    if(view->loop_set_button)
        gtk_widget_set_sensitive(view->loop_set_button,
                                 valid_target);
    if(view->loop_clear_button)
        gtk_widget_set_sensitive(view->loop_clear_button,
                                 valid_target && have_range);
}

static void gvr_pattern_loop_set(GtkButton *button, gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);
    GvrVimsPatternCell *cell;
    int first_row;
    int last_row;
    int first_column;
    int last_column;
    int frame_count;

    (void)button;

    if(!gvr_pattern_target_editable(view) ||
       !gvr_pattern_push_undo(view))
        return;

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);
    if(!cell)
        return;

    gvr_pattern_selection_bounds(view,
                                 &first_row,
                                 &last_row,
                                 &first_column,
                                 &last_column);
    (void)first_column;
    (void)last_column;
    frame_count = gvr_pattern_effective_frame_count(view);
    cell->loop_start =
        gvr_pattern_display_row_to_frame(view, first_row);
    cell->loop_end =
        MIN(frame_count - 1,
            gvr_pattern_display_row_to_frame(view, last_row) +
            gvr_pattern_edit_step(view) - 1);
    cell->loop_enabled = TRUE;

    gvr_pattern_emit_changed(view,
                             view->selected_bank,
                             view->selected_slot);
    gvr_pattern_playback_remove(view,
                                view->selected_bank,
                                view->selected_slot);
    gvr_pattern_sync_loop_controls(view);
    gtk_widget_queue_draw(view->area);
}

static void gvr_pattern_loop_clear(GtkButton *button, gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);
    GvrVimsPatternCell *cell;

    (void)button;

    if(!gvr_pattern_target_editable(view))
        return;

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);
    if(!cell ||
       (cell->loop_start < 0 && cell->loop_end < 0))
        return;

    if(!gvr_pattern_push_undo(view))
        return;

    cell->loop_enabled = FALSE;
    cell->loop_start = -1;
    cell->loop_end = -1;
    gvr_pattern_emit_changed(view,
                             view->selected_bank,
                             view->selected_slot);
    gvr_pattern_playback_remove(view,
                                view->selected_bank,
                                view->selected_slot);
    gvr_pattern_sync_loop_controls(view);
    gtk_widget_queue_draw(view->area);
}

static void gvr_pattern_loop_toggled(GtkToggleButton *button,
                                     gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);
    GvrVimsPatternCell *cell;
    gboolean enabled;

    if(view->syncing_loop_toggle ||
       !gvr_pattern_target_editable(view))
        return;

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);
    if(!cell || cell->loop_start < 0 || cell->loop_end < cell->loop_start)
        return;

    enabled = gtk_toggle_button_get_active(button);
    if(cell->loop_enabled == enabled)
        return;

    if(!gvr_pattern_push_undo(view))
        return;

    cell->loop_enabled = enabled;
    gvr_pattern_emit_changed(view,
                             view->selected_bank,
                             view->selected_slot);
    gvr_pattern_playback_remove(view,
                                view->selected_bank,
                                view->selected_slot);
    gtk_widget_queue_draw(view->area);
}

static void gvr_pattern_follow_toggled(GtkToggleButton *button, gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);

    if(gtk_toggle_button_get_active(button) &&
       view->live_active &&
       view->live_bank == view->selected_bank &&
       view->live_slot == view->selected_slot)
        gvr_pattern_set_cursor(
            view,
            view->live_frame,
            view->selected_column);
}

static void gvr_pattern_learn_toggled(GtkToggleButton *button, gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);

    if(gtk_toggle_button_get_active(button) &&
       gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->follow_toggle)) &&
       view->live_active &&
       view->live_bank == view->selected_bank &&
       view->live_slot == view->selected_slot)
        gvr_pattern_set_cursor(
            view,
            view->live_frame,
            view->selected_column);
}

static gboolean gvr_pattern_track_button_press(GtkWidget *widget,
                                               GdkEventButton *event,
                                               gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);
    int column = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(widget), "gvr-pattern-column"));

    if(event->button != 1 || column < 0 ||
       column >= GVR_VIMS_PATTERN_COLUMNS)
        return FALSE;

    if(event->state & GDK_CONTROL_MASK) {
        view->enabled_columns_mask = 1u << column;
        gvr_pattern_sync_track_toggles(view);
        if(gvr_pattern_target_valid(view->selected_bank,
                                    view->selected_slot))
            gvr_pattern_emit_changed(view,
                                     view->selected_bank,
                                     view->selected_slot);
        return TRUE;
    }

    if(event->state & GDK_SHIFT_MASK) {
        view->enabled_columns_mask = GVR_PATTERN_ALL_COLUMNS_MASK;
        gvr_pattern_sync_track_toggles(view);
        if(gvr_pattern_target_valid(view->selected_bank,
                                    view->selected_slot))
            gvr_pattern_emit_changed(view,
                                     view->selected_bank,
                                     view->selected_slot);
        return TRUE;
    }

    return FALSE;
}

static void gvr_pattern_track_toggled(GtkToggleButton *button,
                                      gpointer user_data)
{
    GvrVimsPatternView *view =
        GVR_VIMS_PATTERN_VIEW(user_data);
    int column =
        GPOINTER_TO_INT(
            g_object_get_data(
                G_OBJECT(button),
                "gvr-pattern-column"));

    if(column < 0 ||
       column >= GVR_VIMS_PATTERN_COLUMNS)
        return;

    if(gtk_toggle_button_get_active(button))
        view->enabled_columns_mask |= 1u << column;
    else
        view->enabled_columns_mask &= ~(1u << column);

    if(view->syncing_track_toggles) {
        gtk_widget_queue_draw(view->area);
        return;
    }

    if(gvr_pattern_target_valid(view->selected_bank,
                                view->selected_slot))
    {
        gvr_pattern_emit_changed(view,
                                 view->selected_bank,
                                 view->selected_slot);
    }
    else {
        gtk_widget_queue_draw(view->area);
    }
}

static void gvr_pattern_sync_track_toggles(GvrVimsPatternView *view)
{
    view->syncing_track_toggles = TRUE;

    for(int column = 0;
        column < GVR_VIMS_PATTERN_COLUMNS;
        column++)
    {
        if(view->track_toggle[column]) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(
                    view->track_toggle[column]),
                (view->enabled_columns_mask &
                 (1u << column)) != 0);
        }
    }

    view->syncing_track_toggles = FALSE;
    gtk_widget_queue_draw(view->area);
}

static void gvr_vims_pattern_view_finalize(GObject *object)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(object);

    for(int bank = 0; bank < GVR_VIMS_PATTERN_BANKS; bank++) {
        for(int slot = 0;
            slot < GVR_VIMS_PATTERN_SLOTS;
            slot++)
        {
            if(view->cells[bank][slot].rows)
                g_tree_destroy(view->cells[bank][slot].rows);
        }

        if(view->bank_cells[bank].rows)
            g_tree_destroy(view->bank_cells[bank].rows);
    }

    if(view->sample_cells)
        g_hash_table_destroy(view->sample_cells);
    if(view->stream_cells)
        g_hash_table_destroy(view->stream_cells);
    if(view->clipboard)
        g_hash_table_destroy(view->clipboard);
    if(view->playback_states)
        g_hash_table_destroy(view->playback_states);

    gvr_pattern_history_queue_clear(view->undo_stack);
    gvr_pattern_history_queue_clear(view->redo_stack);
    g_clear_pointer(&view->undo_stack, g_queue_free);
    g_clear_pointer(&view->redo_stack, g_queue_free);
    gvr_pattern_block_clipboard_clear(&view->block_clipboard);

    if(view->description_lookup_destroy)
        view->description_lookup_destroy(
            view->description_lookup_data);

    G_OBJECT_CLASS(
        gvr_vims_pattern_view_parent_class)->finalize(object);
}


static void gvr_vims_pattern_view_style_updated(GtkWidget *widget)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(widget);

    GTK_WIDGET_CLASS(gvr_vims_pattern_view_parent_class)->style_updated(widget);
    if(view->area && view->vadjustment) {
        gvr_pattern_update_adjustment(view);
        gtk_widget_queue_resize(view->area);
        gtk_widget_queue_draw(view->area);
    }
}

static void gvr_vims_pattern_view_class_init(GvrVimsPatternViewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    widget_class->style_updated = gvr_vims_pattern_view_style_updated;
    object_class->finalize = gvr_vims_pattern_view_finalize;

    gvr_vims_pattern_view_signals[SIGNAL_VIMS_FIRE] =
        g_signal_new("vims-fire",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     g_cclosure_marshal_VOID__STRING,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_STRING);

    gvr_vims_pattern_view_signals[SIGNAL_PATTERN_CHANGED] =
        g_signal_new("pattern-changed",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     3,
                     G_TYPE_INT,
                     G_TYPE_INT,
                     G_TYPE_UINT);

    gvr_vims_pattern_view_signals[SIGNAL_TRANSPORT_REQUEST] =
        g_signal_new("transport-request",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     5,
                     G_TYPE_INT,
                     G_TYPE_INT,
                     G_TYPE_INT,
                     G_TYPE_INT,
                     G_TYPE_UINT);

    gvr_vims_pattern_view_signals[SIGNAL_COMMAND_REQUESTED] =
        g_signal_new("command-requested",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     3,
                     G_TYPE_INT,
                     G_TYPE_INT,
                     G_TYPE_INT);
}

static GtkWidget *gvr_pattern_toolbar_button(GtkWidget *toolbar,
                                             const char *label,
                                             const char *tooltip,
                                             GCallback callback,
                                             gpointer data)
{
    GtkWidget *button = gtk_button_new_with_label(label);

    gtk_style_context_add_class(
        gtk_widget_get_style_context(button),
        "vims-pattern-tool");
    gtk_box_pack_start(GTK_BOX(toolbar),
                       button,
                       FALSE,
                       FALSE,
                       0);
    gtk_widget_set_tooltip_text(button, tooltip);
    g_signal_connect(button, "clicked", callback, data);
    gtk_widget_show(button);
    return button;
}

static void gvr_vims_pattern_view_init(GvrVimsPatternView *view)
{
    GtkWidget *toolbar;
    GtkWidget *editbar;
    GtkWidget *label;
    GtkWidget *body;
    GtkWidget *content;
    GtkWidget *track_header;
    GtkWidget *track_box;
    GtkWidget *learnbar;

    gtk_orientable_set_orientation(
        GTK_ORIENTABLE(view),
        GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing(GTK_BOX(view), 2);

    view->selected_bank = -1;
    view->selected_slot = -1;
    view->selected_sample_id = -1;
    view->selected_sample_type = -1;
    view->frame_count = 0;
    view->frame_count_known = TRUE;
    view->selected_row = 0;
    view->selected_column = 0;
    view->selection_active = FALSE;
    view->selection_anchor_row = 0;
    view->selection_anchor_column = 0;
    view->drag_selecting = FALSE;
    view->last_fired_bank = -1;
    view->last_fired_slot = -1;
    view->last_fired_frame = -1;
    view->last_fired_mask = 0;
    view->live_bank = -1;
    view->live_slot = -1;
    view->live_frame = -1;
    view->enabled_columns_mask =
        GVR_PATTERN_ALL_COLUMNS_MASK;
    view->row_step = 1;
    view->learn_policy = GVR_PATTERN_LEARN_REPLACE_MATCH;
    view->paste_mode = GVR_PATTERN_PASTE_REPLACE;
    view->inline_editing = FALSE;
    view->syncing_track_toggles = FALSE;
    view->syncing_loop_toggle = FALSE;
    view->undo_stack = g_queue_new();
    view->redo_stack = g_queue_new();
    memset(&view->block_clipboard,
           0,
           sizeof(view->block_clipboard));

    view->sample_cells = g_hash_table_new_full(
        g_direct_hash,
        g_direct_equal,
        NULL,
        gvr_pattern_dynamic_cell_free);
    view->stream_cells = g_hash_table_new_full(
        g_direct_hash,
        g_direct_equal,
        NULL,
        gvr_pattern_dynamic_cell_free);
    view->clipboard = g_hash_table_new_full(
        g_direct_hash,
        g_direct_equal,
        NULL,
        gvr_pattern_clipboard_cell_free);
    view->playback_states = g_hash_table_new_full(
        g_int64_hash,
        g_int64_equal,
        g_free,
        g_free);

    for(int bank = 0;
        bank < GVR_VIMS_PATTERN_BANKS;
        bank++)
    {
        for(int slot = 0;
            slot < GVR_VIMS_PATTERN_SLOTS;
            slot++)
        {
            view->cells[bank][slot].sample_id = -1;
            view->cells[bank][slot].sample_type = -1;
            view->cells[bank][slot].loop_start = -1;
            view->cells[bank][slot].loop_end = -1;
            gvr_pattern_cell_ensure(
                &view->cells[bank][slot]);
        }

        view->bank_cells[bank].sample_id = -1;
        view->bank_cells[bank].sample_type = -1;
        view->bank_cells[bank].loop_start = -1;
        view->bank_cells[bank].loop_end = -1;
        gvr_pattern_cell_ensure(&view->bank_cells[bank]);
    }

    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(toolbar),
        "vims-pattern-toolbar");
    gtk_box_pack_start(GTK_BOX(view),
                       toolbar,
                       FALSE,
                       FALSE,
                       0);
    gtk_widget_show(toolbar);

    view->target_badge = gtk_label_new(NULL);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(view->target_badge),
        "vims-pattern-target-badge");
    gtk_box_pack_start(GTK_BOX(toolbar),
                       view->target_badge,
                       FALSE,
                       FALSE,
                       1);

    view->target_label =
        gtk_label_new("No VIMS pattern target   0 frames");
    g_object_set(G_OBJECT(view->target_label),
                 "xalign",
                 0.0f,
                 NULL);
    gtk_widget_set_hexpand(view->target_label, TRUE);
    gtk_box_pack_start(GTK_BOX(toolbar),
                       view->target_label,
                       TRUE,
                       TRUE,
                       2);
    gtk_widget_show(view->target_label);

    label = gtk_label_new("Step");
    gtk_box_pack_start(GTK_BOX(toolbar),
                       label,
                       FALSE,
                       FALSE,
                       1);
    gtk_widget_set_tooltip_text(
        label,
        "FastTracker row step: display every Nth frame and use the same interval for row insertion and Learn advancement.");
    gtk_widget_show(label);

    view->step_spin =
        gtk_spin_button_new_with_range(1, 999, 1);
    gtk_spin_button_set_numeric(
        GTK_SPIN_BUTTON(view->step_spin),
        TRUE);
    gtk_spin_button_set_update_policy(
        GTK_SPIN_BUTTON(view->step_spin),
        GTK_UPDATE_ALWAYS);
    gtk_spin_button_set_value(
        GTK_SPIN_BUTTON(view->step_spin),
        1);
    gtk_widget_set_tooltip_text(
        view->step_spin,
        "Step 4 displays frames 0, 4, 8, 12... Orange markers expose hidden events; right-click to reveal or quantize them.");
    g_signal_connect(
        gtk_spin_button_get_adjustment(
            GTK_SPIN_BUTTON(view->step_spin)),
        "value-changed",
        G_CALLBACK(gvr_pattern_step_adjustment_changed),
        view);
    g_signal_connect(view->step_spin,
                     "changed",
                     G_CALLBACK(gvr_pattern_step_text_changed),
                     view);
    gtk_box_pack_start(GTK_BOX(toolbar),
                       view->step_spin,
                       FALSE,
                       FALSE,
                       0);
    gtk_widget_show(view->step_spin);

    view->loop_toggle = gtk_check_button_new_with_label("Loop");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(view->loop_toggle),
        "vims-pattern-loop-toggle");
    gtk_widget_set_tooltip_text(
        view->loop_toggle,
        "Enable or disable the stored VIMS pattern loop range for the current target. Playback remains backend-authoritative; the editor follows the reported frame without remapping or seeking.");
    g_signal_connect(view->loop_toggle,
                     "toggled",
                     G_CALLBACK(gvr_pattern_loop_toggled),
                     view);
    gtk_box_pack_start(GTK_BOX(toolbar),
                       view->loop_toggle,
                       FALSE,
                       FALSE,
                       0);
    gtk_widget_show(view->loop_toggle);

    view->loop_set_button =
        gvr_pattern_toolbar_button(
            toolbar,
            "Loop Sel",
            "Set and enable the pattern loop from the selected row range, or from the current row when no block is selected",
            G_CALLBACK(gvr_pattern_loop_set),
            view);
    view->loop_clear_button =
        gvr_pattern_toolbar_button(
            toolbar,
            "No Loop",
            "Remove the stored pattern loop range from the current target",
            G_CALLBACK(gvr_pattern_loop_clear),
            view);

    editbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(editbar),
        "vims-pattern-editbar");
    gtk_box_pack_start(GTK_BOX(view),
                       editbar,
                       FALSE,
                       FALSE,
                       0);
    gtk_widget_show(editbar);

    view->command_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(view->command_entry),
        "VIMS command, e.g. 123:arguments;");
    gtk_entry_set_width_chars(
        GTK_ENTRY(view->command_entry),
        28);
    gtk_widget_set_no_show_all(view->command_entry, TRUE);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(view->command_entry),
        "vims-pattern-command");
    g_signal_connect(view->command_entry,
                     "activate",
                     G_CALLBACK(gvr_pattern_command_activate),
                     view);
    g_signal_connect(view->command_entry,
                     "key-press-event",
                     G_CALLBACK(gvr_pattern_command_key_press),
                     view);
    g_signal_connect(view->command_entry,
                     "focus-out-event",
                     G_CALLBACK(gvr_pattern_command_focus_out),
                     view);
    gtk_box_pack_start(GTK_BOX(editbar),
                       view->command_entry,
                       TRUE,
                       TRUE,
                       0);

    view->undo_button =
        gvr_pattern_toolbar_button(
            editbar,
            "Undo",
            "Undo the last pattern edit (Ctrl+Z)",
            G_CALLBACK(gvr_pattern_toolbar_undo),
            view);
    view->redo_button =
        gvr_pattern_toolbar_button(
            editbar,
            "Redo",
            "Redo the last undone pattern edit (Ctrl+Y)",
            G_CALLBACK(gvr_pattern_toolbar_redo),
            view);

    gvr_pattern_toolbar_button(
        editbar,
        "Copy",
        "Copy the selected tracker block (Ctrl+C)",
        G_CALLBACK(gvr_pattern_toolbar_copy),
        view);
    gvr_pattern_toolbar_button(
        editbar,
        "Cut",
        "Cut the selected tracker block (Ctrl+X)",
        G_CALLBACK(gvr_pattern_toolbar_cut),
        view);
    gvr_pattern_toolbar_button(
        editbar,
        "Paste",
        "Paste a copied tracker block or a VIMS command from the VIMS tab (Ctrl+V)",
        G_CALLBACK(gvr_pattern_toolbar_paste),
        view);

    view->paste_mode_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(view->paste_mode_combo),
        "Replace");
    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(view->paste_mode_combo),
        "Merge");
    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(view->paste_mode_combo),
        "Insert");
    gtk_combo_box_set_active(
        GTK_COMBO_BOX(view->paste_mode_combo),
        GVR_PATTERN_PASTE_REPLACE);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(
            view->paste_mode_combo),
        "vims-pattern-paste-mode");
    gtk_widget_set_tooltip_text(
        view->paste_mode_combo,
        "Replace clears destination cells matching empty clipboard cells. Merge writes only copied events. Insert first opens enough timeline rows, then pastes.");
    g_signal_connect(
        view->paste_mode_combo,
        "changed",
        G_CALLBACK(gvr_pattern_paste_mode_changed),
        view);
    gtk_box_pack_start(
        GTK_BOX(editbar),
        view->paste_mode_combo,
        FALSE,
        FALSE,
        0);
    gtk_widget_show(view->paste_mode_combo);
    gvr_pattern_toolbar_button(
        editbar,
        "Ins",
        "Insert one stepped row and push later events forward (Insert)",
        G_CALLBACK(gvr_pattern_toolbar_insert_row),
        view);
    gvr_pattern_toolbar_button(
        editbar,
        "Pull",
        "Delete one stepped row and pull later events backward (Shift+Delete)",
        G_CALLBACK(gvr_pattern_toolbar_delete_row),
        view);
    gvr_pattern_toolbar_button(
        editbar,
        "Del",
        "Remove the current event or selected block",
        G_CALLBACK(gvr_pattern_toolbar_clear_event),
        view);
    gvr_pattern_toolbar_button(
        editbar,
        "Clear Row",
        "Clear the current row or selected block without shifting time",
        G_CALLBACK(gvr_pattern_toolbar_clear_row),
        view);
    gvr_pattern_toolbar_button(
        editbar,
        "Clear Pattern",
        "Remove the complete pattern from the current target",
        G_CALLBACK(gvr_pattern_toolbar_clear_pattern),
        view);

    body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(view),
                       body,
                       TRUE,
                       TRUE,
                       0);
    gtk_widget_show(body);

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(body),
                       content,
                       TRUE,
                       TRUE,
                       0);
    gtk_widget_show(content);

    track_header =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(track_header),
        "vims-pattern-track-header");
    gtk_box_pack_start(GTK_BOX(content),
                       track_header,
                       FALSE,
                       FALSE,
                       0);
    gtk_widget_show(track_header);

    label = gtk_label_new("FRAME");
    gtk_widget_set_size_request(
        label,
        GVR_PATTERN_FRAME_WIDTH,
        -1);
    gtk_widget_set_tooltip_text(
        label,
        "Orange ·N markers show how many stored events are hidden between stepped rows. Green horizontal rules mark the pattern loop boundaries.");
    gtk_box_pack_start(GTK_BOX(track_header),
                       label,
                       FALSE,
                       FALSE,
                       0);
    gtk_widget_show(label);

    track_box =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_homogeneous(GTK_BOX(track_box), TRUE);
    gtk_box_pack_start(GTK_BOX(track_header),
                       track_box,
                       TRUE,
                       TRUE,
                       0);
    gtk_widget_show(track_box);

    for(int column = 0;
        column < GVR_VIMS_PATTERN_COLUMNS;
        column++)
    {
        char title[12];

        g_snprintf(title,
                   sizeof(title),
                   "V%d",
                   column + 1);
        view->track_toggle[column] =
            gtk_check_button_new_with_label(title);
        gtk_style_context_add_class(
            gtk_widget_get_style_context(
                view->track_toggle[column]),
            "vims-pattern-track-toggle");
        gtk_toggle_button_set_active(
            GTK_TOGGLE_BUTTON(
                view->track_toggle[column]),
            TRUE);
        gtk_widget_set_tooltip_text(
            view->track_toggle[column],
            "Toggle to mute this VIMS track; Ctrl-click solos it; Shift-click enables all tracks.");
        g_object_set_data(
            G_OBJECT(view->track_toggle[column]),
            "gvr-pattern-column",
            GINT_TO_POINTER(column));
        gtk_widget_add_events(view->track_toggle[column],
                              GDK_BUTTON_PRESS_MASK);
        g_signal_connect(
            view->track_toggle[column],
            "button-press-event",
            G_CALLBACK(gvr_pattern_track_button_press),
            view);
        g_signal_connect(
            view->track_toggle[column],
            "toggled",
            G_CALLBACK(gvr_pattern_track_toggled),
            view);
        gtk_box_pack_start(
            GTK_BOX(track_box),
            view->track_toggle[column],
            TRUE,
            TRUE,
            0);
        gtk_widget_show(view->track_toggle[column]);
    }

    view->area = gtk_drawing_area_new();
    gtk_widget_set_size_request(
        view->area,
        GVR_PATTERN_FRAME_WIDTH +
        GVR_VIMS_PATTERN_COLUMNS * 34,
        -1);
    gtk_widget_set_can_focus(view->area, TRUE);
    gtk_widget_set_has_tooltip(view->area, TRUE);
    gtk_widget_add_events(
        view->area,
        GDK_BUTTON_PRESS_MASK |
        GDK_BUTTON_RELEASE_MASK |
        GDK_POINTER_MOTION_MASK |
        GDK_SCROLL_MASK |
        GDK_SMOOTH_SCROLL_MASK |
        GDK_KEY_PRESS_MASK);
    gtk_box_pack_start(GTK_BOX(content),
                       view->area,
                       TRUE,
                       TRUE,
                       0);
    g_signal_connect(
        view->area,
        "draw",
        G_CALLBACK(gvr_vims_pattern_view_draw),
        view);
    g_signal_connect(
        view->area,
        "size-allocate",
        G_CALLBACK(gvr_pattern_area_size_allocate),
        view);
    g_signal_connect(
        view->area,
        "scroll-event",
        G_CALLBACK(gvr_pattern_area_scroll),
        view);
    g_signal_connect(
        view->area,
        "button-press-event",
        G_CALLBACK(gvr_pattern_area_button_press),
        view);
    g_signal_connect(
        view->area,
        "button-release-event",
        G_CALLBACK(gvr_pattern_area_button_release),
        view);
    g_signal_connect(
        view->area,
        "motion-notify-event",
        G_CALLBACK(gvr_pattern_area_motion),
        view);
    g_signal_connect(
        view->area,
        "key-press-event",
        G_CALLBACK(gvr_pattern_area_key_press),
        view);
    g_signal_connect(
        view->area,
        "query-tooltip",
        G_CALLBACK(gvr_pattern_area_query_tooltip),
        view);
    gtk_drag_dest_set(
        view->area,
        GTK_DEST_DEFAULT_MOTION |
        GTK_DEST_DEFAULT_HIGHLIGHT |
        GTK_DEST_DEFAULT_DROP,
        gvr_vims_message_drag_targets,
        G_N_ELEMENTS(gvr_vims_message_drag_targets),
        GDK_ACTION_COPY);
    g_signal_connect(
        view->area,
        "drag-data-received",
        G_CALLBACK(gvr_pattern_drag_data_received),
        view);
    gtk_widget_set_tooltip_text(
        view->area,
        "Edit the VIMS pattern or drag a pattern-safe command from VIMS History onto any visible V1-V8 cell.");
    gtk_widget_show(view->area);

    view->vadjustment = GTK_ADJUSTMENT(
        gtk_adjustment_new(0, 0, 1, 1, 1, 1));
    g_signal_connect(
        view->vadjustment,
        "value-changed",
        G_CALLBACK(gvr_pattern_adjustment_changed),
        view);
    view->scrollbar =
        gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL,
                          view->vadjustment);
    gtk_box_pack_start(GTK_BOX(body),
                       view->scrollbar,
                       FALSE,
                       FALSE,
                       0);
    gtk_widget_show(view->scrollbar);

    learnbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(learnbar),
        "vims-pattern-learnbar");
    gtk_box_pack_end(GTK_BOX(view),
                     learnbar,
                     FALSE,
                     FALSE,
                     0);
    gtk_widget_show(learnbar);

    label = gtk_label_new("Learn");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(label),
        "vims-pattern-learn-label");
    gtk_widget_set_tooltip_text(
        label,
        "Learn controls remain visible below the scrolling pattern grid.");
    gtk_box_pack_start(GTK_BOX(learnbar),
                       label,
                       FALSE,
                       FALSE,
                       2);
    gtk_widget_show(label);

    view->learn_toggle =
        gtk_toggle_button_new_with_label("Capture");
    gtk_widget_set_tooltip_text(
        view->learn_toggle,
        "Capture user-triggered VIMS commands using the selected placement policy.");
    gtk_box_pack_start(GTK_BOX(learnbar),
                       view->learn_toggle,
                       FALSE,
                       FALSE,
                       0);
    g_signal_connect(view->learn_toggle,
                     "toggled",
                     G_CALLBACK(gvr_pattern_learn_toggled),
                     view);
    gtk_widget_show(view->learn_toggle);

    view->learn_policy_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(view->learn_policy_combo),
        "Column lock");
    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(view->learn_policy_combo),
        "Next free");
    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(view->learn_policy_combo),
        "Replace match");
    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(view->learn_policy_combo),
        "Overwrite");
    gtk_combo_box_text_append_text(
        GTK_COMBO_BOX_TEXT(view->learn_policy_combo),
        "Layer");
    gtk_combo_box_set_active(
        GTK_COMBO_BOX(view->learn_policy_combo),
        GVR_PATTERN_LEARN_REPLACE_MATCH);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(view->learn_policy_combo),
        "vims-pattern-policy");
    gtk_widget_set_tooltip_text(
        view->learn_policy_combo,
        "Choose how learned commands are placed in V1 through V8.");
    g_signal_connect(view->learn_policy_combo,
                     "changed",
                     G_CALLBACK(gvr_pattern_learn_policy_changed),
                     view);
    gtk_box_pack_start(GTK_BOX(learnbar),
                       view->learn_policy_combo,
                       FALSE,
                       FALSE,
                       0);
    gtk_widget_show(view->learn_policy_combo);

    view->follow_toggle =
        gtk_check_button_new_with_label("Follow");
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(view->follow_toggle),
        TRUE);
    gtk_widget_set_tooltip_text(
        view->follow_toggle,
        "Follow the live source row. Learn temporarily owns the edit cursor while capture is active.");
    gtk_box_pack_start(GTK_BOX(learnbar),
                       view->follow_toggle,
                       FALSE,
                       FALSE,
                       2);
    g_signal_connect(view->follow_toggle,
                     "toggled",
                     G_CALLBACK(gvr_pattern_follow_toggled),
                     view);
    gtk_widget_show(view->follow_toggle);

    gvr_pattern_update_history_buttons(view);
    gvr_pattern_sync_loop_controls(view);
}

GtkWidget *gvr_vims_pattern_view_new(void)
{
    return g_object_new(GVR_TYPE_VIMS_PATTERN_VIEW, NULL);
}

void gvr_vims_pattern_view_clear_target(GtkWidget *widget)
{
    GvrVimsPatternView *view;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    if(view->inline_editing)
        gvr_pattern_inline_cancel(view);

    view->selected_bank = -1;
    view->selected_slot = -1;
    view->selected_sample_id = -1;
    view->selected_sample_type = -1;
    view->frame_count = 0;
    view->frame_count_known = TRUE;
    view->selected_row = 0;
    view->selected_column = 0;
    view->live_active = FALSE;
    view->live_bank = -1;
    view->live_slot = -1;
    view->live_frame = -1;
    gvr_pattern_selection_clear(view);
    gvr_pattern_history_reset(view);
    gvr_pattern_update_target_label(view);
    gvr_pattern_update_adjustment(view);
    gvr_pattern_sync_loop_controls(view);
    gtk_widget_queue_draw(view->area);
}

void gvr_vims_pattern_view_bind_cell(GtkWidget *widget,
                                     int bank,
                                     int slot,
                                     int sample_id,
                                     int sample_type)
{
    GvrVimsPatternView *view;
    GvrVimsPatternCell *cell;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    cell = gvr_pattern_cell(view, bank, slot);
    if(!cell)
        return;

    if(bank == GVR_VIMS_PATTERN_SEQUENCE_BANK) {
        cell->sample_id = -1;
        cell->sample_type = -1;
        return;
    }

    if(sample_id <= 0) {
        if(cell->sample_id > 0 || cell->flags != 0) {
            gvr_pattern_history_invalidate_target(view,
                                                  bank,
                                                  slot);
            gvr_pattern_cell_clear_data(cell);
            cell->sample_id = -1;
            cell->sample_type = -1;
            gvr_pattern_emit_changed(view, bank, slot);
        }
        return;
    }

    if(cell->sample_id > 0 &&
       (cell->sample_id != sample_id ||
        cell->sample_type != sample_type))
    {
        gvr_pattern_history_invalidate_target(view,
                                              bank,
                                              slot);
        gvr_pattern_cell_clear_data(cell);
        gvr_pattern_emit_changed(view, bank, slot);
    }

    cell->sample_id = sample_id;
    cell->sample_type = sample_type;
}

typedef struct {
    int maximum;
} GvrPatternMaximumFrame;

static gboolean gvr_pattern_find_maximum_frame(gpointer key, gpointer value, gpointer data)
{
    (void)value;
    GvrPatternMaximumFrame *maximum = data;
    int frame = GPOINTER_TO_INT(key) - 1;

    if(frame > maximum->maximum)
        maximum->maximum = frame;

    return FALSE;
}

void gvr_vims_pattern_view_select_cell(GtkWidget *widget,
                                       int bank,
                                       int slot,
                                       int sample_id,
                                       int sample_type,
                                       int frame_count)
{
    GvrVimsPatternView *view;
    GvrVimsPatternCell *cell;
    gboolean target_changed;
    int new_frame_count;
    int old_selected_row;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    target_changed =
        view->selected_bank != bank ||
        view->selected_slot != slot ||
        view->selected_sample_id != sample_id ||
        view->selected_sample_type != sample_type;

    gvr_vims_pattern_view_bind_cell(widget,
                                    bank,
                                    slot,
                                    sample_id,
                                    sample_type);

    if(frame_count > 0) {
        new_frame_count =
            gvr_pattern_clampi(frame_count,
                               1,
                               GVR_PATTERN_MAX_FRAMES);
    }
    else {
        int minimum = GVR_PATTERN_DEFAULT_FRAMES;

        cell = gvr_pattern_cell(view, bank, slot);
        if(cell && cell->rows &&
           g_tree_nnodes(cell->rows) > 0)
        {
            GvrPatternMaximumFrame maximum = { -1 };

            g_tree_foreach(
                cell->rows,
                gvr_pattern_find_maximum_frame,
                &maximum);
            if(maximum.maximum >= 0)
                minimum = MAX(minimum,
                              maximum.maximum + 1);
        }

        new_frame_count = minimum;
    }

    gboolean new_frame_count_known = frame_count > 0;

    if(!target_changed &&
       view->frame_count == new_frame_count &&
       view->frame_count_known == new_frame_count_known)
        return;

    old_selected_row = view->selected_row;

    if(target_changed) {
        GvrVimsPatternPlaybackState *state;

        gvr_pattern_clear_fired_highlight(view);

        if(view->inline_editing)
            gvr_pattern_inline_cancel(view);

        view->selected_row = 0;
        view->selected_column = 0;
        gvr_pattern_selection_clear(view);
        gvr_pattern_history_reset(view);

        state = gvr_pattern_playback_state(view,
                                           bank,
                                           slot,
                                           FALSE);
        if(state && state->frame >= 0)
            view->selected_row =
                MAX(0,
                    state->frame /
                    MAX(1, view->row_step));
    }

    view->selected_bank = bank;
    view->selected_slot = slot;
    view->selected_sample_id = sample_id;
    view->selected_sample_type = sample_type;
    view->frame_count = new_frame_count;
    view->frame_count_known = new_frame_count_known;
    view->selected_row =
        gvr_pattern_clampi(
            view->selected_row,
            0,
            MAX(0,
                gvr_pattern_display_row_count(view) - 1));

    gvr_pattern_update_target_label(view);
    gvr_pattern_update_adjustment(view);
    gvr_pattern_sync_loop_controls(view);

    if(target_changed ||
       old_selected_row != view->selected_row)
        gvr_pattern_scroll_to_cursor(view);

    gtk_widget_queue_draw(view->area);
}

void gvr_vims_pattern_view_select_sample(GtkWidget *widget,
                                         int sample_id,
                                         int frame_count)
{
    if(sample_id <= 0)
        return;

    gvr_vims_pattern_view_select_cell(widget,
                                      GVR_VIMS_PATTERN_SAMPLE_BANK,
                                      sample_id,
                                      sample_id,
                                      0,
                                      frame_count);
}

void gvr_vims_pattern_view_select_bank(GtkWidget *widget,
                                       int bank,
                                       int frame_count)
{
    if(bank < 0 || bank >= GVR_VIMS_PATTERN_BANKS)
        return;

    gvr_vims_pattern_view_select_cell(widget,
                                      GVR_VIMS_PATTERN_SEQUENCE_BANK,
                                      bank,
                                      -1,
                                      -1,
                                      frame_count);
}

void gvr_vims_pattern_view_set_live_position(GtkWidget *widget,
                                             int bank,
                                             int slot,
                                             int frame,
                                             gboolean active)
{
    GvrVimsPatternView *view;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    view->live_bank = bank;
    view->live_slot = slot;
    view->live_frame = MAX(0, frame);
    view->live_active = active ? TRUE : FALSE;

    if(view->live_active &&
       bank == view->selected_bank &&
       slot == view->selected_slot &&
       gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->follow_toggle)))
        gvr_pattern_set_cursor(view,
                               gvr_pattern_follow_frame(
                                   view,
                                   view->live_frame),
                               view->selected_column);
    else
        gtk_widget_queue_draw(view->area);
}

void gvr_vims_pattern_view_set_description_lookup(
        GtkWidget *widget,
        GvrVimsPatternDescriptionLookup lookup,
        gpointer user_data,
        GDestroyNotify destroy_notify)
{
    GvrVimsPatternView *view;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);

    if(view->description_lookup_destroy)
        view->description_lookup_destroy(view->description_lookup_data);

    view->description_lookup = lookup;
    view->description_lookup_data = user_data;
    view->description_lookup_destroy = destroy_notify;

    gvr_pattern_relabel_all(view);

    if(view->area)
        gtk_widget_trigger_tooltip_query(view->area);
}

int gvr_vims_pattern_view_get_edit_step(GtkWidget *widget)
{
    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return 1;

    return gvr_pattern_edit_step(GVR_VIMS_PATTERN_VIEW(widget));
}

void gvr_vims_pattern_view_set_edit_step(GtkWidget *widget, int step)
{
    GvrVimsPatternView *view;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    step = gvr_pattern_clampi(step, 1, 999);
    gvr_pattern_apply_step(view, step);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(view->step_spin), step);
}

gboolean gvr_vims_pattern_view_get_learning(GtkWidget *widget)
{
    return GVR_IS_VIMS_PATTERN_VIEW(widget) &&
           gtk_toggle_button_get_active(
               GTK_TOGGLE_BUTTON(GVR_VIMS_PATTERN_VIEW(widget)->learn_toggle));
}

gboolean gvr_vims_pattern_view_insert_message(GtkWidget *widget,
                                                    const char *message,
                                                    int frame,
                                                    int column,
                                                    gboolean advance)
{
    GvrVimsPatternView *view;
    GvrVimsPatternCell *cell;
    GvrVimsPatternRow *row;
    GvrVimsPatternEvent *event;
    char *normalized;
    int id;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return FALSE;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    if(!gvr_pattern_target_editable(view))
        return FALSE;

    normalized = gvr_pattern_normalize_message(message);
    id = gvr_pattern_parse_id(normalized);
    if(!normalized || id < 0) {
        g_free(normalized);
        return FALSE;
    }

    if(frame < 0)
        frame = gvr_pattern_current_frame(view);
    if(column < 0)
        column = view->selected_column;

    frame = view->frame_count_known ?
            gvr_pattern_clampi(frame, 0, view->frame_count - 1) :
            gvr_pattern_clampi(frame, 0, GVR_PATTERN_MAX_FRAMES - 1);
    column = gvr_pattern_clampi(column,
                                0,
                                GVR_VIMS_PATTERN_COLUMNS - 1);

    if(!gvr_pattern_push_undo(view)) {
        g_free(normalized);
        return FALSE;
    }

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);
    row = gvr_pattern_row_get(cell, frame, TRUE);
    event = &row->events[column];
    gvr_pattern_event_clear(event);
    event->vims_id = id;
    event->message = normalized;
    gvr_pattern_format_label(view,
                             id,
                             normalized,
                             event->label,
                             sizeof(event->label));

    if(!view->frame_count_known && frame + 1 > view->frame_count)
        view->frame_count = frame + 1;

    gvr_pattern_emit_changed(view,
                             view->selected_bank,
                             view->selected_slot);
    gvr_pattern_selection_clear(view);
    gvr_pattern_set_cursor(view, frame, column);

    if(advance)
        gvr_pattern_set_cursor_row(view,
                                   view->selected_row + 1,
                                   column);

    return TRUE;
}

gboolean gvr_vims_pattern_view_insert_message_auto_column(
        GtkWidget *widget,
        const char *message,
        int frame,
        gboolean advance)
{
    GvrVimsPatternView *view;
    GvrVimsPatternCell *cell;
    GvrVimsPatternRow *row;
    char *normalized;
    int id;
    int matching_column = -1;
    int first_empty = -1;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return FALSE;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    if(!gvr_pattern_target_editable(view))
        return FALSE;

    normalized = gvr_pattern_normalize_message(message);
    if(!normalized)
        return FALSE;

    id = gvr_pattern_parse_id(normalized);
    if(id < 0) {
        g_free(normalized);
        return FALSE;
    }

    if(frame < 0)
        frame = gvr_pattern_current_frame(view);

    frame = view->frame_count_known ?
            gvr_pattern_clampi(frame, 0, view->frame_count - 1) :
            gvr_pattern_clampi(frame, 0, GVR_PATTERN_MAX_FRAMES - 1);

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);
    row = gvr_pattern_row_get(cell, frame, FALSE);

    if(row) {
        for(int column = 0;
            column < GVR_VIMS_PATTERN_COLUMNS;
            column++)
        {
            GvrVimsPatternEvent *event = &row->events[column];

            if(event->message) {
                if(matching_column < 0 && event->vims_id == id)
                    matching_column = column;
            }
            else if(first_empty < 0) {
                first_empty = column;
            }
        }
    }
    else {
        first_empty = 0;
    }

    g_free(normalized);

    if(matching_column >= 0)
        return gvr_vims_pattern_view_insert_message(widget,
                                                    message,
                                                    frame,
                                                    matching_column,
                                                    advance);

    if(first_empty >= 0)
        return gvr_vims_pattern_view_insert_message(widget,
                                                    message,
                                                    frame,
                                                    first_empty,
                                                    advance);

    return FALSE;
}

static void gvr_pattern_drag_data_received(GtkWidget *widget,
                                           GdkDragContext *context,
                                           gint x,
                                           gint y,
                                           GtkSelectionData *selection,
                                           guint info,
                                           guint time,
                                           gpointer user_data)
{
    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(user_data);
    gchar *message = NULL;
    int frame;
    int column;
    gboolean accepted = FALSE;

    (void)widget;

    if(info == 0) {
        const guchar *payload =
            gtk_selection_data_get_data(selection);
        const gint payload_length =
            gtk_selection_data_get_length(selection);

        if(payload && payload_length > 0)
            message = g_strndup(
                (const gchar *)payload,
                (gsize)payload_length);
    }
    else {
        message =
            (gchar *)gtk_selection_data_get_text(selection);
    }

    if(message &&
       gvr_pattern_position_from_xy(view,
                                    x,
                                    y,
                                    &frame,
                                    &column))
    {
        accepted = gvr_vims_pattern_view_insert_message(
            GTK_WIDGET(view),
            message,
            frame,
            column,
            FALSE);
    }

    g_free(message);
    gtk_drag_finish(context, accepted, FALSE, time);
}

gboolean gvr_vims_pattern_view_capture_message(GtkWidget *widget,
                                                const char *message,
                                                int live_frame)
{
    GvrVimsPatternView *view;
    GvrVimsPatternCell *cell;
    GvrVimsPatternRow *row;
    GvrVimsPatternEvent *event;
    char *normalized;
    int id;
    int frame;
    int target_column;
    int matching_column = -1;
    int first_empty = -1;
    int next_empty = -1;
    int next_row;
    int next_column;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return FALSE;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    if(!gvr_vims_pattern_view_get_learning(widget) ||
       !gvr_pattern_target_editable(view))
        return FALSE;

    normalized = gvr_pattern_normalize_message(message);
    id = gvr_pattern_parse_id(normalized);
    if(!normalized || id < 0) {
        g_free(normalized);
        return FALSE;
    }

    cell = gvr_pattern_cell(view,
                            view->selected_bank,
                            view->selected_slot);

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->follow_toggle)) &&
       live_frame >= 0 &&
       view->live_active &&
       view->live_bank == view->selected_bank &&
       view->live_slot == view->selected_slot)
    {
        frame = gvr_pattern_follow_frame(view, live_frame);
        gvr_pattern_set_cursor(view, frame, view->selected_column);
    }
    else {
        frame = gvr_pattern_current_frame(view);
    }

    frame = view->frame_count_known ?
            gvr_pattern_clampi(frame, 0, view->frame_count - 1) :
            gvr_pattern_clampi(frame, 0, GVR_PATTERN_MAX_FRAMES - 1);
    row = gvr_pattern_row_get(cell, frame, FALSE);

    if(row) {
        for(int column = 0;
            column < GVR_VIMS_PATTERN_COLUMNS;
            column++)
        {
            event = &row->events[column];
            if(!event->message) {
                if(first_empty < 0)
                    first_empty = column;
                continue;
            }

            if(matching_column < 0 && event->vims_id == id)
                matching_column = column;
        }
    }
    else {
        first_empty = 0;
    }

    target_column = view->selected_column;
    next_row = view->selected_row;
    next_column = view->selected_column;

    switch(view->learn_policy) {
        case GVR_PATTERN_LEARN_COLUMN_LOCK: {
            const int last_row =
                MAX(0, gvr_pattern_display_row_count(view) - 1);
            int locked_row = view->selected_row;

            target_column = view->selected_column;
            while(locked_row <= last_row) {
                GvrVimsPatternRow *locked_pattern_row;
                GvrVimsPatternEvent *locked_event;

                frame = gvr_pattern_display_row_to_frame(view,
                                                         locked_row);
                locked_pattern_row =
                    gvr_pattern_row_get(cell, frame, FALSE);
                locked_event = locked_pattern_row ?
                    &locked_pattern_row->events[target_column] :
                    NULL;

                if(!locked_event ||
                   !locked_event->message ||
                   locked_event->vims_id == id)
                    break;

                locked_row++;
            }

            if(locked_row > last_row)
                locked_row = last_row;

            frame = gvr_pattern_display_row_to_frame(view,
                                                     locked_row);
            next_row = locked_row + 1;
            break;
        }

        case GVR_PATTERN_LEARN_NEXT_FREE:
            if(first_empty >= 0) {
                target_column = first_empty;
            }
            else {
                next_row = view->selected_row + 1;
                frame = gvr_pattern_display_row_to_frame(view, next_row);
                row = gvr_pattern_row_get(cell, frame, FALSE);
                target_column = 0;
                if(row) {
                    for(int column = 0;
                        column < GVR_VIMS_PATTERN_COLUMNS;
                        column++)
                    {
                        if(!row->events[column].message) {
                            target_column = column;
                            break;
                        }
                    }
                }
            }
            break;

        case GVR_PATTERN_LEARN_REPLACE_MATCH:
            if(matching_column >= 0)
                target_column = matching_column;
            else if(first_empty >= 0)
                target_column = first_empty;
            else
                target_column = view->selected_column;
            break;

        case GVR_PATTERN_LEARN_OVERWRITE:
            target_column = view->selected_column;
            next_row = view->selected_row + 1;
            break;

        case GVR_PATTERN_LEARN_LAYER:
            target_column = view->selected_column;
            next_column = view->selected_column + 1;
            if(next_column >= GVR_VIMS_PATTERN_COLUMNS) {
                next_column = 0;
                next_row = view->selected_row + 1;
            }
            break;

        default:
            if(matching_column >= 0)
                target_column = matching_column;
            else if(first_empty >= 0)
                target_column = first_empty;
            break;
    }

    if(!gvr_pattern_push_undo(view)) {
        g_free(normalized);
        return FALSE;
    }

    row = gvr_pattern_row_get(cell, frame, TRUE);
    event = &row->events[target_column];
    gvr_pattern_event_clear(event);
    event->vims_id = id;
    event->message = normalized;
    gvr_pattern_format_label(view,
                             id,
                             normalized,
                             event->label,
                             sizeof(event->label));

    if(!view->frame_count_known && frame + 1 > view->frame_count)
        view->frame_count = frame + 1;

    if(view->learn_policy == GVR_PATTERN_LEARN_NEXT_FREE ||
       view->learn_policy == GVR_PATTERN_LEARN_REPLACE_MATCH)
    {
        for(int column = target_column + 1;
            column < GVR_VIMS_PATTERN_COLUMNS;
            column++)
        {
            if(!row->events[column].message) {
                next_empty = column;
                break;
            }
        }

        if(next_empty < 0) {
            for(int column = 0;
                column < target_column;
                column++)
            {
                if(!row->events[column].message) {
                    next_empty = column;
                    break;
                }
            }
        }

        if(view->learn_policy == GVR_PATTERN_LEARN_NEXT_FREE) {
            if(next_empty >= 0) {
                next_row = gvr_pattern_frame_to_display_row(view, frame);
                next_column = next_empty;
            }
            else {
                next_row = gvr_pattern_frame_to_display_row(view, frame) + 1;
                next_column = 0;
            }
        }
        else {
            next_row = gvr_pattern_frame_to_display_row(view, frame);
            next_column = target_column;
        }
    }
    else if(view->learn_policy == GVR_PATTERN_LEARN_COLUMN_LOCK ||
            view->learn_policy == GVR_PATTERN_LEARN_OVERWRITE)
    {
        next_column = target_column;
    }

    gvr_pattern_emit_changed(view,
                             view->selected_bank,
                             view->selected_slot);
    gvr_pattern_selection_clear(view);
    gvr_pattern_set_cursor_row(view,
                               next_row,
                               next_column);
    return TRUE;
}

typedef struct {
    int from;
    int to;
    GPtrArray *rows;
} GvrPatternFireRange;

static gboolean gvr_pattern_collect_fire_rows(gpointer key, gpointer value, gpointer data)
{
    GvrPatternFireRange *range = data;
    int frame = GPOINTER_TO_INT(key) - 1;

    if(frame > range->to)
        return TRUE;

    if(frame >= range->from && frame <= range->to)
        g_ptr_array_add(range->rows, value);

    return FALSE;
}

static void gvr_pattern_fire_range(GvrVimsPatternView *view,
                                   int bank,
                                   int slot,
                                   int from,
                                   int to,
                                   gboolean reverse)
{
    GvrVimsPatternCell *cell = gvr_pattern_cell(view, bank, slot);
    GvrPatternFireRange range;

    if(!cell || !cell->rows || from > to)
        return;

    range.from = from;
    range.to = to;
    range.rows = g_ptr_array_new();
    g_tree_foreach(cell->rows, gvr_pattern_collect_fire_rows, &range);

    if(reverse) {
        for(int index = (int)range.rows->len - 1; index >= 0; index--) {
            GvrVimsPatternRow *row = g_ptr_array_index(range.rows, index);
            guint fired_mask = 0;

            for(int column = 0; column < GVR_VIMS_PATTERN_COLUMNS; column++) {
                if((view->enabled_columns_mask & (1u << column)) &&
                   row->events[column].message)
                {
                    fired_mask |= 1u << column;
                }
            }

            if(fired_mask) {
                view->last_fired_bank = bank;
                view->last_fired_slot = slot;
                view->last_fired_frame = row->frame;
                view->last_fired_mask = fired_mask;
            }
        }
    }
    else {
        for(guint index = 0; index < range.rows->len; index++) {
            GvrVimsPatternRow *row = g_ptr_array_index(range.rows, index);
            guint fired_mask = 0;

            for(int column = 0; column < GVR_VIMS_PATTERN_COLUMNS; column++) {
                if((view->enabled_columns_mask & (1u << column)) &&
                   row->events[column].message)
                {
                    fired_mask |= 1u << column;
                }
            }

            if(fired_mask) {
                view->last_fired_bank = bank;
                view->last_fired_slot = slot;
                view->last_fired_frame = row->frame;
                view->last_fired_mask = fired_mask;
            }
        }
    }

    g_ptr_array_free(range.rows, TRUE);
    if(view->area)
        gtk_widget_queue_draw(view->area);
}

void gvr_vims_pattern_view_set_transport(GtkWidget *widget,
                                          guint epoch,
                                          int direction,
                                          int loop_type,
                                          int loops_remaining)
{
    GvrVimsPatternView *view;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    view->transport_valid = TRUE;
    view->transport_epoch = epoch;
    view->transport_direction = direction < 0 ? -1 : direction > 0 ? 1 : 0;
    view->transport_loop_type = loop_type;
    view->transport_loops_remaining = loops_remaining;
}

void gvr_vims_pattern_view_update_playback(GtkWidget *widget,
                                           int bank,
                                           int slot,
                                           int source_frame,
                                           gboolean active,
                                           int max_linear_delta)
{
    GvrVimsPatternView *view;
    GvrVimsPatternPlaybackState *state;
    int frame;
    int source_delta;
    int mapped_delta;
    gboolean selected_target;
    gboolean transport_changed;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    selected_target = bank == view->selected_bank &&
                      slot == view->selected_slot;
    source_frame = MAX(0, source_frame);

    if(active && selected_target &&
       view->frame_count_known && view->frame_count > 0)
        source_frame = MIN(source_frame, view->frame_count - 1);

    if(selected_target)
        gvr_vims_pattern_view_set_live_position(widget,
                                                bank,
                                                slot,
                                                source_frame,
                                                active);

    if(!active || !gvr_pattern_target_valid(bank, slot)) {
        gvr_pattern_clear_fired_highlight_for(view, bank, slot);
        gvr_pattern_playback_remove(view, bank, slot);
        if(selected_target)
            gvr_vims_pattern_view_set_live_position(widget,
                                                    bank,
                                                    slot,
                                                    0,
                                                    FALSE);
        return;
    }

    frame = source_frame;

    state = gvr_pattern_playback_state(view, bank, slot, TRUE);
    if(!state)
        return;

    if(state->source_frame >= 0 &&
       source_frame != state->source_frame)
        gvr_pattern_clear_fired_highlight_for(view, bank, slot);

    if(selected_target && state->frame < 0)
        gvr_pattern_set_cursor(view,
                               frame,
                               view->selected_column);

    transport_changed = view->transport_valid &&
                        state->transport_valid &&
                        (state->transport_epoch != view->transport_epoch ||
                         state->direction != view->transport_direction ||
                         state->loop_type != view->transport_loop_type ||
                         state->loops_remaining != view->transport_loops_remaining);

    if(transport_changed) {
        const gboolean moved = state->source_frame >= 0 &&
                               source_frame != state->source_frame;
        const gboolean paused_seek = state->paused &&
                                     (state->seeked_while_paused || moved);

        if(max_linear_delta <= 0) {
            state->paused = TRUE;
            state->seeked_while_paused = paused_seek;
        }
        else {
            if(moved || paused_seek)
                gvr_pattern_fire_range(view, bank, slot, frame, frame, FALSE);
            state->paused = FALSE;
            state->seeked_while_paused = FALSE;
        }

        state->frame = frame;
        state->source_frame = source_frame;
        state->transport_valid = TRUE;
        state->transport_epoch = view->transport_epoch;
        state->direction = view->transport_direction;
        state->loop_type = view->transport_loop_type;
        state->loops_remaining = view->transport_loops_remaining;
        return;
    }

    if(view->transport_valid) {
        state->transport_valid = TRUE;
        state->transport_epoch = view->transport_epoch;
        state->direction = view->transport_direction;
        state->loop_type = view->transport_loop_type;
        state->loops_remaining = view->transport_loops_remaining;
    }

    if(max_linear_delta <= 0) {
        if(state->source_frame < 0)
            state->seeked_while_paused = TRUE;
        else if(state->paused && source_frame != state->source_frame)
            state->seeked_while_paused = TRUE;
        else if(!state->paused)
            state->seeked_while_paused = FALSE;

        state->frame = frame;
        state->source_frame = source_frame;
        state->paused = TRUE;
        return;
    }

    max_linear_delta = MAX(1, max_linear_delta);

    if(state->paused) {
        source_delta = source_frame - state->source_frame;
        mapped_delta = frame - state->frame;

        if(state->seeked_while_paused) {
            gvr_pattern_fire_range(view, bank, slot, frame, frame, FALSE);
        }
        else if(mapped_delta > 0 && mapped_delta <= max_linear_delta) {
            gvr_pattern_fire_range(view,
                                   bank,
                                   slot,
                                   state->frame + 1,
                                   frame,
                                   FALSE);
        }
        else if(mapped_delta < 0 && -mapped_delta <= max_linear_delta) {
            gvr_pattern_fire_range(view,
                                   bank,
                                   slot,
                                   frame,
                                   state->frame - 1,
                                   TRUE);
        }
        else if(mapped_delta != 0) {
            gvr_pattern_fire_range(view, bank, slot, frame, frame, FALSE);
        }

        state->frame = frame;
        state->source_frame = source_frame;
        state->paused = FALSE;
        state->seeked_while_paused = FALSE;
        return;
    }

    if(state->frame < 0 || state->source_frame < 0) {
        gvr_pattern_fire_range(view, bank, slot, frame, frame, FALSE);
        state->frame = frame;
        state->source_frame = source_frame;
        state->paused = FALSE;
        state->seeked_while_paused = FALSE;
        return;
    }

    if(source_frame == state->source_frame)
        return;

    source_delta = source_frame - state->source_frame;
    mapped_delta = frame - state->frame;

    if(ABS(source_delta) <= max_linear_delta) {
        if(mapped_delta > 0)
            gvr_pattern_fire_range(view,
                                   bank,
                                   slot,
                                   state->frame + 1,
                                   frame,
                                   FALSE);
        else if(mapped_delta < 0)
            gvr_pattern_fire_range(view,
                                   bank,
                                   slot,
                                   frame,
                                   state->frame - 1,
                                   TRUE);
    }
    else {
        gvr_pattern_fire_range(view, bank, slot, frame, frame, FALSE);
    }

    state->frame = frame;
    state->source_frame = source_frame;
}

void gvr_vims_pattern_view_update_sample_playback(GtkWidget *widget,
                                                  int sample_id,
                                                  int frame,
                                                  gboolean active,
                                                  int max_linear_delta)
{
    gvr_vims_pattern_view_update_playback(widget,
                                          GVR_VIMS_PATTERN_SAMPLE_BANK,
                                          sample_id,
                                          frame,
                                          active && sample_id > 0,
                                          max_linear_delta);
}

void gvr_vims_pattern_view_update_bank_playback(GtkWidget *widget,
                                                int bank,
                                                int frame,
                                                gboolean active,
                                                int max_linear_delta)
{
    gvr_vims_pattern_view_update_playback(widget,
                                          GVR_VIMS_PATTERN_SEQUENCE_BANK,
                                          bank,
                                          frame,
                                          active && bank >= 0 &&
                                          bank < GVR_VIMS_PATTERN_BANKS,
                                          max_linear_delta);
}

void gvr_vims_pattern_view_stop_all_playback(GtkWidget *widget)
{
    GvrVimsPatternView *view;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    g_hash_table_remove_all(view->playback_states);
    view->live_active = FALSE;
    view->live_bank = -1;
    view->live_slot = -1;
    view->live_frame = -1;
    gvr_pattern_clear_fired_highlight(view);
    gtk_widget_queue_draw(view->area);
}

void gvr_vims_pattern_view_clear_cell(GtkWidget *widget,
                                      int bank,
                                      int slot)
{
    GvrVimsPatternView *view;
    GvrVimsPatternCell *cell;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    cell = gvr_pattern_cell(view, bank, slot);
    if(!cell)
        return;

    gvr_pattern_history_invalidate_target(view,
                                          bank,
                                          slot);
    gvr_pattern_cell_clear_data(cell);
    gvr_pattern_emit_changed(view, bank, slot);
    if(bank == view->selected_bank && slot == view->selected_slot)
        gvr_pattern_sync_loop_controls(view);
}

void gvr_vims_pattern_view_clear_bank(GtkWidget *widget, int bank)
{
    if(!GVR_IS_VIMS_PATTERN_VIEW(widget) || bank < 0 || bank >= GVR_VIMS_PATTERN_BANKS)
        return;

    GvrVimsPatternView *view = GVR_VIMS_PATTERN_VIEW(widget);
    for(int slot = 0; slot < GVR_VIMS_PATTERN_SLOTS; slot++)
        gvr_vims_pattern_view_clear_cell(widget, bank, slot);

    gvr_vims_pattern_view_clear_cell(widget,
                                     GVR_VIMS_PATTERN_SEQUENCE_BANK,
                                     bank);

    if(view->selected_bank == bank ||
       (view->selected_bank == GVR_VIMS_PATTERN_SEQUENCE_BANK &&
        view->selected_slot == bank))
        gtk_widget_queue_draw(view->area);
}

void gvr_vims_pattern_view_clear_all(GtkWidget *widget)
{
    GvrVimsPatternView *view;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);

    for(int bank = 0; bank < GVR_VIMS_PATTERN_BANKS; bank++)
        gvr_vims_pattern_view_clear_bank(widget, bank);

    if(view->selected_bank == GVR_VIMS_PATTERN_SAMPLE_BANK ||
       view->selected_bank == GVR_VIMS_PATTERN_STREAM_BANK)
    {
        gvr_pattern_history_reset(view);
        gvr_pattern_selection_clear(view);
    }

    g_hash_table_remove_all(view->sample_cells);
    g_hash_table_remove_all(view->stream_cells);
    gvr_vims_pattern_view_stop_all_playback(widget);
    gvr_pattern_sync_loop_controls(view);
    gtk_widget_queue_draw(view->area);
}

void gvr_vims_pattern_view_swap_cells(GtkWidget *widget,
                                      int bank,
                                      int first_slot,
                                      int second_slot)
{
    GvrVimsPatternView *view;
    GvrVimsPatternCell temporary;
    GvrVimsPatternCell *first;
    GvrVimsPatternCell *second;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget) ||
       first_slot == second_slot)
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    first = gvr_pattern_cell(view, bank, first_slot);
    second = gvr_pattern_cell(view, bank, second_slot);
    if(!first || !second)
        return;

    gvr_pattern_history_invalidate_target(view,
                                          bank,
                                          first_slot);
    gvr_pattern_history_invalidate_target(view,
                                          bank,
                                          second_slot);

    temporary = *first;
    *first = *second;
    *second = temporary;

    gvr_pattern_emit_changed(view, bank, first_slot);
    gvr_pattern_emit_changed(view, bank, second_slot);
}

void gvr_vims_pattern_view_copy_cell(GtkWidget *widget,
                                     int src_bank,
                                     int src_slot,
                                     int dst_bank,
                                     int dst_slot)
{
    GvrVimsPatternView *view;
    GvrVimsPatternCell *src;
    GvrVimsPatternCell *dst;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    src = gvr_pattern_cell(view, src_bank, src_slot);
    dst = gvr_pattern_cell(view, dst_bank, dst_slot);
    if(!src || !dst || src == dst)
        return;

    gvr_pattern_history_invalidate_target(view,
                                          dst_bank,
                                          dst_slot);
    gvr_pattern_cell_clone(dst, src);
    gvr_pattern_emit_changed(view, dst_bank, dst_slot);
}

void gvr_vims_pattern_view_copy_bank(GtkWidget *widget,
                                     int src_bank,
                                     int dst_bank)
{
    if(!GVR_IS_VIMS_PATTERN_VIEW(widget) ||
       src_bank < 0 || src_bank >= GVR_VIMS_PATTERN_BANKS ||
       dst_bank < 0 || dst_bank >= GVR_VIMS_PATTERN_BANKS ||
       src_bank == dst_bank)
        return;

    for(int slot = 0; slot < GVR_VIMS_PATTERN_SLOTS; slot++)
        gvr_vims_pattern_view_copy_cell(widget, src_bank, slot, dst_bank, slot);

    gvr_vims_pattern_view_copy_cell(widget,
                                    GVR_VIMS_PATTERN_SEQUENCE_BANK,
                                    src_bank,
                                    GVR_VIMS_PATTERN_SEQUENCE_BANK,
                                    dst_bank);
}

guint gvr_vims_pattern_view_get_cell_flags(GtkWidget *widget,
                                            int bank,
                                            int slot)
{
    GvrVimsPatternSummary summary;

    if(!gvr_vims_pattern_view_get_cell_summary(widget, bank, slot, &summary))
        return 0;

    return summary.flags;
}

gboolean gvr_vims_pattern_view_get_cell_summary(GtkWidget *widget,
                                                 int bank,
                                                 int slot,
                                                 GvrVimsPatternSummary *summary)
{
    GvrVimsPatternCell *cell;

    if(summary)
        memset(summary, 0, sizeof(*summary));

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return FALSE;

    cell = gvr_pattern_cell_lookup(GVR_VIMS_PATTERN_VIEW(widget), bank, slot);
    return gvr_pattern_cell_summary(cell, summary);
}

gboolean gvr_vims_pattern_view_get_source_summary(GtkWidget *widget,
                                                   int sample_id,
                                                   int sample_type,
                                                   GvrVimsPatternSummary *summary)
{
    int bank;

    if(summary)
        memset(summary, 0, sizeof(*summary));

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget) || sample_id <= 0)
        return FALSE;

    bank = sample_type == 0 ?
           GVR_VIMS_PATTERN_SAMPLE_BANK : GVR_VIMS_PATTERN_STREAM_BANK;

    return gvr_pattern_cell_summary(
        gvr_pattern_cell_lookup(GVR_VIMS_PATTERN_VIEW(widget), bank, sample_id),
        summary);
}

gboolean gvr_vims_pattern_view_get_bank_summary(GtkWidget *widget,
                                                 int bank,
                                                 GvrVimsPatternSummary *summary)
{
    if(summary)
        memset(summary, 0, sizeof(*summary));

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget) ||
       bank < 0 || bank >= GVR_VIMS_PATTERN_BANKS)
        return FALSE;

    return gvr_pattern_cell_summary(
        gvr_pattern_cell_lookup(GVR_VIMS_PATTERN_VIEW(widget),
                                GVR_VIMS_PATTERN_SEQUENCE_BANK,
                                bank),
        summary);
}


typedef struct {
    GString *output;
    int bank;
    int slot;
    int sample_id;
    int sample_type;
} GvrPatternSerializeContext;

static gboolean gvr_pattern_serialize_row(gpointer key,
                                          gpointer value,
                                          gpointer data)
{
    GvrPatternSerializeContext *context = data;
    GvrVimsPatternRow *row = value;
    int frame = GPOINTER_TO_INT(key) - 1;

    if(!context || !context->output || !row)
        return FALSE;

    for(int column = 0; column < GVR_VIMS_PATTERN_COLUMNS; column++) {
        GvrVimsPatternEvent *event = &row->events[column];
        gchar *label_b64;
        gchar *message_b64;

        if(!event->message)
            continue;

        label_b64 = g_base64_encode((const guchar *)event->label,
                                    strlen(event->label));
        message_b64 = g_base64_encode((const guchar *)event->message,
                                      strlen(event->message));

        g_string_append_printf(context->output,
                               "E\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
                               context->bank,
                               context->slot,
                               context->sample_id,
                               context->sample_type,
                               frame,
                               column,
                               event->vims_id,
                               label_b64 ? label_b64 : "",
                               message_b64 ? message_b64 : "");

        g_free(label_b64);
        g_free(message_b64);
    }

    return FALSE;
}

static void gvr_pattern_serialize_cell(GString *output,
                                       GvrVimsPatternCell *cell,
                                       int bank,
                                       int slot)
{
    GvrPatternSerializeContext context;

    if(!output || !cell)
        return;

    if(cell->loop_start >= 0 && cell->loop_end >= cell->loop_start)
        g_string_append_printf(output,
                               "L\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                               bank,
                               slot,
                               cell->sample_id,
                               cell->sample_type,
                               cell->loop_enabled ? 1 : 0,
                               cell->loop_start,
                               cell->loop_end);

    if(!cell->rows || g_tree_nnodes(cell->rows) <= 0)
        return;

    context.output = output;
    context.bank = bank;
    context.slot = slot;
    context.sample_id = cell->sample_id;
    context.sample_type = cell->sample_type;
    g_tree_foreach(cell->rows, gvr_pattern_serialize_row, &context);
}

typedef struct {
    GString *output;
    int bank;
} GvrPatternSerializeHashContext;

static void gvr_pattern_serialize_hash_cell(gpointer key,
                                            gpointer value,
                                            gpointer user_data)
{
    GvrPatternSerializeHashContext *context = user_data;
    int slot = GPOINTER_TO_INT(key);

    if(!context)
        return;

    gvr_pattern_serialize_cell(context->output,
                               value,
                               context->bank,
                               slot);
}

gchar *gvr_vims_pattern_view_serialize(GtkWidget *widget, gsize *length)
{
    GvrVimsPatternView *view;
    GString *output;
    GvrPatternSerializeHashContext hash_context;

    if(length)
        *length = 0;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return NULL;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    output = g_string_new("GVR-VIMS-PATTERN\t3\n");
    g_string_append_printf(output,
                           "M\t%u\n",
                           view->enabled_columns_mask &
                           GVR_PATTERN_ALL_COLUMNS_MASK);
    g_string_append_printf(output,
                           "P\t%d\n",
                           view->learn_policy);
    g_string_append_printf(output,
                           "Q\t%d\n",
                           view->paste_mode);

    for(int bank = 0; bank < GVR_VIMS_PATTERN_BANKS; bank++) {
        for(int slot = 0; slot < GVR_VIMS_PATTERN_SLOTS; slot++)
            gvr_pattern_serialize_cell(output,
                                       &view->cells[bank][slot],
                                       bank,
                                       slot);

        gvr_pattern_serialize_cell(output,
                                   &view->bank_cells[bank],
                                   GVR_VIMS_PATTERN_SEQUENCE_BANK,
                                   bank);
    }

    hash_context.output = output;
    hash_context.bank = GVR_VIMS_PATTERN_SAMPLE_BANK;
    g_hash_table_foreach(view->sample_cells,
                         gvr_pattern_serialize_hash_cell,
                         &hash_context);

    hash_context.bank = GVR_VIMS_PATTERN_STREAM_BANK;
    g_hash_table_foreach(view->stream_cells,
                         gvr_pattern_serialize_hash_cell,
                         &hash_context);

    if(length)
        *length = output->len;

    return g_string_free(output, FALSE);
}

static gboolean gvr_pattern_parse_int(const gchar *text, int *value)
{
    gchar *end = NULL;
    gint64 parsed;

    if(!text || !*text || !value)
        return FALSE;

    parsed = g_ascii_strtoll(text, &end, 10);
    if(end == text || *end != '\0' || parsed < G_MININT || parsed > G_MAXINT)
        return FALSE;

    *value = (int)parsed;
    return TRUE;
}

static void gvr_pattern_clear_document(GvrVimsPatternView *view)
{
    if(!view)
        return;

    for(int bank = 0;
        bank < GVR_VIMS_PATTERN_BANKS;
        bank++)
    {
        for(int slot = 0;
            slot < GVR_VIMS_PATTERN_SLOTS;
            slot++)
        {
            gvr_pattern_cell_clear_data(
                &view->cells[bank][slot]);
        }

        gvr_pattern_cell_clear_data(
            &view->bank_cells[bank]);
    }

    g_hash_table_remove_all(view->sample_cells);
    g_hash_table_remove_all(view->stream_cells);
    g_hash_table_remove_all(view->clipboard);
    g_hash_table_remove_all(view->playback_states);
    gvr_pattern_block_clipboard_clear(
        &view->block_clipboard);
    gvr_pattern_history_reset(view);
    gvr_pattern_selection_clear(view);
    gvr_pattern_sync_loop_controls(view);
}

gboolean gvr_vims_pattern_view_deserialize(GtkWidget *widget,
                                           const gchar *data,
                                           gsize length)
{
    GvrVimsPatternView *view;
    gchar *document;
    gchar **lines;
    gboolean valid = FALSE;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget) || !data)
        return FALSE;

    document = g_strndup(data, length);
    lines = g_strsplit(document, "\n", -1);

    if(!lines[0] ||
       (strcmp(lines[0], "GVR-VIMS-PATTERN\t1") != 0 &&
        strcmp(lines[0], "GVR-VIMS-PATTERN\t2") != 0 &&
        strcmp(lines[0], "GVR-VIMS-PATTERN\t3") != 0))
        goto done;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    gvr_pattern_clear_document(view);
    view->enabled_columns_mask = GVR_PATTERN_ALL_COLUMNS_MASK;
    view->learn_policy = GVR_PATTERN_LEARN_REPLACE_MATCH;
    view->paste_mode = GVR_PATTERN_PASTE_REPLACE;

    for(int line_index = 1; lines[line_index]; line_index++) {
        gchar **fields;
        int bank;
        int slot;
        int sample_id;
        int sample_type;
        int frame;
        int column;
        int vims_id;
        gsize label_length = 0;
        gsize message_length = 0;
        guchar *label_data;
        guchar *message_data;
        GvrVimsPatternCell *cell;
        GvrVimsPatternRow *row;
        GvrVimsPatternEvent *event;

        if(lines[line_index][0] == '\0')
            continue;

        if(g_str_has_prefix(lines[line_index], "M\t")) {
            int mask;

            if(gvr_pattern_parse_int(lines[line_index] + 2, &mask))
                view->enabled_columns_mask =
                    (guint)mask & GVR_PATTERN_ALL_COLUMNS_MASK;
            continue;
        }

        if(g_str_has_prefix(lines[line_index], "P\t")) {
            int policy;

            if(gvr_pattern_parse_int(lines[line_index] + 2, &policy) &&
               policy >= 0 &&
               policy < GVR_PATTERN_LEARN_POLICY_COUNT)
                view->learn_policy = policy;
            continue;
        }

        if(g_str_has_prefix(lines[line_index], "Q\t")) {
            int paste_mode;

            if(gvr_pattern_parse_int(
                   lines[line_index] + 2,
                   &paste_mode) &&
               paste_mode >= 0 &&
               paste_mode < GVR_PATTERN_PASTE_MODE_COUNT)
                view->paste_mode = paste_mode;
            continue;
        }

        if(g_str_has_prefix(lines[line_index], "L\t")) {
            gchar **loop_fields =
                g_strsplit(lines[line_index], "\t", 8);
            int loop_bank;
            int loop_slot;
            int loop_sample_id;
            int loop_sample_type;
            int loop_enabled;
            int loop_start;
            int loop_end;

            if(g_strv_length(loop_fields) == 8 &&
               gvr_pattern_parse_int(loop_fields[1], &loop_bank) &&
               gvr_pattern_parse_int(loop_fields[2], &loop_slot) &&
               gvr_pattern_parse_int(loop_fields[3], &loop_sample_id) &&
               gvr_pattern_parse_int(loop_fields[4], &loop_sample_type) &&
               gvr_pattern_parse_int(loop_fields[5], &loop_enabled) &&
               gvr_pattern_parse_int(loop_fields[6], &loop_start) &&
               gvr_pattern_parse_int(loop_fields[7], &loop_end) &&
               gvr_pattern_target_valid(loop_bank, loop_slot) &&
               loop_start >= 0 &&
               loop_end >= loop_start &&
               loop_end < GVR_PATTERN_MAX_FRAMES)
            {
                GvrVimsPatternCell *loop_cell =
                    gvr_pattern_cell(view, loop_bank, loop_slot);

                if(loop_bank != GVR_VIMS_PATTERN_SEQUENCE_BANK &&
                   loop_sample_id > 0)
                {
                    loop_cell->sample_id = loop_sample_id;
                    loop_cell->sample_type = loop_sample_type;
                }

                loop_cell->loop_enabled = loop_enabled ? TRUE : FALSE;
                loop_cell->loop_start = loop_start;
                loop_cell->loop_end = loop_end;
            }

            g_strfreev(loop_fields);
            continue;
        }

        fields = g_strsplit(lines[line_index], "\t", 10);
        if(g_strv_length(fields) != 10 || strcmp(fields[0], "E") != 0 ||
           !gvr_pattern_parse_int(fields[1], &bank) ||
           !gvr_pattern_parse_int(fields[2], &slot) ||
           !gvr_pattern_parse_int(fields[3], &sample_id) ||
           !gvr_pattern_parse_int(fields[4], &sample_type) ||
           !gvr_pattern_parse_int(fields[5], &frame) ||
           !gvr_pattern_parse_int(fields[6], &column) ||
           !gvr_pattern_parse_int(fields[7], &vims_id) ||
           !gvr_pattern_target_valid(bank, slot) ||
           frame < 0 || frame >= GVR_PATTERN_MAX_FRAMES ||
           column < 0 || column >= GVR_VIMS_PATTERN_COLUMNS ||
           vims_id < 0 || vims_id > 9999)
        {
            g_strfreev(fields);
            continue;
        }

        label_data = g_base64_decode(fields[8], &label_length);
        message_data = g_base64_decode(fields[9], &message_length);
        if(!message_data || message_length == 0 ||
           message_length >= GVR_VIMS_PATTERN_MESSAGE_MAX)
        {
            g_free(label_data);
            g_free(message_data);
            g_strfreev(fields);
            continue;
        }

        cell = gvr_pattern_cell(view, bank, slot);
        row = gvr_pattern_row_get(cell, frame, TRUE);
        if(!cell || !row) {
            g_free(label_data);
            g_free(message_data);
            g_strfreev(fields);
            continue;
        }

        if(bank != GVR_VIMS_PATTERN_SEQUENCE_BANK && sample_id > 0) {
            cell->sample_id = sample_id;
            cell->sample_type = sample_type;
        }

        event = &row->events[column];
        gvr_pattern_event_clear(event);
        event->vims_id = vims_id;
        event->message = g_strndup((const gchar *)message_data, message_length);
        if(label_data && label_length > 0) {
            gsize copy_length = MIN(label_length, sizeof(event->label) - 1);
            memcpy(event->label, label_data, copy_length);
            event->label[copy_length] = '\0';
        }
        else {
            gvr_pattern_format_label(view,
                                     event->vims_id,
                                     event->message,
                                     event->label,
                                     sizeof(event->label));
        }

        g_free(label_data);
        g_free(message_data);
        g_strfreev(fields);
    }

    gvr_pattern_relabel_all(view);
    gvr_pattern_sync_track_toggles(view);
    gtk_combo_box_set_active(GTK_COMBO_BOX(view->learn_policy_combo),
                             view->learn_policy);
    gtk_combo_box_set_active(GTK_COMBO_BOX(view->paste_mode_combo),
                             view->paste_mode);
    gvr_pattern_sync_loop_controls(view);
    gvr_pattern_update_target_label(view);
    gvr_pattern_update_adjustment(view);
    gtk_widget_queue_draw(view->area);
    valid = TRUE;

done:
    g_strfreev(lines);
    g_free(document);
    return valid;
}

void gvr_vims_pattern_view_clipboard_clear(GtkWidget *widget)
{
    if(!GVR_IS_VIMS_PATTERN_VIEW(widget))
        return;

    g_hash_table_remove_all(GVR_VIMS_PATTERN_VIEW(widget)->clipboard);
}

void gvr_vims_pattern_view_clipboard_copy(GtkWidget *widget,
                                          int bank,
                                          int slot,
                                          int offset)
{
    GvrVimsPatternView *view;
    GvrVimsPatternCell *src;
    GvrVimsPatternCell *copy;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget) || offset < 0)
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    src = gvr_pattern_cell(view, bank, slot);
    if(!src)
        return;

    copy = g_new0(GvrVimsPatternCell, 1);
    copy->sample_id = -1;
    copy->sample_type = -1;
    gvr_pattern_cell_ensure(copy);
    gvr_pattern_cell_clone(copy, src);
    g_hash_table_replace(view->clipboard, GINT_TO_POINTER(offset + 1), copy);
}

void gvr_vims_pattern_view_clipboard_paste(GtkWidget *widget,
                                           int offset,
                                           int bank,
                                           int slot)
{
    GvrVimsPatternView *view;
    GvrVimsPatternCell *copy;
    GvrVimsPatternCell *dst;

    if(!GVR_IS_VIMS_PATTERN_VIEW(widget) || offset < 0)
        return;

    view = GVR_VIMS_PATTERN_VIEW(widget);
    copy = g_hash_table_lookup(
        view->clipboard,
        GINT_TO_POINTER(offset + 1));
    dst = gvr_pattern_cell(view, bank, slot);
    if(!dst)
        return;

    gvr_pattern_history_invalidate_target(view,
                                          bank,
                                          slot);

    if(copy)
        gvr_pattern_cell_clone(dst, copy);
    else
        gvr_pattern_cell_clear_data(dst);

    gvr_pattern_emit_changed(view, bank, slot);
}
