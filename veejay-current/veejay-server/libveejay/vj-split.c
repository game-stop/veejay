/* veejay - Linux VeeJay
 * 	     (C) 2015 Niels Elburg <nwelburg@gmail.com> 
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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <pthread.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <libveejay/vj-split.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vj-server.h>
#include <veejaycore/yuvconv.h>
#include <veejaycore/avcommon.h>
#include <libveejay/vj-share.h>
#include <veejaycore/libvevo.h>
#include <libplugger/defs.h>
#include <libplugger/ldefs.h>
#include <libplugger/specs/livido.h>
#include <libavutil/pixfmt.h>
#include <string.h>

#define LOCALHOST "127.0.0.1"
#define SHM_ADDR_OFFSET 4096

#ifndef VJ_SPLIT_SCALER_TYPE
#define VJ_SPLIT_SCALER_TYPE 1
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define VJ_SPLIT_RESTRICT restrict
#else
#define VJ_SPLIT_RESTRICT
#endif

typedef struct
{
    int width;
    int height;

    int left;
    int right;
    int top;
    int bottom;

    int row;
    int col;

    void *shm;
    void *net;

    int edge_v;
    int edge_h;
    int blend_left;
    int blend_right;
    int blend_top;
    int blend_bottom;

    uint8_t *data;
    int n_threads;

    int map_ready;
    int map_src_w;
    int map_src_h;
    int map_dst_w;
    int map_dst_h;
    int map_left;
    int map_right;
    int map_top;
    int map_bottom;
    int map_blend_left;
    int map_blend_right;
    int map_blend_top;
    int map_blend_bottom;
    int *map_x0;
    int *map_y0;
    uint16_t *map_gx;
    uint16_t *map_gy;

    void *scaler;
    int scaler_src_w;
    int scaler_src_h;
    int scaler_dst_w;
    int scaler_dst_h;
    int scaler_src_fmt;
    int scaler_dst_fmt;
} v_screen_t;

typedef struct
{
    VJFrame **frames;
    v_screen_t **screens;
    int n_screens;
    int current_id;
    int rows;
    int columns;
    int off_x;
    int off_y;
    int source_width;
    int source_height;
    int source_format;
} vj_split_t;

typedef struct
{
    int                 resource_id;
    pthread_rwlock_t    rwlock;
    int                 header[8];
} vj_shared_data;

typedef struct
{
    int shm_id;
    char *sms;
    key_t key;
    pthread_rwlock_t rwlock;
    vj_shared_data *data;
} vj_split_shm_t;

typedef struct
{
    int started;
    char *hostname;
    int port;
} vj_split_net_t;

static char *server_ip = NULL;
static int server_port = 0;


static inline int vj_split_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static uint8_t vj_split_gain_y_[257][256];
static uint8_t vj_split_gain_uv_[257][256];
static int vj_split_gain_lut_ready_ = 0;

static void vj_split_init_gain_luts(void)
{
    if(vj_split_gain_lut_ready_)
        return;

    int g, p;
    for(g = 0; g <= 256; g++) {
        for(p = 0; p < 256; p++) {
            int y = (p * g) >> 8;
            int uv = 128 + ((((int)p - 128) * g) >> 8);
            vj_split_gain_y_[g][p] = (uint8_t) vj_split_clampi(y, 0, 255);
            vj_split_gain_uv_[g][p] = (uint8_t) vj_split_clampi(uv, 0, 255);
        }
    }

    vj_split_gain_lut_ready_ = 1;
}

static inline int vj_split_blend_width(int overlap, int dst_len, int crop_len)
{
    if(overlap <= 0 || dst_len <= 1 || crop_len <= 0)
        return 0;

    int w = (overlap * dst_len + (crop_len >> 1)) / crop_len;
    if(w < 1)
        w = 1;
    if(w > (dst_len >> 1))
        w = dst_len >> 1;

    return w;
}

static inline int vj_split_axis_gain(int pos, int len, int fade0, int fade1)
{
    int gain = 256;

    if(fade0 > 0 && pos < fade0) {
        int g = (pos * 256) / fade0;
        if(g < gain)
            gain = g;
    }

    if(fade1 > 0 && pos >= (len - fade1)) {
        int dist = (len - 1) - pos;
        int g = (dist * 256) / fade1;
        if(g < gain)
            gain = g;
    }

    return vj_split_clampi(gain, 0, 256);
}


static inline void vj_split_gather_row_444(uint8_t *VJ_SPLIT_RESTRICT dy,
                                           uint8_t *VJ_SPLIT_RESTRICT du,
                                           uint8_t *VJ_SPLIT_RESTRICT dv,
                                           const uint8_t *VJ_SPLIT_RESTRICT sy,
                                           const uint8_t *VJ_SPLIT_RESTRICT su,
                                           const uint8_t *VJ_SPLIT_RESTRICT sv,
                                           const int *VJ_SPLIT_RESTRICT map_x,
                                           int dst_w)
{
    int x;
    for(x = 0; x < dst_w; x++) {
        const int sx = map_x[x];
        dy[x] = sy[sx];
        du[x] = su[sx];
        dv[x] = sv[sx];
    }
}

static inline void vj_split_apply_hgain_row_444(uint8_t *VJ_SPLIT_RESTRICT dy,
                                                uint8_t *VJ_SPLIT_RESTRICT du,
                                                uint8_t *VJ_SPLIT_RESTRICT dv,
                                                const uint16_t *VJ_SPLIT_RESTRICT gx_map,
                                                int x0, int x1)
{
    int x;
    for(x = x0; x < x1; x++) {
        const int gain = gx_map[x];
        if(gain >= 256)
            continue;
        if(gain <= 0) {
            dy[x] = 0;
            du[x] = 128;
            dv[x] = 128;
        }
        else {
            dy[x] = vj_split_gain_y_[gain][dy[x]];
            du[x] = vj_split_gain_uv_[gain][du[x]];
            dv[x] = vj_split_gain_uv_[gain][dv[x]];
        }
    }
}

static void vj_split_free_map(v_screen_t *box)
{
    if(!box)
        return;

    if(box->map_x0) free(box->map_x0);
    if(box->map_y0) free(box->map_y0);
    if(box->map_gx) free(box->map_gx);
    if(box->map_gy) free(box->map_gy);

    box->map_x0 = NULL;
    box->map_y0 = NULL;
    box->map_gx = NULL;
    box->map_gy = NULL;
    box->map_ready = 0;
}

static int vj_split_map_params_match(const v_screen_t *VJ_SPLIT_RESTRICT box, int src_w, int src_h,
                                     int dst_w, int dst_h,
                                     int left, int right, int top, int bottom,
                                     int blend_left, int blend_right,
                                     int blend_top, int blend_bottom)
{
    return box->map_ready &&
           box->map_src_w == src_w &&
           box->map_src_h == src_h &&
           box->map_dst_w == dst_w &&
           box->map_dst_h == dst_h &&
           box->map_left == left &&
           box->map_right == right &&
           box->map_top == top &&
           box->map_bottom == bottom &&
           box->map_blend_left == blend_left &&
           box->map_blend_right == blend_right &&
           box->map_blend_top == blend_top &&
           box->map_blend_bottom == blend_bottom;
}

static int vj_split_build_map(v_screen_t *VJ_SPLIT_RESTRICT box, int src_w, int src_h,
                              int dst_w, int dst_h,
                              int left, int right, int top, int bottom,
                              int blend_left, int blend_right,
                              int blend_top, int blend_bottom)
{
    if(!box || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
        return 0;

    if(vj_split_map_params_match(box, src_w, src_h, dst_w, dst_h,
                                 left, right, top, bottom,
                                 blend_left, blend_right, blend_top, blend_bottom))
        return 1;

    if(box->map_dst_w != dst_w || box->map_dst_h != dst_h ||
       !box->map_x0 || !box->map_y0 || !box->map_gx || !box->map_gy) {
        vj_split_free_map(box);

        box->map_x0 = (int*) vj_malloc(sizeof(int) * dst_w);
        box->map_y0 = (int*) vj_malloc(sizeof(int) * dst_h);
        box->map_gx = (uint16_t*) vj_malloc(sizeof(uint16_t) * dst_w);
        box->map_gy = (uint16_t*) vj_malloc(sizeof(uint16_t) * dst_h);

        if(!box->map_x0 || !box->map_y0 || !box->map_gx || !box->map_gy) {
            vj_split_free_map(box);
            return 0;
        }
    }

    const int crop_w = right - left;
    const int crop_h = bottom - top;
    const int x_step = (int) ((((int64_t) crop_w) << 16) / dst_w);
    const int y_step = (int) ((((int64_t) crop_h) << 16) / dst_h);

    int x;
    int sx_fp = (left << 16) + (x_step >> 1);
    for(x = 0; x < dst_w; x++, sx_fp += x_step) {
        int sx0 = sx_fp >> 16;
        if(sx0 >= right)
            sx0 = right - 1;
        if(sx0 < left)
            sx0 = left;

        box->map_x0[x] = sx0;
        box->map_gx[x] = (uint16_t) vj_split_axis_gain(x, dst_w, blend_left, blend_right);
    }

    int y;
    int sy_fp = (top << 16) + (y_step >> 1);
    for(y = 0; y < dst_h; y++, sy_fp += y_step) {
        int sy0 = sy_fp >> 16;
        if(sy0 >= bottom)
            sy0 = bottom - 1;
        if(sy0 < top)
            sy0 = top;

        box->map_y0[y] = sy0;
        box->map_gy[y] = (uint16_t) vj_split_axis_gain(y, dst_h, blend_top, blend_bottom);
    }

    box->map_src_w = src_w;
    box->map_src_h = src_h;
    box->map_dst_w = dst_w;
    box->map_dst_h = dst_h;
    box->map_left = left;
    box->map_right = right;
    box->map_top = top;
    box->map_bottom = bottom;
    box->map_blend_left = blend_left;
    box->map_blend_right = blend_right;
    box->map_blend_top = blend_top;
    box->map_blend_bottom = blend_bottom;
    box->map_ready = 1;

    return 1;
}

void vj_split_set_master(int port)
{
    server_ip = vj_server_find_best_ip();
    server_port = port;
}

static void *vj_split_net_new(char *hostname, int port, int w, int h)
{
    (void) w;
    (void) h;

    vj_split_net_t *net = (vj_split_net_t*) vj_calloc(sizeof(vj_split_net_t));
    if(!net)
        return NULL;

    net->hostname = strdup(hostname ? hostname : LOCALHOST);
    if(!net->hostname) {
        free(net);
        return NULL;
    }

    net->port = port;
    return (void*) net;
}

static void *vj_split_shm_new(key_t key, int w, int h)
{
    vj_split_shm_t *shm = NULL;

    if(key < 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Already in use");
        return NULL;
    }

    const size_t plane_len = (size_t) w * (size_t) h;
    const size_t shm_size = SHM_ADDR_OFFSET + (plane_len * 3);

    int id = shmget(key, shm_size, IPC_CREAT | IPC_EXCL | 0666);
    if(id == -1 && errno == EEXIST) {
        int old_id = shmget(key, 0, 0666);
        if(old_id != -1)
            shmctl(old_id, IPC_RMID, NULL);
        id = shmget(key, shm_size, IPC_CREAT | IPC_EXCL | 0666);
    }

    if(id == -1) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to access shared resource %d", key);
        return NULL;
    }

    char *sms = shmat(id, NULL, 0);
    if(sms == NULL || sms == (char*) (-1)) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to attach to shared resource %d", key);
        shmctl(id, IPC_RMID, NULL);
        return NULL;
    }

    shm = (vj_split_shm_t*) vj_calloc(sizeof(vj_split_shm_t));
    if(!shm) {
        shmdt(sms);
        shmctl(id, IPC_RMID, NULL);
        return NULL;
    }

    shm->shm_id = id;
    shm->sms = sms;
    shm->key = key;
    shm->data = (vj_shared_data*) &(shm->sms[0]);

    veejay_memset(shm->sms, 0, SHM_ADDR_OFFSET + plane_len);
    veejay_memset(shm->sms + SHM_ADDR_OFFSET + plane_len, 128, plane_len * 2);

    shm->data->header[0] = w;
    shm->data->header[1] = h;
    shm->data->header[2] = w;
    shm->data->header[3] = w;
    shm->data->header[4] = w;
    shm->data->header[5] = LIVIDO_PALETTE_YUV444P;
    shm->data->header[6] = h;
    shm->data->header[7] = 0;

    pthread_rwlockattr_t rw_lock_attr;

    int ret = pthread_rwlockattr_init(&rw_lock_attr);
    if(ret != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to acquire rwlock on shared resource %d", key);
        shmdt(sms);
        shmctl(shm->shm_id, IPC_RMID, NULL);
        free(shm);
        return NULL;
    }

    ret = pthread_rwlockattr_setpshared(&rw_lock_attr, PTHREAD_PROCESS_SHARED);
    if(ret != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to set PTHREAD_PROCESS_SHARED");
        pthread_rwlockattr_destroy(&rw_lock_attr);
        shmdt(sms);
        shmctl(shm->shm_id, IPC_RMID, NULL);
        free(shm);
        return NULL;
    }

    ret = pthread_rwlock_init(&(shm->data->rwlock), &rw_lock_attr);
    pthread_rwlockattr_destroy(&rw_lock_attr);
    if(ret != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to initialize rw-lock");
        shmdt(sms);
        shmctl(shm->shm_id, IPC_RMID, NULL);
        free(shm);
        return NULL;
    }

    return (void*) shm;
}

static void vj_split_shm_destroy(vj_split_shm_t *shm)
{
    if(!shm)
        return;

    if(shm->sms && shm->sms != (char*) (-1)) {
        vj_shared_data *data = (vj_shared_data*) shm->sms;
        int res = pthread_rwlock_destroy(&(data->rwlock));
        if(res != 0)
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to destroy rw lock");

        shmdt(shm->sms);
    }

    int res = shmctl(shm->shm_id, IPC_RMID, NULL);
    if(res == -1)
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to remove shared memory segment %x:%d", shm->key, shm->shm_id);

    free(shm);
}

static int vj_split_parse_int(const char *line, const char *key, int *value)
{
    const char *p = strstr(line, key);
    if(!p)
        return 0;

    p += strlen(key);
    if(sscanf(p, "%d", value) != 1)
        return 0;

    return 1;
}

static int vj_split_line_is_empty(const char *line)
{
    while(*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n')
        line++;
    return (*line == '\0' || *line == '#');
}

static int vj_split_configure_auto(vj_split_t *x, int screen_id, int edge_x, int edge_y)
{
    if(!x || screen_id < 0 || screen_id >= x->n_screens || !x->screens[screen_id])
        return 0;

    v_screen_t *box = x->screens[screen_id];
    const int src_w = x->source_width > 0 ? x->source_width : (box->width * x->columns);
    const int src_h = x->source_height > 0 ? x->source_height : (box->height * x->rows);

    if(src_w <= 0 || src_h <= 0 || x->columns <= 0 || x->rows <= 0)
        return 0;

    const int base_left = (box->col * src_w) / x->columns;
    const int base_right = ((box->col + 1) * src_w) / x->columns;
    const int base_top = (box->row * src_h) / x->rows;
    const int base_bottom = ((box->row + 1) * src_h) / x->rows;

    const int crop_left = vj_split_clampi(base_left - (box->col > 0 ? edge_x : 0), 0, src_w);
    const int crop_right = vj_split_clampi(base_right + (box->col < (x->columns - 1) ? edge_x : 0), 0, src_w);
    const int crop_top = vj_split_clampi(base_top - (box->row > 0 ? edge_y : 0), 0, src_h);
    const int crop_bottom = vj_split_clampi(base_bottom + (box->row < (x->rows - 1) ? edge_y : 0), 0, src_h);

    vj_split_configure_screen((void*) x,
                              screen_id,
                              edge_x,
                              edge_y,
                              crop_left,
                              crop_right,
                              crop_top,
                              crop_bottom,
                              box->width,
                              box->height);

    const int crop_w = box->right - box->left;
    const int crop_h = box->bottom - box->top;

    box->blend_left = vj_split_blend_width(base_left - box->left, box->width, crop_w);
    box->blend_right = vj_split_blend_width(box->right - base_right, box->width, crop_w);
    box->blend_top = vj_split_blend_width(base_top - box->top, box->height, crop_h);
    box->blend_bottom = vj_split_blend_width(box->bottom - base_bottom, box->height, crop_h);

    return 1;
}

void *vj_split_new_from_file(char *filename, int out_w, int out_h, int vfmt)
{
    FILE *f = fopen(filename, "r");
    if(f == NULL) {
        veejay_msg(VEEJAY_MSG_WARNING, "No split screen configured in %s", filename);
        return NULL;
    }

    char line[2048];
    char hostname[1024];
    int row = 0, col = 0, port = 0;
    int max_col = 0, max_row = 0;
    void *split = NULL;

    veejay_msg(VEEJAY_MSG_INFO, "Splitted screens configured in %s", filename);

    while(fgets(line, sizeof(line), f)) {
        if(vj_split_line_is_empty(line))
            continue;

        if(sscanf(line, "screen=%dx%d", &max_row, &max_col) == 2) {
            if((max_row * max_col) > 0) {
                split = vj_split_init(max_row, max_col);
                if(split) {
                    vj_split_t *x = (vj_split_t*) split;
                    x->source_width = out_w;
                    x->source_height = out_h;
                    x->source_format = vfmt;
                }
            }
            else {
                veejay_msg(VEEJAY_MSG_ERROR, "Invalid row/columns in configuration file");
                fclose(f);
                if(split)
                    vj_split_free(split);
                return NULL;
            }
            continue;
        }

        hostname[0] = '\0';
        if(sscanf(line, "row=%d col=%d port=%d hostname=%1023s", &row, &col, &port, hostname) == 4) {
            if(!split) {
                veejay_msg(VEEJAY_MSG_ERROR, "Screen not initialized");
                fclose(f);
                if(split)
                    vj_split_free(split);
                return NULL;
            }

            int edge_x = 0;
            int edge_y = 0;
            int left = 0;
            int right = 0;
            int top = 0;
            int bottom = 0;

            vj_split_parse_int(line, "edge_x=", &edge_x);
            vj_split_parse_int(line, "edge_y=", &edge_y);
            vj_split_parse_int(line, "edge_h=", &edge_x);
            vj_split_parse_int(line, "edge_v=", &edge_y);
            vj_split_parse_int(line, "overlap_x=", &edge_x);
            vj_split_parse_int(line, "overlap_y=", &edge_y);
            vj_split_parse_int(line, "blend_x=", &edge_x);
            vj_split_parse_int(line, "blend_y=", &edge_y);

            const int has_manual_crop =
                vj_split_parse_int(line, "left=", &left) &&
                vj_split_parse_int(line, "right=", &right) &&
                vj_split_parse_int(line, "top=", &top) &&
                vj_split_parse_int(line, "bottom=", &bottom);

            if(vj_split_add_screen(split, hostname, port, row, col, out_w, out_h, vfmt)) {
                vj_split_t *x = (vj_split_t*) split;
                int screen_id = (x->columns * row) + col;

                if(has_manual_crop)
                    vj_split_configure_screen(split, screen_id, edge_x, edge_y, left, right, top, bottom,
                                              x->screens[screen_id]->width, x->screens[screen_id]->height);
                else if(edge_x > 0 || edge_y > 0)
                    vj_split_configure_auto(x, screen_id, edge_x, edge_y);
            }
            else {
                veejay_msg(VEEJAY_MSG_ERROR, "Failed to configure split-screen receiver %s:%d", hostname, port);
                fclose(f);
                vj_split_free(split);
                return NULL;
            }
            continue;
        }

        veejay_msg(VEEJAY_MSG_WARNING, "Ignoring invalid split-screen config line: %s", line);
    }

    fclose(f);
    return split;
}

void *vj_split_init(int r, int c)
{
    if(r <= 0 || c <= 0)
        return NULL;

    vj_split_init_gain_luts();

    vj_split_t *x = (vj_split_t*) vj_calloc(sizeof(vj_split_t));
    if(!x)
        return NULL;

    x->frames = (VJFrame**) vj_calloc(sizeof(VJFrame*) * r * c);
    x->screens = (v_screen_t**) vj_calloc(sizeof(v_screen_t*) * r * c);
    if(!x->frames || !x->screens) {
        if(x->frames)
            free(x->frames);
        if(x->screens)
            free(x->screens);
        free(x);
        return NULL;
    }

    x->n_screens = r * c;
    x->current_id = 0;
    x->rows = r;
    x->columns = c;
    return (void*) x;
}

static void vj_split_free_screen(vj_split_t *x, int screen_id)
{
    if(!x || screen_id < 0 || screen_id >= x->n_screens)
        return;

    if(x->screens[screen_id]) {
        if(x->screens[screen_id]->shm) {
            vj_split_shm_destroy((vj_split_shm_t*) x->screens[screen_id]->shm);
            x->screens[screen_id]->shm = NULL;
        }

        if(x->screens[screen_id]->net) {
            vj_split_net_t *net = (vj_split_net_t*) x->screens[screen_id]->net;
            free(net->hostname);
            free(net);
        }

        vj_split_free_map(x->screens[screen_id]);

        if(x->screens[screen_id]->scaler) {
            yuv_free_swscaler(x->screens[screen_id]->scaler);
            x->screens[screen_id]->scaler = NULL;
        }

        if(x->screens[screen_id]->data) {
            free(x->screens[screen_id]->data);
            x->screens[screen_id]->data = NULL;
        }

        free(x->screens[screen_id]);
        x->screens[screen_id] = NULL;
    }

    if(x->frames[screen_id]) {
        free(x->frames[screen_id]);
        x->frames[screen_id] = NULL;
    }
}

void vj_split_free(void *ptr)
{
    vj_split_t *x = (vj_split_t*) ptr;
    if(!x)
        return;

    int i;
    for(i = 0; i < x->n_screens; i++)
        vj_split_free_screen(x, i);

    free(x->frames);
    free(x->screens);
    free(x);
}

static int vj_split_shm_get(vj_split_shm_t *shm)
{
    return shm ? shm->key : 0;
}

static void vj_split_frame_setup_444(VJFrame *VJ_SPLIT_RESTRICT frame, int w, int h, int range)
{
    const int len = w * h;

    frame->width = w;
    frame->height = h;
    frame->uv_width = w;
    frame->uv_height = h;
    frame->len = len;
    frame->uv_len = len;
    frame->format = range ? PIX_FMT_YUVJ444P : PIX_FMT_YUV444P;
    frame->range = range;
    frame->ssm = 1;
    frame->shift_h = 0;
    frame->shift_v = 0;
    frame->stride[0] = w;
    frame->stride[1] = w;
    frame->stride[2] = w;
}

static int vj_split_bind_frame_to_shm(v_screen_t *VJ_SPLIT_RESTRICT screen,
                                      VJFrame *VJ_SPLIT_RESTRICT frame)
{
    if(!screen || !frame || !screen->shm)
        return 0;

    vj_split_shm_t *VJ_SPLIT_RESTRICT shm = (vj_split_shm_t*) screen->shm;
    uint8_t *VJ_SPLIT_RESTRICT payload = (uint8_t*) shm->sms + SHM_ADDR_OFFSET;
    const size_t plane_len = (size_t) screen->width * (size_t) screen->height;

    if(screen->data) {
        free(screen->data);
        screen->data = NULL;
    }

    frame->data[0] = payload;
    frame->data[1] = payload + plane_len;
    frame->data[2] = payload + (plane_len * 2);
    frame->data[3] = NULL;
    vj_split_frame_setup_444(frame, screen->width, screen->height, frame->range);

    return 1;
}

static int vj_split_frame_is_bound_to_shm(v_screen_t *VJ_SPLIT_RESTRICT screen,
                                          VJFrame *VJ_SPLIT_RESTRICT frame)
{
    if(!screen || !frame || !screen->shm)
        return 0;

    vj_split_shm_t *VJ_SPLIT_RESTRICT shm = (vj_split_shm_t*) screen->shm;
    return frame->data[0] == ((uint8_t*) shm->sms + SHM_ADDR_OFFSET);
}

static void vj_split_write_shm_header(v_screen_t *VJ_SPLIT_RESTRICT screen,
                                      VJFrame *VJ_SPLIT_RESTRICT frame)
{
    vj_split_shm_t *VJ_SPLIT_RESTRICT shm = (vj_split_shm_t*) screen->shm;
    vj_shared_data *VJ_SPLIT_RESTRICT data = shm->data;

    data->header[0] = frame->width;
    data->header[1] = frame->height;
    data->header[2] = frame->width;
    data->header[3] = frame->width;
    data->header[4] = frame->width;
    data->header[5] = LIVIDO_PALETTE_YUV444P;
    data->header[6] = frame->height;
    data->header[7] = frame->range ? 1 : 0;
}
static inline int __advise_num_threads(const int len) {
	static int ncores = -1;
    if (ncores == -1) {
        ncores = (int) sysconf(_SC_NPROCESSORS_ONLN);
    }
    int nthreads = ncores;

    if (len < (1920*1080)) nthreads = ncores / 2;
    if (nthreads < 1) nthreads = 1;
    if (nthreads > 6) nthreads = 6; // avoid too much overhead

    return nthreads;
}


static int vj_split_allocate_screen(void *ptr, int screen_id, int wid, int hei, int fmt)
{
    (void) fmt;

    vj_split_t *x = (vj_split_t*) ptr;
    if(!x || screen_id < 0 || screen_id >= x->n_screens || wid <= 0 || hei <= 0)
        return 0;

    vj_split_free_screen(x, screen_id);

    v_screen_t *box = (v_screen_t*) vj_calloc(sizeof(v_screen_t));
    if(box == NULL)
        return 0;

    x->screens[screen_id] = box;

    VJFrame *dst = yuv_yuv_template(NULL, NULL, NULL, wid, hei, PIX_FMT_YUV444P);
    if(dst == NULL) {
        free(box);
        x->screens[screen_id] = NULL;
        return 0;
    }

    const size_t plane_len = (size_t) wid * (size_t) hei;
    box->data = (uint8_t*) vj_malloc(plane_len * 3);
    if(!box->data) {
        free(dst);
        free(box);
        x->screens[screen_id] = NULL;
        return 0;
    }

    dst->data[0] = box->data;
    dst->data[1] = box->data + plane_len;
    dst->data[2] = box->data + (plane_len * 2);
    dst->data[3] = NULL;
    vj_split_frame_setup_444(dst, wid, hei, 0);

    vj_frame_clear1(dst->data[0], 0, plane_len);
    vj_frame_clear1(dst->data[1], 128, plane_len * 2);

    box->width = wid;
    box->height = hei;
    box->n_threads = __advise_num_threads(wid * hei);
    x->frames[screen_id] = dst;

    return 1;
}

int vj_split_configure_screen(void *ptr, int screen_id, int edge_x, int edge_y,
                              int left, int right, int top, int bottom, int w, int h)
{
    vj_split_t *x = (vj_split_t*) ptr;
    if(!x || screen_id < 0 || screen_id >= x->n_screens || !x->screens[screen_id])
        return 0;

    v_screen_t *box = x->screens[screen_id];
    const int src_w = x->source_width > 0 ? x->source_width : right;
    const int src_h = x->source_height > 0 ? x->source_height : bottom;

    box->edge_h = edge_x < 0 ? 0 : edge_x;
    box->edge_v = edge_y < 0 ? 0 : edge_y;
    box->width = w > 0 ? w : box->width;
    box->height = h > 0 ? h : box->height;

    box->left = vj_split_clampi(left, 0, src_w > 0 ? src_w : left);
    box->right = vj_split_clampi(right, box->left + 1, src_w > 0 ? src_w : right);
    box->top = vj_split_clampi(top, 0, src_h > 0 ? src_h : top);
    box->bottom = vj_split_clampi(bottom, box->top + 1, src_h > 0 ? src_h : bottom);

    box->blend_left = (box->left > 0) ? vj_split_clampi(box->edge_h, 0, box->width >> 1) : 0;
    box->blend_right = (src_w > 0 && box->right < src_w) ? vj_split_clampi(box->edge_h, 0, box->width >> 1) : 0;
    box->blend_top = (box->top > 0) ? vj_split_clampi(box->edge_v, 0, box->height >> 1) : 0;
    box->blend_bottom = (src_h > 0 && box->bottom < src_h) ? vj_split_clampi(box->edge_v, 0, box->height >> 1) : 0;

    return 1;
}

int vj_split_auto_configure_screen(void *ptr)
{
    vj_split_t *x = (vj_split_t*) ptr;
    if(!x || x->current_id < 0 || x->current_id >= x->n_screens || !x->screens[x->current_id])
        return 0;

    v_screen_t *box = x->screens[x->current_id];
    return vj_split_configure_auto(x, x->current_id, box->edge_h, box->edge_v);
}

static char *get_self(void)
{
    char *path = vj_malloc(1024);
    if(path == NULL)
        return NULL;

    ssize_t n = readlink("/proc/self/exe", path, 1023);
    if(n == -1) {
        free(path);
        return NULL;
    }

    path[n] = '\0';
    return path;
}

static key_t vj_split_suggest_key(int screen_id)
{
    char *progname = get_self();
    if(progname == NULL)
        return 0;

    key_t key = ftok(progname, (screen_id & 0xff) + 1);
    free(progname);
    return key;
}

int vj_split_add_screen(void *ptr, char *hostname, int port, int row, int col, int out_w, int out_h, int fmt)
{
    vj_split_t *x = (vj_split_t*) ptr;
    if(!x || row < 0 || col < 0 || row >= x->rows || col >= x->columns)
        return 0;

    if(out_w > 0)
        x->source_width = out_w;
    if(out_h > 0)
        x->source_height = out_h;
    x->source_format = fmt;

    int use_shm = 0;
    if(hostname == NULL || strcmp(hostname, "localhost") == 0 || strcmp(hostname, LOCALHOST) == 0)
        use_shm = 1;

    int w = 0, h = 0, format = 0, rkey = 0;
    const int screen_id = (x->columns * row) + col;

    int ret = vj_share_get_info(hostname, port, &w, &h, &format, &rkey, screen_id);
    if(ret == 0 || w <= 0 || h <= 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to get screen info from veejay on port %d", port);
        return 0;
    }

    ret = vj_split_allocate_screen(ptr, screen_id, w, h, fmt);
    if(ret == 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Screen %d configuration error", screen_id);
        return 0;
    }

    v_screen_t *box = x->screens[screen_id];
    box->row = row;
    box->col = col;
    x->current_id = screen_id;

    if(!vj_split_configure_auto(x, screen_id, 0, 0)) {
        vj_split_free_screen(x, screen_id);
        return 0;
    }

    key_t key = 0;

    if(use_shm) {
        box->shm = vj_split_shm_new(vj_split_suggest_key(screen_id), w, h);
        if(box->shm == NULL) {
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to setup shared resource with %s:%d", hostname ? hostname : LOCALHOST, port);
            vj_split_free_screen(x, screen_id);
            return 0;
        }

        if(!vj_split_bind_frame_to_shm(box, x->frames[screen_id])) {
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to bind screen %d frame to shared memory", screen_id);
            vj_split_free_screen(x, screen_id);
            return 0;
        }

        key = vj_split_shm_get((vj_split_shm_t*) box->shm);

        if(vj_share_start_slave(hostname, port, key) == 0) {
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to communicate with %s:%d", hostname ? hostname : LOCALHOST, port);
            vj_split_free_screen(x, screen_id);
            return 0;
        }
    }
    else {
        box->net = vj_split_net_new(hostname, port, w, h);
        if(box->net == NULL) {
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to setup networked resource with %s:%d", hostname, port);
            vj_split_free_screen(x, screen_id);
            return 0;
        }

        const char *master_host = server_ip ? server_ip : LOCALHOST;
        if(vj_share_start_net(hostname, port, (char*) master_host, server_port) == 0) {
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to communicate with %s:%d", hostname, port);
            vj_split_free_screen(x, screen_id);
            return 0;
        }
    }

    x->off_y = box->top;
    x->off_x = box->left;

    veejay_msg(VEEJAY_MSG_INFO, "Screen #%d configuration:", screen_id);
    veejay_msg(VEEJAY_MSG_INFO, "\tSize: %dx%d", w, h);
    veejay_msg(VEEJAY_MSG_INFO, "\tType: %s", (use_shm == 1 ? "shared memory" : "network"));
    if(use_shm)
        veejay_msg(VEEJAY_MSG_INFO, "\tshm_id: %d (%x)", key, key);
    veejay_msg(VEEJAY_MSG_INFO, "\tHost: %s:%d", hostname ? hostname : LOCALHOST, port);
    veejay_msg(VEEJAY_MSG_INFO, "\tRegion: x0=%d,x1=%d y0=%d,y1=%d", box->left, box->right, box->top, box->bottom);

    return 1;
}


static inline void vj_split_apply_vgain_row_444(uint8_t *VJ_SPLIT_RESTRICT dy,
                                                uint8_t *VJ_SPLIT_RESTRICT du,
                                                uint8_t *VJ_SPLIT_RESTRICT dv,
                                                int dst_w,
                                                int gain)
{
    if(gain >= 256)
        return;

    if(gain <= 0) {
        memset(dy, 0, (size_t)dst_w);
        memset(du, 128, (size_t)dst_w);
        memset(dv, 128, (size_t)dst_w);
        return;
    }

    const uint8_t *VJ_SPLIT_RESTRICT lut_y = vj_split_gain_y_[gain];
    const uint8_t *VJ_SPLIT_RESTRICT lut_uv = vj_split_gain_uv_[gain];
    int x;

    for(x = 0; x < dst_w; x++) {
        dy[x] = lut_y[dy[x]];
        du[x] = lut_uv[du[x]];
        dv[x] = lut_uv[dv[x]];
    }
}

static void vj_split_apply_edge_blend_444(VJFrame *VJ_SPLIT_RESTRICT dst,
                                          v_screen_t *VJ_SPLIT_RESTRICT box)
{
    const int dst_w = dst->width;
    const int dst_h = dst->height;
    const int x_blend = box->blend_left || box->blend_right;
    const int y_blend = box->blend_top || box->blend_bottom;

    if(!x_blend && !y_blend)
        return;

    const uint16_t *VJ_SPLIT_RESTRICT gx_map = box->map_gx;
    const uint16_t *VJ_SPLIT_RESTRICT gy_map = box->map_gy;
    if((x_blend && !gx_map) || (y_blend && !gy_map))
        return;

    uint8_t *VJ_SPLIT_RESTRICT DY = dst->data[0];
    uint8_t *VJ_SPLIT_RESTRICT DU = dst->data[1];
    uint8_t *VJ_SPLIT_RESTRICT DV = dst->data[2];
    const int left_end = box->blend_left > 0 ? box->blend_left : 0;
    const int right_start = box->blend_right > 0 ? (dst_w - box->blend_right) : dst_w;
    int y;

#pragma omp parallel for schedule(static) num_threads(box->n_threads)
    for(y = 0; y < dst_h; y++) {
        const int d_row = y * dst_w;
        uint8_t *VJ_SPLIT_RESTRICT dy = DY + d_row;
        uint8_t *VJ_SPLIT_RESTRICT du = DU + d_row;
        uint8_t *VJ_SPLIT_RESTRICT dv = DV + d_row;

        if(y_blend)
            vj_split_apply_vgain_row_444(dy, du, dv, dst_w, gy_map[y]);

        if(x_blend) {
            if(left_end > 0)
                vj_split_apply_hgain_row_444(dy, du, dv, gx_map, 0, left_end);
            if(right_start < dst_w)
                vj_split_apply_hgain_row_444(dy, du, dv, gx_map, right_start, dst_w);
        }
    }
}

static void vj_split_free_scaler(v_screen_t *box)
{
    if(!box || !box->scaler)
        return;

    yuv_free_swscaler(box->scaler);
    box->scaler = NULL;
    box->scaler_src_w = 0;
    box->scaler_src_h = 0;
    box->scaler_dst_w = 0;
    box->scaler_dst_h = 0;
    box->scaler_src_fmt = 0;
    box->scaler_dst_fmt = 0;
}

static int vj_split_scaler_matches(const v_screen_t *box,
                                   int src_w, int src_h,
                                   int dst_w, int dst_h,
                                   int src_fmt, int dst_fmt)
{
    return box->scaler &&
           box->scaler_src_w == src_w &&
           box->scaler_src_h == src_h &&
           box->scaler_dst_w == dst_w &&
           box->scaler_dst_h == dst_h &&
           box->scaler_src_fmt == src_fmt &&
           box->scaler_dst_fmt == dst_fmt;
}

static int vj_split_yuv_scale_444(VJFrame *VJ_SPLIT_RESTRICT src,
                                  VJFrame *VJ_SPLIT_RESTRICT dst,
                                  v_screen_t *VJ_SPLIT_RESTRICT box,
                                  int left, int right, int top, int bottom)
{
    const int src_w = src->width;
    const int dst_w = dst->width;
    const int dst_h = dst->height;
    const int crop_w = right - left;
    const int crop_h = bottom - top;
    const int src_stride = src->stride[0] > 0 ? src->stride[0] : src_w;
    const int dst_stride = dst->stride[0] > 0 ? dst->stride[0] : dst_w;
    const int src_fmt = src->range ? PIX_FMT_YUVJ444P : PIX_FMT_YUV444P;
    const int dst_fmt = dst->range ? PIX_FMT_YUVJ444P : PIX_FMT_YUV444P;

    VJFrame crop;
    VJFrame out;
    veejay_memset(&crop, 0, sizeof(VJFrame));
    veejay_memset(&out, 0, sizeof(VJFrame));

    crop.data[0] = src->data[0] + ((size_t)top * (size_t)src_stride) + (size_t)left;
    crop.data[1] = src->data[1] + ((size_t)top * (size_t)src_stride) + (size_t)left;
    crop.data[2] = src->data[2] + ((size_t)top * (size_t)src_stride) + (size_t)left;
    crop.width = crop_w;
    crop.height = crop_h;
    crop.out_width = crop_w;
    crop.out_height = crop_h;
    crop.uv_width = crop_w;
    crop.uv_height = crop_h;
    crop.len = crop_w * crop_h;
    crop.uv_len = crop.len;
    crop.stride[0] = src_stride;
    crop.stride[1] = src->stride[1] > 0 ? src->stride[1] : src_stride;
    crop.stride[2] = src->stride[2] > 0 ? src->stride[2] : src_stride;
    crop.format = src_fmt;
    crop.yuv_fmt = src_fmt;
    crop.range = src->range;

    out.data[0] = dst->data[0];
    out.data[1] = dst->data[1];
    out.data[2] = dst->data[2];
    out.width = dst_w;
    out.height = dst_h;
    out.out_width = dst_w;
    out.out_height = dst_h;
    out.uv_width = dst_w;
    out.uv_height = dst_h;
    out.len = dst_w * dst_h;
    out.uv_len = out.len;
    out.stride[0] = dst_stride;
    out.stride[1] = dst->stride[1] > 0 ? dst->stride[1] : dst_stride;
    out.stride[2] = dst->stride[2] > 0 ? dst->stride[2] : dst_stride;
    out.format = dst_fmt;
    out.yuv_fmt = dst_fmt;
    out.range = dst->range;

    if(!vj_split_scaler_matches(box, crop_w, crop_h, dst_w, dst_h, src_fmt, dst_fmt)) {
        sws_template templ;
        veejay_memset(&templ, 0, sizeof(sws_template));
        templ.flags = VJ_SPLIT_SCALER_TYPE;

        vj_split_free_scaler(box);
        box->scaler = yuv_init_swscaler(&crop, &out, &templ, yuv_sws_get_cpu_flags());
        if(!box->scaler) {
            veejay_msg(VEEJAY_MSG_ERROR, "Unable to create split-screen scaler");
            return 0;
        }

        box->scaler_src_w = crop_w;
        box->scaler_src_h = crop_h;
        box->scaler_dst_w = dst_w;
        box->scaler_dst_h = dst_h;
        box->scaler_src_fmt = src_fmt;
        box->scaler_dst_fmt = dst_fmt;
    }

    yuv_convert_and_scale(box->scaler, &crop, &out);
    return 1;
}

static void vj_split_copy_region(VJFrame *VJ_SPLIT_RESTRICT src,
                                 VJFrame *VJ_SPLIT_RESTRICT dst,
                                 v_screen_t *VJ_SPLIT_RESTRICT box)
{
    const int src_w = src->width;
    const int src_h = src->height;
    const int dst_w = dst->width;
    const int dst_h = dst->height;

    int left = vj_split_clampi(box->left, 0, src_w - 1);
    int right = vj_split_clampi(box->right, left + 1, src_w);
    int top = vj_split_clampi(box->top, 0, src_h - 1);
    int bottom = vj_split_clampi(box->bottom, top + 1, src_h);

    const int crop_w = right - left;
    const int crop_h = bottom - top;
    const int do_blend = box->blend_left || box->blend_right || box->blend_top || box->blend_bottom;
    const int same_w = (crop_w == dst_w);
    const int same_h = (crop_h == dst_h);

    if(src->uv_width != src_w || src->uv_height != src_h) {
        veejay_msg(VEEJAY_MSG_ERROR, "Split-screen processor expects a 4:4:4 source frame");
        return;
    }

    vj_split_frame_setup_444(dst, dst_w, dst_h, src->range);

    uint8_t *VJ_SPLIT_RESTRICT DY = dst->data[0];
    uint8_t *VJ_SPLIT_RESTRICT DU = dst->data[1];
    uint8_t *VJ_SPLIT_RESTRICT DV = dst->data[2];
    const uint8_t *VJ_SPLIT_RESTRICT SY = src->data[0];
    const uint8_t *VJ_SPLIT_RESTRICT SU = src->data[1];
    const uint8_t *VJ_SPLIT_RESTRICT SV = src->data[2];

    if(!vj_split_build_map(box, src_w, src_h, dst_w, dst_h,
                           left, right, top, bottom,
                           box->blend_left, box->blend_right,
                           box->blend_top, box->blend_bottom))
        return;

    if(same_w && same_h) {
        int y;
#pragma omp parallel for schedule(static) num_threads(box->n_threads)
        for(y = 0; y < dst_h; y++) {
            const size_t s_off = (size_t)(top + y) * (size_t)src_w + (size_t)left;
            const size_t d_off = (size_t)y * (size_t)dst_w;
            memcpy(DY + d_off, SY + s_off, (size_t)dst_w);
            memcpy(DU + d_off, SU + s_off, (size_t)dst_w);
            memcpy(DV + d_off, SV + s_off, (size_t)dst_w);
        }

        if(do_blend)
            vj_split_apply_edge_blend_444(dst, box);
        return;
    }

    if(same_w && !do_blend) {
        const int *VJ_SPLIT_RESTRICT map_y = box->map_y0;
        int y;
#pragma omp parallel for schedule(static) num_threads(box->n_threads)
        for(y = 0; y < dst_h; y++) {
            const size_t s_off = (size_t)map_y[y] * (size_t)src_w + (size_t)left;
            const size_t d_off = (size_t)y * (size_t)dst_w;
            memcpy(DY + d_off, SY + s_off, (size_t)dst_w);
            memcpy(DU + d_off, SU + s_off, (size_t)dst_w);
            memcpy(DV + d_off, SV + s_off, (size_t)dst_w);
        }
        return;
    }

    if(!vj_split_yuv_scale_444(src, dst, box, left, right, top, bottom))
        return;

    if(do_blend)
        vj_split_apply_edge_blend_444(dst, box);
}

static int vj_split_process_shm(v_screen_t *VJ_SPLIT_RESTRICT screen,
                                VJFrame *VJ_SPLIT_RESTRICT frame,
                                VJFrame *VJ_SPLIT_RESTRICT src)
{
    vj_split_shm_t *VJ_SPLIT_RESTRICT shm = (vj_split_shm_t*) screen->shm;
    if(!shm || !frame || !src)
        return 0;

    vj_shared_data *VJ_SPLIT_RESTRICT data = shm->data;
    int res = pthread_rwlock_wrlock(&(data->rwlock));
    if(res != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to acquire lock on shared resource");
        return 0;
    }

    vj_split_copy_region(src, frame, screen);
    vj_split_write_shm_header(screen, frame);

    res = pthread_rwlock_unlock(&(data->rwlock));
    if(res != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to unlock shared resource");
        return 0;
    }

    return 1;
}

static int vj_split_push_shm(v_screen_t *VJ_SPLIT_RESTRICT screen, VJFrame *VJ_SPLIT_RESTRICT frame)
{
    vj_split_shm_t *shm = (vj_split_shm_t*) screen->shm;
    if(!shm || !frame)
        return 0;

    unsigned char *VJ_SPLIT_RESTRICT addr = (unsigned char*) shm->sms;
    vj_shared_data *VJ_SPLIT_RESTRICT data = (vj_shared_data*) shm->sms;

    int res = pthread_rwlock_wrlock(&(data->rwlock));
    if(res != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to acquire lock on shared resource");
        return 0;
    }

    if(frame->width != screen->width || frame->height != screen->height) {
        veejay_msg(VEEJAY_MSG_ERROR, "Shared resource frame does not have matching video resolution");
        pthread_rwlock_unlock(&(data->rwlock));
        return 0;
    }

    if(frame->uv_width != frame->width || frame->uv_height != frame->height) {
        veejay_msg(VEEJAY_MSG_ERROR, "Split-screen SHM expects a 4:4:4 tile frame");
        pthread_rwlock_unlock(&(data->rwlock));
        return 0;
    }

    const size_t plane_len = (size_t) frame->width * (size_t) frame->height;
    const size_t max_plane_len = (size_t) screen->width * (size_t) screen->height;

    if(plane_len > max_plane_len) {
        veejay_msg(VEEJAY_MSG_ERROR, "Shared resource frame exceeds allocated 4:4:4 storage");
        pthread_rwlock_unlock(&(data->rwlock));
        return 0;
    }

    vj_split_write_shm_header(screen, frame);

    uint8_t *VJ_SPLIT_RESTRICT offset = addr + SHM_ADDR_OFFSET;
    uint8_t *VJ_SPLIT_RESTRICT Y = offset;
    uint8_t *VJ_SPLIT_RESTRICT U = Y + plane_len;
    uint8_t *VJ_SPLIT_RESTRICT V = U + plane_len;

    const uint8_t *VJ_SPLIT_RESTRICT FY = frame->data[0];
    const uint8_t *VJ_SPLIT_RESTRICT FU = frame->data[1];
    const uint8_t *VJ_SPLIT_RESTRICT FV = frame->data[2];

    memcpy(Y, FY, plane_len);
    memcpy(U, FU, plane_len);
    memcpy(V, FV, plane_len);

    res = pthread_rwlock_unlock(&(data->rwlock));
    if(res != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to unlock shared resource");
        return 0;
    }

    return 1;
}

void vj_split_process(void *ptr, VJFrame *src)
{
    vj_split_t *x = (vj_split_t*) ptr;
    if(!x || !src || !src->data[0] || !src->data[1] || !src->data[2])
        return;

    if(x->source_width <= 0)
        x->source_width = src->width;
    if(x->source_height <= 0)
        x->source_height = src->height;

    int i;
    for(i = 0; i < x->n_screens; i++) {
        if(x->frames[i] == NULL || x->screens[i] == NULL)
            continue;

        if(x->screens[i]->shm) {
            if(vj_split_process_shm(x->screens[i], x->frames[i], src) == 0)
                vj_split_free_screen(x, i);
            continue;
        }

        vj_split_copy_region(src, x->frames[i], x->screens[i]);

        if(x->screens[i] && x->screens[i]->net) {
            vj_split_net_t *net = (vj_split_net_t*) x->screens[i]->net;
            if(net->started == 0)
                net->started = vj_share_play_last(net->hostname, net->port);
        }
    }
}

void vj_split_render(void *ptr)
{
    vj_split_t *x = (vj_split_t*) ptr;
    if(!x)
        return;

    int i;
    for(i = 0; i < x->n_screens; i++) {
        if(x->frames[i] == NULL || x->screens[i] == NULL)
            continue;

        if(x->screens[i]->shm) {
            if(vj_split_frame_is_bound_to_shm(x->screens[i], x->frames[i]))
                continue;
            if(vj_split_push_shm(x->screens[i], x->frames[i]) == 0)
                vj_split_free_screen(x, i);
        }
    }
}

VJFrame *vj_split_get_screen(void *ptr, int screen_id)
{
    vj_split_t *x = (vj_split_t*) ptr;
    if(x == NULL || screen_id < 0 || screen_id >= x->n_screens)
        return NULL;

    return x->frames[screen_id];
}
