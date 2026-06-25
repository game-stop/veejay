/* 
 * LIBVJE - veejay fx library
 *
 * Copyright(C)2011 Niels Elburg <nwelburg@gmail.com>
 * See COPYING for software license and distribution details
 */

/*
   simple generator plugin - reads 4:4:4 planar frames from veejay's shared
   memory video-wall resource.

   The split-screen producer always allocates enough storage for YUV 4:4:4 and
   always publishes a 4:4:4 tile.  This plugin intentionally exposes a single
   YUV444P output channel to avoid accidental 4:2:2/4:2:0 negotiation and UV
   plane remapping errors.
 */

#ifndef IS_LIVIDO_PLUGIN
#define IS_LIVIDO_PLUGIN
#endif

#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include "livido.h"
LIVIDO_PLUGIN
#include "utils.h"
#include "livido-utils.c"

#ifndef SHM_ADDR_OFFSET
#define SHM_ADDR_OFFSET 4096
#endif

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

typedef struct
{
    int                 resource_id;
    pthread_rwlock_t    rwlock;
    int                 header[8];
} vj_shared_data;

typedef struct
{
    unsigned char      *addr;
    int                 shm_key;
    int                 logged_w;
    int                 logged_h;
    int                 logged_palette;
} lvd_shmin_state_t;

static inline const char *lvd_palette_name(int palette)
{
    switch(palette) {
        case LIVIDO_PALETTE_YUV444P: return "YUV444P";
        case LIVIDO_PALETTE_YUV4444P: return "YUV4444P";
        case LIVIDO_PALETTE_YUV422P: return "YUV422P";
        case LIVIDO_PALETTE_YUV420P: return "YUV420P";
        case LIVIDO_PALETTE_YUVA8888: return "YUVA8888";
        default: return "unknown";
    }
}

static inline int lvd_is_444_palette(int palette)
{
    return palette == LIVIDO_PALETTE_YUV444P || palette == LIVIDO_PALETTE_YUV4444P;
}

static inline void lvd_copy_plane(uint8_t *dst, int dst_stride,
                                  const uint8_t *src, int src_stride,
                                  int w, int h)
{
    if(dst_stride == w && src_stride == w) {
        livido_memcpy(dst, src, (size_t) w * (size_t) h);
        return;
    }

    for(int y = 0; y < h; y++)
        livido_memcpy(dst + ((size_t)y * (size_t)dst_stride),
                      src + ((size_t)y * (size_t)src_stride),
                      (size_t)w);
}

static inline int lvd_join_path(char *dst, size_t dst_len, const char *dir, const char *file)
{
    size_t dir_len = dir ? strlen(dir) : 0;
    size_t file_len = file ? strlen(file) : 0;
    int needs_slash = (dir_len > 0 && dir[dir_len - 1] != '/');

    if(dst_len == 0 || dir_len == 0 || file_len == 0)
        return 0;

    if(dir_len + (size_t)needs_slash + file_len + 1 > dst_len)
        return 0;

    livido_memcpy(dst, dir, dir_len);
    size_t pos = dir_len;
    if(needs_slash)
        dst[pos++] = '/';
    livido_memcpy(dst + pos, file, file_len);
    dst[pos + file_len] = '\0';
    return 1;
}

static inline void lvd_uv_dims_for_palette(int palette, int w, int h, int *uw, int *uh)
{
    if(palette == LIVIDO_PALETTE_YUV420P ||
       palette == LIVIDO_PALETTE_YVU420P ||
       palette == LIVIDO_PALETTE_I420) {
        *uw = (w + 1) >> 1;
        *uh = (h + 1) >> 1;
        return;
    }

    if(palette == LIVIDO_PALETTE_YUV422P ||
       palette == LIVIDO_PALETTE_YV16) {
        *uw = (w + 1) >> 1;
        *uh = h;
        return;
    }

    *uw = w;
    *uh = h;
}

static inline int lvd_infer_uv_height_from_planes(uint8_t **p, int uv_stride)
{
    if(!p[1] || !p[2] || uv_stride <= 0)
        return 0;

    uintptr_t u = (uintptr_t)p[1];
    uintptr_t v = (uintptr_t)p[2];
    if(v <= u)
        return 0;

    uintptr_t delta = v - u;
    if((delta % (uintptr_t)uv_stride) != 0)
        return 0;

    return (int)(delta / (uintptr_t)uv_stride);
}

static inline int lvd_correct_palette_from_layout(int palette, int w, int h, uint8_t **p, int *strides)
{
    if(w <= 0 || h <= 0 || !strides)
        return palette;

    const int uv_stride = strides[1];
    if(uv_stride == w)
        return LIVIDO_PALETTE_YUV444P;

    if(uv_stride == ((w + 1) >> 1)) {
        int uv_h = lvd_infer_uv_height_from_planes(p, uv_stride);
        if(uv_h > 0 && uv_h <= ((h + 1) >> 1))
            return LIVIDO_PALETTE_YUV420P;
        return LIVIDO_PALETTE_YUV422P;
    }

    return palette;
}

static int lvd_extract_output_channel(livido_port_t *instance,
                                      int *w,
                                      int *h,
                                      uint8_t **pixel_data,
                                      int *palette,
                                      int *rowstrides)
{
    livido_port_t *c = NULL;
    int error = livido_property_get(instance, "out_channels", 0, &c);
    if(error != LIVIDO_NO_ERROR)
        return error;

    error = livido_property_get(c, "width", 0, w);
    if(error != LIVIDO_NO_ERROR)
        return error;
    error = livido_property_get(c, "height", 0, h);
    if(error != LIVIDO_NO_ERROR)
        return error;
    error = livido_property_get(c, "current_palette", 0, palette);
    if(error != LIVIDO_NO_ERROR)
        return error;

    for(int i = 0; i < 4; i++) {
        error = livido_property_get(c, "pixel_data", i, &(pixel_data[i]));
        if(error != LIVIDO_NO_ERROR)
            return error;
    }

    int uv_w = 0;
    int uv_h = 0;
    lvd_uv_dims_for_palette(*palette, *w, *h, &uv_w, &uv_h);
    (void)uv_h;

    rowstrides[0] = *w;
    rowstrides[1] = uv_w;
    rowstrides[2] = uv_w;
    rowstrides[3] = *w;

    for(int i = 0; i < 4; i++) {
        int rs = 0;
        if(livido_property_get(c, "rowstrides", i, &rs) == LIVIDO_NO_ERROR && rs > 0)
            rowstrides[i] = rs;
    }

    *palette = lvd_correct_palette_from_layout(*palette, *w, *h, pixel_data, rowstrides);
    return LIVIDO_NO_ERROR;
}

static void lvd_downsample_444_to_422(uint8_t *dst, int dst_stride,
                                      const uint8_t *src, int src_stride,
                                      int w, int h)
{
    const int dst_w = (w + 1) >> 1;
    for(int y = 0; y < h; y++) {
        uint8_t *d = dst + ((size_t)y * (size_t)dst_stride);
        const uint8_t *s = src + ((size_t)y * (size_t)src_stride);
        for(int x = 0; x < dst_w; x++) {
            int sx = x << 1;
            int sx1 = sx + 1;
            if(sx1 >= w)
                sx1 = sx;
            d[x] = (uint8_t)(((int)s[sx] + (int)s[sx1] + 1) >> 1);
        }
    }
}

static void lvd_downsample_444_to_420(uint8_t *dst, int dst_stride,
                                      const uint8_t *src, int src_stride,
                                      int w, int h)
{
    const int dst_w = (w + 1) >> 1;
    const int dst_h = (h + 1) >> 1;
    for(int y = 0; y < dst_h; y++) {
        uint8_t *d = dst + ((size_t)y * (size_t)dst_stride);
        int sy0 = y << 1;
        int sy1 = sy0 + 1;
        if(sy1 >= h)
            sy1 = sy0;
        const uint8_t *s0 = src + ((size_t)sy0 * (size_t)src_stride);
        const uint8_t *s1 = src + ((size_t)sy1 * (size_t)src_stride);
        for(int x = 0; x < dst_w; x++) {
            int sx0 = x << 1;
            int sx1 = sx0 + 1;
            if(sx1 >= w)
                sx1 = sx0;
            int sum = (int)s0[sx0] + (int)s0[sx1] + (int)s1[sx0] + (int)s1[sx1];
            d[x] = (uint8_t)((sum + 2) >> 2);
        }
    }
}

static int lvd_read_shmid_file(void)
{
    char *home = getenv("HOME");
    if(!home)
        return 0;

    char veejay_dir[PATH_MAX];
    char path[PATH_MAX];
    if(!lvd_join_path(veejay_dir, sizeof(veejay_dir), home, ".veejay"))
        return 0;
    if(!lvd_join_path(path, sizeof(path), veejay_dir, "veejay.shm"))
        return 0;

    int fd = open(path, O_RDWR);
    if(fd < 0)
        return 0;

    char buf[256];
    livido_memset(buf, 0, sizeof(buf));
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if(n <= 0)
        return 0;

    buf[n] = '\0';

    int shm_key = 0;
    if(sscanf(buf, "master: %d", &shm_key) != 1)
        return 0;

    return shm_key;
}

static int lvd_resolve_shmid(livido_port_t *my_instance)
{
    int shm_key = 0;
    char *env_id = getenv("VEEJAY_SHMID");

    if(livido_property_get(my_instance, "HOST_shmid", 0, &shm_key) == LIVIDO_NO_ERROR) {
        if(shm_key != 0)
            return shm_key;
    }

    if(env_id)
        return atoi(env_id);

    return lvd_read_shmid_file();
}

int init_instance(livido_port_t *my_instance)
{
    int shm_key = lvd_resolve_shmid(my_instance);
    if(shm_key == 0)
        return LIVIDO_ERROR_ENVIRONMENT;

    int r = shmget(shm_key, 0, 0400);
    if(r == -1) {
        printf("lvd_shmin: %s for shm key %d\n", strerror(errno), shm_key);
        return LIVIDO_ERROR_ENVIRONMENT;
    }

    char *ptr = (char*) shmat(r, NULL, 0);
    if(ptr == (char*) (-1)) {
        printf("lvd_shmin: %s attaching shm key %d\n", strerror(errno), shm_key);
        return LIVIDO_ERROR_RESOURCE;
    }

    vj_shared_data *data = (vj_shared_data*) ptr;
    if(data->header[0] <= 0 || data->header[1] <= 0) {
        shmdt(ptr);
        return LIVIDO_ERROR_RESOURCE;
    }

    int dst_w = 0;
    int dst_h = 0;
    lvd_extract_dimensions(my_instance, "out_channels", &dst_w, &dst_h);
    if(dst_w <= 0 || dst_h <= 0) {
        shmdt(ptr);
        return LIVIDO_ERROR_RESOURCE;
    }

    lvd_shmin_state_t *s = (lvd_shmin_state_t*) livido_malloc(sizeof(lvd_shmin_state_t));
    if(!s) {
        shmdt(ptr);
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

    livido_memset(s, 0, sizeof(*s));
    s->addr = (unsigned char*) ptr;
    s->shm_key = shm_key;

    int error = livido_property_set(my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR, 1, &s);
    if(error != LIVIDO_NO_ERROR) {
        shmdt(ptr);
        livido_free(s);
        return LIVIDO_ERROR_INTERNAL;
    }

    return LIVIDO_NO_ERROR;
}

int deinit_instance(livido_port_t *my_instance)
{
    lvd_shmin_state_t *s = NULL;
    int error = livido_property_get(my_instance, "PLUGIN_private", 0, &s);

    if(error == LIVIDO_NO_ERROR && s) {
        if(s->addr && shmdt(s->addr))
            printf("lvd_shmin: error detaching from shm\n");
        livido_free(s);
    }

    livido_property_set(my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR, 0, NULL);
    return LIVIDO_NO_ERROR;
}

int process_instance(livido_port_t *my_instance, double timecode)
{
    (void) timecode;

    uint8_t *O[4] = {NULL, NULL, NULL, NULL};
    int rowstrides[4] = {0, 0, 0, 0};
    int palette = LIVIDO_PALETTE_YUV422P;
    int w = 0;
    int h = 0;

    int error = lvd_extract_output_channel(my_instance, &w, &h, O, &palette, rowstrides);
    if(error != LIVIDO_NO_ERROR)
        return LIVIDO_ERROR_NO_OUTPUT_CHANNELS;

    if(!O[0] || !O[1] || !O[2] || rowstrides[0] < w || rowstrides[1] <= 0 || rowstrides[2] <= 0)
        return LIVIDO_ERROR_NO_OUTPUT_CHANNELS;

    lvd_shmin_state_t *s = NULL;
    error = livido_property_get(my_instance, "PLUGIN_private", 0, &s);
    if(error != LIVIDO_NO_ERROR || !s || !s->addr)
        return LIVIDO_ERROR_INTERNAL;

    vj_shared_data *v = (vj_shared_data*) s->addr;

    int res = pthread_rwlock_rdlock(&v->rwlock);
    if(res != 0)
        return LIVIDO_ERROR_RESOURCE;

    int src_w = v->header[0];
    int src_h = v->header[1];
    int src_y_stride = v->header[2];
    int src_u_stride = v->header[3];
    int src_v_stride = v->header[4];
    int src_palette = v->header[5];
    int src_uv_h = v->header[6] > 0 ? v->header[6] : src_h;

    if(src_w != w || src_h != h || src_uv_h != h ||
       src_y_stride < w || src_u_stride < w || src_v_stride < w ||
       !lvd_is_444_palette(src_palette)) {
        pthread_rwlock_unlock(&v->rwlock);
        return LIVIDO_ERROR_RESOURCE;
    }

    if(s->logged_w != src_w || s->logged_h != src_h || s->logged_palette != palette) {
        printf("lvd_shmin: SHM key=%d source=%dx%d %s -> output=%dx%d %s strides=%d/%d/%d\n",
               s->shm_key, src_w, src_h, lvd_palette_name(src_palette),
               w, h, lvd_palette_name(palette), rowstrides[0], rowstrides[1], rowstrides[2]);
        s->logged_w = src_w;
        s->logged_h = src_h;
        s->logged_palette = palette;
    }

    uint8_t *start_addr = s->addr + SHM_ADDR_OFFSET;
    uint8_t *Y = start_addr;
    uint8_t *U = Y + ((size_t)src_y_stride * (size_t)src_h);
    uint8_t *V = U + ((size_t)src_u_stride * (size_t)src_uv_h);

    lvd_copy_plane(O[0], rowstrides[0], Y, src_y_stride, w, h);

    if(palette == LIVIDO_PALETTE_YUV444P || palette == LIVIDO_PALETTE_YUV4444P) {
        lvd_copy_plane(O[1], rowstrides[1], U, src_u_stride, w, h);
        lvd_copy_plane(O[2], rowstrides[2], V, src_v_stride, w, h);
        if(O[3] && rowstrides[3] > 0)
            livido_memset(O[3], 0xff, (size_t)rowstrides[3] * (size_t)h);
    }
    else if(palette == LIVIDO_PALETTE_YUV420P ||
            palette == LIVIDO_PALETTE_YVU420P ||
            palette == LIVIDO_PALETTE_I420) {
        lvd_downsample_444_to_420(O[1], rowstrides[1], U, src_u_stride, w, h);
        lvd_downsample_444_to_420(O[2], rowstrides[2], V, src_v_stride, w, h);
    }
    else {
        lvd_downsample_444_to_422(O[1], rowstrides[1], U, src_u_stride, w, h);
        lvd_downsample_444_to_422(O[2], rowstrides[2], V, src_v_stride, w, h);
    }

    res = pthread_rwlock_unlock(&v->rwlock);
    if(res != 0)
        return LIVIDO_ERROR_RESOURCE;

    return LIVIDO_NO_ERROR;
}

livido_port_t *livido_setup(livido_setup_t list[], int version)
{
    (void) version;
    LIVIDO_IMPORT(list);

    livido_port_t *port = NULL;
    livido_port_t *info = NULL;
    livido_port_t *filter = NULL;
    livido_port_t *out_chans[1];

    info = livido_port_new(LIVIDO_PORT_TYPE_PLUGIN_INFO);
    port = info;

    livido_set_string_value(port, "maintainer", "Niels");
    livido_set_string_value(port, "version", "1");

    filter = livido_port_new(LIVIDO_PORT_TYPE_FILTER_CLASS);
    livido_set_int_value(filter, "api_version", LIVIDO_API_VERSION);

    livido_set_voidptr_value(filter, "deinit_func", &deinit_instance);
    livido_set_voidptr_value(filter, "init_func", &init_instance);
    livido_set_voidptr_value(filter, "process_func", &process_instance);
    port = filter;

    livido_set_string_value(port, "name", "Shared Memory Reader Veejay");
    livido_set_string_value(port, "description", "Read 4:4:4 planar frame from shared resource and write native output layout");
    livido_set_string_value(port, "author", "Niels Elburg");
    livido_set_int_value(port, "flags", 0);
    livido_set_string_value(port, "license", "GPL2");
    livido_set_int_value(port, "version", 1);
    livido_set_int_value(port, "HOST_shmid", 0);

    int palettes0[] = {
        LIVIDO_PALETTE_YUV420P,
        LIVIDO_PALETTE_YUV422P,
        LIVIDO_PALETTE_YUV444P,
        0
    };

    out_chans[0] = livido_port_new(LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE);
    port = out_chans[0];

    livido_set_string_value(port, "name", "Output Channel");
    livido_set_int_array(port, "palette_list", 3, palettes0);
    livido_set_int_value(port, "flags", 0);

    livido_set_portptr_array(filter, "in_parameter_templates", 0, NULL);
    livido_set_portptr_array(filter, "in_channel_templates", 0, NULL);
    livido_set_portptr_array(filter, "out_channel_templates", 1, out_chans);

    livido_set_portptr_value(info, "filters", filter);
    return info;
}
